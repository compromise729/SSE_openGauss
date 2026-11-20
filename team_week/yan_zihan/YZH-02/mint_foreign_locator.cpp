#include "libpq/row_state_v1.h"
#include <cstdlib>
#include <iostream>
#include <string>

// Test fixture only: encrypt a public rid under another table's associated data.
// The test key is read locally and never printed.
int main(int argc, char** argv) {
    if (argc != 4 || setenv("SSE_V1_STATE_DIR", argv[1], 1) != 0) return 2;
    sse_v1::Scope scope;
    bool registered = false;
    sse_v1::KeySet keys;
    auto status = sse_v1::local_scope("week_demo", scope, registered);
    if (!status.ok() || !registered) return 3;
    status = sse_v1::load_keys(argv[1], keys);
    if (!status.ok()) return 3;
    scope.table = argv[2];
    sse_v1::Locator locator{argv[3], 0};
    std::string blob;
    status = sse_v1::encrypt_locator(scope, locator, keys, blob);
    if (!status.ok()) return 3;
    std::cout << blob << '\n';
    return 0;
}
