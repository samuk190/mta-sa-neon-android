#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {

namespace {

// CVehicleNames.cpp:23-234. szName, then the compatibility alias (szName_replaced) when the desktop
// entry has one. Empty alias is nullptr. Indexed by model - 400.
struct VehicleName {
    const char* name;
    const char* alias;
};

constexpr VehicleName kNames[212] = {
    {"Landstalker", nullptr}, // 400
    {"Bravura", nullptr}, // 401
    {"Buffalo", nullptr}, // 402
    {"Linerunner", nullptr}, // 403
    {"Perennial", nullptr}, // 404
    {"Sentinel", nullptr}, // 405
    {"Dumper", nullptr}, // 406
    {"Fire Truck", nullptr}, // 407
    {"Trashmaster", nullptr}, // 408
    {"Stretch", nullptr}, // 409
    {"Manana", nullptr}, // 410
    {"Infernus", nullptr}, // 411
    {"Voodoo", nullptr}, // 412
    {"Pony", nullptr}, // 413
    {"Mule", nullptr}, // 414
    {"Cheetah", nullptr}, // 415
    {"Ambulance", nullptr}, // 416
    {"Leviathan", nullptr}, // 417
    {"Moonbeam", nullptr}, // 418
    {"Esperanto", nullptr}, // 419
    {"Taxi", nullptr}, // 420
    {"Washington", nullptr}, // 421
    {"Bobcat", nullptr}, // 422
    {"Mr. Whoopee", nullptr}, // 423
    {"BF Injection", nullptr}, // 424
    {"Hunter", nullptr}, // 425
    {"Premier", nullptr}, // 426
    {"Enforcer", nullptr}, // 427
    {"Securicar", nullptr}, // 428
    {"Banshee", nullptr}, // 429
    {"Predator", nullptr}, // 430
    {"Bus", nullptr}, // 431
    {"Rhino", nullptr}, // 432
    {"Barracks", nullptr}, // 433
    {"Hotknife", nullptr}, // 434
    {"Trailer 1", nullptr}, // 435
    {"Previon", nullptr}, // 436
    {"Coach", nullptr}, // 437
    {"Cabbie", nullptr}, // 438
    {"Stallion", nullptr}, // 439
    {"Rumpo", nullptr}, // 440
    {"RC Bandit", nullptr}, // 441
    {"Romero", nullptr}, // 442
    {"Packer", nullptr}, // 443
    {"Monster 1", "Monster"}, // 444
    {"Admiral", nullptr}, // 445
    {"Squalo", nullptr}, // 446
    {"Seasparrow", nullptr}, // 447
    {"Pizzaboy", nullptr}, // 448
    {"Tram", nullptr}, // 449
    {"Trailer 2", nullptr}, // 450
    {"Turismo", nullptr}, // 451
    {"Speeder", nullptr}, // 452
    {"Reefer", nullptr}, // 453
    {"Tropic", nullptr}, // 454
    {"Flatbed", nullptr}, // 455
    {"Yankee", nullptr}, // 456
    {"Caddy", nullptr}, // 457
    {"Solair", nullptr}, // 458
    {"Berkley's RC Van", nullptr}, // 459
    {"Skimmer", nullptr}, // 460
    {"PCJ-600", nullptr}, // 461
    {"Faggio", nullptr}, // 462
    {"Freeway", nullptr}, // 463
    {"RC Baron", nullptr}, // 464
    {"RC Raider", nullptr}, // 465
    {"Glendale", nullptr}, // 466
    {"Oceanic", nullptr}, // 467
    {"Sanchez", nullptr}, // 468
    {"Sparrow", nullptr}, // 469
    {"Patriot", nullptr}, // 470
    {"Quadbike", nullptr}, // 471
    {"Coastguard", nullptr}, // 472
    {"Dinghy", nullptr}, // 473
    {"Hermes", nullptr}, // 474
    {"Sabre", nullptr}, // 475
    {"Rustler", nullptr}, // 476
    {"ZR-350", nullptr}, // 477
    {"Walton", nullptr}, // 478
    {"Regina", nullptr}, // 479
    {"Comet", nullptr}, // 480
    {"BMX", nullptr}, // 481
    {"Burrito", nullptr}, // 482
    {"Camper", nullptr}, // 483
    {"Marquis", nullptr}, // 484
    {"Baggage", nullptr}, // 485
    {"Dozer", nullptr}, // 486
    {"Maverick", nullptr}, // 487
    {"News Chopper", nullptr}, // 488
    {"Rancher", nullptr}, // 489
    {"FBI Rancher", nullptr}, // 490
    {"Virgo", nullptr}, // 491
    {"Greenwood", nullptr}, // 492
    {"Jetmax", nullptr}, // 493
    {"Hotring Racer", nullptr}, // 494
    {"Sandking", nullptr}, // 495
    {"Blista Compact", nullptr}, // 496
    {"Police Maverick", nullptr}, // 497
    {"Boxville", nullptr}, // 498
    {"Benson", nullptr}, // 499
    {"Mesa", nullptr}, // 500
    {"RC Goblin", nullptr}, // 501
    {"Hotring Racer 2", nullptr}, // 502
    {"Hotring Racer 3", nullptr}, // 503
    {"Bloodring Banger", nullptr}, // 504
    {"Rancher Lure", nullptr}, // 505
    {"Super GT", nullptr}, // 506
    {"Elegant", nullptr}, // 507
    {"Journey", nullptr}, // 508
    {"Bike", nullptr}, // 509
    {"Mountain Bike", nullptr}, // 510
    {"Beagle", nullptr}, // 511
    {"Cropduster", nullptr}, // 512
    {"Stuntplane", nullptr}, // 513
    {"Tanker", nullptr}, // 514
    {"Roadtrain", nullptr}, // 515
    {"Nebula", nullptr}, // 516
    {"Majestic", nullptr}, // 517
    {"Buccaneer", nullptr}, // 518
    {"Shamal", nullptr}, // 519
    {"Hydra", nullptr}, // 520
    {"FCR-900", nullptr}, // 521
    {"NRG-500", nullptr}, // 522
    {"HPV1000", nullptr}, // 523
    {"Cement Truck", nullptr}, // 524
    {"Towtruck", nullptr}, // 525
    {"Fortune", nullptr}, // 526
    {"Cadrona", nullptr}, // 527
    {"FBI Truck", nullptr}, // 528
    {"Willard", nullptr}, // 529
    {"Forklift", nullptr}, // 530
    {"Tractor", nullptr}, // 531
    {"Combine Harvester", nullptr}, // 532
    {"Feltzer", nullptr}, // 533
    {"Remington", nullptr}, // 534
    {"Slamvan", nullptr}, // 535
    {"Blade", nullptr}, // 536
    {"Freight", nullptr}, // 537
    {"Streak", nullptr}, // 538
    {"Vortex", nullptr}, // 539
    {"Vincent", nullptr}, // 540
    {"Bullet", nullptr}, // 541
    {"Clover", nullptr}, // 542
    {"Sadler", nullptr}, // 543
    {"Fire Truck Ladder", nullptr}, // 544
    {"Hustler", nullptr}, // 545
    {"Intruder", nullptr}, // 546
    {"Primo", nullptr}, // 547
    {"Cargobob", nullptr}, // 548
    {"Tampa", nullptr}, // 549
    {"Sunrise", nullptr}, // 550
    {"Merit", nullptr}, // 551
    {"Utility Van", nullptr}, // 552
    {"Nevada", nullptr}, // 553
    {"Yosemite", nullptr}, // 554
    {"Windsor", nullptr}, // 555
    {"Monster 2", nullptr}, // 556
    {"Monster 3", nullptr}, // 557
    {"Uranus", nullptr}, // 558
    {"Jester", nullptr}, // 559
    {"Sultan", nullptr}, // 560
    {"Stratum", nullptr}, // 561
    {"Elegy", nullptr}, // 562
    {"Raindance", nullptr}, // 563
    {"RC Tiger", nullptr}, // 564
    {"Flash", nullptr}, // 565
    {"Tahoma", nullptr}, // 566
    {"Savanna", nullptr}, // 567
    {"Bandito", nullptr}, // 568
    {"Freight Train Flatbed", nullptr}, // 569
    {"Streak Train Trailer", nullptr}, // 570
    {"Kart", nullptr}, // 571
    {"Mower", nullptr}, // 572
    {"Dune", nullptr}, // 573
    {"Sweeper", nullptr}, // 574
    {"Broadway", nullptr}, // 575
    {"Tornado", nullptr}, // 576
    {"AT-400", nullptr}, // 577
    {"DFT-30", nullptr}, // 578
    {"Huntley", nullptr}, // 579
    {"Stafford", nullptr}, // 580
    {"BF-400", nullptr}, // 581
    {"Newsvan", nullptr}, // 582
    {"Tug", nullptr}, // 583
    {"Trailer (Tanker Commando)", nullptr}, // 584
    {"Emperor", nullptr}, // 585
    {"Wayfarer", nullptr}, // 586
    {"Euros", nullptr}, // 587
    {"Hotdog", nullptr}, // 588
    {"Club", nullptr}, // 589
    {"Box Freight", nullptr}, // 590
    {"Trailer 3", nullptr}, // 591
    {"Andromada", nullptr}, // 592
    {"Dodo", nullptr}, // 593
    {"RC Cam", nullptr}, // 594
    {"Launch", nullptr}, // 595
    {"Police LS", "Police"}, // 596
    {"Police SF", nullptr}, // 597
    {"Police LV", nullptr}, // 598
    {"Police Ranger", "Ranger"}, // 599
    {"Picador", nullptr}, // 600
    {"S.W.A.T.", nullptr}, // 601
    {"Alpha", nullptr}, // 602
    {"Phoenix", nullptr}, // 603
    {"Glendale Damaged", nullptr}, // 604
    {"Sadler Damaged", "Sadler"}, // 605
    {"Baggage Trailer (covered)", nullptr}, // 606
    {"Baggage Trailer (Uncovered)", nullptr}, // 607
    {"Trailer (Stairs)", nullptr}, // 608
    {"Boxville Mission", nullptr}, // 609
    {"Farm Trailer", nullptr}, // 610
    {"Street Clean Trailer", nullptr}, // 611
};

// CClientVehicleManager.cpp:19-32 (g_ucMaxPassengers). 255 means the model has no published passenger count.
constexpr unsigned char kMaxPassengers[212] = {
    3, 1, 1, 1, 3, 3, 0, 1, 1, 3, 1, 1, 1, 3, 1, 1,
    3, 1, 3, 1, 3, 3, 1, 1, 1, 0, 3, 3, 3, 1, 0, 8,
    0, 1, 1, 255, 1, 8, 3, 1, 3, 0, 1, 1, 1, 3, 0, 1,
    0, 1, 255, 1, 0, 0, 0, 1, 1, 1, 3, 3, 1, 1, 1, 1,
    1, 1, 3, 3, 1, 1, 3, 1, 0, 0, 1, 1, 0, 1, 1, 3,
    1, 0, 3, 3, 0, 0, 0, 3, 1, 1, 3, 1, 3, 0, 1, 1,
    1, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 0, 0, 1,
    0, 0, 1, 1, 3, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 3, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 3, 1, 1, 1,
    1, 1, 3, 3, 1, 1, 3, 3, 1, 0, 1, 1, 1, 1, 1, 1,
    3, 3, 1, 1, 0, 1, 3, 3, 0, 255, 1, 0, 0, 1, 0, 1,
    1, 1, 1, 3, 3, 1, 3, 0, 255, 3, 1, 1, 1, 1, 255, 255,
    1, 1, 1, 0, 3, 3, 3, 1, 1, 1, 1, 1, 3, 1, 255, 255,
    255, 3, 255, 255,
};

static_assert(sizeof(kNames) / sizeof(kNames[0]) == 212, "vehicle name table");
static_assert(sizeof(kMaxPassengers) == 212, "passenger table");

// CVehicleUpgrades.cpp:20-36. Index is the upgrade slot.
constexpr const char* kSlotNames[17] = {
    "Hood", "Vent", "Spoiler", "Sideskirt", "Front Bullbars", "Rear Bullbars", "Headlights", "Roof",
    "Nitro", "Hydraulics", "Stereo", "Unknown", "Wheels", "Exhaust", "Front Bumper", "Rear Bumper", "Misc",
};

// CServerModelDefinition.h. Ids at or above this are server-registry models, not GTA file ids.
constexpr uint32_t kServerModelIdMin = 42341;
constexpr uint32_t kServerModelIdMax = 65534;

constexpr bool IsStandardModel(int model) { return model >= 400 && model <= 611; }

// CVehicleNames.cpp:236-244. Index is ucVehicleTypes[model - 400].
constexpr const char* kVehicleTypes[10] = {
    "Automobile", "Plane", "Bike", "Helicopter", "Boat", "Train", "Trailer", "BMX", "Monster Truck", "Quad",
};
// CVehicleNames.cpp:239-244.
constexpr unsigned char kVehicleTypeIndex[212] = {
    0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0,
    0, 8, 0, 4, 3, 2, 5, 6, 0, 4, 4, 4, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 3, 0, 0, 2, 3, 0, 9, 4, 4, 0, 0, 1, 0, 0, 0, 0, 7, 0, 0, 4, 0,
    0, 3, 3, 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 7, 7, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1, 0, 0, 8, 8, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 5, 5, 0,
    0, 8, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 6, 0, 2, 0, 0, 0, 5, 6, 1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 0, 6, 6,
};
static_assert(sizeof(kVehicleTypeIndex) == 212, "vehicle type table");

// stricmp of the desktop name lookup. Names are ASCII.
bool SameName(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

// CVehicleNames::GetVehicleTypeName (CVehicleNames.cpp 288-301) for a stock model.
// A non-stock id has no CModelInfo parent on the phone, so the type name is empty.
const char* VehicleTypeOf(int model) {
    if (!IsStandardModel(model)) return "";
    const unsigned char index = kVehicleTypeIndex[model - 400];
    return index < 10 ? kVehicleTypes[index] : "";
}

// CVehicleNames::GetVehicleName (CVehicleNames.cpp:258-268). Never null: unknown models are "".
const char* VehicleNameOf(int model) {
    if (!IsStandardModel(model)) return "";
    return kNames[model - 400].name;
}

// CVehicleNames::GetVehicleModel (CVehicleNames.cpp:270-286). 0 means no match.
unsigned ModelFromName(const char* name) {
    if (!name || name[0] == '\0') return 0;
    for (unsigned i = 0; i < 212; ++i) {
        if (SameName(name, kNames[i].name) || (kNames[i].alias && SameName(name, kNames[i].alias)))
            return i + 400;
    }
    return 0;
}

// CVehicleUpgrades::GetSlotFromUpgrade (CVehicleUpgrades.cpp:452-555). First matching list wins.
bool Listed(unsigned short us, std::initializer_list<unsigned short> ids) {
    for (unsigned short id : ids) {
        if (us == id) return true;
    }
    return false;
}

bool SlotFromUpgrade(unsigned short us, unsigned char& ucSlot) {
    if (Listed(us, {1011, 1012, 1111, 1112, 1142, 1143, 1144, 1145})) { ucSlot = 0; return true; }
    if (Listed(us, {1004, 1005, 1142, 1143, 1144, 1145})) { ucSlot = 1; return true; }
    if (Listed(us, {1000, 1001, 1002, 1003, 1014, 1015, 1016, 1023, 1049, 1050, 1058, 1060, 1138, 1139, 1146, 1147, 1158, 1162, 1163, 1164})) { ucSlot = 2; return true; }
    if (Listed(us, {1007, 1017, 1026, 1027, 1030, 1031, 1036, 1039, 1040, 1041, 1042, 1047, 1048, 1051, 1052, 1056, 1057, 1062, 1063, 1069, 1070, 1071, 1072, 1090, 1093, 1094, 1095, 1099, 1101, 1102, 1106, 1107, 1108, 1118, 1119, 1120, 1121, 1122, 1124, 1133, 1134, 1137})) { ucSlot = 3; return true; }
    if (Listed(us, {1115, 1116})) { ucSlot = 4; return true; }
    if (Listed(us, {1109, 1110})) { ucSlot = 5; return true; }
    if (Listed(us, {1013, 1024})) { ucSlot = 6; return true; }
    if (Listed(us, {1006, 1032, 1033, 1035, 1038, 1053, 1054, 1055, 1061, 1067, 1068, 1088, 1091, 1103, 1128, 1130, 1131})) { ucSlot = 7; return true; }
    if (Listed(us, {1008, 1009, 1010})) { ucSlot = 8; return true; }
    if (Listed(us, {1087})) { ucSlot = 9; return true; }
    if (Listed(us, {1086})) { ucSlot = 10; return true; }
    if (Listed(us, {1025, 1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083, 1084, 1085, 1096, 1097, 1098})) { ucSlot = 12; return true; }
    if (Listed(us, {1018, 1019, 1020, 1021, 1022, 1028, 1029, 1034, 1037, 1043, 1044, 1045, 1046, 1059, 1064, 1065, 1066, 1089, 1092, 1104, 1105, 1113, 1114, 1126, 1127, 1129, 1132, 1135, 1136})) { ucSlot = 13; return true; }
    if (Listed(us, {1117, 1152, 1153, 1155, 1157, 1160, 1165, 1166, 1169, 1170, 1171, 1172, 1173, 1174, 1175, 1179, 1181, 1182, 1185, 1188, 1189, 1190, 1191})) { ucSlot = 14; return true; }
    if (Listed(us, {1140, 1141, 1148, 1149, 1150, 1151, 1154, 1156, 1159, 1161, 1167, 1168, 1176, 1177, 1178, 1180, 1183, 1184, 1186, 1187, 1192, 1193})) { ucSlot = 15; return true; }
    if (Listed(us, {1100, 1123, 1125})) { ucSlot = 16; return true; }
    return false;
}

// CVehicleUpgrades::GetSlotName (CVehicleUpgrades.cpp:764-770).
const char* SlotName(unsigned char slot) {
    return slot < 17 ? kSlotNames[slot] : "";
}

const char* LuaTypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TLIGHTUSERDATA: return "lightuserdata";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TUSERDATA: return "userdata";
        case LUA_TTHREAD: return "thread";
        default: return "none";
    }
}

