#ifndef SSE_V1_SEARCH_LIMITS_H
#define SSE_V1_SEARCH_LIMITS_H

namespace sse_v1 {

// One server derivation per update counter value, including consumed updates.
constexpr unsigned kMaxSearchAddCount = 4096;

} // namespace sse_v1

#endif
