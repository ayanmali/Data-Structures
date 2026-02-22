#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <functional>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>

/*
Hash table based on hybrid chaining + open addressing
Each key is associated with a bucket index.
Each key's value is a pointer to its data.
KVPair storage is pre-allocated in a pool to avoid per-insert heap allocations.
*/

template <typename K, typename V>
struct KVPair {
    K k;
    V v;
};

enum class SlotState : uint8_t { EMPTY, OCCUPIED, TOMBSTONE };

template <typename K, typename V>
struct HashPair {
    alignas(64) size_t hash;
    alignas(64) KVPair<K, V>* kv_ptr;
    SlotState state;

    HashPair() : hash(0), kv_ptr(nullptr), state(SlotState::EMPTY) {}
    HashPair(size_t hash, KVPair<K,V>* kv_ptr) : hash(hash), kv_ptr(kv_ptr), state(SlotState::OCCUPIED) {}
};

template <typename K, typename V>
// TODO: replace std::function w/ a lambda or inplace_function
class HashTable {
    private:
        size_t size;
        std::function<size_t(K)> hash_function;
        std::vector<HashPair<K, V>> table;      // hash table entries (pointers into pool)
        std::vector<KVPair<K, V>> pool;          // pre-allocated KVPair storage
        std::vector<size_t> free_list;           // stack of available pool indices

        // Grab a pool slot, write the key-value into it, and return a pointer
        KVPair<K, V>* pool_alloc(const K& key, const V& value) {
            if (free_list.empty()) {
                throw std::runtime_error("Pool exhausted");
            }
            size_t slot = free_list.back();
            free_list.pop_back();
            pool[slot].k = key;
            pool[slot].v = value;
            return &pool[slot];
        }

        // Return a pool slot to the free list
        void pool_free(KVPair<K, V>* kv_ptr) {
            size_t slot = static_cast<size_t>(kv_ptr - pool.data());
            free_list.push_back(slot);
        }

    public:
        HashTable(size_t size, std::function<size_t(K)> hash_function) 
        : size(size),
          hash_function(hash_function),
          table(size),
          pool(size),
          free_list(size)
        {
            // Initialize free list with all pool indices
            for (size_t i = 0; i < size; ++i) {
                free_list[i] = i;
            }
        };

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
    for (const auto& entry : this->table) {
        std::cout << "Bucket " << entry.first << ": " << (entry.second ? entry.second->first : K()) << " -> " << (entry.second ? entry.second->second : V()) << "\n";
    }
    std::cout << "\n";
};

template <typename K, typename V>
V HashTable<K, V>::get(K key) {
    const size_t index = hash_function(key) % size;
    for (size_t step = 0; step < size; ++step) {
        const size_t i = (index + step) % size;
        if (table[i].state == SlotState::EMPTY) {
            break;  // key can't exist beyond an empty slot
        }
        if (table[i].state == SlotState::OCCUPIED && key == table[i].kv_ptr->k) {
            return table[i].kv_ptr->v;
        }
        // TOMBSTONE: keep probing
    }
    throw std::runtime_error("Key not found");
};

template<typename K, typename V>
void HashTable<K, V>::remove(K key) {
    const size_t index = hash_function(key) % size;
    for (size_t step = 0; step < size; ++step) {
        const size_t i = (index + step) % size;
        if (table[i].state == SlotState::EMPTY) {
            break;  // key can't exist beyond an empty slot
        }
        if (table[i].state == SlotState::OCCUPIED && key == table[i].kv_ptr->k) {
            pool_free(table[i].kv_ptr);
            table[i].kv_ptr = nullptr;
            table[i].state = SlotState::TOMBSTONE;  // mark as deleted, don't break probe chains
            return;
        }
        // TOMBSTONE: keep probing
    }
};

template <typename K, typename V>
void HashTable<K, V>::set(K key, V value) {
    const size_t hashcode = hash_function(key);
    const size_t index = hashcode % size;
    size_t first_tombstone = size;  // sentinel: no tombstone seen yet

    for (size_t step = 0; step < size; ++step) {
        const size_t i = (index + step) % size;

        if (table[i].state == SlotState::OCCUPIED && key == table[i].kv_ptr->k) {
            // Key already exists — update in place
            table[i].hash = hashcode;
            table[i].kv_ptr->v = value;
            return;
        }
        if (table[i].state == SlotState::TOMBSTONE && first_tombstone == size) {
            first_tombstone = i;  // remember first reusable slot
        }
        if (table[i].state == SlotState::EMPTY) {
            // Key doesn't exist — insert at first tombstone or this empty slot
            const size_t insert_at = (first_tombstone != size) ? first_tombstone : i;
            table[insert_at].hash = hashcode;
            table[insert_at].kv_ptr = pool_alloc(key, value);
            table[insert_at].state = SlotState::OCCUPIED;
            return;
        }
    }

    // Entire table is OCCUPIED/TOMBSTONE with no EMPTY slot
    if (first_tombstone != size) {
        // Reuse the first tombstone we found
        table[first_tombstone].hash = hashcode;
        table[first_tombstone].kv_ptr = pool_alloc(key, value);
        table[first_tombstone].state = SlotState::OCCUPIED;
        return;
    }

    throw std::runtime_error("Hash table is full, no empty slot found");
};

#endif // HASHTABLE_H
