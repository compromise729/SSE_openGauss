#include "row_aead_v1.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace sse_v1 {
namespace {

using Bytes = std::vector<unsigned char>;
enum class Kind : unsigned char { row = 1, locator = 2 };
constexpr std::size_t kKeyBytes = 32;
constexpr std::size_t kIdBytes = 16;
constexpr std::size_t kRidBytes = 16;
constexpr std::size_t kNonceBytes = 12;
constexpr std::size_t kTagBytes = 16;
constexpr std::size_t kIdOffset = 6;
constexpr std::size_t kNonceOffset = 22;
constexpr std::size_t kSizeOffset = 34;
constexpr std::size_t kHeader = 38;
constexpr std::size_t kOverhead = kHeader + kTagBytes;
constexpr std::size_t kLocatorPlain = 17;
constexpr std::size_t kRowPlainMin = 12;
constexpr char kRowKey[] = "row_aead_v1.key";
constexpr char kLocatorKey[] = "locator_aead_v1.key";
constexpr char kKeyId[] = "row_aead_v1.keyid";
constexpr char kMagic[] = "SSE1";
constexpr char kHex[] = "0123456789abcdef";

using Cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

Status error(Code code, const char* message) { return {code, message}; }
Status success() { return {Code::ok, {}}; }

struct Sensitive {
    Bytes bytes;
    ~Sensitive()
    {
        if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size());
    }
};

struct Fd {
    explicit Fd(int value) : value(value) {}
    ~Fd() { close_checked(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    bool close_checked()
    {
        if (value < 0) return true;
        const int fd = value;
        value = -1;
        return close(fd) == 0;
    }
    int value;
};

void clear_keys(KeySet& keys)
{
    if (!keys.row_key.empty()) OPENSSL_cleanse(keys.row_key.data(), keys.row_key.size());
    if (!keys.locator_key.empty()) OPENSSL_cleanse(keys.locator_key.data(), keys.locator_key.size());
    keys = {};
}

bool valid_keys(const KeySet& keys)
{
    return keys.key_id.size() == kIdBytes && keys.row_key.size() == kKeyBytes &&
           keys.locator_key.size() == kKeyBytes &&
           CRYPTO_memcmp(keys.row_key.data(), keys.locator_key.data(), kKeyBytes) != 0;
}

bool valid_directory(const std::string& path)
{
    // Refuse trailing slashes, which can bypass O_NOFOLLOW on the final component.
    return !path.empty() && path.back() != '/' && path.find('\0') == std::string::npos;
}

bool private_owned(int fd, bool directory)
{
    struct stat info;
    if (fstat(fd, &info) != 0 || info.st_uid != geteuid()) return false;
    if (directory) return S_ISDIR(info.st_mode) && (info.st_mode & 07777) == 0700;
    return S_ISREG(info.st_mode) && (info.st_mode & 07777) == 0600;
}

Status lock_directory(int fd, int type)
{
    if (flock(fd, type | LOCK_NB) == 0) return success();
    if (errno == EWOULDBLOCK) return error(Code::busy, "key directory is locked");
    return error(Code::state_invalid, "cannot lock key directory");
}

Status check_empty(int fd)
{
    const int copy = dup(fd);
    if (copy < 0) return error(Code::state_invalid, "cannot enumerate key directory");
    DIR* directory = fdopendir(copy);
    if (!directory) {
        close(copy);
        return error(Code::state_invalid, "cannot enumerate key directory");
    }
    bool empty = true;
    errno = 0;
    while (auto* entry = readdir(directory)) {
        if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    const int read_error = errno;
    if (closedir(directory) != 0 || read_error != 0) {
        return error(Code::state_invalid, "cannot read key directory");
    }
    return empty ? success() : error(Code::state_invalid, "key initialization requires empty state");
}

bool create_file(int dir, const char* name, const unsigned char* data, std::size_t size)
{
    Fd file(openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file.value < 0 || fchmod(file.value, 0600) != 0) return false;
    std::size_t written = 0;
    while (written < size) {
        const auto n = write(file.value, data + written, size - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        written += static_cast<std::size_t>(n);
    }
    const bool synced = fsync(file.value) == 0;
    return file.close_checked() && synced;
}

bool read_file(int dir, const char* name, std::size_t size, Sensitive& data)
{
    Fd file(openat(dir, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (file.value < 0 || !private_owned(file.value, false)) return false;
    data.bytes.resize(size + 1);
    std::size_t bytes_read = 0;
    while (bytes_read < data.bytes.size()) {
        const auto n = read(file.value, data.bytes.data() + bytes_read, data.bytes.size() - bytes_read);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return false;
        if (n == 0) break;
        bytes_read += static_cast<std::size_t>(n);
    }
    if (!file.close_checked() || bytes_read != size) return false;
    data.bytes.resize(size);
    return true;
}

void assign_keys(KeySet& keys, const Bytes& row, const Bytes& locator, const Bytes& id)
{
    keys = {std::string(id.begin(), id.end()), row, locator};
}

std::string hex(const unsigned char* data, std::size_t size)
{
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 15]);
    }
    return out;
}

bool decode_hex(const std::string& text, std::array<unsigned char, 16>& out)
{
    if (text.size() != 2 * out.size()) return false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char* high = std::strchr(kHex, text[2 * i]);
        const char* low = std::strchr(kHex, text[2 * i + 1]);
        if (!high || !low || !*high || !*low) return false;
        out[i] = static_cast<unsigned char>(((high - kHex) << 4) | (low - kHex));
    }
    return true;
}

bool valid_scope(const Scope& scope)
{
    std::array<unsigned char, 16> deployment;
    if (!decode_hex(scope.deployment_id, deployment) || scope.schema != "public" ||
        scope.table.empty() || scope.table.size() > 40 ||
        scope.table[0] < 'a' || scope.table[0] > 'z' ||
        scope.database_oid.empty() || scope.database_oid.size() > 10 ||
        scope.database_oid[0] == '0') return false;
    for (const char c : scope.table) {
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
    }
    std::uint64_t oid = 0;
    for (const char c : scope.database_oid) {
        if (c < '0' || c > '9') return false;
        oid = 10 * oid + (c - '0');
    }
    return oid <= UINT32_MAX;
}

void append_u32(Bytes& out, std::uint32_t n)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<unsigned char>(n >> shift));
    }
}

