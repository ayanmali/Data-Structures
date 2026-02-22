#include <iostream>
#include <unordered_map>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <iomanip>
#include "HashTable.hpp"

using namespace std::chrono;

// Benchmark configuration
struct BenchmarkConfig {
    int numOperations;
    std::string description;
};

// Helper function to generate random integers
std::vector<int> generateRandomInts(int count, int seed = 42) {
    std::vector<int> result;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dis(1, count * 10);
    for (int i = 0; i < count; ++i) {
        result.push_back(dis(gen));
    }
    return result;
}

// Benchmark custom HashTable insertions
template<typename K, typename V>
long long benchmarkCustomInsert(const std::vector<K>& keys, const std::vector<V>& values) {
    HashTable<K, V> ht(keys.size());
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < keys.size(); ++i) {
        ht.set(keys[i], values[i]);
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Benchmark std::unordered_map insertions
template<typename K, typename V>
long long benchmarkStdInsert(const std::vector<K>& keys, const std::vector<V>& values) {
    std::unordered_map<K, V> ht;
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < keys.size(); ++i) {
        ht[keys[i]] = values[i];
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Benchmark custom HashTable lookups
template<typename K, typename V>
long long benchmarkCustomLookup(HashTable<K, V>& ht, const std::vector<K>& keys) {
    auto start = high_resolution_clock::now();
    
    size_t foundCount = 0; // count to prevent optimization
    for (const auto& key : keys) {
        try {
            V temp = ht.get(key);
            (void)temp; // suppress unused warning
            foundCount++;
        } catch (...) {
            // Key not found
        }
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Benchmark std::unordered_map lookups
template<typename K, typename V>
long long benchmarkStdLookup(std::unordered_map<K, V>& ht, const std::vector<K>& keys) {
    auto start = high_resolution_clock::now();
    
    size_t foundCount = 0; // count to prevent optimization
    for (const auto& key : keys) {
        auto it = ht.find(key);
        if (it != ht.end()) {
            V temp = it->second;
            (void)temp; // suppress unused warning
            foundCount++;
        }
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Benchmark custom HashTable deletions
template<typename K, typename V>
long long benchmarkCustomDelete(HashTable<K, V>& ht, const std::vector<K>& keys) {
    auto start = high_resolution_clock::now();
    
    for (const auto& key : keys) {
        ht.remove(key);
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Benchmark std::unordered_map deletions
template<typename K, typename V>
long long benchmarkStdDelete(std::unordered_map<K, V>& ht, const std::vector<K>& keys) {
    auto start = high_resolution_clock::now();
    
    for (const auto& key : keys) {
        ht.erase(key);
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count();
}

// Print benchmark results
void printResults(const std::string& testName, long long customTime, long long stdTime) {
    std::cout << std::setw(30) << std::left << testName 
              << " | Custom: " << std::setw(10) << std::right << customTime << " μs"
              << " | Std: " << std::setw(10) << std::right << stdTime << " μs"
              << " | Ratio: " << std::fixed << std::setprecision(2) 
              << (double)customTime / stdTime << "x" << std::endl;
}

void runBenchmark(const BenchmarkConfig& config) {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "Benchmark: " << config.description << std::endl;
    std::cout << "Operations: " << config.numOperations << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    // Generate test data
    std::vector<int> keys = generateRandomInts(config.numOperations);
    std::vector<std::string> values;
    for (int i = 0; i < config.numOperations; ++i) {
        values.push_back("value_" + std::to_string(i));
    }
    
    // 1. Insertion Benchmark
    long long customInsertTime = benchmarkCustomInsert<int, std::string>(keys, values);
    long long stdInsertTime = benchmarkStdInsert<int, std::string>(keys, values);
    printResults("Insert", customInsertTime, stdInsertTime);
    
    // 2. Lookup Benchmark - prepare data structures
    HashTable<int, std::string> customHt(config.numOperations);
    std::unordered_map<int, std::string> stdHt;
    for (size_t i = 0; i < keys.size(); ++i) {
        customHt.set(keys[i], values[i]);
        stdHt[keys[i]] = values[i];
    }
    
    long long customLookupTime = benchmarkCustomLookup<int, std::string>(customHt, keys);
    long long stdLookupTime = benchmarkStdLookup<int, std::string>(stdHt, keys);
    printResults("Lookup (existing keys)", customLookupTime, stdLookupTime);
    
    // 3. Lookup non-existing keys
    std::vector<int> nonExistingKeys = generateRandomInts(config.numOperations / 10, 999);
    long long customLookupMissTime = benchmarkCustomLookup<int, std::string>(customHt, nonExistingKeys);
    long long stdLookupMissTime = benchmarkStdLookup<int, std::string>(stdHt, nonExistingKeys);
    printResults("Lookup (missing keys)", customLookupMissTime, stdLookupMissTime);
    
    // 4. Update Benchmark
    std::vector<std::string> newValues;
    for (int i = 0; i < config.numOperations; ++i) {
        newValues.push_back("updated_" + std::to_string(i));
    }
    long long customUpdateTime = benchmarkCustomInsert<int, std::string>(keys, newValues);
    long long stdUpdateTime = benchmarkStdInsert<int, std::string>(keys, newValues);
    printResults("Update", customUpdateTime, stdUpdateTime);
    
    // 5. Deletion Benchmark
    HashTable<int, std::string> customHt2(config.numOperations);
    std::unordered_map<int, std::string> stdHt2;
    for (size_t i = 0; i < keys.size(); ++i) {
        customHt2.set(keys[i], values[i]);
        stdHt2[keys[i]] = values[i];
    }
    
    // Delete half of the keys
    std::vector<int> keysToDelete(keys.begin(), keys.begin() + keys.size() / 2);
    long long customDeleteTime = benchmarkCustomDelete<int, std::string>(customHt2, keysToDelete);
    long long stdDeleteTime = benchmarkStdDelete<int, std::string>(stdHt2, keysToDelete);
    printResults("Delete (50% of keys)", customDeleteTime, stdDeleteTime);
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   Hash Table Performance Benchmark                             ║" << std::endl;
    std::cout << "║   Custom Implementation vs std::unordered_map                  ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
    
    // Run benchmarks with different sizes
    std::vector<BenchmarkConfig> configs = {
        {100, "Small dataset (100 elements)"},
        {1000, "Medium dataset (1,000 elements)"},
        {10000, "Large dataset (10,000 elements)"},
        {50000, "Extra large dataset (50,000 elements)"}
    };
    
    for (const auto& config : configs) {
        runBenchmark(config);
    }
    
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "Benchmark completed!" << std::endl;
    std::cout << "Note: Lower times are better. Ratio < 1.0 means custom is faster." << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    return 0;
}
