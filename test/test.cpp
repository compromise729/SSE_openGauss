#include <iostream>
#include <gmp.h>
#include <sodium.h>  // OpenSSL 用于生成哈希值

// 使用关键词和计数器生成密钥（32字节哈希值）
void generate_key(mpz_t key, const std::string &keyword, int counter) {
    // 将关键词和计数器拼接为字符串
    std::string input = keyword + std::to_string(counter);
    
    // 使用 libsodium 的 SHA256 计算哈希
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, reinterpret_cast<const unsigned char *>(input.c_str()), input.size());

    // 将哈希值转为大整数
    mpz_import(key, crypto_hash_sha256_BYTES, 1, sizeof(hash[0]), 0, 0, hash);
}

// 加密函数：c = m XOR k
void encrypt(mpz_t c, const mpz_t m, const std::string &keyword, int counter) {
    mpz_t key;
    mpz_init(key);

    // 生成密钥
    generate_key(key, keyword, counter);

    // 加密：c = m XOR k
    mpz_xor(c, m, key);

    mpz_clear(key);
}

// 解密函数：m = c XOR k
void decrypt(mpz_t m, const mpz_t c, const std::string &keyword, int counter) {
    mpz_t key;
    mpz_init(key);

    // 生成密钥
    generate_key(key, keyword, counter);

    // 解密：m = c XOR k
    mpz_xor(m, c, key);

    mpz_clear(key);
}

// 同态加法（异或操作）：c_result = c1 XOR c2
void homomorphic_add(mpz_t c_result, const mpz_t c1, const mpz_t c2) {
    mpz_xor(c_result, c1, c2);
}

int main() {
    // 初始化大整数
    mpz_t m1, m2, c1, c2, c_result, m_result;
    mpz_inits(m1, m2, c1, c2, c_result, m_result, nullptr);

    // 设置明文
    mpz_set_str(m1, "1", 2);  // 明文1 (二进制)
    mpz_set_str(m2, "10000", 2);  // 明文2 (二进制)

    std::cout << "Plaintext 1: ";
    mpz_out_str(stdout, 2, m1);
    std::cout << std::endl;
    std::cout << "Plaintext 1: ";
    mpz_out_str(stdout, 2, m2);
    std::cout << std::endl;

    // 关键词
    std::string keyword = "keyword1";

    // 加密
    encrypt(c1, m1, keyword, 1);  // 使用计数器 1
    encrypt(c2, m2, keyword, 2);  // 使用计数器 2

    // 输出密文
    std::cout << "Ciphertext 1: ";
    mpz_out_str(stdout, 2, c1);
    std::cout << std::endl;

    std::cout << "Ciphertext 2: ";
    mpz_out_str(stdout, 2, c2);
    std::cout << std::endl;

    // 同态加法（异或操作）
    homomorphic_add(c_result, c1, c2);

    // 输出加法结果密文
    std::cout << "Ciphertext Result (c1 XOR c2): ";
    mpz_out_str(stdout, 2, c_result);
    std::cout << std::endl;

    // 将大整数转换为字符串（2进制）
    char *str = mpz_get_str(NULL, 2, c_result);

    // 转换为 std::string
    std::string num_str(str);
    std::cout << "Big integer as string: " << num_str << std::endl;

    // 解密
    decrypt(m_result, c_result, keyword, 1);  // 第一步用计数器 1 的密钥解密
    decrypt(m_result, m_result, keyword, 2); // 第二步用计数器 2 的密钥解密

    // 输出解密后的明文结果
    std::cout << "Decrypted Result (m1 XOR m2): ";
    mpz_out_str(stdout, 2, m_result);
    std::cout << std::endl;

    // 清理
    mpz_clears(m1, m2, c1, c2, c_result, m_result, nullptr);
    return 0;
}
