#include "libpq/sse_common.h"
#include "libpq/SSE_map.h"
#include "libpq/row_state_v1.h"
#include <stdexcept>
#include <strings.h>
#include <regex>
#include <iomanip>
#include <set>
#include <openssl/aes.h>
#include <openssl/rand.h>

bool common_sse_join=false;

std::string storage_dir_path  = "/opt/og/openGauss-server/sse_storage";
std::string table_file  = "sse_table.mat";
std::string join_key  = "join_secret_key.key";

// std::map<std::string, std::vector<sse_col>> sse_table_map;

// const char *SSE_KIND_NAME[] = {"undefined", "fides", "mitra", "fbdsse"};

// const char *sse_kind_to_string(sse_kind type)
// {
//   if (type >= sse_kind::UNDEFINED && type < sse_kind::MAXTYPE) {
//     return SSE_KIND_NAME[static_cast<int>(type)];
//   }
//   return "unknown";
// }

// sse_kind sse_kind_from_string(const char *s)
// {
//   for (unsigned int i = 0; i < sizeof(SSE_KIND_NAME) / sizeof(SSE_KIND_NAME[0]); i++) {
//     if (0 == strcasecmp(SSE_KIND_NAME[i], s)) {
//       return (sse_kind)i;
//     }
//   }
//   return sse_kind::UNDEFINED;
// }

// void table_map_init(const std::string& table_map_path)
// {
//     std::ifstream file(table_map_path); 
//     if (!file.is_open()) {
//         //error
//     }
//     else
//     {
//         std::string line;
//         // 逐行读取文件
//         while (std::getline(file, line)) {
//             std::istringstream lineStream(line);
//             std::string tableName;
//             lineStream >> tableName; // 读取表名

//             std::vector<sse_col> columns;
//             std::string columnData;

//             // 处理当前行的每个列数据
//             while (lineStream >> columnData) {
//                 size_t separator = columnData.find('|');
//                 if (separator == std::string::npos) {
//                     //std::cerr << "列数据格式无效\n";
//                     continue;
//                 }

//                 std::string colName = columnData.substr(0, separator);
//                 std::string kindStr = columnData.substr(separator + 1);
//                 sse_kind kind = sse_kind_from_string(kindStr.c_str());

//                 columns.push_back(sse_col{colName, kind});
//             }

//              sse_table_map[tableName] = columns; // 将表名和列数据添加到 map 中
//         }
//     }
// }

// void appendTableMap(const std::string& tableName, const std::vector<sse_col>& columns, const std::string& filename) {
//     std::ofstream ofs(filename, std::ios::app); // 打开文件以追加模式
//     if (!ofs) {
//         //std::cerr << "无法打开文件进行写入：" << filename << std::endl;
//         return;
//     }

//     ofs << tableName;  // 写入表名
//     for (const auto& col : columns) {
//         ofs << " " << col.col_name << "|" << sse_kind_to_string(col.kind);  // 写入列名和 sse_kind
//     }
//     ofs << std::endl;

//     ofs.close();
// }

// void writeTableMap(const std::map<std::string, std::vector<sse_col>>& sse_table_map, const std::string& filename) {
//     std::ofstream ofs(filename, std::ios::trunc); // 打开文件以覆盖模式
//     if (!ofs) {
//         //std::cerr << "无法打开文件进行写入：" << filename << std::endl;
//         return;
//     }

//     // 遍历整个 sse_table_map，写入每个表的数据
//     for (const auto& pair : sse_table_map) {
//         const std::string& tableName = pair.first;
//         const std::vector<sse_col>& columns = pair.second;

//         ofs << tableName;  // 写入表名
//         for (const auto& col : columns) {
//             ofs << " " << col.col_name << "|" << sse_kind_to_string(col.kind);  // 写入列名和 sse_kind
//         }
//         ofs << std::endl;
//     }
//     ofs.close();
// }


std::string getTableNameFromInsertSQL(std::string insertSQL) {
    // 匹配可选的前缀（由非空格字符和下划线组成），后跟 "INSERT INTO 表名"
    std::regex insertRegex(R"((?:\w+_)?INSERT\s+INTO\s+(\w+))", std::regex::icase);
    std::smatch match;

    if (std::regex_search(insertSQL, match, insertRegex) && match.size() > 1) {
        return match[1].str();  // 获取第一个捕获组中的表名
    }
    return "";  // 如果未找到表名则返回空字符串
}

