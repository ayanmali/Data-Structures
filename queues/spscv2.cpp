#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include "ring_buffer_utils.hpp"

/*
Single-producer single-consumer queue that stores variable length byte payloads.
Each message is laid out as: [size_t payload_size][payload bytes...].
The read/write counters grow monotonically; indices into the buffer are derived
with modulo arithmetic so wrap-around is handled transparently.
*/
template <typename T = std::byte, size_t N>
struct SPSC {
    alignas(CACHE_LINE_SIZE) std::atomic_size_t read_idx{0};   // owned by consumer
    alignas(CACHE_LINE_SIZE) std::atomic_size_t write_idx{0};  // owned by producer
    std::array<T, N> buffer{};
    // std::byte buffer[N]{};

    // Returns true on success, false if there is not enough room.
    bool PushOne(const T& data) {
        const size_t payload_size = sizeof(T);
        // const size_t total_size = HEADER_SIZE + payload_size;

        size_t write = write_idx.load(std::memory_order_relaxed);
        size_t read = read_idx.load(std::memory_order_acquire);

        size_t used = write - read;
        if (used + sizeof(T) > N) return false;  // not enough capacity

        size_t offset = write % N;
        CopyIn(buffer, offset, &data, sizeof(T));

        write_idx.store(write + payload_size, std::memory_order_release);
        return true;
    }

    bool PushMany(std::span<const T> data) {
        const size_t payload_size = data.size() * sizeof(T);
        if (payload_size > N) return false;  // message does not fit at all

        size_t write = write_idx.load(std::memory_order_relaxed);
        size_t read = read_idx.load(std::memory_order_acquire);

        size_t used = write - read;
        if (used + payload_size > N) return false;  // not enough capacity
    }

    // Returns an empty optional if there is no message available.
    std::optional<T> PopOne() {
        size_t read = read_idx.load(std::memory_order_relaxed);
        size_t write = write_idx.load(std::memory_order_acquire);
        if (read == write) return std::nullopt;

        size_t offset = read % N;
        // size_t payload_size = 0;
        //CopyOut(buffer, offset, &sizeof(T), sizeof(T));

        if (read + sizeof(T) > write) return std::nullopt;  // incomplete write

        //std::vector<T> payload(payload_size);
        // if (sizeof(T) > 0) {
        //     CopyOut(buffer, offset, payload.data(), payload_size);
        // }
        T payload = buffer[offset];

        read_idx.store(read + sizeof(T), std::memory_order_release);
        return payload;
    }

    std::optional<std::span<T>> PopMany() {
        size_t read = read_idx.load(std::memory_order_relaxed);
        size_t write = write_idx.load(std::memory_order_acquire);
        if (read == write) return std::nullopt;

        size_t offset = read % N;
        size_t payload_size = ;
        //CopyOut(buffer, offset, &payload_size, sizeof(T));

        if (read + payload_size > write) return std::nullopt;  // incomplete write

        std::span<T> payload(buffer.data() + offset, payload_size / sizeof(T));
        read_idx.store(read + payload_size, std::memory_order_release);
        return payload;
    }
};


