#include <iostream>

constexpr size_t CACHE_LINE_SIZE = 64;

template <typename T>
struct FastQueue {
    std::vector<T> data;

    Queue() {};
    void enqueue(T value);
    T dequeue();
}

struct FastQueueWriter {
    FastQueue<T>* queue;
}

struct FastQueueReader {
    FastQueue<T>* queue;
}