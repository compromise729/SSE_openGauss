#ifndef OPERATION_UTILS_H
#define OPERATION_UTILS_H
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <pthread.h>
#include <set>
#include <unistd.h>
#include <bitset>
#include <cassert> 
#include <openssl/hmac.h> 
#include <openssl/sha.h>
using namespace std;

extern pthread_mutex_t file_mutex;  

#define AESKEY "d1c6f7a55b25e9d2b5d2bf18d5c2ed1a"
#define AESIV "3569ae97418c69732964589bdfe19a4f"

/* 每次随机生成字符串时会更新种子 */
void update_seed();

/* 写或读取unordered_map<string, pair<string, string> >结构的数据*/
int readPairmap(const char *file_path, unordered_map<string, pair<string, string> > &pairmap);
int writePairmap(const char *file_path, unordered_map<string, pair<string, string> > &pairmap);

/* 写或读取set */
int readset(const char *file_path, set<string> &sset);
int writeset(const char *file_path, set<string> &sset);

/* 写或读取map结构的数据，key->value，其中initmap是对其进行初始化 */
/* 如果value是数值类型，初始化为0；字符串类型初始化为空 */
int readmap(const char *file_path, map<string, string> &mmap);
int writemap(const char *file_path, map<string, string> &mmap);
void initmap(string key, map<string, string> &mmap, int iscnt);
/* map中根据key获取value，其中iscnt代表value是不是数值*/
string getmapvalue(string key, map<string, string> &mmap, int iscnt);


/* 对sql初始语句获取''的内容 */
void extract_strings(const string& input, vector<string>& result);
/* insert语句时，获取每一个tuple*/
void get_Inserttuple(const string& input, vector<string>& result);

/* 随机生成指定长度的字符串，最终是16进制 */
string generate_random_key(int length, set<string> &generated_keys);

/* 两个字符串进行异或运算，其中str1和str2都是01比特串*/
string xorstring(const string &str1, const string &str2);

/* 将16进制字符串转为字节 */
string hexTobytes(const string& hexstr);

/* 二进制串转化为16进制串 */
string binaryTohex(string binary_string);

/* 16进制串转化为二进制串 */
string hexTobinary(string hex_string);

/* 从sql语句中获取表名 */
string getTableNameFromSQL(const string& sql);

/* 从create table语句中获取表名*/
string getCreatetableName(const string& sql);

/* 获取value列名 */
vector<string> extractColumnNames(string &input);

/* 伪随机函数prf，最终返回16进制串*/
string prf(const string& key, const string& message, int lambda);

/* 双写单引号，用于data_sql*/
string double_quote_values(string& input);

/* AES 专用*/
unsigned char* str2hex(const char* str);
char* hex2str(const unsigned char* hex, int hex_len);
unsigned char* aes_encrypt(const unsigned char* plaintext, int plaintext_len,  int& ciphertext_len);
unsigned char* aes_decrypt(const unsigned char* ciphertext, int ciphertext_len,  int& plaintext_len);

/* string和int128转换 */
string int128_to_string(__int128 value);
__int128 string_to_int128(const string& str);

// 将字符转换为 8 位二进制字符串
string char_to_binary_string(char c);
// 将8位二进制字符串转换为字符
char binary_string_to_char(string& binary_string);

// 将非16进制字符串转换为256二进制字符串
string string_to_binary(const string& input);
// 二进制转为非16进制string
string binary_to_string(const string& input);         

#endif 