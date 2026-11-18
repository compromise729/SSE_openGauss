#include "libpq/row_state_v1.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Catalog and network effects below are explicit fixtures. State persistence
// and all row/locator crypto calls use the actual checked-out implementations.
using namespace sse_v1;
namespace {
bool fail_marker = false;
std::vector<std::string> writes, events;
Catalog server;
int sends = 0, index_rows = 2, displays = 0;
void require(bool yes, const char* what) { if (!yes) throw std::runtime_error(what); }
void expect(const Status& s, Code code) {
    if (s.code != code) throw std::runtime_error(std::string(code_name(s.code)) + ": " + s.message);
}
void ok(const Status& s) { expect(s, Code::ok); }
bool exists(const std::string& path) { struct stat st; return lstat(path.c_str(), &st) == 0; }
std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    require(static_cast<bool>(f), "read private fixture");
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void write(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(f.write(bytes.data(), bytes.size())), "write fixture");
    f.close(); require(static_cast<bool>(f), "close fixture");
    require(chmod(path.c_str(), 0600) == 0, "fixture permissions");
}
// Compare secret bytes only in process memory. Never print key values/digests.
std::map<std::string, std::string> snapshot(const std::string& dir) {
    std::map<std::string, std::string> out;
    DIR* d = opendir(dir.c_str()); require(d != nullptr, "list fixture");
    while (const auto* e = readdir(d)) {
        const std::string name(e->d_name), path = dir + "/" + name;
        struct stat st; require(lstat(path.c_str(), &st) == 0, "stat fixture");
        if (S_ISREG(st.st_mode)) out[name] = std::to_string(st.st_mode) + ":" + read(path);
    }
    require(closedir(d) == 0, "close fixture listing");
    return out;
}
void configure() {
    server = Catalog(); server.database_oid = "14177";
    server.instance_directory = "/fixture/state-crypto/instance";
    ok(set_catalog_reader([](const std::string&, Catalog& c) { c = server; return Status{Code::ok, ""}; }));
}
Scope scope_for_state() {
    Scope scope; bool registered = false;
    ok(local_scope("week_demo", scope, registered)); return scope;
}
Status real_initializer(const std::string& stage, KeySet& keys) {
    const std::string parent = stage.substr(0, stage.rfind('/'));
    struct stat st; require(stat(stage.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700, "private stage");
    DIR* d = opendir(stage.c_str()); require(d != nullptr, "open stage");
    int entries = 0;
    while (const auto* e = readdir(d)) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++entries;
    require(closedir(d) == 0 && entries == 0, "real initializer receives empty directory");
    require(!exists(parent + "/environment.v1"), "environment must be published last");
    const pid_t child = fork(); require(child >= 0, "fork initialization contender");
    if (child == 0) {
        const int fd = open((parent + "/operation.lock").c_str(), O_RDWR);
        const bool busy = fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0 &&
            (errno == EWOULDBLOCK || errno == EAGAIN);
        if (fd >= 0) close(fd);
        _exit(busy ? 0 : 1);
    }
    int result; require(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0,
                        "parent state lock held during real initialization");
    return initialize_keys(stage, keys);
}
void seed(const std::string& dir) {
    require(mkdir(dir.c_str(), 0700) == 0, "create new private case");
    require(setenv("SSE_V1_STATE_DIR", dir.c_str(), 1) == 0, "select state");
    configure(); writes.clear();
    ok(initialize_state("14177", real_initializer));
    require(!writes.empty() && writes.back() == "environment.v1", "environment saved last");
    Scope scope = scope_for_state(); Operation op;
    ok(begin_operation(scope, "create", op));
    server.tables.emplace("week_demo", CatalogTable{1, true}); server.candidate_exists = true;
    ok(register_table(scope, 1)); ok(complete_operation(op));
    ok(begin_operation(scope, "insert", op)); ok(save_counters(op, {{"apple", 2}})); ok(complete_operation(op));
    KeySet keys; ok(load_keys(dir, keys));
    std::string rid, row, locator; ok(new_rid(rid));
    ok(encrypt_row(scope, rid, {"1", "apple", "public-payload"}, keys, row));
    ok(encrypt_locator(scope, {rid, 0}, keys, locator));
    write(dir + "/sample.rid", rid); write(dir + "/sample.row", row); write(dir + "/sample.locator", locator);
}
void decrypt_sample(const std::string& dir, const Scope* known_scope = nullptr) {
    KeySet keys; ok(load_keys(dir, keys)); Scope scope = known_scope ? *known_scope : scope_for_state();
    Row row; Locator loc; const auto rid = read(dir + "/sample.rid");
    ok(decrypt_row(scope, rid, read(dir + "/sample.row"), keys, row));
    ok(decrypt_locator(scope, read(dir + "/sample.locator"), keys, loc));
    require(row.ind == "1" && row.keyword == "apple" && row.payload == "public-payload" &&
            loc.rid == rid && loc.operation == 0, "real row and locator round trip");
}
void reopen(const char* program, const std::string& dir, const char* expected) {
    pid_t child = fork(); require(child >= 0, "fork reopen probe");
    if (child == 0) { execl(program, program, "probe", dir.c_str(), expected, static_cast<char*>(nullptr)); _exit(127); }
    int result; require(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0,
                        "new-process reopen result");
}
void marker_present(const std::string& dir) { require(exists(dir + "/operation.v1"), "marker retained"); }
void outcome(const std::string& name, Code code, const std::string& classification) {
    std::cout << "{\"case\":\"" << name << "\",\"result\":\"PASS\",\"status\":\"" << code_name(code)
              << "\",\"classification\":\"" << classification << "\",\"mock_sends\":" << sends
              << ",\"mock_index_rows\":" << index_rows << ",\"displays\":" << displays << ",\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) std::cout << (i ? "," : "") << '"' << events[i] << '"';
    std::cout << "]}\n";
}
}

