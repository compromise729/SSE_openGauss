#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
void require(bool value) { if (!value) throw std::runtime_error("OpenSSL probe failed"); }
struct Sealed { std::array<unsigned char,12> iv; std::array<unsigned char,16> tag; std::vector<unsigned char> ct; };
Sealed seal(const std::string& text, const std::array<unsigned char,32>& key) {
    Sealed out; require(RAND_bytes(out.iv.data(),out.iv.size()) == 1);
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new(); require(ctx != nullptr);
    out.ct.resize(text.size()+16); int n=0, tail=0;
    require(EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,nullptr,nullptr) == 1);
    require(EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_IVLEN,out.iv.size(),nullptr) == 1);
    require(EVP_EncryptInit_ex(ctx,nullptr,nullptr,key.data(),out.iv.data()) == 1);
    require(EVP_EncryptUpdate(ctx,out.ct.data(),&n,reinterpret_cast<const unsigned char*>(text.data()),text.size()) == 1);
    require(EVP_EncryptFinal_ex(ctx,out.ct.data()+n,&tail) == 1);
    out.ct.resize(n+tail);
    require(EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,out.tag.size(),out.tag.data()) == 1);
    EVP_CIPHER_CTX_free(ctx); return out;
}
bool open(const Sealed& in,const std::array<unsigned char,32>& key,std::string& output) {
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new(); require(ctx != nullptr);
    std::vector<unsigned char> plain(in.ct.size()+16); int n=0,tail=0;
    require(EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),nullptr,nullptr,nullptr) == 1);
    require(EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_IVLEN,in.iv.size(),nullptr) == 1);
    require(EVP_DecryptInit_ex(ctx,nullptr,nullptr,key.data(),in.iv.data()) == 1);
    require(EVP_DecryptUpdate(ctx,plain.data(),&n,in.ct.data(),in.ct.size()) == 1);
    require(EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,in.tag.size(),const_cast<unsigned char*>(in.tag.data())) == 1);
    bool valid=EVP_DecryptFinal_ex(ctx,plain.data()+n,&tail) == 1;
    EVP_CIPHER_CTX_free(ctx); output.clear();
    if (valid) output.assign(reinterpret_cast<const char*>(plain.data()),n+tail);
    return valid;
}
int main() {
 try {
    std::array<unsigned char,32> key; require(RAND_bytes(key.data(),key.size()) == 1);
    for (const std::string text : {std::string(),std::string("apple"),std::string("北京"),std::string("x\0",2)}) {
        Sealed a=seal(text,key), b=seal(text,key); std::string result;
        require(a.iv != b.iv); require(open(a,key,result) && result == text);
        a.tag[0]^=1; require(!open(a,key,result) && result.empty());
    }
    std::cout << "PASS AES-256-GCM: random nonce, exact bytes, empty input, tag rejection" << std::endl;
 } catch(const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