std::uint32_t read_u32(const unsigned char* bytes)
{
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n = (n << 8) | bytes[i];
    return n;
}

void append_field(Bytes& out, const std::string& field)
{
    append_u32(out, static_cast<std::uint32_t>(field.size()));
    out.insert(out.end(), field.begin(), field.end());
}

Bytes make_aad(const Bytes& envelope, const Scope& scope,
               const std::array<unsigned char, kRidBytes>* row_rid)
{
    Bytes aad(envelope.begin(), envelope.begin() + kHeader);
    append_field(aad, scope.deployment_id);
    append_field(aad, scope.database_oid);
    append_field(aad, scope.schema);
    append_field(aad, scope.table);
    if (row_rid) aad.insert(aad.end(), row_rid->begin(), row_rid->end());
    return aad;
}

bool encode_base64(const Bytes& bytes, std::string& out)
{
    const std::size_t size = 4 * ((bytes.size() + 2) / 3);
    std::string encoded(size + 1, '\0'); // EVP also writes the terminator.
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]),
                                  bytes.data(), static_cast<int>(bytes.size()));
    if (n < 0 || static_cast<std::size_t>(n) != size) return false;
    encoded.resize(size);
    out.swap(encoded);
    return true;
}

bool decode_base64(const std::string& input, Bytes& out)
{
    if (input.empty() || input.size() % 4 != 0) return false;
    std::size_t pad = 0;
    if (input.back() == '=') ++pad;
    if (input[input.size() - 2] == '=') ++pad;
    for (std::size_t i = 0; i < input.size() - pad; ++i) {
        const unsigned char c = input[i];
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
            !(c >= '0' && c <= '9') && c != '+' && c != '/') return false;
    }
    out.resize(input.size() / 4 * 3);
    const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(input.data()),
                                  static_cast<int>(input.size()));
    if (n < 0 || static_cast<std::size_t>(n) != out.size()) return false;
    out.resize(out.size() - pad);
    std::string canonical;
    return encode_base64(out, canonical) && canonical == input;
}

