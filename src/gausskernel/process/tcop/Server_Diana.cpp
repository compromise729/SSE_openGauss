#include "Server_Diana.h"

namespace sse{
namespace diana{

DianaServer::DianaServer( )
{
}


bool DianaServer::get_unmask(uint8_t* key, update_token_type& ut, bool delete_key)
// bool DianaServer::get_unmask(uint8_t* key, std::string& index, bool delete_key)
{
    std::string        mask;

    gen_update_token_mask(key, ut, sizeof(std::string), reinterpret_cast<uint8_t*>(&mask));

    return true;
}

std::list<update_token_type> DianaServer::search( const SearchRequest& req, bool delete_results)
{
    std::list<update_token_type> results;

    auto callback = [&results](update_token_type ut) { results.push_back(ut); };

    search(req, callback, delete_results);

    return results;
}

void DianaServer::search(const SearchRequest& req, const basic_callback_type& post_callback, bool delete_results)
{
    // if the search request is empty, return immediately
    if (req.add_count == 0) {
        return;
    }

    auto eval_callback
        // cppcheck-suppress variableScope
        = [this, &post_callback, delete_results](uint64_t /*leaf_index*/, search_token_key_type st) {
              update_token_type ut;
              if (get_unmask(st.data(), ut, delete_results)) {
                  post_callback(ut);
              }
          };
    req.constrained_rcprf.eval_range(0, req.constrained_rcprf.max_leaf(), eval_callback);
    //这个方法会遍历树的叶子节点，并对每个节点应用 eval_callback。每次评估都将生成一个搜索令牌并调用 eval_callback。
}

DianaServer* init_DianaServer()
{
    return new DianaServer();
}

}//diana
}//sse
sse::diana::DianaServer* Diana_server = nullptr;