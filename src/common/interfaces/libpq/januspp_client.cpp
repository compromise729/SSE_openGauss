#include "libpq/januspp_client.h"
#include "SSE_time.h"

string januspp_fixedivhex = "512b1d33df5b0ec55f839e30350fc70b";
int januspp_d = 2000;
/*januspp_cachetable token use*/
string januspp_Kshex = "1bb4ff4705bf3231062c9930efffbfdd4cbc0f49bd38b949dd912955474bb462";
string januspp_sk0hexinit = "0c75fa263f0bb7c1a99ef603437a6b344a10b0498f8d7b8950f708c23510f606";
string januspp_skIhexinit = "1ffa75cca89973a7d809aba1637be57a4bc4d07700357b882f6d5e9556c8deea";


string januspp_generate_random_key(int length, set<string> &generated_keys) {
    ostringstream oss;
    // 每次生成前更新随机数种子
    update_seed();
    // 生成长度为 length 的十六进制字符串
    for (int i = 0; i < length / 2; i++) { // 每次生成1个字节(2位十六进制数)
        int random_byte = rand() % 256;  // 随机生成0-255的整数
        oss << setw(2) << setfill('0') << hex << random_byte; // 转换为两位十六进制
    }
    string hex_key = oss.str();
    // 如果生成的字符串已经存在，则重新生成
    while (generated_keys.find(hex_key) != generated_keys.end()) {
        oss.str(""); // 清空之前的内容
        for (int i = 0; i < length / 2; i++) {
            int random_byte = rand() % 256;
            oss << setw(2) << setfill('0') << hex << random_byte;
        }
        string tmp = oss.str();
        string nullstr(32, '0');
        /*不能全0 不能与上面的密钥重复*/
        if (tmp != nullstr && tmp != januspp_Kshex && tmp != januspp_sk0hexinit && tmp !=januspp_skIhexinit)
            hex_key = tmp;
    }
    // 将新生成的字符串存储到集合中
    generated_keys.insert(hex_key);
    return hex_key;
}

/*计算32bits二进制哈希*/
string januspp_string_to_hashed_binary(const string& msg) {
    // Convert msg to bytes
    const unsigned char* msg_bytes = reinterpret_cast<const unsigned char*>(msg.c_str());
    size_t msg_len = msg.length();

    // Compute SHA256 hash
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(msg_bytes, msg_len, hash);

    // Convert hash to hex string
    char hex_string[SHA256_DIGEST_LENGTH * 2 + 1]; // 每个字节两位，加一个结尾的 '\0'
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        sprintf(hex_string + (i * 2), "%02x", hash[i]);
    }
    hex_string[SHA256_DIGEST_LENGTH * 2] = '\0'; // 确保字符串结尾

    string hashcode(hex_string); // 将C风格字符串转换为C++字符串

    // Convert hex string to binary string
    auto hex_to_bin = [](const string& hex_str) -> string {
        string bin_str;
        for (char c : hex_str) {
            uint8_t n = 0;
            if (c >= '0' && c <= '9')
                n = c - '0';
            else if (c >= 'a' && c <= 'f')
                n = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                n = c - 'A' + 10;
            else
                continue; // Skip non-hex characters

            for (int i = 3; i >= 0; --i)
                bin_str.push_back((n & (1 << i)) ? '1' : '0');
        }
        return bin_str;
    };

    string bin_str = hex_to_bin(hashcode);

    //32-bit binary hashing
    auto xor_strings = [](const string& x, const string& y) -> string {
        string result;
        for (size_t i = 0; i < x.size(); ++i) {
            result += (x[i] == y[i]) ? '0' : '1';
        }
        return result;
    };

    string result = bin_str;
    while (result.size() > 32) {
        string new_result;
        for (size_t i = 0; i + 32 <= result.size(); i += 32) {
            string c1 = result.substr(i, 16);
            string c2 = result.substr(i + 16, 16);
            new_result += xor_strings(c1, c2);
        }
        result = new_result;
    }

    return result.substr(0, 32);
}

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