std::string getTableNameFromSelectSQL(std::string selectSQL) {
    // 匹配可选的前缀（由非空格字符和下划线组成），后跟 "匹配 "SELECT ... FROM 表名"
    std::regex selectRegex(R"((?:\w+_)?SELECT\s+.*\s+FROM\s+(\w+))", std::regex::icase); 
    std::smatch match;
    if (std::regex_search(selectSQL, match, selectRegex) && match.size() > 1) {
        return match[1].str();  // 获取第一个捕获组中的表名
    }
    return "";  // 如果未找到表名则返回空字符串
}

std::string getTableNameFromDeleteSQL(std::string deleteSQL) {
    // 匹配可选的前缀（由非空格字符和下划线组成），后跟 DELETE FROM 表名 的语句
    std::regex deleteRegex(R"((?:\w+_)?DELETE\s+FROM\s+(\w+))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(deleteSQL, match, deleteRegex) && match.size() > 1) {
        return match[1].str(); // 获取第一个捕获组中的表名
    }
    return ""; // 如果未找到表名则返回空字符串
}

std::tuple<std::string, std::string, std::string> getTableNameAndConditionsFromDeleteSQL(std::string deleteSQL) {
    // 正则匹配删除语句的表名和 WHERE 子句的条件
    std::regex deleteRegex(R"((?:\w+_)?DELETE\s+FROM\s+(\w+)\s+WHERE\s+(\w+)='([^']+)'(?:\s+AND\s+(\w+)='([^']+)')?)", std::regex::icase);
    std::smatch match;

    if (std::regex_search(deleteSQL, match, deleteRegex) && match.size() > 5) {
        // 返回表名，ind 条件，keyword 条件
        std::string tableName = match[1].str();         // 获取表名
        std::string indValue = match[3].str();          // 获取 ind 对应的值
        std::string keywordValue = match[5].str();      // 获取 keyword 对应的值
        return {tableName, indValue, keywordValue};
    }
    return {"", "", ""};  // 如果未找到匹配，则返回空字符串
}

// 提取 SQL 中的表名
std::set<std::string> getTableNameFromJoinSQL(std::string& sql) {
    std::set<std::string> tableNames;
    
    // 正则表达式匹配 SELECT ... FROM <table1> JOIN <table2> ...
    std::regex joinRegex(R"(FROM\s+(\w+)\s+(?:INNER\s+|LEFT\s+|RIGHT\s+|CROSS\s+)?JOIN\s+(\w+))", std::regex::icase);
    std::smatch match;
    
    // 检查是否包含 JOIN 子句
    if (std::regex_search(sql, match, joinRegex)) {
        // 提取表名并添加到 set 中
        tableNames.insert(match[1].str());  // 从 FROM 后提取第一个表名
        tableNames.insert(match[2].str());  // 从 JOIN 后提取第二个表名
    }

    return tableNames;
}

//提取 JOIN 类型
std::string getJoinType(std::string& query) {

    // 检查常见的JOIN类型
    if (query.find("inner join") != std::string::npos) {
        return "INNER";
    } else if (query.find("left join") != std::string::npos) {
        return "LEFT";
    } else if (query.find("right join") != std::string::npos) {
        return "RIGHT";
    } else if (query.find("cross join") != std::string::npos) {
        return "CROSS";
    }
    return "UNKNOWN";
}


//提取INSERT语句的VALUES
void extractValues(const std::string& sql,
    std::vector<std::pair<std::string, std::string>>& firstTwoColumns,
    std::vector<std::vector<std::string>>& allColumns) {
    // Regular expression to match the VALUES section
    std::regex valuesRegex(R"(VALUES\s*(\(.*?\))(,\s*\(.*?\))*)", std::regex::icase);
    std::smatch match;

    if (std::regex_search(sql, match, valuesRegex)) {
        std::string valuesSection = match[0].str(); // Extract the entire VALUES section

        // Match each row in the VALUES section
        std::regex rowRegex(R"(\((.*?)\))");
        std::sregex_iterator rowsBegin(valuesSection.begin(), valuesSection.end(), rowRegex);
        std::sregex_iterator rowsEnd;

        for (std::sregex_iterator it = rowsBegin; it != rowsEnd; ++it) {
            std::string row = (*it)[1].str(); // Extract the content inside a row

            // Split the row into individual values
            std::regex valueRegex(R"('([^']*)'|([^',\s]+))"); // Adjusted regex
            std::sregex_iterator valueBegin(row.begin(), row.end(), valueRegex);
            std::sregex_iterator valueEnd;

            std::vector<std::string> extractedRow;
            for (std::sregex_iterator valIt = valueBegin; valIt != valueEnd; ++valIt) {
                std::string value = (*valIt)[1].matched ? (*valIt)[1].str() : (*valIt)[2].str();

                // Trim whitespace
                value = std::regex_replace(value, std::regex(R"(^\s+|\s+$)"), "");

                // Remove single quotes if present
                value = std::regex_replace(value, std::regex(R"(^'|'$)"), "");

                extractedRow.push_back(value);
            }

            if (!extractedRow.empty()) {
                if (extractedRow.size() >= 2) {
                    firstTwoColumns.emplace_back(extractedRow[0], extractedRow[1]);
                    std::vector<std::string> remainingColumns(extractedRow.begin() + 2, extractedRow.end());
                    allColumns.push_back(remainingColumns); // Store columns excluding the first two
                }
            }
        }
    }
}


