// Test-only libpq boundary recorder. Enabled only for public fixture runs.
#include "libpq-fe.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>

static std::string json(const std::string& s) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for (unsigned char c:s) {
        if (c=='"' || c=='\\') { out+='\\'; out+=c; }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=c;
    }
    return out+'"';
}
static void record(const char* sql, int count, const char* const* values, PGresult* result) {
    const char* path=std::getenv("ZYC_TRACE_FILE");
    if (!path || !std::getenv("ZYC_PUBLIC_FIXTURE")) return;
    std::string query=sql?sql:"";
    const auto start=query.find("derivation_key=");
    if (start!=std::string::npos) {
        const auto value=start+std::strlen("derivation_key=");
        const auto end=query.find(" and add_count=",value);
        query.replace(value,end==std::string::npos?std::string::npos:end-value,"<redacted>");
    }
    std::string line="{\"sql\":"+json(query)+",\"params\":[";
    for (int i=0;i<count;++i) { if(i) line+=','; line+=values&&values[i]?json(values[i]):"null"; }
    line+="],\"status\":"+json(result?PQresStatus(PQresultStatus(result)):"NULL")+",\"rows\":[";
    const bool public_result=result && PQresultStatus(result)==PGRES_TUPLES_OK && PQnfields(result)==2 &&
        ((!std::strcmp(PQfname(result,0),"ut") && !std::strcmp(PQfname(result,1),"locator_blob")) ||
         (!std::strcmp(PQfname(result,0),"rid") && !std::strcmp(PQfname(result,1),"data_blob")));
    if (public_result) for(int r=0;r<PQntuples(result);++r) {
        if(r) line+=',';
        line+='['; line+=json(PQgetvalue(result,r,0)); line+=','; line+=json(PQgetvalue(result,r,1)); line+=']';
    }
    line+="]}\n";
    const int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0) _exit(94);
    const char* p=line.data(); std::size_t left=line.size();
    while(left) { const auto n=write(fd,p,left); if(n<=0) _exit(94); p+=n; left-=n; }
    if(close(fd)) _exit(94);
}
extern "C" PGresult* PQexec(PGconn* c,const char* sql) {
    using Fn=PGresult*(*)(PGconn*,const char*);
    static const auto original=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"PQexec"));
    if(!original) _exit(95);
    auto* result=original(c,sql); record(sql,0,nullptr,result); return result;
}
extern "C" PGresult* PQexecParams(PGconn* c,const char* sql,int n,const Oid* types,
                                  const char* const* values,const int* lengths,const int* formats,int format) {
    using Fn=PGresult*(*)(PGconn*,const char*,int,const Oid*,const char* const*,const int*,const int*,int);
    static const auto original=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"PQexecParams"));
    if(!original) _exit(95);
    auto* result=original(c,sql,n,types,values,lengths,formats,format); record(sql,n,values,result); return result;
}
