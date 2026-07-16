#include "libpq/Client_FBDSSE.h"
#include "libpq/sse_common.h"
std::string FB_storage_dir_path  = "/opt/og/openGauss-server/sse_storage";

map<string, pair<int, string>> read_file(const string &filename) {
    map<string, pair<int, string>> dictionary;
    
    // 打开文件
    ifstream file(filename);
    if (!file.is_open()) {
        return dictionary;  // 如果文件打开失败，返回空的字典
    }

    string line;

    // 逐行读取文件内容
    while (getline(file, line)) {
        stringstream ss(line);
        string key, countStr, st;

        // 读取并分割字符串
        getline(ss, key, ',');      // 读取第一个部分（string的keyword）
        getline(ss, countStr, ','); // 读取第二个部分（int的count）
        getline(ss, st);            // 读取第三个部分（string的st）

        // 将 countStr 转换为 int
        int count = stoi(countStr);

        // 将数据存入字典
        dictionary[key] = make_pair(count, st);
    }

    // 关闭文件
    file.close();
    return dictionary;  // 返回字典
}

unordered_map<string, pair<int, string>> read_file2(const string &filename) {
    unordered_map<string, pair<int, string>> dictionary;

    // 打开文件
    ifstream file(filename);
    if (!file.is_open()) {
        return dictionary;  // 如果文件打开失败，返回空的字典
    }

    string line;

    // 逐行读取文件内容
    while (getline(file, line)) {
        stringstream ss(line);
        string key, countStr, st;

        // 读取并分割字符串
        getline(ss, key, ',');      // 读取第一个部分（string的keyword）
        getline(ss, countStr, ','); // 读取第二个部分（int的count）
        getline(ss, st);            // 读取第三个部分（string的st）

        // 将 countStr 转换为 int
        int count = stoi(countStr);

        // 将数据存入字典
        dictionary[key] = make_pair(count, st);
    }

    // 关闭文件
    file.close();
    return dictionary;  // 返回字典
}

void FB_write_file(const string &filename, map<string, pair<int, string>> &CT) {
    ofstream file(filename);  // 打开文件（如果文件不存在会创建）

    if (!file.is_open()) {
        // cerr << "Failed to open the file for writing." << endl;
        return;
    }

    // 遍历字典CT，将每个元素写入文件
    for (const auto &entry : CT) {
        // 格式化输出: keyword, count, st
        file << entry.first << "," << entry.second.first << "," << entry.second.second << endl;
    }

    file.close();  // 关闭文件
}

void FB_write_file(const string &filename, unordered_map<string, pair<int, string>> &CT) {
    ofstream file(filename);  // 打开文件（如果文件不存在会创建）

    if (!file.is_open()) {
        return;
    }

    for (const auto &entry : CT) {
        // 格式化输出: keyword, count, st
        file << entry.first << "," << entry.second.first << "," << entry.second.second << endl;
    }

    file.close();  // 关闭文件
}

void append_file(const string &filename, string key, int cnt, string st) {
    ofstream file(filename, std::ios::app);   // 打开文件（如果文件不存在会创建）

    if (!file.is_open()) {
        return;
    }

    file << key << "," << cnt << "," << st << endl;

    file.close();  // 关闭文件
}

// 生成一个指定长度的伪随机字符串
string generate_random_string(size_t length) {
    // 定义字符集：大小写字母和数字
    const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    // 使用std::random_device获取一个随机种子
    std::random_device rd;
    // 使用该种子初始化一个随机数引擎
    std::mt19937 gen(rd());
    // 定义一个uniform_int_distribution来从字符集选择随机字符
    std::uniform_int_distribution<> dis(0, charset.size() - 1);

    std::string random_str;
    random_str.reserve(length);  // 预留空间

    // 生成指定长度的随机字符串
    for (size_t i = 0; i < length; ++i) {
        random_str += charset[dis(gen)];
    }

    return random_str;
}

