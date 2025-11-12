#include <iostream>
#include <functional>
#include <vector>
#include <memory>

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
        std::vector<std::pair<size_t, std::unique_ptr<std::pair<K, V>> > > table; // each entry is a pair of the hash and the pointer to the KV pair
        //std::vector<std::pair<K, V>> buckets; // each entry is a pair of the key and the value

    public:
        HashTable(size_t size, std::function<size_t(K)> hashFunction) 
        : size(size),
          hashFunction(hashFunction),
          table(size) {};

        HashTable(size_t size) 
        : HashTable(size, 
            [size](K key) -> size_t { return std::hash<K>()(key); } ) {};
        
        void print();
        V get(K key);
        void remove(K key);
        void set(K key, V value);
        
};

template<typename K, typename V>
void HashTable<K, V>::print() {
    for (const std::pair<size_t, std::unique_ptr<std::pair<K, V>>>& entry : this->table) {
        std::cout << "Bucket " << entry.first << ": " << (entry.second ? entry.second->first : K()) << " -> " << (entry.second ? entry.second->second : V()) << std::endl;
    }
    std::cout << std::endl;
};

template <typename K, typename V>
V HashTable<K, V>::get(K key) {
    // get the index of the given key
    const size_t index = this->hashFunction(key) % this->size;
    // check if that index's key matches the provided key
    if (this->table[index].second != nullptr) {
        auto& [foundKey, value] = *this->table[index].second;
        if (key == foundKey) { // match
            return value;
        }
    }
    // does not match - search forward
    for (size_t i = index + 1; i < this->size; ++i) {
        if (this->table[i].second != nullptr) {
            auto& [foundKey, value] = *this->table[i].second;
            if (key == foundKey) {
                return value;
            }
        }
    }
    // Key not found - return default value (you might want to throw an exception instead)
    throw std::runtime_error("Key not found");
};

template<typename K, typename V>
void HashTable<K, V>::remove(K key) {
    // get the index of the given key
    const size_t index = this->hashFunction(key) % this->size;
    // check if that index's key matches the provided key
    if (this->table[index].second != nullptr) {
        auto& [foundKey, value] = *this->table[index].second;
        if (key == foundKey) { // match
            // delete the KV pair by resetting the unique_ptr
            this->table[index].second.reset();
            return;
        }
    }
    // does not match - search forward
    for (size_t i = index + 1; i < this->size; ++i) {
        if (this->table[i].second != nullptr) {
            auto& [foundKey, value] = *this->table[i].second;
            if (key == foundKey) {
                this->table[i].second.reset();
                return;
            }
        }
    }
    std::cout << "Key " << key << " not found" << std::endl;
};

template <typename K, typename V>
void HashTable<K, V>::set(K key, V value) {
    // compute the index to insert
    const size_t hash = this->hashFunction(key);
    size_t index = hash % this->size;
    const size_t indexCopy = index;
    
    // First, check if the key already exists
    while (index < this->size + indexCopy) {
        const size_t currentIndex = index % this->size;
        if (this->table[currentIndex].second != nullptr) {
            auto& [foundKey, foundValue] = *this->table[currentIndex].second;
            if (key == foundKey) {
                // Key already exists, update its value
                this->table[currentIndex].first = hash;
                this->table[currentIndex].second->second = value;
                return;
            }
        } else {
            // Found an empty slot, insert here
            this->table[currentIndex].first = hash;
            this->table[currentIndex].second = std::make_unique<std::pair<K, V>>(key, value);
            return;
        }
        ++index;
    }
    
    // No empty slot found, add to the end of the vector
    this->size++;
    this->table.push_back(std::make_pair(hash, std::make_unique<std::pair<K, V>>(key, value)));
};

int main() {
    std::cout << "Hash Table based on hybrid chaining + open addressing in C++" << std::endl;
    HashTable<int, std::string> map = HashTable<int, std::string>(5);
    map.print();
    map.set(1, "one");
    map.set(2, "two");
    map.set(3, "three");
    map.set(4, "four");
    map.set(5, "five");
    map.print();
    map.remove(3);
    map.print();
    map.set(6, "six");
    map.print();
    return 0;
};