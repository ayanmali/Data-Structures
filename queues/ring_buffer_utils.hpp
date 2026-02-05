#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

static constexpr size_t CACHE_LINE_SIZE = 64;
static constexpr size_t HEADER_SIZE = sizeof(size_t);

// Copy helpers that handle wrap-around for a fixed-size byte ring buffer.
template <size_t N>
inline void CopyIn(std::array<std::byte, N>& buffer, size_t offset, const void* src, size_t len) {
    const std::byte* src_bytes = static_cast<const std::byte*>(src);
    size_t start = offset % N;
    size_t first = std::min(len, N - start);
    std::memcpy(buffer.data() + start, src_bytes, first);
    if (len > first) {
        std::memcpy(buffer.data(), src_bytes + first, len - first);
    }
}

template <size_t N>
inline void CopyOut(const std::array<std::byte, N>& buffer, size_t offset, void* dst, size_t len) {
    std::byte* dst_bytes = static_cast<std::byte*>(dst);
    size_t start = offset % N;
    size_t first = std::min(len, N - start);
    std::memcpy(dst_bytes, buffer.data() + start, first);
    if (len > first) {
        std::memcpy(dst_bytes + first, buffer.data(), len - first);
    }
}