// 异或两个字符串
std::string xor_strings(const std::string& str1, const std::string& str2) {
    // 确保两个字符串长度相同，如果不相同，可以选择填充（此处为不处理，假设长度相同）
    size_t length = std::min(str1.size(), str2.size());
    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        result += str1[i] ^ str2[i];  // 字符的异或操作
    }

    return result;
}

std::string generate_binary_string(const std::string& input) {
    int position = std::stoi(input); 
    std::string result;
    if(position<FBDSSEClient::N)
    {
        result = "1";  
        result.append(position - 1, '0');  
    }
    else
    {
        result = "0";  
    }
    return result;
}

std::vector<int> generate_ind(const std::string& binary_string) {
    std::vector<int> positions;
    int size=binary_string.size();
    for (size_t i = 0; i < size; i++) {
        if (binary_string[i] == '1') {
            positions.push_back(size - i);  // 将位置从0开始改为从1开始
        }
    }
    return positions;
}

// 使用libsodium的crypto_generichash进行哈希
string sodium_hash(const std::string& str) {
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


FBDSSEClient::FBDSSEClient()
{
	counter_map = read_file2(FB_storage_dir_path+"/fbdsse_client.mat");
}

FBDSSEClient::~FBDSSEClient()
{
}

FBDSSEClient* init_FBDSSEClient()
{
    return new FBDSSEClient();
}

std::string FBDSSEClient::get_and_increment(std::map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter)
{
	auto it = keymap.find(keyword);
	if (it != keymap.end())
	{
		kw_counter = ++keymap[keyword].first;
        string st_old=counter_map[keyword].second;
        string st_new=generate_random_string(kKeywordIndexSize);
        counter_map[keyword].second=st_new;
		FB_write_file(FB_storage_dir_path+"/fbdsse_client.mat", keymap);
        return st_old;
	}
	else
	{
        string st0=generate_random_string(kKeywordIndexSize);
        string st1=generate_random_string(kKeywordIndexSize);
		keymap.insert( make_pair(keyword, make_pair(1, st1)) );
		append_file(FB_storage_dir_path+ "/fbdsse_client.mat", keyword, 1, st1);
		kw_counter = 1;
        return st0;
	}
}

std::string FBDSSEClient::get_and_increment(std::unordered_map<std::string, std::pair<int, std::string>>& keymap, std::string keyword, int& kw_counter)
{
    auto it = keymap.find(keyword);
    if (it != keymap.end())
    {
        kw_counter = ++(it->second.first);
        std::string st_old = it->second.second;
        std::string st_new = generate_random_string(kKeywordIndexSize);
        it->second.second = st_new;
        FB_write_file(FB_storage_dir_path + "/fbdsse_client.mat", keymap);
        return st_old;
    }
    else
    {
        std::string st0 = generate_random_string(kKeywordIndexSize);
        std::string st1 = generate_random_string(kKeywordIndexSize);
        keymap[keyword] = std::make_pair(1, st1);
        append_file(FB_storage_dir_path + "/fbdsse_client.mat", keyword, 1, st1);
        kw_counter = 1;
        return st0;
    }
}

std::string FBDSSEClient::get_and_increment(std::unordered_map<std::string, std::pair<int, std::string>>& keymap, std::string keyword, int& kw_counter, bool writefile)
{
    auto it = keymap.find(keyword);
    if (it != keymap.end())
    {
        kw_counter = ++(it->second.first);
        std::string st_old = it->second.second;
        std::string st_new = generate_random_string(kKeywordIndexSize);
        it->second.second = st_new;
        if(writefile)
        {
            FB_write_file(FB_storage_dir_path + "/fbdsse_client.mat", keymap);
        }
        return st_old;
    }
    else
    {
        std::string st0 = generate_random_string(kKeywordIndexSize);
        std::string st1 = generate_random_string(kKeywordIndexSize);
        keymap[keyword] = std::make_pair(1, st1);
        // append_file(FB_storage_dir_path + "/fbdsse_client.mat", keyword, 1, st1);
        if(writefile)
        {
            FB_write_file(FB_storage_dir_path + "/fbdsse_client.mat", keymap);
        }
        kw_counter = 1;
        return st0;
    }
}

std::string FBDSSEClient::re_get_and_increment(std::map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter)
{
	auto it = keymap.find(keyword);
	if (it != keymap.end())
	{
        kw_counter++;
        string st0=generate_random_string(kKeywordIndexSize);
        string st1=generate_random_string(kKeywordIndexSize);
		it->second = make_pair(1, st1);
		FB_write_file(FB_storage_dir_path+"/fbdsse_client.mat", keymap);
        return st0;
	}
}

std::string FBDSSEClient::re_get_and_increment(std::unordered_map<std::string, std::pair<int, std::string>>& keymap, std::string keyword, int& kw_counter)
{
    auto it = keymap.find(keyword);
    if (it != keymap.end())
    {
        kw_counter++;
        std::string st0 = generate_random_string(kKeywordIndexSize);
        std::string st1 = generate_random_string(kKeywordIndexSize);
        it->second = std::make_pair(1, st1);
        FB_write_file(FB_storage_dir_path + "/fbdsse_client.mat", keymap);
        return st0;
    }

    // 如果 keyword 不存在，函数没有返回值，补充默认返回（必要时）
    return "";
}

FB_UpdateRequest FBDSSEClient::FB_insert(string keyword, string ind)
{
    FB_UpdateRequest update;
    int count=0;
    string st0=get_and_increment(counter_map, keyword, count);
    string bs=generate_binary_string(ind);

    string st=counter_map[keyword].second;
    

    update.ut =sodium_hash(st);
    update.ct =base64_encode(xor_strings(st0,st));

    mpz_t plaintext, ciphertext;
    mpz_inits(plaintext, ciphertext, nullptr);
    mpz_set_str(plaintext, bs.c_str(), 2);
    encrypt(ciphertext, plaintext, keyword, count);  // 使用计数器
     // 将大整数转换为字符串（2进制）
    char *str = mpz_get_str(NULL, 2, ciphertext);
    // 转换为 std::string
    update.e=str;

    return update;
}

FB_UpdateRequest FBDSSEClient::FB_insert(string keyword, string ind, bool writefile)
{
    FB_UpdateRequest update;
    int count=0;
    string st0=get_and_increment(counter_map, keyword, count, writefile);
    string bs=generate_binary_string(ind);

    string st=counter_map[keyword].second;
    

    update.ut =sodium_hash(st);
    update.ct =base64_encode(xor_strings(st0,st));

    mpz_t plaintext, ciphertext;
    mpz_inits(plaintext, ciphertext, nullptr);
    mpz_set_str(plaintext, bs.c_str(), 2);
    encrypt(ciphertext, plaintext, keyword, count);  // 使用计数器
     // 将大整数转换为字符串（2进制）
    char *str = mpz_get_str(NULL, 2, ciphertext);
    // 转换为 std::string
    update.e=str;

    return update;
}

FB_UpdateRequest FBDSSEClient::FB_re_insert(string keyword, string inds)
{
    FB_UpdateRequest update;
    int count=0;
    string st0=re_get_and_increment(counter_map, keyword, count);
    string bs=inds;

    string st=counter_map[keyword].second;
    

    update.ut =sodium_hash(st);
    update.ct =base64_encode(xor_strings(st0,st));

    mpz_t plaintext, ciphertext;
    mpz_inits(plaintext, ciphertext, nullptr);
    mpz_set_str(plaintext, bs.c_str(), 2);
    encrypt(ciphertext, plaintext, keyword, count);  // 使用计数器
     // 将大整数转换为字符串（2进制）
    char *str = mpz_get_str(NULL, 2, ciphertext);
    // 转换为 std::string
    update.e=str;

    return update;
}

FB_SearchRequest FBDSSEClient::FB_search(string keyword)
{
    FB_SearchRequest search;
    auto it = counter_map.find(keyword);
	if (it != counter_map.end())
	{
		search.count=it->second.first;
        search.st=it->second.second;
	}
	else
	{
	}
    return search;
}


FBDSSEClient* FBdsse_Client=nullptr;