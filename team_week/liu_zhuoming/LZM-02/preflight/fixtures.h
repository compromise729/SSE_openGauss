#ifndef LZM02_FIXTURES_H
#define LZM02_FIXTURES_H
#include "libpq/row_state_v1.h"
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <unistd.h>

inline sse_v1::Status fixture_keys(const std::string& dir, sse_v1::KeySet& out)
{
    out = sse_v1::KeySet();
    const char* names[] = {"row_aead_v1.key", "locator_aead_v1.key", "row_aead_v1.keyid"};
    const std::string values[] = {std::string(32, 'r'), std::string(32, 'l'), std::string(16, 'i')};
    for (unsigned i = 0; i < 3; ++i) {
        const int fd = open((dir + "/" + names[i]).c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) return {sse_v1::Code::key_invalid, "cannot create synthetic fixture"};
        const auto written = write(fd, values[i].data(), values[i].size());
        const bool synced = fsync(fd) == 0;
        const bool closed = close(fd) == 0;
        if (written != static_cast<ssize_t>(values[i].size()) || !synced || !closed)
            return {sse_v1::Code::key_invalid, "cannot write synthetic fixture"};
    }
    out.key_id = values[2];
    return {sse_v1::Code::ok, ""};
}

inline std::string fixture_read(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
#endif
