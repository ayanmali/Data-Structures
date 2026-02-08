#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <vector>
#include <cassert>
#include <thread>
#include <chrono>
#include <unordered_set>
#include <mutex>
#include "ring_buffer_utils.hpp"

/*
Single-producer multi-consumer queue that stores variable-size byte messages.
Each message is prefixed with a size header (HEADER_SIZE bytes).
The read/write counters grow monotonically; indices into the buffer are derived
with modulo arithmetic so wrap-around is handled transparently.

Capacity should be a power of 2
*/
template <size_t N>
struct SPMCUnicast {
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_idx{0};   // owned by consumer
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_idx{0};  // owned by producer
    uint8_t buffer[N];

    // Returns true on success, false if there is not enough room.
    bool Push(std::span<const std::byte> data) {
        const size_t payload_size = data.size_bytes();
        const size_t total_size = HEADER_SIZE + payload_size;

        const uint64_t write = write_idx.load(std::memory_order_relaxed);
        const uint64_t read = read_idx.load(std::memory_order_acquire);

        const uint64_t used = write - read;
        if (used + total_size > N) return false;  // not enough capacity (element count)

        const uint64_t offset = write & (N - 1);
        CopyIn(buffer, N, offset, &payload_size, HEADER_SIZE);
        CopyIn(buffer, N, offset + HEADER_SIZE, data.data(), payload_size);

        write_idx.store(write + total_size, std::memory_order_release);
        return true;
    }

    // Returns an empty vector if there is no message available.
    std::vector<std::byte> Pop() {
        while (true) {
            uint64_t read = read_idx.load(std::memory_order_relaxed);
            const uint64_t write = write_idx.load(std::memory_order_acquire);
            if (read == write) return std::vector<std::byte>{};

            // Speculatively read data BEFORE advancing read_idx.
            // This is safe because while read_idx == read, the producer
            // cannot have wrapped around to overwrite this slot (the
            // buffer would appear full from the producer's perspective).
            const uint64_t offset = read & (N - 1);
            size_t payload_size;
            CopyOut(buffer, N, offset, &payload_size, HEADER_SIZE);

            const size_t total_size = HEADER_SIZE + payload_size;
            if (read + total_size > write) return std::vector<std::byte>{}; // incomplete write

            std::vector<std::byte> payload(payload_size);
            if (payload_size > 0) {
                CopyOut(buffer, N, offset + HEADER_SIZE, payload.data(), payload_size);
            }

            if(read_idx.compare_exchange_weak(
                read, read + total_size, std::memory_order_release, std::memory_order_relaxed)) {
                    return payload;
                }
            // CAS failed — another consumer claimed this slot; retry.
        }
    }

};

// Helper functions for working with variable-size byte messages
std::vector<std::byte> MakeMessage(const std::string& str) {
    std::vector<std::byte> msg(str.size());
    std::memcpy(msg.data(), str.data(), str.size());
    return msg;
}

std::vector<std::byte> MakeMessage(const std::vector<uint8_t>& data) {
    std::vector<std::byte> msg(data.size());
    std::memcpy(msg.data(), data.data(), data.size());
    return msg;
}

