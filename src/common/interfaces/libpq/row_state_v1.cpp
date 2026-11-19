#include "libpq/row_state_v1.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace sse_v1 {
namespace {
const char* const kLock = "operation.lock";
const char* const kMarker = "operation.v1";
const char* const kCounter = "fides_client.mat";
const char* const kMapping = "sse_table.mat";
const char* const kTables = "row_aead_tables.v1";
const char* const kDeployment = "deployment.v1";
const char* const kEnvironment = "environment.v1";
const char* const kKeyStage = ".row-keys-init-v1";
const char* const kKeyFiles[] = {"row_aead_v1.key", "locator_aead_v1.key", "row_aead_v1.keyid"};
const char* const kTableHeader = "SSE_ROW_TABLES_V1\n";
const char* const kEnvironmentHeader = "SSE_ROW_ENVIRONMENT_V1\n";
const size_t kStateSizeLimit = 64U * 1024U * 1024U;

struct Fd {
    int fd;
    explicit Fd(int value = -1) : fd(value) {}
    ~Fd() { if (fd >= 0) close(fd); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    bool valid() const { return fd >= 0; }
    int release() { int value = fd; fd = -1; return value; }
};

struct Snapshot {
    std::string deployment_id;
    std::string database_oid;
    std::string table;
    std::string instance_identity;
    Counters counters;
};

struct Active {
    Fd directory;
    Fd lock;
    Scope scope;
    std::string id;
    std::string kind;
    std::string marker;
    std::string instance_identity;
    Counters counters;
    bool saved = false;
    bool failed = false;
    bool advanced = false;
};
std::mutex state_mutex;
CatalogReader catalog_reader;
Active active;

Status good() { return {Code::ok, ""}; }
Status state_error(const std::string& message) { return {Code::state_invalid, message}; }
Status key_error(const std::string& message) { return {Code::key_invalid, message}; }
Status io_error(const std::string& action, const std::string& file)
{
    const int error = errno;
    return state_error(action + " " + file + ": " + std::strerror(error));
}

std::string directory_path()
{
    const char* value = std::getenv("SSE_V1_STATE_DIR");
    return value == nullptr ? "/opt/og/openGauss-server/sse_storage" : value;
}

bool owned_regular(const struct stat& st)
{
    return S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_nlink == 1;
}

Status open_directory(const std::string& path, Fd& dir)
{
    if (path.empty() || path[0] != '/') return state_error("SSE_V1_STATE_DIR must be an absolute path");
    dir.fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (!dir.valid()) return io_error("open directory", path);
    struct stat st;
    if (fstat(dir.fd, &st) != 0) return io_error("stat directory", path);
    if (st.st_uid != geteuid() || (st.st_mode & 07777) != 0700)
        return state_error("state directory must be owned by the current user and have mode 0700");
    return good();
}

Status lock_directory(int dir, Fd& lock)
{
    lock.fd = openat(dir, kLock, O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (!lock.valid()) return io_error("open", kLock);
    struct stat st;
    if (fstat(lock.fd, &st) != 0 || !owned_regular(st) || (st.st_mode & 07777) != 0600)
        return state_error("invalid operation.lock");
    if (flock(lock.fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) return {Code::busy, "state directory is locked"};
        return io_error("lock", kLock);
    }
    return good();
}

Status no_marker(int dir)
{
    struct stat st;
    if (fstatat(dir, kMarker, &st, AT_SYMLINK_NOFOLLOW) == 0)
        return {Code::incomplete_operation, "operation.v1 exists; preserve the environment for diagnosis"};
    return errno == ENOENT ? good() : io_error("stat", kMarker);
}

bool sync_fd(int fd)
{
    int rc;
    do { rc = fsync(fd); } while (rc < 0 && errno == EINTR);
    return rc == 0;
}

bool random_bytes(std::string& bytes)
{
    Fd source(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (!source.valid()) return false;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n = read(source.fd, &bytes[offset], bytes.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += static_cast<size_t>(n);
    }
    return close(source.release()) == 0;
}

std::string hex(const std::string& bytes)
{
    const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

Status durable_write(int dir, const char* name, const std::string& data, bool create_only = false)
{
    struct stat existing;
    if (fstatat(dir, name, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (create_only || !owned_regular(existing))
            return state_error(std::string("refusing to replace ") + name);
    } else if (errno != ENOENT) {
        return io_error("stat", name);
    }
    std::string random(16, '\0');
    if (!random_bytes(random)) return {Code::crypto_failed, "random source failed"};
    const std::string tmp = ".row-state-" + hex(random);
    Fd fd(openat(dir, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!fd.valid()) return io_error("create temporary file for", name);
    Status s = good();
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = write(fd.fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            s = n < 0 ? io_error("write", name) : state_error(std::string("short write: ") + name);
            break;
        }
        offset += static_cast<size_t>(n);
    }
    if (s.ok() && !sync_fd(fd.fd)) s = io_error("fsync", name);
    if (close(fd.release()) != 0 && s.ok()) s = io_error("close", name);
    if (s.ok() && renameat(dir, tmp.c_str(), dir, name) != 0) s = io_error("rename", name);
    if (!s.ok()) {
        (void)unlinkat(dir, tmp.c_str(), 0);
        return s;
    }
    return sync_fd(dir) ? good() : io_error("fsync directory after saving", name);
}

Status read_file(int dir, const char* name, std::string& out)
{
    out.clear();
    Fd fd(openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (!fd.valid()) return io_error("open", name);
    struct stat st;
    if (fstat(fd.fd, &st) != 0 || !owned_regular(st))
        return state_error(std::string("invalid file: ") + name);
    char buffer[4096];
    for (;;) {
        const ssize_t n = read(fd.fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return io_error("read", name);
        if (n == 0) break;
        if (out.size() + static_cast<size_t>(n) > kStateSizeLimit)
            return state_error(std::string("state file too large: ") + name);
        out.append(buffer, static_cast<size_t>(n));
    }
    return close(fd.release()) == 0 ? good() : io_error("close", name);
}

Status read_key_id(int dir, std::string& id)
{
    const off_t lengths[] = {32, 32, 16};
    for (size_t i = 0; i < 3; ++i) {
        Fd fd(openat(dir, kKeyFiles[i], O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        struct stat st;
        if (!fd.valid() || fstat(fd.fd, &st) != 0 || !owned_regular(st) ||
            (st.st_mode & 07777) != 0600 || st.st_size != lengths[i])
            return key_error(std::string("missing, unreadable or invalid key metadata: ") + kKeyFiles[i]);
    }
    std::string raw;
    const Status s = read_file(dir, kKeyFiles[2], raw);
    if (!s.ok() || raw.size() != 16) return key_error("unable to read row_aead_v1.keyid");
    id = hex(raw);
    return good();
}

Status initialize_crypto_keys(int dir, const std::string& path, const KeyInitializer& initialize, std::string& key_id)
{
    // The state lock and deployment record already exist in the parent.
    // The crypto initializer needs a new private directory that is still empty.
    if (mkdirat(dir, kKeyStage, 0700) != 0) return io_error("create", kKeyStage);
    const auto stage_path = path + "/" + kKeyStage;
    Fd stage;
    auto status = open_directory(stage_path, stage);
    if (!status.ok()) return status;

    KeySet keys;
    status = initialize(stage_path, keys);
    if (status.ok()) status = read_key_id(stage.fd, key_id);
    if (!status.ok()) return status;
    if (hex(keys.key_id) != key_id) return key_error("initialized key_id does not match its file");

    // Keep partial initialization for diagnosis. environment.v1 is published
    // only after promotion completes. Each link refuses an existing target.
    for (const auto* name : kKeyFiles) {
        if (linkat(stage.fd, name, dir, name, 0) != 0) return io_error("publish", name);
        if (unlinkat(stage.fd, name, 0) != 0) return io_error("remove staged file", name);
    }
    if (unlinkat(dir, kKeyStage, AT_REMOVEDIR) != 0) return io_error("remove empty directory", kKeyStage);
    return sync_fd(dir) ? good() : io_error("fsync directory after publishing", kKeyStage);
}

bool identifier(const std::string& name)
{
    if (name.empty() || name.size() > 40 || name.front() < 'a' || name.front() > 'z') return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool decimal(const std::string& value, unsigned long limit, unsigned long& number)
{
    number = 0;
    if (value.empty()) return false;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (number > (limit - digit) / 10) return false;
        number = number * 10 + digit;
    }
    return true;
}

bool oid(const std::string& value)
{
    unsigned long n;
    return decimal(value, 0xffffffffUL, n) && n > 0 && std::to_string(n) == value;
}

bool valid_keyword(const std::string& text)
{
    if (text.size() > kMaxKeywordBytes) return false;
    for (size_t i = 0; i < text.size();) {
        const unsigned char first = text[i++];
        if (first == '\0' || first == '\n' || first == '\r') return false;
        if (first < 0x80) continue;
        unsigned n;
        unsigned value;
        if (first >= 0xc2 && first <= 0xdf) { n = 1; value = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { n = 2; value = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { n = 3; value = first & 0x07; }
        else return false;
        if (text.size() - i < n) return false;
        for (unsigned j = 0; j < n; ++j) {
            const unsigned char c = text[i++];
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 0x3f);
        }
        if ((n == 2 && value < 0x800) || (n == 3 && value < 0x10000) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return false;
    }
    return true;
}

std::string registration(const Snapshot& s, const std::string& table)
{
    return s.deployment_id + "\t" + s.database_oid + "\tpublic\t" + table + "\t1\n";
}

std::string mapping(const std::string& table)
{
    return table + " ind|undefined keyword|fides payload|undefined\n";
}

Status read_registration(int dir, std::string& data, std::string& table)
{
    table.clear();
    const Status status = read_file(dir, kTables, data);
    if (!status.ok()) return status;
    const size_t header = std::strlen(kTableHeader);
    if (data.compare(0, header, kTableHeader) != 0)
        return state_error("invalid row_aead_tables.v1 header");
    if (data.size() == header) return good();
    const size_t start = data.find("\tpublic\t", header);
    if (start == std::string::npos) return state_error("invalid row_aead_tables.v1 entry");
    const size_t end = data.find('\t', start + 8);
    if (end == std::string::npos) return state_error("invalid row_aead_tables.v1 entry");
    table = data.substr(start + 8, end - start - 8);
    if (!identifier(table)) return state_error("invalid row_aead_tables.v1 table name");
    return good();
}

Status load_local(int dir, Snapshot& out)
{
    Status s = no_marker(dir);
    if (!s.ok()) return s;
    std::string data, metadata, key_id;
    s = read_file(dir, kDeployment, data);
    if (!s.ok()) return s;
    if (data.size() != 16) return state_error("invalid deployment.v1 length");
    out.deployment_id = hex(data);
    s = read_file(dir, kEnvironment, metadata);
    if (!s.ok()) return s;
    s = read_key_id(dir, key_id);
    if (!s.ok()) return s;
    const std::string prefix = std::string(kEnvironmentHeader) + out.deployment_id + "\t";
    if (metadata.compare(0, prefix.size(), prefix) != 0) return state_error("invalid environment.v1 deployment");
    const size_t split = metadata.find('\t', prefix.size());
    if (split == std::string::npos) return state_error("invalid environment.v1");
    out.database_oid = metadata.substr(prefix.size(), split - prefix.size());
    if (!oid(out.database_oid)) return state_error("invalid environment.v1 database OID");
    const size_t identity_end = metadata.find('\t', split + 1);
    if (identity_end == std::string::npos || identity_end == split + 1)
        return state_error("missing instance identity in environment.v1");
    out.instance_identity = metadata.substr(split + 1, identity_end - split - 1);
    if (metadata.substr(identity_end + 1) != key_id + "\n") return key_error("key_id does not match environment.v1");

    s = read_registration(dir, data, out.table);
    if (!s.ok()) return s;
    if (!out.table.empty() && data != kTableHeader + registration(out, out.table))
        return state_error("table version or scope in row_aead_tables.v1 is invalid");
    s = read_file(dir, kMapping, data);
    if (!s.ok()) return s;
    if (data != (out.table.empty() ? "" : mapping(out.table)))
        return state_error("sse_table.mat does not match version registration");
    s = read_file(dir, kCounter, data);
    if (!s.ok()) return s;
    if (out.table.empty() && !data.empty()) return state_error("counters exist without table registration");
    size_t start = 0;
    while (start < data.size()) {
        const size_t end = data.find('\n', start);
        if (end == std::string::npos) return state_error("truncated fides_client.mat record");
        const std::string line = data.substr(start, end - start);
        const size_t split_pos = line.rfind(' ');
        unsigned long count;
        if (split_pos == std::string::npos || !valid_keyword(line.substr(0, split_pos)) ||
            !decimal(line.substr(split_pos + 1), INT_MAX, count) ||
            !out.counters.emplace(line.substr(0, split_pos), static_cast<int>(count)).second)
            return state_error("invalid or duplicate fides_client.mat record");
        start = end + 1;
    }
    return good();
}

Status check_scope(const Snapshot& state, const Scope& scope)
{
    if (scope.schema != "public" || !identifier(scope.table) || !oid(scope.database_oid))
        return {Code::invalid_input, "invalid table scope"};
    if (scope.deployment_id != state.deployment_id || scope.database_oid != state.database_oid ||
        (!state.table.empty() && scope.table != state.table))
        return state_error("scope does not match the local environment");
    return good();
}

Status check_remote(const Snapshot& state, const std::string& candidate, bool expected_table, bool creating = false)
{
    if (!catalog_reader) return state_error("current connection catalog reader is unavailable");
    Catalog catalog;
    Status s = catalog_reader(candidate, catalog);
    if (!s.ok()) return s;
    if (catalog.database_oid != state.database_oid ||
        catalog.instance_directory.empty() || catalog.instance_directory[0] != '/' ||
        hex(catalog.instance_directory) != state.instance_identity)
        return state_error("connected instance or database OID does not match state");
    if (!expected_table) {
        if (!catalog.tables.empty())
            return state_error("server contains a table missing from local version registration");
        if (catalog.candidate_exists && creating) return {Code::db_failed, "server table already exists"};
        if (catalog.candidate_exists)
            return state_error("server contains a table missing from local version registration");
        return good();
    }
    const auto table = catalog.tables.find(candidate);
    if (catalog.tables.size() != 1 || table == catalog.tables.end() ||
        table->second.version != kProtocolVersion || !table->second.physical_shape)
        return state_error("server table, physical shape or protocol version does not match state");
    return good();
}

Status load_checked(int dir, Snapshot& state, const Scope* scope)
{
    auto s = load_local(dir, state);
    if (s.ok() && scope) s = check_scope(state, *scope);
    if (s.ok()) s = check_remote(state, scope ? scope->table : state.table, !state.table.empty());
    return s;
}

bool empty_state_directory(int dir)
{
    Fd fd(dup(dir));
    if (!fd.valid()) return false;
    DIR* entries = fdopendir(fd.fd);
    if (!entries) return false;
    (void)fd.release();
    bool empty = true;
    for (;;) {
        errno = 0;
        const dirent* entry = readdir(entries);
        if (!entry) {
            empty = empty && errno == 0;
            break;
        }
        const std::string name(entry->d_name);
        if (name != "." && name != ".." && name != kLock) {
            empty = false;
            break;
        }
    }
    return closedir(entries) == 0 && empty;
}

Status operation_failure(Status status)
{
    active.failed = true;
    return status;
}

void release_operation()
{
    close(active.lock.release());
    close(active.directory.release());
    active.id.clear();
    active.kind.clear();
    active.marker.clear();
    active.counters.clear();
    active.saved = false;
    active.failed = false;
    active.advanced = false;
}

Status clear_marker()
{
    std::string stored;
    Status s = read_file(active.directory.fd, kMarker, stored);
    if (!s.ok() || stored != active.marker) return operation_failure(state_error("operation marker changed or is missing"));
    if (unlinkat(active.directory.fd, kMarker, 0) != 0)
        return operation_failure(io_error("unlink", kMarker));
    if (!sync_fd(active.directory.fd)) {
        s = io_error("fsync directory after clearing", kMarker);
        const Status restore = durable_write(active.directory.fd, kMarker, active.marker, true);
        if (!restore.ok()) s.message += "; marker restoration is uncertain: " + restore.message;
        return operation_failure(s);
    }
    release_operation();
    return good();
}
} // namespace

const char* code_name(Code code)
{
    switch (code) {
#define STATE_CODE(name) case Code::name: return #name
        STATE_CODE(ok); STATE_CODE(unsupported); STATE_CODE(invalid_input);
        STATE_CODE(state_invalid); STATE_CODE(unknown_keyword); STATE_CODE(key_invalid);
        STATE_CODE(crypto_failed); STATE_CODE(auth_failed); STATE_CODE(protocol_invalid);
        STATE_CODE(db_failed); STATE_CODE(incomplete_operation); STATE_CODE(busy);
        STATE_CODE(inconsistent_result);
#undef STATE_CODE
    }
    return "state_invalid";
}

Status set_catalog_reader(const CatalogReader& reader)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (active.lock.valid()) return {Code::busy, "operation is active"};
    catalog_reader = reader;
    return good();
}

Status initialize_state(const std::string& database_oid, const KeyInitializer& initialize)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!oid(database_oid) || !initialize) return {Code::invalid_input, "explicit database OID and key initializer required"};
    const auto path = directory_path();
    Fd dir, lock;
    Status s = open_directory(path, dir);
    if (s.ok()) s = lock_directory(dir.fd, lock);
    if (s.ok()) s = no_marker(dir.fd);
    if (!s.ok()) return s;
    if (!empty_state_directory(dir.fd)) return state_error("initialization requires an empty state directory");
    Snapshot snap;
    snap.database_oid = database_oid;
    if (!catalog_reader) return state_error("current connection catalog reader is unavailable");
    Catalog catalog;
    s = catalog_reader("", catalog);
    if (!s.ok()) return s;
    if (catalog.database_oid != database_oid || catalog.instance_directory.empty() ||
        catalog.instance_directory[0] != '/' || !catalog.tables.empty())
        return state_error("initialization requires the selected database and an empty server catalog");
    snap.instance_identity = hex(catalog.instance_directory);
    std::string deployment(16, '\0');
    if (!random_bytes(deployment)) return {Code::crypto_failed, "deployment random source failed"};
    snap.deployment_id = hex(deployment);
    // After this point a partial setup is preserved. A second init is rejected.
    s = durable_write(dir.fd, kDeployment, deployment, true);
    std::string key_id;
    if (s.ok()) s = initialize_crypto_keys(dir.fd, path, initialize, key_id);
    if (s.ok()) s = durable_write(dir.fd, kCounter, "", true);
    if (s.ok()) s = durable_write(dir.fd, kMapping, "", true);
    if (s.ok()) s = durable_write(dir.fd, kTables, kTableHeader, true);
    if (s.ok()) s = durable_write(dir.fd, kEnvironment,
        kEnvironmentHeader + snap.deployment_id + "\t" + database_oid + "\t" + snap.instance_identity + "\t" + key_id + "\n", true);
    return s;
}

Status validate_local_startup()
{
    std::lock_guard<std::mutex> guard(state_mutex);
    Fd dir;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    return s.ok() ? load_local(dir.fd, state) : s;
}

Status validate_startup()
{
    std::lock_guard<std::mutex> guard(state_mutex);
    Fd dir;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    return s.ok() ? load_checked(dir.fd, state, nullptr) : s;
}

Status registered_table(std::string& table)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    Fd dir;
    std::string registration_data;
    const Status status = open_directory(directory_path(), dir);
    return status.ok() ? read_registration(dir.fd, registration_data, table) : status;
}

Status local_scope(const std::string& table, Scope& scope, bool& table_registered)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    scope = Scope();
    table_registered = false;
    if (!identifier(table)) return {Code::invalid_input, "invalid table name"};
    Fd dir;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    if (s.ok()) s = load_local(dir.fd, state);
    if (!s.ok()) return s;
    scope = {state.deployment_id, state.database_oid, "public", table};
    table_registered = state.table == table;
    return good();
}

Preflight preflight(const Scope& scope, const std::string& keyword)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!valid_keyword(keyword)) return {{Code::invalid_input, "invalid keyword"}, false, 0};
    Fd dir;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    if (s.ok()) s = load_checked(dir.fd, state, &scope);
    if (!s.ok()) return {s, false, 0};
    const auto value = state.counters.find(keyword);
    return value == state.counters.end() ? Preflight{good(), false, 0} : Preflight{good(), true, value->second};
}

Status load_table_version(const Scope& scope, unsigned& version)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    version = 0;
    Fd dir;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    if (s.ok()) s = load_checked(dir.fd, state, &scope);
    if (s.ok() && state.table.empty()) return state_error("table is not registered");
    if (s.ok()) version = kProtocolVersion;
    return s;
}

Status begin_operation(const Scope& scope, const std::string& kind, Operation& operation)
{
    return begin_checked_operation(scope, kind, operation, CounterCheck());
}

Status begin_checked_operation(const Scope& scope, const std::string& kind, Operation& operation,
                               const CounterCheck& check)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    operation.operation_id.clear();
    if (kind != "create" && kind != "insert" && kind != "search")
        return {Code::invalid_input, "unknown operation kind"};
    if (active.lock.valid()) return {Code::busy, "an operation is active in this process"};
    Fd dir, lock;
    Snapshot state;
    Status s = open_directory(directory_path(), dir);
    if (s.ok()) s = lock_directory(dir.fd, lock);
    if (s.ok()) s = load_local(dir.fd, state);
    if (s.ok()) s = check_scope(state, scope);
    if (s.ok() && check) s = check(state.counters);
    if (!s.ok()) return s;
    if (kind == "create") {
        if (!state.table.empty()) return {Code::db_failed, "table already registered"};
        s = check_remote(state, scope.table, false, true);
    } else {
        if (state.table.empty()) return state_error("table is not registered");
        s = check_remote(state, scope.table, true);
    }
    if (!s.ok()) return s;
    std::string random(16, '\0');
    if (!random_bytes(random)) return {Code::crypto_failed, "operation random source failed"};
    const std::string id = hex(random);
    const std::string marker = "SSE_OPERATION_V1\n1\t" + id + "\t" + scope.deployment_id +
        "\t" + scope.database_oid + "\t" + scope.schema + "\t" + scope.table + "\t" + kind + "\n";
    s = durable_write(dir.fd, kMarker, marker, true);
    if (!s.ok()) return s;
    active.directory.fd = dir.release();
    active.lock.fd = lock.release();
    active.scope = scope;
    active.id = id;
    active.kind = kind;
    active.marker = marker;
    active.instance_identity = state.instance_identity;
    active.counters.swap(state.counters);
    operation.operation_id = id;
    return good();
}

Status register_table(const Scope& scope, unsigned version)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!active.lock.valid() || active.failed || active.kind != "create")
        return {Code::incomplete_operation, "active create operation required"};
    if (active.saved || version != kProtocolVersion || scope.schema != active.scope.schema ||
        scope.table != active.scope.table || scope.database_oid != active.scope.database_oid ||
        scope.deployment_id != active.scope.deployment_id)
        return operation_failure(state_error("table registration does not match the active create"));
    Snapshot state;
    state.deployment_id = scope.deployment_id;
    state.database_oid = scope.database_oid;
    state.instance_identity = active.instance_identity;
    Status s = check_remote(state, scope.table, true);
    if (s.ok()) s = durable_write(active.directory.fd, kTables, kTableHeader + registration(state, scope.table));
    if (s.ok()) s = durable_write(active.directory.fd, kMapping, mapping(scope.table));
    if (!s.ok()) return operation_failure(s);
    active.saved = true;
    return good();
}

