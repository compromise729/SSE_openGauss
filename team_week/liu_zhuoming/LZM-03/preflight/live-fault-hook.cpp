#include "libpq/Client_Sophos.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <unistd.h>
extern "C" void sse_v1_request_event(const char* phase) {
    if (!std::getenv("STATE_TRACE")) return;
    const char prefix[] = "STATE_EVENT ";
    (void)!write(STDERR_FILENO,prefix,sizeof(prefix)-1);
    (void)!write(STDERR_FILENO,phase,strlen(phase));
    (void)!write(STDERR_FILENO,"\n",1);
}
namespace sse { namespace sophos {
new_SearchRequest SophosClient::search_request(const std::string& keyword) {
    using Function = new_SearchRequest(*)(SophosClient*,const std::string&);
    static const auto original = reinterpret_cast<Function>(dlsym(RTLD_NEXT,"_ZN3sse6sophos12SophosClient14search_requestERKSs"));
    if (!original) throw std::runtime_error("cannot resolve real Fides search generator");
    auto result=original(this,keyword);
    const char* fault=std::getenv("STATE_FAULT");
    if (fault && !strcmp(fault,"empty-token")) result.token.clear();
    if (fault && !strcmp(fault,"empty-derivation")) result.derivation_key.clear();
    return result;
}
}}
