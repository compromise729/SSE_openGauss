#pragma once
#include <map>
#include <fstream>
#include <sstream>
#include <vector>
#include <tuple>
#include <set>
#include <unordered_map>

// enum class sse_kind
// {
//   UNDEFINED,   ///< 没有用SSE
//   FIDES,     
//   MITRA,
//   FBDSSE,
//   MAXTYPE,   ///< 请在 UNDEFINED 与 MAXTYPE 之间增加新类型     
// };

// struct sse_col
// {
//     std::string col_name;
//     sse_kind kind;
// };

extern bool common_sse_join ;

extern std::string storage_dir_path;
extern std::string table_file;
extern std::string join_key;

// extern std::map<std::string, std::vector<sse_col>> sse_table_map;

// const char *sse_kind_to_string(sse_kind type);
// sse_kind    sse_kind_from_string(const char *s);

// void table_map_init(const std::string& table_map_path);

// 向文件中追加写入一条字典记录
// void appendTableMap(const std::string& tableName, const std::vector<sse_col>& columns, const std::string& filename);

// 将字典内容覆盖写入文件
// void writeTableMap(const std::map<std::string, std::vector<sse_col>>& sse_table_map, const std::string& filename);

std::string getTableNameFromInsertSQL(std::string insertSQL);
std::string getTableNameFromSelectSQL(std::string selectSQL);
std::string getTableNameFromDeleteSQL(std::string deleteSQL);
std::tuple<std::string, std::string, std::string> getTableNameAndConditionsFromDeleteSQL(std::string deleteSQL);
std::set<std::string> getTableNameFromJoinSQL(std::string& sql);
std::string getJoinType(std::string& query) ;
void extractValues(const std::string& sql, std::vector<std::pair<std::string, std::string>>& firstTwoColumns,  std::vector<std::vector<std::string>>& allColumns);
void splitRow(const std::string& row, std::vector<std::string>& result);
void parseValuesSection(const std::string& sql, std::vector<std::pair<std::string, std::string>>& firstTwoColumns, std::vector<std::vector<std::string>>& allColumns);
std::map<std::string, std::set<std::string>> getWhereConditions(std::string& sql);
std::vector<std::string> getMulSelectFromJoin(std::string input);

std::string parser_create_table(std::string createTableSQL);
std::string double_quote_values(const std::string &sql) ;

// 将字典内容覆盖写入文件
void writeDictionary(const std::map<std::string, int>& dictionary, const std::string& filename);
void writeDictionary(const std::unordered_map<std::string, int>& dictionary, const std::string& filename);

// 向文件中追加写入一条字典记录
void appendDictionaryEntry(const std::string& key, int value, const std::string& filename);

// 从文件中读取并恢复字典内容
std::map<std::string, int> loadDictionary(const std::string& filename);
namespace sse::sophos {
    // 包装函数声明（注入到命名空间中）
    std::map<std::string, int> wrapped_loadDictionary(const std::string& filename);
}

std::unordered_map<std::string, int> loadDictionary2(const std::string& filename);
namespace sse::sophos {
    // 包装函数声明（注入到命名空间中）
    std::unordered_map<std::string, int> wrapped_loadDictionary2(const std::string& filename);
}

void get_and_increment(const std::string& filename, std::map<std::string, int>& keymap, std::string keyword, int& kw_counter);

static inline bool is_base64(unsigned char c);
std::string base64_encode(const std::string &data);
std::string base64_decode(std::string const& encoded_string);

// std::string generate_random_value() ;
std::string generate_aes_key(const std::string& file_path) ;

std::string aes_encrypt(const std::string& pt, const std::string& key);
std::string aes_decrypt(const std::string& ct, const std::string& key) ;

//parse
void sse_parse(std::string qs_cpy, std::string qs_ori, bool ind, std::vector<std::pair<std::string,std::string>>& index, std::vector<std::tuple<std::string,std::string,std::string>>& data);
std::string generateJoinSQL1(std::map<std::string, std::string> t1_join_value_pt, std::map<std::string, std::string> t2_join_value_pt, std::map<std::string, std::vector<std::string>>& result_map1, std::map<std::string, std::vector<std::string>>& result_map2, 
                                           std::string table1, std::string table2, std::vector<std::string>& left_ind, std::vector<std::string>& right_ind, std::string join_mode);


                     