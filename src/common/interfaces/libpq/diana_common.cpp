#include <../../../../sse/opensse-schemes/lib/include/sse/schemes/diana/diana_common.hpp>

namespace sse {
namespace diana {

void gen_update_token_mask(uint8_t*           search_token,
                           update_token_type& update_token,
                           const size_t       mask_len,
                           uint8_t*           mask)
{
    crypto::Prg prg((crypto::Key<crypto::Prg::kKeySize>(search_token)));

    prg.derive(0, kUpdateTokenSize, update_token.data());
    prg.derive(kUpdateTokenSize, mask_len, mask);
}

} //diana
} //sse