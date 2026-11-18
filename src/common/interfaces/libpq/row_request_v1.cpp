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
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<Token> lex(const std::string& sql) {
    std::vector<Token> out;
    for (size_t i = 0; i < sql.size();) {
        const auto c = static_cast<unsigned char>(sql[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (c == '\'') {
            ++i; std::string value; bool closed = false;
            while (i < sql.size()) {
                if (sql[i] == '\'') {
                    if (i + 1 < sql.size() && sql[i + 1] == '\'') { value += '\''; i += 2; }
                    else { ++i; closed = true; break; }
                } else { value += sql[i++]; }
            }
            out.push_back({value, closed ? 's' : '!'});
        } else if (std::isalnum(c) || c == '_') {
            const size_t start = i++;
            while (i < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_')) ++i;
            out.push_back({sql.substr(start, i - start), 'w'});
        } else { out.push_back({std::string(1, sql[i++]), 'p'}); }
    }
    return out;
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
        if (i >= ts.size() || ts[i].kind != 's') fail(Code::invalid_input, "v1 field must be a SQL single-quoted string");
        return ts[i++].text;
    }
    std::string table() {
        if (i >= ts.size() || ts[i].kind != 'w') fail(Code::unsupported, "unquoted v1 table name required");
        auto name = ts[i++].text;
        if (i < ts.size() && ts[i].text == ".") { ++i; if (name != "public") fail(Code::unsupported, "public schema required");
            if (i >= ts.size() || ts[i].kind != 'w') fail(Code::invalid_input, "missing table name"); name = ts[i++].text; }
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
        const std::string text(sql); const auto tokens = lex(text);
        if (tokens.empty()) return {Code::ok, ""};
        const auto kind = lower(tokens.front().text);
        bool create = kind == "create";
        if (kind != "select" && kind != "insert" && !create) return {Code::ok, ""};
        size_t table_pos = 0;
        if (create || kind == "insert") table_pos = 2;
        else for (size_t i = 1; i < tokens.size(); ++i) if (tokens[i].kind == 'w' && lower(tokens[i].text) == "from") { table_pos = i + 1; break; }
        if (!table_pos || table_pos >= tokens.size() || tokens[table_pos].kind != 'w') return {Code::ok, ""};
        Parser locator{tokens}; locator.i = table_pos; const auto table = locator.table();
        const bool has_sse = std::any_of(tokens.begin(), tokens.end(), [](const Token& t) { return t.kind == 'w' && lower(t.text) == "sse"; });
        checked(set_catalog_reader([conn](const std::string& name, Catalog& c) { return read_catalog(conn, name, c); }));
        Scope scope; bool registered = false; checked(local_scope(table, scope, registered));
        if (create ? !has_sse : !registered) return {Code::ok, ""};
        handled = true;
        if (text.size() > 6 * 1024 * 1024 || text.find_first_of("\r\n") != std::string::npos)
            fail(Code::invalid_input, "v1 SQL exceeds size limit or contains a line break");
        if (PQtransactionStatus(conn) != PQTRANS_IDLE) fail(Code::unsupported, "v1 operations require an idle connection");
        Parser p{tokens}; std::vector<Row> input; std::string keyword;
        if (create) {
            p.word("create"); p.word("table"); p.table(); p.punct("("); p.word("ind"); p.word("text"); p.punct(",");
            p.word("keyword"); p.word("text"); p.word("sse"); p.punct("=");
            if (lower(p.literal()) != "fides") fail(Code::unsupported, "only Fides v1 is supported");
            p.punct(","); p.word("payload"); p.word("text"); p.punct(")"); p.end();
        } else if (kind == "insert") {
            p.word("insert"); p.word("into"); p.table(); p.word("values");
            do {
                if (!input.empty()) p.punct(","); p.punct("("); Row row;
                row.ind = p.literal(); p.punct(","); row.keyword = p.literal(); p.punct(","); row.payload = p.literal(); p.punct(")");
                if (input.size() >= kMaxBatchRows) fail(Code::invalid_input, "batch row limit exceeded");
                input.push_back(row);
            } while (p.i < tokens.size() && tokens[p.i].text == ",");
            p.end();
        } else {
            p.word("select"); p.punct("*"); p.word("from"); p.table(); p.word("where"); p.word("keyword"); p.punct("="); keyword = p.literal(); p.end();
        }
        KeySet keys; const char* state = std::getenv("SSE_V1_STATE_DIR");
        checked(load_keys(state ? state : "/opt/og/openGauss-server/sse_storage", keys)); event("keys_loaded");
        Strings rids, blobs, positions, uts;
        for (const auto& row : input) {
            std::string rid, blob, position; checked(new_rid(rid)); checked(encrypt_row(scope,rid,row,keys,blob));
            checked(encrypt_locator(scope,{rid,0},keys,position)); checked(preflight(scope,row.keyword).status);
            rids.push_back(rid); blobs.push_back(blob); positions.push_back(position);
        }
        if (!create && kind == "select") {
            const auto before = preflight(scope, keyword); checked(before.status); event("preflight");
            if (!before.keyword_registered) { output = row_result(conn, {}).release(); event("unknown_empty"); return {Code::ok,""}; }
        }
        Operation op; checked(begin_operation(scope, create ? "create" : (kind == "insert" ? "insert" : "search"), op)); event("begin_durable");
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
        if (kind == "insert") {
            for (const auto& row : input) { uts.push_back(client->insertion_request(row.keyword, "", false).token); event("counter_advanced"); }
            event("insert_sent"); query(conn,"SELECT public.sse_v1_insert('public',$1,$2::text[],$3::text[],$4::text[],$5::text[])",
                  {table,array(rids),array(blobs),array(uts),array(positions)});
            Counters counters; checked(operation_counters(op,counters)); checked(save_counters(op,counters)); event("state_saved");
            Result result(PQmakeEmptyPGresult(conn,PGRES_COMMAND_OK),PQclear);
            if (!result) fail(Code::state_invalid,"allocate insert result failed");
            checked(complete_operation(op)); event("complete_durable"); output=result.release(); return {Code::ok,""};
        }
        const auto request=client->search_request(keyword); event("token_generated");
        if (request.token.empty() || request.derivation_key.empty() || !request.add_count || request.add_count>kMaxSearchAddCount)
            fail(Code::protocol_invalid,"registered keyword produced invalid or empty search token");
        const auto allowed_tokens=expected_tokens(request,*client);
        setup(conn,client.get());
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
        Counters counters; checked(operation_counters(op,counters)); checked(save_counters(op,counters)); event("state_saved");
        checked(complete_operation(op)); event("complete_durable"); output=result.release(); return {Code::ok,""};
    } catch (const Failure& e) { PQclear(output); output=nullptr; return e.status; }
      catch (const std::exception& e) { PQclear(output); output=nullptr; return {Code::state_invalid,e.what()}; }
}
}
