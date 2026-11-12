// PED (type 14) body of ENTITY_ADD. STUB: implemented by a later stage (S2/S3).
// Until then the packet stops at this element.

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {

bool ReadEntityBodyPed(Net::BitStreamReader& reader, const EntityCommon& common, Visual& visual) {
    (void)reader;
    (void)visual;
    DIAG("ENTITY_ADD body not implemented type=%u id=%u bit=%zu", static_cast<unsigned>(common.type), common.id, reader.GetReadOffsetBits());
    return false;
}

} // namespace WorldRPG::Entity