Status operation_counters(const Operation& operation, Counters& out)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    out.clear();
    if (!active.lock.valid() || active.failed || active.kind == "create" || operation.operation_id != active.id)
        return {Code::incomplete_operation, "matching active insert or search required"};
    out = active.counters;
    return good();
}

bool v1_state_selected()
{
    return std::getenv("SSE_V1_STATE_DIR") || std::getenv("SSE_V1_MODE");
}

Status save_current_counters(const Counters& counters)
{
    Operation operation;
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        if (!active.lock.valid() || active.failed || active.kind == "create")
            return {Code::incomplete_operation, "begin_operation required before saving counters"};
        operation.operation_id = active.id;
    }
    return save_counters(operation, counters);
}

Status increment_counter(const std::string& keyword, int& counter, bool persist)
{
    counter = 0;
    Counters updated;
    Operation operation;
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        if (!active.lock.valid() || active.failed || active.kind == "create")
            return {Code::incomplete_operation, "begin_operation required before advancing counters"};
        if (!valid_keyword(keyword)) return operation_failure({Code::invalid_input, "invalid keyword"});
        auto found = active.counters.find(keyword);
        const int previous = found == active.counters.end() ? 0 : found->second;
        if (previous == INT_MAX) return operation_failure(state_error("counter overflow"));
        active.counters[keyword] = previous + 1;
        active.advanced = true;
        active.saved = false;
        updated = active.counters;
        operation.operation_id = active.id;
        if (!persist) {
            counter = previous + 1;
            return good();
        }
    }
    Status s = save_counters(operation, updated);
    if (s.ok()) counter = updated.at(keyword);
    return s;
}

