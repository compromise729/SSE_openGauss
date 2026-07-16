#pragma once
#include <../../sse/opensse-schemes/lib/include/sse/schemes/diana/diana_common.hpp>
#include <../../../sse/opensse-schemes/third_party/crypto/src/include/sse/crypto/rcprf.hpp>
#include <../../sse/opensse-schemes/third_party/crypto/src/include/sse/crypto/prf.hpp>
#include <list>

namespace sse{
namespace diana{


class DianaServer
{
public:
    static constexpr size_t kKeySize = 32;

    // Callback only taking the result as input
    using basic_callback_type = std::function<void(update_token_type)>;

    explicit DianaServer();

    std::list<update_token_type> search(const SearchRequest& req, bool delete_results = false);
    void  search(const SearchRequest& req, const basic_callback_type& post_callback, bool delete_results = false);

private:
    // bool get_unmask(uint8_t* key, std::string& index, bool delete_key);
    bool get_unmask(uint8_t* key, update_token_type& ut, bool delete_key);
};
DianaServer* init_DianaServer();

}//diana
}//sse
extern sse::diana::DianaServer* Diana_server;