#include "ocpp/runtime/executor.hpp"
#include <stdexcept>
#include <string_view>

namespace ocpp {
Executor::Executor(unsigned workers, std::size_t capacity) : capacity_(capacity) {
    if (!workers || workers > 32 || !capacity)
        throw std::invalid_argument("Invalid executor limits");
    for (unsigned i = 0; i < workers; ++i)
        lanes_.push_back(std::make_unique<Lane>());
    try {
        for (auto &lane : lanes_)
            lane->thread = std::thread([p = lane.get()] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(p->mutex);
                        p->ready.wait(lock, [&] { return p->stopping || !p->queue.empty(); });
                        if (p->queue.empty() && p->stopping)
                            break;
                        task = std::move(p->queue.front());
                        p->queue.pop_front();
                    }
                    try {
                        task();
                    } catch (...) { /* Tasks own their failure channel; keep worker alive. */
                    }
                }
            });
    } catch (...) {
        for (auto &lane : lanes_) {
            {
                std::lock_guard lock(lane->mutex);
                lane->stopping = true;
            }
            lane->ready.notify_one();
        }
        for (auto &lane : lanes_)
            if (lane->thread.joinable())
                lane->thread.join();
        throw;
    }
}
Executor::~Executor() {
    for (auto &lane : lanes_) {
        {
            std::lock_guard lock(lane->mutex);
            lane->stopping = true;
        }
        lane->ready.notify_one();
    }
    for (auto &lane : lanes_)
        lane->thread.join();
}
bool Executor::submit(std::string_view key, std::function<void()> task) {
    auto &lane = *lanes_[std::hash<std::string_view>{}(key) % lanes_.size()];
    {
        std::lock_guard lock(lane.mutex);
        if (lane.stopping || lane.queue.size() >= capacity_)
            return false;
        lane.queue.push_back(std::move(task));
    }
    lane.ready.notify_one();
    return true;
}
} // namespace ocpp
