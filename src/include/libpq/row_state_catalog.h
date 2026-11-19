#ifndef ROW_STATE_CATALOG_H
#define ROW_STATE_CATALOG_H

#include "libpq-fe.h"
#include "row_state_v1.h"

namespace sse_v1 {
// Reads catalog and physical relation shape on the supplied Unix-socket connection.
Status read_catalog(PGconn*, const std::string& candidate, Catalog&);
} // namespace sse_v1

#endif
