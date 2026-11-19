#ifndef ROW_REQUEST_V1_H
#define ROW_REQUEST_V1_H
#include "libpq-fe.h"
#include "sse_v1_contract.hpp"

namespace sse_v1 {
// Called by gsql before legacy SQL rewriting. Results are exposed only after
// complete_operation succeeds. Internal statements use PQexecParams/PQexec.
Status execute_request(PGconn*, const char* sql, bool& handled, PGresult*& output);
}
#endif