// Desktop CScriptDebugging::LogWarning prefix plus CScriptArgReader::GetFullErrorMessage.
void LogWarning(const char* text) { LUA_LOGI("WARNING: %s", text); }

void LogFull(const char* function, const char* category, const char* message) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s @ '%s' [%s]", category, function, message);
    LogWarning(buf);
}

// SetTypeError: "Expected <type> at argument N, got <lua type>" and the value when it is a string or number.
void LogTypeError(lua_State* L, const char* function, const char* expected, int index) {
    const int type = lua_type(L, index);
    const char* value = nullptr;
    if (type == LUA_TNUMBER || type == LUA_TSTRING) value = lua_tostring(L, index);
    char buf[512];
    if (value && value[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s '%.64s']",
                      function, expected, index, LuaTypeName(type), value);
    } else {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function,
                      expected, index, LuaTypeName(type));
    }
    LogWarning(buf);
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// Element tables {id, element = true} stand in for desktop userdata (see PushElement).
bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

// CScriptArgReader::ReadUserData<CClientVehicle>. False logs the type error.
bool ReadVehicleModel(lua_State* L, const char* function, int& model) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0) {
        LogTypeError(L, function, "vehicle", 1);
        return false;
    }
    model = ServerWorld::ModelOf(ElementIdOf(L, 1));
    return true;
}

// CScriptArgReader::ReadString. Numbers are accepted and converted, as on desktop.
bool ReadStringArg(lua_State* L, const char* function, std::string& out) {
    const int type = lua_type(L, 1);
    if (type == LUA_TSTRING || type == LUA_TNUMBER) {
        size_t length = 0;
        const char* text = lua_tolstring(L, 1, &length);
        out.assign(text ? text : "", text ? length : 0);
        return true;
    }
    LogTypeError(L, function, "string", 1);
    return false;
}

// CScriptArgReader::ReadNumber<lua_Number> then ReadNumberBounded(0, SERVER_MODEL_ID_MAX).
bool ReadModelArg(lua_State* L, const char* function, uint32_t& model) {
    const int type = lua_type(L, 1);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogTypeError(L, function, "number", 1);
        return false;
    }
    if (!lua_isnumber(L, 1)) {
        LogFull(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, 1);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (!std::isfinite(number) || number < 0.0 || number > static_cast<lua_Number>(kServerModelIdMax)) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    model = static_cast<uint32_t>(number);
    return true;
}

// CScriptArgReader::ReadNumber<unsigned short>. Narrows through int64, then keeps the low 16 bits.
bool ReadUnsignedShortArg(lua_State* L, const char* function, int index, unsigned short& value) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogTypeError(L, function, "number", index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogFull(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (number < -static_cast<lua_Number>(FLT_EPSILON)) {
        LogFull(function, "Bad argument", "Expected positive value, got negative");
        return false;
    }
    if (!std::isfinite(number) || number > static_cast<lua_Number>(std::numeric_limits<int64_t>::max())) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    value = static_cast<unsigned short>(static_cast<int64_t>(number));
    return true;
}

// CClientModelManager::ResolveModelID (CClientModelManager.cpp:326-368) with an empty server-model
// map and no native-world pack. Stock GTA ids pass through. Registry ids fail ("invalid model ID").
// CGameSA::IsNativeWorldModelIdReserved is not called: nothing on the phone reserves a model id.
bool ResolveStockModel(uint32_t supplied, uint16_t& runtime) {
    if (supplied >= kServerModelIdMin) return false;
    runtime = static_cast<uint16_t>(supplied);
    return true;
}

// Parent remap (CLuaVehicleDefs.cpp GetVehicleName 1369-1372) reads g_pGame->GetModelInfo.
// That hook is not wired. Stock models have parent 0, so the name is the model's own.
// A non-stock model is left unchanged, which is the null model-info result.
void NoteMissingParent(const char* function, int model) {
    if (IsStandardModel(model)) return;
    LUA_LOGI("DIAG %s: CModelInfo::GetParentID not wired, model %d", function, model);
}

// getVehicleMaxPassengers(vehicle or model). CLuaVehicleDefs.cpp:652-690.
// Passenger counts: CClientVehicleManager::GetMaxPassengerCount (CClientVehicleManager.cpp:497-509).
int GetVehicleMaxPassengers(lua_State* L) {
    int model = -1;
    if (IsElementArg(L, 1)) {
        if (!ReadVehicleModel(L, "getVehicleMaxPassengers", model)) return Fail(L);
    } else {
        uint32_t supplied = 0;
        if (!ReadModelArg(L, "getVehicleMaxPassengers", supplied)) return Fail(L);
        uint16_t runtime = 0;
        if (!ResolveStockModel(supplied, runtime)) {
            LogFull("getVehicleMaxPassengers", "Bad usage", "invalid model ID");
            return Fail(L);
        }
        model = runtime;
    }

    // CClientVehicleManager::IsValidModel (CClientVehicleManager.cpp:442-453) asks CModelInfo::IsVehicle.
    // Every stock id 400-611 is a vehicle, so that check passes. A native id outside that range is
    // not a vehicle. A stored custom id has no model info on the phone, so it fails the same way.
    if (!IsStandardModel(model)) {
        if (model < 0 || model > 611)
            LUA_LOGI("DIAG getVehicleMaxPassengers: CModelInfo::IsVehicle not wired, model %d", model);
        return Fail(L);
    }
    const unsigned char count = kMaxPassengers[model - 400];
    if (count == 0xFF) return Fail(L);
    // Desktop pushes with lua_pushnumber. This VM is Lua 5.3, where a whole count is an integer.
    lua_pushinteger(L, count);
    return 1;
}

// getVehicleModelFromName(name). CLuaVehicleDefs.cpp:604-626 and
// CStaticFunctionDefinitions::GetVehicleModelFromName (CStaticFunctionDefinitions.cpp:2855-2867).
int GetVehicleModelFromName(lua_State* L) {
    std::string name;
    if (!ReadStringArg(L, "getVehicleModelFromName", name)) return Fail(L);
    const unsigned model = ModelFromName(name.c_str());
    if (model == 0) return Fail(L);
    // Desktop pushes with lua_pushnumber. This VM is Lua 5.3, where a model id is an integer.
    lua_pushinteger(L, model);
    return 1;
}

// getVehicleName(vehicle). CLuaVehicleDefs.cpp:1360-1386.
int GetVehicleName(lua_State* L) {
    int model = -1;
    if (!ReadVehicleModel(L, "getVehicleName", model)) return Fail(L);
    NoteMissingParent("getVehicleName", model);
    lua_pushstring(L, VehicleNameOf(model));
    return 1;
}

// getVehicleNameFromModel(model). CLuaVehicleDefs.cpp:1607-1642 and
// CStaticFunctionDefinitions::GetVehicleNameFromModel (CStaticFunctionDefinitions.cpp:2887-2891).
int GetVehicleNameFromModel(lua_State* L) {
    uint32_t supplied = 0;
    if (!ReadModelArg(L, "getVehicleNameFromModel", supplied)) return Fail(L);
    uint16_t runtime = 0;
    if (!ResolveStockModel(supplied, runtime)) {
        LogFull("getVehicleNameFromModel", "Bad usage", "invalid model ID");
        return Fail(L);
    }
    // Non-stock native ids are not vehicles, so the CModelInfo parent remap (lines 1620-1629)
    // does not change the model. Registry ids already failed ResolveStockModel above.
    const char* name = VehicleNameOf(runtime);
    if (name[0] == '\0') return Fail(L);
    lua_pushstring(L, name);
    return 1;
}

// getVehicleType(vehicle or model). CLuaVehicleDefs.cpp:455-487 and
// CVehicleNames::GetVehicleTypeName (CVehicleNames.cpp 288-301).
int GetVehicleType(lua_State* L) {
    int model = -1;
    if (IsElementArg(L, 1)) {
        if (!ReadVehicleModel(L, "getVehicleType", model)) return Fail(L);
    } else {
        uint32_t supplied = 0;
        if (!ReadModelArg(L, "getVehicleType", supplied)) return Fail(L);
        uint16_t runtime = 0;
        if (!ResolveStockModel(supplied, runtime)) {
            LogFull("getVehicleType", "Bad usage", "invalid model ID");
            return Fail(L);
        }
        model = runtime;
    }
    NoteMissingParent("getVehicleType", model);
    lua_pushstring(L, VehicleTypeOf(model));
    return 1;
}

// getVehicleUpgradeSlotName(slot or upgrade). CLuaVehicleDefs.cpp:953-988 and
// CStaticFunctionDefinitions::GetVehicleUpgradeSlotName (CStaticFunctionDefinitions.cpp:2869-2885).
int GetVehicleUpgradeSlotName(lua_State* L) {
    unsigned short number = 0;
    if (!ReadUnsignedShortArg(L, "getVehicleUpgradeSlotName", 1, number)) return Fail(L);
    if (number < 17) {
        lua_pushstring(L, SlotName(static_cast<unsigned char>(number)));
        return 1;
    }
    if (number >= 1000 && number <= 1193) {
        unsigned char slot = 0;
        if (SlotFromUpgrade(number, slot)) {
            const char* name = SlotName(slot);
            if (name[0] != '\0') {
                lua_pushstring(L, name);
                return 1;
            }
        }
        return Fail(L);
    }
    // CScriptDebugging::LogBadPointer (CScriptDebugging.cpp:194-199), argument 1.
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Bad 'slot/upgrade' pointer @ 'getVehicleUpgradeSlotName'(1)");
    LogWarning(buf);
    return Fail(L);
}

// Desktop CClientVehicle::GetOccupant (CClientVehicle.cpp:1884-1897). The local player is the
// driver of the car the game reports; other peds come from the seat table (see PedOccupiedVehicle
// in LuaPedApi.cpp). A ped that left the session is not returned.
uint32_t VehicleOccupant(uint32_t vehicleId, unsigned seat) {
    if (seat > 8) return 0;
    uint32_t ped = 0;
    if (seat == 0 && WorldRPG::Game::LocalVehicleId() == vehicleId) {
        ped = ServerWorld::LocalPlayer();
    } else if (!WorldRPG::RemoteVehicle::FindSeatOccupant(vehicleId, static_cast<uint8_t>(seat), ped)) {
        return 0;
    }
    if (ped == 0 || ped == 0xFFFFFFFFu) return 0;
    const char* kind = ServerWorld::ElementKindName(ped);
    if (std::strcmp(kind, "player") != 0 && std::strcmp(kind, "ped") != 0) return 0;
    return ped;
}

// getVehicleOccupant(vehicle [, seat = 0]). CLuaVehicleDefs.cpp:692-714.
int GetVehicleOccupant(lua_State* L) {
    int model = 0;
    if (!ReadVehicleModel(L, "getVehicleOccupant", model)) return Fail(L);
    // ReadIfNextIsNumber: a number or a numeric string, otherwise seat 0.
    unsigned seat = 0;
    if (lua_isnumber(L, 2)) {
        const lua_Number raw = lua_tonumber(L, 2);
        if (!(raw >= 0 && raw <= 8)) return Fail(L);
        seat = static_cast<unsigned>(raw);
    }
    const uint32_t ped = VehicleOccupant(ElementIdOf(L, 1), seat);
    if (ped == 0) return Fail(L);
    PushElement(L, ped);
    return 1;
}

// getVehicleOccupants(vehicle). CLuaVehicleDefs.cpp:716-755: a table keyed by seat, or false
// when the model has no seats.
int GetVehicleOccupants(lua_State* L) {
    int model = 0;
    if (!ReadVehicleModel(L, "getVehicleOccupants", model)) return Fail(L);
    const unsigned char maxPassengers = IsStandardModel(model) ? kMaxPassengers[model - 400] : 255;
    if (maxPassengers == 255) return Fail(L);
    const uint32_t vehicle = ElementIdOf(L, 1);
    lua_newtable(L);
    for (unsigned seat = 0; seat <= maxPassengers; ++seat) {
        const uint32_t ped = VehicleOccupant(vehicle, seat);
        if (ped == 0) continue;
        lua_pushinteger(L, seat);
        PushElement(L, ped);
        lua_settable(L, -3);
    }
    return 1;
}

// getVehicleController(vehicle). CLuaVehicleDefs.cpp:757-777 and CClientVehicle::GetControllingPlayer
// (CClientVehicle.cpp:1899-1930): the driver. The towing-vehicle and train-chain walks are not
// ported (no tow link or chain is stored), so a towed vehicle reports its own driver only.
int GetVehicleController(lua_State* L) {
    int model = 0;
    if (!ReadVehicleModel(L, "getVehicleController", model)) return Fail(L);
    const uint32_t ped = VehicleOccupant(ElementIdOf(L, 1), 0);
    if (ped == 0) return Fail(L);
    PushElement(L, ped);
    return 1;
}

// CClientVehicle fields that exist before the GTA vehicle is created
// (CClientVehicle.cpp:150-190). The native body write stays DIAG.
struct VehicleFlags {
    bool locked = false;
    int doorLock = 1;  // DOOR_LOCK_UNLOCKED
    bool engine = false;
    unsigned char overrideLights = 0;
    unsigned char paintjob = 3;
    std::string plate;
    bool damageProof = false;
    bool fuelExplodable = true;
    bool frozen = false;
    bool landingGearDown = true;
    bool sirensOn = false;
    bool taxiLight = false;
    bool tyresCanBurst = true;
    float dirt = 0.f;
    bool damageReady = false;
    unsigned char doors[6] = {};
    unsigned char wheels[4] = {};
    unsigned char panels[7] = {};
    unsigned char lights[4] = {};
    bool blown = false;
    bool onGround = false;
    float turnX = 0.f;
    float turnY = 0.f;
    float turnZ = 0.f;
    float gravityX = 0.f;
    float gravityY = 0.f;
    float gravityZ = -1.f;
    unsigned char headR = 255;
    unsigned char headG = 255;
    unsigned char headB = 255;
    float heliRotor = 0.f;
    bool derailed = false;
    bool derailable = true;
    bool trainDirection = false;
    float trainSpeed = 0.f;
    float trainPosition = -1.f;
    bool chainEngine = false;
    bool colorRgb = false;
    unsigned char palette[4] = {};
    unsigned char rgb[12] = {};
    unsigned char variant = 255;
    unsigned char variant2 = 255;
    float wheelScale = 1.f;
    bool smokeTrail = false;
    float nitroLevel = 1.f;
    char nitroCount = 0;
    bool nitroInstalled = false;
    unsigned short adjustable = 0;
    float turretH = 0.f;
    float turretV = 0.f;
    float doorRatio[6] = {};
    bool heliBlades = true;
    bool windows[7] = {};
    float planeRotor = 0.f;
    bool parkingBrake = false;
    bool doorsUndamageable = false;
    bool loadCollision = false;
    uint32_t towed = 0;
    uint32_t towedBy = 0;
    unsigned short upgrades[17] = {};
    unsigned char sirenCount = 0;
    unsigned char sirenType = 0;
    bool siren360 = false;
    bool sirenLos = true;
    bool sirenRandom = true;
    bool sirenSilent = false;
    bool rotorState = true;
    struct SirenPoint {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        unsigned char a = 255;
        unsigned minAlpha = 0;
    };
    SirenPoint sirenPoints[8] = {};
};

std::mutex g_vehicleFlagsLock;
std::unordered_map<uint32_t, VehicleFlags> g_vehicleFlags;

void NoteNativeVehicleGap() {
    static bool noted = false;
    if (noted) return;
    noted = true;
    LUA_LOGI("DIAG vehicle script state: CVehicle is not written; the client element fields are stored");
}

VehicleFlags CopyFlags(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_vehicleFlagsLock);
    return g_vehicleFlags[id];
}

