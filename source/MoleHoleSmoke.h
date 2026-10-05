#pragma once

#include "ObjectDrawDistance.h"
#include <algorithm>
#include <limits>

namespace MoleHoleSmoke
{
    constexpr size_t HoleCount = 7;
    constexpr size_t Capacity = 64; // Separate visual storage, never gameplay particle slots.
    constexpr int Lifetime = 48;
    constexpr int32_t Height = -0xBB0; // Exact PUSH 0xFFFFF450 at the native emitter.
    constexpr float NativeDistance = 4096.0f;
    constexpr float MaximumDistance = 48000.0f;
    constexpr int64_t NativeDistanceSquared = 0x40000; // Native fixed-point deltas >> 8.

    struct Hole { int32_t x, y, z; };
    struct HoleList
    {
        uint16_t count, reserved;
        std::array<Hole, HoleCount> holes;
    };
    static_assert(sizeof(HoleList) == 4 + HoleCount * 12);

    inline float DistanceLimit(float configured, float projectionFar)
    {
        if (!std::isfinite(projectionFar) || projectionFar <= NativeDistance)
            return NativeDistance;
        const float requested = ObjectDrawDistance::SanitizeDistance(configured);
        return (std::max)(NativeDistance, (std::min)(requested, (std::min)(projectionFar, MaximumDistance)));
    }

    inline uint32_t CullSquare(float configured)
    {
        const auto radius = static_cast<uint32_t>(DistanceLimit(configured, MaximumDistance) / 8.0f);
        return radius * radius; // At most 36,000,000, below the native signed-int ceiling.
    }

    inline int64_t FloorShift8(int64_t value)
    {
        return value >= 0 ? value / 256 : -((-value + 255) / 256);
    }

    inline int64_t EmissionSquare(int32_t x, int32_t z, const int32_t* camera)
    {
        const int64_t dx = FloorShift8(static_cast<int64_t>(camera[0]) - x);
        const int64_t dy = FloorShift8(static_cast<int64_t>(camera[1]) - Height);
        const int64_t dz = FloorShift8(static_cast<int64_t>(camera[2]) - z);
        return dx * dx + dy * dy + dz * dz;
    }

    inline bool WithinRange(int64_t x, int64_t y, int64_t z, const int32_t* camera, float distance)
    {
        if (camera == nullptr || !std::isfinite(distance) || distance <= 0.0f)
            return false;
        const double dx = (static_cast<double>(x) - camera[0]) / 32.0;
        const double dy = (static_cast<double>(y) - camera[1]) / 32.0;
        const double dz = (static_cast<double>(z) - camera[2]) / 32.0;
        return dx * dx + dy * dy + dz * dz < static_cast<double>(distance) * distance;
    }

    struct Particle
    {
        int64_t x = 0, y = 0, z = 0;
        int32_t vx = 0, vz = 0;
        int life = 0, size = 110;
        size_t hole = 0;
        int32_t originX = 0, originZ = 0;

        uint32_t Color() const
        {
            // Native type 0x3A: RGB 96/80/64; fade during the last 32 game ticks.
            const auto fade = static_cast<uint32_t>((std::max)(0, (std::min)(life, 32)));
            return 0xFF000000 | ((96 * fade / 32) << 16) | ((80 * fade / 32) << 8) | (64 * fade / 32);
        }
    };

    struct Pool
    {
        std::array<Particle, Capacity> particles{};
        const HoleList* source = nullptr;
        const void* world = nullptr;
        size_t next = 0;
        uint32_t random = 0x4D4F4C45; // Private stream: additional smoke never consumes native RNG.

        void Clear()
        {
            particles = {};
            source = nullptr;
            world = nullptr;
            next = 0;
        }

        bool Sync(int level, const HoleList* holes, const void* currentWorld)
        {
            if (level != 2 || holes == nullptr || currentWorld == nullptr || holes->count != HoleCount)
            {
                Clear();
                return false;
            }
            if (holes != source || currentWorld != world)
            {
                Clear();
                source = holes;
                world = currentWorld;
            }
            for (auto& particle : particles)
            {
                if (particle.life == 0)
                    continue;
                const auto& hole = holes->holes[particle.hole];
                if (hole.y != (std::numeric_limits<int32_t>::min)() ||
                    hole.x != particle.originX || hole.z != particle.originZ)
                    particle.life = 0;
            }
            return true;
        }

        uint32_t Random()
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            return random;
        }

        bool Emit(int32_t x, int32_t y, int32_t z, const int32_t* camera, float distance)
        {
            if (source == nullptr || camera == nullptr || y != Height ||
                EmissionSquare(x, z, camera) < NativeDistanceSquared || !WithinRange(x, y, z, camera, distance))
                return false;
            size_t index = 0;
            for (; index < HoleCount; ++index)
            {
                const auto& hole = source->holes[index];
                if (hole.y == (std::numeric_limits<int32_t>::min)() &&
                    static_cast<int64_t>(hole.x) * 32 == x && static_cast<int64_t>(hole.z) * 32 == z)
                    break;
            }
            if (index == HoleCount)
                return false;
            // Exhaustion drops only extra visual smoke, never overwrites live records.
            for (size_t visited = 0; visited < Capacity; ++visited)
            {
                auto& particle = particles[next];
                next = (next + 1) % Capacity;
                if (particle.life != 0)
                    continue;
                particle = {};
                particle.x = x;
                particle.y = y;
                particle.z = z;
                // Native motion template 3: ((random_byte & 31) - 15) * 8 / 2.
                particle.vx = (static_cast<int32_t>(Random() & 31) - 15) * 4;
                particle.vz = (static_cast<int32_t>(Random() & 31) - 15) * 4;
                particle.life = Lifetime;
                particle.hole = index;
                particle.originX = source->holes[index].x;
                particle.originZ = source->holes[index].z;
                return true;
            }
            return false;
        }

        void Tick(int ticks)
        {
            if (ticks <= 0)
                return; // No wall-clock animation while paused or unfocused.
            for (auto& particle : particles)
            {
                if (particle.life <= ticks)
                {
                    particle.life = 0;
                    continue;
                }
                particle.life -= ticks;
                particle.x += static_cast<int64_t>(particle.vx) * ticks;
                particle.y -= static_cast<int64_t>(256) * ticks;
                particle.z += static_cast<int64_t>(particle.vz) * ticks;
                particle.size += 2 * ticks;
            }
        }
    };
}
