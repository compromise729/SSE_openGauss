#ifndef SSEMAP_H
#define SSEMAP_H
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <strings.h>
using namespace std;

enum class sse_kind
{
  UNDEFINED,   ///< 没有用SSE
  FIDES,     
  MITRA,
  FBDSSE,
  ORION,
  DUAL,
  CLOSEFB,
  JANUSPP,
  MAXTYPE,   ///< 请在 UNDEFINED 与 MAXTYPE 之间增加新类型     
};

typedef struct
{
    string col_name;
    sse_kind kind;
} sse_col;

extern const char *SSE_KIND_NAME[];

extern unordered_map<string, sse_kind> ssestr_to_enum;

extern map<string, vector<sse_col>> sse_table_map;

const char *sse_kind_to_string(sse_kind type);
sse_kind    sse_kind_from_string(const char *s);
void table_map_init(const char* sse_map_path);
void table_map_write(const char* sse_map_path);

// 向文件中追加写入一条字典记录
void appendTableMap(const std::string& tableName, const std::vector<sse_col>& columns, const std::string& filename);
// 将字典内容覆盖写入文件
void writeTableMap(const std::map<std::string, std::vector<sse_col>>& sse_table_map, const std::string& filename);


#endif 

