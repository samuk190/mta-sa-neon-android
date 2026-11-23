// A small in-process WMI: CoCreateInstance(CLSID_WbemLocator) and the COM
// object tree behind it (IWbemLocator -> IWbemServices -> IEnumWbemClassObject
// -> IWbemClassObject), answering the hardware classes netc fingerprints.
//
// Every Windows install has WMI, and netc's hardware-id code assumes a query
// returns at least one instance; without this it dereferences an empty result
// vector. Values derive from the per-install machine seed and agree with the
// other identity shims (disk IOCTL serial, adapter MAC, volume serial).

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace NetEmu {

namespace {

constexpr uint32_t kSOk = 0;
constexpr uint32_t kSFalse = 1;  // WBEM_S_FALSE: fewer objects than asked for
constexpr uint32_t kENoInterface = 0x80004002u;
constexpr uint32_t kENotImpl = 0x80004001u;
constexpr uint32_t kEPointer = 0x80004003u;
constexpr uint32_t kWbemEInvalidClass = 0x80041010u;
constexpr uint32_t kWbemEInvalidQuery = 0x80041017u;
constexpr uint32_t kClassNotReg = 0x80040154u;

// VARIANT types and CIM types used here.
constexpr uint16_t kVtNull = 1, kVtI4 = 3, kVtBstr = 8, kVtBool = 11;
constexpr uint32_t kCimSint32 = 3, kCimString = 8, kCimBoolean = 11, kCimUint16 = 18, kCimUint32 = 19, kCimUint64 = 21;

struct Value {
    enum class Type { Null, String, Int, Bool, Uint64 } type = Type::Null;
    std::string s;
    int64_t i = 0;
    static Value Str(std::string v) { return {Type::String, std::move(v), 0}; }
    static Value Int(int64_t v) { return {Type::Int, {}, v}; }
    static Value Bool(bool v) { return {Type::Bool, {}, v ? 1 : 0}; }
    static Value U64(uint64_t v) { return {Type::Uint64, std::to_string(v), static_cast<int64_t>(v)}; }
};
using Row = std::vector<std::pair<std::string, Value>>;  // property order is the class's own

struct WmiObject {
    enum class Kind { Locator, Services, Enum, Instance };
    WmiObject(Kind k) : kind(k) {}  // NOLINT: implicit so NewObject({Kind::X}) reads naturally
    Kind kind;
    uint32_t refs = 1;
    std::string className;   // Instance: its class; Enum: the queried class
    std::vector<Row> rows;   // Enum: the result set
    size_t cursor = 0;       // Enum: next row; Instance: property enumeration
    Row row;                 // Instance: its properties
};

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

uint64_t Mix(uint64_t seed, uint64_t salt) {
    uint64_t z = seed + salt * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string Hex(uint64_t v, int digits) {
    char b[24];
    std::snprintf(b, sizeof(b), "%0*llX", digits, static_cast<unsigned long long>(v));
    return std::string(b).substr(0, static_cast<size_t>(digits));
}

// "SELECT a, b FROM Win32_X WHERE ..." -> "win32_x"; also plain class names
// (CreateInstanceEnum).
std::string QueryClass(const std::string& query) {
    const std::string q = Lower(query);
    size_t from = q.find(" from ");
    if (from == std::string::npos) {
        if (q.find(' ') == std::string::npos) return q;
        return {};
    }
    from += 6;
    while (from < q.size() && q[from] == ' ') ++from;
    size_t end = from;
    while (end < q.size() && (std::isalnum(static_cast<unsigned char>(q[end])) || q[end] == '_')) ++end;
    return q.substr(from, end - from);
}

class Wmi {
public:
    explicit Wmi(Win32State& st) : st_(st), mem_(st.machine.Mem()) {}

    void Install();

private:
    Win32State& st_;
    GuestMemory& mem_;
    std::unordered_map<uint32_t, WmiObject> objects_;  // keyed by guest object address
    uint32_t locatorVtbl_ = 0, servicesVtbl_ = 0, enumVtbl_ = 0, instanceVtbl_ = 0;

    uint32_t Bstr(const std::string& utf8) {
        // Same layout as SysAllocString: length prefix, UTF-16, NUL.
        std::u16string w;
        for (size_t i = 0; i < utf8.size(); ++i) w.push_back(static_cast<unsigned char>(utf8[i]));
        const uint32_t bytes = static_cast<uint32_t>(w.size() * 2);
        const uint32_t p = mem_.HeapAlloc(bytes + 6, true);
        if (!p) return 0;
        mem_.Write32(p, bytes);
        for (size_t i = 0; i < w.size(); ++i) mem_.Write16(p + 4 + static_cast<uint32_t>(i) * 2, w[i]);
        return p + 4;
    }

    uint32_t NewObject(WmiObject obj) {
        const uint32_t vtbl = obj.kind == WmiObject::Kind::Locator    ? locatorVtbl_
                              : obj.kind == WmiObject::Kind::Services ? servicesVtbl_
                              : obj.kind == WmiObject::Kind::Enum     ? enumVtbl_
                                                                      : instanceVtbl_;
        const uint32_t va = mem_.HeapAlloc(16, true);
        if (!va) return 0;
        mem_.Write32(va, vtbl);
        objects_.emplace(va, std::move(obj));
        return va;
    }
    WmiObject* Get(uint32_t va) {
        auto it = objects_.find(va);
        return it == objects_.end() ? nullptr : &it->second;
    }

    uint32_t MakeVtbl(const char* iface, const std::vector<std::pair<int, ShimFn>>& methods) {
        const uint32_t vtbl = mem_.HeapAlloc(static_cast<uint32_t>(methods.size() * 4), true);
        for (size_t i = 0; i < methods.size(); ++i) {
            const std::string label = std::string(iface) + "::" + std::to_string(i);
            mem_.Write32(vtbl + static_cast<uint32_t>(i) * 4,
                         st_.machine.MakeThunk(label, methods[i].first * 4, methods[i].second));
        }
        return vtbl;
    }
    // A method the tree never needs: correct stack cleanup, E_NOTIMPL, logged once.
    std::pair<int, ShimFn> NotImpl(const char* name, int args) {
        return {args, [this, name](ShimCall& c) {
                    st_.NoteStub(name);
                    c.Return(kENotImpl);
                }};
    }
    std::vector<std::pair<int, ShimFn>> Unknown() {
        return {
            {3, [this](ShimCall& c) {  // QueryInterface: every object answers for itself
                 if (!c.Arg(2)) return c.Return(kEPointer);
                 WmiObject* o = Get(c.Arg(0));
                 if (!o) {
                     c.mem.Write32(c.Arg(2), 0);
                     return c.Return(kENoInterface);
                 }
                 ++o->refs;
                 c.mem.Write32(c.Arg(2), c.Arg(0));
                 c.Return(kSOk);
             }},
            {1, [this](ShimCall& c) {  // AddRef
                 WmiObject* o = Get(c.Arg(0));
                 c.Return(o ? ++o->refs : 1);
             }},
            {1, [this](ShimCall& c) {  // Release
                 WmiObject* o = Get(c.Arg(0));
                 if (!o) return c.Return(0);
                 const uint32_t left = --o->refs;
                 if (!left) {
                     objects_.erase(c.Arg(0));
                     c.mem.HeapFree(c.Arg(0));
                 }
                 c.Return(left);
             }},
        };
    }

    void WriteVariant(uint32_t va, const Value& v, uint32_t typeVa) {
        mem_.Fill(va, 0, 16);
        uint32_t cim = kCimString;
        switch (v.type) {
        case Value::Type::Null:
            mem_.Write16(va, kVtNull);
            break;
        case Value::Type::String:
        case Value::Type::Uint64:  // WMI hands 64-bit integers back as strings
            mem_.Write16(va, kVtBstr);
            mem_.Write32(va + 8, Bstr(v.s));
            cim = v.type == Value::Type::Uint64 ? kCimUint64 : kCimString;
            break;
        case Value::Type::Int:
            mem_.Write16(va, kVtI4);
            mem_.Write32(va + 8, static_cast<uint32_t>(v.i));
            cim = v.i >= 0 && v.i <= 0xFFFF ? kCimUint16 : (v.i < 0 ? kCimSint32 : kCimUint32);
            break;
        case Value::Type::Bool:
            mem_.Write16(va, kVtBool);
            mem_.Write16(va + 8, v.i ? 0xFFFF : 0);
            cim = kCimBoolean;
            break;
        }
        if (typeVa) mem_.Write32(typeVa, cim);
    }

    std::vector<Row> Rows(const std::string& cls);
};

std::vector<Row> Wmi::Rows(const std::string& cls) {
    const uint64_t s = st_.machineSeed;
    const std::string diskSerial = "SAO" + Hex(s, 16);  // matches IOCTL_STORAGE_QUERY_PROPERTY
    char mac[18];
    std::snprintf(mac, sizeof(mac), "02:%02X:%02X:%02X:%02X:%02X", static_cast<unsigned>(s & 0xFF),
                  static_cast<unsigned>((s >> 8) & 0xFF), static_cast<unsigned>((s >> 16) & 0xFF),
                  static_cast<unsigned>((s >> 24) & 0xFF), static_cast<unsigned>((s >> 32) & 0xFF));
    const uint64_t u1 = Mix(s, 1), u2 = Mix(s, 2);
    char uuid[40];
    std::snprintf(uuid, sizeof(uuid), "%08X-%04X-%04X-%04X-%012llX", static_cast<uint32_t>(u1),
                  static_cast<uint32_t>((u1 >> 32) & 0xFFFF), static_cast<uint32_t>(0x4000 | ((u1 >> 48) & 0x0FFF)),
                  static_cast<uint32_t>(0x8000 | (u2 & 0x3FFF)), static_cast<unsigned long long>((u2 >> 16) & 0xFFFFFFFFFFFFull));
    const std::string board = Hex(Mix(s, 3), 12), bios = Hex(Mix(s, 4), 10), osSerial = "00330-" + Hex(Mix(s, 5), 5) + "-00000-AAOEM";
    const std::string& host = st_.env.computerName;

    if (cls == "win32_diskdrive")
        return {{{"Caption", Value::Str("SAONLINE VIRTUAL DISK")},
                 {"DeviceID", Value::Str("\\\\.\\PHYSICALDRIVE0")},
                 {"Index", Value::Int(0)},
                 {"InterfaceType", Value::Str("SCSI")},
                 {"Manufacturer", Value::Str("(Standard disk drives)")},
                 {"MediaType", Value::Str("Fixed hard disk media")},
                 {"Model", Value::Str("SAONLINE VIRTUAL DISK")},
                 {"PNPDeviceID", Value::Str("SCSI\\DISK&VEN_NVME&PROD_SAONLINE\\5&" + Hex(Mix(s, 6), 8) + "&0&000000")},
                 {"SerialNumber", Value::Str(diskSerial)},
                 {"Signature", Value::Int(static_cast<int64_t>(static_cast<uint32_t>(Mix(s, 7)) & 0x7FFFFFFF))},
                 {"Size", Value::U64(1000202273280ull)},
                 {"Status", Value::Str("OK")}}};
    if (cls == "win32_physicalmedia")
        return {{{"SerialNumber", Value::Str(diskSerial)}, {"Tag", Value::Str("\\\\.\\PHYSICALDRIVE0")}}};
    if (cls == "win32_baseboard")
        return {{{"Manufacturer", Value::Str(Hw::kVendor)},
                 {"Product", Value::Str(Hw::kBoardProduct)},
                 {"SerialNumber", Value::Str(board)},
                 {"Version", Value::Str("1.0")}}};
    if (cls == "win32_bios")
        return {{{"Manufacturer", Value::Str(Hw::kVendor)},
                 {"Name", Value::Str("SAONLINE BIOS")},
                 {"ReleaseDate", Value::Str(Hw::kBiosDateWmi)},
                 {"SMBIOSBIOSVersion", Value::Str(Hw::kBiosVersion)},
                 {"SerialNumber", Value::Str(bios)},
                 {"Version", Value::Str("SAO - 1")}}};
    if (cls == "win32_processor")
        return {{{"Caption", Value::Str(Hw::kCpuIdentifier)},
                 {"Description", Value::Str(Hw::kCpuIdentifier)},
                 {"DeviceID", Value::Str("CPU0")},
                 {"Family", Value::Int(205)},
                 {"L2CacheSize", Value::Int(1536)},
                 {"L3CacheSize", Value::Int(9216)},
                 {"Manufacturer", Value::Str("GenuineIntel")},
                 {"MaxClockSpeed", Value::Int(Hw::kCpuMhz)},
                 {"Name", Value::Str(Hw::kCpuName)},
                 {"NumberOfCores", Value::Int(4)},
                 {"NumberOfLogicalProcessors", Value::Int(4)},
                 {"ProcessorId", Value::Str("BFEBFBFF000906EA")}}};
    if (cls == "win32_computersystemproduct")
        return {{{"IdentifyingNumber", Value::Str(board)},
                 {"Name", Value::Str(Hw::kSystemProduct)},
                 {"UUID", Value::Str(uuid)},
                 {"Vendor", Value::Str(Hw::kVendor)}}};
    if (cls == "win32_computersystem")
        return {{{"Domain", Value::Str("WORKGROUP")},
                 {"Manufacturer", Value::Str(Hw::kVendor)},
                 {"Model", Value::Str(Hw::kSystemProduct)},
                 {"Name", Value::Str(host)},
                 {"NumberOfProcessors", Value::Int(1)},
                 {"PartOfDomain", Value::Bool(false)},
                 {"TotalPhysicalMemory", Value::U64(8ull << 30)}}};
    if (cls == "win32_operatingsystem")
        return {{{"BuildNumber", Value::Str("19045")},
                 {"Caption", Value::Str("Microsoft Windows 10 Pro")},
                 {"CSName", Value::Str(host)},
                 {"InstallDate", Value::Str("20240101000000.000000+000")},
                 {"OSArchitecture", Value::Str("64-bit")},
                 {"SerialNumber", Value::Str(osSerial)},
                 {"Version", Value::Str("10.0.19045")}}};
    if (cls == "win32_networkadapter" || cls == "win32_networkadapterconfiguration")
        return {{{"AdapterType", Value::Str("Ethernet 802.3")},
                 {"Description", Value::Str("SAONLINE Virtual Ethernet")},
                 {"GUID", Value::Str("{SAONLINE-NIC-0001}")},
                 {"Index", Value::Int(1)},
                 {"IPEnabled", Value::Bool(true)},
                 {"MACAddress", Value::Str(mac)},
                 {"Manufacturer", Value::Str(Hw::kVendor)},
                 {"Name", Value::Str("SAONLINE Virtual Ethernet")},
                 {"NetEnabled", Value::Bool(true)},
                 {"PhysicalAdapter", Value::Bool(true)},
                 {"PNPDeviceID", Value::Str("PCI\\VEN_8086&DEV_15B8&SUBSYS_00000000&REV_00\\" + Hex(Mix(s, 8), 12))}}};
    if (cls == "win32_videocontroller")
        return {{{"AdapterCompatibility", Value::Str(Hw::kVendor)},
                 {"AdapterDACType", Value::Str("Internal")},
                 {"AdapterRAM", Value::Int(0x40000000)},
                 {"Caption", Value::Str("SAONLINE Virtual Display")},
                 {"Description", Value::Str("SAONLINE Virtual Display")},
                 {"DriverVersion", Value::Str("31.0.101.2111")},
                 {"Name", Value::Str("SAONLINE Virtual Display")},
                 {"PNPDeviceID", Value::Str("PCI\\VEN_8086&DEV_3E92&SUBSYS_00000000&REV_00\\3&11583659&0&10")},
                 {"VideoProcessor", Value::Str("SAONLINE Virtual Display")}}};
    if (cls == "win32_keyboard")
        return {{{"Description", Value::Str("Enhanced (101- or 102-key)")},
                 {"DeviceID", Value::Str("HID\\VID_046D&PID_C31C&MI_00\\7&" + Hex(Mix(s, 9), 8) + "&0&0000")},
                 {"Layout", Value::Str("00000409")},
                 {"Name", Value::Str("Enhanced (101- or 102-key)")},
                 {"NumberOfFunctionKeys", Value::Int(12)},
                 {"Status", Value::Str("OK")}}};
    if (cls == "win32_physicalmemory")
        return {{{"BankLabel", Value::Str("BANK 0")},
                 {"Capacity", Value::U64(8ull << 30)},
                 {"DeviceLocator", Value::Str("DIMM 0")},
                 {"Manufacturer", Value::Str(Hw::kVendor)},
                 {"PartNumber", Value::Str("SAO-DDR4-8G")},
                 {"SerialNumber", Value::Str(Hex(Mix(s, 10), 8))},
                 {"Speed", Value::Int(3200)}}};
    if (cls == "win32_sounddevice")
        return {{{"Caption", Value::Str("High Definition Audio Device")},
                 {"DeviceID", Value::Str("HDAUDIO\\FUNC_01&VEN_10EC&DEV_0887&SUBSYS_00000000&REV_1003\\4&" + Hex(Mix(s, 11), 8) + "&0&0001")},
                 {"Manufacturer", Value::Str("Microsoft")},
                 {"Name", Value::Str("High Definition Audio Device")},
                 {"ProductName", Value::Str("High Definition Audio Device")},
                 {"Status", Value::Str("OK")}}};
    if (cls == "win32_logicaldisk")
        return {{{"DeviceID", Value::Str("C:")},
                 {"DriveType", Value::Int(3)},
                 {"FileSystem", Value::Str("NTFS")},
                 {"VolumeName", Value::Str("System")},
                 {"VolumeSerialNumber", Value::Str(Hex(static_cast<uint32_t>(s), 8))}}};
    return {};
}

void Wmi::Install() {
    Machine& m = st_.machine;

    auto locator = Unknown();
    locator.push_back({9, [this](ShimCall& c) {  // ConnectServer(this, ns, user, pw, locale, flags, authority, ctx, ppSvc)
                           const std::string ns = c.Arg(1) ? c.mem.ReadW(c.Arg(1)) : std::string();
                           NETEMU_LOGI("WMI ConnectServer %s", ns.c_str());
                           if (!c.Arg(8)) return c.Return(kEPointer);
                           c.mem.Write32(c.Arg(8), NewObject({WmiObject::Kind::Services}));
                           c.Return(kSOk);
                       }});
    locatorVtbl_ = MakeVtbl("IWbemLocator", locator);

    // ExecQuery / CreateInstanceEnum share one result builder.
    auto enumFor = [this](ShimCall& c, const std::string& query, uint32_t out) {
        const std::string cls = QueryClass(query);
        if (!out) return c.Return(kEPointer);
        c.mem.Write32(out, 0);
        if (cls.empty()) {
            NETEMU_LOGW("WMI query not understood: %s", query.c_str());
            return c.Return(kWbemEInvalidQuery);
        }
        std::vector<Row> rows = Rows(cls);
        NETEMU_LOGI("WMI query \"%s\" -> %zu instance(s)", query.c_str(), rows.size());
        if (rows.empty() && cls.rfind("win32_", 0) != 0) return c.Return(kWbemEInvalidClass);
        WmiObject e{WmiObject::Kind::Enum};
        e.className = cls;
        e.rows = std::move(rows);
        c.mem.Write32(out, NewObject(std::move(e)));
        c.Return(kSOk);
    };

    auto services = Unknown();
    services.push_back(NotImpl("IWbemServices::OpenNamespace", 6));
    services.push_back(NotImpl("IWbemServices::CancelAsyncCall", 2));
    services.push_back(NotImpl("IWbemServices::QueryObjectSink", 3));
    services.push_back(NotImpl("IWbemServices::GetObject", 6));
    services.push_back(NotImpl("IWbemServices::GetObjectAsync", 5));
    services.push_back(NotImpl("IWbemServices::PutClass", 5));
    services.push_back(NotImpl("IWbemServices::PutClassAsync", 5));
    services.push_back(NotImpl("IWbemServices::DeleteClass", 5));
    services.push_back(NotImpl("IWbemServices::DeleteClassAsync", 5));
    services.push_back(NotImpl("IWbemServices::CreateClassEnum", 5));
    services.push_back(NotImpl("IWbemServices::CreateClassEnumAsync", 5));
    services.push_back(NotImpl("IWbemServices::PutInstance", 5));
    services.push_back(NotImpl("IWbemServices::PutInstanceAsync", 5));
    services.push_back(NotImpl("IWbemServices::DeleteInstance", 5));
    services.push_back(NotImpl("IWbemServices::DeleteInstanceAsync", 5));
    services.push_back({5, [enumFor](ShimCall& c) {  // CreateInstanceEnum(this, class, flags, ctx, ppEnum)
                            enumFor(c, c.Arg(1) ? c.mem.ReadW(c.Arg(1)) : std::string(), c.Arg(4));
                        }});
    services.push_back(NotImpl("IWbemServices::CreateInstanceEnumAsync", 5));
    services.push_back({6, [enumFor](ShimCall& c) {  // ExecQuery(this, lang, query, flags, ctx, ppEnum)
                            enumFor(c, c.Arg(2) ? c.mem.ReadW(c.Arg(2)) : std::string(), c.Arg(5));
                        }});
    services.push_back(NotImpl("IWbemServices::ExecQueryAsync", 6));
    services.push_back(NotImpl("IWbemServices::ExecNotificationQuery", 6));
    services.push_back(NotImpl("IWbemServices::ExecNotificationQueryAsync", 6));
    services.push_back(NotImpl("IWbemServices::ExecMethod", 8));
    services.push_back(NotImpl("IWbemServices::ExecMethodAsync", 7));
    servicesVtbl_ = MakeVtbl("IWbemServices", services);

    auto enumerator = Unknown();
    enumerator.push_back({1, [this](ShimCall& c) {  // Reset
                              if (WmiObject* o = Get(c.Arg(0))) o->cursor = 0;
                              c.Return(kSOk);
                          }});
    enumerator.push_back({5, [this](ShimCall& c) {  // Next(this, timeout, count, apObjects, puReturned)
                              WmiObject* o = Get(c.Arg(0));
                              const uint32_t want = c.Arg(2), out = c.Arg(3), returned = c.Arg(4);
                              if (!o || !out || !returned) return c.Return(kEPointer);
                              uint32_t n = 0;
                              while (n < want && o->cursor < o->rows.size()) {
                                  WmiObject inst{WmiObject::Kind::Instance};
                                  inst.className = o->className;
                                  inst.row = o->rows[o->cursor++];
                                  // NewObject may rehash objects_; re-find the enumerator afterwards.
                                  const uint32_t enumVa = c.Arg(0);
                                  c.mem.Write32(out + n * 4, NewObject(std::move(inst)));
                                  o = Get(enumVa);
                                  ++n;
                              }
                              c.mem.Write32(returned, n);
                              c.Return(n == want ? kSOk : kSFalse);
                          }});
    enumerator.push_back(NotImpl("IEnumWbemClassObject::NextAsync", 3));
    enumerator.push_back(NotImpl("IEnumWbemClassObject::Clone", 2));
    enumerator.push_back({3, [this](ShimCall& c) {  // Skip(this, timeout, count)
                              WmiObject* o = Get(c.Arg(0));
                              if (!o) return c.Return(kEPointer);
                              const size_t left = o->rows.size() - o->cursor;
                              const size_t skip = std::min<size_t>(left, c.Arg(2));
                              o->cursor += skip;
                              c.Return(skip == c.Arg(2) ? kSOk : kSFalse);
                          }});
    enumVtbl_ = MakeVtbl("IEnumWbemClassObject", enumerator);

    auto instance = Unknown();
    instance.push_back(NotImpl("IWbemClassObject::GetQualifierSet", 2));
    instance.push_back({6, [this](ShimCall& c) {  // Get(this, name, flags, pVal, pType, plFlavor)
                            WmiObject* o = Get(c.Arg(0));
                            if (!o || !c.Arg(1)) return c.Return(kEPointer);
                            const std::string name = Lower(c.mem.ReadW(c.Arg(1)));
                            const Value* v = nullptr;
                            Value cls = Value::Str(o->className);
                            if (name == "__class") v = &cls;
                            for (const auto& [k, val] : o->row)
                                if (Lower(k) == name) v = &val;
                            // A property the class has but this machine leaves empty
                            // reads as VT_NULL, like real WMI.
                            static const Value kNull{};
                            if (!v) {
                                NETEMU_LOGW("WMI %s.%s: not provided, returning NULL", o->className.c_str(), name.c_str());
                                v = &kNull;
                            }
                            if (c.Arg(3)) WriteVariant(c.Arg(3), *v, c.Arg(4));
                            if (c.Arg(5)) c.mem.Write32(c.Arg(5), 0);  // WBEM_FLAVOR_ORIGIN_LOCAL
                            c.Return(kSOk);
                        }});
    instance.push_back(NotImpl("IWbemClassObject::Put", 5));
    instance.push_back(NotImpl("IWbemClassObject::Delete", 2));
    instance.push_back(NotImpl("IWbemClassObject::GetNames", 5));
    instance.push_back({2, [this](ShimCall& c) {  // BeginEnumeration(this, flags)
                            if (WmiObject* o = Get(c.Arg(0))) o->cursor = 0;
                            c.Return(kSOk);
                        }});
    instance.push_back({6, [this](ShimCall& c) {  // Next(this, flags, pstrName, pVal, pType, plFlavor)
                            WmiObject* o = Get(c.Arg(0));
                            if (!o) return c.Return(kEPointer);
                            if (o->cursor >= o->row.size()) return c.Return(0x40005u);  // WBEM_S_NO_MORE_DATA
                            const auto& [k, v] = o->row[o->cursor++];
                            if (c.Arg(2)) c.mem.Write32(c.Arg(2), Bstr(k));
                            if (c.Arg(3)) WriteVariant(c.Arg(3), v, c.Arg(4));
                            if (c.Arg(5)) c.mem.Write32(c.Arg(5), 0);
                            c.Return(kSOk);
                        }});
    instance.push_back({1, [](ShimCall& c) { c.Return(kSOk); }});  // EndEnumeration
    instance.push_back(NotImpl("IWbemClassObject::GetPropertyQualifierSet", 3));
    instance.push_back(NotImpl("IWbemClassObject::Clone", 2));
    instance.push_back(NotImpl("IWbemClassObject::GetObjectText", 3));
    instance.push_back(NotImpl("IWbemClassObject::SpawnDerivedClass", 3));
    instance.push_back(NotImpl("IWbemClassObject::SpawnInstance", 3));
    instance.push_back(NotImpl("IWbemClassObject::CompareTo", 3));
    instance.push_back(NotImpl("IWbemClassObject::GetPropertyOrigin", 3));
    instance.push_back(NotImpl("IWbemClassObject::InheritsFrom", 2));
    instance.push_back(NotImpl("IWbemClassObject::GetMethod", 5));
    instance.push_back(NotImpl("IWbemClassObject::PutMethod", 5));
    instance.push_back(NotImpl("IWbemClassObject::DeleteMethod", 2));
    instance.push_back(NotImpl("IWbemClassObject::BeginMethodEnumeration", 2));
    instance.push_back(NotImpl("IWbemClassObject::NextMethod", 5));
    instance.push_back(NotImpl("IWbemClassObject::EndMethodEnumeration", 1));
    instance.push_back(NotImpl("IWbemClassObject::GetMethodQualifierSet", 3));
    instance.push_back(NotImpl("IWbemClassObject::GetMethodOrigin", 3));
    instanceVtbl_ = MakeVtbl("IWbemClassObject", instance);

    constexpr char kO[] = "ole32.dll";
    m.RegisterShim(kO, "CoInitializeEx", 8, [](ShimCall& c) { c.Return(kSOk); });
    m.RegisterShim(kO, "CoInitializeSecurity", 36, [](ShimCall& c) { c.Return(kSOk); });
    m.RegisterShim(kO, "CoSetProxyBlanket", 32, [](ShimCall& c) { c.Return(kSOk); });
    m.RegisterShim(kO, "CoCreateInstance", 20, [this](ShimCall& c) {  // (clsid, outer, ctx, iid, ppv)
        static constexpr uint8_t kWbemLocator[16] = {0x11, 0xF8, 0x90, 0x45, 0x3A, 0x1D, 0xD0, 0x11,
                                                     0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24};
        uint8_t clsid[16] = {};
        if (c.Arg(0)) c.mem.ReadBytes(c.Arg(0), clsid, sizeof(clsid));
        if (!c.Arg(4)) return c.Return(kEPointer);
        if (std::memcmp(clsid, kWbemLocator, sizeof(clsid)) == 0) {
            c.mem.Write32(c.Arg(4), NewObject({WmiObject::Kind::Locator}));
            return c.Return(kSOk);
        }
        char label[80];
        std::snprintf(label, sizeof(label), "CoCreateInstance {%02X%02X%02X%02X-...-%02X%02X}", clsid[3], clsid[2], clsid[1],
                      clsid[0], clsid[14], clsid[15]);
        st_.NoteStub(label);
        c.mem.Write32(c.Arg(4), 0);
        c.Return(kClassNotReg);
    });
}

}  // namespace

void InstallWmi(Win32State& s) {
    // Lives as long as the shim state: the registered thunks capture it.
    auto wmi = std::make_shared<Wmi>(s);
    s.keepAlive.push_back(wmi);
    wmi->Install();
}

}  // namespace NetEmu
