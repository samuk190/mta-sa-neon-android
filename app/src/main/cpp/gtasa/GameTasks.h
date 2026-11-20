#pragma once
#include <functional>
namespace WorldRPG::GameTasks {
// Service exclusively from the game hook. Run cancels work that has not started
// after 200 ms; started mutations finish before their caller can return.
void Service();
bool Run(std::function<void()> task);
// Cleanup survives a caller timeout. False retries on the next game pulse.
// Callers coalesce requests and retain ownership of captured data.
void Defer(std::function<bool()> cleanup);
}