const Bytes& secret(const KeySet& keys, Kind kind)
{
    return kind == Kind::row ? keys.row_key : keys.locator_key;
}

Status seal(const Scope& scope, const std::array<unsigned char, kRidBytes>* row_rid,
            Kind kind, const Bytes& plain, const KeySet& keys, std::string& out)
{
    if (!valid_keys(keys)) return error(Code::key_invalid, "invalid encryption key set");
    Bytes envelope;
    envelope.reserve(kOverhead + plain.size());
    envelope.insert(envelope.end(), kMagic, kMagic + 4);
    envelope.push_back(kProtocolVersion);
    envelope.push_back(static_cast<unsigned char>(kind));
    envelope.insert(envelope.end(), keys.key_id.begin(), keys.key_id.end());
    envelope.resize(kSizeOffset);
    if (RAND_bytes(envelope.data() + kNonceOffset, kNonceBytes) != 1) {
        return error(Code::crypto_failed, "nonce generation failed");
    }
    append_u32(envelope, static_cast<std::uint32_t>(plain.size()));
    const Bytes aad = make_aad(envelope, scope, row_rid);
    envelope.resize(kOverhead + plain.size());

    Cipher ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    int ignored = 0, written = 0, tail = 0;
    auto* ciphertext = envelope.data() + kHeader;
    auto* tag = ciphertext + plain.size();
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, secret(keys, kind).data(),
                           envelope.data() + kNonceOffset) != 1 ||
        EVP_EncryptUpdate(ctx.get(), nullptr, &ignored, aad.data(), aad.size()) != 1 ||
        EVP_EncryptUpdate(ctx.get(), ciphertext, &written, plain.data(), plain.size()) != 1 ||
        written != static_cast<int>(plain.size()) ||
        EVP_EncryptFinal_ex(ctx.get(), tag, &tail) != 1 || tail != 0 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, kTagBytes, tag) != 1 ||
        !encode_base64(envelope, out)) return error(Code::crypto_failed, "AES-GCM encryption failed");
    return success();
}

Status open_envelope(const Scope& scope, const std::array<unsigned char, kRidBytes>* row_rid,
                     Kind kind, const std::string& blob, const KeySet& keys, Sensitive& clear)
{
    const bool row = kind == Kind::row;
    if ((row && (blob.size() < kRowBlobMin || blob.size() > kRowBlobMax)) ||
        (!row && blob.size() != kLocatorBlobSize)) {
        return error(Code::protocol_invalid, "invalid encoded envelope length");
    }
    Bytes envelope;
    if (!decode_base64(blob, envelope) || envelope.size() < kOverhead ||
        std::memcmp(envelope.data(), kMagic, 4) != 0 || envelope[4] != kProtocolVersion ||
        envelope[5] != static_cast<unsigned char>(kind)) {
        return error(Code::protocol_invalid, "invalid envelope encoding, version or purpose");
    }
    const std::size_t length = read_u32(envelope.data() + kSizeOffset);
    if (length != envelope.size() - kOverhead ||
        (row && (length < kRowPlainMin || length > kMaxPlainRowBytes)) ||
        (!row && length != kLocatorPlain)) return error(Code::protocol_invalid, "invalid ciphertext length");
    if (!valid_keys(keys) ||
        CRYPTO_memcmp(keys.key_id.data(), envelope.data() + kIdOffset, kIdBytes) != 0) {
        return error(Code::key_invalid, "missing or mismatched encryption key");
    }

    const Bytes aad = make_aad(envelope, scope, row_rid);
    Cipher ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    clear.bytes.resize(length + EVP_MAX_BLOCK_LENGTH);
    int ignored = 0, written = 0, tail = 0;
    auto* ciphertext = envelope.data() + kHeader;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, secret(keys, kind).data(),
                           envelope.data() + kNonceOffset) != 1 ||
        EVP_DecryptUpdate(ctx.get(), nullptr, &ignored, aad.data(), aad.size()) != 1 ||
        EVP_DecryptUpdate(ctx.get(), clear.bytes.data(), &written, ciphertext, length) != 1 ||
        written != static_cast<int>(length) ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kTagBytes, ciphertext + length) != 1) {
        return error(Code::crypto_failed, "AES-GCM decryption failed");
    }
    if (EVP_DecryptFinal_ex(ctx.get(), clear.bytes.data() + written, &tail) != 1) {
        return error(Code::auth_failed, "AES-GCM tag verification failed");
    }
    if (tail != 0) return error(Code::crypto_failed, "unexpected AES-GCM output");
    clear.bytes.resize(length);
    return success();
}

} // namespace

