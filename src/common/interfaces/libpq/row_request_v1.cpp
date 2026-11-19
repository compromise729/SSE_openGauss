#include "libpq/row_request_v1.h"
#include "libpq/row_state_catalog.h"
#include "libpq/Client_Sophos.h"
#include "sse_v1_search_limits.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

extern "C" void sse_v1_request_event(const char*) __attribute__((weak));
namespace sse_v1 {
namespace {
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
using Strings = std::vector<std::string>;
struct Failure { Status status; };
void fail(Code c, const char* message) { throw Failure{{c, message}}; }
void checked(const Status& s) { if (!s.ok()) throw Failure{s}; }
void event(const char* phase) { if (sse_v1_request_event) sse_v1_request_event(phase); }
struct Token { std::string text; char kind; };
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
bool identifier_start(unsigned char c) {
    return c >= 128 || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool word(const Token& token, const char* value) { return token.kind == 'w' && lower(token.text) == value; }
bool punct(const Token& token, const char* value) { return token.kind == 'p' && token.text == value; }

// Keep quoted SQL text opaque for routing. The strict grammar below accepts
// only ordinary strings and unquoted identifiers for protected requests.
std::vector<Token> lex(const std::string& sql, bool standard_strings) {
    std::vector<Token> out;
    for (size_t i = 0; i < sql.size();) {
        const auto c = static_cast<unsigned char>(sql[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (sql.compare(i, 2, "--") == 0) {
            i = sql.find_first_of("\r\n", i + 2);
            continue;
        }
        if (sql.compare(i, 2, "/*") == 0) {
            i += 2;
            size_t depth = 1;
            while (i < sql.size() && depth) {
                if (sql.compare(i, 2, "/*") == 0) { ++depth; i += 2; }
                else if (sql.compare(i, 2, "*/") == 0) { --depth; i += 2; }
                else ++i;
            }
            if (depth) out.push_back({"", '!'});
            continue;
        }
        if (c == '$') {
            size_t end = i + 1;
            if (end < sql.size() && identifier_start(sql[end])) {
                while (end < sql.size() && (identifier_start(sql[end]) || digit(sql[end]))) ++end;
            }
            if (end < sql.size() && sql[end] == '$') {
                const auto tag = sql.substr(i, end - i + 1);
                const auto close = sql.find(tag, end + 1);
                i = close == std::string::npos ? sql.size() : close + tag.size();
                out.push_back({"", '$'});
                continue;
            }
        }
        const bool escape = (c == 'e' || c == 'E') && sql.compare(i + 1, 1, "'") == 0;
        const bool unicode = (c == 'u' || c == 'U') && i + 2 < sql.size() && sql[i + 1] == '&' &&
            (sql[i + 2] == '"' || sql[i + 2] == '\'');
        if (c == '\'' || c == '"' || escape || unicode) {
            i += unicode ? 2 : (escape ? 1 : 0);
            const char quote = sql[i++];
            char kind = quote == '"' ? (unicode ? 'u' : 'q') : (escape || unicode ? 'e' : 's');
            std::string value;
            bool closed = false;
            while (i < sql.size()) {
                if (sql[i] == quote) {
                    if (i + 1 < sql.size() && sql[i + 1] == quote) { value += quote; i += 2; }
                    else { ++i; closed = true; break; }
                } else if (sql[i] == '\\' && quote == '\'' && !unicode && (escape || !standard_strings)) {
                    kind = 'e';
                    value += sql[i++];
                    if (i < sql.size()) value += sql[i++];
                } else value += sql[i++];
            }
            out.push_back({value, closed ? kind : '!'});
        } else if (identifier_start(c) || digit(c)) {
            const size_t start = i++;
            while (i < sql.size() && (identifier_start(sql[i]) || digit(sql[i]) || sql[i] == '$')) ++i;
            out.push_back({sql.substr(start, i - start), 'w'});
        } else { out.push_back({std::string(1, sql[i++]), 'p'}); }
    }
    return out;
}
std::string name(const std::vector<Token>& tokens, size_t& i) {
    const Token& token = tokens[i++];
    if (token.kind == 'w') return lower(token.text);
    if (token.kind == 'q') return token.text;
    if (token.kind != 'u') return {};
    char escape = '\\';
    if (i + 1 < tokens.size() && word(tokens[i], "uescape") && tokens[i + 1].kind == 's') {
        if (tokens[i + 1].text.size() != 1) return {};
        escape = tokens[i + 1].text[0];
        i += 2;
    }
    std::string decoded;
    for (size_t pos = 0; pos < token.text.size();) {
        const char c = token.text[pos++];
        if (c != escape) { decoded += c; continue; }
        if (pos < token.text.size() && token.text[pos] == escape) { decoded += escape; ++pos; continue; }
        size_t digits = 4;
        if (pos < token.text.size() && token.text[pos] == '+') { ++pos; digits = 6; }
        if (token.text.size() - pos < digits) return {};
        unsigned value = 0;
        for (size_t j = 0; j < digits; ++j) {
            const char h = token.text[pos++];
            if (!std::isxdigit(static_cast<unsigned char>(h))) return {};
            value = value * 16 + (digit(h) ? h - '0' : (h | 32) - 'a' + 10);
        }
        // The registered name and its schema contain ASCII only.
        if (value == 0 || value >= 128) return {};
        decoded += static_cast<char>(value);
    }
    return decoded;
}
bool relation_at(const std::vector<Token>& tokens, size_t i, const std::string& table) {
    while (i < tokens.size() && (word(tokens[i], "only") || word(tokens[i], "lateral") ||
           word(tokens[i], "if") || word(tokens[i], "not") || word(tokens[i], "exists") ||
           word(tokens[i], "table") || punct(tokens[i], "("))) ++i;
    if (i == tokens.size()) return false;
    const auto first = name(tokens, i);
    if (i == tokens.size() || !punct(tokens[i], ".")) return first == table;
    if (++i == tokens.size()) return false;
    const auto second = name(tokens, i);
    if (i == tokens.size() || !punct(tokens[i], ".")) return first == "public" && second == table;
    // SQL also accepts database.schema.relation qualification in some modes.
    return ++i < tokens.size() && second == "public" && name(tokens, i) == table;
}
bool references_table(const std::vector<Token>& tokens, const std::string& table) {
    if (table.empty()) return false;
    std::vector<bool> relation_list(1, false);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& t = tokens[i];
        if (punct(t, "(")) { relation_list.push_back(false); continue; }
        if (punct(t, ")")) { if (relation_list.size() > 1) relation_list.pop_back(); continue; }
        if (punct(t, ";") || word(t, "where") || word(t, "group") || word(t, "having") ||
            word(t, "order") || word(t, "limit") || word(t, "returning") || word(t, "union") ||
            word(t, "intersect") || word(t, "except") || word(t, "set") || word(t, "values"))
            relation_list.back() = false;
        const bool from = word(t, "from") || word(t, "using");
        const bool names = word(t, "table") || word(t, "truncate") || word(t, "lock");
        if (from || names) relation_list.back() = true;
        if ((from || names || word(t, "join") || word(t, "into") || word(t, "update") ||
             word(t, "copy") || word(t, "references") || (relation_list.back() && punct(t, ","))) &&
            relation_at(tokens, i + 1, table)) return true;
    }
    return false;
}
bool version_option(const std::vector<Token>& tokens) {
    size_t depth = 0;
    bool create_table = false;
    size_t options = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& t = tokens[i];
        if (!depth && punct(t, ";")) create_table = false;
        if (!depth && word(t, "create")) {
            size_t next = i + 1;
            while (next < tokens.size() && (word(tokens[next], "global") || word(tokens[next], "local") ||
                   word(tokens[next], "temporary") || word(tokens[next], "temp") || word(tokens[next], "unlogged"))) ++next;
            create_table = next < tokens.size() && word(tokens[next], "table");
        }
        if (create_table && !depth && word(t, "with") && i + 1 < tokens.size() && punct(tokens[i + 1], "("))
            options = i + 2;
        if (options && i >= options && depth == 1 &&
            (i == options || punct(tokens[i - 1], ","))) {
            size_t j = i;
            if (name(tokens, j) == "sse_storage") return true;
        }
        if (punct(t, "(")) ++depth;
        if (punct(t, ")") && depth && !--depth) options = 0;
    }
    return false;
}
struct Parser {
    const std::vector<Token>& ts; size_t i = 0;
    explicit Parser(const std::vector<Token>& value) : ts(value) {}
    void word(const char* w) {
        if (i >= ts.size() || ts[i].kind != 'w' || lower(ts[i++].text) != w) fail(Code::unsupported, "unsupported v1 SQL form");
    }
    void punct(const char* p) {
        if (i >= ts.size() || ts[i].kind != 'p' || ts[i++].text != p) fail(Code::unsupported, "unsupported v1 SQL form");
    }
    std::string literal() {
        if (i < ts.size() && ts[i].kind == '!') fail(Code::invalid_input, "unterminated v1 SQL string");
        if (i >= ts.size() || ts[i].kind != 's') fail(Code::unsupported, "v1 field must be a SQL single-quoted string");
        const auto& value = ts[i++].text;
        if (value.find_first_of("\r\n") != std::string::npos)
            fail(Code::invalid_input, "v1 SQL strings must not contain a line break");
        return value;
    }
    std::string table() {
        if (i >= ts.size() || ts[i].kind != 'w') fail(Code::unsupported, "unquoted v1 table name required");
        auto name = ts[i++].text;
        if (i < ts.size() && sse_v1::punct(ts[i], ".")) { ++i; if (name != "public") fail(Code::unsupported, "public schema required");
            if (i >= ts.size() || ts[i].kind != 'w') fail(Code::unsupported, "unquoted v1 table name required");
            name = ts[i++].text;
        }
        if (name.empty() || name.size() > 40 || name[0] < 'a' || name[0] > 'z' ||
            !std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || digit(c) || c == '_'; }))
            fail(Code::unsupported, "lowercase v1 table identifier required");
        return name;
    }
    void end() {
        if (i < ts.size() && ts[i].kind == 'p' && ts[i].text == ";") ++i;
        if (i != ts.size()) fail(Code::unsupported, "single simple v1 statement required");
    }
};
Result query(PGconn* conn, const std::string& sql, const Strings& args = {}) {
    std::vector<const char*> values;
    for (const auto& s : args) values.push_back(s.c_str());
    Result r(PQexecParams(conn, sql.c_str(), values.size(), nullptr, values.data(), nullptr, nullptr, 0), PQclear);
    if (!r || (PQresultStatus(r.get()) != PGRES_COMMAND_OK && PQresultStatus(r.get()) != PGRES_TUPLES_OK)) {
        const char* state = r ? PQresultErrorField(r.get(), PG_DIAG_SQLSTATE) : nullptr;
        fail(state && !strcmp(state, "21000") ? Code::inconsistent_result : Code::db_failed,
             r ? PQresultErrorMessage(r.get()) : PQerrorMessage(conn));
    }
    return r;
}
class SearchTransaction {
public:
    explicit SearchTransaction(PGconn* conn) : conn_(conn) { query(conn_, "BEGIN"); }
    ~SearchTransaction() {
        if (!finished_ && PQtransactionStatus(conn_) != PQTRANS_IDLE)
            PQclear(PQexec(conn_, "ROLLBACK"));
    }
    void finish(const char* command) {
        const auto result = query(conn_, command);
        if (std::strcmp(PQcmdStatus(result.get()), command) != 0 || PQtransactionStatus(conn_) != PQTRANS_IDLE)
            fail(Code::db_failed, "could not confirm v1 search transaction outcome");
        finished_ = true;
    }
private:
    PGconn* conn_;
    bool finished_ = false;
    SearchTransaction(const SearchTransaction&) = delete;
    SearchTransaction& operator=(const SearchTransaction&) = delete;
};

int counter_for(const Counters& counters, const std::string& keyword) {
    const auto found = counters.find(keyword);
    return found == counters.end() ? 0 : found->second;
}
Status check_budget(int current, size_t additions) {
    if (current < 0 || static_cast<unsigned>(current) > kMaxSearchAddCount)
        return {Code::state_invalid, "Fides v1 keyword counter exceeds the protocol limit"};
    if (additions > kMaxSearchAddCount - static_cast<unsigned>(current))
        return {Code::unsupported, "Fides v1 keyword update budget exhausted (maximum " +
            std::to_string(kMaxSearchAddCount) + ")"};
    return {Code::ok, ""};
}
std::string field(const Result& r, int row, int col) {
    if (PQgetisnull(r.get(), row, col)) fail(Code::protocol_invalid, "NULL in v1 result");
    return std::string(PQgetvalue(r.get(), row, col), PQgetlength(r.get(), row, col));
}
std::string array(const Strings& values) {
    std::string out = "{";
    for (const auto& value : values) {
        if (out.size() > 1) out += ','; out += '"';
        for (char c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; }
        out += '"';
    }
    return out + '}';
}
Result row_result(PGconn* conn, const std::vector<Row>& rows) {
    Result r(PQmakeEmptyPGresult(conn, PGRES_TUPLES_OK), PQclear);
    if (!r) fail(Code::state_invalid, "allocate v1 result failed");
    char a[] = "ind", b[] = "keyword", c[] = "payload";
    PGresAttDesc attrs[3] = {};
    char* names[] = {a,b,c};
    for (int j = 0; j < 3; ++j) { attrs[j].name = names[j]; attrs[j].typid = 25; attrs[j].typlen = -1; attrs[j].atttypmod = -1; }
    if (!PQsetResultAttrs(r.get(), 3, attrs)) fail(Code::state_invalid, "allocate v1 columns failed");
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string* fields[] = {&rows[i].ind,&rows[i].keyword,&rows[i].payload};
        for (int j = 0; j < 3; ++j)
            if (!PQsetvalue(r.get(), i, j, const_cast<char*>(fields[j]->data()), fields[j]->size()))
                fail(Code::state_invalid, "allocate v1 row failed");
    }
    return r;
}
std::unique_ptr<sse::sophos::SophosClient> fides_client() {
    const auto root = sse::sophos::storage_dir_path;
    const char* files[] = {"tdp_sk.key", "derivation_master.key", "rsa_prg.key"};
    for (unsigned i = 0; i < 3; ++i) {
        struct stat st; const auto path = root + "/" + files[i];
        if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 0777) != 0600 || (i == 0 ? st.st_size == 0 : st.st_size != 32))
            fail(Code::key_invalid, "missing or invalid existing Fides key; initialization is explicit only");
    }
    try { return std::unique_ptr<sse::sophos::SophosClient>(sse::sophos::init_SophosClient()); }
    catch (const std::exception& e) { fail(Code::key_invalid, e.what()); }
    return nullptr;
}
void setup(PGconn* conn, sse::sophos::SophosClient* client) {
    // Use the legacy setup wire form directly, avoiding any automatic key path.
    auto* previous = Sophos_client; Sophos_client = client;
    Result r(PQexec(conn, "fides_setup;"), PQclear);
    Sophos_client = previous;
    if (!r || PQresultStatus(r.get()) != PGRES_TUPLES_OK) fail(Code::db_failed, "Fides setup failed");
    event("fides_setup");
}
std::set<std::string> expected_tokens(const sse::sophos::new_SearchRequest& request,
                                     sse::sophos::SophosClient& client) {
    using namespace sse::sophos;
    const auto token=base64_decode(request.token), key=base64_decode(request.derivation_key);
    if (token.size()!=kSearchTokenSize || key.size()!=kDerivationKeySize ||
        base64_encode(token)!=request.token || base64_encode(key)!=request.derivation_key)
        fail(Code::protocol_invalid,"invalid generated Fides token encoding");
    search_token_type st; std::copy(token.begin(),token.end(),st.begin());
    std::array<unsigned char,kDerivationKeySize> key_bytes;
    std::copy(key.begin(),key.end(),key_bytes.begin());
    sse::crypto::Prf<kUpdateTokenSize> prf(sse::crypto::Key<kDerivationKeySize>(key_bytes.data()));
    sse::crypto::Tdp public_tdp(client.public_key()); std::set<std::string> result;
    for (unsigned i=0;i<request.add_count;++i) {
        update_token_type ut; std::array<unsigned char,kUpdateTokenSize> mask;
        gen_update_token_masks(prf,st.data(),ut,mask);
        result.insert(base64_encode(std::string(ut.begin(),ut.end())));
        st=public_tdp.eval(st);
    }
    return result;
}
}

