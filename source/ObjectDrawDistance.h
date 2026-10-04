#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ObjectDrawDistance
{
    // Renderer coordinates, not the player's 32x fixed-point coordinates.
    // These finite bounds also keep the legacy pickup distance squares inside int32.
    constexpr float DefaultDistance = 65536.0f;
    constexpr float MinimumDistance = 1024.0f;
    constexpr float MaximumDistance = 65536.0f;
    constexpr size_t ActorCount = 64;
    constexpr size_t BoneCount = 32;
    constexpr size_t ModelTypeCount = 128;
    constexpr size_t RenderSlots = ActorCount + 1; // All loaded actors plus Buzz.
    constexpr size_t ListSlots = RenderSlots + 1; // Include the null terminator.
    constexpr size_t SpriteCapacity = 16384;
    constexpr size_t TransformCapacity = 8192;
    constexpr size_t EntryCapacity = 32768;
    constexpr size_t TriangleCapacity = 32768;

    inline float SanitizeDistance(float value)
    {
        if (!std::isfinite(value) || value <= 0.0f)
            return DefaultDistance;
        if (value < MinimumDistance)
            return MinimumDistance;
        return value > MaximumDistance ? MaximumDistance : value;
    }

    // A byte-exact view of the recovered 0x9c-byte actor slot. Never change its layout.
    struct Actor
    {
        std::array<uint8_t, 0x9C> bytes{};
        template <typename T> T Read(size_t offset) const
        {
            T value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }
        template <typename T> void Write(size_t offset, T value)
        {
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }
    };
    static_assert(sizeof(Actor) == 0x9C);

    inline bool HasRenderableModel(const Actor& actor, const uint8_t* const* models, size_t modelCount,
        const uint8_t* const* metadata)
    {
        const auto type = actor.Read<int16_t>(0x14);
        if (type <= 0 || static_cast<size_t>(type) >= modelCount ||
            static_cast<size_t>(type) >= ModelTypeCount || models == nullptr || metadata == nullptr ||
            models[type] == nullptr || metadata[type] == nullptr)
            return false;
        int16_t renderEnabled;
        int32_t bones;
        std::memcpy(&renderEnabled, metadata[type], sizeof(renderEnabled));
        std::memcpy(&bones, models[type] + 4, sizeof(bones));
        return renderEnabled != 0 && bones > 0 && bones <= static_cast<int32_t>(BoneCount);
    }

    inline bool WithinRange(const Actor& actor, const int32_t* camera, float distance)
    {
        // Subtract before scaling, in double precision: no int32 overflow, FLT_MAX,
        // infinity, or changes to the engine's FPU stack/control word.
        double squared = 0.0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            const double delta = (static_cast<double>(actor.Read<int32_t>(axis * 4)) - camera[axis]) / 32.0;
            squared += delta * delta;
        }
        return squared < static_cast<double>(distance) * distance;
    }

    // Render-only overlay: preserve the native list's order and gameplay flag bits.
    // The original 64-slot activation/update/collision/targeting pools are NOT enlarged.
    struct RenderOverlay
    {
        std::array<Actor*, ListSlots> savedList{};
        std::array<Actor*, RenderSlots> flaggedActors{};
        std::array<uint16_t, RenderSlots> savedFlags{};
        size_t flaggedCount = 0;
        bool active = false;

        template <typename IsVisible>
        bool Begin(std::array<Actor*, ListSlots>& list, Actor* pool, Actor* player,
            const int32_t* camera, float distance, IsVisible isVisible)
        {
            if (active || pool == nullptr || player == nullptr || camera == nullptr)
                return false;
            size_t count = 0;
            while (count < ListSlots && list[count] != nullptr)
                ++count;
            if (count > RenderSlots || count == 0 || list[count - 1] != player)
                return false; // Menu/unrecognized render list: leave it exactly alone.
            for (size_t i = 0; i + 1 < count; ++i)
            {
                bool belongsToPool = false;
                for (size_t slot = 0; slot < ActorCount; ++slot)
                    belongsToPool |= list[i] == pool + slot;
                if (!belongsToPool)
                    return false;
                for (size_t j = 0; j < i; ++j)
                    if (list[j] == list[i])
                        return false;
            }
            savedList = list;
            flaggedCount = 0;
            active = true;
            --count; // Temporarily move Buzz to the end, after any extra actors.
            for (size_t slot = 0; slot < ActorCount; ++slot)
            {
                auto actor = pool + slot;
                // Same live/model-ready gates as UpdateNearbyActors, plus native hide flag.
                if (actor->Read<int16_t>(0x14) <= 0 || actor->Read<int16_t>(0x7E) <= 0 ||
                    (actor->Read<uint16_t>(0x40) & 0x2000) != 0 ||
                    !WithinRange(*actor, camera, distance) || !isVisible(*actor))
                    continue;
                bool present = false;
                for (size_t i = 0; i < count; ++i)
                    present |= list[i] == actor;
                if (!present)
                    list[count++] = actor;
                const auto flags = actor->Read<uint16_t>(0x40);
                if ((flags & 1) == 0)
                {
                    flaggedActors[flaggedCount] = actor;
                    savedFlags[flaggedCount++] = flags;
                    actor->Write<uint16_t>(0x40, flags | 1);
                }
            }
            list[count++] = player;
            list[count] = nullptr;
            return true;
        }

        void End(std::array<Actor*, ListSlots>& list)
        {
            if (!active)
                return;
            for (size_t i = 0; i < flaggedCount; ++i)
            {
                // Restore only our visibility bit; preserve any unrelated native writes.
                const auto current = flaggedActors[i]->Read<uint16_t>(0x40);
                flaggedActors[i]->Write<uint16_t>(0x40, (current & ~uint16_t{1}) | (savedFlags[i] & 1));
            }
            list = savedList;
            flaggedCount = 0;
            active = false;
        }
    };
}