extern "C" int __real_renameat(int, const char*, int, const char*);
extern "C" int __wrap_renameat(int a, const char* b, int c, const char* name) {
    writes.emplace_back(name);
    if (fail_marker && strcmp(name, "operation.v1") == 0) { errno = EIO; return -1; }
    return __real_renameat(a, b, c, name);
}

int main(int argc, char** argv) {
    try {
        require(geteuid() != 0, "run fixture as ordinary state owner");
        if (argc == 4 && std::string(argv[1]) == "probe") {
            require(setenv("SSE_V1_STATE_DIR", argv[2], 1) == 0, "probe state"); configure();
            server.tables.emplace("week_demo", CatalogTable{1, true}); server.candidate_exists = true;
            const auto s = validate_startup(); require(std::string(code_name(s.code)) == argv[3], "reopen status");
            if (s.ok()) decrypt_sample(argv[2]);
            return 0;
        }
        require(argc == 3, "usage: state-crypto-cases PRIVATE_ROOT CASE");
        const std::string name(argv[2]), dir = std::string(argv[1]) + "/" + name;
        if (name == "KEY-08") {
            require(mkdir(dir.c_str(), 0700) == 0, "partial case"); setenv("SSE_V1_STATE_DIR", dir.c_str(), 1); configure();
            expect(initialize_state("14177", [](const std::string& stage, KeySet& keys) {
                const auto s = real_initializer(stage, keys);
                return s.ok() ? Status{Code::crypto_failed, "injected initializer failure"} : s;
            }), Code::crypto_failed);
            const auto before = snapshot(dir);
            require(exists(dir + "/deployment.v1") && !exists(dir + "/environment.v1"), "partial environment retained");
            expect(validate_local_startup(), Code::state_invalid);
            expect(initialize_state("14177", real_initializer), Code::state_invalid);
            require(snapshot(dir) == before, "partial environment not rebuilt");
            reopen(argv[0], dir, "state_invalid"); outcome(name, Code::state_invalid, "partial_init_blocked"); return 0;
        }
        seed(dir); const auto scope = scope_for_state();
        if (name == "KEY-01" || name == "KEY-02") {
            const auto before = snapshot(dir); KeySet first, second; ok(load_keys(dir, first)); ok(load_keys(dir, second));
            require(first.row_key.size() == 32 && first.locator_key.size() == 32 && first.key_id.size() == 16,
                    "32/32/16 byte key convention");
            require(first.row_key == second.row_key && first.locator_key == second.locator_key && first.key_id == second.key_id,
                    "repeat load identical");
            decrypt_sample(dir); reopen(argv[0], dir, "ok");
            expect(initialize_state("14177", real_initializer), Code::state_invalid);
            require(snapshot(dir) == before, "reads/rejected reinit leave all bytes and permissions unchanged");
            outcome(name, Code::ok, name == "KEY-01" ? "real_keys_state_crypto_compatible" : "repeat_and_process_reopen_unchanged"); return 0;
        }
        if (name >= "KEY-03" && name <= "KEY-07") {
            if (name == "KEY-03") require(unlink((dir + "/row_aead_v1.key").c_str()) == 0, "remove row key");
            if (name == "KEY-04") require(unlink((dir + "/locator_aead_v1.key").c_str()) == 0, "remove locator key");
            if (name == "KEY-05") require(unlink((dir + "/row_aead_v1.keyid").c_str()) == 0, "remove key ID");
            if (name == "KEY-06") write(dir + "/row_aead_v1.key", "short");
            if (name == "KEY-07") {
                auto id = read(dir + "/row_aead_v1.keyid"); id[0] ^= 1; write(dir + "/row_aead_v1.keyid", id);
            }
            const auto before = snapshot(dir); expect(preflight(scope, "apple").status, Code::key_invalid);
            KeySet output; output.key_id = "old"; output.row_key = {1}; output.locator_key = {2};
            const auto loaded = load_keys(dir, output);
            if (name == "KEY-07") {
                ok(loaded); Row row; row.payload = "stale";
                expect(decrypt_row(scope, read(dir + "/sample.rid"), read(dir + "/sample.row"), output, row), Code::key_invalid);
                require(row.ind.empty() && row.keyword.empty() && row.payload.empty(), "failure clears plaintext");
            } else {
                expect(loaded, Code::key_invalid);
                require(output.key_id.empty() && output.row_key.empty() && output.locator_key.empty(), "failure clears loaded keys");
            }
            expect(initialize_state("14177", real_initializer), Code::state_invalid);
            reopen(argv[0], dir, "key_invalid");
            require(snapshot(dir) == before, "missing/bad keys not replaced and existing data unchanged");
            outcome(name, Code::key_invalid, "existing_encrypted_data_refused"); return 0;
        }
        if (name == "FLOW-01" || name == "FLOW-02") {
            if (name == "FLOW-02") write(dir + "/fides_client.mat", "apple 2");
            const auto before = snapshot(dir); events.emplace_back("preflight");
            const auto p = preflight(scope, name == "FLOW-01" ? "unknown" : "apple");
            expect(p.status, name == "FLOW-01" ? Code::ok : Code::state_invalid);
            require(!p.keyword_registered && !exists(dir + "/operation.v1"), "short circuit before begin");
            require(snapshot(dir) == before && sends == 0, "no state or mock network effect");
            outcome(name, p.status.code, name == "FLOW-01" ? "normal_empty_result" : "broken_state_failure"); return 0;
        }
        Operation op; const std::string before_counter = read(dir + "/fides_client.mat");
        events.emplace_back("preflight"); ok(preflight(scope, "apple").status);
        if (name == "ORDER-01" || name == "ORDER-02" || name == "HOOK-01") {
            fail_marker = name != "HOOK-01";
            if (name == "HOOK-01") require(setenv("STATE_FAULT", "marker-rename", 1) == 0, "activate scoped preload fault");
            const auto s = begin_operation(scope, name == "ORDER-01" ? "insert" : "search", op);
            expect(s, Code::state_invalid); events.emplace_back("begin_failed");
            fail_marker = false; unsetenv("STATE_FAULT");
            require(op.operation_id.empty() && !exists(dir + "/operation.v1"), "failed begin gives no operation");
            int count = 99; expect(increment_counter("apple", count, true), Code::incomplete_operation);
            require(count == 0 && read(dir + "/fides_client.mat") == before_counter && sends == 0 && index_rows == 2,
                    "failure precedes counters and mock index extraction");
            outcome(name, s.code, "marker_failed_before_effects"); return 0;
        }
        ok(begin_operation(scope, name == "ORDER-03" ? "insert" : "search", op));
        marker_present(dir); events.emplace_back("begin_durable");
        if (name == "FLOW-03" || name == "FLOW-04") {
            SearchInput token{"nonempty", "nonempty", 2};
            if (name == "FLOW-03") token.token.clear(); else token.derivation_key.clear();
            require(token.token.empty() || token.derivation_key.empty(), "injected empty token component");
            events.emplace_back("token_protocol_invalid");
            require(read(dir + "/fides_client.mat") == before_counter && sends == 0, "no effect for bad token");
            marker_present(dir); reopen(argv[0], dir, "incomplete_operation");
            outcome(name, Code::protocol_invalid, "registered_empty_token_failure"); return 0;
        }
        if (name == "ORDER-03" || name == "ORDER-04" || name == "FLOW-05") {
            if (name != "ORDER-03") { events.emplace_back("mock_extract_index"); ++sends; index_rows = 0; }
            int count = 0; ok(increment_counter("apple", count, true)); require(count == 3, "counter advanced after marker");
            events.emplace_back("counter_saved");
            ++sends;
            if (name == "FLOW-05") {
                events.emplace_back("mock_reinsert_failed"); marker_present(dir); reopen(argv[0], dir, "incomplete_operation");
                require(displays == 0 && index_rows == 0, "mock failure stops display and preserves failure evidence");
                outcome(name, Code::db_failed, "mock_reinsert_failure_marker_retained"); return 0;
            }
            events.emplace_back(name == "ORDER-03" ? "mock_insert_success" : "mock_reinsert_success"); index_rows = 2;
            decrypt_sample(dir, &scope); events.emplace_back("real_decrypt_success");
            ok(complete_operation(op)); events.emplace_back("complete_durable");
            require(!exists(dir + "/operation.v1"), "success clears marker");
            ++displays; events.emplace_back("fixture_display_allowed"); reopen(argv[0], dir, "ok");
            outcome(name, Code::ok, "ordered_fixture_success"); return 0;
        }
        throw std::runtime_error("unknown case");
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
