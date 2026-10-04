#include "../source/MouseLookCamera.h"
#include <cassert>
#include <cstdio>

int main()
{
    ThirdPersonMouseLook input;
    uint16_t yaw = 0xFF0;
    int16_t pitch = 0x40;

    // Native follow/recenter must not undo mouse orbit, including wrapping and clamping.
    input.BeginFrame(yaw, pitch, 0x30, -0x800, false, false);
    yaw = 0;
    pitch = 0x40;
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0x20 && pitch == -0x200);

    input.EndFrame();
    input.BeginFrame(yaw, pitch, 0, 0, false, false);
    yaw = 7;
    pitch = 0x40;
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0x20 && pitch == -0x200);

    // Pitch auto-return is suppressed, but collision correction is preserved.
    pitch += 8;
    input.PreservePitch(pitch, true, 0);
    assert(pitch == -0x200);
    pitch += 8;
    input.PreservePitch(pitch, true, 8);
    assert(pitch == -0x1F8);

    // Native keyboard camera controls retain their yaw; recenter releases both axes.
    input.BeginFrame(yaw, pitch, 100, 0, true, false);
    yaw = 0xABC;
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0xABC && !input.ownsYaw);
    input.BeginFrame(yaw, pitch, 100, 100, false, true);
    yaw = 0x123;
    pitch = 0x40;
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0x123 && pitch == 0x40 && !input.ownsPitch);

    // Hooks outside the scoped gameplay frame are inert (visor/cinematic/focus resets).
    input.Reset();
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0x123 && pitch == 0x40);
    input.BeginFrame(yaw, pitch, -0x200, 0x800, false, false);
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 0xF23 && pitch == 0x300);
    input.EndFrame();
    yaw = 5;
    pitch = 6;
    input.ApplyOrbitAngles(yaw, pitch);
    assert(yaw == 5 && pitch == 6);

    std::puts("Mouse-look camera tests passed.");
}
