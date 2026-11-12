#pragma once

#include "../net/raknet/RakNetWrapper.h"

#include <cmath>
#include <cstdint>

// Native task sync structures ported from mta_neon desktop client
// Shared/sdk/net/SyncStructures.h — exact field names, enum values, and
// bitstream layout preserved for wire compatibility.

namespace WorldRPG::Sync {

// Bit-level helpers. A bit-field has no address, and the bit-stream wrapper has
// no int8 or bool overloads. Widths match the mta_neon wire layout.
namespace detail {
inline bool ReadBitsValue(Net::BitStreamReader& r, unsigned int& value, std::size_t bits)
{
    std::uint8_t raw = 0;
    if (!r.ReadBits(&raw, bits))
        return false;
    value = raw;
    return true;
}
inline void WriteBitsValue(Net::BitStreamWriter& w, unsigned int value, std::size_t bits)
{
    const std::uint8_t raw = static_cast<std::uint8_t>(value);
    w.WriteBits(&raw, bits);
}
inline bool ReadFlag(Net::BitStreamReader& r, bool& flag)
{
    unsigned int raw = 0;
    if (!ReadBitsValue(r, raw, 1))
        return false;
    flag = raw != 0;
    return true;
}
inline void WriteFlag(Net::BitStreamWriter& w, bool flag)
{
    WriteBitsValue(w, flag ? 1u : 0u, 1);
}
}  // namespace detail

/// PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE — matches mta_neon SyncStructures.h:1309
constexpr std::uint8_t PED_SYNC_FLAG2_NATIVE_TASK_ANIMATION_LANE = 0x10;

/// SNativeTaskLocomotionSync — matches mta_neon SyncStructures.h:1023
/// 2-bit mode (NONE/WALK/RUN/SPRINT) + signed stick X/Y when active.
struct SNativeTaskLocomotionSync
{
    enum
    {
        BITCOUNT = 2
    };

    enum eMode : unsigned int
    {
        NONE = 0,
        WALK,
        RUN,
        SPRINT,
    };

    struct Data
    {
        unsigned int uiMode : BITCOUNT;
        short        sLeftStickX;
        short        sLeftStickY;
    } data{};

    bool Read(Net::BitStreamReader& bitStream)
    {
        data = {};
        unsigned int mode = 0;
        if (!detail::ReadBitsValue(bitStream, mode, BITCOUNT))
            return false;
        data.uiMode = mode;

        if (data.uiMode == NONE)
            return true;

        std::uint8_t rawX = 0;
        std::uint8_t rawY = 0;
        if (!bitStream.Read(rawX) || !bitStream.Read(rawY))
            return false;

        const auto leftStickX = static_cast<std::int8_t>(rawX);
        const auto leftStickY = static_cast<std::int8_t>(rawY);
        data.sLeftStickX = static_cast<short>(static_cast<float>(leftStickX) * 128.0f / 127.0f);
        data.sLeftStickY = static_cast<short>(static_cast<float>(leftStickY) * 128.0f / 127.0f);
        return true;
    }

    void Write(Net::BitStreamWriter& bitStream) const
    {
        detail::WriteBitsValue(bitStream, data.uiMode, BITCOUNT);
        if (data.uiMode == NONE)
            return;

        const int8_t leftStickX = static_cast<int8_t>(static_cast<float>(data.sLeftStickX) * 127.0f / 128.0f);
        const int8_t leftStickY = static_cast<int8_t>(static_cast<float>(data.sLeftStickY) * 127.0f / 128.0f);
        bitStream.Write(static_cast<std::uint8_t>(leftStickX));
        bitStream.Write(static_cast<std::uint8_t>(leftStickY));
    }
};

/// SNativeTaskWeaponPresentationSync — matches mta_neon SyncStructures.h:1080
/// 2-bit mode (NONE/FIRE/DRIVEBY) + weapon params when active.
struct SNativeTaskWeaponPresentationSync
{
    enum
    {
        BITCOUNT = 2
    };

    static constexpr unsigned char MAX_DRIVEBY_STYLE = 8;

    enum eMode : unsigned int
    {
        NONE = 0,
        FIRE,
        DRIVEBY,
    };

    struct TargetVector
    {
        float fX;
        float fY;
        float fZ;
    };

    struct Data
    {
        unsigned int  uiMode : BITCOUNT;
        uint8_t       ucWeaponType;
        uint16_t      usBurstLength;
        uint8_t       ucShootingRate;
        TargetVector  vecTarget;
        float         fAbortRange;
        uint8_t       ucFrequencyPercentage;
        uint8_t       ucDriveByStyle;
        bool          bIsLeftHandDriveBy;
    } data{};

    bool Read(Net::BitStreamReader& bitStream)
    {
        data = {};
        unsigned int mode = 0;
        if (!detail::ReadBitsValue(bitStream, mode, BITCOUNT))
            return false;
        data.uiMode = mode;
        if (data.uiMode == NONE)
            return true;
        if (data.uiMode != FIRE && data.uiMode != DRIVEBY)
            return false;

        if (!bitStream.Read(data.ucWeaponType) || !bitStream.Read(data.usBurstLength) || !bitStream.Read(data.ucShootingRate) ||
            !bitStream.Read(data.vecTarget.fX) || !bitStream.Read(data.vecTarget.fY) || !bitStream.Read(data.vecTarget.fZ))
        {
            return false;
        }

        if (data.usBurstLength < 1 || data.usBurstLength > 32767 || !std::isfinite(data.vecTarget.fX) || !std::isfinite(data.vecTarget.fY) ||
            !std::isfinite(data.vecTarget.fZ))
        {
            return false;
        }

        if (data.uiMode == DRIVEBY)
        {
            if (!bitStream.Read(data.fAbortRange) || !bitStream.Read(data.ucFrequencyPercentage) || !bitStream.Read(data.ucDriveByStyle) ||
                !detail::ReadFlag(bitStream, data.bIsLeftHandDriveBy))
            {
                return false;
            }
            if (data.ucDriveByStyle > MAX_DRIVEBY_STYLE)
                return false;
        }

        return true;
    }

