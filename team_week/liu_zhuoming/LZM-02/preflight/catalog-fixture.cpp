#include "fixtures.h"
#include "libpq/row_state_catalog.h"

#include <cstdlib>
#include <iostream>
#include <memory>

using namespace sse_v1;
namespace {
void require(const Status& s)
{
    if (!s.ok()) {
        std::cerr << code_name(s.code) << ": " << s.message << "\n";
        exit(1);
    }
}
void sql(PGconn* conn, const char* statement)
{
    const std::unique_ptr<PGresult, void(*)(PGresult*)> result(
        PQexecParams(conn, statement, 0, nullptr, nullptr, nullptr, nullptr, 0), PQclear);
    if (!result || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << "fixture SQL failed: " << PQerrorMessage(conn);
        exit(1);
    }
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 2 || !getenv("SSE_V1_STATE_DIR")) return 2;
    const std::unique_ptr<PGconn, void(*)(PGconn*)> conn(PQconnectdb(argv[1]), PQfinish);
    Catalog catalog;
    require(read_catalog(conn.get(), "", catalog));
    require(set_catalog_reader([&](const std::string& table, Catalog& seen) {
        return read_catalog(conn.get(), table, seen);
    }));
    require(initialize_state(catalog.database_oid, fixture_keys));
    Scope scope;
    bool registered;
    require(local_scope("week_demo", scope, registered));
    if (registered) return 1;
    Operation op;
    require(begin_operation(scope, "create", op));
    sql(conn.get(), "BEGIN");
    sql(conn.get(), "CREATE TABLE public.week_demo_data (rid text PRIMARY KEY, data_blob text NOT NULL)");
    sql(conn.get(), "CREATE TABLE public.week_demo_index (ut text PRIMARY KEY, locator_blob text NOT NULL)");
    sql(conn.get(), "CREATE TABLE public.sse_v1_catalog (logical_schema text NOT NULL, logical_table text NOT NULL,"
                   " protocol_version integer NOT NULL, PRIMARY KEY(logical_schema, logical_table))");
    sql(conn.get(), "INSERT INTO public.sse_v1_catalog VALUES ('public','week_demo',1)");
    sql(conn.get(), "COMMIT");
    require(register_table(scope, 1));
    require(complete_operation(op));
    require(validate_startup());
    require(begin_operation(scope, "insert", op));
    require(save_counters(op, {{"apple", 2}}));
    require(complete_operation(op));
    std::cout << "PASS CATALOG-01: same-connection table and version verified with isolated SQL shape fixture\n";
}
