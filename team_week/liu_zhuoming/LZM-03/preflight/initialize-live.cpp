#include "libpq/row_state_catalog.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sys/stat.h>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (mkdir(argv[2], 0700) != 0 || setenv("SSE_V1_STATE_DIR",argv[2],1) != 0) return 3;
    std::unique_ptr<PGconn, decltype(&PQfinish)> c(PQconnectdb(argv[1]), PQfinish);
    if (!c || PQstatus(c.get()) != CONNECTION_OK) return 3;
    sse_v1::Catalog catalog;
    auto status = sse_v1::read_catalog(c.get(),"",catalog);
    if (status.ok()) status = sse_v1::set_catalog_reader([&](const std::string& table,sse_v1::Catalog& out) {
        return sse_v1::read_catalog(c.get(),table,out);
    });
    if (status.ok()) status = sse_v1::initialize_state(catalog.database_oid,sse_v1::initialize_keys);
    std::cout << "INITIALIZE_STATUS=" << sse_v1::code_name(status.code) << '\n';
    if (!status.ok()) std::cerr << status.message << '\n';
    return status.ok() ? 0 : 3;
}