Status execute_request(PGconn* conn, const char* sql, bool& handled, PGresult*& output) {
    handled = false; output = nullptr;
    try {
        if (!sql || !conn || PQstatus(conn) != CONNECTION_OK) fail(Code::db_failed, "connected v1 client required");
        const std::string text(sql);
        const char* standard_strings = PQparameterStatus(conn, "standard_conforming_strings");
        const auto tokens = lex(text, !standard_strings || std::strcmp(standard_strings, "off") != 0);
        if (tokens.empty()) return {Code::ok, ""};
        const bool marked = version_option(tokens);
        if (!v1_state_selected()) {
            if (marked) fail(Code::unsupported, "explicit v1 state is required for sse_storage");
            return {Code::ok, ""};
        }
        std::string registered;
        checked(registered_table(registered));
        if (!marked && !references_table(tokens, registered)) return {Code::ok, ""};
        handled = true;
        const bool create = marked && word(tokens.front(), "create");
        const bool insert = !marked && word(tokens.front(), "insert");
        const bool select = !marked && word(tokens.front(), "select");
        if (!create && !insert && !select) fail(Code::unsupported, "unsupported v1 SQL statement");
        if (text.size() > 6 * 1024 * 1024) fail(Code::invalid_input, "v1 SQL exceeds size limit");
        if (PQtransactionStatus(conn) != PQTRANS_IDLE) fail(Code::unsupported, "v1 operations require an idle connection");
        Parser p{tokens}; std::vector<Row> input; std::string keyword, table;
        if (create) {
            p.word("create"); p.word("table"); table = p.table(); p.punct("("); p.word("ind"); p.word("text"); p.punct(",");
            p.word("keyword"); p.word("text"); p.word("sse"); p.punct("=");
            if (lower(p.literal()) != "fides") fail(Code::unsupported, "only Fides v1 is supported");
            p.punct(","); p.word("payload"); p.word("text"); p.punct(")");
            p.word("with"); p.punct("("); p.word("sse_storage"); p.punct("=");
            if (p.literal() != "ROW_AEAD_V1") fail(Code::unsupported, "unsupported sse_storage version");
            p.punct(")"); p.end();
        } else if (insert) {
            p.word("insert"); p.word("into"); table = p.table(); p.word("values");
            do {
                if (!input.empty()) p.punct(","); p.punct("("); Row row;
                row.ind = p.literal(); p.punct(","); row.keyword = p.literal(); p.punct(","); row.payload = p.literal(); p.punct(")");
                if (input.size() >= kMaxBatchRows) fail(Code::invalid_input, "batch row limit exceeded");
                input.push_back(row);
            } while (p.i < tokens.size() && punct(tokens[p.i], ","));
            p.end();
        } else {
            p.word("select"); p.punct("*"); p.word("from"); table = p.table(); p.word("where"); p.word("keyword"); p.punct("="); keyword = p.literal(); p.end();
        }
        if (!create && table != registered) fail(Code::unsupported, "single registered v1 table required");
        Scope scope;
        bool table_registered;
        checked(local_scope(table, scope, table_registered));
        if (!create && !table_registered) fail(Code::state_invalid, "local v1 registration changed");
        KeySet keys; const char* state = std::getenv("SSE_V1_STATE_DIR");
        checked(load_keys(state ? state : "/opt/og/openGauss-server/sse_storage", keys)); event("keys_loaded");
        Strings rids, blobs, positions, uts;
        std::map<std::string, size_t> additions;
        for (const auto& row : input) {
            std::string rid, blob, position; checked(new_rid(rid)); checked(encrypt_row(scope,rid,row,keys,blob));
            checked(encrypt_locator(scope,{rid,0},keys,position));
            rids.push_back(rid); blobs.push_back(blob); positions.push_back(position);
            ++additions[row.keyword];
        }
        checked(set_catalog_reader([conn](const std::string& target, Catalog& c) { return read_catalog(conn, target, c); }));
        if (select) {
            const auto before = preflight(scope, keyword); checked(before.status); event("preflight");
            if (!before.keyword_registered) { output = row_result(conn, {}).release(); event("unknown_empty"); return {Code::ok,""}; }
        }
        int search_count = 0;
        const auto check_counters = [&](const Counters& counters) -> Status {
            if (select) {
                search_count = counter_for(counters, keyword);
                if (search_count == 0) return {Code::protocol_invalid, "registered keyword has no updates"};
                return check_budget(search_count, 1);
            }
            for (const auto& entry : additions) {
                const auto status = check_budget(counter_for(counters, entry.first), entry.second);
                if (!status.ok()) return status;
            }
            return {Code::ok, ""};
        };
        Operation op;
        checked(begin_checked_operation(scope, create ? "create" : (insert ? "insert" : "search"), op, check_counters));
        event("begin_durable");
        if (create) {
            event("create_sent"); query(conn,"SELECT public.sse_v1_create('public',$1)",{table});
            checked(register_table(scope,kProtocolVersion)); event("state_saved");
            Result result(PQmakeEmptyPGresult(conn,PGRES_COMMAND_OK),PQclear);
            if (!result) fail(Code::state_invalid,"allocate create result failed");
            checked(complete_operation(op)); event("complete_durable"); output=result.release(); return {Code::ok,""};
        }
        // Construct while holding the operation lock, so search uses the latest
        // counters read from SSE_V1_STATE_DIR rather than a stale object cache.
        auto client=fides_client();
        if (insert) {
            for (const auto& row : input) { uts.push_back(client->insertion_request(row.keyword, "", false).token); event("counter_advanced"); }
            event("insert_sent"); query(conn,"SELECT public.sse_v1_insert('public',$1,$2::text[],$3::text[],$4::text[],$5::text[])",
                  {table,array(rids),array(blobs),array(uts),array(positions)});
            Counters counters; checked(operation_counters(op,counters)); checked(save_counters(op,counters)); event("state_saved");
            Result result(PQmakeEmptyPGresult(conn,PGRES_COMMAND_OK),PQclear);
            if (!result) fail(Code::state_invalid,"allocate insert result failed");
            checked(complete_operation(op)); event("complete_durable"); output=result.release(); return {Code::ok,""};
        }
        const auto request=client->search_request(keyword); event("token_generated");
        if (request.token.empty() || request.derivation_key.empty() || request.add_count != static_cast<unsigned>(search_count))
            fail(Code::protocol_invalid,"registered keyword produced invalid or empty search token");
        const auto allowed_tokens=expected_tokens(request,*client);
        setup(conn,client.get());
        SearchTransaction transaction(conn);
        event("search_take_sent");
        Result hits(PQexec(conn,("fides_v1_select * from public."+table+"_index where token="+request.token+
            " and derivation_key="+request.derivation_key+" and add_count="+std::to_string(request.add_count)+";").c_str()),PQclear);
        if (!hits || PQresultStatus(hits.get())!=PGRES_TUPLES_OK) fail(Code::db_failed,hits?PQresultErrorMessage(hits.get()):PQerrorMessage(conn));
        if (PQnfields(hits.get())!=2 || strcmp(PQfname(hits.get(),0),"ut") || strcmp(PQfname(hits.get(),1),"locator_blob") ||
            PQntuples(hits.get())>static_cast<int>(request.add_count)) fail(Code::protocol_invalid,"invalid Fides result shape/count");
        if (PQntuples(hits.get()) == 0) fail(Code::inconsistent_result,"registered insert-only keyword has no index hits");
        std::set<std::string> unique_rids, unique_uts;
        for (int i=0;i<PQntuples(hits.get());++i) {
            const auto ut=field(hits,i,0);
            if (!allowed_tokens.count(ut) || !unique_uts.insert(ut).second) fail(Code::protocol_invalid,"unexpected or repeated update token");
            Locator decoded; checked(decrypt_locator(scope,field(hits,i,1),keys,decoded));
            if (decoded.operation!=0) fail(Code::unsupported,"only insert locators supported");
            if (unique_rids.insert(decoded.rid).second) rids.push_back(decoded.rid);
        }
        const auto capacity = check_budget(search_count, rids.size());
        if (!capacity.ok()) {
            transaction.finish("ROLLBACK");
            checked(cancel_unmodified_search(op));
            event("budget_rollback_durable");
            checked(capacity);
        }
        for (const auto& rid : rids) {
            uts.push_back(client->insertion_request(keyword,"",false).token); event("counter_advanced");
            std::string blob; checked(encrypt_locator(scope,{rid,0},keys,blob)); positions.push_back(blob);
        }
        event("reinsert_sent"); query(conn,"SELECT public.sse_v1_reinsert('public',$1,$2::text[],$3::text[])",{table,array(uts),array(positions)});
        std::vector<Row> rows;
        if (!rids.empty()) {
            event("fetch_sent"); auto data=query(conn,"SELECT rid,data_blob FROM public.sse_v1_fetch('public',$1,$2::text[])",{table,array(rids)});
            if (PQnfields(data.get())!=2 || PQntuples(data.get())!=static_cast<int>(rids.size())) fail(Code::inconsistent_result,"wrong fetched row count");
            for (int i=0;i<PQntuples(data.get());++i) {
                const auto rid=field(data,i,0);
                if (unique_rids.erase(rid)!=1) fail(Code::inconsistent_result,"unexpected or repeated fetched rid");
                Row row; checked(decrypt_row(scope,rid,field(data,i,1),keys,row));
                if (row.keyword!=keyword) fail(Code::inconsistent_result,"authenticated row keyword mismatch"); rows.push_back(row);
            }
            if (!unique_rids.empty()) fail(Code::inconsistent_result,"missing fetched rid");
        }
        auto result=row_result(conn,rows); event("rows_authenticated");
        transaction.finish("COMMIT"); event("search_commit");
        Counters counters; checked(operation_counters(op,counters)); checked(save_counters(op,counters)); event("state_saved");
        checked(complete_operation(op)); event("complete_durable"); output=result.release(); return {Code::ok,""};
    } catch (const Failure& e) { PQclear(output); output=nullptr; return e.status; }
      catch (const std::exception& e) { PQclear(output); output=nullptr; return {Code::state_invalid,e.what()}; }
}
}
