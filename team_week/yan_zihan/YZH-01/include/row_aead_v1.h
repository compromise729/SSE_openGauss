#ifndef ROW_AEAD_V1_H
#define ROW_AEAD_V1_H

#include "sse_v1_contract.hpp"

namespace sse_v1 {
constexpr std::size_t kRowBlobMin = 88;
constexpr std::size_t kRowBlobMax = 65624;  // SHY-02 wire cap
constexpr std::size_t kLocatorBlobSize = 96;
} // namespace sse_v1

#endif
