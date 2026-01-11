#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
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
auto make_unique_arena(ArenaAllocator& allocator, Args&&... args) {
    void* ptr = allocator.allocate(sizeof(T), alignof(T));
    const auto deleter = [&allocator](T* p) {
        // call destructor
        p->~T();
        // clear the memory and update the allocator's offset
        std::memset(p, 0, sizeof(T));
        allocator.current_offset -= sizeof(T);
    };

    // construct the object in the allocated memory (placement new)
    new (ptr) T(std::forward<Args>(args)...);

    return std::unique_ptr<T, decltype(deleter)>(static_cast<T*>(ptr), deleter);
}


template <typename T, typename... Args>
std::shared_ptr<T> make_shared_arena(ArenaAllocator& allocator, Args&&... args) {
    void* ptr = allocator.allocate(sizeof(T), alignof(T));
    const auto deleter = [&allocator](T* p) {
        // call destructor
        p->~T();
        // clear the memory and update the allocator's offset
        std::memset(p, 0, sizeof(T));
        allocator.current_offset -= sizeof(T);
    };

    // construct the object in the allocated memory (placement new)
    new (ptr) T(std::forward<Args>(args)...);

    auto shared_ptr = std::shared_ptr<T>(static_cast<T*>(ptr), deleter);

    return shared_ptr;
};

int main() {
    ArenaAllocator allocator(sizeof(int) * 10);
    std::cout << "Arena Allocator\n";
    auto p = make_unique_arena<int>(allocator, 42);
    std::cout << "p: " << *p << "\n";
    auto q = make_shared_arena<int>(allocator, 42);
    std::cout << "q: " << *q << "\n";

}