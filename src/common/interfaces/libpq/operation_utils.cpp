#include "libpq/operation_utils.h"

string storage_path="/opt/og/openGauss-server/sse_storage";
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; 
void update_seed(){
    unsigned long long g_seed = static_cast<unsigned int>(time(0)) ^ getpid();
    srand(g_seed);
}

int readPairmap(const char *file_path, unordered_map<string, pair<string, string> > &pairmap) {
    pthread_mutex_lock(&file_mutex);
    string path=storage_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "r"); 
    if (!file) {
        cerr << "Unable to open file\n";
        pthread_mutex_unlock(&file_mutex);  
        return 1;  
    }

    char key[512], pairval1[512], pairval2[512];
    while (fgets(key, sizeof(key), file) && 
           fgets(pairval1, sizeof(pairval1), file) && 
           fgets(pairval2, sizeof(pairval2), file)) {
        
        // 去除字符串末尾的换行符
        key[strcspn(key, "\n")] = 0;
        pairval1[strcspn(pairval1, "\n")] = 0;
        pairval2[strcspn(pairval2, "\n")] = 0;

        pairmap[string(key)] = make_pair(string(pairval1), string(pairval2));
    }
    if (ferror(file)) {
        cerr << "Error occurred while reading the file\n";
        fclose(file); 
        pthread_mutex_unlock(&file_mutex); 
        return 1;
    }

    fclose(file); 
    pthread_mutex_unlock(&file_mutex); 
    return 0;
}

int writePairmap(const char *file_path, unordered_map<string, pair<string, string> > &pairmap) {
    pthread_mutex_lock(&file_mutex);  
    string path=storage_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "w");  
    if (!file) {
        cerr << "Unable to open file\n";
        pthread_mutex_unlock(&file_mutex);  
        return 1;  
    }

    for (unordered_map<string, pair<string, string> >::iterator it = pairmap.begin(); it != pairmap.end(); ++it) {
        // 使用 fprintf 以确保字符串中包含空格时不会被截断
        fprintf(file, "%s\n%s\n%s\n", it->first.c_str(), it->second.first.c_str(), it->second.second.c_str());
    }

    fflush(file);  
    fclose(file);  
    pthread_mutex_unlock(&file_mutex); 
    return 0;
}

int readset(const char *file_path, set<string> &sset) {
    pthread_mutex_lock(&file_mutex);
    string path=storage_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "r");
    if (!file) {
        cerr << "Unable to open file\n";
        pthread_mutex_unlock(&file_mutex);
        return 1;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file)) {
        string val(buffer);
        // 去除末尾的换行符
        if (!val.empty() && val.back() == '\n') {
            val.pop_back();
        }
        sset.insert(val);
    }

    if (ferror(file)) {
        cerr << "Error occurred while reading the file\n";
        fclose(file);
        pthread_mutex_unlock(&file_mutex);
        return 1;
    }

    fclose(file);
    pthread_mutex_unlock(&file_mutex);
    return 0;
}

int writeset(const char *file_path, set<string> &sset) {
    pthread_mutex_lock(&file_mutex);
    string path=storage_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "w");
    if (!file) {
        cerr << "Unable to open file\n";
        pthread_mutex_unlock(&file_mutex);
        return 1;
    }

    for (const string &val : sset) {
        string line = val + "\n";
        fwrite(line.c_str(), sizeof(char), line.size(), file);
    }

    fflush(file);
    fclose(file);
    pthread_mutex_unlock(&file_mutex);
    return 0;
}

int readmap(const char *file_path, map<string, string> &mmap) {
    pthread_mutex_lock(&file_mutex);
    string path=storage_path+ "/"+file_path;
    ifstream file(path.c_str());
    if (!file) {
        cerr << "Unable to open file\n";
        pthread_mutex_unlock(&file_mutex);  
        return 1; 
    }

    string key, value;
    while (getline(file, key)) {  
        if (!getline(file, value)) {
            cerr << "Error: Key found without a corresponding value\n";
            break;
        }
        mmap[key] = value;
    }

    file.close();
    pthread_mutex_unlock(&file_mutex);
    return 0;
}