void splitRow(const std::string& row, std::vector<std::string>& result) {
    std::string value;
    bool insideQuote = false;  // 跟踪是否在单引号内部

    for (size_t i = 0; i < row.size(); ++i) {
        char c = row[i];

        if (c == '\'' && (i == 0 || row[i - 1] != '\\')) {  // 检查是否为引号，且不是转义引号
            insideQuote = !insideQuote;  // 如果是引号，切换状态
        }

        if (c == ',' && !insideQuote) {  // 如果逗号外部且不是引号内
            // 去掉前后的单引号（如果有的话）
            if (!value.empty() && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);  // 去掉前后单引号
            }

            result.push_back(value);    // 将当前值添加到结果
            value.clear();              // 清空当前值，准备下一项

            // 跳过逗号后的空格（如果有的话）
            while (i + 1 < row.size() && row[i + 1] == ' ') {
                ++i;
            }
        }
        else {
            value += c;  // 正常情况下，直接添加字符
        }
    }

    // 处理最后一个值（没有逗号结尾时）
    if (!value.empty()) {
        // 去掉前后的单引号（如果有的话）
        if (!value.empty() && value.front() == '\'' && value.back() == '\'') {
            value = value.substr(1, value.size() - 2);  // 去掉前后单引号
        }
        result.push_back(value);
    }
}

// 解析 VALUES 部分
void parseValuesSection(const std::string& sql,
    std::vector<std::pair<std::string, std::string>>& firstTwoColumns,
    std::vector<std::vector<std::string>>& allColumns) {
    size_t startPos = sql.find("VALUES");
    if (startPos == std::string::npos) return;

    std::string valuesSection = sql.substr(startPos + 6); // 提取 VALUES 之后的内容
    size_t currentPos = 0, endPos = 0;

    while ((endPos = valuesSection.find("),", currentPos)) != std::string::npos) {
        std::string row = valuesSection.substr(currentPos, endPos - currentPos + 1); // 提取一行
        currentPos = endPos + 2;

        // 去掉括号和空格
        row.erase(0, row.find_first_not_of(" \t\n\r")); // 去掉行首空格
        row.erase(row.find_last_not_of(" \t\n\r") + 1); // 去掉行尾空格
        if (row.front() == '(' && row.back() == ')') {
            row = row.substr(1, row.size() - 2); // 去掉括号
        }

        // 分割字段
        std::vector<std::string> values;
        splitRow(row, values);

        // 保存前两列和后续列
        if (values.size() >= 2) {
            firstTwoColumns.emplace_back(values[0], values[1]);
            allColumns.emplace_back(values.begin() + 2, values.end());
        }
    }

    // 处理最后一行
    if (currentPos < valuesSection.size()) {
        std::string row = valuesSection.substr(currentPos);

        // 去掉前后空格、括号和分号
        row.erase(0, row.find_first_not_of(" \t\n\r"));          // 去掉行首空格
        row.erase(row.find_last_not_of(" \t\n\r") + 1);          // 去掉行尾空格
        if (!row.empty() && row.back() == ';') row.pop_back();   // 去掉行尾分号
        if (!row.empty() && row.front() == '(') row.erase(0, 1); // 去掉行首括号
        if (!row.empty() && row.back() == ')') row.pop_back();   // 去掉行尾括号
        

        std::vector<std::string> values;
        splitRow(row, values);

        if (values.size() >= 2) {
            firstTwoColumns.emplace_back(values[0], values[1]);
            allColumns.emplace_back(values.begin() + 2, values.end());
        }
    }
}


