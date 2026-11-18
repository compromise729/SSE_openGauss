#include "fixtures.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace sse_v1;

namespace {
const char* fail_rename = nullptr;
bool fail_next_sync = false;
bool fail_clear = false;

void require(bool ok, const char* expr, int line)
{
    if (ok) return;
    std::cerr << "FAIL line " << line << ": " << expr << "\n";
    exit(1);
}
#define EXPECT(expr) require((expr), #expr, __LINE__)

void status(const Status& actual, Code expected)
{
    if (actual.code == expected) return;
    std::cerr << "FAIL expected " << code_name(expected) << ", got "
              << code_name(actual.code) << ": " << actual.message << "\n";
    exit(1);
}

Catalog server;
Status fixture_catalog(const std::string& table, Catalog& out)
{
    out = server;
    out.candidate_exists = server.candidate_exists || server.tables.count(table) != 0;
    return {Code::ok, ""};
}

void reset_server()
{
    server = Catalog();
    server.database_oid = "123";
    server.instance_directory = "/fixture/db/instance";
    status(set_catalog_reader(fixture_catalog), Code::ok);
}

Scope initialize(const std::string& root, bool registered)
{
    reset_server();
    std::string pattern = root + "/case.XXXXXX";
    const char* dir = mkdtemp(&pattern[0]);
    EXPECT(dir != nullptr);
    EXPECT(setenv("SSE_V1_STATE_DIR", dir, 1) == 0);
    status(initialize_state("123", fixture_keys), Code::ok);
    Scope scope;
    bool seen;
    status(local_scope("week_demo", scope, seen), Code::ok);
    EXPECT(!seen);
    if (registered) {
        Operation op;
        status(begin_operation(scope, "create", op), Code::ok);
        server.tables.emplace(scope.table, CatalogTable{1, true});
        status(register_table(scope, 1), Code::ok);
        status(complete_operation(op), Code::ok);
        status(begin_operation(scope, "insert", op), Code::ok);
        status(save_counters(op, {{"apple", 2}}), Code::ok);
        status(complete_operation(op), Code::ok);
    }
    return scope;
}

std::string file(const char* name) { return std::string(getenv("SSE_V1_STATE_DIR")) + "/" + name; }
bool exists(const char* name) { return access(file(name).c_str(), F_OK) == 0; }

void replace(const char* name, const std::string& bytes)
{
    std::ofstream f(file(name), std::ios::binary | std::ios::trunc);
    EXPECT(static_cast<bool>(f.write(bytes.data(), bytes.size())));
    f.close();
    EXPECT(static_cast<bool>(f));
}

void child_status(const char* program, const Scope& scope, Code expected)
{
    const pid_t child = fork();
    EXPECT(child >= 0);
    if (child == 0) {
        execl(program, program, "probe", scope.deployment_id.c_str(), code_name(expected), static_cast<char*>(nullptr));
        _exit(127);
    }
    int rc;
    EXPECT(waitpid(child, &rc, 0) == child && WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
}
} // namespace

extern "C" int __real_renameat(int, const char*, int, const char*);
extern "C" int __wrap_renameat(int from_dir, const char* from, int to_dir, const char* to)
{
    if (fail_rename && strcmp(fail_rename, to) == 0) { errno = EIO; return -1; }
    return __real_renameat(from_dir, from, to_dir, to);
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd)
{
    if (fail_next_sync) { fail_next_sync = false; errno = EIO; return -1; }
    return __real_fsync(fd);
}
extern "C" int __real_unlinkat(int, const char*, int);
extern "C" int __wrap_unlinkat(int dir, const char* name, int flags)
{
    if (fail_clear && strcmp(name, "operation.v1") == 0) { errno = EIO; return -1; }
    return __real_unlinkat(dir, name, flags);
}

int main(int argc, char** argv)
{
    EXPECT(geteuid() != 0);
    if (argc == 4 && std::string(argv[1]) == "probe") {
        reset_server();
        server.tables.emplace("week_demo", CatalogTable{1, true});
        Operation op;
        const auto s = begin_operation({argv[2], "123", "public", "week_demo"}, "insert", op);
        EXPECT(std::string(code_name(s.code)) == argv[3] && op.operation_id.empty());
        return 0;
    }
    if (argc == 3) {
        const std::string fault = argv[2];
        const auto scope = initialize(argv[1], fault != "map");
        Operation op;
        status(begin_operation(scope, fault == "map" ? "create" : "search", op), Code::ok);
        if (fault == "staged") {
            int counter = 0;
            status(increment_counter("apple", counter, false), Code::ok);
            EXPECT(counter == 3 && fixture_read(file("fides_client.mat")) == "apple 2\n");
            status(complete_operation(op), Code::state_invalid);
            std::cout << "PASS STATE-12: unsaved batched counter prevents completion\n";
        } else if (fault == "map") {
            server.tables.emplace(scope.table, CatalogTable{1, true});
            fail_rename = "sse_table.mat";
            status(register_table(scope, 1), Code::state_invalid);
            EXPECT(fixture_read(file("sse_table.mat")).empty());
            std::cout << "PASS STATE-09: map rename failure after server create leaves marker\n";
        } else if (fault == "save") {
            fail_next_sync = true;
            status(save_counters(op, {{"apple", 4}}), Code::state_invalid);
            EXPECT(fixture_read(file("fides_client.mat")) == "apple 2\n");
            std::cout << "PASS STATE-10: fsync failure preserves original counters and marker\n";
        } else {
            status(save_counters(op, {{"apple", 4}}), Code::ok);
            fail_clear = fault == "unlink";
            fail_next_sync = fault == "clear-sync";
            EXPECT(fail_clear || fail_next_sync);
            status(complete_operation(op), Code::state_invalid);
            std::cout << "PASS STATE-11: marker removal failure blocks completion (" << fault << ")\n";
        }
        EXPECT(exists("operation.v1"));
        status(complete_operation(op), Code::incomplete_operation);
        return 0;
    }
    EXPECT(argc == 2);
    const std::string root = argv[1];
    auto scope = initialize(root, true);
    auto before = fixture_read(file("fides_client.mat"));
    const auto found = preflight(scope, "apple");
    const auto unknown = preflight(scope, "green apple");
    EXPECT(found.status.ok() && found.keyword_registered && found.counter == 2);
    EXPECT(unknown.status.ok() && !unknown.keyword_registered);
    EXPECT(before == fixture_read(file("fides_client.mat")));
    std::cout << "PASS STATE-01: registered and healthy unknown keyword are distinct\n";

    replace("fides_client.mat", "apple 2");
    status(preflight(scope, "apple").status, Code::state_invalid);
    EXPECT(unlink(file("fides_client.mat").c_str()) == 0);
    status(preflight(scope, "apple").status, Code::state_invalid);
    status(initialize_state("123", fixture_keys), Code::state_invalid);
    EXPECT(!exists("fides_client.mat"));
    replace("fides_client.mat", before);
    EXPECT(chmod(file("fides_client.mat").c_str(), 0000) == 0);
    status(preflight(scope, "apple").status, Code::state_invalid);
    EXPECT(chmod(file("fides_client.mat").c_str(), 0600) == 0);
    std::cout << "PASS STATE-02: truncated, missing, and unreadable state rejected without initialization\n";

    EXPECT(chmod(file("row_aead_v1.key").c_str(), 0640) == 0);
    status(preflight(scope, "apple").status, Code::key_invalid);
    EXPECT(chmod(file("row_aead_v1.key").c_str(), 0600) == 0);
    replace("row_aead_v1.key", "short");
    status(preflight(scope, "apple").status, Code::key_invalid);
    EXPECT(unlink(file("row_aead_v1.key").c_str()) == 0);
    status(validate_startup(), Code::key_invalid);
    EXPECT(!exists("row_aead_v1.key"));
    replace("row_aead_v1.key", std::string(32, 'r'));
    EXPECT(chmod(file("row_aead_v1.key").c_str(), 0600) == 0);
    replace("row_aead_v1.keyid", std::string(16, 'x'));
    status(validate_startup(), Code::key_invalid);
    std::cout << "PASS STATE-03: key mode, length, absence and key_id mismatch rejected\n";

    scope = initialize(root, true);
    server.instance_directory = "/another/instance";
    status(validate_startup(), Code::state_invalid);
    server.instance_directory = "/fixture/db/instance";
    server.database_oid = "124";
    status(validate_startup(), Code::state_invalid);
    server.database_oid = "123";
    server.tables.at("week_demo").version = 2;
    status(validate_startup(), Code::state_invalid);
    server.tables.at("week_demo").version = 1;
    server.tables.at("week_demo").physical_shape = false;
    status(validate_startup(), Code::state_invalid);
    server.tables.clear();
    status(validate_startup(), Code::state_invalid);
    std::cout << "PASS STATE-04: wrong instance, OID, version, physical shape and missing registration rejected\n";

    scope = initialize(root, true);
    Operation op;
    EXPECT(chmod(getenv("SSE_V1_STATE_DIR"), 0500) == 0);
    status(begin_operation(scope, "insert", op), Code::state_invalid);
    EXPECT(chmod(getenv("SSE_V1_STATE_DIR"), 0700) == 0);
    EXPECT(chmod(file("operation.lock").c_str(), 0400) == 0);
    status(begin_operation(scope, "insert", op), Code::state_invalid);
    EXPECT(chmod(file("operation.lock").c_str(), 0600) == 0);
    fail_rename = "operation.v1";
    status(begin_operation(scope, "insert", op), Code::state_invalid);
    fail_rename = nullptr;
    EXPECT(op.operation_id.empty() && !exists("operation.v1") && before == fixture_read(file("fides_client.mat")));
    std::cout << "PASS STATE-05: unwritable directory and marker write failure block before counters\n";

    status(begin_operation(scope, "insert", op), Code::ok);
    EXPECT(exists("operation.v1"));
    child_status(argv[0], scope, Code::busy);
    EXPECT(before == fixture_read(file("fides_client.mat")));
    Counters counters;
    status(operation_counters(op, counters), Code::ok);
    EXPECT(counters.at("apple") == 2);
    counters["apple"] = 4;
    status(save_counters(op, counters), Code::ok);
    status(complete_operation(op), Code::ok);
    EXPECT(!exists("operation.v1") && preflight(scope, "apple").counter == 4);
    std::cout << "PASS STATE-06: second process refused; successful operation persists and releases lock\n";

    const pid_t crashed = fork();
    EXPECT(crashed >= 0);
    if (crashed == 0) {
        Operation unfinished;
        status(begin_operation(scope, "search", unfinished), Code::ok);
        kill(getpid(), SIGKILL);
        _exit(127);
    }
    int rc;
    EXPECT(waitpid(crashed, &rc, 0) == crashed && WIFSIGNALED(rc) && WTERMSIG(rc) == SIGKILL);
    child_status(argv[0], scope, Code::incomplete_operation);
    status(validate_startup(), Code::incomplete_operation);
    std::cout << "PASS STATE-07: process exit releases lock but restart is blocked by marker\n";

    scope = initialize(root, false);
    server.candidate_exists = true;
    status(begin_operation(scope, "create", op), Code::db_failed);
    EXPECT(!exists("operation.v1"));
    server.candidate_exists = false;
    status(begin_operation(scope, "create", op), Code::ok);
    status(register_table(scope, 1), Code::state_invalid);
    EXPECT(exists("operation.v1") && fixture_read(file("sse_table.mat")).empty());
    std::cout << "PASS STATE-08: failed server create leaves marker and no local enrollment\n";

    // Each failure retains the active lock until process exit. Use separate
    // child processes for the remaining independent fault injections.
    return 0;
}