void UpdateFlags(uint32_t id, const VehicleFlags& flags) {
    std::lock_guard<std::mutex> lock(g_vehicleFlagsLock);
    g_vehicleFlags[id] = flags;
}

bool VehicleFrozenOf(uint32_t id) {
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return false;
    std::lock_guard<std::mutex> lock(g_vehicleFlagsLock);
    const auto found = g_vehicleFlags.find(id);
    return found != g_vehicleFlags.end() && found->second.frozen;
}

bool HasModel(int model, std::initializer_list<int> ids) {
    for (int id : ids) if (model == id) return true;
    return false;
}

// CClientVehicleManager::HasSirens, gentable_vehattr.cpp:40-54.
bool ModelHasSirens(int model) {
    return HasModel(model, {523, 407, 544, 416, 427, 490, 528, 596, 597, 598, 599, 601, 423, 432, 433});
}

// CClientVehicleManager::HasLandingGears, gentable_vehattr.cpp:56-61.
bool ModelHasLandingGears(int model) {
    return HasModel(model, {592, 577, 553, 476, 519, 520});
}

// CClientVehicle::DoCheckHasLandingGear (CClientVehicle.cpp:2662-2667).
bool ModelHasLandingGearBody(int model) {
    return HasModel(model, {592, 577, 553, 476, 519, 520, 513});
}

int FailNil(lua_State* L) {
    lua_pushnil(L);
    return 1;
}

bool ReadElement(lua_State* L, const char* function, uint32_t& id) {
    if (!IsElementArg(L, 1)) {
        LogTypeError(L, function, "element", 1);
        return false;
    }
    id = ElementIdOf(L, 1);
    return true;
}

bool ReadBoolArg(lua_State* L, const char* function, int index, bool& out) {
    if (lua_type(L, index) != LUA_TBOOLEAN) {
        LogTypeError(L, function, "bool", index);
        return false;
    }
    out = lua_toboolean(L, index) != 0;
    return true;
}

bool ReadByteArg(lua_State* L, const char* function, int index, unsigned char& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogTypeError(L, function, "number", index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogFull(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (number < -static_cast<lua_Number>(FLT_EPSILON)) {
        LogFull(function, "Bad argument", "Expected positive value, got negative");
        return false;
    }
    if (!std::isfinite(number) || number > static_cast<lua_Number>(std::numeric_limits<int64_t>::max())) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<unsigned char>(static_cast<int64_t>(number));
    return true;
}

bool ReadScriptStringArg(lua_State* L, const char* function, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
        (void)lua_rawlen(L, index);
        out.clear();
        return true;
    }
    if (type != LUA_TSTRING) {
        LogTypeError(L, function, "string", index);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    out.assign(text ? text : "", text ? length : 0);
    return true;
}

std::string ParserError(lua_State* L, const char* function, const char* expected, int index) {
    const int type = lua_type(L, index);
    const char* value = nullptr;
    if (type == LUA_TNUMBER || type == LUA_TSTRING) value = lua_tostring(L, index);
    char buf[512];
    if (value && value[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s '%.64s']",
                      function, expected, index, LuaTypeName(type), value);
    } else {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function,
                      expected, index, LuaTypeName(type));
    }
    return buf;
}

bool ReadVehicleId(lua_State* L, const char* function, uint32_t& id, int& model) {
    if (!ReadVehicleModel(L, function, model)) return false;
    id = ElementIdOf(L, 1);
    return true;
}

int IsVehicleLocked(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleLocked", id, model)) return FailNil(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).locked ? 1 : 0);
    return 1;
}

