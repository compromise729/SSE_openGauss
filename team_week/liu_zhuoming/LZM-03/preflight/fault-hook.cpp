#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>

// Test-only process-local injection. No system configuration or key changes.
extern "C" int renameat(int from, const char* source, int to, const char* target) {
    using Rename = int(*)(int, const char*, int, const char*);
    static const auto original = reinterpret_cast<Rename>(dlsym(RTLD_NEXT, "renameat"));
    if (!original) { errno = ENOSYS; return -1; }
    const char* mode = std::getenv("STATE_FAULT");
    if (mode && strcmp(mode, "marker-rename") == 0 && target && strcmp(target, "operation.v1") == 0) {
        const char* root = std::getenv("ISOLATED_RUN_DIR");
        const char* state = std::getenv("SSE_V1_STATE_DIR");
        char root_path[PATH_MAX], state_path[PATH_MAX];
        struct stat expected, actual;
        if (!root || !state || !realpath(root, root_path) || !realpath(state, state_path) ||
            std::string(state_path).compare(0, std::strlen(root_path) + 1, std::string(root_path) + "/") != 0 ||
            stat(state_path, &expected) != 0 || fstat(to, &actual) != 0 ||
            expected.st_dev != actual.st_dev || expected.st_ino != actual.st_ino) {
            errno = EPERM; return -1;
        }
        errno = EIO; return -1;
    }
    return original(from, source, to, target);
}
