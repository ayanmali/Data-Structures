#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <tuple>
#include <utility>
#include <mutex>
#include "../queues/spmc_unicast_fixed_size.hpp"

/*
 * Thread pool implementation supporting functions with parameters without std::function
 * 
 * Uses polymorphism (virtual functions) for type erasure:
 * - Base Task class with pure virtual execute()
 * - Templated TaskImpl stores any callable with its arguments
 * - Perfect forwarding and std::decay_t ensure proper type storage
 * - std::apply invokes the callable with stored arguments
 */

// Mutex for thread-safe console output
std::mutex cout_mutex;

// Base class for type-erased tasks
struct Task {
    virtual ~Task() = default;
    virtual void execute() = 0;
};

// Templated task implementation that captures callable and arguments
template <typename F, typename... Args>
struct TaskImpl : Task {
    std::decay_t<F> func;                    // Decay removes references/cv-qualifiers
    std::tuple<std::decay_t<Args>...> args;  // Store arguments as values
    
    TaskImpl(F&& f, Args&&... a) 
        : func(std::forward<F>(f)), args(std::forward<Args>(a)...) {}
    
    void execute() override {
        std::apply(func, args);  // Invoke func with unpacked args
    }
};

int work() {
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Executing work()\n";
    }
    int x = 2;
    int y = 3;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int res = x + y;
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "work() result: " << res << "\n";
    }
    return res;
}

int work_with_params(int a, int b) {
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Executing work_with_params(" << a << ", " << b << ")\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int res = a + b;
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "work_with_params(" << a << ", " << b << ") result: " << res << "\n";
    }
    return res;
}

template <uint32_t N>
struct FixedThreadPool {
    SPMCUnicast<Task*, N> tasks;  // Initialize queue first
    std::array<std::thread, N> threads;

    void spin_and_consume_task() {
        while (true) {
            Task* task;
            auto start = std::chrono::steady_clock::now();
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            // Try to pop a task with timeout
            while (!tasks.Pop(&task)) {
                end = std::chrono::steady_clock::now();
                duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (duration.count() > 10000) {
                    return; // Exit if no task for 10 seconds
                }
            }
            
            task->execute();
            delete task; // Clean up the task after execution
        }
    }

    FixedThreadPool() {
        // Start all worker threads
        for (size_t i = 0; i < N; ++i) {
            threads[i] = std::thread(&FixedThreadPool::spin_and_consume_task, this);
        }
    }

    ~FixedThreadPool() {
        for (auto& t : threads) {
            t.join();
        }
    }

    // Submit any callable with any arguments
    template <typename F, typename... Args>
    void submit(F&& func, Args&&... args) {
        using DecayF = std::decay_t<F>;
        using DecayArgs = std::tuple<std::decay_t<Args>...>;
        auto* task = new TaskImpl<DecayF, std::decay_t<Args>...>(std::forward<F>(func), std::forward<Args>(args)...);
        tasks.PushOne(task);
    }

};

int main() {
    auto tp = FixedThreadPool<3>();
    
    // Example 1: Function without parameters
    tp.submit(&work);
    
    // Example 2: Functions with parameters
    tp.submit(&work_with_params, 10, 20);
    tp.submit(&work_with_params, 5, 15);
    
    // Example 3: Lambda with captures
    int x = 100;
    tp.submit([x](int y) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Lambda result: " << x + y << "\n";
    }, 50);
    
    // Example 4: Lambda without captures
    tp.submit([](int a, int b) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Lambda result: " << a * b << "\n";
    }, 7, 8);
    
    // Wait for tasks to complete (worker threads auto-exit after 10s idle)
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    return 0;
}