std::string BytesToString(const std::vector<std::byte>& bytes) {
    std::string result(bytes.size(), '\0');
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

bool CompareBytes(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}


int main() {
    bool all_tests_passed = true;
    int test_count = 0;
    int pass_count = 0;

    // Test 1: Basic push and pop (single-threaded)
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Basic push and pop\n";
        SPMCUnicast<128> queue;

        auto msg = MakeMessage("Hello");
        
        if (!queue.Push(msg)) {
            std::cerr << "  ERROR: Failed to push message\n";
            all_tests_passed = false;
        } else {
            auto popped = queue.Pop();
            if (popped.empty()) {
                std::cerr << "  ERROR: Queue unexpectedly empty\n";
                all_tests_passed = false;
            } else if (!CompareBytes(popped, msg)) {
                std::cerr << "  ERROR: Popped message doesn't match pushed message\n";
                std::cerr << "  Expected: " << BytesToString(msg) << "\n";
                std::cerr << "  Got:      " << BytesToString(popped) << "\n";
                all_tests_passed = false;
            } else {
                std::cout << "  PASS\n";
                ++pass_count;
            }
        }
    }

    // Test 2: Multiple messages in sequence (variable sizes)
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multiple variable-size messages in sequence\n";
        SPMCUnicast<512> queue;

        std::vector<std::vector<std::byte>> messages = {
            MakeMessage("A"),
            MakeMessage("BB"),
            MakeMessage("CCC"),
            MakeMessage("DDDD"),
            MakeMessage("EEEEE")
        };

        // Push all messages
        for (const auto& msg : messages) {
            if (!queue.Push(msg)) {
                std::cerr << "  ERROR: Failed to push message of size " << msg.size() << "\n";
                all_tests_passed = false;
                goto test2_end;
            }
        }

        // Pop all messages and verify order and content
        for (size_t i = 0; i < messages.size(); ++i) {
            auto popped = queue.Pop();
            if (popped.empty()) {
                std::cerr << "  ERROR: Expected message " << i << " but got empty\n";
                all_tests_passed = false;
                goto test2_end;
            }
            if (!CompareBytes(popped, messages[i])) {
                std::cerr << "  ERROR: Message " << i << " doesn't match\n";
                std::cerr << "  Expected: " << BytesToString(messages[i]) << " (size " << messages[i].size() << ")\n";
                std::cerr << "  Got:      " << BytesToString(popped) << " (size " << popped.size() << ")\n";
                all_tests_passed = false;
                goto test2_end;
            }
        }
        
        // Verify queue is empty
        {
            auto extra = queue.Pop();
            if (!extra.empty()) {
                std::cerr << "  ERROR: Queue should be empty but returned a message\n";
                all_tests_passed = false;
                goto test2_end;
            }
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test2_end:;
    }

    // Test 3: Empty queue behavior
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Empty queue behavior\n";
        SPMCUnicast<128> queue;

        auto popped = queue.Pop();
        if (!popped.empty()) {
            std::cerr << "  ERROR: Pop on empty queue should return empty vector\n";
            all_tests_passed = false;
        } else {
            std::cout << "  PASS\n";
            ++pass_count;
        }
    }

    // Test 4: Capacity limit
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Capacity limit (byte capacity)\n";
        SPMCUnicast<64> queue;  // Small capacity in bytes

        // Fill the queue with messages (each message takes HEADER_SIZE + payload_size bytes)
        std::vector<std::vector<std::byte>> messages;
        size_t total_bytes = 0;
        
        // Push messages until we fill the queue
        for (int i = 0; i < 10; ++i) {
            auto msg = MakeMessage("MSG" + std::to_string(i));
            size_t msg_total_size = HEADER_SIZE + msg.size();
            if (total_bytes + msg_total_size <= 64) {
                if (!queue.Push(msg)) {
                    std::cerr << "  ERROR: Push should have succeeded but failed\n";
                    all_tests_passed = false;
                    goto test4_end;
                }
                messages.push_back(msg);
                total_bytes += msg_total_size;
            } else {
                // This push should fail
                if (queue.Push(msg)) {
                    std::cerr << "  ERROR: Push should have failed on full queue\n";
                    all_tests_passed = false;
                    goto test4_end;
                }
                break;
            }
        }

        std::cout << "  PASS (filled " << total_bytes << " bytes)\n";
        ++pass_count;
        test4_end:;
    }

    // Test 5: Wrap-around scenario
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Wrap-around scenario\n";
        SPMCUnicast<128> queue;  // Small capacity to force wrap-around

        // Push messages that will cause wrap-around via modulo indexing
        std::vector<std::vector<std::byte>> messages1 = {
            MakeMessage("First"),
            MakeMessage("Second"),
            MakeMessage("Third")
        };
        
        for (const auto& msg : messages1) {
            if (!queue.Push(msg)) {
                std::cerr << "  ERROR: Failed to push message during wrap-around test\n";
                all_tests_passed = false;
                goto test5_wrap_end;
            }
        }

        // Pop first message
        {
            auto popped1 = queue.Pop();
            if (popped1.empty() || !CompareBytes(popped1, messages1[0])) {
                std::cerr << "  ERROR: First message doesn't match\n";
                all_tests_passed = false;
                goto test5_wrap_end;
            }
        }

        // Push another message (should wrap around)
        {
            auto msg4 = MakeMessage("Fourth");
            if (!queue.Push(msg4)) {
                std::cerr << "  ERROR: Failed to push message after wrap-around\n";
                all_tests_passed = false;
                goto test5_wrap_end;
            }

            // Pop remaining messages in order
            std::vector<std::vector<std::byte>> expected_tail = {messages1[1], messages1[2], msg4};
            for (size_t i = 0; i < expected_tail.size(); ++i) {
                auto popped2 = queue.Pop();
                if (popped2.empty() || !CompareBytes(popped2, expected_tail[i])) {
                    std::cerr << "  ERROR: Message " << i << " doesn't match after wrap-around\n";
                    std::cerr << "  Expected: " << BytesToString(expected_tail[i]) << "\n";
                    std::cerr << "  Got:      " << BytesToString(popped2) << "\n";
                    all_tests_passed = false;
                    goto test5_wrap_end;
                }
            }
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test5_wrap_end:;
    }

    // Test 6: Interleaved push and pop
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Interleaved push and pop\n";
        SPMCUnicast<256> queue;

        auto msg1 = MakeMessage("Message1");
        auto msg2 = MakeMessage("Message2");
        auto msg3 = MakeMessage("Message3");

        // Push, pop, push, pop pattern
        if (!queue.Push(msg1)) {
            std::cerr << "  ERROR: Failed to push msg1\n";
            all_tests_passed = false;
            goto test6_end;
        }

        {
            auto popped = queue.Pop();
            if (popped.empty() || !CompareBytes(popped, msg1)) {
                std::cerr << "  ERROR: msg1 doesn't match\n";
                all_tests_passed = false;
                goto test6_end;
            }
        }

        if (!queue.Push(msg2)) {
            std::cerr << "  ERROR: Failed to push msg2\n";
            all_tests_passed = false;
            goto test6_end;
        }

        if (!queue.Push(msg3)) {
            std::cerr << "  ERROR: Failed to push msg3\n";
            all_tests_passed = false;
            goto test6_end;
        }

        {
            auto popped2 = queue.Pop();
            if (popped2.empty() || !CompareBytes(popped2, msg2)) {
                std::cerr << "  ERROR: msg2 doesn't match\n";
                all_tests_passed = false;
                goto test6_end;
            }
        }

        {
            auto popped3 = queue.Pop();
            if (popped3.empty() || !CompareBytes(popped3, msg3)) {
                std::cerr << "  ERROR: msg3 doesn't match\n";
                all_tests_passed = false;
                goto test6_end;
            }
        }

        std::cout << "  PASS\n";
        ++pass_count;
        test6_end:;
    }

    // Test 7: Multi-threaded SPMC - Unicast with 2 consumers
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multi-threaded SPMC (2 consumers)\n";
        SPMCUnicast<4096> queue;
        
        const int NUM_MESSAGES = 1000;
        const int NUM_CONSUMERS = 2;
        
        std::atomic<bool> producer_done{false};
        std::vector<std::vector<std::string>> consumed_by_consumer(NUM_CONSUMERS);
        std::mutex result_mutex;
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < NUM_MESSAGES; ++i) {
                auto msg = MakeMessage("msg:" + std::to_string(i));
                while (!queue.Push(msg)) {
                    std::this_thread::yield();  // Wait for space
                }
            }
            producer_done.store(true, std::memory_order_release);
        });
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
            consumers.emplace_back([&, consumer_id]() {
                std::vector<std::string> local_consumed;
                int consecutive_empty = 0;
                while (true) {
                    auto msg = queue.Pop();
                    if (!msg.empty()) {
                        local_consumed.push_back(BytesToString(msg));
                        consecutive_empty = 0;
                    } else {
                        // Only exit if producer is done AND we've seen empty queue multiple times
                        if (producer_done.load(std::memory_order_acquire)) {
                            if (++consecutive_empty >= 100) {
                                break;
                            }
                        }
                        std::this_thread::yield();
                    }
                }
                
                std::lock_guard<std::mutex> lock(result_mutex);
                consumed_by_consumer[consumer_id] = std::move(local_consumed);
            });
        }
        
        // Wait for all threads
        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }
        
        // Verify: Each message consumed exactly once
        std::unordered_set<std::string> all_consumed;
        int total_consumed = 0;
        for (int i = 0; i < NUM_CONSUMERS; ++i) {
            total_consumed += consumed_by_consumer[i].size();
            for (const auto& msg : consumed_by_consumer[i]) {
                if (all_consumed.count(msg) > 0) {
                    std::cerr << "  ERROR: Message " << msg << " consumed by multiple consumers!\n";
                    all_tests_passed = false;
                    goto test7_end;
                }
                all_consumed.insert(msg);
            }
        }
        
        if (total_consumed != NUM_MESSAGES) {
            std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
            all_tests_passed = false;
            goto test7_end;
        }
        
        if (all_consumed.size() != NUM_MESSAGES) {
            std::cerr << "  ERROR: Some messages were lost or duplicated\n";
            all_tests_passed = false;
            goto test7_end;
        }
        
        std::cout << "  PASS (consumed " << total_consumed << " messages uniquely)\n";
        ++pass_count;
        test7_end:;
    }
    
    // Test 8: Multi-threaded SPMC - Unicast with 4 consumers
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multi-threaded SPMC (4 consumers)\n";
        SPMCUnicast<8192> queue;
        
        const int NUM_MESSAGES = 2000;
        const int NUM_CONSUMERS = 4;
        
        std::atomic<bool> producer_done{false};
        std::vector<std::vector<std::string>> consumed_by_consumer(NUM_CONSUMERS);
        std::mutex result_mutex;
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < NUM_MESSAGES; ++i) {
                auto msg = MakeMessage("message_" + std::to_string(i));
                while (!queue.Push(msg)) {
                    std::this_thread::yield();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
            consumers.emplace_back([&, consumer_id]() {
                std::vector<std::string> local_consumed;
                int consecutive_empty = 0;
                while (true) {
                    auto msg = queue.Pop();
                    if (!msg.empty()) {
                        local_consumed.push_back(BytesToString(msg));
                        consecutive_empty = 0;
                    } else {
                        // Only exit if producer is done AND we've seen empty queue multiple times
                        if (producer_done.load(std::memory_order_acquire)) {
                            if (++consecutive_empty >= 100) {
                                break;
                            }
                        }
                        std::this_thread::yield();
                    }
                }
                
                std::lock_guard<std::mutex> lock(result_mutex);
                consumed_by_consumer[consumer_id] = std::move(local_consumed);
            });
        }
        
        // Wait for all threads
        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }
        
        // Verify: Each message consumed exactly once
        std::unordered_set<std::string> all_consumed;
        int total_consumed = 0;
        for (int i = 0; i < NUM_CONSUMERS; ++i) {
            total_consumed += consumed_by_consumer[i].size();
            for (const auto& msg : consumed_by_consumer[i]) {
                if (all_consumed.count(msg) > 0) {
                    std::cerr << "  ERROR: Message " << msg << " consumed by multiple consumers!\n";
                    all_tests_passed = false;
                    goto test8_end;
                }
                all_consumed.insert(msg);
            }
        }
        
        if (total_consumed != NUM_MESSAGES) {
            std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
            all_tests_passed = false;
            goto test8_end;
        }
        
        // Print distribution
        std::cout << "  Distribution: ";
        for (int i = 0; i < NUM_CONSUMERS; ++i) {
            std::cout << "C" << i << "=" << consumed_by_consumer[i].size() << " ";
        }
        std::cout << "\n";
        
        std::cout << "  PASS (consumed " << total_consumed << " messages uniquely)\n";
        ++pass_count;
        test8_end:;
    }
    
    // Test 9: Multi-threaded SPMC - Variable-size messages
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multi-threaded SPMC with variable-size messages\n";
        SPMCUnicast<8192> queue;
        
        const int NUM_MESSAGES = 500;
        const int NUM_CONSUMERS = 3;
        
        std::atomic<bool> producer_done{false};
        std::vector<std::vector<std::string>> consumed_by_consumer(NUM_CONSUMERS);
        std::mutex result_mutex;
        
        // Producer thread - pushes messages of varying sizes
        std::thread producer([&]() {
            for (int i = 0; i < NUM_MESSAGES; ++i) {
                // Create messages of varying sizes
                std::string content = "msg" + std::to_string(i) + ":";
                // Add variable padding to create different message sizes
                for (int j = 0; j < (i % 20); ++j) {
                    content += "X";
                }
                auto msg = MakeMessage(content);
                while (!queue.Push(msg)) {
                    std::this_thread::yield();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
            consumers.emplace_back([&, consumer_id]() {
                std::vector<std::string> local_consumed;
                int consecutive_empty = 0;
                while (true) {
                    auto msg = queue.Pop();
                    if (!msg.empty()) {
                        local_consumed.push_back(BytesToString(msg));
                        consecutive_empty = 0;
                    } else {
                        // Only exit if producer is done AND we've seen empty queue multiple times
                        if (producer_done.load(std::memory_order_acquire)) {
                            if (++consecutive_empty >= 100) {
                                break;
                            }
                        }
                        std::this_thread::yield();
                    }
                }
                
                std::lock_guard<std::mutex> lock(result_mutex);
                consumed_by_consumer[consumer_id] = std::move(local_consumed);
            });
        }
        
        // Wait for all threads
        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }
        
        // Verify: Each message consumed exactly once
        std::unordered_set<std::string> all_consumed;
        int total_consumed = 0;
        size_t total_bytes = 0;
        for (int i = 0; i < NUM_CONSUMERS; ++i) {
            total_consumed += consumed_by_consumer[i].size();
            for (const auto& msg : consumed_by_consumer[i]) {
                total_bytes += msg.size();
                if (all_consumed.count(msg) > 0) {
                    std::cerr << "  ERROR: Message " << msg << " consumed by multiple consumers!\n";
                    all_tests_passed = false;
                    goto test9_end;
                }
                all_consumed.insert(msg);
            }
        }
        
        if (total_consumed != NUM_MESSAGES) {
            std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
            all_tests_passed = false;
            goto test9_end;
        }
        
        std::cout << "  PASS (consumed " << total_consumed << " messages, " << total_bytes << " total bytes)\n";
        ++pass_count;
        test9_end:;
    }
    
    // Test 10: Multi-threaded SPMC - High contention (many consumers)
    {
        ++test_count;
        std::cout << "Test " << test_count << ": Multi-threaded SPMC (8 consumers, high contention)\n";
        SPMCUnicast<16384> queue;
        
        const int NUM_MESSAGES = 5000;
        const int NUM_CONSUMERS = 8;
        
        std::atomic<bool> producer_done{false};
        std::vector<std::vector<std::string>> consumed_by_consumer(NUM_CONSUMERS);
        std::mutex result_mutex;
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < NUM_MESSAGES; ++i) {
                auto msg = MakeMessage("DATA[" + std::to_string(i) + "]");
                while (!queue.Push(msg)) {
                    std::this_thread::yield();
                }
            }
            producer_done.store(true, std::memory_order_release);
        });
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
            consumers.emplace_back([&, consumer_id]() {
                std::vector<std::string> local_consumed;
                int consecutive_empty = 0;
                while (true) {
                    auto msg = queue.Pop();
                    if (!msg.empty()) {
                        local_consumed.push_back(BytesToString(msg));
                        consecutive_empty = 0;
                    } else {
                        // Only exit if producer is done AND we've seen empty queue multiple times
                        if (producer_done.load(std::memory_order_acquire)) {
                            if (++consecutive_empty >= 100) {
                                break;
                            }
                        }
                        std::this_thread::yield();
                    }
                }
                
                std::lock_guard<std::mutex> lock(result_mutex);
                consumed_by_consumer[consumer_id] = std::move(local_consumed);
            });
        }
        
        // Wait for all threads
        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }
        
        // Verify: Each message consumed exactly once
        std::unordered_set<std::string> all_consumed;
        int total_consumed = 0;
        for (int i = 0; i < NUM_CONSUMERS; ++i) {
            total_consumed += consumed_by_consumer[i].size();
            for (const auto& msg : consumed_by_consumer[i]) {
                if (all_consumed.count(msg) > 0) {
                    std::cerr << "  ERROR: Message " << msg << " consumed by multiple consumers!\n";
                    all_tests_passed = false;
                    goto test10_end;
                }
                all_consumed.insert(msg);
            }
        }
        
        if (total_consumed != NUM_MESSAGES) {
            std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
            all_tests_passed = false;
            goto test10_end;
        }
        
        std::cout << "  PASS (consumed " << total_consumed << " messages uniquely across " << NUM_CONSUMERS << " consumers)\n";
        ++pass_count;
        test10_end:;
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