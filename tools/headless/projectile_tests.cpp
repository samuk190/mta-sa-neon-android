#include "gtasa/GameTasks.h"
#include "gtasa/ProjectileEngine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    std::atomic<int> changes{0};
    check(!WorldRPG::GameTasks::Run([&] { ++changes; }), "unserviced request times out");
    std::thread game([&] { WorldRPG::GameTasks::Service(); });
    game.join();
    check(changes == 0, "cancelled mutation never runs later");
    std::thread::id executed;
    bool noNative = false;
    std::thread running([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        WorldRPG::GameTasks::Service();
    });
    check(WorldRPG::GameTasks::Run([&] {
        executed = std::this_thread::get_id();
        ++changes;
        WorldRPG::GameTasks::Run([&] { ++changes; }); // Reentrant creation must not wait on its own game thread.
        WorldRPG::Projectiles::Pump(+[](uint32_t) -> void* { return nullptr; });
        noNative = WorldRPG::Projectiles::Create({}) == 0;
    }), "serviced request completes");
    check(executed == running.get_id(), "mutation ran on the game thread");
    running.join();
    check(changes == 2, "nested work executes once");
    check(noNative, "no fake projectile without a native engine");
    std::atomic<bool> finished{false};
    int pulse = 0, readPulses[12]{}, mutationPulse = 0;
    std::thread gameLoop([&] {
        while (!finished.load()) {
            ++pulse;
            WorldRPG::GameTasks::Service();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });
    for (int& readPulse : readPulses) {
        check(WorldRPG::GameTasks::RunQuery([&] { readPulse = pulse; }), "traffic read completes");
    }
    check(WorldRPG::GameTasks::Run([&] { mutationPulse = pulse; }), "queued write completes");
    finished.store(true);
    gameLoop.join();
    bool sharedPulse = false;
    for (unsigned i = 1; i < 12; ++i) sharedPulse |= readPulses[i] == readPulses[i - 1];
    check(sharedPulse, "consecutive traffic reads share a game pulse");
    check(mutationPulse > readPulses[11], "a following mutation waits for its normal pulse");
    WorldRPG::Projectiles::Snapshot out;
    check(!WorldRPG::Projectiles::Copy(0x62000001, out), "failed native creation has no live element");
    check(!WorldRPG::Projectiles::Release(0x62000001), "unknown handle cannot remove native objects");
    std::printf("neon_projectile_tests: %d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
