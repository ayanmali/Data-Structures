#include <atomic>
#include <span>
#include <cstddef>
#include <sys/types.h>

constexpr size_t CACHE_LINE_SIZE = 64;

/*
Contains a read and write counter. Consumers only read the counters.
By default, both counters point to the same location. When a write takes place,
the write counter is moved forward, the data is copied, and then the read counter
is moved up to match the write counter.

at any given point in time, [ReadCounter, WriteCounter] is the range of data that
is being written, and [WriteCounter, ReadCounter] is the range of data that can
be read.
*/
template <typename T>
struct SPMCMulticast {
        uint8_t buffer[0];
        std::atomic_uint64_t read_ctr;
        std::atomic_uint64_t write_ctr;
        SPMCMulticast() : read_ctr(0), write_ctr(0) {}
    
};

template <typename T>
struct MCProducer {
    SPMCMulticast<T>* queue;
    uint64_t local_ctr;

    void Write(std::span<std::byte> buffer) {
        size_t payload_size = buffer.size();
        local_ctr += payload_size;
        // move write counter up
        queue->write_ctr.store(local_ctr);

        // copy

        // move read counter up
        queue->read_ctr.store(local_ctr);

    }
};

template <typename T>
struct MCConsumer {
    SPMCMulticast<T>* queue;
    uint64_t local_ctr;

    T value;

    // returns # of bytes read (0 if nothing to read)
    int32_t TryRead(std::span<std::byte> buffer) {


    }
};