Status initialize_keys(const std::string& directory, KeySet& output)
{
    clear_keys(output);
    if (!valid_directory(directory)) return error(Code::invalid_input, "invalid state directory path");
    // State management explicitly creates this 0700 directory for a new environment.
    Fd dir(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dir.value < 0 || !private_owned(dir.value, true)) {
        return error(Code::state_invalid, "initialization requires a private 0700 state directory");
    }
    const auto locked = lock_directory(dir.value, LOCK_EX);
    if (!locked.ok()) return locked;
    const auto empty = check_empty(dir.value);
    if (!empty.ok()) return empty;
    Sensitive row, locator, id;
    row.bytes.resize(kKeyBytes);
    locator.bytes.resize(kKeyBytes);
    id.bytes.resize(kIdBytes);
    if (RAND_bytes(row.bytes.data(), kKeyBytes) != 1 ||
        RAND_bytes(locator.bytes.data(), kKeyBytes) != 1 ||
        RAND_bytes(id.bytes.data(), kIdBytes) != 1 ||
        CRYPTO_memcmp(row.bytes.data(), locator.bytes.data(), kKeyBytes) == 0) {
        return error(Code::crypto_failed, "key generation failed");
    }
    // Write the key ID last. An interrupted partial set is rejected by load_keys
    // and blocks future initialization; preserve the files for diagnosis.
    if (!create_file(dir.value, kRowKey, row.bytes.data(), kKeyBytes) ||
        !create_file(dir.value, kLocatorKey, locator.bytes.data(), kKeyBytes) ||
        !create_file(dir.value, kKeyId, id.bytes.data(), kIdBytes) ||
        fsync(dir.value) != 0 || !dir.close_checked()) {
        return error(Code::state_invalid, "key set creation incomplete");
    }
    assign_keys(output, row.bytes, locator.bytes, id.bytes);
    return success();
}

Status load_keys(const std::string& directory, KeySet& output)
{
    clear_keys(output);
    if (!valid_directory(directory)) return error(Code::invalid_input, "invalid state directory path");
    Fd dir(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dir.value < 0 || !private_owned(dir.value, true)) {
        return error(Code::key_invalid, "missing or unsafe key directory");
    }
    const auto locked = lock_directory(dir.value, LOCK_SH);
    if (!locked.ok()) return locked;
    Sensitive row, locator, id;
    if (!read_file(dir.value, kKeyId, kIdBytes, id) ||
        !read_file(dir.value, kRowKey, kKeyBytes, row) ||
        !read_file(dir.value, kLocatorKey, kKeyBytes, locator) ||
        CRYPTO_memcmp(row.bytes.data(), locator.bytes.data(), kKeyBytes) == 0 ||
        !dir.close_checked()) return error(Code::key_invalid, "missing, malformed or unsafe key file");
    assign_keys(output, row.bytes, locator.bytes, id.bytes);
    return success();
}

Status new_rid(std::string& output)
{
    output.clear();
    std::array<unsigned char, kRidBytes> random;
    if (RAND_bytes(random.data(), random.size()) != 1) {
        return error(Code::crypto_failed, "rid generation failed");
    }
    output = hex(random.data(), random.size());
    return success();
}

