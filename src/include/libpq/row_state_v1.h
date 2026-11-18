#ifndef ROW_STATE_V1_H
#define ROW_STATE_V1_H

#include "sse_v1_contract.hpp"
#include <functional>
#include <map>

namespace sse_v1 {
// Supplied by the client on its current connection. For tests, use explicit
// fixture data. A missing catalog can be represented only as an empty catalog.
struct CatalogTable {
    unsigned version;
    bool physical_shape;
};
struct Catalog {
    std::string database_oid;
    std::string instance_directory;
    std::map<std::string, CatalogTable> tables; // public logical tables
    bool candidate_exists = false; // logical name or either physical object
};
using CatalogReader = std::function<Status(const std::string&, Catalog&)>;
using Counters = std::map<std::string, int>;
using KeyInitializer = std::function<Status(const std::string&, KeySet&)>;

// Refresh the reader after connecting and before using a different connection.
Status set_catalog_reader(const CatalogReader&);
Status validate_local_startup();
Status validate_startup();
Status local_scope(const std::string& table, Scope&, bool& table_registered);
const char* code_name(Code);

// Explicit setup only. The caller provides the SHY-02 key initializer.
// No startup or preflight path invokes it implicitly.
Status initialize_state(const std::string& database_oid, const KeyInitializer&);
Status operation_counters(const Operation&, Counters&);
// Request adapters construct the Fides object only after begin succeeds.
Status current_operation_counters(Counters&);
bool v1_state_selected();
Status increment_counter(const std::string&, int&, bool persist);
Status save_current_counters(const Counters&);
Status register_current_table(const std::string& table);
Status save_counters(const Operation&, const Counters&);
} // namespace sse_v1
#endif
