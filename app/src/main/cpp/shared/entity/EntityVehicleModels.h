#pragma once

// Vehicle model tables used by the ENTITY_ADD vehicle body (EntityAddVehicle.cpp).
// Source: mta_neon_source Client/mods/deathmatch/logic/CClientVehicleManager.cpp
//   g_ulVehicleAttributes (table below, copied line by line), IsTrainModel,
//   HasDoors (switch list) and HasDamageModel. GTA model classes are
//   reproduced from the stock GTA SA model ids.
//
// Limits: only standard models 400-611 are classified. The reference resolves
// custom model ids to a parent id through the engine model info; this layer has no
// such lookup, so custom ids (> 611) report false for every predicate.

#include <cstdint>

namespace WorldRPG::Entity::VehicleModels {

constexpr uint8_t kAttrTurret = 0x01;
constexpr uint8_t kAttrSirens = 0x02;
constexpr uint8_t kAttrLandingGears = 0x04;
constexpr uint8_t kAttrAdjustableProperty = 0x08;
constexpr uint8_t kAttrSmokeTrail = 0x10;
constexpr uint8_t kAttrTaxiLights = 0x20;
constexpr uint8_t kAttrSearchLight = 0x40;

// Indexed by (model - 400). Values from CClientVehicleManager.cpp g_ulVehicleAttributes.
constexpr uint8_t kVehicleAttributes[212] = {
    0, 0, 0, 0, 0, 0, 8, 3, 0, 0, 0, 0, 0,  0,  0, 0, 2, 0,  0, 0, 32, 0, 0,  2, 0,  // 400-424
    0, 0, 2, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0,  32, 0, 0, 0, 0,  8, 0, 0,  0, 0,  0, 0,  // 425-449
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0, 0, 0, 0,  0, 0, 0,  0, 0,  0, 0,  // 450-474
    0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0,  0,  0, 2, 0, 0,  0, 0, 0,  0, 64, 0, 0,  // 475-499
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 16, 0, 0, 0, 0,  0, 4, 12, 0, 0,  2, 8,  // 500-524
    8, 0, 0, 2, 0, 8, 8, 0, 0, 0, 0, 0, 0,  0,  0, 0, 0, 0,  0, 2, 0,  0, 0,  0, 0,  // 525-549
    0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0, 0, 0, 0,  0, 0, 0,  0, 0,  0, 0,  // 550-574
    0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0, 0, 0, 12, 0, 0, 0,  2, 2,  2, 2,  // 575-599
    0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

constexpr bool IsStandardModel(uint32_t model) { return model >= 400 && model <= 611; }

constexpr uint8_t Attributes(uint32_t model) {
    return IsStandardModel(model) ? kVehicleAttributes[model - 400] : 0;
}

constexpr bool HasTurret(uint32_t model) { return (Attributes(model) & kAttrTurret) != 0; }
constexpr bool HasSirens(uint32_t model) { return (Attributes(model) & kAttrSirens) != 0; }
constexpr bool HasLandingGears(uint32_t model) { return (Attributes(model) & kAttrLandingGears) != 0; }
constexpr bool HasAdjustableProperty(uint32_t model) { return (Attributes(model) & kAttrAdjustableProperty) != 0; }
constexpr bool HasTaxiLight(uint32_t model) { return (Attributes(model) & kAttrTaxiLights) != 0; }

// CClientVehicleManager::IsTrainModel
constexpr bool IsTrainModel(uint32_t model) {
    return model == 449 || model == 537 || model == 538 || model == 569 || model == 590 || model == 570;
}

// Models whose GTA class has no damage model (boats, bikes, BMX, trains). HasDamageModel is false for them.
constexpr bool IsNoDamageClass(uint32_t model) {
    switch (model) {
        // boats
        case 430: case 446: case 452: case 453: case 454: case 472: case 473: case 484: case 493: case 595:
        // bikes and BMX
        case 448: case 461: case 462: case 463: case 468: case 481: case 509: case 510: case 521: case 522: case 523: case 581: case 586:
        // trains
        case 449: case 537: case 538: case 569: case 570: case 590:
            return true;
        default:
            return false;
    }
}

// Damage-model vehicles excluded from doors by CClientVehicleManager::HasDoors.
constexpr bool IsDoorlessModel(uint32_t model) {
    switch (model) {
        case 424: case 441: case 457: case 465: case 485: case 486: case 501: case 504: case 530:
        case 531: case 564: case 568: case 571: case 572: case 594:
            return true;
        default:
            return false;
    }
}

// CClientVehicleManager::HasDoors
constexpr bool HasDoors(uint32_t model) {
    return IsStandardModel(model) && !IsNoDamageClass(model) && !IsDoorlessModel(model);
}

} // namespace WorldRPG::Entity::VehicleModels
