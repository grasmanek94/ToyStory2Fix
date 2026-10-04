#pragma once

#include <algorithm>
#include <cstdint>

// Third-person orbit input owns camera angles, never the player's facing/movement yaw.
// Feed the angles back after native follow logic, before the game's collision rays.
struct ThirdPersonMouseLook
{
    bool ownsYaw = false;
    bool ownsPitch = false;
    bool frameActive = false;
    bool anglesApplied = false;
    uint16_t frameYaw = 0;
    int16_t framePitch = 0;

    void Reset()
    {
        *this = {};
    }

    void BeginFrame(uint16_t yaw, int16_t pitch, int yawDelta, int pitchDelta,
        bool keyboardCamera, bool recenter)
    {
        frameActive = true;
        anglesApplied = false;

        // Explicit keyboard camera controls take priority without changing input masks.
        if (keyboardCamera || recenter)
            ownsYaw = false;
        if (recenter)
            ownsPitch = false;

        if (!keyboardCamera && !recenter && yawDelta != 0)
            ownsYaw = true;
        if (!recenter && pitchDelta != 0)
            ownsPitch = true;

        frameYaw = static_cast<uint16_t>((static_cast<int>(yaw) + yawDelta) & 0x0FFF);
        framePitch = static_cast<int16_t>(std::clamp(static_cast<int>(pitch) + pitchDelta, -0x200, 0x300));
    }

    void ApplyOrbitAngles(uint16_t& yaw, int16_t& pitch)
    {
        if (!frameActive)
            return;

        if (ownsYaw)
            yaw = frameYaw;
        if (ownsPitch)
            pitch = framePitch;
        anglesApplied = true;
    }

    void PreservePitch(int16_t& pitch, bool movingBranch, uint16_t collisionFlags)
    {
        // Keep native obstacle avoidance, but not automatic return to the default pitch.
        if (frameActive && anglesApplied && ownsPitch &&
            (!movingBranch || (collisionFlags & 0x0C) == 0))
        {
            pitch = framePitch;
        }
    }

    void EndFrame()
    {
        frameActive = false;
    }
};
