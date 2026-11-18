#include "libpq/row_state_catalog.h"

#include <map>
#include <memory>
#include <string>

namespace sse_v1 {
namespace {
using Result = std::unique_ptr<PGresult, void(*)(PGresult*)>;

Result query(PGconn* conn, const char* sql, int count = 0, const char* const* params = nullptr)
{
    return Result(PQexecParams(conn, sql, count, nullptr, params, nullptr, nullptr, 0), PQclear);
}

bool rows(const Result& result)
{
    return result && PQresultStatus(result.get()) == PGRES_TUPLES_OK;
}

std::string field(const Result& result, int row, int col)
{
    return PQgetvalue(result.get(), row, col);
}

bool relation_shape(const std::map<std::string, std::map<std::string, std::string>>& columns,
                    const std::string& relation, const std::string& key, const std::string& value)
{
    const auto found = columns.find(relation);
    if (found == columns.end() || found->second.size() != 2) return false;
    const auto& fields = found->second;
    const auto k = fields.find(key), v = fields.find(value);
    return k != fields.end() && v != fields.end() && k->second == "r:25:t:t" && v->second == "r:25:t:f";
}
} // namespace

Status read_catalog(PGconn* conn, const std::string& candidate, Catalog& result)
{
    result = Catalog();
    if (!conn || PQstatus(conn) != CONNECTION_OK || !PQhost(conn) || PQhost(conn)[0] != '/')
        return {Code::state_invalid, "connected Unix socket required for state validation"};
    const auto identity = query(conn,
        "SELECT oid::text,current_setting('data_directory'),"
        " EXISTS (SELECT 1 FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n"
        " ON n.oid=c.relnamespace WHERE n.nspname='public' AND c.relname='sse_v1_catalog')"
        " FROM pg_catalog.pg_database WHERE datname=current_database()");
    if (!rows(identity) || PQntuples(identity.get()) != 1)
        return {Code::state_invalid, "could not read database identity"};
    Catalog catalog;
    catalog.database_oid = field(identity, 0, 0);
    catalog.instance_directory = field(identity, 0, 1);
    std::string target(candidate);
    if (field(identity, 0, 2) == "t") {
        const auto registry = query(conn,
            "SELECT logical_schema,logical_table,protocol_version::text FROM public.sse_v1_catalog LIMIT 2");
        if (!rows(registry)) return {Code::state_invalid, "could not read public.sse_v1_catalog"};
        const int size = PQntuples(registry.get());
        if (size > 1) return {Code::state_invalid, "only one v1 table per state environment is supported"};
        if (size == 1) {
            const auto table = field(registry, 0, 1);
            if (field(registry, 0, 0) != "public" || field(registry, 0, 2) != "1" ||
                table.empty() || (!target.empty() && target != table))
                return {Code::state_invalid, "server catalog scope or version mismatch"};
            target = table;
            catalog.tables.emplace(target, CatalogTable{1, false});
        }
    }
    if (!target.empty()) {
        const std::string data = target + "_data";
        const std::string index = target + "_index";
        const char* params[] = {target.c_str(), data.c_str(), index.c_str()};
        const auto physical = query(conn,
            "SELECT c.relname,c.relkind,a.attname,a.atttypid::text,a.attnotnull,"
            " EXISTS (SELECT 1 FROM pg_catalog.pg_constraint pk WHERE pk.conrelid=c.oid"
            " AND pk.contype='p' AND pk.conkey=ARRAY[a.attnum])"
            " FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace"
            " LEFT JOIN pg_catalog.pg_attribute a ON a.attrelid=c.oid AND a.attnum>0 AND NOT a.attisdropped"
            " WHERE n.nspname='public' AND c.relname IN ($1,$2,$3)", 3, params);
        if (!rows(physical)) return {Code::state_invalid, "could not verify physical table shape"};
        catalog.candidate_exists = PQntuples(physical.get()) != 0;
        std::map<std::string, std::map<std::string, std::string>> columns;
        bool legacy_relation = false;
        for (int i = 0; i < PQntuples(physical.get()); ++i) {
            const auto relation = field(physical, i, 0);
            if (relation == target) legacy_relation = true;
            columns[relation][field(physical, i, 2)] =
                field(physical, i, 1) + ":" + field(physical, i, 3) + ":" + field(physical, i, 4) + ":" + field(physical, i, 5);
        }
        if (!catalog.tables.empty()) {
            catalog.tables.begin()->second.physical_shape = !legacy_relation &&
                relation_shape(columns, data, "rid", "data_blob") &&
                relation_shape(columns, index, "ut", "locator_blob");
        }
    }
    result = std::move(catalog);
    return {Code::ok, ""};
}
} // namespace sse_v1
