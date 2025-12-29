#include <cstddef>
#include <vector>

struct ArenaAllocator {
    std::vector<char> data;
    std::size_t current_offset;

    explicit ArenaAllocator(std::size_t arena_size) : data(arena_size), current_offset(0) {};
    ~ArenaAllocator() {
        data.clear();
    }
    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        std::size_t padding = (alignment - (current_offset % alignment)) % alignment;
        current_offset += padding;
        void* raw_ptr = data.data() + current_offset;
        current_offset += size;
        return raw_ptr;
    }

};

template <typename T, typename... Args>
std::unique_ptr<T> make_unique_arena(ArenaAllocator& allocator, Args&&... args) {
    T* ptr = static_cast<T*>(allocator.allocate(sizeof(T), alignof(T)));
    return std::unique_ptr<T>(ptr);
};


template <typename T, typename... Args>
std::unique_ptr<T> make_shared_arena(ArenaAllocator& allocator, Args&&... args) {
    T* ptr = static_cast<T*>(allocator.allocate(sizeof(T), alignof(T)));
    return std::shared_ptr<T>(ptr);
};