Status register_current_table(const std::string& table)
{
    Scope scope;
    {
        std::lock_guard<std::mutex> guard(state_mutex);
        if (!active.lock.valid() || active.failed || active.kind != "create")
            return {Code::incomplete_operation, "begin_operation(create) required before mapping changes"};
        if (table != active.scope.table)
            return operation_failure(state_error("mapping table differs from active create"));
        scope = active.scope;
    }
    return register_table(scope, kProtocolVersion);
}

Status current_operation_counters(Counters& out)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    out.clear();
    if (!active.lock.valid() || active.failed || active.kind == "create")
        return {Code::incomplete_operation, "active insert or search required before loading Fides counters"};
    out = active.counters;
    return good();
}

Status save_counters(const Operation& operation, const Counters& counters)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!active.lock.valid() || active.failed || active.kind == "create" || operation.operation_id != active.id)
        return {Code::incomplete_operation, "matching active insert or search required"};
    std::string data;
    for (const auto& entry : counters) {
        if (!valid_keyword(entry.first) || entry.second < 0)
            return operation_failure(state_error("invalid counter"));
        data += entry.first + " " + std::to_string(entry.second) + "\n";
        if (data.size() > kStateSizeLimit) return operation_failure(state_error("counter file too large"));
    }
    const Status s = durable_write(active.directory.fd, kCounter, data);
    if (!s.ok()) return operation_failure(s);
    active.counters = counters;
    active.saved = true;
    return good();
}

Status complete_operation(const Operation& operation)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!active.lock.valid() || active.failed || operation.operation_id != active.id)
        return {Code::incomplete_operation, "matching successful operation required"};
    if (!active.saved) return operation_failure(state_error("required local writes have not completed"));
    return clear_marker();
}

Status cancel_unmodified_search(const Operation& operation)
{
    std::lock_guard<std::mutex> guard(state_mutex);
    if (!active.lock.valid() || active.failed || active.kind != "search" || operation.operation_id != active.id ||
        active.saved || active.advanced)
        return {Code::incomplete_operation, "unmodified active search required"};
    return clear_marker();
}
} // namespace sse_v1