// 提取 WHERE 子句中的过滤条件
std::map<std::string, std::set<std::string>> getWhereConditions(std::string& sql) {
    std::map<std::string, std::set<std::string>> tableConditions;

    // 提取 WHERE 子句
    std::regex whereRegex(R"(\bWHERE\b\s*(.*))", std::regex::icase); 
    std::smatch whereMatch;
    if (std::regex_search(sql, whereMatch, whereRegex)) {
        std::string whereClause = whereMatch[1].str();
        
        // 正则表达式匹配 WHERE 子句中的条件：例如 t1.keyword='a'
        std::regex conditionRegex(R"((\w+)\.(\w+)\s*=\s*'([^']+)')", std::regex::icase);
        std::sregex_iterator condBegin(whereClause.begin(), whereClause.end(), conditionRegex);
        std::sregex_iterator condEnd;

        for (std::sregex_iterator i = condBegin; i != condEnd; ++i) {
            std::smatch condMatch = *i;
            std::string tableName = condMatch[1].str();
            std::string condition = condMatch[2].str() + "=" + "'" + condMatch[3].str() + "'";

            // 转换表名为小写
            std::transform(tableName.begin(), tableName.end(), tableName.begin(), ::tolower);
            tableConditions[tableName].insert(condition);  // 存储表名和对应的过滤条件
        }
    }

    return tableConditions;
}

std::vector<std::string> getMulSelectFromJoin(std::string input)
{
    // 用分号分割字符串
    std::vector<std::string> queries;
    std::string query;
    std::stringstream ss(input);

    // 通过循环逐个提取分号前的子字符串
    while (std::getline(ss, query, ';')) {
        // 去除可能的前后空格
        if (!query.empty()) {
            // 去除两边的空白字符
            query.erase(0, query.find_first_not_of(" \t"));
            query.erase(query.find_last_not_of(" \t") + 1);
            queries.push_back(query);
        }
    }
    return queries;
}


std::string parser_create_table(std::string createTableSQL)
{
    if (createTableSQL.find("SSE='") == std::string::npos) {
        return "";
    }
    
    //  提取表名
    std::regex tableRegex(R"(CREATE\s+TABLE\s+(\w+)\s*\()");
    std::smatch match;
    std::string tableName;

    if (std::regex_search(createTableSQL, match, tableRegex)) {
        tableName = match[1].str();
    } else {
        //std::cerr << "未找到表名\n";
        return "";
    }

    // 提取列定义部分
    std::regex colRegex( R"(\((.*)\))" );  // 匹配第一个括号中的内容
    std::smatch tableMatch;
    if (std::regex_search(createTableSQL, tableMatch, colRegex)) {
        std::string columnsPart = tableMatch[1].str();  // 只取括号内的内容

        // 针对列定义部分提取列名和 SSE 注释
        std::regex columnRegex(R"((\w+)\s+\w+(?:\s+SSE='(\w+)')?)");
        std::sregex_iterator colBegin(columnsPart.begin(), columnsPart.end(), columnRegex);
        std::sregex_iterator colEnd;

        std::vector<sse_col> columns;

        for (std::sregex_iterator i = colBegin; i != colEnd; ++i) {
            std::smatch colMatch = *i;
            std::string colName = colMatch[1].str();
            sse_kind kind = sse_kind::UNDEFINED;

            // 仅在有 SSE 注释时解析
            if (colMatch[2].matched) {
                kind = sse_kind_from_string(colMatch[2].str().c_str() );
            }
            columns.push_back(sse_col{colName, kind});
        }

        // 将结果存入 table_map
        transform(tableName.begin(), tableName.end(), tableName.begin(), (int(*)(int))tolower);
        sse_table_map[tableName] = columns;
        appendTableMap(tableName, columns, storage_dir_path + "/" + table_file);
    }
    return tableName;
}

std::string double_quote_values(const std::string &sql) {
    std::string result;
    result.reserve(sql.size() * 2); // 预分配内存以提高性能

    bool inside_single_quote = false; // 标记是否在单引号内

    for (size_t i = 0; i < sql.size(); ++i) {
        char current = sql[i];

        if (current == '\'') {
            if (!inside_single_quote) {
                // 进入单引号内，加一个额外的单引号
                result += "''";
                inside_single_quote = true;
            } else {
                // 离开单引号内，再加一个额外的单引号
                result += "''";
                inside_single_quote = false;
            }
        } else {
            // 普通字符，直接添加
            result += current;
        }
    }

    return result;
}