int writemap(const char *file_path, map<string, string> &mmap) {
    pthread_mutex_lock(&file_mutex);
    string path=storage_path+ "/"+file_path;
    ofstream file(path.c_str());
    if (!file) {
        cerr << "Unable to open file for writing\n";
        pthread_mutex_unlock(&file_mutex);
        return 1;
    }

    for (const auto &pair : mmap) {
        file << pair.first << "\n" << pair.second << "\n";
    }

    file.close();
    pthread_mutex_unlock(&file_mutex);
    return 0;
}

void initmap(string key, map<string, string> &mmap, int iscnt){
    if(!iscnt)
        mmap[key] = "";
    else
        mmap[key] = "0";
}

string getmapvalue(string key, map<string, string> &mmap, int iscnt){
    string val;
    if(mmap.find(key) != mmap.end())
        val = mmap[key];
    else{
        initmap(key, mmap, iscnt);
        val = mmap[key];
    }
    return val;
}

void extract_strings(const string& input, vector<string>& result){
    size_t start_pos = 0;
    while ((start_pos = input.find("'", start_pos)) != string::npos) {
        size_t end_pos = input.find("'", start_pos + 1);
        result.push_back(input.substr(start_pos + 1, end_pos - start_pos - 1));
        start_pos = end_pos + 1; 
    }
}

void get_Inserttuple(const string& input, vector<string>& result) {
    string input_lower = input;
    transform(input_lower.begin(), input_lower.end(), input_lower.begin(), (int(*)(int))tolower);
    // 获取values后面的语句
    size_t value_pos = input_lower.find("values");
    string value_sql(input, value_pos);
    size_t start_pos = 0;
    while ((start_pos = value_sql.find('(', start_pos)) != string::npos) {
        size_t end_pos = value_sql.find(')', start_pos + 1);
        result.push_back(value_sql.substr(start_pos + 1, end_pos - start_pos - 1));
        start_pos = end_pos + 1; 
    }
}

string generate_random_key(int length, set<string> &generated_keys){
    ostringstream oss;
    update_seed();
    for (int i = 0; i < length / 2; i++) { 
        int random_byte = rand() % 256; 
        oss << setw(2) << setfill('0') << hex << random_byte;
    }
    string hex_key = oss.str();
    while (generated_keys.find(hex_key) != generated_keys.end()) {
        oss.str(""); 
        for (int i = 0; i < length / 2; i++) {
            int random_byte = rand() % 256;
            oss << setw(2) << setfill('0') << hex << random_byte;
        }
        string tmp = oss.str();
        string nullstr(32, '0');
        if (tmp != nullstr)
            hex_key = tmp;
    }
    generated_keys.insert(hex_key);
    return hex_key;
}

string xorstring(const string &str1, const string &str2){
    string res;
    int len1 = str1.length(), len2 = str2.length();
    if (len1!=len2)
        return res;
    for(int i=0;i<len1;i++)
        res+=(str1[i]==str2[i])?'0':'1';
    return res;
}

string hexTobytes(const string& hexstr) {
    string bytes;
    bytes.reserve(hexstr.length() / 2);
    for (size_t i = 0; i < hexstr.length(); i += 2) {
        string byteString = hexstr.substr(i, 2);
        char byte = static_cast<char>(strtol(byteString.c_str(), nullptr, 16));
        bytes += byte;
    }
    return bytes;
}

string binaryTohex(string binary_string){
    int len = binary_string.length();
    ostringstream hexos;
    for (int i = 0; i < len; i+=4){
        string fourbitChunk =  binary_string.substr(i, 4);
        int hex_value = bitset<4>(fourbitChunk).to_ulong();
        hexos << hex << hex_value;
    }
    return hexos.str();
}

string hexTobinary(string hex_string) {
    ostringstream binaryos;
    int len = hex_string.length();
    for (int i = 0; i < len; i++) {
        char hex_char = hex_string[i];
        int hex_value = 0;
        if (hex_char >= '0' && hex_char <= '9') {
            hex_value = hex_char - '0';
        } else if (hex_char >= 'a' && hex_char <= 'f') {
            hex_value = hex_char - 'a' + 10;
        } else if (hex_char >= 'A' && hex_char <= 'F') {
            hex_value = hex_char - 'A' + 10;
        }
        binaryos << bitset<4>(hex_value);
    }
    return binaryos.str();
}

