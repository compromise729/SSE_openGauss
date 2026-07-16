#include "januspp_server.h"

string januspp_fixedivhex = "512b1d33df5b0ec55f839e30350fc70b";
int januspp_d = 2000;

int isjanuspp = 0;
string januspp_tkn;
string januspp_enckeyword;
string januspp_msk;
string januspp_delcntstr;
string januspp_skI;
vector<string> januspp_encindvec;// encind
vector<string> januspp_tagvec;//tag
set<string> januspp_cacheset;//ind
vector<string> januspp_punckeyvec;//punckey

map<string, string> januspp_decryptmap;// decrypt ind
vector<string> januspp_reinsertvec;// for reinsert


/*hmac*/
string januspp_crypto_primitives_hmac(const string& key, const string& msg, int lambda)
{
    unsigned char* result;
    unsigned int len = SHA256_DIGEST_LENGTH; 
    result = HMAC(EVP_sha256(), key.c_str(), key.size(),
                  reinterpret_cast<const unsigned char*>(msg.c_str()), msg.size(), 
                  NULL, NULL);

    ostringstream oss;
    int bytes_to_output = lambda / 8; 
    for (int i = 0; i < bytes_to_output; i++) 
        oss << hex << setw(2) << setfill('0') << (int)result[i];
    return oss.str(); 
}

/*pad operation*/
string januspp_pad(const string& s, size_t bs)
{
    if (s.size()>=bs)
        return s;
    size_t pad_len = bs - (s.size() % bs);
    string padding(pad_len, (char)pad_len);
    return s + padding;
}

/*取32位密钥*/
string januspp_key_trim(const string& key)
{
    if (key.size() >= 32) {
        return key.substr(0, 32);
    } else {
        return januspp_pad(key, 32);
    }
}

/*januspp-aes加密*/
string januspp_encrypt(const string& key, const string& raw, const string& iv)
{
    string padded_raw = januspp_pad(raw, 32);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw runtime_error("Failed to create EVP_CIPHER_CTX");
    }

    int len;
    int ciphertext_len;
    unsigned char ciphertext[128];

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (unsigned char*)key.data(), (unsigned char*)iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("EVP_EncryptInit_ex failed");
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, (unsigned char*)padded_raw.data(), padded_raw.size())) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("EVP_EncryptUpdate failed");
    }
    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw runtime_error("EVP_EncryptFinal_ex failed");
    }
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return string((char*)ciphertext, ciphertext_len);
}

string januspp_getresult(string &tablename){
    vector<pair<string, string>> punckey_vec;//解码punckey，以pair类型存储
    string fixediv = hexTobytes(januspp_fixedivhex);
    int delcnt;
    istringstream iss(januspp_delcntstr);
    iss >> delcnt;
    for (int i=0;i<januspp_punckeyvec.size();i++){
        string punckey = januspp_punckeyvec[i];
        size_t pos = punckey.find('|');
        string resultkey = punckey.substr(0, pos);
        string path = punckey.substr(pos+1);
        punckey_vec.push_back(make_pair(resultkey, path));
    }

    for (int i = 0; i < januspp_encindvec.size(); i++) {
        string encind = base64_decode(januspp_encindvec[i]);
        string tag = base64_decode(januspp_tagvec[i]);
        __int128 k_id = 0;

        // Process deletion paths
        int punckeyvec_size = punckey_vec.size();
        for (int j = 0; j < punckeyvec_size; j++) {
            string result = base64_decode(punckey_vec[j].first);
            string path = base64_decode(punckey_vec[j].second);
            if (tag.substr(0, path.size()) == path) {
                if (path.size() != 32) {
                    // Encrypt remaining bits
                    for (size_t k = path.size(); k < tag.size(); ++k) {
                        string bit(1, tag[k]);
                        result = januspp_encrypt(januspp_key_trim(result), bit, fixediv);
                    }
                }
                __int128 int_result = 0;
                memcpy(&int_result, result.data(), sizeof(__int128));
                k_id ^= int_result;
            }
        }

        string cur_key = januspp_msk;
        int cur_delcnt = delcnt;
        while (cur_delcnt <= januspp_d) {
            string result = cur_key;
            for (char bit : tag) {
                string bit_str(1, bit);
                result = januspp_encrypt(januspp_key_trim(result), bit_str, fixediv);
            }
            __int128 int_result = 0;
            memcpy(&int_result, result.data(), sizeof(__int128));
            k_id ^= int_result;

            cur_key = januspp_crypto_primitives_hmac(januspp_skI, cur_key, 256);
            cur_delcnt++;
        }

        __int128 int_encind = string_to_int128(encind);
        __int128 int_ind = k_id ^ int_encind;

        // Get ind from int_ind
        char buffer[sizeof(__int128) + 1] = {0};
        memcpy(buffer, &int_ind, sizeof(__int128));
        string ind(buffer);

        januspp_decryptmap[januspp_encindvec[i]] = ind;
        if (januspp_cacheset.find(ind) == januspp_cacheset.end()){
            string reinserttuple = "('" + januspp_tkn + "', '" + ind + "', '" + januspp_tagvec[i] +"')";
            januspp_reinsertvec.push_back(reinserttuple);
        }
    }
    
    string sql1 = "select encind from " + tablename + "_indexadd where tag not in (select deltag from " + tablename + "_indexdel where enckeyword = ";
    sql1 += "'" + januspp_enckeyword + "') and enckeyword = '" + januspp_enckeyword + "'";

    // get ind from cache
    string sql2 = "select ind from "+ tablename + "_cache where tag not in (select deltag from " + tablename + "_indexdel where enckeyword = ";
    sql2 += "'" + januspp_enckeyword + "') and tkn = '" + januspp_tkn + "'";
    string modified_sql  = sql1 + " union all " + sql2 + ";";
    return modified_sql;
}