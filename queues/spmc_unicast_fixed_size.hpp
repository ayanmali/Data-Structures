#pragma once

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

#include <unordered_set>
#include <mutex>
#include "ring_buffer_utils.hpp"

/*
Single-producer multi-consumer queue that stores fixed size payloads.
The read/write counters grow monotonically; indices into the buffer are derived
with modulo arithmetic so wrap-around is handled transparently.

Capacity should be a power of 2
*/
template <typename T, size_t N>
struct SPMCUnicast {
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_idx{0};   // owned by consumer
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_idx{0};  // owned by producer
    T buffer[N];

    // Returns true on success, false if there is not enough room.
    bool PushOne(const T& data) {
        const uint64_t write = write_idx.load(std::memory_order_relaxed);
        const uint64_t read = read_idx.load(std::memory_order_acquire);

        const uint64_t used = write - read;
        if (used + 1 > N) return false;  // not enough capacity (element count)

        const uint64_t offset = write & (N - 1);
        std::memcpy(&buffer[offset], &data, sizeof(T));

        write_idx.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool PushMany(std::span<const T> data) {
        //if (data.size() > N) return false;  // message does not fit at all
        const uint64_t write = write_idx.load(std::memory_order_relaxed);
        const uint64_t read = read_idx.load(std::memory_order_acquire);

        const uint64_t used = write - read;
        if (used + data.size() > N) return false;  // not enough capacity
        
        // Copy elements one by one, handling wrap-around
        const uint64_t offset = write & (N - 1);
        CopyIn(buffer, N, offset, data.data(), data.size() * sizeof(T));

        write_idx.fetch_add(data.size(), std::memory_order_release);
        return true;
    }

    // Returns std::nullopt if there is no message available.
    bool Pop(T* payload) {
        while (true) {
            uint64_t read = read_idx.load(std::memory_order_relaxed);
            const uint64_t write = write_idx.load(std::memory_order_acquire);
            if (read == write) return false;

            // Speculatively read data BEFORE advancing read_idx.
            // This is safe because while read_idx == read, the producer
            // cannot have wrapped around to overwrite this slot (the
            // buffer would appear full from the producer's perspective).
            const uint64_t offset = read & (N - 1);
            std::memcpy(payload, &buffer[offset], sizeof(T));

            if(read_idx.compare_exchange_weak(
                read, read + 1, std::memory_order_release, std::memory_order_relaxed)) {
                    return true;
                }
            // CAS failed — another consumer claimed this slot; retry.
        }
    }

};

// Helper function to compare two sequences of values
template <typename T>
bool CompareSequence(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// // TODO: refactor Pop() calls in tests
// int test() {
//     bool all_tests_passed = true;
//     int test_count = 0;
//     int pass_count = 0;

//     // Test 1: Basic push and pop (single-threaded)
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Basic push and pop\n";
//         SPMCUnicast<int, 64> queue;

//         int value = 42;

//         if (!queue.PushOne(value)) {
//             std::cerr << "  ERROR: Failed to push value\n";
//             all_tests_passed = false;
//         } else {
//             auto popped = queue.Pop();
//             if (!popped.has_value()) {
//                 std::cerr << "  ERROR: Queue unexpectedly empty\n";
//                 all_tests_passed = false;
//             } else if (popped.value() != value) {
//                 std::cerr << "  ERROR: Popped value doesn't match pushed value\n";
//                 std::cerr << "  Expected: " << value << "\n";
//                 std::cerr << "  Got:      " << popped.value() << "\n";
//                 all_tests_passed = false;
//             } else {
//                 std::cout << "  PASS\n";
//                 ++pass_count;
//             }
//         }
//     }

//     // Test 2: Multiple messages in sequence
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Multiple values in sequence\n";
//         SPMCUnicast<int, 128> queue;

//         std::vector<int> values = {1, 2, 3, 4, 5};
//         const size_t n = values.size();

//         // Push all values
//         if(!queue.PushMany(values)) {
//             std::cerr << "  ERROR: Failed to push value\n";
//             all_tests_passed = false;
//             goto test2_end;
//         }

//         // Pop all values and verify order
//         for (const auto x : values) {
//             auto popped = queue.Pop();
//             if (!popped.has_value() || popped.value() != x) {
//                 std::cerr << "  ERROR: Expected " << x << " but got " << (popped.has_value() ? std::to_string(popped.value()) : "empty") << "\n";
//                 all_tests_passed = false;
//                 goto test2_end;
//             }
//         }
        
//         // Verify queue is empty
//         {
//             auto extra = queue.Pop();
//             if (extra.has_value()) {
//                 std::cerr << "  ERROR: Queue should be empty but returned a message\n";
//                 all_tests_passed = false;
//                 goto test2_end;
//             }
//         }

//         std::cout << "  PASS\n";
//         ++pass_count;
//         test2_end:;
//     }

//     // Test 3: Empty queue behavior
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Empty queue behavior\n";
//         SPMCUnicast<int, 64> queue;

//         auto popped = queue.Pop();
//         if (popped.has_value()) {
//             std::cerr << "  ERROR: Pop on empty queue should return nullopt\n";
//             all_tests_passed = false;
//         } else {
//             std::cout << "  PASS\n";
//             ++pass_count;
//         }
//     }

//     // Test 4: Capacity limit
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Capacity limit (fixed size)\n";
//         SPMCUnicast<int, 4> queue;  // Small capacity in elements

//         // Fill the queue
//         assert(queue.PushOne(1));
//         assert(queue.PushOne(2));
//         assert(queue.PushOne(3));
//         assert(queue.PushOne(4));

//         // Next push should fail because we treat N as element capacity
//         if (queue.PushOne(5)) {
//             std::cerr << "  ERROR: Push should have failed on full queue\n";
//             all_tests_passed = false;
//         } else {
//             std::cout << "  PASS\n";
//             ++pass_count;
//         }
//     }

//     // Test 5: Wrap-around scenario
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Wrap-around scenario\n";
//         SPMCUnicast<int, 4> queue;  // Small capacity to force wrap-around

//         // Push values that will cause wrap-around via modulo indexing
//         std::vector<int> values1 = {10, 20, 30};
//         for (int v : values1) {
//             if (!queue.PushOne(v)) {
//                 std::cerr << "  ERROR: Failed to push value during wrap-around test\n";
//                 all_tests_passed = false;
//                 goto test5_wrap_end;
//             }
//         }

//         // Pop first value
//         {
//             auto popped1 = queue.Pop();
//             if (!popped1.has_value() || popped1.value() != values1[0]) {
//                 std::cerr << "  ERROR: First value doesn't match after wrap-around\n";
//                 all_tests_passed = false;
//                 goto test5_wrap_end;
//             }
//         }

//         // Push another value (should wrap around)
//         {
//             int v4 = 40;
//             if (!queue.PushOne(v4)) {
//                 std::cerr << "  ERROR: Failed to push value after wrap-around\n";
//                 all_tests_passed = false;
//                 goto test5_wrap_end;
//             }

//             // Pop remaining values in order
//             std::vector<int> expected_tail = {values1[1], values1[2], v4};
//             for (size_t i = 0; i < expected_tail.size(); ++i) {
//                 auto popped2 = queue.Pop();
//                 if (!popped2.has_value() || popped2.value() != expected_tail[i]) {
//                     std::cerr << "  ERROR: Value " << i << " doesn't match after wrap-around\n";
//                     all_tests_passed = false;
//                     goto test5_wrap_end;
//                 }
//             }
//         }

//         std::cout << "  PASS\n";
//         ++pass_count;
//         test5_wrap_end:;
//     }

//     // Test 6: Interleaved push and pop
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Interleaved push and pop\n";
//         SPMCUnicast<int, 8> queue;

//         int v1 = 10;
//         int v2 = 20;
//         int v3 = 30;

//         // Push, pop, push, pop pattern
//         if (!queue.PushOne(v1)) {
//             std::cerr << "  ERROR: Failed to push v1\n";
//             all_tests_passed = false;
//             goto test6_end;
//         }

//         {
//             auto popped = queue.Pop();
//             if (!popped.has_value() || popped.value() != v1) {
//                 std::cerr << "  ERROR: v1 doesn't match\n";
//                 all_tests_passed = false;
//                 goto test6_end;
//             }
//         }

//         if (!queue.PushOne(v2)) {
//             std::cerr << "  ERROR: Failed to push v2\n";
//             all_tests_passed = false;
//             goto test6_end;
//         }

//         if (!queue.PushOne(v3)) {
//             std::cerr << "  ERROR: Failed to push v3\n";
//             all_tests_passed = false;
//             goto test6_end;
//         }

//         {
//             auto popped2 = queue.Pop();
//             if (!popped2.has_value() || popped2.value() != v2) {
//                 std::cerr << "  ERROR: v2 doesn't match\n";
//                 all_tests_passed = false;
//                 goto test6_end;
//             }
//         }

//         {
//             auto popped3 = queue.Pop();
//             if (!popped3.has_value() || popped3.value() != v3) {
//                 std::cerr << "  ERROR: v3 doesn't match\n";
//                 all_tests_passed = false;
//                 goto test6_end;
//             }
//         }

//         std::cout << "  PASS\n";
//         ++pass_count;
//         test6_end:;
//     }

//     // Test 7: Multi-threaded SPMC - Unicast with 2 consumers
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Multi-threaded SPMC (2 consumers)\n";
//         SPMCUnicast<int, 1024> queue;
        
//         const int NUM_MESSAGES = 1000;
//         const int NUM_CONSUMERS = 2;
        
//         std::atomic<bool> producer_done{false};
//         std::vector<std::vector<int>> consumed_by_consumer(NUM_CONSUMERS);
//         std::mutex result_mutex;
        
//         // Producer thread
//         std::thread producer([&]() {
//             for (int i = 0; i < NUM_MESSAGES; ++i) {
//                 while (!queue.PushOne(i)) {
//                     std::this_thread::yield();  // Wait for space
//                 }
//             }
//             producer_done.store(true, std::memory_order_release);
//         });
        
//         // Consumer threads
//         std::vector<std::thread> consumers;
//         for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
//             consumers.emplace_back([&, consumer_id]() {
//                 std::vector<int> local_consumed;
//                 int consecutive_empty = 0;
//                 while (true) {
//                     auto value = queue.Pop();
//                     if (value.has_value()) {
//                         local_consumed.push_back(value.value());
//                         consecutive_empty = 0;
//                     } else {
//                         // Only exit if producer is done AND we've seen empty queue multiple times
//                         if (producer_done.load(std::memory_order_acquire)) {
//                             if (++consecutive_empty >= 100) {
//                                 break;
//                             }
//                         }
//                         std::this_thread::yield();
//                     }
//                 }
                
//                 std::lock_guard<std::mutex> lock(result_mutex);
//                 consumed_by_consumer[consumer_id] = std::move(local_consumed);
//             });
//         }
        
//         // Wait for all threads
//         producer.join();
//         for (auto& consumer : consumers) {
//             consumer.join();
//         }
        
//         // Verify: Each message consumed exactly once
//         std::unordered_set<int> all_consumed;
//         int total_consumed = 0;
//         for (int i = 0; i < NUM_CONSUMERS; ++i) {
//             total_consumed += consumed_by_consumer[i].size();
//             for (int val : consumed_by_consumer[i]) {
//                 if (all_consumed.count(val) > 0) {
//                     std::cerr << "  ERROR: Message " << val << " consumed by multiple consumers!\n";
//                     all_tests_passed = false;
//                     goto test7_end;
//                 }
//                 all_consumed.insert(val);
//             }
//         }
        
//         if (total_consumed != NUM_MESSAGES) {
//             std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
//             all_tests_passed = false;
//             goto test7_end;
//         }
        
//         if (all_consumed.size() != NUM_MESSAGES) {
//             std::cerr << "  ERROR: Some messages were lost or duplicated\n";
//             all_tests_passed = false;
//             goto test7_end;
//         }
        
//         std::cout << "  PASS (consumed " << total_consumed << " messages uniquely)\n";
//         ++pass_count;
//         test7_end:;
//     }
    
//     // Test 8: Multi-threaded SPMC - Unicast with 4 consumers
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Multi-threaded SPMC (4 consumers)\n";
//         SPMCUnicast<int, 1024> queue;
        
//         const int NUM_MESSAGES = 2000;
//         const int NUM_CONSUMERS = 4;
        
//         std::atomic<bool> producer_done{false};
//         std::vector<std::vector<int>> consumed_by_consumer(NUM_CONSUMERS);
//         std::mutex result_mutex;
        
//         // Producer thread
//         std::thread producer([&]() {
//             for (int i = 0; i < NUM_MESSAGES; ++i) {
//                 while (!queue.PushOne(i)) {
//                     std::this_thread::yield();
//                 }
//             }
//             producer_done.store(true, std::memory_order_release);
//         });
        
//         // Consumer threads
//         std::vector<std::thread> consumers;
//         for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
//             consumers.emplace_back([&, consumer_id]() {
//                 std::vector<int> local_consumed;
//                 int consecutive_empty = 0;
//                 while (true) {
//                     auto value = queue.Pop();
//                     if (value.has_value()) {
//                         local_consumed.push_back(value.value());
//                         consecutive_empty = 0;
//                     } else {
//                         // Only exit if producer is done AND we've seen empty queue multiple times
//                         if (producer_done.load(std::memory_order_acquire)) {
//                             if (++consecutive_empty >= 100) {
//                                 break;
//                             }
//                         }
//                         std::this_thread::yield();
//                     }
//                 }
                
//                 std::lock_guard<std::mutex> lock(result_mutex);
//                 consumed_by_consumer[consumer_id] = std::move(local_consumed);
//             });
//         }
        
//         // Wait for all threads
//         producer.join();
//         for (auto& consumer : consumers) {
//             consumer.join();
//         }
        
//         // Verify: Each message consumed exactly once
//         std::unordered_set<int> all_consumed;
//         int total_consumed = 0;
//         for (int i = 0; i < NUM_CONSUMERS; ++i) {
//             total_consumed += consumed_by_consumer[i].size();
//             for (int val : consumed_by_consumer[i]) {
//                 if (all_consumed.count(val) > 0) {
//                     std::cerr << "  ERROR: Message " << val << " consumed by multiple consumers!\n";
//                     all_tests_passed = false;
//                     goto test8_end;
//                 }
//                 all_consumed.insert(val);
//             }
//         }
        
//         if (total_consumed != NUM_MESSAGES) {
//             std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
//             all_tests_passed = false;
//             goto test8_end;
//         }
        
//         // Print distribution
//         std::cout << "  Distribution: ";
//         for (int i = 0; i < NUM_CONSUMERS; ++i) {
//             std::cout << "C" << i << "=" << consumed_by_consumer[i].size() << " ";
//         }
//         std::cout << "\n";
        
//         std::cout << "  PASS (consumed " << total_consumed << " messages uniquely)\n";
//         ++pass_count;
//         test8_end:;
//     }
    
//     // Test 9: Multi-threaded SPMC - Batch operations
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Multi-threaded SPMC with batch PushMany\n";
//         SPMCUnicast<int, 2048> queue;
        
//         const int NUM_BATCHES = 100;
//         const int BATCH_SIZE = 10;
//         const int NUM_MESSAGES = NUM_BATCHES * BATCH_SIZE;
//         const int NUM_CONSUMERS = 3;
        
//         std::atomic<bool> producer_done{false};
//         std::vector<std::vector<int>> consumed_by_consumer(NUM_CONSUMERS);
//         std::mutex result_mutex;
        
//         // Producer thread - pushes batches
//         std::thread producer([&]() {
//             for (int batch = 0; batch < NUM_BATCHES; ++batch) {
//                 std::vector<int> batch_data(BATCH_SIZE);
//                 for (int i = 0; i < BATCH_SIZE; ++i) {
//                     batch_data[i] = batch * BATCH_SIZE + i;
//                 }
//                 while (!queue.PushMany(batch_data)) {
//                     std::this_thread::yield();
//                 }
//             }
//             producer_done.store(true, std::memory_order_release);
//         });
        
//         // Consumer threads
//         std::vector<std::thread> consumers;
//         for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
//             consumers.emplace_back([&, consumer_id]() {
//                 std::vector<int> local_consumed;
//                 int consecutive_empty = 0;
//                 while (true) {
//                     auto value = queue.Pop();
//                     if (value.has_value()) {
//                         local_consumed.push_back(value.value());
//                         consecutive_empty = 0;
//                     } else {
//                         // Only exit if producer is done AND we've seen empty queue multiple times
//                         if (producer_done.load(std::memory_order_acquire)) {
//                             if (++consecutive_empty >= 100) {
//                                 break;
//                             }
//                         }
//                         std::this_thread::yield();
//                     }
//                 }
                
//                 std::lock_guard<std::mutex> lock(result_mutex);
//                 consumed_by_consumer[consumer_id] = std::move(local_consumed);
//             });
//         }
        
//         // Wait for all threads
//         producer.join();
//         for (auto& consumer : consumers) {
//             consumer.join();
//         }
        
//         // Verify: Each message consumed exactly once
//         std::unordered_set<int> all_consumed;
//         int total_consumed = 0;
//         for (int i = 0; i < NUM_CONSUMERS; ++i) {
//             total_consumed += consumed_by_consumer[i].size();
//             for (int val : consumed_by_consumer[i]) {
//                 if (all_consumed.count(val) > 0) {
//                     std::cerr << "  ERROR: Message " << val << " consumed by multiple consumers!\n";
//                     all_tests_passed = false;
//                     goto test9_end;
//                 }
//                 all_consumed.insert(val);
//             }
//         }
        
//         if (total_consumed != NUM_MESSAGES) {
//             std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
//             all_tests_passed = false;
//             goto test9_end;
//         }
        
//         std::cout << "  PASS (consumed " << total_consumed << " messages uniquely)\n";
//         ++pass_count;
//         test9_end:;
//     }
    
//     // Test 10: Multi-threaded SPMC - High contention (many consumers)
//     {
//         ++test_count;
//         std::cout << "Test " << test_count << ": Multi-threaded SPMC (8 consumers, high contention)\n";
//         SPMCUnicast<int, 512> queue;
        
//         const int NUM_MESSAGES = 5000;
//         const int NUM_CONSUMERS = 8;
        
//         std::atomic<bool> producer_done{false};
//         std::vector<std::vector<int>> consumed_by_consumer(NUM_CONSUMERS);
//         std::mutex result_mutex;
        
//         // Producer thread
//         std::thread producer([&]() {
//             for (int i = 0; i < NUM_MESSAGES; ++i) {
//                 while (!queue.PushOne(i)) {
//                     std::this_thread::yield();
//                 }
//             }
//             producer_done.store(true, std::memory_order_release);
//         });
        
//         // Consumer threads
//         std::vector<std::thread> consumers;
//         for (int consumer_id = 0; consumer_id < NUM_CONSUMERS; ++consumer_id) {
//             consumers.emplace_back([&, consumer_id]() {
//                 std::vector<int> local_consumed;
//                 int consecutive_empty = 0;
//                 while (true) {
//                     auto value = queue.Pop();
//                     if (value.has_value()) {
//                         local_consumed.push_back(value.value());
//                         consecutive_empty = 0;
//                     } else {
//                         // Only exit if producer is done AND we've seen empty queue multiple times
//                         if (producer_done.load(std::memory_order_acquire)) {
//                             if (++consecutive_empty >= 100) {
//                                 break;
//                             }
//                         }
//                         std::this_thread::yield();
//                     }
//                 }
                
//                 std::lock_guard<std::mutex> lock(result_mutex);
//                 consumed_by_consumer[consumer_id] = std::move(local_consumed);
//             });
//         }
        
//         // Wait for all threads
//         producer.join();
//         for (auto& consumer : consumers) {
//             consumer.join();
//         }
        
//         // Verify: Each message consumed exactly once
//         std::unordered_set<int> all_consumed;
//         int total_consumed = 0;
//         for (int i = 0; i < NUM_CONSUMERS; ++i) {
//             total_consumed += consumed_by_consumer[i].size();
//             for (int val : consumed_by_consumer[i]) {
//                 if (all_consumed.count(val) > 0) {
//                     std::cerr << "  ERROR: Message " << val << " consumed by multiple consumers!\n";
//                     all_tests_passed = false;
//                     goto test10_end;
//                 }
//                 all_consumed.insert(val);
//             }
//         }
        
//         if (total_consumed != NUM_MESSAGES) {
//             std::cerr << "  ERROR: Expected " << NUM_MESSAGES << " messages, got " << total_consumed << "\n";
//             all_tests_passed = false;
//             goto test10_end;
//         }
        
//         std::cout << "  PASS (consumed " << total_consumed << " messages uniquely across " << NUM_CONSUMERS << " consumers)\n";
//         ++pass_count;
//         test10_end:;
//     }

//     // Summary
//     std::cout << "\n";
//     std::cout << "========================================\n";
//     std::cout << "Test Summary: " << pass_count << "/" << test_count << " tests passed\n";
//     std::cout << "========================================\n";

//     if (all_tests_passed) {
//         std::cout << "SUCCESS: All tests passed!\n";
//         return 0;
//     } else {
//         std::cerr << "FAILURE: Some tests failed. See errors above.\n";
//         return 1;
//     }
// }
