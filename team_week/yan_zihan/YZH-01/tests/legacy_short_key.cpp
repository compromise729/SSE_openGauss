#include "libpq/sse_common.h"

#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const std::string dir = argv[1];
    const bool fixed = std::string(argv[2]) == "fixed";
    const std::string key(16, 'k');
    for (const auto n : {0, 15, 16}) {
        const std::string path = dir + "/key" + std::to_string(n);
        std::ofstream file(path, std::ios::binary);
        file.write(key.data(), n);
        file.close();
        if (!file) return 2;
        const auto loaded = generate_aes_key(path);
        const auto expected = fixed && n < 16 ? 0U : 16U;
        if (loaded.size() != expected || (n == 16 && loaded != key)) return 1;
        std::cout << "PASS legacy " << argv[2] << " file_bytes=" << n << " returned_bytes=" << loaded.size() << '\n';
    }
    if (fixed && (!aes_encrypt("test", key.substr(1)).empty() ||
                  !aes_decrypt("test", key.substr(1)).empty())) return 1;
    if (fixed) std::cout << "PASS legacy fixed short key rejected by both AES helpers\n";
    return 0;
}
