#include "row_aead_v1.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace sse_v1;

namespace {

const Scope scope{"00112233445566778899aabbccddeeff", "16384", "public", "week_demo"};
const std::string rid_a(32, 'a'), rid_b(32, 'b'), rid_c(32, 'c');
const Row demo{"17", u8"苹果", std::string("two words\0\0", 11)};
const Row basics[] = {{"1", "apple", "red apple"}, {"2", "apple", "green apple"}, {"3", "banana", "yellow banana"}};
const std::string rids[] = {rid_a, rid_b, rid_c};
const std::string row_file = "/row_aead_v1.key";
const std::string locator_file = "/locator_aead_v1.key";
const std::string id_file = "/row_aead_v1.keyid";
unsigned checks = 0;

void check(bool condition, const std::string& label)
{
    if (!condition) throw std::runtime_error(label);
    ++checks;
    std::cout << "PASS " << label << '\n';
}

void expect(Status actual, Code expected, const std::string& label)
{
    if (actual.code != expected) {
        throw std::runtime_error(label + ": code " + std::to_string(static_cast<int>(actual.code)) +
                                 ", expected " + std::to_string(static_cast<int>(expected)) +
                                 " (" + actual.message + ")");
    }
    check(true, label);
}

bool equal(const Row& a, const Row& b)
{
    return a.ind == b.ind && a.keyword == b.keyword && a.payload == b.payload;
}

bool empty(const KeySet& keys)
{
    return keys.key_id.empty() && keys.row_key.empty() && keys.locator_key.empty();
}

void make_directory(const std::string& path)
{
    if (mkdir(path.c_str(), 0700) != 0) throw std::runtime_error("mkdir fixture failed");
}

void put(const std::string& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), bytes.size());
    out.close();
    if (!out || chmod(path.c_str(), 0600) != 0) throw std::runtime_error("write fixture failed");
}

std::string sequence(unsigned start, unsigned size)
{
    std::string bytes;
    for (unsigned i = 0; i < size; ++i) bytes.push_back(static_cast<char>(start + i));
    return bytes;
}

KeySet public_keys(const std::string& dir)
{
    make_directory(dir);
    put(dir + row_file, sequence(0, 32));
    put(dir + locator_file, sequence(32, 32));
    put(dir + id_file, sequence(0, 16));
    KeySet keys;
    auto result = load_keys(dir, keys);
    if (!result.ok()) throw std::runtime_error(result.message);
    return keys;
}