// 从文件中读取并恢复字典内容
std::map<std::string, int> loadDictionary(const std::string& filename) {
	std::map<std::string, int> dictionary;
	std::ifstream file(filename);
	if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::size_t pos = line.find_last_of(' ');
            if (pos != std::string::npos) {
                std::string name = line.substr(0, pos);  // 提取名字部分
                int value = std::stoi(line.substr(pos + 1));  // 提取并转换最后的整数部分
                dictionary[name] = value;
            }
        }
        cout << "Dictionary loaded from file: " << filename << endl;
        cout << "Dictionary size: " << dictionary.size() << endl;
        file.close();
    }
	else {
        cout << "Error: Unable to open file for reading: " << filename << endl;
	}
	return dictionary;
}
// 包装函数实现（通过命名空间调用全局函数）
namespace sse::sophos {
    std::map<std::string, int> wrapped_loadDictionary(const std::string& filename) {
        // 使用 :: 显式调用全局函数
        return ::loadDictionary(filename);
    }
}

std::unordered_map<std::string, int> loadDictionary2(const std::string& filename) {
    std::unordered_map<std::string, int> dictionary;
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::size_t pos = line.find_last_of(' ');
            if (pos != std::string::npos) {
                std::string name = line.substr(0, pos);  // 提取名字部分
                int value = std::stoi(line.substr(pos + 1));  // 提取并转换最后的整数部分
                dictionary[name] = value;
            }
        }
        cout << "Dictionary loaded from file: " << filename << endl;
        cout << "Dictionary size: " << dictionary.size() << endl;
        file.close();
    } else {
        cout << "Error: Unable to open file for reading: " << filename << endl;
    }
    return dictionary;
}

namespace sse::sophos {
    std::unordered_map<std::string, int> wrapped_loadDictionary2(const std::string& filename) {
        // 使用 :: 显式调用全局函数
        return ::loadDictionary2(filename);
    }
}

// 将字典内容覆盖写入文件
static void require_v1_state(const sse_v1::Status& status)
{
    if (!status.ok())
        throw std::runtime_error(std::string(sse_v1::code_name(status.code)) + ": " + status.message);
}

void writeDictionary(const std::map<std::string, int>& dictionary, const std::string& filename) {
    if (sse_v1::v1_state_selected()) {
        require_v1_state(sse_v1::save_current_counters(sse_v1::Counters(dictionary.begin(), dictionary.end())));
        return;
    }

	std::ofstream file(filename);
	if (file.is_open()) {
		for (const auto& pair : dictionary) {
			file << pair.first << " " << pair.second << std::endl;
		}
		file.close();
		//cout << "Dictionary written to file: " << filename << endl;
	}
	else {
		//cerr << "Error: Unable to open file for writing: " << filename << endl;
	}
}

void writeDictionary(const std::unordered_map<std::string, int>& dictionary, const std::string& filename) {
    if (sse_v1::v1_state_selected()) {
        require_v1_state(sse_v1::save_current_counters(sse_v1::Counters(dictionary.begin(), dictionary.end())));
        return;
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        for (const auto& pair : dictionary) {
            file << pair.first << " " << pair.second << std::endl;
        }
        file.close();
        // cout << "Dictionary written to file: " << filename << endl;
    } else {
        // cerr << "Error: Unable to open file for writing: " << filename << endl;
    }
}

// 向文件中追加写入一条字典记录
void appendDictionaryEntry(const std::string& key, int value, const std::string& filename) {
    if (sse_v1::v1_state_selected()) {
        int count = 0;
        if (value != 1) throw std::runtime_error("state_invalid: new counter must start at one");
        require_v1_state(sse_v1::increment_counter(key, count, true));
        return;
    }

	std::ofstream file(filename,std::ios::app);
	if (file.is_open()) {
		file << key << " " << value << std::endl;
		file.close();
		//cout << "New entry appended to file: " << filename << endl;
	}
	else {
		//cerr << "Error: Unable to open file for writing: " << filename << endl;
	}
}

