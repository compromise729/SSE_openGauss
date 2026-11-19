#include "libpq/row_state_catalog.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>

namespace {
std::string quote(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c < 32) {
            const char hex[] = "0123456789abcdef";
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += c;
        }
    }
    return out + '"';
}

int usage()
{
    std::cerr << "Usage: check-state --state-dir DIR --conninfo CONNECTION --table TABLE --keyword KEYWORD\n";
    return 2;
}

int print_error(const sse_v1::Status& status)
{
    std::cout << "{\"status\":" << quote(sse_v1::code_name(status.code))
              << ",\"reason\":" << quote(status.message) << "}\n";
    return 3;
}
} // namespace

int main(int argc, char** argv)
{
    std::map<std::string, std::string> args;
    if (argc != 9) return usage();
    for (int i = 1; i < argc; i += 2) {
        const std::string option(argv[i]);
        if ((option != "--state-dir" && option != "--conninfo" && option != "--table" &&
             option != "--keyword") || !args.emplace(option, argv[i + 1]).second) return usage();
    }
    using namespace sse_v1;
    if (args["--state-dir"].empty() || args["--state-dir"][0] != '/' || args["--conninfo"].empty()) return usage();
    if (setenv("SSE_V1_STATE_DIR", args["--state-dir"].c_str(), 1) != 0)
        return print_error({Code::state_invalid, "could not select state directory"});

    Scope scope;
    bool table_registered;
    auto status = local_scope(args["--table"], scope, table_registered);
    if (status.code == Code::invalid_input) return usage();
    if (!status.ok()) return print_error(status);
    const std::unique_ptr<PGconn, void(*)(PGconn*)> conn(PQconnectdb(args["--conninfo"].c_str()), PQfinish);
    status = set_catalog_reader([&](const std::string& candidate, Catalog& out) {
        return read_catalog(conn.get(), candidate, out);
    });
    if (!status.ok()) return print_error(status);
    const auto checked = preflight(scope, args["--keyword"]);
    if (checked.status.code == Code::invalid_input) return usage();
    if (!checked.status.ok()) return print_error(checked.status);
    std::cout << "{\"status\":\"ok\",\"counter\":{\"target\":" << quote(args["--keyword"])
              << ",\"status\":" << quote(checked.keyword_registered ? "found" : "unregistered");
    if (checked.keyword_registered) std::cout << ",\"count\":" << checked.counter;
    std::cout << "},\"mapping\":{\"target\":" << quote(args["--table"])
              << ",\"status\":" << quote(table_registered ? "found" : "unregistered") << "}}\n";
    return checked.keyword_registered && table_registered ? 0 : 1;
}