string getTableNameFromSQL(const string& sql) {
    string sql_lower = sql;
    transform(sql_lower.begin(), sql_lower.end(), sql_lower.begin(), ::tolower);

    size_t from_pos = sql_lower.find("from");
    size_t insert_pos = sql_lower.find("insert into");
    size_t delete_pos = sql_lower.find("delete from");

    if (from_pos != string::npos && (sql_lower.find("select") != string::npos || sql_lower.find("delete") != string::npos)) {
        from_pos += 4;  // 跳过 "from"

        // 跳过空格
        while (from_pos < sql.size() && isspace(sql[from_pos])) {
            from_pos++;
        }

        size_t end_pos = sql.find_first_of(" ;", from_pos); 
        string table_name = sql.substr(from_pos, end_pos - from_pos);

        // 去除表名前后的空格
        table_name.erase(0, table_name.find_first_not_of(' '));
        table_name.erase(table_name.find_last_not_of(' ') + 1);

        return table_name;
    }
    else if (insert_pos != string::npos) {
        insert_pos += 11;  // 跳过 "insert into"

        // 跳过空格
        while (insert_pos < sql.size() && isspace(sql[insert_pos])) {
            insert_pos++;
        }
        size_t end_pos = sql.find_first_of(" (;", insert_pos); 
        string table_name = sql.substr(insert_pos, end_pos - insert_pos);

        // 去除表名前后的空格
        table_name.erase(0, table_name.find_first_not_of(' '));
        table_name.erase(table_name.find_last_not_of(' ') + 1);

        return table_name;
    }
    else if (delete_pos != string::npos) {
        delete_pos += 11;  // 跳过 "delete from"

        // 跳过空格
        while (delete_pos < sql.size() && isspace(sql[delete_pos])) {
            delete_pos++;
        }

        size_t end_pos = sql.find_first_of(" ;", delete_pos); 
        string table_name = sql.substr(delete_pos, end_pos - delete_pos);

        // 去除表名前后的空格
        table_name.erase(0, table_name.find_first_not_of(' '));
        table_name.erase(table_name.find_last_not_of(' ') + 1);

        return table_name;
    }
    return "";
}

string getCreatetableName(const string& sql){
    string sql_lower = sql;
    transform(sql_lower.begin(), sql_lower.end(), sql_lower.begin(), ::tolower);

    istringstream stream(sql_lower);
    string word;
    bool foundCreate = false, foundTable = false;

    while (stream >> word) {
        if (!foundCreate && word == "create") {
            foundCreate = true;
        } else if (foundCreate && !foundTable && word == "table") {
            foundTable = true;
        } else if (foundTable) {
            return word;  // 表名是 TABLE 之后的第一个单词
        }
    }
    return "";
}

vector<string> extractColumnNames(string &input) {
    vector<string> columnNames;
    istringstream ss(input);
    string token;
    while (std::getline(ss, token, ',')) {
        istringstream pairStream(token);
        string colName, typeName;   
        if (pairStream >> colName >> typeName) {
            columnNames.push_back(colName);
        }
    }
    return columnNames;
}

string prf(const string& key, const string& message, int lambda) {
    unsigned char* result;
    unsigned int len = SHA256_DIGEST_LENGTH; 

    result = HMAC(EVP_sha256(), key.c_str(), key.size(),
                  reinterpret_cast<const unsigned char*>(message.c_str()), message.size(), 
                  NULL, NULL);

    ostringstream oss;
    int bytes_to_output = lambda / 8;
    for (int i = 0; i < bytes_to_output; i++) {
        oss << hex << setw(2) << setfill('0') << (int)result[i];
    }
    return oss.str();
}

string double_quote_values(string& input) {
    string output;
    for (char c : input) {
        if (c == '\'') {
            output += "''";  // 双写单引号
        } else {
            output += c;
        }
    }
    return output;
}

unsigned char* str2hex(const char* str) {
    int str_len = strlen(str);
    // key must be meaningful
    assert((str_len % 2) == 0);
    unsigned char* ret = (unsigned char*)malloc(str_len / 2);
    for (int i = 0; i < str_len; i += 2) {
        sscanf(str + i, "%2hhx", &ret[i / 2]);
    }
    return ret;
}

