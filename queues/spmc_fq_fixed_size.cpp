#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <span>
#include <thread>
#include <vector>
#include "ring_buffer_utils.hpp"

/*
Single-producer multiple-consumer multicast queue.
The producer writes messages sequentially and updates both read_idx and write_idx.
Multiple consumers can independently read from their own position (local_ctr).
Variable-length message size; Each message is: [size_t payload_size][payload bytes...].
*/

template <typename T, size_t N>
struct SPMCFastQueue {
    alignas(CACHE_LINE_SIZE) std::atomic_uint64_t read_idx{0};
    alignas(CACHE_LINE_SIZE) std::atomic_uint64_t write_idx{0};
    T buffer[N]; // buffer

};

template <typename T, size_t N>
struct MCConsumer {
    size_t local_ctr{0};

    // returns bytes read (empty if nothing to read)
    bool Pop(const SPMCFastQueue<T, N>& queue, T* payload) {
        uint64_t read = queue.read_idx.load(std::memory_order_acquire);
        
        // nothing to read
        if (local_ctr == read) {
            return false;
        }
        
        size_t offset = local_ctr & (N - 1);
        std::memcpy(payload, &queue.buffer[offset], sizeof(T));

        return true;
    }

    // returns bytes read (empty if nothing to read)
    std::vector<T> PopMany(const SPMCFastQueue<T, N>& queue, size_t num_elements) {
        uint64_t read = queue.read_idx.load(std::memory_order_acquire);
        
        // nothing to read
        if (local_ctr == read) {
            return std::vector<T>{};
        }
        
        size_t offset = local_ctr & (N - 1);
      
        std::vector<T> payload(num_elements);
        CopyOut(queue.buffer, N, offset, payload.data(), num_elements);
        
        local_ctr += num_elements;
        return payload;
    }
};

template <typename T, size_t N>
struct MCProducer {
    size_t local_ctr{0};
    
    // Returns true on success, false if there is not enough room.
    bool PushOne(SPMCFastQueue<T, N>& queue, T data) {
        // Check if there's enough space
        if (local_ctr + 1 > N) {
            return false;
        }

        // copy data into ring buffer
        size_t offset = local_ctr & (N - 1);
        CopyIn(queue.buffer, N, offset, &data, 1);

        local_ctr += 1;

        // set the write counter first (data is written)
        queue.write_idx.store(local_ctr, std::memory_order_release);
        
        // set the read counter (data is ready to be consumed)
        queue.read_idx.store(local_ctr, std::memory_order_release);
        return true;
    }

    bool PushMany(SPMCFastQueue<T, N>& queue, std::span<T> data) {
        const size_t total_size = data.size();
        
        // Check if there's enough space
        if (local_ctr + total_size > N) {
            return false;
        }

        // copy data into ring buffer
        size_t offset = local_ctr & (N - 1);
        CopyIn(queue.buffer, N, offset, data.data(), total_size);

        local_ctr += total_size;

        // set the write counter first (data is written)
        queue.write_idx.store(local_ctr, std::memory_order_release);
        
        // set the read counter (data is ready to be consumed)
        queue.read_idx.store(local_ctr, std::memory_order_release);
        return true;
    }
};

// TODO: refactor Pop() calls in tests