void get_and_increment(const std::string& filename, std::map<std::string, int>& keymap, std::string keyword, int& kw_counter)
{
    if (sse_v1::v1_state_selected()) {
        require_v1_state(sse_v1::increment_counter(keyword, kw_counter, true));
        keymap[keyword] = kw_counter;
        return;
    }

	auto it = keymap.find(keyword);
	if (it != keymap.end())
	{
		kw_counter = ++keymap[keyword];
		writeDictionary(keymap, filename);
	}
	else
	{
		keymap.insert(make_pair(keyword, 1));
		appendDictionaryEntry(keyword,1,filename);
		kw_counter = 1;
	}
}

static const std::string base64_chars =
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(const std::string &in) {
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string base64_decode(const std::string &encoded_string) {
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }

    return ret;
}

void sse_parse(std::string qs_cpy, std::string qs_ori, bool ind, std::vector<std::pair<std::string,std::string>>& index, std::vector<std::tuple<std::string,std::string,std::string>>& data)
{
    int pos = qs_cpy.find("(");
    while (pos != std::string::npos) 
    {
    // 找到左括号后的第一个单引号的位置
        size_t start = qs_cpy.find("'", pos);
        if (start != std::string::npos) 
        {
            // 找到第一个单引号后的第二个单引号的位置，表示值的结束
            size_t end = qs_cpy.find("'", start + 1);
            if (end != std::string::npos) 
            {
                // 提取值ind
                std::string first = qs_ori.substr(start + 1, end - start - 1);
                start = qs_cpy.find("'", end + 1);
                if (start != std::string::npos) 
                {
                    end = qs_cpy.find("'", start + 1);
                    if (end != std::string::npos) 
                    {
                        // 提取值keyword
                        std::string second = qs_ori.substr(start + 1, end - start - 1);
                        if(!ind)
                        {
                            start = qs_cpy.find("'", end + 1);
                            if (start != std::string::npos) {
                                end = qs_cpy.find("'", start + 1);
                                if (end != std::string::npos) {
                                    // 提取值value
                                    std::string third = qs_ori.substr(start + 1, end - start - 1);
                                    index.push_back({second, first});
                                    data.push_back(make_tuple(first,second,third));
                                }
                            }
                        }
                        else
                        {
                            index.push_back({second, first});
                        }
                    }
                }
            }
        }
        // 继续查找下一个值对的位置
        pos = qs_cpy.find("(", pos + 1);
    }
}


// 生成或加载AES密钥
std::string generate_aes_key(const std::string& file_path) {
    std::string key;
    std::ifstream key_file(file_path, std::ios::binary);
    // 如果文件存在，读取文件中的密钥
    if (key_file) {
        key.resize(16);  // 假设文件保存的是128位密钥
        key_file.read(&key[0], 16);
        key_file.close();

        if (key.size() != 16) {
            //std::cerr << "Invalid key size from file!" << std::endl;
            return "";
        }
        //std::cout << "AES key loaded from file: " << file_path << std::endl;
    }
    else
    {
        // 如果文件不存在，生成并保存密钥
        unsigned char new_key[16];  // 128位 AES 密钥
        if (!RAND_bytes(new_key, sizeof(new_key))) {
            //std::cerr << "Error generating AES key!" << std::endl;
            return "";
        }

        // 将生成的密钥保存到文件
        std::ofstream key_file(file_path, std::ios::binary);
        if (!key_file) {
            //std::cerr << "Error opening key file for writing!" << std::endl;
            return "";
        }
        key_file.write(reinterpret_cast<char*>(new_key), sizeof(new_key));
        key_file.close();

        // 将密钥转为string形式
        key.assign(reinterpret_cast<char*>(new_key), sizeof(new_key));
    }

    return key;
}

