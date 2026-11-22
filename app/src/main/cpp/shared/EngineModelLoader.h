#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace WorldRPG::Script {

// Returns a validated RenderWare chunk including its header.
std::span<const std::uint8_t> FindDffClump(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> ReadDffClump(const char* filepath);
std::span<const std::uint8_t> FindTxdDictionary(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> ReadTxdDictionary(const char* filepath);

struct DffSource {
    std::string path;
    std::vector<std::uint8_t> bytes;
};

void* NativeLoadTXD(std::span<const std::uint8_t> bytes, bool filtering);
bool NativeImportTXD(std::uint16_t model, std::uint64_t asset, void* dictionary);
bool NativeReplaceDFF(std::uint16_t model, std::uint64_t asset, const DffSource& source, bool alpha);
bool NativeRestoreDFF(std::uint16_t model);
void NativeReleaseModelAsset(std::uint64_t asset, void* dictionary);

} // namespace WorldRPG::Script