int SetVehicleLocked(lua_State* L) {
    uint32_t id = 0;
    bool locked = false;
    if (!ReadElement(L, "setVehicleLocked", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleLocked", 2, locked)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.locked = locked;
    flags.doorLock = locked ? 2 : 1;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleDoorLockMode(lua_State* L) {
    int model = 0;
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleDoorLockMode", "vehicle", 1).c_str());
    (void)model;
    lua_pushnumber(L, CopyFlags(ElementIdOf(L, 1)).doorLock);
    return 1;
}

int SetVehicleDoorLockMode(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleDoorLockMode", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2)) return luaL_error(L, "%s", ParserError(L, "setVehicleDoorLockMode", "number", 2).c_str());
    const lua_Number number = lua_tonumber(L, 2);
    if (std::isnan(number))
        return luaL_error(L, "Bad argument @ 'setVehicleDoorLockMode' [Expected number at argument 2, got NaN]");
    if (std::isinf(number) || number < -2147483648.0 || number >= 2147483648.0)
        return luaL_error(L, "Bad argument @ 'setVehicleDoorLockMode' [Expected number within the destination type range at argument 2, got out-of-range number]");
    const int mode = static_cast<int>(number);
    if (mode < 1 || mode > 6) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.doorLock = mode;
    flags.locked = mode != 1;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleEngineState(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleEngineState", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).engine ? 1 : 0);
    return 1;
}

int SetVehicleEngineState(lua_State* L) {
    uint32_t id = 0;
    bool on = false;
    if (!ReadElement(L, "setVehicleEngineState", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleEngineState", 2, on)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.engine = on;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleOverrideLights(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleOverrideLights", id, model)) return Fail(L);
    (void)model;
    lua_pushnumber(L, CopyFlags(id).overrideLights);
    return 1;
}

int SetVehicleOverrideLights(lua_State* L) {
    uint32_t id = 0;
    unsigned char lights = 0;
    if (!ReadElement(L, "setVehicleOverrideLights", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleOverrideLights", 2, lights)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0 || lights > 2) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.overrideLights = lights;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehiclePaintjob(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehiclePaintjob", id, model)) return Fail(L);
    (void)model;
    lua_pushnumber(L, CopyFlags(id).paintjob);
    return 1;
}

int SetVehiclePaintjob(lua_State* L) {
    uint32_t id = 0;
    unsigned char paint = 0;
    if (!ReadElement(L, "setVehiclePaintjob", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehiclePaintjob", 2, paint)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    auto flags = CopyFlags(id);
    if (paint > 3 || paint == flags.paintjob) return Fail(L);
    NoteNativeVehicleGap();
    flags.paintjob = paint;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehiclePlateText(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehiclePlateText", id, model)) return Fail(L);
    (void)model;
    const std::string plate = CopyFlags(id).plate;
    if (plate.empty()) return Fail(L);
    lua_pushlstring(L, plate.data(), plate.size());
    return 1;
}

int SetVehiclePlateText(lua_State* L) {
    uint32_t id = 0;
    std::string text;
    if (!ReadElement(L, "setVehiclePlateText", id)) return Fail(L);
    if (!ReadScriptStringArg(L, "setVehiclePlateText", 2, text)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    if (text.size() > 8) text.resize(8);
    auto flags = CopyFlags(id);
    if (text == flags.plate) return Fail(L);
    NoteNativeVehicleGap();
    flags.plate = std::move(text);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleDamageProof(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleDamageProof", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).damageProof ? 1 : 0);
    return 1;
}

int SetVehicleDamageProof(lua_State* L) {
    uint32_t id = 0;
    bool proof = false;
    if (!ReadElement(L, "setVehicleDamageProof", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleDamageProof", 2, proof)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.damageProof = proof;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleFuelTankExplodable(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleFuelTankExplodable", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).fuelExplodable ? 1 : 0);
    return 1;
}

int SetVehicleFuelTankExplodable(lua_State* L) {
    uint32_t id = 0;
    bool explodable = false;
    if (!ReadElement(L, "setVehicleFuelTankExplodable", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleFuelTankExplodable", 2, explodable)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.fuelExplodable = explodable;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleFrozen(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleFrozen", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).frozen ? 1 : 0);
    return 1;
}

int SetVehicleFrozen(lua_State* L) {
    uint32_t id = 0;
    bool frozen = false;
    if (!ReadElement(L, "setVehicleFrozen", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleFrozen", 2, frozen)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.frozen = frozen;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleLandingGearDown(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleLandingGearDown", id, model)) return FailNil(L);
    if (!ModelHasLandingGears(model)) return FailNil(L);
    lua_pushboolean(L, CopyFlags(id).landingGearDown ? 1 : 0);
    return 1;
}

int SetVehicleLandingGearDown(lua_State* L) {
    uint32_t id = 0;
    bool down = false;
    if (!ReadElement(L, "setVehicleLandingGearDown", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleLandingGearDown", 2, down)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    const int model = ServerWorld::ModelOf(id);
    auto flags = CopyFlags(id);
    if (!ModelHasLandingGearBody(model) || down == flags.landingGearDown) return Fail(L);
    NoteNativeVehicleGap();
    flags.landingGearDown = down;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleSirensOn(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleSirensOn", id, model)) return FailNil(L);
    if (!ModelHasSirens(model)) return FailNil(L);
    lua_pushboolean(L, CopyFlags(id).sirensOn ? 1 : 0);
    return 1;
}

int SetVehicleSirensOn(lua_State* L) {
    uint32_t id = 0;
    bool on = false;
    if (!ReadElement(L, "setVehicleSirensOn", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleSirensOn", 2, on)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    const int model = ServerWorld::ModelOf(id);
    auto flags = CopyFlags(id);
    if (!ModelHasSirens(model) || on == flags.sirensOn) return Fail(L);
    NoteNativeVehicleGap();
    flags.sirensOn = on;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleTaxiLightOn(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleTaxiLightOn", id, model)) return Fail(L);
    if ((model == 420 || model == 438) && CopyFlags(id).taxiLight) {
        lua_pushboolean(L, 1);
        return 1;
    }
    return Fail(L);
}

int SetVehicleTaxiLightOn(lua_State* L) {
    uint32_t id = 0;
    bool on = false;
    if (!ReadElement(L, "setVehicleTaxiLightOn", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleTaxiLightOn", 2, on)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    const int model = ServerWorld::ModelOf(id);
    if (model != 420 && model != 438) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.taxiLight = on;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleTyresCanBurst(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleTyresCanBurst", "vehicle", 1).c_str());
    lua_pushboolean(L, CopyFlags(ElementIdOf(L, 1)).tyresCanBurst ? 1 : 0);
    return 1;
}

int SetVehicleTyresCanBurst(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleTyresCanBurst", "vehicle", 1).c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN) return luaL_error(L, "%s", ParserError(L, "setVehicleTyresCanBurst", "boolean", 2).c_str());
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.tyresCanBurst = lua_toboolean(L, 2) != 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

bool IsTrainModel(int model) { return std::strcmp(VehicleTypeOf(model), "Train") == 0; }

bool IsHeliModel(int model) { return std::strcmp(VehicleTypeOf(model), "Helicopter") == 0; }

bool IsPlaneModel(int model) { return std::strcmp(VehicleTypeOf(model), "Plane") == 0; }

bool MissingDoorModel(int model) {
    return HasModel(model, {424, 441, 457, 465, 485, 486, 501, 504, 530, 531, 539, 564, 568, 571, 572, 471, 594});
}

bool DoorWriteBlocked(int model) {
    return HasModel(model, {424, 441, 457, 465, 485, 486, 501, 530, 531, 564, 568, 571, 572, 594});
}

void PrepareDamage(VehicleFlags& flags, int model) {
    if (flags.damageReady) return;
    flags.damageReady = true;
    if (!MissingDoorModel(model)) return;
    for (unsigned char& door : flags.doors) door = 4;
    flags.doors[0] = flags.doors[1] = 0;
}

bool ReadFinite(lua_State* L, const char* function, int index, double& out) {
    if (!lua_isnumber(L, index)) {
        LogTypeError(L, function, "number", index);
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    out = static_cast<double>(number);
    return true;
}

bool ReadOptionalSigned(lua_State* L, const char* function, int index, int fallback, int& out) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) {
        out = fallback;
        return true;
    }
    double number = 0;
    if (!ReadFinite(L, function, index, number)) return false;
    if (!std::isfinite(number) || number < -2147483648.0 || number >= 2147483648.0) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

int WheelIndex(lua_State* L, const char* function, int& wheel) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* text = lua_tostring(L, 2);
        if (std::strcmp(text, "front_left") == 0) wheel = 0;
        else if (std::strcmp(text, "rear_left") == 0) wheel = 1;
        else if (std::strcmp(text, "front_right") == 0) wheel = 2;
        else if (std::strcmp(text, "rear_right") == 0) wheel = 3;
        else {
            LogTypeError(L, function, "wheel-position", 2);
            return Fail(L);
        }
        return 0;
    }
    if (!lua_isnumber(L, 2)) {
        LogTypeError(L, function, "wheel-position", 2);
        return Fail(L);
    }
    const lua_Number number = lua_tonumber(L, 2);
    if (number < 0 || number > 3 || !std::isfinite(number)) {
        LogTypeError(L, function, "wheel-position", 2);
        return Fail(L);
    }
    wheel = static_cast<int>(number);
    return 0;
}

int GetVehicleDoorState(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char door = 0;
    if (!ReadVehicleId(L, "getVehicleDoorState", id, model)) return Fail(L);
    if (!ReadByteArg(L, "getVehicleDoorState", 2, door)) return Fail(L);
    auto flags = CopyFlags(id);
    PrepareDamage(flags, model);
    UpdateFlags(id, flags);
    lua_pushnumber(L, door < 6 ? flags.doors[door] : 0);
    return 1;
}

int SetVehicleDoorState(lua_State* L) {
    uint32_t id = 0;
    unsigned char door = 0;
    unsigned char state = 0;
    bool flying = true;
    if (!ReadElement(L, "setVehicleDoorState", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleDoorState", 2, door)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleDoorState", 3, state)) return Fail(L);
    if (lua_type(L, 4) != LUA_TNONE && lua_type(L, 4) != LUA_TNIL && !ReadBoolArg(L, "setVehicleDoorState", 4, flying)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    const int model = ServerWorld::ModelOf(id);
    if (door >= 6 || DoorWriteBlocked(model)) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    PrepareDamage(flags, model);
    flags.doors[door] = state;
    UpdateFlags(id, flags);
    (void)flying;
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleLightState(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char light = 0;
    if (!ReadVehicleId(L, "getVehicleLightState", id, model)) return Fail(L);
    if (!ReadByteArg(L, "getVehicleLightState", 2, light)) return Fail(L);
    (void)model;
    lua_pushnumber(L, light < 4 ? CopyFlags(id).lights[light] : 0);
    return 1;
}

int SetVehicleLightState(lua_State* L) {
    uint32_t id = 0;
    unsigned char light = 0;
    unsigned char state = 0;
    if (!ReadElement(L, "setVehicleLightState", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleLightState", 2, light)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleLightState", 3, state)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0 || light >= 4) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.lights[light] = state;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehiclePanelState(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char panel = 0;
    if (!ReadVehicleId(L, "getVehiclePanelState", id, model)) return Fail(L);
    if (!ReadByteArg(L, "getVehiclePanelState", 2, panel)) return Fail(L);
    (void)model;
    lua_pushnumber(L, panel < 7 ? CopyFlags(id).panels[panel] : 0);
    return 1;
}

int SetVehiclePanelState(lua_State* L) {
    uint32_t id = 0;
    unsigned char panel = 0;
    unsigned char state = 0;
    if (!ReadElement(L, "setVehiclePanelState", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehiclePanelState", 2, panel)) return Fail(L);
    if (!ReadByteArg(L, "setVehiclePanelState", 3, state)) return Fail(L);
    if (lua_type(L, 4) != LUA_TNONE && lua_type(L, 4) != LUA_TNIL && lua_type(L, 4) != LUA_TBOOLEAN) {
        LogTypeError(L, "setVehiclePanelState", "bool", 4);
        return Fail(L);
    }
    if (lua_type(L, 5) != LUA_TNONE && lua_type(L, 5) != LUA_TNIL && lua_type(L, 5) != LUA_TBOOLEAN) {
        LogTypeError(L, "setVehiclePanelState", "bool", 5);
        return Fail(L);
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0 || panel >= 7) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.panels[panel] = state;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleWheelStates(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleWheelStates", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.wheels[0]);
    lua_pushnumber(L, flags.wheels[1]);
    lua_pushnumber(L, flags.wheels[2]);
    lua_pushnumber(L, flags.wheels[3]);
    return 4;
}

int SetVehicleWheelStates(lua_State* L) {
    uint32_t id = 0;
    int frontLeft = -1;
    int rearLeft = -1;
    int frontRight = -1;
    int rearRight = -1;
    if (!ReadElement(L, "setVehicleWheelStates", id)) return Fail(L);
    if (!ReadOptionalSigned(L, "setVehicleWheelStates", 2, -1, frontLeft)) return Fail(L);
    if (!ReadOptionalSigned(L, "setVehicleWheelStates", 3, -1, rearLeft)) return Fail(L);
    if (!ReadOptionalSigned(L, "setVehicleWheelStates", 4, -1, frontRight)) return Fail(L);
    if (!ReadOptionalSigned(L, "setVehicleWheelStates", 5, -1, rearRight)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    auto flags = CopyFlags(id);
    unsigned char next[4] = {flags.wheels[0], flags.wheels[1], flags.wheels[2], flags.wheels[3]};
    const int values[4] = {frontLeft, rearLeft, frontRight, rearRight};
    for (int i = 0; i < 4; ++i) {
        if (values[i] >= 0 && values[i] <= 3) next[i] = static_cast<unsigned char>(values[i]);
    }
    if (next[0] == flags.wheels[0] && next[1] == flags.wheels[1] && next[2] == flags.wheels[2] && next[3] == flags.wheels[3]) return Fail(L);
    NoteNativeVehicleGap();
    flags.wheels[0] = next[0];
    flags.wheels[1] = next[1];
    flags.wheels[2] = next[2];
    flags.wheels[3] = next[3];
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleWheelOnGround(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    int wheel = 0;
    if (!ReadVehicleId(L, "isVehicleWheelOnGround", id, model)) return Fail(L);
    if (WheelIndex(L, "isVehicleWheelOnGround", wheel) != 0) return 1;
    (void)id;
    (void)model;
    (void)wheel;
    static bool noted = false;
    if (!noted) {
        noted = true;
        LUA_LOGI("DIAG isVehicleWheelOnGround: CVehicle::IsWheelCollided is not read");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleWheelFrictionState(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleWheelFrictionState", "vehicle", 1).c_str());
    int wheel = 0;
    if (WheelIndex(L, "getVehicleWheelFrictionState", wheel) != 0) return luaL_error(L, "%s", ParserError(L, "getVehicleWheelFrictionState", "wheel-position", 2).c_str());
    (void)wheel;
    lua_pushnumber(L, 0);
    return 1;
}

int IsVehicleOnGround(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleOnGround", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).onGround ? 1 : 0);
    return 1;
}

int IsVehicleOnAllWheels(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleOnAllWheels", id, model)) return Fail(L);
    (void)id;
    (void)model;
    static bool noted = false;
    if (!noted) {
        noted = true;
        LUA_LOGI("DIAG isVehicleOnAllWheels: CVehicle::IsOnAllWheels is not read");
    }
    lua_pushboolean(L, 0);
    return 1;
}

int FixVehicle(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, "fixVehicle", id)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    const int model = ServerWorld::ModelOf(id);
    flags.damageReady = false;
    for (unsigned char& door : flags.doors) door = 0;
    PrepareDamage(flags, model);
    for (unsigned char& wheel : flags.wheels) wheel = 0;
    for (unsigned char& panel : flags.panels) panel = 0;
    for (unsigned char& light : flags.lights) light = 0;
    flags.blown = false;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleBlown(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "isVehicleBlown", id, model)) return Fail(L);
    (void)model;
    lua_pushboolean(L, CopyFlags(id).blown ? 1 : 0);
    return 1;
}

int BlowVehicle(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, "blowVehicle", id)) return Fail(L);
    if (lua_type(L, 2) != LUA_TNONE && lua_type(L, 2) != LUA_TNIL && lua_type(L, 2) != LUA_TBOOLEAN) {
        LogTypeError(L, "blowVehicle", "bool", 2);
        return Fail(L);
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.blown = true;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int AreVehicleLightsOn(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "areVehicleLightsOn", "vehicle", 1).c_str());
    lua_pushboolean(L, CopyFlags(ElementIdOf(L, 1)).overrideLights == 2 ? 1 : 0);
    return 1;
}

int GetVehicleCurrentGear(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleCurrentGear", id, model)) return Fail(L);
    (void)id;
    (void)model;
    lua_pushnumber(L, 0);
    return 1;
}

int GetVehicleTurnVelocity(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleTurnVelocity", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.turnX);
    lua_pushnumber(L, flags.turnY);
    lua_pushnumber(L, flags.turnZ);
    return 3;
}

int SetVehicleTurnVelocity(lua_State* L) {
    uint32_t id = 0;
    double x = 0;
    double y = 0;
    double z = 0;
    if (!ReadElement(L, "setVehicleTurnVelocity", id)) return Fail(L);
    if (!ReadFinite(L, "setVehicleTurnVelocity", 2, x)) return Fail(L);
    if (!ReadFinite(L, "setVehicleTurnVelocity", 3, y)) return Fail(L);
    if (!ReadFinite(L, "setVehicleTurnVelocity", 4, z)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.turnX = static_cast<float>(x);
    flags.turnY = static_cast<float>(y);
    flags.turnZ = static_cast<float>(z);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleGravity(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleGravity", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.gravityX);
    lua_pushnumber(L, flags.gravityY);
    lua_pushnumber(L, flags.gravityZ);
    return 3;
}

int SetVehicleGravity(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double x = 0;
    double y = 0;
    double z = 0;
    if (!ReadVehicleId(L, "setVehicleGravity", id, model)) return Fail(L);
    if (!ReadFinite(L, "setVehicleGravity", 2, x)) return Fail(L);
    if (!ReadFinite(L, "setVehicleGravity", 3, y)) return Fail(L);
    if (!ReadFinite(L, "setVehicleGravity", 4, z)) return Fail(L);
    (void)model;
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.gravityX = static_cast<float>(x);
    flags.gravityY = static_cast<float>(y);
    flags.gravityZ = static_cast<float>(z);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleHeadLightColor(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleHeadLightColor", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.headR);
    lua_pushnumber(L, flags.headG);
    lua_pushnumber(L, flags.headB);
    return 3;
}

int SetVehicleHeadLightColor(lua_State* L) {
    uint32_t id = 0;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    if (!ReadElement(L, "setVehicleHeadLightColor", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleHeadLightColor", 2, r)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleHeadLightColor", 3, g)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleHeadLightColor", 4, b)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.headR = r;
    flags.headG = g;
    flags.headB = b;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetHelicopterRotorSpeed(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getHelicopterRotorSpeed", id, model)) return Fail(L);
    if (!IsHeliModel(model)) return Fail(L);
    lua_pushnumber(L, CopyFlags(id).heliRotor);
    return 1;
}

int TrainGetBool(lua_State* L, const char* function, bool VehicleFlags::*field) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, function, id, model)) return Fail(L);
    if (!IsTrainModel(model)) return Fail(L);
    lua_pushboolean(L, CopyFlags(id).*field ? 1 : 0);
    return 1;
}

int TrainSetBool(lua_State* L, const char* function, bool VehicleFlags::*field) {
    int model = 0;
    uint32_t id = 0;
    bool value = false;
    if (!ReadVehicleId(L, function, id, model)) return Fail(L);
    if (!ReadBoolArg(L, function, 2, value)) return Fail(L);
    if (!IsTrainModel(model)) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.*field = value;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsTrainDerailed(lua_State* L) { return TrainGetBool(L, "isTrainDerailed", &VehicleFlags::derailed); }
int SetTrainDerailed(lua_State* L) { return TrainSetBool(L, "setTrainDerailed", &VehicleFlags::derailed); }
int IsTrainDerailable(lua_State* L) { return TrainGetBool(L, "isTrainDerailable", &VehicleFlags::derailable); }
int SetTrainDerailable(lua_State* L) { return TrainSetBool(L, "setTrainDerailable", &VehicleFlags::derailable); }
int GetTrainDirection(lua_State* L) { return TrainGetBool(L, "getTrainDirection", &VehicleFlags::trainDirection); }
int SetTrainDirection(lua_State* L) { return TrainSetBool(L, "setTrainDirection", &VehicleFlags::trainDirection); }
int IsTrainChainEngine(lua_State* L) { return TrainGetBool(L, "isTrainChainEngine", &VehicleFlags::chainEngine); }

int GetTrainSpeed(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getTrainSpeed", id, model)) return Fail(L);
    if (!IsTrainModel(model)) return Fail(L);
    lua_pushnumber(L, CopyFlags(id).trainSpeed);
    return 1;
}

int SetTrainSpeed(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double speed = 0;
    if (!ReadVehicleId(L, "setTrainSpeed", id, model)) return Fail(L);
    if (!ReadFinite(L, "setTrainSpeed", 2, speed)) return Fail(L);
    if (!IsTrainModel(model)) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.trainSpeed = static_cast<float>(speed);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetTrainPosition(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getTrainPosition", id, model)) return Fail(L);
    if (!IsTrainModel(model)) return Fail(L);
    const auto flags = CopyFlags(id);
    if (flags.derailed) return Fail(L);
    lua_pushnumber(L, flags.trainPosition);
    return 1;
}

int SetTrainPosition(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double position = 0;
    if (!ReadVehicleId(L, "setTrainPosition", id, model)) return Fail(L);
    if (!ReadFinite(L, "setTrainPosition", 2, position)) return Fail(L);
    if (std::fabs(position) > static_cast<double>(std::numeric_limits<long>::max())) {
        LogFull("setTrainPosition", "Bad argument", "Train position supplied is too large");
        return Fail(L);
    }
    if (!IsTrainModel(model)) return Fail(L);
    auto flags = CopyFlags(id);
    if (flags.derailed) return Fail(L);
    NoteNativeVehicleGap();
    flags.trainPosition = static_cast<float>(position);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleColor(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleColor", id, model)) return Fail(L);
    (void)model;
    bool rgb = false;
    if (lua_type(L, 2) == LUA_TBOOLEAN) rgb = lua_toboolean(L, 2) != 0;
    const auto flags = CopyFlags(id);
    if (rgb) {
        for (int i = 0; i < 12; ++i) lua_pushnumber(L, flags.rgb[i]);
        return 12;
    }
    for (int i = 0; i < 4; ++i) lua_pushnumber(L, flags.palette[i]);
    return 4;
}

int SetVehicleColor(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, "setVehicleColor", id)) return Fail(L);
    unsigned char values[12] = {};
    int count = 0;
    for (; count < 12; ++count) {
        if (!lua_isnumber(L, count + 2)) break;
        double number = 0;
        if (!ReadFinite(L, "setVehicleColor", count + 2, number)) return Fail(L);
        if (number < -static_cast<double>(FLT_EPSILON)) {
            LogFull("setVehicleColor", "Bad argument", "Expected positive value, got negative");
            return Fail(L);
        }
        values[count] = static_cast<unsigned char>(static_cast<int64_t>(number));
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    if (count != 4 && count != 3 && count != 6 && count != 9 && count != 12) {
        LogFull("setVehicleColor", "Bad usage", "Incorrect number of color arguments");
        return Fail(L);
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    if (count == 4) {
        flags.colorRgb = false;
        for (int i = 0; i < 4; ++i) flags.palette[i] = values[i];
    } else {
        flags.colorRgb = true;
        for (int i = 0; i < 12; ++i) flags.rgb[i] = 0;
        for (int i = 0; i < count; ++i) flags.rgb[i] = values[i];
    }
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleVariant(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleVariant", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.variant);
    lua_pushnumber(L, flags.variant2);
    return 2;
}

int SetVehicleVariant(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleVariant", "vehicle", 1).c_str());
    auto read = [&](int index, unsigned char fallback, unsigned char& out) {
        if (lua_type(L, index) == LUA_TNONE || lua_type(L, index) == LUA_TNIL) {
            out = fallback;
            return true;
        }
        if (!lua_isnumber(L, index)) return false;
        const lua_Number number = lua_tonumber(L, index);
        if (std::isnan(number) || number < 0 || number > 255) return false;
        out = static_cast<unsigned char>(number);
        return true;
    };
    unsigned char first = 254;
    unsigned char second = 254;
    if (!read(2, 254, first) || !read(3, 254, second))
        return luaL_error(L, "%s", ParserError(L, "setVehicleVariant", "number", 2).c_str());
    if (first == 254 && second == 254) {
        LUA_LOGI("DIAG setVehicleVariant: GetRandomVariation is not hooked");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!((first <= 5 || first == 255) && (second <= 5 || second == 255))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.variant = first;
    flags.variant2 = second;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleWheelScale(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleWheelScale", "vehicle", 1).c_str());
    lua_pushnumber(L, CopyFlags(ElementIdOf(L, 1)).wheelScale);
    return 1;
}

int SetVehicleWheelScale(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleWheelScale", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleWheelScale", "number", 2).c_str());
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.wheelScale = static_cast<float>(lua_tonumber(L, 2));
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleSmokeTrailEnabled(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "isVehicleSmokeTrailEnabled", "vehicle", 1).c_str());
    lua_pushboolean(L, CopyFlags(ElementIdOf(L, 1)).smokeTrail ? 1 : 0);
    return 1;
}

int SetVehicleSmokeTrailEnabled(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleSmokeTrailEnabled", "vehicle", 1).c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN)
        return luaL_error(L, "%s", ParserError(L, "setVehicleSmokeTrailEnabled", "boolean", 2).c_str());
    const int smokeModel = ServerWorld::ModelOf(ElementIdOf(L, 1));
    if (smokeModel != 512 && smokeModel != 513) return luaL_error(L, "Invalid model ID");
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.smokeTrail = lua_toboolean(L, 2) != 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

bool KnownHandling(const char* name) {
    static const char* names[] = {
        "mass", "turnMass", "dragCoeff", "centerOfMass", "percentSubmerged", "tractionMultiplier", "driveType", "engineType",
        "numberOfGears", "engineAcceleration", "engineInertia", "maxVelocity", "brakeDeceleration", "brakeBias", "ABS",
        "steeringLock", "tractionLoss", "tractionBias", "suspensionForceLevel", "suspensionDamping", "suspensionHighSpeedDamping",
        "suspensionUpperLimit", "suspensionLowerLimit", "suspensionFrontRearBias", "suspensionAntiDiveMultiplier",
        "collisionDamageMultiplier", "seatOffsetDistance", "monetary", "handlingFlags", "modelFlags", "headLight", "tailLight", "animGroup",
    };
    for (const char* item : names) if (std::strcmp(item, name) == 0) return true;
    return false;
}

bool KnownAudio(const char* name) {
    static const char* names[] = {
        "door-sound", "engine-off-soundbank-id", "engine-on-soundbank-id", "horn-high", "horn-ton", "horn-volume-delta",
        "radio-num", "radio-type", "sound-type", "bass-setting", "bass-eq", "field-c", "engine-upgrade", "vehicle-type-for-audio",
    };
    for (const char* item : names) if (std::strcmp(item, name) == 0) return true;
    return false;
}

bool KnownDummy(const char* name) {
    static const char* names[] = {
        "light_front_main", "light_rear_main", "light_front_second", "light_rear_second", "seat_front", "seat_rear",
        "exhaust", "engine", "gas_cap", "trailer_attach", "hand_rest", "exhaust_second", "wing_airtrail", "veh_gun",
    };
    for (const char* item : names) if (std::strcmp(item, name) == 0) return true;
    return false;
}

void NoteMissingModelData(const char* function) {
    static bool noted = false;
    if (noted) return;
    noted = true;
    LUA_LOGI("DIAG %s: CModelInfo and the handling/audio tables are not loaded", function);
}

int AddVehicleSirens(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "addVehicleSirens", "vehicle", 1).c_str());
    auto readCount = [&](int index, unsigned char& out) {
        if (!lua_isnumber(L, index) || std::isnan(lua_tonumber(L, index)))
            return luaL_error(L, "%s", ParserError(L, "addVehicleSirens", "number", index).c_str());
        const lua_Number number = lua_tonumber(L, index);
        if (number < 0.0 || number > 255.0)
            return luaL_error(L, "%s", ParserError(L, "addVehicleSirens", "number within the destination type range", index).c_str());
        out = static_cast<unsigned char>(number);
        return 0;
    };
    auto readFlag = [&](int index, bool fallback, bool& out) {
        const int type = lua_type(L, index);
        if (type == LUA_TNONE || type == LUA_TNIL) {
            out = fallback;
            return 0;
        }
        if (type != LUA_TBOOLEAN)
            return luaL_error(L, "%s", ParserError(L, "addVehicleSirens", "boolean", index).c_str());
        out = lua_toboolean(L, index) != 0;
        return 0;
    };
    unsigned char type = 0;
    unsigned char count = 0;
    bool flag360 = false;
    bool flagLos = true;
    bool flagRandom = true;
    bool flagSilent = false;
    if (readCount(2, type) != 0 || readCount(3, count) != 0) return 1;
    if (readFlag(4, false, flag360) != 0 || readFlag(5, true, flagLos) != 0 || readFlag(6, true, flagRandom) != 0 ||
        readFlag(7, false, flagSilent) != 0)
        return 1;
    if (type < 1 || type > 6 || count > 8) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.sirenType = type;
    flags.sirenCount = count;
    flags.siren360 = flag360;
    flags.sirenLos = flagLos;
    flags.sirenRandom = flagRandom;
    flags.sirenSilent = flagSilent;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int RemoveVehicleSirens(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "removeVehicleSirens", "vehicle", 1).c_str());
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.sirenCount = 0;
    flags.sirenType = 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleSirenParams(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleSirenParams", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_newtable(L);
    lua_pushnumber(L, flags.sirenCount);
    lua_setfield(L, -2, "SirenCount");
    lua_pushnumber(L, flags.sirenType);
    lua_setfield(L, -2, "SirenType");
    lua_newtable(L);
    lua_pushboolean(L, flags.siren360 ? 1 : 0);
    lua_setfield(L, -2, "360");
    lua_pushboolean(L, flags.sirenLos ? 1 : 0);
    lua_setfield(L, -2, "DoLOSCheck");
    lua_pushboolean(L, flags.sirenRandom ? 1 : 0);
    lua_setfield(L, -2, "UseRandomiser");
    lua_pushboolean(L, flags.sirenSilent ? 1 : 0);
    lua_setfield(L, -2, "Silent");
    lua_setfield(L, -2, "Flags");
    return 1;
}

int GetVehicleSirens(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleSirens", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_newtable(L);
    for (unsigned i = 0; i < flags.sirenCount && i < 8; ++i) {
        lua_pushinteger(L, static_cast<int>(i + 1));
        lua_newtable(L);
        lua_pushnumber(L, flags.sirenPoints[i].minAlpha);
        lua_setfield(L, -2, "Min_Alpha");
        lua_pushnumber(L, flags.sirenPoints[i].r);
        lua_setfield(L, -2, "Red");
        lua_pushnumber(L, flags.sirenPoints[i].g);
        lua_setfield(L, -2, "Green");
        lua_pushnumber(L, flags.sirenPoints[i].b);
        lua_setfield(L, -2, "Blue");
        lua_pushnumber(L, flags.sirenPoints[i].a);
        lua_setfield(L, -2, "Alpha");
        lua_pushnumber(L, flags.sirenPoints[i].x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, flags.sirenPoints[i].y);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, flags.sirenPoints[i].z);
        lua_setfield(L, -2, "z");
        lua_settable(L, -3);
    }
    return 1;
}

int SetVehicleSirens(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char siren = 0;
    if (!ReadVehicleId(L, "setVehicleSirens", id, model)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleSirens", 2, siren)) return Fail(L);
    (void)model;
    if (siren < 1 || siren > 8) return Fail(L);
    double x = 0;
    double y = 0;
    double z = 0;
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
    unsigned char minAlpha = 0;
    if (!ReadFinite(L, "setVehicleSirens", 3, x) || !ReadFinite(L, "setVehicleSirens", 4, y) || !ReadFinite(L, "setVehicleSirens", 5, z)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleSirens", 6, r) || !ReadByteArg(L, "setVehicleSirens", 7, g) || !ReadByteArg(L, "setVehicleSirens", 8, b)) return Fail(L);
    if (lua_type(L, 9) != LUA_TNONE && lua_type(L, 9) != LUA_TNIL && !ReadByteArg(L, "setVehicleSirens", 9, a)) return Fail(L);
    if (lua_type(L, 10) != LUA_TNONE && lua_type(L, 10) != LUA_TNIL && !ReadByteArg(L, "setVehicleSirens", 10, minAlpha)) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    auto& point = flags.sirenPoints[siren - 1];
    point.x = static_cast<float>(x);
    point.y = static_cast<float>(y);
    point.z = static_cast<float>(z);
    point.r = r;
    point.g = g;
    point.b = b;
    point.a = a;
    point.minAlpha = minAlpha;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleRotorState(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleRotorState", "vehicle", 1).c_str());
    lua_pushboolean(L, CopyFlags(ElementIdOf(L, 1)).rotorState ? 1 : 0);
    return 1;
}

int SetVehicleRotorState(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleRotorState", "vehicle", 1).c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN) return luaL_error(L, "%s", ParserError(L, "setVehicleRotorState", "boolean", 2).c_str());
    const int model = ServerWorld::ModelOf(ElementIdOf(L, 1));
    if (!IsHeliModel(model) && !IsPlaneModel(model)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.rotorState = lua_toboolean(L, 2) != 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetOriginalHandling(lua_State* L) {
    uint32_t supplied = 0;
    if (!ReadModelArg(L, "getOriginalHandling", supplied)) return Fail(L);
    uint16_t runtime = 0;
    if (!ResolveStockModel(supplied, runtime)) {
        LogFull("getOriginalHandling", "Bad usage", "invalid model ID");
        return Fail(L);
    }
    if (!IsStandardModel(runtime)) return Fail(L);
    NoteMissingModelData("getOriginalHandling");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleHandling(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleHandling", id, model)) return Fail(L);
    (void)id;
    (void)model;
    if (lua_type(L, 2) == LUA_TSTRING && !KnownHandling(lua_tostring(L, 2) ? lua_tostring(L, 2) : "")) return Fail(L);
    NoteMissingModelData("getVehicleHandling");
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleHandling(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "setVehicleHandling", id, model)) return Fail(L);
    (void)id;
    (void)model;
    if (lua_type(L, 2) != LUA_TSTRING || !KnownHandling(lua_tostring(L, 2) ? lua_tostring(L, 2) : "")) return Fail(L);
    NoteMissingModelData("setVehicleHandling");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleModelAudioSettings(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", ParserError(L, "getVehicleModelAudioSettings", "number", 1).c_str());
    const lua_Number number = lua_tonumber(L, 1);
    if (std::isnan(number) || number < 0 || number > 65534) return luaL_error(L, "Invalid model id");
    uint16_t runtime = 0;
    if (!ResolveStockModel(static_cast<uint32_t>(number), runtime) || !IsStandardModel(runtime)) return luaL_error(L, "Invalid model id");
    NoteMissingModelData("getVehicleModelAudioSettings");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleAudioSettings(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleAudioSettings", "vehicle", 1).c_str());
    NoteMissingModelData("getVehicleAudioSettings");
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleModelAudioSetting(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", ParserError(L, "setVehicleModelAudioSetting", "number", 1).c_str());
    const int model = static_cast<int>(lua_tonumber(L, 1));
    if (!IsStandardModel(model)) return luaL_error(L, "Cannot change audio setting for allocated vechiles");
    const char* property = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!property || !KnownAudio(property)) return luaL_error(L, "%s", ParserError(L, "setVehicleModelAudioSetting", "vehicle-audio-setting", 2).c_str());
    if (!lua_isnumber(L, 3) || std::isnan(lua_tonumber(L, 3)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleModelAudioSetting", "number", 3).c_str());
    NoteMissingModelData("setVehicleModelAudioSetting");
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleAudioSetting(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleAudioSetting", "vehicle", 1).c_str());
    const char* property = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!property || !KnownAudio(property)) return luaL_error(L, "%s", ParserError(L, "setVehicleAudioSetting", "vehicle-audio-setting", 2).c_str());
    if (!lua_isnumber(L, 3) || std::isnan(lua_tonumber(L, 3)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleAudioSetting", "number", 3).c_str());
    NoteMissingModelData("setVehicleAudioSetting");
    lua_pushboolean(L, 0);
    return 1;
}

int ResetVehicleAudioSettings(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "resetVehicleAudioSettings", "vehicle", 1).c_str());
    NoteMissingModelData("resetVehicleAudioSettings");
    lua_pushboolean(L, 0);
    return 1;
}

int ResetVehicleModelAudioSettings(lua_State* L) {
    if (!lua_isnumber(L, 1) || !IsStandardModel(static_cast<int>(lua_tonumber(L, 1))))
        return luaL_error(L, "Cannot change audio setting for allocated vechiles");
    NoteMissingModelData("resetVehicleModelAudioSettings");
    lua_pushboolean(L, 0);
    return 1;
}

int ModelDataMiss(lua_State* L, const char* function) {
    uint32_t supplied = 0;
    if (!ReadModelArg(L, function, supplied)) return Fail(L);
    uint16_t runtime = 0;
    if (!ResolveStockModel(supplied, runtime)) {
        LogFull(function, "Bad usage", "invalid model ID");
        return Fail(L);
    }
    const char* dummy = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!dummy || !KnownDummy(dummy)) {
        LogTypeError(L, function, "vehicle-dummy", 2);
        return Fail(L);
    }
    NoteMissingModelData(function);
    (void)runtime;
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleModelDummyPosition(lua_State* L) { return ModelDataMiss(L, "getVehicleModelDummyPosition"); }
int SetVehicleModelDummyPosition(lua_State* L) { return ModelDataMiss(L, "setVehicleModelDummyPosition"); }

int GetVehicleModelDummyDefaultPosition(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", ParserError(L, "getVehicleModelDummyDefaultPosition", "number", 1).c_str());
    const char* dummy = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!dummy || !KnownDummy(dummy)) return luaL_error(L, "%s", ParserError(L, "getVehicleModelDummyDefaultPosition", "vehicle-dummy", 2).c_str());
    NoteMissingModelData("getVehicleModelDummyDefaultPosition");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleDummyPosition(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleDummyPosition", "vehicle", 1).c_str());
    const char* dummy = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!dummy || !KnownDummy(dummy)) return luaL_error(L, "%s", ParserError(L, "getVehicleDummyPosition", "vehicle-dummy", 2).c_str());
    NoteMissingModelData("getVehicleDummyPosition");
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleDummyPosition(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleDummyPosition", "vehicle", 1).c_str());
    const char* dummy = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!dummy || !KnownDummy(dummy)) return luaL_error(L, "%s", ParserError(L, "setVehicleDummyPosition", "vehicle-dummy", 2).c_str());
    NoteMissingModelData("setVehicleDummyPosition");
    lua_pushboolean(L, 0);
    return 1;
}

int ResetVehicleDummyPositions(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "resetVehicleDummyPositions", "vehicle", 1).c_str());
    NoteMissingModelData("resetVehicleDummyPositions");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleModelExhaustFumesPosition(lua_State* L) {
    uint32_t supplied = 0;
    if (!ReadModelArg(L, "getVehicleModelExhaustFumesPosition", supplied)) return Fail(L);
    uint16_t runtime = 0;
    if (!ResolveStockModel(supplied, runtime) || !IsStandardModel(runtime)) {
        LogFull("getVehicleModelExhaustFumesPosition", "Bad usage", "invalid model ID");
        return Fail(L);
    }
    NoteMissingModelData("getVehicleModelExhaustFumesPosition");
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleModelExhaustFumesPosition(lua_State* L) {
    uint32_t supplied = 0;
    if (!ReadModelArg(L, "setVehicleModelExhaustFumesPosition", supplied)) return Fail(L);
    uint16_t runtime = 0;
    if (!ResolveStockModel(supplied, runtime) || !IsStandardModel(runtime)) {
        LogFull("setVehicleModelExhaustFumesPosition", "Bad usage", "invalid model ID");
        return Fail(L);
    }
    NoteMissingModelData("setVehicleModelExhaustFumesPosition");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleModelWheelSize(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", ParserError(L, "getVehicleModelWheelSize", "number", 1).c_str());
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* group = lua_tostring(L, 2);
        if (!group || (std::strcmp(group, "front_axle") != 0 && std::strcmp(group, "rear_axle") != 0 && std::strcmp(group, "all_wheels") != 0))
            return luaL_error(L, "%s", ParserError(L, "getVehicleModelWheelSize", "resizable-vehicle-wheel-group", 2).c_str());
    } else if (lua_type(L, 2) != LUA_TNONE && lua_type(L, 2) != LUA_TNIL) {
        return luaL_error(L, "%s", ParserError(L, "getVehicleModelWheelSize", "resizable-vehicle-wheel-group", 2).c_str());
    }
    return luaL_error(L, "Invalid model ID");
}

int SetVehicleModelWheelSize(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", ParserError(L, "setVehicleModelWheelSize", "number", 1).c_str());
    const char* group = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (lua_type(L, 2) != LUA_TSTRING)
        return luaL_error(L, "%s", ParserError(L, "setVehicleModelWheelSize", "enum", 2).c_str());
    if (!group || (std::strcmp(group, "front_axle") != 0 && std::strcmp(group, "rear_axle") != 0 && std::strcmp(group, "all_wheels") != 0))
        return luaL_error(L, "%s", ParserError(L, "setVehicleModelWheelSize", "resizable-vehicle-wheel-group", 2).c_str());
    if (!lua_isnumber(L, 3) || std::isnan(lua_tonumber(L, 3)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleModelWheelSize", "number", 3).c_str());
    if (!(lua_tonumber(L, 3) > 0.0)) return luaL_error(L, "Invalid wheel size");
    return luaL_error(L, "Invalid model ID");
}

int GetVehicleEntryPoints(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleEntryPoints", "vehicle", 1).c_str());
    NoteMissingModelData("getVehicleEntryPoints");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleCompatibleUpgrades(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleCompatibleUpgrades", id, model)) return Fail(L);
    (void)id;
    (void)model;
    NoteMissingModelData("getVehicleCompatibleUpgrades");
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleNativeAutoPilotDiagnostic(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleNativeAutoPilotDiagnostic", id, model)) return Fail(L);
    (void)id;
    (void)model;
    lua_pushboolean(L, 0);
    return 1;
}

int SpawnVehicleFlyingComponent(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "spawnVehicleFlyingComponent", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2)) return luaL_error(L, "%s", ParserError(L, "spawnVehicleFlyingComponent", "number", 2).c_str());
    const int node = static_cast<int>(lua_tonumber(L, 2));
    if (node < 1 || node >= 25) return luaL_error(L, "Invalid component index");
    if (lua_type(L, 3) != LUA_TNONE && lua_type(L, 3) != LUA_TNIL) {
        if (!lua_isnumber(L, 3)) return luaL_error(L, "%s", ParserError(L, "spawnVehicleFlyingComponent", "number", 3).c_str());
        const int collision = static_cast<int>(lua_tonumber(L, 3));
        if (collision < 0 || collision >= 6) return luaL_error(L, "Invalid collision type index");
    }
    LUA_LOGI("DIAG spawnVehicleFlyingComponent: CAutomobile::SpawnFlyingComponent is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int NitroMiss(lua_State* L, const char* function) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, function, id, model)) return Fail(L);
    (void)model;
    (void)id;
    return Fail(L);
}

int IsVehicleNitroRecharging(lua_State* L) { return NitroMiss(L, "isVehicleNitroRecharging"); }
int IsVehicleNitroActivated(lua_State* L) { return NitroMiss(L, "isVehicleNitroActivated"); }
int GetVehicleNitroCount(lua_State* L) { return NitroMiss(L, "getVehicleNitroCount"); }
int GetVehicleNitroLevel(lua_State* L) { return NitroMiss(L, "getVehicleNitroLevel"); }
int SetVehicleNitroActivated(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    bool value = false;
    if (!ReadVehicleId(L, "setVehicleNitroActivated", id, model)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleNitroActivated", 2, value)) return Fail(L);
    (void)id;
    (void)model;
    (void)value;
    return Fail(L);
}
int SetVehicleNitroCount(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char count = 0;
    if (!ReadElement(L, "setVehicleNitroCount", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleNitroCount", 2, count)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0 || count > 127) return Fail(L);
    (void)model;
    return Fail(L);
}
int SetVehicleNitroLevel(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double level = 0;
    if (!ReadElement(L, "setVehicleNitroLevel", id)) return Fail(L);
    if (!ReadFinite(L, "setVehicleNitroLevel", 2, level)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    (void)model;
    (void)level;
    return Fail(L);
}

bool IsAdjustableModel(int model) {
    return HasModel(model, {520, 525, 524, 486, 406, 443, 531, 530, 592});
}

bool RadioUpgradeAllowed(int model) {
    const char* type = VehicleTypeOf(model);
    return std::strcmp(type, "Train") != 0 && std::strcmp(type, "Boat") != 0 && std::strcmp(type, "Bike") != 0 &&
           std::strcmp(type, "BMX") != 0 && std::strcmp(type, "Helicopter") != 0 && type[0] != '\0';
}

void PushElementId(lua_State* L, uint32_t id) {
    lua_newtable(L);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "element");
}

int GetVehicleAdjustableProperty(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleAdjustableProperty", id, model)) return Fail(L);
    if (!IsAdjustableModel(model)) return Fail(L);
    lua_pushnumber(L, CopyFlags(id).adjustable);
    return 1;
}

int SetVehicleAdjustableProperty(lua_State* L) {
    uint32_t id = 0;
    unsigned short value = 0;
    if (!ReadElement(L, "setVehicleAdjustableProperty", id)) return Fail(L);
    if (!ReadUnsignedShortArg(L, "setVehicleAdjustableProperty", 2, value)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.adjustable = value;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleTurretPosition(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleTurretPosition", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_pushnumber(L, flags.turretH);
    lua_pushnumber(L, flags.turretV);
    return 2;
}

int SetVehicleTurretPosition(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double horizontal = 0;
    double vertical = 0;
    if (!ReadVehicleId(L, "setVehicleTurretPosition", id, model)) return Fail(L);
    if (!ReadFinite(L, "setVehicleTurretPosition", 2, horizontal)) return Fail(L);
    if (!ReadFinite(L, "setVehicleTurretPosition", 3, vertical)) return Fail(L);
    (void)model;
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.turretH = static_cast<float>(horizontal);
    flags.turretV = static_cast<float>(vertical);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleDoorOpenRatio(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char door = 0;
    if (!ReadVehicleId(L, "getVehicleDoorOpenRatio", id, model)) return Fail(L);
    if (!ReadByteArg(L, "getVehicleDoorOpenRatio", 2, door)) return Fail(L);
    (void)model;
    if (door > 5) return Fail(L);
    lua_pushnumber(L, CopyFlags(id).doorRatio[door]);
    return 1;
}

int SetVehicleDoorOpenRatio(lua_State* L) {
    uint32_t id = 0;
    unsigned char door = 0;
    double ratio = 0;
    if (!ReadElement(L, "setVehicleDoorOpenRatio", id)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleDoorOpenRatio", 2, door)) return Fail(L);
    if (!ReadFinite(L, "setVehicleDoorOpenRatio", 3, ratio)) return Fail(L);
    if (lua_type(L, 4) != LUA_TNONE && lua_type(L, 4) != LUA_TNIL && !lua_isnumber(L, 4)) {
        LogTypeError(L, "setVehicleDoorOpenRatio", "number", 4);
        return Fail(L);
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0 || door > 5) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.doorRatio[door] = static_cast<float>(ratio);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetHeliBladeCollisionsEnabled(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getHeliBladeCollisionsEnabled", id, model)) return Fail(L);
    (void)model;
    if (!CopyFlags(id).heliBlades) return Fail(L);
    lua_pushboolean(L, 1);
    return 1;
}

int SetHeliBladeCollisionsEnabled(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    bool enabled = true;
    if (!ReadVehicleId(L, "setHeliBladeCollisionsEnabled", id, model)) return Fail(L);
    if (!ReadBoolArg(L, "setHeliBladeCollisionsEnabled", 2, enabled)) return Fail(L);
    (void)model;
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.heliBlades = enabled;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int SetHelicopterRotorSpeed(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    double speed = 0;
    if (!ReadVehicleId(L, "setHelicopterRotorSpeed", id, model)) return Fail(L);
    if (!ReadFinite(L, "setHelicopterRotorSpeed", 2, speed)) return Fail(L);
    if (!IsHeliModel(model)) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.heliRotor = static_cast<float>(speed);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleRotorSpeed(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleRotorSpeed", "vehicle", 1).c_str());
    const int model = ServerWorld::ModelOf(ElementIdOf(L, 1));
    const auto flags = CopyFlags(ElementIdOf(L, 1));
    if (IsHeliModel(model)) {
        lua_pushnumber(L, flags.heliRotor);
        return 1;
    }
    if (IsPlaneModel(model)) {
        lua_pushnumber(L, flags.planeRotor);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleRotorSpeed(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleRotorSpeed", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleRotorSpeed", "number", 2).c_str());
    const int model = ServerWorld::ModelOf(ElementIdOf(L, 1));
    if (!IsHeliModel(model) && !IsPlaneModel(model)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    if (IsHeliModel(model)) flags.heliRotor = static_cast<float>(lua_tonumber(L, 2));
    else flags.planeRotor = static_cast<float>(lua_tonumber(L, 2));
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleWindowOpen(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char window = 0;
    if (!ReadVehicleId(L, "isVehicleWindowOpen", id, model)) return Fail(L);
    if (!ReadByteArg(L, "isVehicleWindowOpen", 2, window)) return Fail(L);
    (void)model;
    if (window < 7 && CopyFlags(id).windows[window]) {
        lua_pushboolean(L, 1);
        return 1;
    }
    return Fail(L);
}

int SetVehicleWindowOpen(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char window = 0;
    bool open = false;
    if (!ReadVehicleId(L, "setVehicleWindowOpen", id, model)) return Fail(L);
    if (!ReadByteArg(L, "setVehicleWindowOpen", 2, window)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleWindowOpen", 3, open)) return Fail(L);
    (void)model;
    if (window >= 7) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.windows[window] = open;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int IsVehicleParkingBrakeEnabled(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "isVehicleParkingBrakeEnabled", "vehicle", 1).c_str());
    lua_pushboolean(L, CopyFlags(ElementIdOf(L, 1)).parkingBrake ? 1 : 0);
    return 1;
}

int SetVehicleParkingBrake(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleParkingBrake", "vehicle", 1).c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN)
        return luaL_error(L, "%s", ParserError(L, "setVehicleParkingBrake", "boolean", 2).c_str());
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.parkingBrake = lua_toboolean(L, 2) != 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int SetVehicleDoorsUndamageable(lua_State* L) {
    uint32_t id = 0;
    bool value = false;
    if (!ReadElement(L, "setVehicleDoorsUndamageable", id)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleDoorsUndamageable", 2, value)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.doorsUndamageable = value;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int SetVehicleLoadCollisionFlag(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleLoadCollisionFlag", "vehicle", 1).c_str());
    if (lua_type(L, 2) != LUA_TBOOLEAN)
        return luaL_error(L, "%s", ParserError(L, "setVehicleLoadCollisionFlag", "boolean", 2).c_str());
    NoteNativeVehicleGap();
    auto flags = CopyFlags(ElementIdOf(L, 1));
    flags.loadCollision = lua_toboolean(L, 2) != 0;
    UpdateFlags(ElementIdOf(L, 1), flags);
    lua_pushboolean(L, 1);
    return 1;
}

int SetVehiclePhysicalProofs(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehiclePhysicalProofs", "vehicle", 1).c_str());
    for (int index = 2; index <= 6; ++index) {
        if (lua_type(L, index) != LUA_TBOOLEAN)
            return luaL_error(L, "%s", ParserError(L, "setVehiclePhysicalProofs", "boolean", index).c_str());
    }
    NoteNativeVehicleGap();
    lua_pushboolean(L, 1);
    return 1;
}

int SetVehicleWheelsRotation(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleWheelsRotation", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleWheelsRotation", "number", 2).c_str());
    static bool noted = false;
    if (!noted) {
        noted = true;
        LUA_LOGI("DIAG setVehicleWheelsRotation: CVehicle::SetVehicleWheelRotation is not hooked");
    }
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleStraightLineDistance(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "getVehicleStraightLineDistance", "vehicle", 1).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleStraightLineDistance(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehicleStraightLineDistance", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "setVehicleStraightLineDistance", "number", 2).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleTowedByVehicle(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleTowedByVehicle", id, model)) return Fail(L);
    (void)model;
    const uint32_t towed = CopyFlags(id).towed;
    if (towed == 0) return Fail(L);
    PushElementId(L, towed);
    return 1;
}

int GetVehicleTowingVehicle(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleTowingVehicle", id, model)) return Fail(L);
    (void)model;
    const uint32_t tower = CopyFlags(id).towedBy;
    if (tower == 0) return Fail(L);
    PushElementId(L, tower);
    return 1;
}

int AttachTrailerToVehicle(lua_State* L) {
    int model = 0;
    uint32_t tower = 0;
    if (!ReadVehicleId(L, "attachTrailerToVehicle", tower, model)) return Fail(L);
    if (!IsElementArg(L, 2) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 2)), "vehicle") != 0) {
        LogTypeError(L, "attachTrailerToVehicle", "vehicle", 2);
        return Fail(L);
    }
    for (int index = 3; index <= 5; ++index) {
        if (lua_type(L, index) == LUA_TNONE || lua_type(L, index) == LUA_TNIL) continue;
        if (!lua_isnumber(L, index) || std::isnan(lua_tonumber(L, index))) {
            LogTypeError(L, "attachTrailerToVehicle", "number", index);
            return Fail(L);
        }
    }
    const uint32_t trailer = ElementIdOf(L, 2);
    if (trailer == tower) return Fail(L);
    auto towerFlags = CopyFlags(tower);
    auto trailerFlags = CopyFlags(trailer);
    if (towerFlags.towed != 0 || trailerFlags.towedBy != 0) return Fail(L);
    NoteNativeVehicleGap();
    towerFlags.towed = trailer;
    trailerFlags.towedBy = tower;
    if (IsTrainModel(model) && IsTrainModel(ServerWorld::ModelOf(trailer)) && trailerFlags.towed == 0) towerFlags.chainEngine = true;
    UpdateFlags(tower, towerFlags);
    UpdateFlags(trailer, trailerFlags);
    lua_pushboolean(L, 1);
    return 1;
}

int DetachTrailerFromVehicle(lua_State* L) {
    int model = 0;
    uint32_t tower = 0;
    if (!ReadVehicleId(L, "detachTrailerFromVehicle", tower, model)) return Fail(L);
    uint32_t requested = 0;
    if (lua_type(L, 2) != LUA_TNONE && lua_type(L, 2) != LUA_TNIL) {
        if (!IsElementArg(L, 2) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 2)), "vehicle") != 0) {
            LogTypeError(L, "detachTrailerFromVehicle", "vehicle", 2);
            return Fail(L);
        }
        requested = ElementIdOf(L, 2);
    }
    auto flags = CopyFlags(tower);
    if (flags.towed == 0 || (requested != 0 && flags.towed != requested)) return Fail(L);
    (void)model;
    auto trailer = CopyFlags(flags.towed);
    trailer.towedBy = 0;
    UpdateFlags(flags.towed, trailer);
    flags.towed = 0;
    UpdateFlags(tower, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleUpgradeOnSlot(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    unsigned char slot = 0;
    if (!ReadVehicleId(L, "getVehicleUpgradeOnSlot", id, model)) return Fail(L);
    if (!ReadByteArg(L, "getVehicleUpgradeOnSlot", 2, slot)) return Fail(L);
    (void)model;
    lua_pushnumber(L, slot < 17 ? CopyFlags(id).upgrades[slot] : 0);
    return 1;
}

int GetVehicleUpgrades(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleUpgrades", id, model)) return Fail(L);
    (void)model;
    const auto flags = CopyFlags(id);
    lua_newtable(L);
    int index = 1;
    for (unsigned short upgrade : flags.upgrades) {
        if (upgrade == 0) continue;
        lua_pushinteger(L, index++);
        lua_pushnumber(L, upgrade);
        lua_settable(L, -3);
    }
    return 1;
}

int AddVehicleUpgrade(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, "addVehicleUpgrade", id)) return Fail(L);
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* text = lua_tostring(L, 2);
        if (!text || std::strcmp(text, "all") != 0) {
            LogTypeError(L, "addVehicleUpgrade", "number", 2);
            return Fail(L);
        }
        LUA_LOGI("DIAG addVehicleUpgrade: the full compatibility table is not loaded");
        lua_pushboolean(L, 0);
        return 1;
    }
    unsigned short upgrade = 0;
    if (!ReadUnsignedShortArg(L, "addVehicleUpgrade", 2, upgrade)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    const int model = ServerWorld::ModelOf(id);
    if (upgrade < 1000 || upgrade > 1193 || !RadioUpgradeAllowed(model) || upgrade != 1086) {
        if (upgrade == 1086) return Fail(L);
        LUA_LOGI("DIAG addVehicleUpgrade: upgrade %u compatibility is not loaded", upgrade);
        lua_pushboolean(L, 0);
        return 1;
    }
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.upgrades[10] = 1086;
    flags.nitroInstalled = false;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int RemoveVehicleUpgrade(lua_State* L) {
    uint32_t id = 0;
    unsigned short upgrade = 0;
    if (!ReadElement(L, "removeVehicleUpgrade", id)) return Fail(L);
    if (!ReadUnsignedShortArg(L, "removeVehicleUpgrade", 2, upgrade)) return Fail(L);
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    unsigned char slot = 0;
    if (!SlotFromUpgrade(upgrade, slot)) return Fail(L);
    auto flags = CopyFlags(id);
    if (flags.upgrades[slot] != upgrade) return Fail(L);
    flags.upgrades[slot] = 0;
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

int GetVehicleComponents(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    if (!ReadVehicleId(L, "getVehicleComponents", id, model)) return Fail(L);
    (void)id;
    (void)model;
    static bool noted = false;
    if (!noted) {
        noted = true;
        LUA_LOGI("DIAG getVehicleComponents: the vehicle clump component list is not loaded");
    }
    lua_newtable(L);
    return 1;
}

int ComponentMiss(lua_State* L, const char* function) {
    int model = 0;
    uint32_t id = 0;
    std::string name;
    if (!ReadVehicleId(L, function, id, model)) return Fail(L);
    if (!ReadScriptStringArg(L, function, 2, name)) return Fail(L);
    (void)id;
    (void)model;
    (void)name;
    lua_pushboolean(L, 0);
    return 1;
}

int GetVehicleComponentPosition(lua_State* L) { return ComponentMiss(L, "getVehicleComponentPosition"); }
int GetVehicleComponentRotation(lua_State* L) { return ComponentMiss(L, "getVehicleComponentRotation"); }
int GetVehicleComponentScale(lua_State* L) { return ComponentMiss(L, "getVehicleComponentScale"); }
int GetVehicleComponentVisible(lua_State* L) { return ComponentMiss(L, "getVehicleComponentVisible"); }
int ResetVehicleComponentPosition(lua_State* L) { return ComponentMiss(L, "resetVehicleComponentPosition"); }
int ResetVehicleComponentRotation(lua_State* L) { return ComponentMiss(L, "resetVehicleComponentRotation"); }
int ResetVehicleComponentScale(lua_State* L) { return ComponentMiss(L, "resetVehicleComponentScale"); }

int SetComponentVector(lua_State* L, const char* function, const char* defaultBase) {
    int model = 0;
    uint32_t id = 0;
    std::string name;
    if (!ReadVehicleId(L, function, id, model)) return Fail(L);
    if (!ReadScriptStringArg(L, function, 2, name)) return Fail(L);
    for (int index = 3; index <= 5; ++index) {
        if (!lua_isnumber(L, index) || std::isnan(lua_tonumber(L, index))) {
            LogTypeError(L, function, "number", index);
            return Fail(L);
        }
    }
    if (lua_type(L, 6) != LUA_TNONE && lua_type(L, 6) != LUA_TNIL) {
        const char* base = lua_type(L, 6) == LUA_TSTRING ? lua_tostring(L, 6) : nullptr;
        if (!base || (std::strcmp(base, "root") != 0 && std::strcmp(base, "parent") != 0)) {
            LogTypeError(L, function, "component-base", 6);
            return Fail(L);
        }
    }
    (void)id;
    (void)model;
    (void)name;
    (void)defaultBase;
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleComponentPosition(lua_State* L) { return SetComponentVector(L, "setVehicleComponentPosition", "root"); }
int SetVehicleComponentRotation(lua_State* L) { return SetComponentVector(L, "setVehicleComponentRotation", "parent"); }
int SetVehicleComponentScale(lua_State* L) { return SetComponentVector(L, "setVehicleComponentScale", "parent"); }
int SetVehicleComponentVisible(lua_State* L) {
    int model = 0;
    uint32_t id = 0;
    std::string name;
    bool visible = false;
    if (!ReadVehicleId(L, "setVehicleComponentVisible", id, model)) return Fail(L);
    if (!ReadScriptStringArg(L, "setVehicleComponentVisible", 2, name)) return Fail(L);
    if (!ReadBoolArg(L, "setVehicleComponentVisible", 3, visible)) return Fail(L);
    (void)id;
    (void)model;
    (void)name;
    (void)visible;
    lua_pushboolean(L, 0);
    return 1;
}

int CreateVehicle(lua_State* L) {
    uint32_t model = 0;
    if (!ReadModelArg(L, "createVehicle", model)) return Fail(L);
    for (int index = 2; index <= 4; ++index) {
        if (!lua_isnumber(L, index) || std::isnan(lua_tonumber(L, index))) {
            LogTypeError(L, "createVehicle", "number", index);
            return Fail(L);
        }
    }
    if (ActiveVmOwnerNet() == 0) return Fail(L);
    LUA_LOGI("DIAG createVehicle: client vehicles are not spawned");
    (void)model;
    lua_pushboolean(L, 0);
    return 1;
}

int RequestVehicleRecording(lua_State* L) {
    if (!lua_isnumber(L, 1) || std::isnan(lua_tonumber(L, 1)))
        return luaL_error(L, "%s", ParserError(L, "requestVehicleRecording", "number", 1).c_str());
    if (ActiveVmOwnerNet() == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    LUA_LOGI("DIAG requestVehicleRecording: CVehicleRecording is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int LoadVehicleRecording(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING && lua_type(L, 1) != LUA_TNUMBER)
        return luaL_error(L, "%s", ParserError(L, "loadVehicleRecording", "string", 1).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int IsVehicleRecordingLoaded(lua_State* L) {
    if (!lua_isnumber(L, 1) || std::isnan(lua_tonumber(L, 1)))
        return luaL_error(L, "%s", ParserError(L, "isVehicleRecordingLoaded", "number", 1).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int IsVehiclePlaybackActive(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "isVehiclePlaybackActive", "vehicle", 1).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int StartVehiclePlayback(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "startVehiclePlayback", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "startVehiclePlayback", "number", 2).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int StopVehiclePlayback(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "stopVehiclePlayback", "vehicle", 1).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehiclePlaybackSpeed(lua_State* L) {
    if (!IsElementArg(L, 1) || std::strcmp(ServerWorld::ElementKindName(ElementIdOf(L, 1)), "vehicle") != 0)
        return luaL_error(L, "%s", ParserError(L, "setVehiclePlaybackSpeed", "vehicle", 1).c_str());
    if (!lua_isnumber(L, 2) || std::isnan(lua_tonumber(L, 2)))
        return luaL_error(L, "%s", ParserError(L, "setVehiclePlaybackSpeed", "number", 2).c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int SetVehicleDirtLevel(lua_State* L) {
    uint32_t id = 0;
    if (!ReadElement(L, "setVehicleDirtLevel", id)) return Fail(L);
    if (!lua_isnumber(L, 2)) {
        LogTypeError(L, "setVehicleDirtLevel", "number", 2);
        return Fail(L);
    }
    const lua_Number number = lua_tonumber(L, 2);
    if (std::isnan(number)) {
        LogFull("setVehicleDirtLevel", "Bad argument", "Expected number, got NaN");
        return Fail(L);
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") != 0) return Fail(L);
    NoteNativeVehicleGap();
    auto flags = CopyFlags(id);
    flags.dirt = static_cast<float>(number);
    UpdateFlags(id, flags);
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"getVehicleController", GetVehicleController},
        {"getVehicleMaxPassengers", GetVehicleMaxPassengers},
        {"getVehicleOccupant", GetVehicleOccupant},
        {"getVehicleOccupants", GetVehicleOccupants},
        {"getVehicleModelFromName", GetVehicleModelFromName},
        // CLuaCompatibilityDefs.cpp:55.
        {"getVehicleIDFromName", GetVehicleModelFromName},
        {"getVehicleName", GetVehicleName},
        {"getVehicleNameFromModel", GetVehicleNameFromModel},
        // CLuaCompatibilityDefs.cpp:58.
        {"getVehicleNameFromID", GetVehicleNameFromModel},
        {"getVehicleType", GetVehicleType},
        {"getVehicleUpgradeSlotName", GetVehicleUpgradeSlotName},
        {"isVehicleLocked", IsVehicleLocked},
        {"setVehicleLocked", SetVehicleLocked},
        {"getVehicleDoorLockMode", GetVehicleDoorLockMode},
        {"setVehicleDoorLockMode", SetVehicleDoorLockMode},
        {"getVehicleEngineState", GetVehicleEngineState},
        {"setVehicleEngineState", SetVehicleEngineState},
        {"getVehicleOverrideLights", GetVehicleOverrideLights},
        {"setVehicleOverrideLights", SetVehicleOverrideLights},
        {"getVehiclePaintjob", GetVehiclePaintjob},
        {"setVehiclePaintjob", SetVehiclePaintjob},
        {"getVehiclePlateText", GetVehiclePlateText},
        {"setVehiclePlateText", SetVehiclePlateText},
        {"isVehicleDamageProof", IsVehicleDamageProof},
        {"setVehicleDamageProof", SetVehicleDamageProof},
        {"isVehicleFuelTankExplodable", IsVehicleFuelTankExplodable},
        {"setVehicleFuelTankExplodable", SetVehicleFuelTankExplodable},
        {"isVehicleFrozen", IsVehicleFrozen},
        {"setVehicleFrozen", SetVehicleFrozen},
        {"getVehicleLandingGearDown", GetVehicleLandingGearDown},
        {"setVehicleLandingGearDown", SetVehicleLandingGearDown},
        {"getVehicleSirensOn", GetVehicleSirensOn},
        {"setVehicleSirensOn", SetVehicleSirensOn},
        {"isVehicleTaxiLightOn", IsVehicleTaxiLightOn},
        {"setVehicleTaxiLightOn", SetVehicleTaxiLightOn},
        {"getVehicleTyresCanBurst", GetVehicleTyresCanBurst},
        {"setVehicleTyresCanBurst", SetVehicleTyresCanBurst},
        {"setVehicleDirtLevel", SetVehicleDirtLevel},
        {"getVehicleDoorState", GetVehicleDoorState},
        {"setVehicleDoorState", SetVehicleDoorState},
        {"getVehicleLightState", GetVehicleLightState},
        {"setVehicleLightState", SetVehicleLightState},
        {"getVehiclePanelState", GetVehiclePanelState},
        {"setVehiclePanelState", SetVehiclePanelState},
        {"getVehicleWheelStates", GetVehicleWheelStates},
        {"setVehicleWheelStates", SetVehicleWheelStates},
        {"isVehicleWheelOnGround", IsVehicleWheelOnGround},
        {"getVehicleWheelFrictionState", GetVehicleWheelFrictionState},
        {"isVehicleOnGround", IsVehicleOnGround},
        {"isVehicleOnAllWheels", IsVehicleOnAllWheels},
        {"fixVehicle", FixVehicle},
        {"isVehicleBlown", IsVehicleBlown},
        {"blowVehicle", BlowVehicle},
        {"areVehicleLightsOn", AreVehicleLightsOn},
        {"getVehicleCurrentGear", GetVehicleCurrentGear},
        {"getVehicleTurnVelocity", GetVehicleTurnVelocity},
        {"setVehicleTurnVelocity", SetVehicleTurnVelocity},
        {"getVehicleGravity", GetVehicleGravity},
        {"setVehicleGravity", SetVehicleGravity},
        {"getVehicleHeadLightColor", GetVehicleHeadLightColor},
        {"setVehicleHeadLightColor", SetVehicleHeadLightColor},
        {"getHelicopterRotorSpeed", GetHelicopterRotorSpeed},
        {"isTrainDerailed", IsTrainDerailed},
        {"setTrainDerailed", SetTrainDerailed},
        {"isTrainDerailable", IsTrainDerailable},
        {"setTrainDerailable", SetTrainDerailable},
        {"getTrainDirection", GetTrainDirection},
        {"setTrainDirection", SetTrainDirection},
        {"isTrainChainEngine", IsTrainChainEngine},
        {"getTrainSpeed", GetTrainSpeed},
        {"setTrainSpeed", SetTrainSpeed},
        {"getTrainPosition", GetTrainPosition},
        {"setTrainPosition", SetTrainPosition},
        {"getVehicleColor", GetVehicleColor},
        {"setVehicleColor", SetVehicleColor},
        {"getVehicleVariant", GetVehicleVariant},
        {"setVehicleVariant", SetVehicleVariant},
        {"getVehicleWheelScale", GetVehicleWheelScale},
        {"setVehicleWheelScale", SetVehicleWheelScale},
        {"isVehicleSmokeTrailEnabled", IsVehicleSmokeTrailEnabled},
        {"setVehicleSmokeTrailEnabled", SetVehicleSmokeTrailEnabled},
        {"isVehicleNitroRecharging", IsVehicleNitroRecharging},
        {"isVehicleNitroActivated", IsVehicleNitroActivated},
        {"getVehicleNitroCount", GetVehicleNitroCount},
        {"getVehicleNitroLevel", GetVehicleNitroLevel},
        {"setVehicleNitroActivated", SetVehicleNitroActivated},
        {"setVehicleNitroCount", SetVehicleNitroCount},
        {"setVehicleNitroLevel", SetVehicleNitroLevel},
        {"getVehicleAdjustableProperty", GetVehicleAdjustableProperty},
        {"setVehicleAdjustableProperty", SetVehicleAdjustableProperty},
        {"getVehicleTurretPosition", GetVehicleTurretPosition},
        {"setVehicleTurretPosition", SetVehicleTurretPosition},
        {"getVehicleDoorOpenRatio", GetVehicleDoorOpenRatio},
        {"setVehicleDoorOpenRatio", SetVehicleDoorOpenRatio},
        {"getHeliBladeCollisionsEnabled", GetHeliBladeCollisionsEnabled},
        {"setHeliBladeCollisionsEnabled", SetHeliBladeCollisionsEnabled},
        {"setHelicopterRotorSpeed", SetHelicopterRotorSpeed},
        {"getVehicleRotorSpeed", GetVehicleRotorSpeed},
        {"setVehicleRotorSpeed", SetVehicleRotorSpeed},
        {"isVehicleWindowOpen", IsVehicleWindowOpen},
        {"setVehicleWindowOpen", SetVehicleWindowOpen},
        {"isVehicleParkingBrakeEnabled", IsVehicleParkingBrakeEnabled},
        {"setVehicleParkingBrake", SetVehicleParkingBrake},
        {"setVehicleDoorsUndamageable", SetVehicleDoorsUndamageable},
        {"setVehicleLoadCollisionFlag", SetVehicleLoadCollisionFlag},
        {"setVehiclePhysicalProofs", SetVehiclePhysicalProofs},
        {"setVehicleWheelsRotation", SetVehicleWheelsRotation},
        {"getVehicleStraightLineDistance", GetVehicleStraightLineDistance},
        {"setVehicleStraightLineDistance", SetVehicleStraightLineDistance},
        {"getVehicleTowedByVehicle", GetVehicleTowedByVehicle},
        {"getVehicleTowingVehicle", GetVehicleTowingVehicle},
        {"attachTrailerToVehicle", AttachTrailerToVehicle},
        {"detachTrailerFromVehicle", DetachTrailerFromVehicle},
        {"getVehicleUpgradeOnSlot", GetVehicleUpgradeOnSlot},
        {"getVehicleUpgrades", GetVehicleUpgrades},
        {"addVehicleUpgrade", AddVehicleUpgrade},
        {"removeVehicleUpgrade", RemoveVehicleUpgrade},
        {"getVehicleComponents", GetVehicleComponents},
        {"getVehicleComponentPosition", GetVehicleComponentPosition},
        {"setVehicleComponentPosition", SetVehicleComponentPosition},
        {"getVehicleComponentRotation", GetVehicleComponentRotation},
        {"setVehicleComponentRotation", SetVehicleComponentRotation},
        {"getVehicleComponentScale", GetVehicleComponentScale},
        {"setVehicleComponentScale", SetVehicleComponentScale},
        {"getVehicleComponentVisible", GetVehicleComponentVisible},
        {"setVehicleComponentVisible", SetVehicleComponentVisible},
        {"resetVehicleComponentPosition", ResetVehicleComponentPosition},
        {"resetVehicleComponentRotation", ResetVehicleComponentRotation},
        {"resetVehicleComponentScale", ResetVehicleComponentScale},
        {"createVehicle", CreateVehicle},
        {"requestVehicleRecording", RequestVehicleRecording},
        {"loadVehicleRecording", LoadVehicleRecording},
        {"isVehicleRecordingLoaded", IsVehicleRecordingLoaded},
        {"isVehiclePlaybackActive", IsVehiclePlaybackActive},
        {"startVehiclePlayback", StartVehiclePlayback},
        {"stopVehiclePlayback", StopVehiclePlayback},
        {"setVehiclePlaybackSpeed", SetVehiclePlaybackSpeed},
        {"addVehicleSirens", AddVehicleSirens},
        {"removeVehicleSirens", RemoveVehicleSirens},
        {"getVehicleSirenParams", GetVehicleSirenParams},
        {"getVehicleSirens", GetVehicleSirens},
        {"setVehicleSirens", SetVehicleSirens},
        {"getVehicleRotorState", GetVehicleRotorState},
        {"setVehicleRotorState", SetVehicleRotorState},
        {"getOriginalHandling", GetOriginalHandling},
        {"getVehicleHandling", GetVehicleHandling},
        {"setVehicleHandling", SetVehicleHandling},
        {"getVehicleModelAudioSettings", GetVehicleModelAudioSettings},
        {"getVehicleAudioSettings", GetVehicleAudioSettings},
        {"setVehicleModelAudioSetting", SetVehicleModelAudioSetting},
        {"setVehicleAudioSetting", SetVehicleAudioSetting},
        {"resetVehicleAudioSettings", ResetVehicleAudioSettings},
        {"resetVehicleModelAudioSettings", ResetVehicleModelAudioSettings},
        {"getVehicleModelDummyPosition", GetVehicleModelDummyPosition},
        {"setVehicleModelDummyPosition", SetVehicleModelDummyPosition},
        {"getVehicleModelDummyDefaultPosition", GetVehicleModelDummyDefaultPosition},
        {"getVehicleDummyPosition", GetVehicleDummyPosition},
        {"setVehicleDummyPosition", SetVehicleDummyPosition},
        {"resetVehicleDummyPositions", ResetVehicleDummyPositions},
        {"getVehicleModelExhaustFumesPosition", GetVehicleModelExhaustFumesPosition},
        {"setVehicleModelExhaustFumesPosition", SetVehicleModelExhaustFumesPosition},
        {"getVehicleModelWheelSize", GetVehicleModelWheelSize},
        {"setVehicleModelWheelSize", SetVehicleModelWheelSize},
        {"getVehicleEntryPoints", GetVehicleEntryPoints},
        {"getVehicleCompatibleUpgrades", GetVehicleCompatibleUpgrades},
        {"getVehicleNativeAutoPilotDiagnostic", GetVehicleNativeAutoPilotDiagnostic},
        {"spawnVehicleFlyingComponent", SpawnVehicleFlyingComponent},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void BindVehicleLua(lua_State* L) { Register(L); }

bool VehicleBodyFrozen(uint32_t id) { return VehicleFrozenOf(id); }

}  // namespace WorldRPG::Script

// Strong definition. A weak InstallVehicleLua in another translation unit resolves to this one.
extern "C" void InstallVehicleLua(lua_State* L) { WorldRPG::Script::BindVehicleLua(L); }