Status encode_row(const Row& row, Bytes& output)
{
    output.clear();
    if (row.ind.size() > kMaxFieldBytes || row.keyword.size() > kMaxKeywordBytes ||
        row.payload.size() > kMaxFieldBytes) {
        return error(Code::invalid_input, "row field exceeds byte limit");
    }
    output.reserve(kRowPlainMin + row.ind.size() + row.keyword.size() + row.payload.size());
    append_field(output, row.ind);
    append_field(output, row.keyword);
    append_field(output, row.payload);
    return success();
}

Status decode_row(const Bytes& bytes, Row& output)
{
    output = {};
    if (bytes.size() < kRowPlainMin || bytes.size() > kMaxPlainRowBytes) {
        return error(Code::protocol_invalid, "invalid encoded row length");
    }
    const std::size_t limits[] = {kMaxFieldBytes, kMaxKeywordBytes, kMaxFieldBytes};
    std::size_t offsets[3], lengths[3], offset = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (bytes.size() - offset < 4) return error(Code::protocol_invalid, "truncated field length");
        lengths[i] = read_u32(bytes.data() + offset);
        offset += 4;
        if (lengths[i] > limits[i] || lengths[i] > bytes.size() - offset) {
            return error(Code::protocol_invalid, "invalid encoded field length");
        }
        offsets[i] = offset;
        offset += lengths[i];
    }
    if (offset != bytes.size()) return error(Code::protocol_invalid, "trailing bytes in row");
    const auto* text = reinterpret_cast<const char*>(bytes.data());
    output = {std::string(text + offsets[0], lengths[0]), std::string(text + offsets[1], lengths[1]),
              std::string(text + offsets[2], lengths[2])};
    return success();
}

Status encrypt_row(const Scope& scope, const std::string& rid, const Row& row,
                   const KeySet& keys, std::string& blob)
{
    blob.clear();
    std::array<unsigned char, kRidBytes> raw;
    if (!valid_scope(scope) || !decode_hex(rid, raw)) return error(Code::invalid_input, "invalid scope or rid");
    Sensitive clear;
    const auto status = encode_row(row, clear.bytes);
    if (!status.ok()) return status;
    return seal(scope, &raw, Kind::row, clear.bytes, keys, blob);
}

Status decrypt_row(const Scope& scope, const std::string& rid, const std::string& blob,
                   const KeySet& keys, Row& output)
{
    output = {};
    std::array<unsigned char, kRidBytes> raw;
    if (!valid_scope(scope) || !decode_hex(rid, raw)) return error(Code::invalid_input, "invalid scope or rid");
    Sensitive clear;
    const auto status = open_envelope(scope, &raw, Kind::row, blob, keys, clear);
    if (!status.ok()) return status;
    return decode_row(clear.bytes, output);
}

Status encrypt_locator(const Scope& scope, const Locator& locator, const KeySet& keys,
                       std::string& blob)
{
    blob.clear();
    std::array<unsigned char, kRidBytes> raw;
    if (!valid_scope(scope) || !decode_hex(locator.rid, raw) || locator.operation != 0) {
        return error(Code::invalid_input, "invalid scope, rid or locator operation");
    }
    Sensitive clear;
    clear.bytes.assign(raw.begin(), raw.end());
    clear.bytes.push_back(locator.operation);
    return seal(scope, nullptr, Kind::locator, clear.bytes, keys, blob);
}

Status decrypt_locator(const Scope& scope, const std::string& blob, const KeySet& keys,
                       Locator& output)
{
    output = {};
    if (!valid_scope(scope)) return error(Code::invalid_input, "invalid scope");
    Sensitive clear;
    const auto status = open_envelope(scope, nullptr, Kind::locator, blob, keys, clear);
    if (!status.ok()) return status;
    if (clear.bytes[kRidBytes] != 0) return error(Code::protocol_invalid, "unsupported locator operation");
    output = {hex(clear.bytes.data(), kRidBytes), 0};
    return success();
}

} // namespace sse_v1
