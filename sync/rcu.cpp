/*
Userspace RCU
*/
#include <atomic>
#include <cstddef>
#include <thread>

template <typename T>

// TODO: test cache alignment
struct RCU {
    private:
        // pointer to the data
        std::atomic<T*> data;

        std::atomic<size_t> reader_count;

    public:
        explicit RCU(T* initial_data): data(initial_data), reader_count(0) {}

        ~RCU() {
            delete data.load(std::memory_order_acquire);
        }

        // delete copy constructor/assignment
        RCU(const RCU& r) = delete;
        RCU& operator=(const RCU& r) = delete;

        struct ReadGuard {
            private:
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

                const T* operator->() { return snapshot; }
                const T& operator*() { return *snapshot; }
        };

        ReadGuard read() {
            return ReadGuard(this);
        }

        void write(T* new_data) {
            // atomically swap the pointer
            T* old_data = data.exchange(new_data, std::memory_order_acq_rel);
            // wait for active readers to finish
            synchronize();
            // safe to delete the old data
            delete old_data;
        }

        template <typename Func>
        void update(Func&& func) {
            // read current data
            T* old_data = data.load();

            // copy
            T* new_data = new T(*old_data);
            func(*new_data);

            // update
            write(new_data);
            
        }

    private:
        void synchronize() {
            // wait for all readers to exit their critical section
            // sleep (yield) until all readers are finished
            while (reader_count.load(std::memory_order_acquire) > 0) {
                std::this_thread::yield();
            }
        }
};