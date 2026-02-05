#include "ring_buffer_utils.hpp"
#include <atomic>
#include <span>
#include <cstddef>
#include <sys/types.h>

/*
Contains a read and write counter. Consumers only read the counters.
By default, both counters point to the same location. When a write takes place,
the write counter is moved forward, the data is copied, and then the read counter
is moved up to match the write counter.

at any given point in time, [ReadCounter, WriteCounter] is the range of data that
is being written, and [WriteCounter, ReadCounter] is the range of data that can
be read.
*/
template <size_t N>
struct SPMCMulticast {
    alignas(CACHE_LINE_SIZE) std::atomic_uint64_t read_idx{0};
    alignas(CACHE_LINE_SIZE) std::atomic_uint64_t write_idx{0};
    alignas(CACHE_LINE_SIZE) uint64_t write_local_ctr{0};
    uint8_t buffer[N]; // buffer of bytes

    bool Push(std::span<std::byte> data) {
        size_t payload_size = data.size();
        size_t total_size = payload_size + HEADER_SIZE;
        if (total_size > N) return false;

        write_local_ctr += total_size;

        size_t write = write_idx.load();
        size_t read = read_idx.load();

        if (write - read + total_size > N) return false;  // not enough capacity
        size_t offset = write % N;

        // move write counter up
        write_idx.store(write_local_ctr);

        // copy data into buffer
        CopyIn(&buffer, offset, payload_size, HEADER_SIZE);
        CopyIn(&buffer, offset + HEADER_SIZE, data.data(), payload_size);

        // move read counter up
        read_idx.store(write_local_ctr);
        return true;
    }

    std::optional<std::vector<std::byte>> Pop() {

    }
};

template <size_t N>
struct MCConsumer {
    SPMCMulticast<N>* queue;
    uint64_t local_ctr;

    T value;

    // returns # of bytes read (0 if nothing to read)
    int32_t TryRead(std::span<std::byte> buffer) {


    }
};