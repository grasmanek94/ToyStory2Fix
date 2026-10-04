#pragma once

#include <array>
#include <cstdint>

namespace SceneRenderDistance
{
    // Replace FSTP [scene-distance-squared] without changing x87 stack depth.
    // NOPing all six bytes leaks one x87 value each time the renderer sets its range.
    constexpr std::array<uint8_t, 6> PreserveThresholdStore = { 0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90 };
}
