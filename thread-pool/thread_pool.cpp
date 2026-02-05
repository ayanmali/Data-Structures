struct fixed_thread_pool {
    private:
        std::vector<std::thread> threads;
        std::queue tasks;
        
    public:
        fixed_thread_pool(int count) {
            
        }

        template <typename T, typename... Args>
        void put_task(T func, &&Args... args) {

            // func(args);
        }

}

struct dynamic_thread_pool {

}