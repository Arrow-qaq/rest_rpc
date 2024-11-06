/*****************************************************
 * FilePath: ThreadPool.hpp
 * Author: Arrow
 * Date: 2023-05-16 09:59:32
 * LastEditors: Arrow
 * LastEditTime: 2024-10-31 15:34:02
 * Description:
 ******************************************************/
#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    ThreadPool(size_t numThreads) : stop(false)
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.emplace_back([this] {
                for (;;)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        using return_type = typename std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    template<class F, class... Args>
    static auto run(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        return getInstance().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;

    std::mutex              queue_mutex;
    std::condition_variable condition;
    bool                    stop;

    static ThreadPool& getInstance()
    {
        static ThreadPool instance(std::thread::hardware_concurrency());
        return instance;
    }
};
