// VEHICLE (type 2) body of ENTITY_ADD. STUB: implemented by a later stage (S2/S3).
// Until then the packet stops at this element.

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {

bool ReadEntityBodyVehicle(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    (void)reader;
    (void)visual;
    DIAG("ENTITY_ADD body not implemented type=%u id=%u bit=%zu", static_cast<unsigned>(common.type), common.id, reader.GetReadOffsetBits());
    return false;
}

} // namespace WorldRPG::Entity
