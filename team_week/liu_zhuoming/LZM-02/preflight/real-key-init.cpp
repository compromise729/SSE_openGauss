#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

#include "libpq/row_state_v1.h"

namespace {
using namespace sse_v1;

void require(bool condition, const char* label)
{
    if (!condition) throw std::runtime_error(label);
}

void expect(const Status& actual, Code code, const char* label)
{
    if (actual.code != code) {
        throw std::runtime_error(std::string(label) + ": " + code_name(actual.code) + " (" + actual.message + ")");
    }
}

void use_state(const std::string& state)
{
    require(mkdir(state.c_str(), 0700) == 0, "create private test state directory");
    require(setenv("SSE_V1_STATE_DIR", state.c_str(), 1) == 0, "select test state directory");
}

bool exists(const std::string& path)
{
    struct stat info;
    return lstat(path.c_str(), &info) == 0;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace sse_v1;
    if (argc != 2) return 2;
    try {
        const std::string root = argv[1];
        expect(set_catalog_reader([](const std::string&, Catalog& catalog) {
            catalog.database_oid = "14177";
            catalog.instance_directory = "/fixture/instance";
            return Status{Code::ok, {}};
        }), Code::ok, "register empty catalog fixture");

        const auto good = root + "/good";
        use_state(good);
        expect(initialize_state("14177", initialize_keys), Code::ok, "fresh state uses the real YZH initializer");
        expect(validate_startup(), Code::ok, "complete environment validates");
        KeySet first, reopened;
        expect(load_keys(good, first), Code::ok, "load keys from the published state directory");
        require(first.row_key.size() == 32 && first.locator_key.size() == 32 &&
                first.key_id.size() == 16 && first.row_key != first.locator_key, "published key lengths and separation");
        expect(initialize_state("14177", initialize_keys), Code::state_invalid, "second initialization refuses existing state");
        expect(load_keys(good, reopened), Code::ok, "reload state keys");
        require(first.key_id == reopened.key_id && first.row_key == reopened.row_key &&
                first.locator_key == reopened.locator_key, "reinitialization never changes existing keys");
        std::cout << "PASS real keys initialize, validate and reopen without replacement\n";

        const auto partial = root + "/partial";
        use_state(partial);
        expect(initialize_state("14177", [](const std::string& directory, KeySet& keys) {
            auto status = initialize_keys(directory, keys);
            return status.ok() ? Status{Code::crypto_failed, "fixture interrupted before key promotion"} : status;
        }), Code::crypto_failed, "preserve an initializer error");
        require(exists(partial + "/deployment.v1") && !exists(partial + "/environment.v1") &&
                !exists(partial + "/row_aead_v1.key"), "partial bootstrap did not publish usable state or keys");
        expect(validate_local_startup(), Code::state_invalid, "partial state blocks startup");
        expect(initialize_state("14177", initialize_keys), Code::state_invalid, "partial state blocks another initialization");
        std::cout << "PASS partial key initialization blocks restart and retry\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