/*processinserttuple*/
void januspp_processinserttuple(string &init_sql, unordered_map<string, pair<string, string> > &storeskhex, vector<string> &enckeyword_vec, vector<string> &encind_vec, vector<string> &tag_vec, unordered_map<string, pair<string, string> > &keyword_stage){
    vector<string> get_indw;
    extract_strings(init_sql, get_indw);

    string sk0hex, skIhex;
    if (storeskhex.find(get_indw[1]) != storeskhex.end()){
        sk0hex = storeskhex[get_indw[1]].first;
        skIhex = storeskhex[get_indw[1]].second;
    }
    else{
        sk0hex = januspp_sk0hexinit;
        skIhex = januspp_skIhexinit;
    }
    /*convert hex to bytes*/
    string sk0 = hexTobytes(sk0hex);
    string skI = hexTobytes(skIhex);
    string fixediv = hexTobytes(januspp_fixedivhex);

    // Hash keyword first
    string enc_keyword = base64_encode(januspp_crypto_primitives_hmac(sk0, get_indw[1], 256));
    string enc_ind, tag;

    // Generate key series
    vector<string> key_series(januspp_d+1);
    string sk = januspp_encrypt(januspp_key_trim(sk0), get_indw[1], fixediv);
    key_series[0] = sk;
    for (int i = 1; i <= januspp_d; ++i) {
        string next_sk = januspp_crypto_primitives_hmac(skI, key_series[i - 1], 256);
        key_series[i] = next_sk;
    }

    // get tag and encrypt ind
    tag = januspp_string_to_hashed_binary(get_indw[0]);
    __int128 k_id = 0;
    __int128 int_ind = 0;
    __int128 int_encind = 0;
    for (const string& key_item : key_series) {
        string result = key_item;
        for (size_t i = 0; i < tag.length(); ++i) {
            string bit(1, tag[i]);
            result = januspp_encrypt(januspp_key_trim(result), bit, fixediv);
        }
         __int128 int_result = 0;
        memcpy(&int_result, result.data(), sizeof(__int128));
        k_id ^= int_result;
    }

    memcpy(&int_ind, get_indw[0].data(), sizeof(__int128));
    int_encind = int_ind^k_id;

    enc_ind = base64_encode(int128_to_string(int_encind));
    tag = base64_encode(tag);

    enckeyword_vec.push_back(enc_keyword);
    encind_vec.push_back(enc_ind);
    tag_vec.push_back(tag);

    if (keyword_stage.find(enc_keyword) == keyword_stage.end())
        keyword_stage[enc_keyword] = make_pair(base64_encode(key_series[0]), "0");
}

/*processInsert*/
string januspp_processInsert(string &init_sql, string &tablename){

    tracker.start("januspp_client init time");
    vector<string> tuplevec;
    get_Inserttuple(init_sql, tuplevec);
    int tuplevecsize = tuplevec.size();

    string januspp_skhex = tablename + "januspp_skhex.txt";
    string januspp_keywordstage = tablename + "januspp_keywordstage.txt";

    unordered_map<string, pair<string, string> > storeskhex;
    readPairmap(januspp_skhex.c_str(), storeskhex);
    
    /*januspp_keyword_stage*/
    unordered_map<string, pair<string, string> > keyword_stage;
    readPairmap(januspp_keywordstage.c_str(), keyword_stage);
    tracker.stop("januspp_client init time");

    tracker.start("januspp_client Rewrite insert SQL time");
    /*vector of object*/
    vector<string> enckeyword_vec;
    vector<string> encind_vec;
    vector<string> tag_vec;

    /*string of object*/
    string p_enckeyword = "[", p_encind = "[", p_tag = "[";

    string modified_sql;

    /*get data_sql*/
    string init_sqllower = init_sql;
    transform(init_sqllower.begin(), init_sqllower.end(), init_sqllower.begin(), ::tolower);
    size_t pos = init_sqllower.find("values");
    string data_sql(init_sql, pos, init_sql.length() - pos);
    data_sql.erase(data_sql.end() - 1);
    data_sql = "insert into "+tablename+"_data "+data_sql;
    data_sql = double_quote_values(data_sql);

    tracker.start("januspp_client insert computation time");
    for (int i=0;i < tuplevecsize; i++)
        januspp_processinserttuple(tuplevec[i], storeskhex, enckeyword_vec, encind_vec, tag_vec, keyword_stage);
    tracker.stop("januspp_client insert computation time");

    int enckeywordvecsize = enckeyword_vec.size();
    for (int i=0;i<enckeywordvecsize; i++){
        if (i!=enckeywordvecsize - 1){
            p_enckeyword += "'" + enckeyword_vec[i] + "', ";
            p_encind += "'" + encind_vec[i] + "', ";
            p_tag += "'" + tag_vec[i] + "', ";
        }
        else{
            p_enckeyword += "'" + enckeyword_vec[i] + "']";
            p_encind += "'" + encind_vec[i] + "']";
            p_tag += "'" + tag_vec[i] + "']";
        }
    }

    modified_sql = "DO $$\n"
                  "DECLARE\n"
                  "    start_time TIMESTAMP;\n"
                  "    end_time TIMESTAMP;\n"
                  "    duration INTERVAL;\n"
                  "    enckeyword_array TEXT[] := ARRAY" + p_enckeyword + ";\n"
                  "    encind_array TEXT[] := ARRAY" + p_encind + ";\n"
                  "    tag_array TEXT[] := ARRAY" + p_tag + ";\n"
                  "    table_prefix TEXT := '" + tablename + "';\n"
                  "    data_sql TEXT := '" + data_sql + "';\n"
                  "BEGIN\n"
                  "    start_time := clock_timestamp();\n"
                  "    PERFORM januspp_insert(enckeyword_array, encind_array, tag_array, table_prefix, data_sql);\n"
                  "    end_time := clock_timestamp();\n"
                  "    duration := end_time - start_time;\n"
                  "    RAISE NOTICE 'januspp_insert SQL execution time: %', duration;\n"
                  "END $$;\n";

    writePairmap(januspp_keywordstage.c_str(), keyword_stage);
    writePairmap(januspp_skhex.c_str(), storeskhex);
    tracker.stop("januspp_client Rewrite insert SQL time");
    return modified_sql;
}

