#include "GameTasks.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
namespace WorldRPG::GameTasks {
namespace {
struct Task {
    std::function<void()> action;
    bool query = false;
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false, done = false, cancelled = false;
};
using Clock = std::chrono::steady_clock;
// The first batch keeps its normal frame boundary. Only short native reads
// may follow it while the caller walks the same Lua callback.
constexpr auto queryBudget = std::chrono::milliseconds(2);
constexpr auto nextQueryGrace = std::chrono::microseconds(300);
std::mutex queueMutex;
std::condition_variable queued;
std::deque<std::shared_ptr<Task>> queue;
std::deque<std::function<bool()>> deferred;
thread_local bool gameThread = false;
bool Execute(const std::shared_ptr<Task>& task) {
    {
        std::lock_guard lock(task->mutex);
        if (task->cancelled) return false;
        task->started = true;
    }
    task->action();
    { std::lock_guard lock(task->mutex); task->done = true; }
    task->cv.notify_one();
    return true;
}
}
void Service() {
    gameThread = true;
    std::deque<std::shared_ptr<Task>> pending;
    { std::lock_guard lock(queueMutex); pending.swap(queue); }
    const auto deadline = Clock::now() + queryBudget;
    bool servedQuery = false;
    for (const auto& task : pending) {
        if (Execute(task) && task->query) servedQuery = true;
    }
    while (servedQuery && Clock::now() < deadline) {
        std::unique_lock lock(queueMutex);
        const auto until = std::min(deadline, Clock::now() + nextQueryGrace);
        if (!queued.wait_until(lock, until, [] { return !queue.empty(); })) break;
        // Keep writes on their normal pulse and preserve FIFO order.
        if (!queue.front()->query || Clock::now() >= deadline) break;
        auto task = std::move(queue.front());
        queue.pop_front();
        lock.unlock();
        Execute(task);
    }
    std::deque<std::function<bool()>> cleanup;
    { std::lock_guard lock(queueMutex); cleanup.swap(deferred); }
    for(auto& action:cleanup) {
        if(!action()) { std::lock_guard lock(queueMutex);deferred.push_back(std::move(action)); }
    }
}
void Defer(std::function<bool()> cleanup) {
    std::lock_guard lock(queueMutex);deferred.push_back(std::move(cleanup));
}
static bool Submit(std::function<void()> action, bool query) {
    if (gameThread) { action(); return true; }
    auto task = std::make_shared<Task>();
    task->action = std::move(action);
    task->query = query;
    {
        std::lock_guard lock(queueMutex);
        if (queue.size() >= 32) return false;
        queue.push_back(task);
    }
    queued.notify_one();
    std::unique_lock lock(task->mutex);
    if (!task->cv.wait_for(lock, std::chrono::milliseconds(200), [&] { return task->done; })) {
        if (!task->started) { task->cancelled = true; return false; }
        task->cv.wait(lock, [&] { return task->done; });
    }
    return true;
}
bool Run(std::function<void()> action) {
    return Submit(std::move(action), false);
}
bool RunQuery(std::function<void()> action) {
    return Submit(std::move(action), true);
}
}
