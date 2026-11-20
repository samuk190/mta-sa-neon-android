#include "GameTasks.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
namespace WorldRPG::GameTasks {
namespace {
struct Task {
    std::function<void()> action;
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false, done = false, cancelled = false;
};
std::mutex queueMutex;
std::deque<std::shared_ptr<Task>> queue;
std::deque<std::function<bool()>> deferred;
thread_local bool gameThread = false;
}
void Service() {
    gameThread = true;
    std::deque<std::shared_ptr<Task>> pending;
    { std::lock_guard lock(queueMutex); pending.swap(queue); }
    for (auto& task : pending) {
        {
            std::lock_guard lock(task->mutex);
            if (task->cancelled) continue;
            task->started = true;
        }
        task->action();
        { std::lock_guard lock(task->mutex); task->done = true; }
        task->cv.notify_one();
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
bool Run(std::function<void()> action) {
    if (gameThread) { action(); return true; }
    auto task = std::make_shared<Task>();
    task->action = std::move(action);
    {
        std::lock_guard lock(queueMutex);
        if (queue.size() >= 32) return false;
        queue.push_back(task);
    }
    std::unique_lock lock(task->mutex);
    if (!task->cv.wait_for(lock, std::chrono::milliseconds(200), [&] { return task->done; })) {
        if (!task->started) { task->cancelled = true; return false; }
        task->cv.wait(lock, [&] { return task->done; });
    }
    return true;
}
}