char* hex2str(const unsigned char* hex, int hex_len) {
    char* ret = (char*)malloc(hex_len * 2 + 1);
    for (int i = 0; i < hex_len; ++i) {
        sprintf(ret + i * 2, "%02x", hex[i]);
    }
    ret[hex_len * 2] = '\0'; // Null-terminate the string
    return ret;
}

unsigned char* aes_encrypt(const unsigned char* plaintext, int plaintext_len,  int& ciphertext_len) {
    unsigned char* key = str2hex(AESKEY);
    unsigned char* iv = str2hex(AESIV);
    // 创建加密上下文
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

    // 对加密操作初始化，使用aes_128_cbc算法，key，iv
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);

    // 获取aes算法块的大小
    int block_size = EVP_CIPHER_block_size(EVP_aes_128_cbc());

    // 补充一个块用于密文填充
    int buffer_len = plaintext_len + block_size;
    unsigned char* ciphertext = (unsigned char*)malloc(buffer_len);
    int len;

    // 初步加密
    EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
    ciphertext_len = len;

    // 处理可能存在的填充
    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    ciphertext_len += len;
    // 释放加密上下文
    
    EVP_CIPHER_CTX_free(ctx);
    free(iv);
    free(key);
    iv = NULL;
    key = NULL;
    return ciphertext;
}

unsigned char* aes_decrypt(const unsigned char* ciphertext, int ciphertext_len,  int& plaintext_len) {
    unsigned char* key = str2hex(AESKEY);
    unsigned char* iv = str2hex(AESIV);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);

    int buffer_len = ciphertext_len;
    unsigned char* plaintext = (unsigned char*)malloc(buffer_len);
    int len;
    
    EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len);
    plaintext_len = len;

    EVP_DecryptFinal_ex(ctx, plaintext + len, &len);
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    free(iv);
    free(key);
    iv = NULL;
    key = NULL;
    return plaintext;
}

string int128_to_string(__int128 value) {
    if (value == 0) {
        return "0";
    }
    string result;
    bool is_negative = value < 0;
    if (is_negative) {
        value = -value;
    }
    while (value > 0) {
        result.push_back('0' + (value % 10));
        value /= 10;
    }
    if (is_negative) {
        result.push_back('-');
    }
    reverse(result.begin(), result.end());
    return result;
}

__int128 string_to_int128(const string& str) {
    if (str.empty()) {
        throw invalid_argument("Input string is empty");
    }

    __int128 result = 0;
    bool is_negative = false;
    size_t i = 0;

    if (str[0] == '-') {
        is_negative = true;
        i++;
    } else if (str[0] == '+') {
        i++;
    }

    for (; i < str.size(); i++) {
        if (!isdigit(str[i])) {
            throw invalid_argument("Invalid character in input string");
        }
        result = result * 10 + (str[i] - '0');
    }

    if (is_negative) {
        result = -result;
    }

    return result;
}


// 将字符转换为 8 位二进制字符串
string char_to_binary_string(char c) {
    return bitset<8>(static_cast<unsigned char>(c)).to_string();
}

// 将8位二进制字符串转换为字符
char binary_string_to_char(string& binary_string) {
    return static_cast<char>(bitset<8>(binary_string).to_ulong());
}

// 将非16进制字符串转换为256二进制字符串
string string_to_binary(const string& input) {
    // check length satisfication
    int len = input.length();
    ostringstream binary_string_stream;
    
    // 将每个字符转换为 8 位二进制并拼接
    for(int i=0;i<len;i++){
        binary_string_stream << char_to_binary_string(input[i]);
    }
    
    // 获取拼接后的字符串
    string binary_string = binary_string_stream.str();
    
    // 确保最终字符串的长度为 256位
    if (binary_string.length() < 256)
        return binary_string + string(256 - binary_string.length(), '0');
    else
        return binary_string;
}

// 二进制转为非16进制string
string binary_to_string(const string& input){
    int len = input.length();
    if (len > 256)
        return NULL;
    string result;
    for (int i = 0; i<len; i+=8){
        string byte_str = input.substr(i, 8);
        if(byte_str == "00000000")
            break;
        result += binary_string_to_char(byte_str);
    }

    return result;
}
