#pragma once
#include <../../../sse/opensse-schemes/lib/include/sse/schemes/diana/diana_common.hpp>
#include <../../../sse/opensse-schemes/third_party/crypto/src/include/sse/crypto/rcprf.hpp>
#include <list>
#include <map>
#include <../../../sse/opensse-schemes/third_party/crypto/src/include/sse/crypto/prf.hpp>
#include "sse_common.h"

namespace sse {
namespace diana {

using diana_UpdateRequest = UpdateRequest<std::string>;

extern const char* MASTER_KEY_FILE ;
extern const char* KW_TOKEN_MASTER_KEY_FILE ;

extern const char* Diana_count ;

class DianaClient
{
public:
    static constexpr size_t kKeywordIndexSize = 16;
    static constexpr size_t kKeySize          = 32;

    using keyword_index_type = std::array<uint8_t, kKeywordIndexSize>;
    std::string index_type ;

    static constexpr size_t kTreeDepth = 48;

    // issue with cppcheck on Xenial
    // cppcheck-suppress noExplicitConstructor
    DianaClient(crypto::Key<kKeySize>&& derivation_master_key, crypto::Key<kKeySize>&& kw_token_master_key);
    ~DianaClient();

    keyword_index_type get_keyword_index(const std::string& kw) const;

    diana_UpdateRequest insertion_request(const std::string& keyword, const std::string   index);
    SearchRequest search_request(const std::string& keyword) ;
    void search_request(const std::string& keyword, keyword_token_type&token, keyword_index_type& kw_index, uint32_t& kw_counter);
    // std::list<diana_UpdateRequest> bulk_insertion_request(const std::list<std::pair<std::string, std::string>>& update_list);

    // const crypto::Prf<kSearchTokenKeySize>& root_prf() const;
    // const crypto::Prf<kKeywordTokenSize>&   kw_token_prf() const;

private:
    // std::list<std::tuple<std::string, std::string, uint32_t>> get_counters_and_increment( const std::list<std::pair<std::string, std::string>>& update_list);
    crypto::Prf<kSearchTokenKeySize> root_prf_;
    crypto::Prf<kKeywordTokenSize>   kw_token_prf_;
    std::map<std::string, int> counter_map_;
};

void init_DianaClient_key();

DianaClient* init_DianaClient();

}//diana
}//sse
extern sse::diana::DianaClient* Diana_client;