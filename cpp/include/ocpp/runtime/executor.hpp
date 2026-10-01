#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace ocpp {
// A station always hashes to the same FIFO lane. Slow SQL never runs on an I/O loop.
class Executor {
    struct Lane {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::function<void()>> queue;
        bool stopping = false;
        std::thread thread;
    };
    std::vector<std::unique_ptr<Lane>> lanes_;
    std::size_t capacity_;

  public:
    Executor(unsigned workers, std::size_t capacity);
    ~Executor();
    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;
    bool submit(std::string_view key, std::function<void()> task);
};
} // namespace ocpp
