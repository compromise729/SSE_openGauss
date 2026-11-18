#ifndef SSE_V1_CONTRACT_HPP
#define SSE_V1_CONTRACT_HPP
#include <cstdint>
#include <string>
#include <vector>
namespace sse_v1 {
constexpr unsigned kProtocolVersion = 1;
constexpr std::size_t kMaxFieldBytes = 16384;
constexpr std::size_t kMaxKeywordBytes = 1024;
constexpr std::size_t kMaxBatchRows = 100;
constexpr std::size_t kMaxPlainRowBytes = 49164;
enum class Code { ok, unsupported, invalid_input, state_invalid, unknown_keyword,
    key_invalid, crypto_failed, auth_failed, protocol_invalid, db_failed,
    incomplete_operation, busy, inconsistent_result };
struct Status {
    Code code;
    std::string message;
    bool ok() const { return code == Code::ok; }
};
struct Scope { std::string deployment_id, database_oid, schema, table; };
struct Row { std::string ind, keyword, payload; };
struct Locator { std::string rid; std::uint8_t operation; };
struct KeySet { std::string key_id; std::vector<unsigned char> row_key, locator_key; };
struct EncodedRow { std::string rid, data_blob; };
struct IndexEntry { std::string ut, locator_blob; };
struct SearchInput { std::string token, derivation_key; int add_count; };
struct Preflight { Status status; bool keyword_registered; int counter; };
struct Operation { std::string operation_id; };
// YZH-01 owns definitions. All outputs are cleared on entry and on failure.
Status initialize_keys(const std::string& directory, KeySet& output);
Status load_keys(const std::string& directory, KeySet& output);
Status new_rid(std::string& output);
Status encode_row(const Row& row, std::vector<unsigned char>& output);
Status decode_row(const std::vector<unsigned char>& bytes, Row& output);
Status encrypt_row(const Scope&, const std::string& rid, const Row&, const KeySet&, std::string& blob);
Status decrypt_row(const Scope&, const std::string& rid, const std::string& blob, const KeySet&, Row&);
Status encrypt_locator(const Scope&, const Locator&, const KeySet&, std::string& blob);
Status decrypt_locator(const Scope&, const std::string& blob, const KeySet&, Locator&);
// LZM-02 owns definitions and holds a directory lock through completion or failure.
Preflight preflight(const Scope&, const std::string& keyword);
Status begin_operation(const Scope&, const std::string& kind, Operation&);
Status complete_operation(const Operation&);
// On failure caller stops. Marker survives, OS lock is released on client exit.
Status register_table(const Scope&, unsigned version);
Status load_table_version(const Scope&, unsigned& version);
// ZYC-03 owns the v1 filter. Unique identity is complete rid, never ciphertext.
Status filter_insert_only(const std::vector<Locator>&, std::vector<std::string>& rids);
} // namespace sse_v1
#endif
