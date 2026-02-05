#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <vector>
#include "ring_buffer_utils.hpp"

/*
Single-producer single-consumer queue that stores variable length byte payloads.
Each message is laid out as: [size_t payload_size][payload bytes...].
The read/write counters grow monotonically; indices into the buffer are derived
with modulo arithmetic so wrap-around is handled transparently.
*/
template <size_t N>
struct SPSC {
    alignas(CACHE_LINE_SIZE) std::atomic_size_t read_idx{0};   // owned by consumer
    alignas(CACHE_LINE_SIZE) std::atomic_size_t write_idx{0};  // owned by producer
    std::array<std::byte, N> buffer{};

    // Returns true on success, false if there is not enough room.
    bool Push(std::span<const std::byte> data) {
        const size_t payload_size = data.size_bytes();
        const size_t total_size = HEADER_SIZE + payload_size;
        if (total_size > N) return false;  // message does not fit at all

        size_t write = write_idx.load(std::memory_order_relaxed);
        size_t read = read_idx.load(std::memory_order_acquire);

        size_t used = write - read;
        if (used + total_size > N) return false;  // not enough capacity

        size_t offset = write % N;
        CopyIn(buffer, offset, &payload_size, HEADER_SIZE);
        CopyIn(buffer, offset + HEADER_SIZE, data.data(), payload_size);

        write_idx.store(write + total_size, std::memory_order_release);
        return true;
    }

    // Returns an empty optional if there is no message available.
    std::optional<std::vector<std::byte>> Pop() {
        size_t read = read_idx.load(std::memory_order_relaxed);
        size_t write = write_idx.load(std::memory_order_acquire);
        if (read == write) return std::nullopt;

        size_t offset = read % N;
        size_t payload_size = 0;
        CopyOut(buffer, offset, &payload_size, HEADER_SIZE);

        const size_t total_size = HEADER_SIZE + payload_size;
        if (read + total_size > write) return std::nullopt;  // incomplete write

        std::vector<std::byte> payload(payload_size);
        if (payload_size > 0) {
            CopyOut(buffer, offset + HEADER_SIZE, payload.data(), payload_size);
        }

        read_idx.store(read + total_size, std::memory_order_release);
        return payload;
    }
};

int main() {
    SPSC<64> queue;

    std::array<std::byte, 5> message{
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c},
        std::byte{0x6c}, std::byte{0x6f},
    };

    if (!queue.Push(message)) {
        std::cerr << "Failed to enqueue message\n";
        return 1;
    }

    auto popped = queue.Pop();
    if (!popped) {
        std::cerr << "Queue unexpectedly empty\n";
        return 1;
    }

    std::cout << "Read " << popped->size() << " bytes: ";
    for (std::byte b : *popped) {
        std::cout << std::to_integer<int>(b) << ' ';
    }
    std::cout << '\n';
    return 0;
}
