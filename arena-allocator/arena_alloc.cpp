#include <cstddef>
#include <vector>

struct ArenaAllocator {
    std::vector<char> data;
    size_t current_offset;

    ArenaAllocator(size_t arena_size) : data(arena_size), current_offset(0) {};
    ~ArenaAllocator() {
        data.clear();
    }
    void* allocate(size_t size) {
        void* ptr = data.data() + current_offset;
        current_offset += size;
        return ptr;
    }

};

template <typename T, typename... Args>
std::unique_ptr<T> make_unique_arena(ArenaAllocator& allocator, Args&&... args) {
   // return std::unique_ptr<T>{new T{std::forward<Args>(args)...}};
};

template <typename T, typename... Args>
std::shared_ptr<T> make_shared_arena(ArenaAllocator& allocator, Args&&... args) {
   // return std::shared_ptr<T>{new T{std::forward<Args>(args)...}};
};