// AES加密函数
std::string aes_encrypt(const std::string& pt, const std::string& key) {
    // 生成一个随机的值v
    unsigned char v[AES_BLOCK_SIZE];
    if (!RAND_bytes(v, sizeof(v))) {
        //std::cerr << "Random value generation failed!" << std::endl;
        return "";
    }
    std::string random_value;
    random_value.assign(reinterpret_cast<char*>(v), AES_BLOCK_SIZE);  // 保存随机值v

    // 设置AES加密器
    AES_KEY aes_key;
    if (AES_set_encrypt_key(reinterpret_cast<const unsigned char*>(key.c_str()), 128, &aes_key) < 0) {
        //std::cerr << "Failed to set AES encryption key!" << std::endl;
        return "";
    }

    // 填充明文到16字节的倍数（如果不满16字节需要填充）
    int length = pt.size();
    int padded_length = ((length / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;
    unsigned char* input_data = new unsigned char[padded_length];
    std::memset(input_data, 0, padded_length);
    std::memcpy(input_data, pt.c_str(), length);

    // 加密
    unsigned char* encrypted_data = new unsigned char[padded_length];
    for (int i = 0; i < padded_length; i += AES_BLOCK_SIZE) {
        AES_cbc_encrypt(input_data + i, encrypted_data + i, AES_BLOCK_SIZE, &aes_key, v, AES_ENCRYPT);
    }

    // 拼接密文ct和随机值v
    std::string ct(reinterpret_cast<char*>(encrypted_data), padded_length);
    ct.append(random_value);

    delete[] input_data;
    delete[] encrypted_data;
    return ct;
}

// AES解密函数
std::string aes_decrypt(const std::string& ct, const std::string& key) {
    // 提取随机值v
    size_t len = ct.size();
    std::string random_value = ct.substr(len - AES_BLOCK_SIZE, AES_BLOCK_SIZE);
    std::string encrypted_data = ct.substr(0, len - AES_BLOCK_SIZE);

    // 设置AES解密器
    AES_KEY aes_key;
    if (AES_set_decrypt_key(reinterpret_cast<const unsigned char*>(key.c_str()), 128, &aes_key) < 0) {
        //std::cerr << "Failed to set AES decryption key!" << std::endl;
        return "";
    }

    // 解密
    unsigned char* decrypted_data = new unsigned char[encrypted_data.size()];
    unsigned char v[AES_BLOCK_SIZE];
    std::memcpy(v, random_value.c_str(), AES_BLOCK_SIZE);  // 将v复制到解密时使用的v中
    for (size_t i = 0; i < encrypted_data.size(); i += AES_BLOCK_SIZE) {
        AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(encrypted_data.c_str()) + i, decrypted_data + i, AES_BLOCK_SIZE, &aes_key, v, AES_DECRYPT);
    }

    // 将解密结果转换为字符串
    std::string pt(reinterpret_cast<char*>(decrypted_data), encrypted_data.size());
    delete[] decrypted_data;
    return pt;
}


std::string generateJoinSQL1(std::map<std::string, std::string> t1_join_value_pt, std::map<std::string, std::string> t2_join_value_pt, std::map<std::string, std::vector<std::string>>& result_map1, std::map<std::string, std::vector<std::string>>& result_map2, 
                                           std::string table1, std::string table2, std::vector<std::string>& left_ind, std::vector<std::string>& right_ind, std::string join_mode)
{
    // 遍历map1
    for (const auto& pair1 : t1_join_value_pt) 
    {
        // 遍历map2，比较值是否相等
        bool just_left=true;
        for (const auto& pair2 : t2_join_value_pt) 
        {
            if (pair1.second == pair2.second) 
            {  // 如果值相等
                result_map1[pair1.second].push_back(pair1.first);//记录两个表都有的
                result_map2[pair2.second].push_back(pair2.first);
                just_left=false;
            }
        }
        if(just_left)//LEFT JOIN
        {
            left_ind.push_back(pair1.first);//记录只有左表有的
        }
    }
    if(join_mode=="RIGHT")
    {
        for (const auto& pair2 : t2_join_value_pt) 
        {
            bool just_right=true;
            // 遍历map1，比较值是否相等
            for (const auto& pair1 : t1_join_value_pt) 
            {
                if (pair1.second == pair2.second) 
                {  // 如果值相等
                    just_right=false;
                    continue;
                }
            }
            if(just_right)//RIGHT JOIN
            {
                right_ind.push_back(pair2.first);
            }
        }
    }

    std::string join_select;
    size_t map_size = result_map1.size();
    size_t count = 0;
                    
    if(join_mode=="INNER" || join_mode=="UNKNOWN" || join_mode=="LEFT" || join_mode=="RIGHT")
    {
        if(map_size!=0)//inner and left or inner and right   //有两表共有的值
        {
            for (const auto& pair : result_map1) 
            {
                count++;
                join_select=join_select+ "select * from "+table1 + "_data, "+table2 + "_data where " + table1 + "_data.ind in(''";
                for(int i=0; i< pair.second.size();i++)//table1
                {
                    if(i != pair.second.size()-1)
                    {
                        join_select=join_select+ pair.second[i] + "'', ''" ;
                    }
                    else
                    {
                        join_select=join_select+ pair.second[i] + "'') AND " + table2 + "_data.ind in(''";
                    }
                }
                // table2
                std::vector<std::string> t2_values=result_map2[pair.first];
                for(int i=0; i<t2_values.size(); i++)
                {
                    if(i != t2_values.size()-1)
                    {
                        join_select=join_select+ t2_values[i] + "'', ''" ;
                    }
                    else
                    {
                        join_select=join_select+ t2_values[i] + "'')";
                    }
                }
                if (count < map_size)
                {
                    join_select=join_select+" UNION ";
                }
                else
                {
                    if(join_mode=="INNER" || join_mode=="UNKNOWN")
                    {
                        join_select=join_select+";";
                    }
                    if(join_mode=="LEFT")
                    {
                        join_select=join_select+" UNION select * ,NULL AS ind, NULL AS keyword, NULL AS values from " + table1 + "_data where ind in(''";
                        for(int i=0;i<left_ind.size();i++)
                        {
                            if(i!=left_ind.size()-1)
                            {
                                join_select=join_select+left_ind[i]+"'', ''";
                            }
                            else
                            {
                                join_select=join_select+left_ind[i]+"'');";
                            }
                        }
                        if(left_ind.size()==0)
                        {
                            join_select=join_select+"'');";
                        }
                    }
                    if(join_mode=="RIGHT")
                    {
                        join_select=join_select+" UNION select NULL AS ind, NULL AS keyword, NULL AS values, * from " + table2 + "_data where ind in(''";
                        for(int i=0;i<right_ind.size();i++)
                        {
                            if(i!=right_ind.size()-1)
                            {
                                join_select=join_select+right_ind[i]+"'', ''";
                            }
                            else
                            {
                                join_select=join_select+right_ind[i]+"'');";
                            }
                        }
                        if(right_ind.size()==0)
                        {
                            join_select=join_select+"'');";
                        }
                    }
                }
            }
        }
        else//just left or right   //无两表共有的值
        {
            if(join_mode=="LEFT")
            {
                join_select=join_select+"select * ,NULL AS ind, NULL AS keyword, NULL AS values from " + table1 + "_data where ind in(''";
                for(int i=0;i<left_ind.size();i++)
                {
                    if(i!=left_ind.size()-1)
                    {
                        join_select=join_select+left_ind[i]+"'', ''";
                    }
                    else
                    {
                        join_select=join_select+left_ind[i]+"'');";
                    }
                }
                if(left_ind.size()==0)
                {
                    join_select=join_select+"'');";
                }
            }
            if(join_mode=="RIGHT")
            {
                join_select=join_select+"select NULL AS ind, NULL AS keyword, NULL AS values, * from " + table2 + "_data where ind in(''";
                for(int i=0;i<right_ind.size();i++)
                {
                    if(i!=right_ind.size()-1)
                    {
                        join_select=join_select+right_ind[i]+"'', ''";
                    }
                    else
                    {
                        join_select=join_select+right_ind[i]+"'');";
                    }
                }
                if(right_ind.size()==0)
                {
                    join_select=join_select+"'');";
                }
            }
        }
    }

    if(join_mode=="CROSS")
    {
        join_select="select * from "+table1 + "_data, "+table2 + "_data where " + table1 + "_data.ind in(''";
        size_t map1_size = t1_join_value_pt.size();
        size_t count1 = 0;
        size_t map2_size = t2_join_value_pt.size();
        size_t count2 = 0;
        for (const auto& pair1 : t1_join_value_pt) 
        {
            count1++;
            if (count1 < map1_size)
            {
                join_select=join_select+pair1.first+"'', ''";
            }
            else
            {
                join_select=join_select+pair1.first+"'') and " +table2 + "_data.ind in(''";
            }
        }
        if(map1_size==0)
        {
            join_select=join_select + "'') and " + table2 + "_data.ind in(''";
        }

        for (const auto& pair2 : t2_join_value_pt) 
        {
            count2++;
            if (count2 < map2_size)
            {
                join_select=join_select+pair2.first+"'', ''";
            }
            else
            {
                join_select=join_select+pair2.first+"'');";
            }
        }
        if(map2_size==0)
        {
            join_select=join_select + "'');";
        }
    }


    if(join_select.empty())
    {
        join_select="select * from "+table1 + "_data, " + table2  + "_data LIMIT 0;";
    }

    return join_select;
}


