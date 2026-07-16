#include "Server_FBDSSE.h"
#include <iomanip>

bool FB_flag=false;
char* FB_bs=nullptr;
int FB_count=0;
std::string FB_st="";
std::string FB_ut="";
std::string FB_ct="";


// 使用libsodium的crypto_generichash进行哈希
std::string FBserver_sodium_hash(const std::string& str) {
    unsigned char hash[crypto_generichash_BYTES];  // 输出哈希的字节数组
    crypto_generichash_state state;
    
    // 初始化哈希状态
    crypto_generichash_init(&state, nullptr, 0, crypto_generichash_BYTES);
    
    // 更新哈希状态，传入要哈希的数据
    crypto_generichash_update(&state, reinterpret_cast<const unsigned char*>(str.c_str()), str.size());
    
    // 完成哈希计算，得到哈希值
    crypto_generichash_final(&state, hash, crypto_generichash_BYTES);
    
    // 将哈希值转换为十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < crypto_generichash_BYTES; ++i) {
        ss << std::setw(2) << std::setfill('0') << std::hex << (int)hash[i];
    }
    return ss.str();
}

// 同态加法（异或操作）：c_result = c1 XOR c2
void homomorphic_add(mpz_t c_result, const mpz_t c1, const mpz_t c2) {
    mpz_xor(c_result, c1, c2);
}

// 异或两个字符串
std::string FBserver_xor_strings(const std::string& str1, const std::string& str2) {
    // 确保两个字符串长度相同，如果不相同，可以选择填充（此处为不处理，假设长度相同）
    size_t length = std::min(str1.size(), str2.size());
    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        result += str1[i] ^ str2[i];  // 字符的异或操作
    }

    return result;
}