// Test helpers
bool test_single_message() {
    std::cout << "Test 1: Single message push and pop... ";
    
    SPMCFastQueue<256> queue;
    MCProducer<256> producer;
    MCConsumer<256> consumer;
    
    std::array<std::byte, 5> message{
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c},
        std::byte{0x6c}, std::byte{0x6f}  // "Hello"
    };
    
    if (!producer.Push(queue, message)) {
        std::cout << "FAILED (push failed)\n";
        return false;
    }
    
    std::vector<std::byte> popped = consumer.Pop(queue);
    if (popped.size() != message.size()) {
        std::cout << "FAILED (size mismatch: expected " << message.size() 
                  << ", got " << popped.size() << ")\n";
        return false;
    }
    
    for (size_t i = 0; i < message.size(); ++i) {
        if (popped[i] != message[i]) {
            std::cout << "FAILED (data mismatch at index " << i << ")\n";
            return false;
        }
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_multiple_messages() {
    std::cout << "Test 2: Multiple messages... ";
    
    SPMCFastQueue<256> queue;
    MCProducer<256> producer;
    MCConsumer<256> consumer;
    
    std::array<std::byte, 3> msg1{std::byte{1}, std::byte{2}, std::byte{3}};
    std::array<std::byte, 2> msg2{std::byte{4}, std::byte{5}};
    std::array<std::byte, 4> msg3{std::byte{6}, std::byte{7}, std::byte{8}, std::byte{9}};
    
    if (!producer.Push(queue, msg1)) {
        std::cout << "FAILED (push 1 failed)\n";
        return false;
    }
    if (!producer.Push(queue, msg2)) {
        std::cout << "FAILED (push 2 failed)\n";
        return false;
    }
    if (!producer.Push(queue, msg3)) {
        std::cout << "FAILED (push 3 failed)\n";
        return false;
    }
    
    auto popped1 = consumer.Pop(queue);
    if (popped1.size() != msg1.size() || popped1[0] != std::byte{1}) {
        std::cout << "FAILED (msg1 mismatch)\n";
        return false;
    }
    
    auto popped2 = consumer.Pop(queue);
    if (popped2.size() != msg2.size() || popped2[0] != std::byte{4}) {
        std::cout << "FAILED (msg2 mismatch)\n";
        return false;
    }
    
    auto popped3 = consumer.Pop(queue);
    if (popped3.size() != msg3.size() || popped3[0] != std::byte{6}) {
        std::cout << "FAILED (msg3 mismatch)\n";
        return false;
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_empty_pop() {
    std::cout << "Test 3: Pop from empty queue... ";
    
    SPMCFastQueue<256> queue;
    MCConsumer<256> consumer;
    
    auto popped = consumer.Pop(queue);
    if (!popped.empty()) {
        std::cout << "FAILED (expected empty vector)\n";
        return false;
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_multiple_consumers() {
    std::cout << "Test 4: Multiple consumers reading same data... ";
    
    SPMCFastQueue<256> queue;
    MCProducer<256> producer;
    MCConsumer<256> consumer1;
    MCConsumer<256> consumer2;
    MCConsumer<256> consumer3;
    
    std::array<std::byte, 4> message{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}};
    
    if (!producer.Push(queue, message)) {
        std::cout << "FAILED (push failed)\n";
        return false;
    }
    
    // All three consumers should be able to read the same message
    auto pop1 = consumer1.Pop(queue);
    auto pop2 = consumer2.Pop(queue);
    auto pop3 = consumer3.Pop(queue);
    
    if (pop1.size() != message.size() || pop2.size() != message.size() || pop3.size() != message.size()) {
        std::cout << "FAILED (size mismatch)\n";
        return false;
    }
    
    for (size_t i = 0; i < message.size(); ++i) {
        if (pop1[i] != message[i] || pop2[i] != message[i] || pop3[i] != message[i]) {
            std::cout << "FAILED (data mismatch)\n";
            return false;
        }
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_empty_message() {
    std::cout << "Test 5: Empty message... ";
    
    SPMCFastQueue<256> queue;
    MCProducer<256> producer;
    MCConsumer<256> consumer;
    
    std::array<std::byte, 0> empty_message{};
    
    if (!producer.Push(queue, empty_message)) {
        std::cout << "FAILED (push failed)\n";
        return false;
    }
    
    auto popped = consumer.Pop(queue);
    if (!popped.empty()) {
        std::cout << "FAILED (expected empty payload, got " << popped.size() << " bytes)\n";
        return false;
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_consumer_independence() {
    std::cout << "Test 6: Consumer independence... ";
    
    SPMCFastQueue<256> queue;
    MCProducer<256> producer;
    MCConsumer<256> consumer1;
    MCConsumer<256> consumer2;
    
    std::array<std::byte, 2> msg1{std::byte{1}, std::byte{2}};
    std::array<std::byte, 2> msg2{std::byte{3}, std::byte{4}};
    
    producer.Push(queue, msg1);
    producer.Push(queue, msg2);
    
    // Consumer1 reads first message
    auto c1_m1 = consumer1.Pop(queue);
    if (c1_m1.size() != 2 || c1_m1[0] != std::byte{1}) {
        std::cout << "FAILED (consumer1 msg1)\n";
        return false;
    }
    
    // Consumer2 should start from beginning and also read first message
    auto c2_m1 = consumer2.Pop(queue);
    if (c2_m1.size() != 2 || c2_m1[0] != std::byte{1}) {
        std::cout << "FAILED (consumer2 msg1)\n";
        return false;
    }
    
    // Consumer1 reads second message
    auto c1_m2 = consumer1.Pop(queue);
    if (c1_m2.size() != 2 || c1_m2[0] != std::byte{3}) {
        std::cout << "FAILED (consumer1 msg2)\n";
        return false;
    }
    
    // Consumer2 reads second message
    auto c2_m2 = consumer2.Pop(queue);
    if (c2_m2.size() != 2 || c2_m2[0] != std::byte{3}) {
        std::cout << "FAILED (consumer2 msg2)\n";
        return false;
    }
    
    std::cout << "PASSED\n";
    return true;
}

bool test_multithreaded_consumers() {
    std::cout << "Test 7: Multithreaded consumers... ";
    
    SPMCFastQueue<1024> queue;
    MCProducer<1024> producer;
    
    const int num_messages = 10;
    
    // Producer pushes messages
    for (int i = 0; i < num_messages; ++i) {
        std::array<std::byte, 1> msg{std::byte(i)};
        if (!producer.Push(queue, msg)) {
            std::cout << "FAILED (push failed at " << i << ")\n";
            return false;
        }
    }
    
    // Multiple consumers read in parallel
    bool success1 = true, success2 = true, success3 = true;
    
    auto consumer_func = [&](MCConsumer<1024>& consumer, bool& success) {
        int count = 0;
        while (count < num_messages) {
            auto msg = consumer.Pop(queue);
            if (!msg.empty()) {
                if (msg.size() != 1 || std::to_integer<int>(msg[0]) != count) {
                    success = false;
                    return;
                }
                count++;
            }
        }
    };
    
    MCConsumer<1024> c1, c2, c3;
    std::thread t1(consumer_func, std::ref(c1), std::ref(success1));
    std::thread t2(consumer_func, std::ref(c2), std::ref(success2));
    std::thread t3(consumer_func, std::ref(c3), std::ref(success3));
    
    t1.join();
    t2.join();
    t3.join();
    
    if (!success1 || !success2 || !success3) {
        std::cout << "FAILED (consumer read error)\n";
        return false;
    }
    
    std::cout << "PASSED\n";
    return true;
}

int main() {
    std::cout << "Running SPMC Fast Queue Tests\n";
    std::cout << "==============================\n\n";
    
    int passed = 0;
    int total = 7;
    
    if (test_single_message()) passed++;
    if (test_multiple_messages()) passed++;
    if (test_empty_pop()) passed++;
    if (test_multiple_consumers()) passed++;
    if (test_empty_message()) passed++;
    if (test_consumer_independence()) passed++;
    if (test_multithreaded_consumers()) passed++;
    
    std::cout << "\n==============================\n";
    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    
    return (passed == total) ? 0 : 1;
}