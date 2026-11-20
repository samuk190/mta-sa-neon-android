#pragma once

#include <cstdint>
#include <vector>

namespace WorldRPG::Script {

// Returns the first clump, including its header, in a bounded stream buffer.
std::vector<std::uint8_t> ReadDffClump(const char* filepath);
void* NativeLoadDFF(const char* filepath);
void* NativeLoadTXD(const char* filepath);
void NativeDestroyClump(void* clump);
void NativeDestroyTXD(void* txd);

} // namespace WorldRPG::Script
