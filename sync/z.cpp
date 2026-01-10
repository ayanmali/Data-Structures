#include <atomic>
#include <thread>

template <typename T>
class RCU {
    private:
        std::atomic<T*> data;

        // Simple grace period: wait for all readers
        // In production, use epoch-based or quiescent-state mechanisms
        std::atomic<size_t> reader_count;

    public:
        explicit RCU(T* initial_data) : data(initial_data), reader_count(0) {}

        ~RCU() {
            delete data.load(std::memory_order_acquire);
        }

        // Read-side critical section
        class ReadGuard {
            RCU* rcu;
            T* snapshot;
            public:
                explicit ReadGuard(RCU* r) : rcu(r) {
                    rcu->reader_count.fetch_add(1, std::memory_order_acquire);
                    snapshot = rcu->data.load(std::memory_order_consume);
                }

                ~ReadGuard() {
                    rcu->reader_count.fetch_sub(1, std::memory_order_release);
                }

                const T* operator->() const { return snapshot; }
                const T& operator*() const { return *snapshot; }
        };

        ReadGuard read() {
            return ReadGuard(this);
        }

        // Write with grace period
        void write(T* new_data) {
            // Atomically swap the pointer
            T* old_data = data.exchange(new_data, std::memory_order_acq_rel);

            // Grace period: wait for all active readers to finish
            synchronize();

            // Safe to delete old data now
            delete old_data;
        }

        // Update in place (read-modify-write)
        template <typename Func>
        void update(Func&& func) {
            // Read current data
            T* old_data = data.load(std::memory_order_acquire);

            // Create modified copy
            T* new_data = new T(*old_data);
            func(*new_data);

            // Publish new data
            write(new_data);
        }

    private:
        void synchronize() {
            // Wait for all readers to exit their critical sections
            // sleep (yield) until all readers are finished
            while (reader_count.load(std::memory_order_acquire) > 0) {
                std::this_thread::yield();
            }
        }
};