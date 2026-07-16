#include "libpq/Client_Diana.h"
#include "libpq/sse_common.h"

namespace sse {
namespace diana {

std::string storage_dir_path  = "/opt/og/openGauss-server/sse_storage";

const char* MASTER_KEY_FILE         = "master_derivation.key";
const char* KW_TOKEN_MASTER_KEY_FILE = "kw_token_master.key";

const char* Diana_count         = "diana_client.mat";

DianaClient::DianaClient(crypto::Key<kKeySize>&& derivation_master_key, crypto::Key<kKeySize>&& kw_token_master_key)
    : root_prf_(std::move(derivation_master_key)),
      kw_token_prf_(std::move(kw_token_master_key))
{
    counter_map_ = loadDictionary("/opt/og/openGauss-server/sse_storage/diana_client.mat");
}

DianaClient::~DianaClient()
{
}

DianaClient::keyword_index_type DianaClient::get_keyword_index(const std::string& kw) const
{
    std::string hash_string = crypto::Hash::hash(kw);

    keyword_index_type ret;
    std::copy_n(hash_string.begin(), kKeywordIndexSize, ret.begin());

    return ret;
}

diana_UpdateRequest DianaClient::insertion_request(const std::string& keyword, const std::string   index)
{
    diana_UpdateRequest req;
    search_token_key_type st;
    // std::string mask;
    uint64_t  mask;
    uint64_t u64index= std::stoull(index);
    // get (and possibly construct) the keyword index
    keyword_index_type kw_index = get_keyword_index(keyword);

    // retrieve the counter
    int kw_counter=0;

    std::string map_path   = storage_dir_path + "/" + Diana_count;
    get_and_increment(map_path, counter_map_, keyword, kw_counter);

    sse::crypto::RCPrf<kKeySize> rcprf_root(root_prf_.derive_key(kw_index.data(), kw_index.size()),  kTreeDepth);

    st = rcprf_root.eval(kw_counter);

    gen_update_token_mask(st, req.token, mask);

    req.index = std::to_string(xor_mask(u64index, mask));

    return req;
}

SearchRequest DianaClient::search_request(const std::string& keyword)
{
    keyword_index_type kw_index = get_keyword_index(keyword);

    bool     found;
    uint32_t kw_counter=0;
    // SearchRequest req;

    auto iter = counter_map_.find(keyword);
	if (iter != counter_map_.end()) {
		found = true;
		kw_counter = iter->second;
	}
	else {
		found = false;
	}

    if (!found) {
        return SearchRequest( {{}}, crypto::ConstrainedRCPrf<kSearchTokenKeySize>({}), 0);
    }
    // else {
    uint32_t add_count = kw_counter + 1;

    // Compute the root of the tree attached to kw_index
    crypto::RCPrf<kKeySize> rcprf_root(root_prf_.derive_key(kw_index.data(), kw_index.size()), kTreeDepth);

    return SearchRequest(kw_token_prf_.prf(kw_index), rcprf_root.constrain(0, kw_counter) , add_count);
}

void DianaClient::search_request(const std::string& keyword, keyword_token_type&token, keyword_index_type& kw_index, uint32_t& kw_counter)
{
    kw_index = get_keyword_index(keyword);

    bool     found;
    kw_counter=0;
    // SearchRequest req;

    auto iter = counter_map_.find(keyword);
	if (iter != counter_map_.end()) {
		found = true;
		kw_counter = iter->second;
	}
	else {
		found = false;
	}

    if (!found) {
        kw_counter=0;
        token=kw_token_prf_.prf(kw_index);
    }

}

DianaClient* init_DianaClient()
{
	// try to initialize everything from this directory
    std::string master_key_path  = storage_dir_path + "/" + MASTER_KEY_FILE;
    std::string kw_token_master_key_path = storage_dir_path + "/" + KW_TOKEN_MASTER_KEY_FILE;

    std::ifstream     master_key_in(master_key_path.c_str());
    std::ifstream     kw_token_key_in(kw_token_master_key_path.c_str());

    std::stringstream master_key_buf;
    std::stringstream kw_token_key_buf;

    master_key_buf << master_key_in.rdbuf();
    kw_token_key_buf << kw_token_key_in.rdbuf();

	std::array<uint8_t, 32> client_master_key_array;
    std::array<uint8_t, 32> client_kw_token_key_array;

    if (master_key_buf.str().size() != client_master_key_array.size()) {
        throw std::runtime_error(
            "Invalid master key size when constructing the Sophos client: "
            + std::to_string(master_key_buf.str().size())
            + " bytes instead of 32");
    }
    if (kw_token_key_buf.str().size() != client_kw_token_key_array.size()) {
        throw std::runtime_error(
            "Invalid PRG key size when constructing the Sophos client: "
            + std::to_string(kw_token_key_buf.str().size())
            + " bytes instead of 32");
    }
    auto master_key_str  = master_key_buf.str();
    auto kw_token_key_str = kw_token_key_buf.str();

    std::copy(master_key_str.begin(),
              master_key_str.end(),
              client_master_key_array.begin());
    std::copy(kw_token_key_str.begin(),
              kw_token_key_str.end(),
              client_kw_token_key_array.begin());

	return new DianaClient(sse::crypto::Key<DianaClient::kKeySize>(client_master_key_array.data()), sse::crypto::Key<DianaClient::kKeySize>(client_kw_token_key_array.data()));
}

void init_DianaClient_key()
{
    std::string master_key_path   = storage_dir_path + "/" + MASTER_KEY_FILE;
    std::string kw_token_master_key_path= storage_dir_path + "/" + KW_TOKEN_MASTER_KEY_FILE;

    std::array<uint8_t, DianaClient::kKeySize> master_derivation_key
        = sse::crypto::random_bytes<uint8_t, DianaClient::kKeySize>();
    std::array<uint8_t, DianaClient::kKeySize> kw_token_master_key
        = sse::crypto::random_bytes<uint8_t, DianaClient::kKeySize>();
    
    std::ofstream master_key_out(master_key_path.c_str());
    if (!master_key_out.is_open()) {
        throw std::runtime_error(
            master_key_path + ": unable to write the master derivation key");
    }

    master_key_out << std::string(master_derivation_key.begin(),
                                  master_derivation_key.end());
    master_key_out.close();

    std::ofstream kw_token_key_out(kw_token_master_key_path.c_str());
    if (!kw_token_key_out.is_open()) {
        throw std::runtime_error(
            kw_token_master_key_path
            + ": unable to write the master derivation key");
    }

    kw_token_key_out << std::string(kw_token_master_key.begin(),
                                    kw_token_master_key.end());
    kw_token_key_out.close();
}


}//diana
}//sse
extern sse::diana::DianaClient* Diana_client=nullptr;