void key_cases(const std::string& folder, const KeySet& fixture)
{
    const auto fresh = folder + "/fresh";
    KeySet keys = fixture;
    expect(load_keys(fresh, keys), Code::key_invalid, "load absent directory refuses");
    check(empty(keys) && access(fresh.c_str(), F_OK) != 0, "absent load clears output and creates nothing");
    make_directory(fresh);
    expect(initialize_keys(fresh, keys), Code::ok, "generate keys explicitly");
    const KeySet generated = keys;
    check(keys.key_id.size() == 16 && keys.row_key.size() == 32 && keys.locator_key.size() == 32 &&
          keys.row_key != keys.locator_key, "generated key lengths and distinct purposes");
    for (const auto& file : {row_file, locator_file, id_file}) {
        struct stat info;
        check(stat((fresh + file).c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "0600 " + file);
    }
    expect(initialize_keys(fresh, keys), Code::state_invalid, "second initialization refuses");
    check(empty(keys), "failure leaves no key output");
    expect(load_keys(fresh, keys), Code::ok, "load after initialization");
    check(keys.key_id == generated.key_id && keys.row_key == generated.row_key &&
          keys.locator_key == generated.locator_key, "second initialization never overwrites");
    std::string blob;
    expect(encrypt_row(scope, rid_a, demo, keys, blob), Code::ok, "generated keys usable");
    Row out;
    expect(decrypt_row(scope, rid_a, blob, keys, out), Code::ok, "generated keys decrypt");
    check(equal(out, demo), "generated key roundtrip bytes");

    const int lock = open(fresh.c_str(), O_RDONLY | O_DIRECTORY);
    check(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0, "fixture holds key directory lock");
    expect(load_keys(fresh, keys), Code::busy, "load during initialization refuses");
    check(close(lock) == 0, "release fixture lock");

    check(unlink((fresh + locator_file).c_str()) == 0, "fixture remove locator key");
    expect(load_keys(fresh, keys), Code::key_invalid, "existing state missing key");
    expect(initialize_keys(fresh, keys), Code::state_invalid, "existing state cannot replace missing key");
    check(empty(keys) && access((fresh + locator_file).c_str(), F_OK) != 0, "missing key never auto-created");

    for (const unsigned n : {0, 16, 31, 33}) {
        put(fresh + locator_file, sequence(32, n));
        expect(load_keys(fresh, keys), Code::key_invalid, "reject key length " + std::to_string(n));
        check(empty(keys), "invalid key length clears output");
    }
    put(fresh + locator_file, sequence(32, 32));
    put(fresh + id_file, sequence(0, 15));
    expect(load_keys(fresh, keys), Code::key_invalid, "short key ID refused");
    put(fresh + id_file, sequence(0, 16));
    check(chmod((fresh + locator_file).c_str(), 0644) == 0, "fixture unsafe file mode");
    expect(load_keys(fresh, keys), Code::key_invalid, "unsafe file mode refused");
    check(unlink((fresh + locator_file).c_str()) == 0 &&
          symlink((fresh + row_file).c_str(), (fresh + locator_file).c_str()) == 0, "fixture symlink");
    expect(load_keys(fresh, keys), Code::key_invalid, "key symlink refused");
}

std::string seal_row(const Row& row, const std::string& rid, const KeySet& keys)
{
    std::string blob;
    const auto status = encrypt_row(scope, rid, row, keys, blob);
    if (!status.ok()) throw std::runtime_error(status.message);
    return blob;
}

std::string seal_locator(const std::string& rid, const KeySet& keys)
{
    std::string blob;
    const auto status = encrypt_locator(scope, {rid, 0}, keys, blob);
    if (!status.ok()) throw std::runtime_error(status.message);
    return blob;
}

void row_test(const Row& row, const std::string& rid, const KeySet& keys, const std::string& label)
{
    std::vector<unsigned char> bytes;
    expect(encode_row(row, bytes), Code::ok, label + " encode");
    Row decoded;
    expect(decode_row(bytes, decoded), Code::ok, label + " decode");
    const std::size_t length = 12 + row.ind.size() + row.keyword.size() + row.payload.size();
    check(equal(decoded, row) && bytes.size() == length, label + " codec exact bytes=" + std::to_string(length));
    const auto first = seal_row(row, rid, keys);
    const auto second = seal_row(row, rid, keys);
    check(first != second && first.substr(32, 12) != second.substr(32, 12) &&
          first.size() == 4 * ((length + 54 + 2) / 3) && second.size() == first.size(),
          label + " different ciphertexts Base64=" + std::to_string(first.size()));
    expect(decrypt_row(scope, rid, first, keys, decoded), Code::ok, label + " decrypt first");
    check(equal(decoded, row), label + " first exact");
    expect(decrypt_row(scope, rid, second, keys, decoded), Code::ok, label + " decrypt second");
    check(equal(decoded, row), label + " second exact");
}

void rows(const KeySet& keys)
{
    for (int i = 0; i < 3; ++i) row_test(basics[i], rids[i], keys, "CRYPTO-01 row " + std::to_string(i + 1));
    row_test({}, rid_a, keys, "CRYPTO-02 all fields empty");
    row_test(demo, rid_a, keys, "CRYPTO-02 Chinese, spaces and trailing NULs");
    for (const unsigned n : {15, 16, 17, 63, 64, 65}) {
        row_test({"", "", std::string(n, '\0')}, rid_a, keys, "CRYPTO-02 payload " + std::to_string(n));
    }
    row_test({std::string(16384, 'i'), std::string(1024, 'k'), std::string(16384, 'p')},
             rid_a, keys, "CRYPTO-02 all fields at limit");
    std::string blob = "old value";
    expect(encrypt_row(scope, rid_a, {"", "", std::string(16385, 'x')}, keys, blob),
           Code::invalid_input, "payload over 16384");
    check(blob.empty(), "overlength payload clears ciphertext");
    expect(encrypt_row(scope, rid_a, {"", std::string(1025, 'x'), ""}, keys, blob),
           Code::invalid_input, "keyword over 1024");
    std::vector<unsigned char> malformed(13, 0);
    Row out = demo;
    expect(decode_row(malformed, out), Code::protocol_invalid, "trailing bytes in serialized row");
    check(equal(out, {}), "bad codec does not emit partial fields");
}

char flip_b64(char c) { return c == 'A' ? 'B' : 'A'; }

std::string flip(const std::string& input, std::size_t at)
{
    auto out = input;
    out.at(at) = flip_b64(out.at(at));
    return out;
}

void reject_row(const Scope& context, const std::string& rid, const std::string& blob,
                const KeySet& keys, Code expected, const std::string& label)
{
    Row out = demo;
    expect(decrypt_row(context, rid, blob, keys, out), expected, label);
    check(equal(out, {}), label + " no plaintext");
}

void reject_locator(const Scope& context, const std::string& blob, const KeySet& keys,
                    Code expected, const std::string& label)
{
    Locator out{rid_a, 1};
    expect(decrypt_locator(context, blob, keys, out), expected, label);
    check(out.rid.empty() && out.operation == 0, label + " no locator");
}

void attacks(const KeySet& keys)
{
    std::string rid, second_rid;
    expect(new_rid(rid), Code::ok, "fresh rid");
    expect(new_rid(second_rid), Code::ok, "second fresh rid");
    check(rid.size() == 32 && rid != second_rid &&
          rid.find_first_not_of("0123456789abcdef") == std::string::npos, "rid is 16 random bytes as lowercase hex");
    const auto row = seal_row(demo, rid_a, keys);
    const auto loc = seal_locator(rid_a, keys);
    const auto repeat = seal_locator(rid_a, keys);
    check(loc.size() == 96 && loc != repeat && loc.find(rid_a + "|add") == std::string::npos,
          "CRYPTO-03 locator size, randomization and no plaintext rid|add");
    Locator recovered;
    expect(decrypt_locator(scope, repeat, keys, recovered), Code::ok, "re-encrypted locator authenticates");
    check(recovered.rid == rid_a && recovered.operation == 0, "locator exact rid and op=0");
    const auto another = seal_locator(rid_b, keys);
    expect(decrypt_locator(scope, another, keys, recovered), Code::ok, "v1 same-scope valid locator substitution");
    check(recovered.rid == rid_b, "v1 contract has no ut or expected rid in locator AAD");

    Scope other = scope;
    other.table = "another";
    reject_row(other, rid_a, row, keys, Code::auth_failed, "row cross-table");
    reject_locator(other, loc, keys, Code::auth_failed, "locator cross-table");
    other = scope;
    other.database_oid = "16385";
    reject_row(other, rid_a, row, keys, Code::auth_failed, "row cross-database");
    reject_locator(other, loc, keys, Code::auth_failed, "locator cross-database");
    other = scope;
    other.deployment_id[0] = '1';
    reject_row(other, rid_a, row, keys, Code::auth_failed, "row cross-deployment");
    reject_locator(other, loc, keys, Code::auth_failed, "locator cross-deployment");
    reject_row(scope, rid_b, row, keys, Code::auth_failed, "row cross-record");
    reject_row(scope, "", row, keys, Code::invalid_input, "row empty rid");
    std::string blob = "seed";
    expect(encrypt_locator(scope, {"", 0}, keys, blob), Code::invalid_input, "locator empty rid");
    check(blob.empty(), "locator error clears output");
    expect(encrypt_locator(scope, {rid_a, 1}, keys, blob), Code::invalid_input, "unsupported locator op");
    reject_locator(scope, row, keys, Code::protocol_invalid, "wrong locator purpose");
    reject_row(scope, rid_a, loc, keys, Code::protocol_invalid, "wrong row purpose");
    reject_row(scope, rid_a, flip(row, 6), keys, Code::protocol_invalid, "row version");
    reject_locator(scope, flip(loc, 6), keys, Code::protocol_invalid, "locator version");
    reject_row(scope, rid_a, flip(row, 7), keys, Code::protocol_invalid, "row purpose byte");
    reject_row(scope, rid_a, flip(row, 0), keys, Code::protocol_invalid, "row magic");
    reject_row(scope, rid_a, flip(row, 47), keys, Code::protocol_invalid, "row length field");
    reject_locator(scope, flip(loc, 47), keys, Code::protocol_invalid, "locator length field");
    reject_row(scope, rid_a, flip(row, 8), keys, Code::key_invalid, "row key_id");
    reject_locator(scope, flip(loc, 8), keys, Code::key_invalid, "locator key_id");
    reject_row(scope, rid_a, row.substr(0, row.size() - 4), keys, Code::protocol_invalid, "row truncated");
    reject_locator(scope, loc.substr(0, loc.size() - 4), keys, Code::protocol_invalid, "locator truncated");
    reject_row(scope, rid_a, row + "\n", keys, Code::protocol_invalid, "row Base64 whitespace");
    reject_locator(scope, loc.substr(0, 95) + "A", keys, Code::protocol_invalid, "locator invalid Base64 padding");
    for (const unsigned offset : {32, 56, 88}) {
        reject_row(scope, rid_a, flip(row, offset), keys, Code::auth_failed, "row tamper offset " + std::to_string(offset));
        reject_locator(scope, flip(loc, offset), keys, Code::auth_failed, "locator tamper offset " + std::to_string(offset));
    }
    reject_row(scope, rid_a, row, {}, Code::key_invalid, "row missing key");
    reject_locator(scope, loc, {}, Code::key_invalid, "locator missing key");
    auto wrong = keys;
    wrong.row_key[0] ^= 1;
    wrong.locator_key[0] ^= 1;
    reject_row(scope, rid_a, row, wrong, Code::auth_failed, "row wrong key same ID");
    reject_locator(scope, loc, wrong, Code::auth_failed, "locator wrong key same ID");
    wrong = keys;
    std::swap(wrong.row_key, wrong.locator_key);
    reject_row(scope, rid_a, row, wrong, Code::auth_failed, "row key isolation");
    reject_locator(scope, loc, wrong, Code::auth_failed, "locator key isolation");
}

void vectors(const std::string& path, const KeySet& keys)
{
    std::ifstream in(path);
    std::string first, second, line, name, aad, plain, blob;
    check(static_cast<bool>(std::getline(in, line)), "public vector file readable");
    int read = 0;
    while (std::getline(in, line)) {
        std::istringstream parts(line);
        if (!(parts >> name >> aad >> plain >> blob)) throw std::runtime_error("bad public vector");
        ++read;
        if (name == "row" || name == "row_repeat") {
            Row out;
            expect(decrypt_row(scope, rid_a, blob, keys, out), Code::ok, "known answer " + name);
            check(equal(out, demo), name + " exact plaintext");
            (name == "row" ? first : second) = blob;
        } else if (name == "locator") {
            Locator out;
            expect(decrypt_locator(scope, blob, keys, out), Code::ok, "known answer locator");
            check(out.rid == rid_a && out.operation == 0, "locator exact public vector");
        } else if (name == "row_bad_length") {
            reject_row(scope, rid_a, blob, keys, Code::protocol_invalid, "valid tag then malformed row");
        } else if (name == "locator_bad_op") {
            reject_locator(scope, blob, keys, Code::protocol_invalid, "valid tag then invalid locator op");
        } else {
            throw std::runtime_error("unrecognized public vector");
        }
    }
    check(read == 5 && !first.empty() && !second.empty() && first != second, "all 5 public vectors verified");
}

void rng_case(const std::string& dir, const KeySet& keys)
{
    const auto fresh = dir + "/rng";
    make_directory(fresh);
    KeySet fresh_keys = keys;
    expect(initialize_keys(fresh, fresh_keys), Code::crypto_failed, "key-generation RNG failure");
    check(empty(fresh_keys) && access((fresh + row_file).c_str(), F_OK) != 0,
          "RNG failure leaves no key file or output");
    std::string blob = "seed";
    expect(new_rid(blob), Code::crypto_failed, "rid RNG failure");
    check(blob.empty(), "rid RNG failure clears output");
    expect(encrypt_row(scope, rid_a, demo, keys, blob), Code::crypto_failed, "row nonce RNG failure");
    check(blob.empty(), "row RNG failure clears output");
    expect(encrypt_locator(scope, {rid_a, 0}, keys, blob), Code::crypto_failed, "locator nonce RNG failure");
    check(blob.empty(), "locator RNG failure clears output");
}

int damaged_last_row(const KeySet& keys)
{
    std::vector<Row> staged;
    for (unsigned i = 0; i < 3; ++i) {
        auto blob = seal_row(basics[i], rids[i], keys);
        if (i == 2) blob = flip(blob, blob.size() - 6);
        Row row;
        const auto status = decrypt_row(scope, rids[i], blob, keys, row);
        if (status.code == Code::auth_failed && i == 2 && equal(row, {})) {
            std::cerr << "auth_failed\n";
            return 17;
        }
        if (!status.ok()) return 2;
        staged.push_back(row);
    }
    for (const auto& row : staged) std::cout << row.ind << '\n';
    return 0;
}

Scope sql_scope()
{
    const char* oid = std::getenv("YZH_FIXTURE_DB_OID");
    if (!oid || !*oid) throw std::runtime_error("missing fixture database OID");
    Scope context = scope;
    context.database_oid = oid;
    return context;
}

void write_sql_fixture(const std::string& path, const KeySet& keys)
{
    const auto context = sql_scope();
    std::ofstream expected(path);
    std::cout << "CREATE TABLE public.week_demo_data (rid text PRIMARY KEY CHECK (rid ~ '^[0-9a-f]{32}$'),"
                 " data_blob text NOT NULL CHECK (octet_length(data_blob) BETWEEN 88 AND 65624));\n"
                 "CREATE TABLE public.week_demo_index (ut text PRIMARY KEY,"
                 " locator_blob text NOT NULL CHECK (octet_length(locator_blob) = 96));\n";
    for (unsigned i = 0; i < 3; ++i) {
        std::string blob;
        if (!encrypt_row(context, rids[i], basics[i], keys, blob).ok()) throw std::runtime_error("fixture encode row");
        std::cout << "INSERT INTO public.week_demo_data VALUES ('" << rids[i] << "','" << blob << "');\n";
        expected << "YZH:d\t" << rids[i] << '\t' << blob << '\n';
    }
    for (unsigned i = 0; i < 3; ++i) {
        std::string blob;
        if (!encrypt_locator(context, {rids[i], 0}, keys, blob).ok()) throw std::runtime_error("fixture encode locator");
        std::cout << "INSERT INTO public.week_demo_index VALUES ('fixture" << i << "','" << blob << "');\n";
        if (i == 0) {
            const auto first = blob;
            if (!encrypt_locator(context, {rids[i], 0}, keys, blob).ok() || blob == first) {
                throw std::runtime_error("fixture reseal locator");
            }
            std::cout << "DELETE FROM public.week_demo_index WHERE ut='fixture0';\n"
                         "INSERT INTO public.week_demo_index VALUES ('fixture0_new','" << blob << "');\n";
        }
        expected << "YZH:l\tfixture" << i << (i == 0 ? "_new" : "") << '\t' << blob << '\n';
    }
    expected.close();
    if (!expected) throw std::runtime_error("cannot write expected ciphertext file");
    std::cout << "SELECT 'YZH:d',rid,data_blob FROM public.week_demo_data ORDER BY rid;\n"
                 "SELECT 'YZH:l',ut,locator_blob FROM public.week_demo_index ORDER BY ut;\n";
}

void read_sql_fixture(const std::string& path, const KeySet& keys)
{
    const auto context = sql_scope();
    std::ifstream from_db(path);
    std::string kind, id, blob;
    for (unsigned i = 0; i < 3; ++i) {
        Row row;
        check(static_cast<bool>(from_db >> kind >> id >> blob) && kind == "YZH:d" && id == rids[i],
              "database read row " + std::to_string(i + 1));
        expect(decrypt_row(context, id, blob, keys, row), Code::ok, "database row authentication");
        check(equal(row, basics[i]), "database row recovers full original bytes");
    }
    for (unsigned i = 0; i < 3; ++i) {
        Locator locator;
        check(static_cast<bool>(from_db >> kind >> id >> blob) && kind == "YZH:l" &&
              id == "fixture" + std::to_string(i) + (i == 0 ? "_new" : "") && blob.size() == 96,
              "database read locator " + std::to_string(i + 1));
        expect(decrypt_locator(context, blob, keys, locator), Code::ok, "database locator authentication");
        check(locator.rid == rids[i] && locator.operation == 0, "database locator recovers rid and op");
    }
    check(!(from_db >> kind), "database returned only the six expected ciphertexts");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) return 2;
    try {
        const std::string mode = argc == 4 ? argv[3] : "";
        const std::string dir = argv[1];
        const auto keys = public_keys(dir + (mode.empty() ? "/public" : "/public" + mode));
        if (mode == "--sql-write") {
            write_sql_fixture(argv[2], keys);
            return 0;
        }
        if (mode == "--sql-read") {
            read_sql_fixture(argv[2], keys);
            return 0;
        }
        if (mode == "--last-row") return damaged_last_row(keys);
        if (mode == "--rng-failure") rng_case(dir, keys);
        else if (mode.empty()) {
            key_cases(dir, keys);
            rows(keys);
            attacks(keys);
            vectors(argv[2], keys);
        } else return 2;
        std::cout << "PASS total=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
