#include "libpq/row_state_catalog.h"
#include "libpq/Client_Sophos.h"
#include "sse/crypto/utils.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
void ok(const sse_v1::Status& s){if(!s.ok())throw std::runtime_error(s.message);}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try {
        setenv("SSE_V1_STATE_DIR",argv[2],1);sse::crypto::init_crypto_lib();
        std::unique_ptr<PGconn,decltype(&PQfinish)> c(PQconnectdb(argv[1]),PQfinish);
        if(PQstatus(c.get())!=CONNECTION_OK)return 3;
        ok(sse_v1::set_catalog_reader([&](const std::string&t,sse_v1::Catalog&out){return sse_v1::read_catalog(c.get(),t,out);}));
        sse_v1::Scope scope;bool registered;ok(sse_v1::local_scope("week_demo",scope,registered));
        sse_v1::Operation op;ok(sse_v1::begin_operation(scope,"search",op));
        std::unique_ptr<sse::sophos::SophosClient> client(sse::sophos::init_SophosClient());
        Sophos_client=client.get();
        PGresult* r=PQexec(c.get(),"fides_setup;");if(!r||PQresultStatus(r)!=PGRES_TUPLES_OK)return 3;PQclear(r);
        auto req=client->search_request("apple");
        const auto sql="fides_v1_select * from public.week_demo_index where token="+req.token+" and derivation_key="+req.derivation_key+" and add_count="+std::to_string(req.add_count)+";";
        // Deliberately omit the current request's enclosing transaction: old control only.
        r=PQexec(c.get(),sql.c_str());if(!r||PQresultStatus(r)!=PGRES_TUPLES_OK||PQntuples(r)!=2)return 3;
        std::string uts="{",locs="{";
        for(int i=0;i<2;++i){if(i){uts+=',';locs+=',';}uts+='"'+std::string(PQgetvalue(r,i,0))+'"';locs+='"'+std::string(PQgetvalue(r,i,1))+'"';}
        uts+='}';locs+='}';PQclear(r);
        const char* values[]={uts.c_str(),locs.c_str()};
        r=PQexecParams(c.get(),"SELECT public.sse_v1_reinsert('public','week_demo',$1::text[],$2::text[])",2,nullptr,values,nullptr,nullptr,0);
        const bool failed=r&&PQresultStatus(r)==PGRES_FATAL_ERROR;PQclear(r);
        if(!failed)return 3;
        r=PQexec(c.get(),"SELECT count(*) FROM public.week_demo_index");
        const bool empty=r&&PQresultStatus(r)==PGRES_TUPLES_OK&&std::string(PQgetvalue(r,0,0))=="0";PQclear(r);
        std::cout<<"LEGACY_INDEX_BEFORE=2 LEGACY_INDEX_AFTER="<<(empty?0:-1)<<" REINSERT_FAILED=1 MARKER_RETAINED=1\n";
        return empty?0:3;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 3;}
}