    void Write(Net::BitStreamWriter& bitStream) const
    {
        detail::WriteBitsValue(bitStream, data.uiMode, BITCOUNT);
        if (data.uiMode == NONE)
            return;

        bitStream.Write(data.ucWeaponType);
        bitStream.Write(data.usBurstLength);
        bitStream.Write(data.ucShootingRate);
        bitStream.Write(data.vecTarget.fX);
        bitStream.Write(data.vecTarget.fY);
        bitStream.Write(data.vecTarget.fZ);

        if (data.uiMode == DRIVEBY)
        {
            bitStream.Write(data.fAbortRange);
            bitStream.Write(data.ucFrequencyPercentage);
            bitStream.Write(data.ucDriveByStyle);
            detail::WriteFlag(bitStream, data.bIsLeftHandDriveBy);
        }
    }
};

/// SNativeTaskAnimationPresentationSync — matches mta_neon SyncStructures.h:1169
/// 2-bit mode (NONE/ANIMATION/AIRBORNE_ANIMATION/CLIMB_ANIMATION) + anim params.
struct SNativeTaskAnimationPresentationSync
{
    enum
    {
        BITCOUNT = 2
    };

    enum eMode : unsigned int
    {
        NONE = 0,
        ANIMATION,
        // Observers must distinguish a task-owned airborne clip from an
        // ordinary animation. Animation IDs are reused by several GTA task
        // families and cannot safely carry this physical-state semantic.
        AIRBORNE_ANIMATION,
        // Climbing is anchored physical motion rather than a free airborne
        // clip. Its native handhold and phase are carried with the animation
        // so a new owner can reconstruct GTA's task after a handoff.
        CLIMB_ANIMATION,
    };

    static bool IsAnimationMode(unsigned int mode) { return mode == ANIMATION || mode == AIRBORNE_ANIMATION || mode == CLIMB_ANIMATION; }
    static bool IsPhysicalMode(unsigned int mode) { return mode == AIRBORNE_ANIMATION || mode == CLIMB_ANIMATION; }

    struct Data
    {
        unsigned int uiMode : BITCOUNT;
        uint16_t     usAnimGroup;
        uint16_t     usAnimId;
        float        fProgress;
        float        fSpeed;
        float        fBlendAmount;
        float        fHeading;
    } data{};

    bool Read(Net::BitStreamReader& bitStream)
    {
        data = {};
        unsigned int mode = 0;
        if (!detail::ReadBitsValue(bitStream, mode, BITCOUNT))
            return false;
        data.uiMode = mode;
        if (data.uiMode == NONE)
            return true;
        if (!IsAnimationMode(data.uiMode) || !bitStream.Read(data.usAnimGroup) || !bitStream.Read(data.usAnimId) || !bitStream.Read(data.fProgress) ||
            !bitStream.Read(data.fSpeed) || !bitStream.Read(data.fBlendAmount) || !bitStream.Read(data.fHeading))
            return false;

        if (!std::isfinite(data.fProgress) || data.fProgress < 0.0f || data.fProgress > 1.0f || !std::isfinite(data.fSpeed) || data.fSpeed <= 0.0f ||
            data.fSpeed > 16.0f || !std::isfinite(data.fBlendAmount) || data.fBlendAmount < 0.0f || data.fBlendAmount > 1.0f || !std::isfinite(data.fHeading) ||
            data.fHeading < -6.2831855f || data.fHeading > 6.2831855f)
        {
            return false;
        }

        return true;
    }

    void Write(Net::BitStreamWriter& bitStream) const
    {
        detail::WriteBitsValue(bitStream, data.uiMode, BITCOUNT);
        if (data.uiMode == NONE)
            return;

        bitStream.Write(data.usAnimGroup);
        bitStream.Write(data.usAnimId);
        bitStream.Write(data.fProgress);
        bitStream.Write(data.fSpeed);
        bitStream.Write(data.fBlendAmount);
        bitStream.Write(data.fHeading);
    }
};

/// SLastSyncedPedData — matches mta_neon CClientPed.h:119
/// Tracks last synced state for ownership handoff and reset detection.
struct SLastSyncedPedData
{
    float                                fHealth = 100.0f;
    float                                fArmour = 0.0f;
    float                                vPosition[3] = {0.0f, 0.0f, 0.0f};
    float                                vVelocity[3] = {0.0f, 0.0f, 0.0f};
    float                                fRotation = 0.0f;
    float                                cameraRotation = 0.0f;
    bool                                 bOnFire = false;
    bool                                 bIsInWater = false;
    bool                                 isReloadingWeapon = false;
    SNativeTaskLocomotionSync            nativeTaskLocomotion;
    bool                                 nativeTaskLocomotionResetPending = true;
    SNativeTaskWeaponPresentationSync    nativeTaskWeaponPresentation;
    bool                                 nativeTaskWeaponPresentationResetPending = true;
    SNativeTaskAnimationPresentationSync nativeTaskAnimationPresentation;
    bool                                 nativeTaskAnimationPresentationResetPending = true;
    bool                                 nativeTaskAnimationPresentationWasPhysical = false;
};

} // namespace WorldRPG::Sync