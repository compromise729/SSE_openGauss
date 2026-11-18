#include "sse_v1_contract.hpp"
#include <type_traits>
static_assert(sse_v1::kMaxPlainRowBytes == 3 * sse_v1::kMaxFieldBytes + 12, "row size");
static_assert(std::is_same<decltype(&sse_v1::encrypt_locator),
    sse_v1::Status (*)(const sse_v1::Scope&, const sse_v1::Locator&, const sse_v1::KeySet&, std::string&)>::value, "locator API");
int main() {
    sse_v1::Status status{sse_v1::Code::ok, ""};
    sse_v1::Row row{"1", "apple", "red apple"};
    return status.ok() && row.ind == "1" ? 0 : 1;
}
