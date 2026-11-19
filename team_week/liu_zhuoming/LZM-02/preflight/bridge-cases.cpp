#include "fixtures.h"
#include "libpq/SSE_map.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unordered_map>

namespace sse { namespace sophos {
void writeDictionary(const std::map<std::string, int>&, const std::string&);
void writeDictionary(const std::unordered_map<std::string, int>&, const std::string&);
void appendDictionaryEntry(const std::string&, int, const std::string&);
}}
void writeDictionary(const std::map<std::string, int>&, const std::string&);
void writeDictionary(const std::unordered_map<std::string, int>&, const std::string&);
void appendDictionaryEntry(const std::string&, int, const std::string&);
void get_and_increment(const std::string&, std::map<std::string, int>&, std::string, int&);

using namespace sse_v1;
void check(bool success)
{
    if (!success) throw std::runtime_error("bridge assertion failed");
}
void ok(const Status& status)
{
    if (!status.ok()) throw std::runtime_error(std::string(code_name(status.code)) + ": " + status.message);
}
template<class Action>
void rejected(Action action)
{
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("write unexpectedly allowed");
}

int main(int argc, char** argv)
{
    if (argc != 3 || geteuid() == 0) return 2;
    const std::string dir(argv[1]), mode(argv[2]);
    check(setenv("SSE_V1_STATE_DIR", dir.c_str(), 1) == 0);
    Catalog catalog;
    catalog.database_oid = "123";
    catalog.instance_directory = "/fixture/bridge/instance";
    ok(set_catalog_reader([&](const std::string&, Catalog& out) {
        out = catalog;
        out.candidate_exists = !out.tables.empty();
        return Status{Code::ok, ""};
    }));
    ok(initialize_state("123", fixture_keys));
    Scope scope;
    bool registered;
    ok(local_scope("week_demo", scope, registered));
    const std::vector<sse_col> cols{{"ind",sse_kind::UNDEFINED},
        {"keyword",sse_kind::FIDES},{"payload",sse_kind::UNDEFINED}};
    sse_table_map = {{"week_demo", cols}};
    rejected([&] { appendTableMap("week_demo", cols, "/unused"); });
    check(fixture_read(dir + "/sse_table.mat").empty());
    Operation op;
    ok(begin_operation(scope, "create", op));
    catalog.tables.emplace("week_demo", CatalogTable{1,true});
    if (mode == "map-failure") {
        check(mkdir((dir + "/sse_table.mat.blocked").c_str(),0700)==0);
        check(rename((dir + "/sse_table.mat").c_str(),(dir + "/sse_table.mat.saved").c_str())==0);
        check(rename((dir + "/sse_table.mat.blocked").c_str(),(dir + "/sse_table.mat").c_str())==0);
        rejected([&] { table_map_write("/unused"); });
        check(access((dir + "/operation.v1").c_str(),F_OK)==0);
        check(complete_operation(op).code==Code::incomplete_operation);
        std::cout << "PASS BRIDGE-04: existing mapping entry propagates save failure and retains marker\n";
        return 0;
    }
    if (mode == "global-map") table_map_write("/unused");
    else if (mode == "whole-map") writeTableMap(sse_table_map,"/unused");
    else appendTableMap("week_demo",cols,"/unused");
    ok(complete_operation(op));
    std::cout << "PASS BRIDGE-01: existing mapping entry is guarded by create operation (" << mode << ")\n";
    std::map<std::string,int> counters;
    std::unordered_map<std::string,int> unordered;
    rejected([&] { sse::sophos::writeDictionary(counters,"/unused"); });
    rejected([&] { ::writeDictionary(unordered,"/unused"); });
    rejected([&] { ::appendDictionaryEntry("apple",1,"/unused"); });
    int counter = 91;
    rejected([&] { ::get_and_increment("/unused",counters,"apple",counter); });
    check(counters.empty() && counter==0 && fixture_read(dir+"/fides_client.mat").empty());
    ok(begin_operation(scope, "insert", op));
    if (mode == "counter-failure") {
        check(chmod(dir.c_str(),0500)==0);
        rejected([&] { sse::sophos::writeDictionary(std::map<std::string,int>{{"apple",3}},"/unused"); });
        check(chmod(dir.c_str(),0700)==0);
        check(fixture_read(dir+"/fides_client.mat").empty());
        check(access((dir+"/operation.v1").c_str(),F_OK)==0);
        check(complete_operation(op).code==Code::incomplete_operation);
        std::cout << "PASS BRIDGE-03: existing counter entry propagates unwritable directory error\n";
        return 0;
    }
    ok(increment_counter("apple",counter,false));
    check(counter==1 && fixture_read(dir+"/fides_client.mat").empty());
    sse::sophos::appendDictionaryEntry("apple",1,"/unused");
    ok(operation_counters(op,counters));
    check(counters.at("apple")==2);
    sse::sophos::writeDictionary(counters,"/unused");
    unordered.insert(counters.begin(),counters.end());
    sse::sophos::writeDictionary(unordered,"/unused");
    ::writeDictionary(counters,"/unused");
    ::writeDictionary(unordered,"/unused");
    ::appendDictionaryEntry("banana",1,"/unused");
    ::get_and_increment("/unused",counters,"apple",counter);
    check(counter==3);
    ok(complete_operation(op));
    check(preflight(scope,"apple").counter==3 && preflight(scope,"banana").counter==1);
    std::cout << "PASS BRIDGE-02: existing counter entries reject unlocked writes and persist active counters\n";
}
