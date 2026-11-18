#include "libpq/row_state_catalog.h"
#include <dlfcn.h>
#include <cstdlib>
#include <iostream>
#include <limits.h>
#include <string>
#include <unistd.h>

static bool from_module(void* address, const char* expected)
{
    Dl_info info;
    char actual[PATH_MAX], wanted[PATH_MAX];
    return dladdr(address, &info) != 0 && info.dli_fname &&
        realpath(info.dli_fname, actual) && realpath(expected, wanted) &&
        std::string(actual) == wanted;
}

#ifdef STATE_VERIFY_LIBRARY
// Check the symbols inside the actual gsql/bridge process, before main().
__attribute__((constructor)) static void verify_loaded_module()
{
    const char* expected = std::getenv("STATE_VERIFY_EXPECT_MODULE");
    if (!expected) return;
    if (!from_module(reinterpret_cast<void*>(&sse_v1::begin_operation), expected) ||
        !from_module(reinterpret_cast<void*>(&sse_v1::read_catalog), expected)) {
        const char message[] = "FAIL RUNTIME-02: process loaded an unexpected state module\n";
        (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(125);
    }
    const char message[] = "PASS RUNTIME-02: process loaded the current temporary state/catalog module\n";
    (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
}
#else
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    if (!from_module(reinterpret_cast<void*>(&sse_v1::begin_operation), argv[1]) ||
        !from_module(reinterpret_cast<void*>(&sse_v1::read_catalog), argv[1])) {
        std::cerr << "FAIL RUNTIME-01: state/catalog resolved outside the temporary module\n";
        return 1;
    }
    std::cout << "PASS RUNTIME-01: state and catalog resolve to " << argv[1] << '\n';
}

#endif
