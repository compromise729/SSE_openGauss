#ifndef FIDES_SQL_LITERAL_H
#define FIDES_SQL_LITERAL_H

#include <string>

// Read one standard SQL string literal, including doubled single quotes.
inline std::string::size_type read_fides_sql_literal(
    const std::string& sql, std::string::size_type opening, std::string& value)
{
    value.clear();
    if (opening >= sql.size() || sql[opening] != '\'') {
        return std::string::npos;
    }

    for (auto i = opening + 1; i < sql.size(); ++i) {
        if (sql[i] == '\'') {
            if (i + 1 == sql.size() || sql[i + 1] != '\'') {
                return i;
            }
            ++i;
        }
        value += sql[i];
    }

    value.clear();
    return std::string::npos;
}

#endif
