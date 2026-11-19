// Independent verifier for synthetic row/locator envelopes, never prints keys.
#include "libpq/row_state_v1.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static std::string json(const std::string& s) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for (unsigned char c:s) {
        if(c=='"' || c=='\\') { out+='\\'; out+=c; }
        else if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=c;
    }
    return out+'"';
}
static void check(const sse_v1::Status& s) {
    if(!s.ok()) throw std::runtime_error(sse_v1::code_name(s.code));
}
int main(int argc,char** argv) {
    if(argc!=4 || (std::string(argv[3])!="row" && std::string(argv[3])!="locator")) return 2;
    try {
        if(setenv("SSE_V1_STATE_DIR",argv[1],1)) return 3;
        sse_v1::Scope scope; bool registered=false; sse_v1::KeySet keys;
        check(sse_v1::local_scope(argv[2],scope,registered));
        if(!registered) return 3;
        check(sse_v1::load_keys(argv[1],keys));
        std::string line;
        while(std::getline(std::cin,line)) {
            if(std::string(argv[3])=="locator") {
                sse_v1::Locator loc; check(sse_v1::decrypt_locator(scope,line,keys,loc));
                std::cout<<"{\"rid\":"<<json(loc.rid)<<",\"operation\":"<<unsigned(loc.operation)<<"}\n";
            } else {
                const auto tab=line.find('\t'); if(tab==std::string::npos) return 2;
                sse_v1::Row row; const auto rid=line.substr(0,tab);
                check(sse_v1::decrypt_row(scope,rid,line.substr(tab+1),keys,row));
                std::cout<<"{\"rid\":"<<json(rid)<<",\"ind\":"<<json(row.ind)
                         <<",\"keyword\":"<<json(row.keyword)<<",\"payload\":"<<json(row.payload)<<"}\n";
            }
        }
        return std::cin.bad()?3:0;
    } catch(const std::exception& e) { std::cerr<<"DECODE_STATUS="<<e.what()<<'\n'; return 3; }
}
