/*
A seqlock is a lock that allows for concurrent reads but only one writer.
Designed to be wait-free for writers and lock-free for readers.
*/

#include <atomic>

template <typename T>
// TODO: test cache alignment
struct Seqlock {
    std::atomic_uint32_t seq;
    std::atomic<T> data;

    T read() {
        uint32_t seq1, seq2;
        T d1;
        do {
            seq1 = seq.load(std::memory_order_acquire);
            // "critical section"
            d1 = data.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            seq2 = seq.load(std::memory_order_relaxed);
        } while (seq1 != seq2 || seq1 & 1); // spin until both sequence nums are the same and even
        
        return d1;
    };


    // pass by value
    void write(const T new_data) {
        uint32_t seq1 = seq.load();
        // spin while a write is in progress
        while (seq1 & 1 || !seq.compare_exchange_weak(seq1, seq1+1)) {};
        // "critical section"
        data = new_data;
        seq = seq1 + 2;
    };

    // TODO: test to see if this works
    // Passing by reference for larger data types
    void write(const T& new_data) {
        uint32_t seq1 = seq.load();
        // spin while a write is in progress
        while (seq1 & 1 || !seq.compare_exchange_weak(seq1, seq1+1)) {};
        // "critical section"
        data = new_data;
        seq = seq1 + 2;  
    }
};