string januspp_processdelete(string &init_sql, string &tablename){
    vector<string> get_indw; //ind keyword
    extract_strings(init_sql, get_indw);

    /*vector of object*/
    vector<string> ind_vec;
    vector<string> keyword_vec;
    vector<string> enckeyword_vec;
    vector<pair<string,string>> punckey_vec;
    vector<string> deltag_vec;

    /*string of object*/
    string p_ind = "[", p_keyword = "[",p_enckeyword = "[", p_punckey = "[", p_deltag = "[";

    string modified_sql;

    unordered_map<string, pair<string, string> > storeskhex;

    tracker.start("januspp_client init time");
    string januspp_skhex = tablename + "januspp_skhex.txt";
    string januspp_keywordstage = tablename + "januspp_keywordstage.txt";

    readPairmap(januspp_skhex.c_str(), storeskhex);
    string sk0hex, skIhex, sk0, skI, fixediv;
    if (storeskhex.find(get_indw[1]) != storeskhex.end()){
        sk0hex = storeskhex[get_indw[1]].first;
        skIhex = storeskhex[get_indw[1]].second;
    }
    else{
        sk0hex = januspp_sk0hexinit;
        skIhex = januspp_skIhexinit;
    }
    sk0 = hexTobytes(sk0hex);
    skI = hexTobytes(skIhex);
    fixediv = hexTobytes(januspp_fixedivhex);

    string enc_keyword = base64_encode(januspp_crypto_primitives_hmac(sk0, get_indw[1], 256));
    string tag = januspp_string_to_hashed_binary(get_indw[0]);

    unordered_map<string, pair<string, string>> keyword_stage;
    readPairmap(januspp_keywordstage.c_str(), keyword_stage);
    tracker.stop("januspp_client init time");

    tracker.start("januspp_client Rewrite delete SQL time");
    int next_deletion = 1;
    if (keyword_stage.find(enc_keyword) != keyword_stage.end()) {
        int deletion_cnt;
        istringstream iss(keyword_stage[enc_keyword].second);
        iss >> deletion_cnt;
        next_deletion = deletion_cnt + 1;
    }

    if (next_deletion > januspp_d) {
        return modified_sql = 
            "DO $$\n"
            "BEGIN\n"
            "    RAISE NOTICE 'not allowed to delete';\n"
            "END $$;\n";
    }

    /* Get key_series */
    vector<string> key_series(januspp_d + 1);
    string sk = januspp_encrypt(januspp_key_trim(sk0), get_indw[1], fixediv);
    key_series[0] = sk;

    for (int i = 1; i <= januspp_d; ++i) {
        string next_sk = januspp_crypto_primitives_hmac(skI, key_series[i - 1], 256);
        key_series[i] = next_sk;
    }

    /* Get initial key */
    string key_initiator;
    if (keyword_stage.find(enc_keyword) != keyword_stage.end()) {
        key_initiator = base64_decode(keyword_stage[enc_keyword].first);
    } else {
        key_initiator = key_series[0];
    }

    tracker.start("januspp_client delete computation time");
    /* Calculate punckey path */
    string traveled;
    for (size_t b_index = 0; b_index < tag.size(); ++b_index) {
        string result;
        if (traveled.empty()) {
            string bit = tag[b_index] == '0' ? "1" : "0";
            result = januspp_encrypt(januspp_key_trim(key_initiator), bit, fixediv);
            punckey_vec.emplace_back(base64_encode(result), base64_encode(bit));
        } else {
            string t_path = traveled + (tag[b_index] == '0' ? '1' : '0');
            result = key_initiator;
            for (char c : t_path) {
                string bit(1, c);
                result = januspp_encrypt(januspp_key_trim(result), bit, fixediv);
            }
            punckey_vec.emplace_back(base64_encode(result), base64_encode(t_path));
        }
        traveled += tag[b_index];
    }

    string next_key = key_series[next_deletion];
    ostringstream oss;
    oss << next_deletion;
    keyword_stage[enc_keyword] = make_pair(base64_encode(next_key), oss.str());

    tracker.stop("januspp_client delete computation time");

    /* Write keyword_stage to disk */
    writePairmap(januspp_keywordstage.c_str(), keyword_stage);

    ind_vec.push_back(get_indw[0]);
    keyword_vec.push_back(get_indw[1]);
    enckeyword_vec.push_back(enc_keyword);
    deltag_vec.push_back(base64_encode(tag));

    /* Get p_ind and others */
    int indvecsize = ind_vec.size();
    for (int i = 0; i < indvecsize; i++) {
        if (i != indvecsize - 1) {
            p_ind += "'" + ind_vec[i] + "', ";
            p_keyword += "'" + keyword_vec[i] + "', ";
        } else {
            p_ind += "'" + ind_vec[i] + "']";
            p_keyword += "'" + keyword_vec[i] + "']";
        }
    }

    int punckeyvecsize = punckey_vec.size();
    for (int i = 0; i < punckeyvecsize; i++) {
        if (i != punckeyvecsize - 1) {
            p_enckeyword += "'" + enckeyword_vec[0] + "', ";
            p_deltag += "'" + deltag_vec[0] + "', ";
            p_punckey += "'" + punckey_vec[i].first + "|" + punckey_vec[i].second + "', ";
        } else {
            p_enckeyword += "'" + enckeyword_vec[0] + "']";
            p_deltag += "'" + deltag_vec[0] + "']";
            p_punckey += "'" + punckey_vec[i].first + "|" + punckey_vec[i].second + "']";
        }
    }

    tracker.stop("januspp_client Rewrite delete SQL time");

    modified_sql = 
        "DO $$\n"
        "DECLARE\n"
        "    start_time TIMESTAMP;\n"
        "    end_time TIMESTAMP;\n"
        "    duration INTERVAL;\n"
        "    ind_array TEXT[] := ARRAY" + p_ind + ";\n"
        "    keyword_array TEXT[] := ARRAY" + p_keyword + ";\n"
        "    enckeyword_array TEXT[] := ARRAY" + p_enckeyword + ";\n"
        "    punckey_array TEXT[] := ARRAY" + p_punckey + ";\n"
        "    deltag_array TEXT[] := ARRAY" + p_deltag + ";\n"
        "    table_prefix TEXT := '" + tablename + "';\n"
        "BEGIN\n"
        "    start_time := clock_timestamp();\n"
        "    PERFORM januspp_delete(ind_array, keyword_array, enckeyword_array, punckey_array, deltag_array, table_prefix);\n"
        "    end_time := clock_timestamp();\n"
        "    duration := end_time - start_time;\n"
        "    RAISE NOTICE 'januspp_delete execution time: %', duration;\n"
        "END $$;\n";
    return modified_sql;
}