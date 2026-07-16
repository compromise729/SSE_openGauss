#include "libpq/SSE_map.h"

const char *SSE_KIND_NAME[] = {"undefined", "fides", "mitra", "fbdsse", "orion", "dual", "closefb", "januspp"};

unordered_map<string, sse_kind> ssestr_to_enum = {
    {"undefined", sse_kind::UNDEFINED},
    {"fides", sse_kind::FIDES},
    {"mitra", sse_kind::MITRA},
    {"fbdsse", sse_kind::FBDSSE},
    {"orion", sse_kind::ORION},
    {"dual", sse_kind::DUAL},
    {"closefb", sse_kind::CLOSEFB},
    {"januspp", sse_kind::JANUSPP},
    {"maxtype", sse_kind::MAXTYPE}
};

map<string, vector<sse_col>> sse_table_map;

const char *sse_kind_to_string(sse_kind type)
{
    if (type >= sse_kind::UNDEFINED && type < sse_kind::MAXTYPE) {
        return SSE_KIND_NAME[static_cast<int>(type)];
    }
    return "unknown";
}

sse_kind sse_kind_from_string(const char *s)
{
  for (unsigned int i = 0; i < sizeof(SSE_KIND_NAME) / sizeof(SSE_KIND_NAME[0]); i++) {
    if (0 == strcasecmp(SSE_KIND_NAME[i], s)) {
      return (sse_kind)i;
    }
  }
  return sse_kind::UNDEFINED;
}

void table_map_init(const char* sse_map_path){
    ifstream sse_map(sse_map_path);
    if (!sse_map.is_open()) {
        cerr << "Error: Unable to open file: " << sse_map_path << endl;
        return;
    }
    string line;
    while (getline(sse_map, line)) {
        istringstream iss(line);
        string key;
        vector<sse_col> values;
        if (iss >> key) {
            string value;
            while (iss >> value) {
                size_t pos = value.find('|');
                sse_col col;
                col.col_name = value.substr(0, pos);
                string ssestr = value.substr(pos + 1);
                // 查找映射
                auto it = ssestr_to_enum.find(ssestr);
                if (it != ssestr_to_enum.end())
                    col.kind = it->second;
                else
                    col.kind = sse_kind::UNDEFINED;
                values.push_back(col);
            }
            // 将 key 和 values 存入 map
            sse_table_map[key] = values;
        }
    }
    sse_map.close();
}

void appendTableMap(const std::string& tableName, const std::vector<sse_col>& columns, const std::string& filename) {
    std::ofstream ofs(filename, std::ios::app); // 打开文件以追加模式
    if (!ofs) {
        //std::cerr << "无法打开文件进行写入：" << filename << std::endl;
        return;
    }

    ofs << tableName;  // 写入表名
    for (const auto& col : columns) {
        ofs << " " << col.col_name << "|" << sse_kind_to_string(col.kind);  // 写入列名和 sse_kind
    }
    ofs << std::endl;

    ofs.close();
}

void writeTableMap(const std::map<std::string, std::vector<sse_col>>& sse_table_map, const std::string& filename) {
    std::ofstream ofs(filename, std::ios::trunc); // 打开文件以覆盖模式
    if (!ofs) {
        //std::cerr << "无法打开文件进行写入：" << filename << std::endl;
        return;
    }

    // 遍历整个 sse_table_map，写入每个表的数据
    for (const auto& pair : sse_table_map) {
        const std::string& tableName = pair.first;
        const std::vector<sse_col>& columns = pair.second;

        ofs << tableName;  // 写入表名
        for (const auto& col : columns) {
            ofs << " " << col.col_name << "|" << sse_kind_to_string(col.kind);  // 写入列名和 sse_kind
        }
        ofs << std::endl;
    }
    ofs.close();
}

void table_map_write(const char* sse_map_path) {
    ofstream sse_map(sse_map_path);
    if (!sse_map.is_open()) {
        cerr << "Error: Unable to open file for writing: " << sse_map_path << endl;
        return;
    }
    for (const auto& pair : sse_table_map) {
        const string& key = pair.first;
        const vector<sse_col>& values = pair.second;
        sse_map << key;
        for (const sse_col& col : values) {
            string ssestr;
            for (const auto& enum_pair : ssestr_to_enum) {
                if (enum_pair.second == col.kind) {
                    ssestr = enum_pair.first;
                    break;
                }
            }
            sse_map << " " << col.col_name << "|" << ssestr;
        }
        sse_map << endl;
    }
    sse_map.close();
}