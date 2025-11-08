#include <iostream>
#include <functional>
#include <vector>

/*
Hash table based on hybrid chaining + open addressing
Each key is associated with a bucket index.
Each key's value is a pointer to its data
*/
template <typename K, typename V>
class HashTable {
    private:
        size_t size;
        std::function<size_t(K)> hashFunction;
        std::vector<std::pair<K, V>> table;

        //K computeHash(K key) {};
    public:
        HashTable(size_t size) {
            this->size = size;
            this->table.resize(size);
        }
        V get(K key) {};
        void set(K key, V value) {};
        void remove(K key) {};
        bool contains(K key) {};
};

int main() {
    std::cout << "Hash Table based on hybrid chaining + open addressing in C++" << std::endl;
    return 0;
};