// Helper function to compare two byte spans
bool CompareBytes(std::span<const std::byte> a, std::span<const std::byte> b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// Helper function to print byte span for debugging
void PrintBytes(std::span<const std::byte> data, const char* label) {
    std::cout << label << ": [";
    for (size_t i = 0; i < data.size(); ++i) {
        std::cout << std::to_integer<int>(data[i]);
        if (i < data.size() - 1) std::cout << ", ";
    }
    std::cout << "]\n";
}

int main() {
    bool all_tests_passed = true;
    int test_count = 0;
    int pass_count = 0;

    // Test 1: Basic push and pop
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Basic push and pop\n";
        SPSC<int, 64> queue;
        
        std::array<int, 5> message{
            48, 65, 6c, 6c, 6f
        };

        if (!queue.Push(message[0])) {
            std::cerr << "  ERROR: Failed to push message\n";
            all_tests_passed = false;
        } else {
            auto popped = queue.Pop();
            if (!popped) {
                std::cerr << "  ERROR: Queue unexpectedly empty\n";
                all_tests_passed = false;
            } else if (!CompareBytes(*popped, message)) {
                std::cerr << "  ERROR: Popped message doesn't match pushed message\n";
                PrintBytes(message, "  Expected");
                PrintBytes(*popped, "  Got");
                all_tests_passed = false;
            } else {
                std::cout << "  PASS\n";
                ++pass_count;
            }
        }
    }

    // Test 2: Multiple messages in sequence
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multiple messages in sequence\n";
        SPSC<128> queue;
        
        std::vector<std::vector<std::byte>> messages = {
            {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
            {std::byte{0x0A}, std::byte{0x0B}},
            {std::byte{0xFF}, std::byte{0xFE}, std::byte{0xFD}, std::byte{0xFC}},
        };

        // Push all messages
        for (const auto& msg : messages) {
            if (!queue.Push(msg)) {
                std::cerr << "  ERROR: Failed to push message\n";
                all_tests_passed = false;
                goto test2_end;
            }
        }

        // Pop all messages and verify order
        for (size_t i = 0; i < messages.size(); ++i) {
            auto popped = queue.Pop();
            if (!popped) {
                std::cerr << "  ERROR: Expected message " << i << " but queue was empty\n";
                all_tests_passed = false;
                goto test2_end;
            }
            if (!CompareBytes(*popped, messages[i])) {
                std::cerr << "  ERROR: Message " << i << " doesn't match\n";
                PrintBytes(messages[i], "  Expected");
                PrintBytes(*popped, "  Got");
                all_tests_passed = false;
                goto test2_end;
            }
        }

        // Verify queue is empty
        auto extra = queue.Pop();
        if (extra) {
            std::cerr << "  ERROR: Queue should be empty but returned a message\n";
            all_tests_passed = false;
            goto test2_end;
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test2_end:;
    }

    // Test 3: Empty queue behavior
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Empty queue behavior\n";
        SPSC<64> queue;
        
        auto popped = queue.Pop();
        if (popped) {
            std::cerr << "  ERROR: Pop on empty queue should return nullopt\n";
            all_tests_passed = false;
        } else {
            std::cout << "  PASS\n";
            ++pass_count;
        }
    }

    // Test 4: Different message sizes
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Different message sizes\n";
        SPSC<256> queue;
        
        std::vector<std::pair<size_t, std::vector<std::byte>>> test_cases = {
            {1, {std::byte{0x42}}},
            {2, {std::byte{0x11}, std::byte{0x22}}},
            {10, {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                   std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
                   std::byte{0x09}, std::byte{0x0A}}},
        };

        for (const auto& [size, msg] : test_cases) {
            if (!queue.Push(msg)) {
                std::cerr << "  ERROR: Failed to push message of size " << size << "\n";
                all_tests_passed = false;
                goto test4_end;
            }
            auto popped = queue.Pop();
            if (!popped || !CompareBytes(*popped, msg)) {
                std::cerr << "  ERROR: Message of size " << size << " doesn't match\n";
                all_tests_passed = false;
                goto test4_end;
            }
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test4_end:;
    }

    // Test 5: Full queue rejection
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Full queue rejection\n";
        SPSC<16> queue;  // Small buffer
        
        std::vector<std::byte> msg1(8);
        std::vector<std::byte> msg2(8);
        std::vector<std::byte> msg3(1);

        if (!queue.Push(msg1)) {
            std::cerr << "  ERROR: Failed to push first message\n";
            all_tests_passed = false;
            goto test5_end;
        }
        if (!queue.Push(msg2)) {
            std::cerr << "  ERROR: Failed to push second message\n";
            all_tests_passed = false;
            goto test5_end;
        }
        // Third message should fail (8 + 8 = 16, which is the buffer size)
        if (queue.Push(msg3)) {
            std::cerr << "  ERROR: Push should have failed on full queue\n";
            all_tests_passed = false;
            goto test5_end;
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test5_end:;
    }

    // Test 6: Wrap-around scenario
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Wrap-around scenario\n";
        SPSC<32> queue;  // Small buffer to force wrap-around
        
        // Push messages that will cause wrap-around
        std::vector<std::vector<std::byte>> messages = {
            {std::byte{0xAA}, std::byte{0xBB}},  // 2 bytes
            {std::byte{0xCC}, std::byte{0xDD}},  // 2 bytes
            {std::byte{0xEE}},                    // 1 byte
        };

        for (const auto& msg : messages) {
            if (!queue.Push(msg)) {
                std::cerr << "  ERROR: Failed to push message during wrap-around test\n";
                all_tests_passed = false;
                goto test6_end;
            }
        }

        // Pop first message
        auto popped1 = queue.Pop();
        if (!popped1 || !CompareBytes(*popped1, messages[0])) {
            std::cerr << "  ERROR: First message doesn't match after wrap-around\n";
            all_tests_passed = false;
            goto test6_end;
        }

        // Push another message (should wrap around)
        std::vector<std::byte> msg4{std::byte{0xFF}, std::byte{0x00}};
        if (!queue.Push(msg4)) {
            std::cerr << "  ERROR: Failed to push message after wrap-around\n";
            all_tests_passed = false;
            goto test6_end;
        }

        // Pop remaining messages in order
        for (size_t i = 1; i < messages.size(); ++i) {
            auto popped = queue.Pop();
            if (!popped || !CompareBytes(*popped, messages[i])) {
                std::cerr << "  ERROR: Message " << i << " doesn't match after wrap-around\n";
                all_tests_passed = false;
                goto test6_end;
            }
        }

        // Pop the last pushed message
        auto popped4 = queue.Pop();
        if (!popped4 || !CompareBytes(*popped4, msg4)) {
            std::cerr << "  ERROR: Last message doesn't match after wrap-around\n";
            all_tests_passed = false;
            goto test6_end;
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test6_end:;
    }

    // Test 7: Interleaved push and pop
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Interleaved push and pop\n";
        SPSC<128> queue;
        
        std::vector<std::byte> msg1{std::byte{0x10}, std::byte{0x20}};
        std::vector<std::byte> msg2{std::byte{0x30}, std::byte{0x40}};
        std::vector<std::byte> msg3{std::byte{0x50}, std::byte{0x60}};

        // Push, pop, push, pop pattern
        if (!queue.Push(msg1)) {
            std::cerr << "  ERROR: Failed to push msg1\n";
            all_tests_passed = false;
            goto test7_end;
        }

        auto popped1 = queue.Pop();
        if (!popped1 || !CompareBytes(*popped1, msg1)) {
            std::cerr << "  ERROR: msg1 doesn't match\n";
            all_tests_passed = false;
            goto test7_end;
        }

        if (!queue.Push(msg2)) {
            std::cerr << "  ERROR: Failed to push msg2\n";
            all_tests_passed = false;
            goto test7_end;
        }

        if (!queue.Push(msg3)) {
            std::cerr << "  ERROR: Failed to push msg3\n";
            all_tests_passed = false;
            goto test7_end;
        }

        auto popped2 = queue.Pop();
        if (!popped2 || !CompareBytes(*popped2, msg2)) {
            std::cerr << "  ERROR: msg2 doesn't match\n";
            all_tests_passed = false;
            goto test7_end;
        }

        auto popped3 = queue.Pop();
        if (!popped3 || !CompareBytes(*popped3, msg3)) {
            std::cerr << "  ERROR: msg3 doesn't match\n";
            all_tests_passed = false;
            goto test7_end;
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test7_end:;
    }

    // Summary
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "Test Summary: " << pass_count << "/" << test_count << " tests passed\n";
    std::cout << "========================================\n";

    if (all_tests_passed) {
        std::cout << "SUCCESS: All tests passed!\n";
        return 0;
    } else {
        std::cerr << "FAILURE: Some tests failed. See errors above.\n";
        return 1;
    }
}
