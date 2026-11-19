#include <iostream>
#include <memory>
#include <string>

#include "libpq-fe.h"
#include "libpq/Client_Sophos.h"
#include "sse/crypto/utils.hpp"

int main(int argc, char* argv[])
{
    if (argc != 3) return 2;
    sse::crypto::init_crypto_lib();
    std::unique_ptr<sse::sophos::SophosClient> fides(sse::sophos::init_SophosClient());
    const char* names[] = {"host", "port", "dbname", nullptr};
    const char* values[] = {argv[1], argv[2], "postgres", nullptr};
    std::unique_ptr<PGconn, decltype(&PQfinish)> conn(PQconnectdbParams(names, values, 0), PQfinish);
    if (!conn || PQstatus(conn.get()) != CONNECTION_OK) return 3;
    const auto token = fides->insertion_request("it's OR not", "1|add", true).token;
    const char* params[] = {token.c_str(), "1|add", "public-test-fixture"};
    std::unique_ptr<PGresult, decltype(&PQclear)> result(PQexecParams(conn.get(),
        "INSERT INTO public.zyc01_quoted_sample_index (ut,en_ind,join_col) VALUES ($1,$2,$3)",
        3, nullptr, params, nullptr, nullptr, 0), PQclear);
    if (!result || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << PQerrorMessage(conn.get());
        return 4;
    }
}