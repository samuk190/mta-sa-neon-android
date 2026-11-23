#pragma once

#include <cstdint>
#include <string_view>

namespace WorldRPG::Models {

// CModelNames.h: the only id GetModelID uses when the name is absent.
constexpr std::uint32_t kInvalidModelId = 0xFFFFu;

// CModelNames::GetModelID (CModelNames.cpp:29275-29281). Case folding is the
// desktop A-Z table. Clothes names are a separate map and are not searched.
std::uint32_t ModelIdFromName(std::string_view name);

// CModelNames::GetModelName (CModelNames.cpp:29299-29305). Empty when the id
// has no stock name. The pointer is stable for the process lifetime.
const char* ModelNameFromId(std::uint32_t id);

}  // namespace WorldRPG::Models
