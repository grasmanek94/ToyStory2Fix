#include "stdafx.h"
#include "MouseLookCamera.h"
#include "MouseButtonActions.h"
#include "NativeD3DResolution.h"
#include "HighResolutionWindow.h"
#include "NativeObjectRendering.h"
#include "NativeSceneryRendering.h"
#include "NativeMoleHoleSmoke.h"
#include "SceneRenderDistance.h"
#include "AltTabRecovery.h"
#include <MMSystem.h>
#include <ddraw.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <vector>

uintptr_t sub_490860_addr;
uintptr_t sub_49D910_addr;
uintptr_t sub_UpdateCameraController_addr;
uintptr_t sub_InitializeDisplay_addr;
uint8_t** g_ppNativeDisplayContext = nullptr;
bool g_widescreenProjectionHookInstalled = false;
uintptr_t g_beginNativeSceneAddress = 0;
uintptr_t g_presentNativeDisplayAddress = 0;
uintptr_t g_initializeNativeDrawAddress = 0;
uint8_t** g_ppAltTabDisplayContext = nullptr;
AltTabRecovery::CooperativeSettings g_altTabCooperativeSettings;
AltTabRecovery::Bindings g_altTabBindings;
AltTabRecovery::State g_altTabRecovery;
HRESULT g_lastAltTabRecoveryError = DD_OK;
AltTabRecovery::Stage g_lastAltTabRecoveryStage = AltTabRecovery::Stage::None;
ObjectDrawDistance::RenderOverlay g_objectRenderOverlay;
size_t g_largestExtraActorCount = 0;

void LogMessage(const std::string& message);

bool IsLoadedActorVisibleForRendering(const ObjectDrawDistance::Actor& actor)
{
    // Ghidra: world +0x274 is the model-pointer table, +0x278 its allocated length.
    const auto world = *reinterpret_cast<const uint8_t**>(0x00B62410);
    if (world == nullptr)
        return false;
    const auto count = *reinterpret_cast<const uint32_t*>(world + 0x278);
    const auto models = *reinterpret_cast<const uint8_t* const* const*>(world + 0x274);
    // Native model metadata has 128 pointers; its first short gates visibility.
    // A loaded mesh alone does not mean that an actor should be rendered.
    const auto metadata = reinterpret_cast<const uint8_t* const*>(0x00547CD4);
    if (!ObjectDrawDistance::HasRenderableModel(actor, models, count, metadata))
        return false;
    const auto room = actor.Read<int8_t>(0x6C);
    const auto roomVisible = reinterpret_cast<int(__cdecl*)(uint32_t)>(0x004BC160);
    if (room < 0 || roomVisible(static_cast<uint32_t>(room)) == 0)
        return false; // Keep native room/portal visibility, not "draw through walls".
    const float position[] = {
        static_cast<float>(actor.Read<int32_t>(0)) / 32.0f,
        static_cast<float>(actor.Read<int32_t>(4)) / 32.0f,
        static_cast<float>(actor.Read<int32_t>(8)) / 32.0f
    };
    const auto sphereVisibility = reinterpret_cast<uint32_t(__cdecl*)(const float*, float)>(0x004BA1F0);
    return (sphereVisibility(position, static_cast<float>(actor.Read<int16_t>(0x3E))) & 0x55555555) == 0;
}

void __cdecl RenderGameplayWithExtendedObjectDistance(int renderObjects)
{
    const auto render = reinterpret_cast<void(__cdecl*)(int)>(0x00440F70);
    if (renderObjects == 0 || NativeObjectRendering::arenaMemory == nullptr || g_objectRenderOverlay.active)
    {
        render(renderObjects);
        return;
    }
    auto& list = NativeObjectRendering::RenderList();
    // Arm cleanup before Begin/logging: even an allocation/formatting exception
    // must not let render-only visibility escape into gameplay updates.
    struct RestoreOverlay
    {
        ~RestoreOverlay() { g_objectRenderOverlay.End(NativeObjectRendering::RenderList()); }
    } restore;
    const auto before = std::find(list.begin(), list.end(), nullptr) - list.begin();
    const bool overlay = g_objectRenderOverlay.Begin(list,
        reinterpret_cast<ObjectDrawDistance::Actor*>(0x0052C840),
        reinterpret_cast<ObjectDrawDistance::Actor*>(0x0052F300),
        reinterpret_cast<const int32_t*>(0x0052ADC0), NativeObjectRendering::drawDistance,
        IsLoadedActorVisibleForRendering);
    if (overlay)
    {
        const auto after = std::find(list.begin(), list.end(), nullptr) - list.begin();
        const auto extra = static_cast<size_t>(after - before);
        if (extra > g_largestExtraActorCount)
        {
            g_largestExtraActorCount = extra;
            LogMessage(format("IncreaseObjectRenderDistance: rendering %u additional loaded actors; native gameplay list unchanged",
                static_cast<unsigned>(extra)));
        }
    }
    render(renderObjects);
}

struct MouseLookSettings
{
    bool enabled = false;
    bool invertX = false;
    bool invertY = true; // Matches the default in ToyStory2Fix.ini.
    float sensitivity = 4.0f;
    POINT lastCursorPosition{};
    bool hasCursorPosition = false;
    uint32_t lastCameraMode = static_cast<uint32_t>(-1);
    DWORD lastSampleTime = 0;
    bool movingCameraBranch = false;
} g_mouseLook;

struct MouseButtonSettings
{
    bool enabled = false;
    DWORD lastSampleTime = 0;
    MouseButtonActions actions;
} g_mouseButtons;

uint16_t* g_pPolledRawInputActions;
ThirdPersonMouseLook g_thirdPersonMouseLook;
uint32_t* g_pVisorCameraMode;
uint8_t* g_pCameraState;
uint16_t* g_pPlayerFacingYaw;
uint16_t* g_pPlayerDesiredYaw;
uint16_t* g_pGameplayActions;
uint16_t* g_pRawInputActions;
uint16_t* g_pPreviousRawInputActions;
uint8_t* g_pCinematicCameraFlags;
int32_t* g_pCameraLookAtX;
int32_t* g_pThirdPersonCameraDistance;

TIMECAPS tc;
LARGE_INTEGER Frequency;
LARGE_INTEGER PreviousTime, CurrentTime, ElapsedMicroseconds;
int sleepTime;
int framerateFactor;
std::string g_logPath;

struct Variables
{
    uint32_t nWidth;
    uint32_t nHeight;
    float fAspectRatio;
    float fScaleValue;
    float f2DScaleValue;
    uint32_t* speedMultiplier;
    bool* isDemoMode;
} Variables;

void UpdateWidescreenDimensions(const uint32_t* viewportDimensions)
{
    Variables.nWidth = viewportDimensions[0];
    Variables.nHeight = viewportDimensions[1];
    Variables.fAspectRatio = (Variables.nWidth != 0 && Variables.nHeight != 0)
        ? static_cast<float>(Variables.nWidth) / static_cast<float>(Variables.nHeight)
        : 4.0f / 3.0f;
    Variables.fScaleValue = 1.0f / Variables.fAspectRatio;
    Variables.f2DScaleValue = (4.0f / 3.0f) / Variables.fAspectRatio;
}

struct WidescreenProjectionHook
{
    void operator()(injector::reg_pack& regs)
    {
        // Original MOV [EAX+44],0.75 uses a byte offset, not 0x44 float elements.
        auto aspect = reinterpret_cast<float*>(regs.eax + 0x44);
        *aspect = Variables.fScaleValue;
    }
};

void UpdateElapsedMicroseconds() {
    QueryPerformanceCounter(&CurrentTime);
    ElapsedMicroseconds.QuadPart = CurrentTime.QuadPart - PreviousTime.QuadPart;
    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
}

int __cdecl sub_490860(int a1) {
    timeBeginPeriod(tc.wPeriodMin);

    if (PreviousTime.QuadPart == 0)
        QueryPerformanceCounter(&PreviousTime); // initialise

    UpdateElapsedMicroseconds();

    framerateFactor = ((int)ElapsedMicroseconds.QuadPart / 16667) + 1;
    // Demo mode needs 30fps maximum
    if (*Variables.isDemoMode && framerateFactor < 2)
        framerateFactor = 2;

    *Variables.speedMultiplier = std::clamp(framerateFactor, 1, 3);

    sleepTime = 0;
    // Loop until next frame due
    do {
        sleepTime = (16949 * framerateFactor - (uint32_t)ElapsedMicroseconds.QuadPart) / 1000; // calculate sleep time, 16949 µs = 59 fps (to limit frame drops)
        sleepTime = ((sleepTime / tc.wPeriodMin) * tc.wPeriodMin) - tc.wPeriodMin; // truncate to multiple of period
        if (sleepTime > 0)
            Sleep(sleepTime); // sleep to avoid wasted CPU
        UpdateElapsedMicroseconds();
    } while (ElapsedMicroseconds.QuadPart < 16667 * framerateFactor);

    QueryPerformanceCounter(&PreviousTime);
    timeEndPeriod(tc.wPeriodMin);
    return (int)(PreviousTime.QuadPart / 1000);
}

int sub_49D910() {
    auto _sub_49D910 = (int(*)()) sub_49D910_addr;

    /* FIX WIDESCREEN */
    // this code can't be in Init() because width/height are not set at first

    /* Set width and height */
    auto pattern = hook::pattern("8B 15 ? ? ? ? 89 4C 24 08 89 44 24 0C"); //4B5672
    // The instruction operand is the address of viewport width; read its contents.
    UpdateWidescreenDimensions(*pattern.get_first<uint32_t*>(2));

    /* Fix 3D stretch */
    if (!g_widescreenProjectionHookInstalled)
    {
        pattern = hook::pattern("C7 40 44 00 00 40 3F"); //4CE08F
        if (pattern.size() == 1)
        {
            // Replace the entire seven-byte instruction; leaving its final 0x3F byte
            // behind would execute AAS and corrupt the camera-object pointer in EAX.
            injector::MakeInline<WidescreenProjectionHook>(pattern.get_first(0), pattern.get_first(7));
            g_widescreenProjectionHookInstalled = true;
        }
        else
        {
            LogMessage("Widescreen: projection hook skipped (signature does not match uniquely)");
        }
    }


    return _sub_49D910();
}

uint16_t AddGameAngle(uint16_t angle, int delta)
{
    return static_cast<uint16_t>((static_cast<int>(angle) + delta) & 0x0FFF);
}

void AddVisorYaw(int delta)
{
    // Only visor aiming uses player yaw: the shot direction must follow the aim camera.
    *g_pPlayerFacingYaw = AddGameAngle(*g_pPlayerFacingYaw, delta);
    *g_pPlayerDesiredYaw = AddGameAngle(*g_pPlayerDesiredYaw, delta);
}

bool CenterCursorInGameWindow(HWND gameWindow)
{
    RECT clientRect{};
    if (!GetClientRect(gameWindow, &clientRect))
        return false;

    POINT center{
        (clientRect.right - clientRect.left) / 2,
        (clientRect.bottom - clientRect.top) / 2
    };

    if (!ClientToScreen(gameWindow, &center) || !SetCursorPos(center.x, center.y))
        return false;

    g_mouseLook.lastCursorPosition = center;
    g_mouseLook.hasCursorPosition = true;
    return true;
}

HWND GetForegroundGameWindow()
{
    const HWND gameWindow = GetForegroundWindow();
    DWORD processId = 0;
    if (gameWindow == nullptr || GetWindowThreadProcessId(gameWindow, &processId) == 0 ||
        processId != GetCurrentProcessId())
    {
        return nullptr;
    }
    return gameWindow;
}

uint16_t SampleMouseButtonActions()
{
    const DWORD sampleTime = GetTickCount();
    if (sampleTime - g_mouseButtons.lastSampleTime > 250)
        g_mouseButtons.actions.Reset();
    g_mouseButtons.lastSampleTime = sampleTime;

    if (!g_mouseButtons.enabled || GetForegroundGameWindow() == nullptr)
        return g_mouseButtons.actions.GetActions(false, false, false);

    // Use the held-state bit, not GetAsyncKeyState's unreliable "pressed since last call" bit.
    return g_mouseButtons.actions.GetActions(true,
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
}

struct MouseButtonsHook
{
    void operator()(injector::reg_pack& regs)
    {
        // Replay MOV CX,[polled raw actions], preserving ECX's upper half and EFLAGS.
        // The native store and previous-frame snapshot then handle holds and press edges.
        const uint16_t actions = *g_pPolledRawInputActions | SampleMouseButtonActions();
        regs.ecx = (regs.ecx & 0xFFFF0000u) | actions;
    }
};

bool GetMouseLookDelta(int& deltaX, int& deltaY)
{
    deltaX = 0;
    deltaY = 0;

    const HWND gameWindow = GetForegroundGameWindow();
    if (gameWindow == nullptr)
    {
        g_mouseLook.hasCursorPosition = false;
        return false;
    }

    const DWORD sampleTime = GetTickCount();
    if (sampleTime - g_mouseLook.lastSampleTime > 250)
        g_mouseLook.hasCursorPosition = false;
    g_mouseLook.lastSampleTime = sampleTime;

    POINT currentCursor{};
    if (!GetCursorPos(&currentCursor))
    {
        g_mouseLook.hasCursorPosition = false;
        return false;
    }

    if (!g_mouseLook.hasCursorPosition)
    {
        g_mouseLook.lastCursorPosition = currentCursor;
        g_mouseLook.hasCursorPosition = true;
        CenterCursorInGameWindow(gameWindow);
        return false;
    }

    deltaX = currentCursor.x - g_mouseLook.lastCursorPosition.x;
    deltaY = currentCursor.y - g_mouseLook.lastCursorPosition.y;

    if (!CenterCursorInGameWindow(gameWindow))
        g_mouseLook.lastCursorPosition = currentCursor;

    // A valid zero delta still lets us preserve the manually chosen orbit angles.
    return true;
}

void ApplyMouseLook()
{
    g_thirdPersonMouseLook.EndFrame();
    if (!g_mouseLook.enabled)
        return;

    const uint32_t cameraMode = *g_pVisorCameraMode;
    if ((cameraMode != 0 && cameraMode != 4) || (*g_pCinematicCameraFlags & 1) != 0)
    {
        // Do not carry a mouse movement across visor transitions or cinematic camera states.
        g_mouseLook.lastCameraMode = cameraMode;
        g_mouseLook.hasCursorPosition = false;
        g_thirdPersonMouseLook.Reset();
        return;
    }

    if (cameraMode != g_mouseLook.lastCameraMode)
    {
        g_mouseLook.lastCameraMode = cameraMode;
        g_mouseLook.hasCursorPosition = false;
        g_thirdPersonMouseLook.Reset();
    }

    int mouseX = 0;
    int mouseY = 0;
    if (!GetMouseLookDelta(mouseX, mouseY))
    {
        g_thirdPersonMouseLook.Reset();
        return;
    }

    if (g_mouseLook.invertX)
        mouseX = -mouseX;

    const int yawDelta = static_cast<int>(std::lround(mouseX * g_mouseLook.sensitivity));
    const int pitchSign = g_mouseLook.invertY ? 1 : -1;
    const int pitchDelta = static_cast<int>(std::lround(mouseY * g_mouseLook.sensitivity)) * pitchSign;

    // g_dwVisorCameraMode is set by UpdatePlayerMovementAndAiming when the original
    // 0x400 action edge is received (Tab or right click). Mouse look only adjusts camera state.
    switch (cameraMode)
    {
    case 0: // Normal third-person camera.
    {
        if (*g_pCameraLookAtX != INT32_MIN)
        {
            // Let scripted look-at requests take control rather than fighting them.
            g_thirdPersonMouseLook.Reset();
            break;
        }

        const bool keyboardCamera = (*g_pGameplayActions & 0x300) != 0;
        const bool recenter = (*g_pRawInputActions & 0x1000) != 0 &&
            (*g_pPreviousRawInputActions & 0x1000) == 0;
        g_thirdPersonMouseLook.BeginFrame(
            *reinterpret_cast<uint16_t*>(g_pCameraState + 0x28),
            *reinterpret_cast<int16_t*>(g_pCameraState + 0x2E),
            yawDelta, pitchDelta, keyboardCamera, recenter);
        break;
    }

    case 4: // Visor/aim camera after the transition state has completed.
    {
        auto visorPitch = reinterpret_cast<uint32_t*>(g_pCameraState + 0x0C);
        int signedPitch = static_cast<int>(*visorPitch & 0x0FFF);
        if (signedPitch > 0x7FF)
            signedPitch -= 0x1000;

        AddVisorYaw(yawDelta);
        *visorPitch = static_cast<uint32_t>(std::clamp(signedPitch + pitchDelta, -0x338, 0x320)) & 0x0FFF;
        break;
    }

    default:
        // Leave the normal-to-visor transition and all cinematic camera states to the game.
        break;
    }
}

void ApplyThirdPersonOrbitAngles(uint8_t* camera, bool movingBranch)
{
    if (camera != g_pCameraState || *g_pVisorCameraMode != 0 ||
        (*g_pCinematicCameraFlags & 1) != 0)
    {
        return;
    }

    g_mouseLook.movingCameraBranch = movingBranch;
    g_thirdPersonMouseLook.ApplyOrbitAngles(
        *reinterpret_cast<uint16_t*>(camera + 0x28),
        *reinterpret_cast<int16_t*>(camera + 0x2E));
}

struct IdleCameraOrbitHook
{
    void operator()(injector::reg_pack& regs)
    {
        auto camera = reinterpret_cast<uint8_t*>(regs.esi);
        ApplyThirdPersonOrbitAngles(camera, false);
        // Replay MOVSX EDX,[ESI+2E] / MOV EBX,[camera distance].
        regs.edx = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x2E));
        regs.ebx = *g_pThirdPersonCameraDistance;
    }
};

struct MovingCameraOrbitHook
{
    void operator()(injector::reg_pack& regs)
    {
        auto camera = reinterpret_cast<uint8_t*>(regs.esi);
        ApplyThirdPersonOrbitAngles(camera, true);
        // ECX already holds pitch at this point; refresh it after applying mouse input.
        regs.ecx = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x2E));
        // Replay MOVSX EBP,[ESI+28] / MOVSX EDX,[ESI+26].
        regs.ebp = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x28));
        regs.edx = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x26));
    }
};

struct CameraPlacementHook
{
    void operator()(injector::reg_pack& regs)
    {
        auto camera = reinterpret_cast<uint8_t*>(regs.esi);
        if (camera == g_pCameraState && *g_pVisorCameraMode == 0 &&
            (*g_pCinematicCameraFlags & 1) == 0)
        {
            g_thirdPersonMouseLook.PreservePitch(
                *reinterpret_cast<int16_t*>(camera + 0x2E),
                g_mouseLook.movingCameraBranch,
                *reinterpret_cast<uint16_t*>(camera + 0x32));
        }
        // Replay MOVSX EDX,[ESI+2E] / MOVSX EDI,[ESI+26].
        regs.edx = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x2E));
        regs.edi = static_cast<int32_t>(*reinterpret_cast<int16_t*>(camera + 0x26));
    }
};

void __cdecl UpdateCameraControllerWithMouseLook()
{
    ApplyMouseLook();
    reinterpret_cast<void(__cdecl*)()>(sub_UpdateCameraController_addr)();
    g_thirdPersonMouseLook.EndFrame();
}

// Simple runtime log, written next to the .asi/.ini as "ToyStory2Fix.log".
// Truncated on the first write of each session, then appended to for the rest of the run.
void LogMessage(const std::string& message)
{
    if (g_logPath.empty())
        return; // log path not initialised yet

    static bool firstWrite = true;
    std::ofstream logFile(g_logPath, firstWrite ? std::ios::out : std::ios::app);
    firstWrite = false;

    if (logFile.is_open())
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logFile << format("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond) << message << std::endl;
    }
}

void LogDisplayWindow(HWND window, const char* stage)
{
    RECT outer{}, client{};
    if (GetWindowRect(window, &outer) && GetClientRect(window, &client))
    {
        LogMessage(format("FixHighResolution: window %s: outer=%ld,%ld %ldx%ld, client=%ldx%ld, desktop=%dx%d",
            stage, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top,
            client.right - client.left, client.bottom - client.top,
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)));
        MONITORINFOEXA monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            DEVMODEA current{};
            current.dmSize = sizeof(current);
            const bool hasMode = EnumDisplaySettingsA(monitor.szDevice, ENUM_CURRENT_SETTINGS, &current) != FALSE;
            LogMessage(format("FixHighResolution: monitor %s: bounds=%ld,%ld %ldx%ld, current OS mode=%lux%lu @ %luHz (query=%u)",
                monitor.szDevice, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency, hasMode ? 1u : 0u));
        }

        // Optional Win10 APIs: diagnostics must not add a hard OS-version dependency.
        const auto user32 = GetModuleHandleA("user32.dll");
        const auto getDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(user32, "GetDpiForWindow"));
        const auto getContext = reinterpret_cast<HANDLE(WINAPI*)(HWND)>(GetProcAddress(user32, "GetWindowDpiAwarenessContext"));
        const auto getAwareness = reinterpret_cast<int(WINAPI*)(HANDLE)>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext"));
        LogMessage(format("FixHighResolution: window DPI=%u, awareness=%d, style=0x%08X, exstyle=0x%08X",
            getDpi != nullptr ? getDpi(window) : 0,
            getContext != nullptr && getAwareness != nullptr ? getAwareness(getContext(window)) : -1,
            static_cast<uint32_t>(GetWindowLongPtrA(window, GWL_STYLE)),
            static_cast<uint32_t>(GetWindowLongPtrA(window, GWL_EXSTYLE))));
        DWORD owner = 0;
        if (GetWindowThreadProcessId(window, &owner) == GetCurrentThreadId())
        {
            MINMAXINFO limits{};
            SendMessageA(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
            LogMessage(format("FixHighResolution: window size limits: maximize=%ldx%ld, tracking=%ldx%ld",
                limits.ptMaxSize.x, limits.ptMaxSize.y, limits.ptMaxTrackSize.x, limits.ptMaxTrackSize.y));
        }
    }
    else
    {
        LogMessage(format("FixHighResolution: window %s: geometry unavailable", stage));
    }
}

void LogNativeDisplaySurfaces()
{
    if (g_ppNativeDisplayContext == nullptr || *g_ppNativeDisplayContext == nullptr)
    {
        LogMessage("FixHighResolution: native surface diagnostics unavailable (display-context signature not found)");
        return;
    }
    const auto context = *g_ppNativeDisplayContext;
    const auto draw = *reinterpret_cast<IDirectDraw4**>(context + 0x48);
    if (draw != nullptr)
    {
        DDSURFACEDESC2 mode{};
        mode.dwSize = sizeof(mode);
        const auto result = draw->GetDisplayMode(&mode);
        LogMessage(format("FixHighResolution: DirectDraw display mode=%lux%lu, bpp=%lu, result=0x%08X",
            mode.dwWidth, mode.dwHeight, mode.ddpfPixelFormat.dwRGBBitCount, static_cast<uint32_t>(result)));
    }
    struct SurfaceEntry { const char* name; size_t offset; };
    const SurfaceEntry entries[] = { { "primary", 0x30 }, { "back", 0x34 }, { "render target", 0x38 }, { "depth", 0x3C } };
    for (const auto& entry : entries)
    {
        const auto surface = *reinterpret_cast<IDirectDrawSurface4**>(context + entry.offset);
        if (surface == nullptr)
        {
            LogMessage(format("FixHighResolution: %s surface is null", entry.name));
            continue;
        }
        DDSURFACEDESC2 desc{};
        desc.dwSize = sizeof(desc);
        const auto result = surface->GetSurfaceDesc(&desc);
        LogMessage(format("FixHighResolution: %s surface=%lux%lu, pitch=%ld, caps=0x%08X, desc=0x%08X, lost=0x%08X",
            entry.name, desc.dwWidth, desc.dwHeight, desc.lPitch, desc.ddsCaps.dwCaps,
            static_cast<uint32_t>(result), static_cast<uint32_t>(surface->IsLost())));
        if (entry.offset == 0x30)
        {
            IDirectDrawClipper* clipper = nullptr;
            const auto clipperResult = surface->GetClipper(&clipper);
            LogMessage(format("FixHighResolution: primary clipper query=0x%08X", static_cast<uint32_t>(clipperResult)));
            if (SUCCEEDED(clipperResult) && clipper != nullptr)
            {
                HWND clippedWindow = nullptr;
                const auto windowResult = clipper->GetHWnd(&clippedWindow);
                LogMessage(format("FixHighResolution: primary clipper HWND=0x%08X, query=0x%08X",
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clippedWindow)), static_cast<uint32_t>(windowResult)));
                if (SUCCEEDED(windowResult) && clippedWindow != nullptr)
                    LogDisplayWindow(clippedWindow, "primary clipper");
                DWORD bytes = 0;
                clipper->GetClipList(nullptr, nullptr, &bytes);
                if (bytes >= sizeof(RGNDATAHEADER) && bytes <= 1024 * 1024)
                {
                    std::vector<uint8_t> data(bytes);
                    auto region = reinterpret_cast<RGNDATA*>(data.data());
                    const auto regionResult = clipper->GetClipList(nullptr, region, &bytes);
                    if (SUCCEEDED(regionResult))
                    {
                        const auto& bounds = region->rdh.rcBound;
                        LogMessage(format("FixHighResolution: primary clip bounds=%ld,%ld %ldx%ld, rectangles=%lu",
                            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, region->rdh.nCount));
                    }
                }
                clipper->Release();
            }
        }
    }
}

int __cdecl InitializeDisplayWithHighResolutionSupport(HWND window, void* drawInfo,
    const GUID* deviceInfo, const DDSURFACEDESC2* mode, uint32_t flags)
{
    const uint32_t width = mode != nullptr ? mode->dwWidth : 0;
    const uint32_t height = mode != nullptr ? mode->dwHeight : 0;
    LogMessage(format("FixHighResolution: initializing display %ux%u (flags=0x%X)", width, height, flags));
    bool adjustFullscreenWindow = false;

    if ((width > NativeD3DResolution::OriginalLimit || height > NativeD3DResolution::OriginalLimit) &&
        NativeD3DResolution::IsHalDevice(deviceInfo))
    {
        // Enumeration has already loaded the native runtime, if it is in use. Do not
        // load/modify a different renderer when a DirectDraw translation wrapper is active.
        const auto patch = NativeD3DResolution::RaiseLimit(GetModuleHandleA("d3dim.dll"));
        switch (patch.status)
        {
        case NativeD3DResolution::Status::Applied:
        case NativeD3DResolution::Status::AlreadyApplied:
            LogMessage(format("FixHighResolution: native D3DIM surface limit=%u (process memory only)",
                NativeD3DResolution::RaisedLimit));
            // Ghidra: CreateDisplayContext flag 1 selects native fullscreen; do not
            // resize windowed modes, software renderers, or replacement graphics wrappers.
            adjustFullscreenWindow = (flags & 1) != 0 && width <= NativeD3DResolution::RaisedLimit &&
                height <= NativeD3DResolution::RaisedLimit;
            break;
        case NativeD3DResolution::Status::ModuleMissing:
            LogMessage("FixHighResolution: native D3DIM not loaded; leaving the graphics wrapper unchanged");
            break;
        case NativeD3DResolution::Status::SignatureMismatch:
            LogMessage(format("FixHighResolution: native patch skipped (create matches=%u, target matches=%u)",
                static_cast<unsigned>(patch.createMatches), static_cast<unsigned>(patch.targetMatches)));
            break;
        case NativeD3DResolution::Status::ProtectionFailed:
            LogMessage("FixHighResolution: native patch skipped (memory protection change failed)");
            break;
        }
    }

    bool windowAttached = false;
    if (adjustFullscreenWindow)
    {
        LogDisplayWindow(window, "before setup");
        windowAttached = HighResolutionWindow::Attach(window, width, height);
        if (!windowAttached || !HighResolutionWindow::Resize())
            LogMessage("FixHighResolution: unable to apply fullscreen window bounds before setup");
        LogDisplayWindow(window, "before DirectDraw");
    }

    const auto initialize = reinterpret_cast<int(__cdecl*)(HWND, void*, const GUID*, const DDSURFACEDESC2*, uint32_t)>(
        sub_InitializeDisplay_addr);
    const int result = initialize(window, drawInfo, deviceInfo, mode, flags);
    LogMessage(format("FixHighResolution: display initialization result=0x%08X", static_cast<uint32_t>(result)));
    if (result < 0)
    {
        // The original caller continues into renderer initialization after this failure,
        // using a display context whose graphics resources were released. Stop cleanly.
        const auto message = format(
            "Unable to initialize Toy Story 2 graphics at %ux%u (error 0x%08X).\n\n"
            "Try a lower resolution. The native high-resolution fix supports dimensions up to %u "
            "on matching Windows runtimes; graphics-driver limits still apply.\n\n"
            "See ToyStory2Fix.log for details.", width, height, static_cast<uint32_t>(result),
            NativeD3DResolution::RaisedLimit);
        MessageBoxA(window, message.c_str(), "ToyStory2Fix: graphics initialization failed", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    if (windowAttached)
    {
        if (!HighResolutionWindow::Resize())
            LogMessage("FixHighResolution: unable to apply fullscreen window bounds after setup");
        LogDisplayWindow(window, "after setup");
        LogNativeDisplaySurfaces();
    }
    return result;
}

bool InstallHighResolutionHook()
{
    // Ghidra: InitializeGameDisplay -> CreateDisplayContext, before renderer initialization.
    auto display = hook::pattern("8B 90 44 01 00 00 52 50 8B 44 24 14 50 51 E8 ? ? ? ? 83 C4 14 85 C0 5E 7C ? C7 05 ? ? ? ? 01 00 00 00");
    const auto count = display.size();
    if (count != 1)
    {
        LogMessage(format("FixHighResolution: disabled (display call pattern matches=%u)", static_cast<unsigned>(count)));
        return false;
    }
    auto call = display.get_first<uint8_t>(14);
    // GetDisplayBackBuffer loads the context cell used by initialization.
    auto backBuffer = hook::pattern("A1 ? ? ? ? 8B 40 34 C3");
    if (backBuffer.size() == 1)
        g_ppNativeDisplayContext = *backBuffer.get_first<uint8_t**>(1);
    sub_InitializeDisplay_addr = reinterpret_cast<uintptr_t>(call) + 5 + *reinterpret_cast<int32_t*>(call + 1);
    injector::MakeCALL(call, InitializeDisplayWithHighResolutionSupport);
    return true;
}

int __fastcall InitializeNativeDrawWithAltTabSettings(uint8_t* context, void*, const GUID* driver, uint32_t flags)
{
    const auto initialize = reinterpret_cast<int(__thiscall*)(uint8_t*, const GUID*, uint32_t)>(g_initializeNativeDrawAddress);
    const auto result = initialize(context, driver, flags);
    g_altTabCooperativeSettings = {};
    if (result >= 0 && context != nullptr)
    {
        g_altTabCooperativeSettings = { AltTabRecovery::Read<IDirectDraw4*>(context, 0x48),
            AltTabRecovery::Read<HWND>(context, 0),
            AltTabRecovery::NativeCooperativeFlags(AltTabRecovery::Read<uint32_t>(context, 4) != 0, flags) };
        LogMessage(format("FixAltTab: captured initial cooperative flags=0x%X", g_altTabCooperativeSettings.flags));
    }
    return result;
}

HRESULT __cdecl BeginSceneWithAltTabRecovery()
{
    const auto begin = reinterpret_cast<HRESULT(__cdecl*)()>(g_beginNativeSceneAddress);
    // Native Direct3D6 only. Do not replace a graphics wrapper's own recovery policy.
    const auto nativeD3D = GetModuleHandleA("d3dim.dll");
    if (nativeD3D == nullptr || g_ppAltTabDisplayContext == nullptr)
    {
        g_altTabRecovery.ObserveContext(nullptr);
        return begin();
    }
    const auto context = *g_ppAltTabDisplayContext;
    if (!AltTabRecovery::UsesNativeModules(context, nativeD3D, GetModuleHandleA("ddraw.dll")))
    {
        g_altTabRecovery.ObserveContext(nullptr);
        return begin();
    }
    const auto window = AltTabRecovery::Read<HWND>(context, 0);
    const bool focused = window != nullptr && GetForegroundWindow() == window && !IsIconic(window);
    const auto result = g_altTabRecovery.Begin(context, g_altTabBindings, focused, begin);
    const auto& recovery = g_altTabRecovery.lastResult;
    if (recovery.attempted && (SUCCEEDED(recovery.error) || recovery.error != g_lastAltTabRecoveryError ||
        recovery.stage != g_lastAltTabRecoveryStage))
    {
        LogMessage(format("FixAltTab: recovery result=0x%08X stage=%s, exclusive reacquired=%u, restored surfaces=%u, reloaded textures=%u, mode restored=%u, BeginScene=0x%08X",
            static_cast<uint32_t>(recovery.error), AltTabRecovery::StageName(recovery.stage),
            recovery.exclusiveReacquired ? 1u : 0u,
            recovery.surfaces, recovery.textures, recovery.modeRestored ? 1u : 0u, static_cast<uint32_t>(result)));
        g_lastAltTabRecoveryError = recovery.error;
        g_lastAltTabRecoveryStage = recovery.stage;
    }
    return result;
}

HRESULT __cdecl PresentWithAltTabRecovery()
{
    const auto present = reinterpret_cast<HRESULT(__cdecl*)()>(g_presentNativeDisplayAddress);
    const auto result = present();
    if (g_altTabRecovery.context != nullptr)
        g_altTabRecovery.ObservePresent(result);
    return result; // Preserve the native presentation result and existing fallback.
}

bool InstallAltTabRecoveryHooks(HMODULE module = GetModuleHandleW(nullptr))
{
    if (module == nullptr)
        return false;
    auto frame = hook::module_pattern(module, "E8 ? ? ? ? 85 C0 75 ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 85 C0 74 ? E8 ? ? ? ? B8 01 00 00 00 C3 33 C0 C3");
    auto begin = hook::module_pattern(module, "E8 ? ? ? ? 85 C0 74 ? 8B 08 50 FF 51 24 C3 83 C8 FF C3");
    auto present = hook::module_pattern(module, "E8 ? ? ? ? 3D C2 01 76 88 75 05 E9 ? ? ? ? C3");
    auto back = hook::module_pattern(module, "A1 ? ? ? ? 8B 40 34 C3");
    auto device = hook::module_pattern(module, "A1 ? ? ? ? 8B 40 40 C3");
    auto textures = hook::module_pattern(module, "A1 ? ? ? ? 85 C0 74 ? C7 80 04 01 00 00 00 00 00 00 A1 ? ? ? ? 50 E8 ? ? ? ? A1 ? ? ? ? 83 C4 04 85 C0 75 ? C3");
    auto recreate = hook::module_pattern(module, "81 EC 8C 02 00 00 53 8B 9C 24 94 02 00 00 55 56 8A 83 00 01 00 00 57 A8 40 0F 85");
    auto textureBinding = hook::module_pattern(module, "A1 ? ? ? ? 56 57 8B 7C 24 0C 3B C7 C7 05 ? ? ? ? 00 00 00 00 74 ? 6A 00 57 89 3D ? ? ? ? E8 ? ? ? ?");
    auto initializeDraw = hook::module_pattern(module, "8B 44 24 04 53 8B 5C 24 14 56 57 53 8B F1 50 E8 ? ? ? ? 85 C0 0F 8C");
    auto cooperativeFlags = hook::module_pattern(module, "8B 46 04 B9 08 00 00 00 85 C0 74 05 B9 13 00 00 00 8B 44 24 10 83 E0 10 84 C0 75 03 80 CD 08 8B 07 51 8B 0E 8B 10 51 50 FF 52 50 33 D2 5F 85 C0 0F 9D C2 4A 5E 81 E2 02 00 00 82 8B C2 C2 08 00");
    const auto unique = [](hook::pattern& pattern, const char* name)
    {
        const auto matches = pattern.size();
        if (matches == 1)
            return true;
        LogMessage(format("FixAltTab: skipped safely (%s pattern matches=%u)", name, static_cast<unsigned>(matches)));
        return false;
    };
    if (!unique(frame, "frame") || !unique(begin, "BeginScene") || !unique(present, "presentation") ||
        !unique(back, "back buffer") || !unique(device, "device") || !unique(textures, "texture list") ||
        !unique(recreate, "texture uploader") || !unique(textureBinding, "texture binding") ||
        !unique(initializeDraw, "DirectDraw initialization") || !unique(cooperativeFlags, "cooperative flags"))
    {
        return false;
    }
    const auto frameCall = frame.get_first<uint8_t>();
    const auto originalBegin = reinterpret_cast<uintptr_t>(frameCall) + 5 + *reinterpret_cast<int32_t*>(frameCall + 1);
    const auto beginCall = begin.get_first<uint8_t>();
    const auto originalGetDevice = reinterpret_cast<uintptr_t>(beginCall) + 5 + *reinterpret_cast<int32_t*>(beginCall + 1);
    const auto displayCell = *back.get_first<uint8_t**>(1);
    const auto textureCell = *textures.get_first<uint8_t**>(1);
    const auto cachedTexture = *textureBinding.get_first<int32_t*>(1);
    const auto initializeDrawCall = initializeDraw.get_first<uint8_t>(15);
    const auto originalInitializeDraw = reinterpret_cast<uintptr_t>(initializeDrawCall) + 5 +
        *reinterpret_cast<int32_t*>(initializeDrawCall + 1);
    // Cross-check repeated references before installing either hook.
    if (originalBegin != reinterpret_cast<uintptr_t>(beginCall) ||
        originalGetDevice != reinterpret_cast<uintptr_t>(device.get_first()) ||
        displayCell != *device.get_first<uint8_t**>(1) ||
        textureCell != *textures.get_first<uint8_t**>(20) || textureCell != *textures.get_first<uint8_t**>(31) ||
        cachedTexture != *textureBinding.get_first<int32_t*>(30) ||
        originalInitializeDraw + 0x53 != reinterpret_cast<uintptr_t>(cooperativeFlags.get_first()))
    {
        LogMessage("FixAltTab: skipped safely (native references disagree)");
        return false;
    }
    const auto presentCall = present.get_first<uint8_t>();
    g_beginNativeSceneAddress = originalBegin;
    g_presentNativeDisplayAddress = reinterpret_cast<uintptr_t>(presentCall) + 5 + *reinterpret_cast<int32_t*>(presentCall + 1);
    g_ppAltTabDisplayContext = displayCell;
    g_initializeNativeDrawAddress = originalInitializeDraw;
    g_altTabBindings = { textureCell, reinterpret_cast<int(__cdecl*)(uint8_t*)>(recreate.get_first()), cachedTexture,
        &g_altTabCooperativeSettings };
    injector::MakeCALL(initializeDrawCall, InitializeNativeDrawWithAltTabSettings);
    injector::MakeCALL(frameCall, BeginSceneWithAltTabRecovery);
    injector::MakeCALL(presentCall, PresentWithAltTabRecovery);
    return true;
}

bool InstallMouseButtonHook()
{
    // Ghidra: UpdateRawInputActions copies previous input, polls DirectInput, then stores CX.
    auto input = hook::pattern("66 A1 ? ? ? ? 66 A3 ? ? ? ? E8 ? ? ? ? 66 8B 0D ? ? ? ? 66 89 0D ? ? ? ? C3");
    const auto count = input.size();
    if (count != 1)
    {
        LogMessage(format("MouseButtons: disabled (input snapshot pattern matches=%u)", static_cast<unsigned>(count)));
        return false;
    }

    g_pPolledRawInputActions = *input.get_first<uint16_t*>(20);
    injector::MakeInline<MouseButtonsHook>(input.get_first(17), input.get_first(24));
    return true;
}

bool InstallMouseLookHooks()
{
    auto dispatcher = hook::pattern("A1 ? ? ? ? 68 ? ? ? ? 3B C6 75 ? E8 ? ? ? ? EB ? E8 ? ? ? ? A1 ? ? ? ? 83 C4 04");
    auto frame = hook::pattern("E8 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ?");
    auto idle = hook::pattern("0F BF 56 2E 8B 1D ? ? ? ? 68 D0 07 00 00 0F BF 4E 28");
    auto moving = hook::pattern("0F BF 6E 28 0F BF 56 26 8D 81 00 04 00 00 89 6C 24 30");
    auto placement = hook::pattern("0F BF 56 2E 0F BF 7E 26 8D 82 00 04 00 00 81 C2 00 F8 FF FF 0F BF 5E 28");
    auto actions = hook::pattern("66 A1 ? ? ? ? 66 8B D0 81 E2 00 03 00 00 66 85 D2");
    auto center = hook::pattern("66 8B 0D ? ? ? ? 81 E1 00 10 00 00 66 85 C9 74 ? 66 8B 15 ? ? ? ? 81 E2 00 10 00 00 66 85 D2 75 ? F6 05 ? ? ? ? F0 75 ? 66 8B 0D");
    auto cinematic = hook::pattern("84 1D ? ? ? ? A1 ? ? ? ? 0F 85 ? ? ? ? 3B C6");
    auto lookAt = hook::pattern("A1 ? ? ? ? 3D 00 00 00 80 74 ? 8B 15 ? ? ? ? 8B 2D");

    const auto unique = [](hook::pattern& pattern, const char* name)
    {
        const auto count = pattern.size();
        if (count == 1)
            return true;
        LogMessage(format("MouseLook: disabled (%s pattern matches=%u)", name, static_cast<unsigned>(count)));
        return false;
    };

    // Validate everything before installing any hook, including on other regional executables.
    if (!unique(dispatcher, "dispatcher") || !unique(frame, "frame") ||
        !unique(idle, "idle orbit") || !unique(moving, "moving orbit") ||
        !unique(placement, "placement") || !unique(actions, "actions") ||
        !unique(center, "recenter") || !unique(cinematic, "cinematic") || !unique(lookAt, "look-at"))
    {
        return false;
    }

    g_pVisorCameraMode = *dispatcher.get_first<uint32_t*>(1);
    g_pCameraState = *dispatcher.get_first<uint8_t*>(6);
    g_pPlayerFacingYaw = reinterpret_cast<uint16_t*>(g_pCameraState - 0x92);
    g_pPlayerDesiredYaw = reinterpret_cast<uint16_t*>(g_pCameraState - 0x58);
    g_pGameplayActions = *actions.get_first<uint16_t*>(2);
    g_pRawInputActions = *center.get_first<uint16_t*>(3);
    g_pPreviousRawInputActions = *center.get_first<uint16_t*>(21);
    g_pCinematicCameraFlags = *cinematic.get_first<uint8_t*>(2);
    g_pCameraLookAtX = *lookAt.get_first<int32_t*>(1);
    g_pThirdPersonCameraDistance = *idle.get_first<int32_t*>(6);

    // Ghidra: idle/moving orbit calculations, followed by final camera placement.
    injector::MakeInline<IdleCameraOrbitHook>(idle.get_first(0), idle.get_first(10));
    injector::MakeInline<MovingCameraOrbitHook>(moving.get_first(0), moving.get_first(8));
    injector::MakeInline<CameraPlacementHook>(placement.get_first(0), placement.get_first(8));

    auto cameraUpdateCall = frame.get_first(20);
    sub_UpdateCameraController_addr = reinterpret_cast<uintptr_t>(cameraUpdateCall) + 5 +
        *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(cameraUpdateCall) + 1);
    injector::MakeCALL(cameraUpdateCall, UpdateCameraControllerWithMouseLook);
    return true;
}

// Safety fallback used whenever the ini value is invalid, unparseable, zero, negative, NaN,
// or a raw (non-keyword) infinity.
const float RENDER_DISTANCE_DEFAULT = sqrtf(FLT_MAX);

// Default game render distance value (closest match to the game's original render distance,
// per empirical testing on the first level using the ceiling lamp behind the bedroom door).
constexpr const float RENDER_DISTANCE_MATCHING_GAME = 1.45e8f;

// Safety cap for finite values, to avoid destabilising the FPU / game logic with extreme numbers.
// INFINITY and SQRT_FLT_MAX are intentionally exempt from this cap, as they are explicit, known-safe sentinels.
constexpr float RENDER_DISTANCE_MAX = 1e15f;

/**
Parses the "RenderDistanceValue" ini entry into the float written to the distance-culling threshold.
Accepts plain decimal or scientific notation, with or without a trailing 'f'/'F' suffix
(e.g. "1.45e8", "1.45e8f"), plus the case-insensitive keywords "INFINITY" and "SQRT_FLT_MAX".
Falls back to RENDER_DISTANCE_DEFAULT for any input that is invalid, non-positive, NaN, or a
raw (non-keyword) infinity. Finite values above RENDER_DISTANCE_MAX are clamped.
*/
float ParseRenderDistanceValue(const std::string& rawValue)
{
    // Trim surrounding whitespace
    std::string value = rawValue;
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return RENDER_DISTANCE_DEFAULT;

    value.erase(0, start);
    value.erase(value.find_last_not_of(" \t\r\n") + 1);

    // Case-insensitive keyword check
    std::string upperValue = value;
    std::transform(upperValue.begin(), upperValue.end(), upperValue.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upperValue == "INFINITY")
        return INFINITY;    // literal infinity, matching upstream's code: **flt_5088B0_addr = INFINITY;

    if (upperValue == "SQRT_FLT_MAX")
        return sqrtf(FLT_MAX);

    // Try to parse a plain or scientific-notation float (strtof natively handles "1.45e8")
    char* endPtr = nullptr;
    float parsed = strtof(value.c_str(), &endPtr);

    // Reject if nothing was parsed at all
    if (endPtr == value.c_str())
        return RENDER_DISTANCE_DEFAULT;

    // Allow an optional trailing 'f'/'F' float-literal suffix (e.g. "1.45e8f", "100f")
    if ((*endPtr == 'f' || *endPtr == 'F') && *(endPtr + 1) == '\0')
        ++endPtr;

    // Reject if there's any other leftover/garbage after the number
    if (*endPtr != '\0')
        return RENDER_DISTANCE_DEFAULT;

    // Reject NaN and any raw (non-keyword) infinity, e.g. someone typing "inf" directly
    if (std::isnan(parsed) || std::isinf(parsed))
        return RENDER_DISTANCE_DEFAULT;

    // 0 or negative is treated as invalid/disabled -> fall back to game's default
    if (parsed <= 0.0f)
        return RENDER_DISTANCE_MATCHING_GAME;

    // Clamp finite values against the safety cap
    if (parsed > RENDER_DISTANCE_MAX)
        return RENDER_DISTANCE_MAX;

    return parsed;
}

DWORD WINAPI Init(LPVOID bDelay)
{
    /* INITIALISE */
    auto pattern = hook::pattern("03 D1 2B D7 85 D2 7E 09 52 E8 ? ? ? ?"); //4909B5

    if (pattern.count_hint(1).empty() && !bDelay)
    {
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&Init, (LPVOID)true, 0, NULL);
        return 0;
    }

    if (bDelay)
        while (pattern.clear().count_hint(1).empty()) { Sleep(0); };

    CIniReader iniReader("ToyStory2Fix.ini");
    constexpr const char* INI_KEY = "ToyStory2Fix";

    g_logPath = iniReader.GetIniPath();
    g_logPath = g_logPath.substr(0, g_logPath.find_last_of('.')) + ".log";

    if (iniReader.ReadBoolean(INI_KEY, "FixHighResolution", true))
    {
        if (InstallHighResolutionHook())
            LogMessage("FixHighResolution: enabled (experimental native limit fix and safe startup failure handling)");
    }

    if (iniReader.ReadBoolean(INI_KEY, "FixAltTab", true) && InstallAltTabRecoveryHooks())
        LogMessage("FixAltTab: enabled (focused exclusive ownership and surface/texture recovery; native Direct3D6 only)");

    if (iniReader.ReadBoolean(INI_KEY, "IncreaseObjectRenderDistance", true))
    {
        const float distance = ObjectDrawDistance::SanitizeDistance(
            iniReader.ReadFloat(INI_KEY, "ObjectDrawDistance", 65536.0f));
        const auto status = NativeObjectRendering::Install(GetModuleHandleW(nullptr),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderGameplayWithExtendedObjectDistance)), distance);
        if (status == NativeObjectRendering::Status::Applied)
            LogMessage(format("IncreaseObjectRenderDistance: finite distance=%g; sprite queue=16384, transforms=8192, render entries/triangles=32768; gameplay actor pool remains 64",
                distance));
        else
            LogMessage(format("IncreaseObjectRenderDistance: skipped safely (%s); object distances/pools left unchanged",
                NativeObjectRendering::StatusName(status)));
    }

    if (iniReader.ReadBoolean(INI_KEY, "IncreaseSceneryRenderDistance", true))
    {
        const auto status = NativeSceneryRendering::Install(GetModuleHandleW(nullptr));
        if (status == NativeSceneryRendering::Status::Applied)
            LogMessage(format("IncreaseSceneryRenderDistance: detailed scenery split extended toward %g within the native far clip; bounded spatial grid; particle spawning/fade table unchanged",
                NativeObjectRendering::drawDistance));
        else
            LogMessage(format("IncreaseSceneryRenderDistance: skipped safely (%s)", NativeSceneryRendering::StatusName(status)));
    }

    if (iniReader.ReadBoolean(INI_KEY, "IncreaseEffectRenderDistance", true))
    {
        const auto status = NativeMoleHoleSmoke::Install(GetModuleHandleW(nullptr));
        if (status == NativeMoleHoleSmoke::Status::Applied)
            LogMessage(format("IncreaseEffectRenderDistance: mole-hole smoke extended toward %g within native far clip; 64 additional visual-only particle slots; native 64-slot gameplay particle pool/RNG unchanged",
                MoleHoleSmoke::DistanceLimit(NativeObjectRendering::drawDistance, MoleHoleSmoke::MaximumDistance)));
        else
            LogMessage(format("IncreaseEffectRenderDistance: skipped safely (%s)", NativeMoleHoleSmoke::StatusName(status)));
    }

    if (iniReader.ReadBoolean(INI_KEY, "MouseButtons", true))
    {
        g_mouseButtons.enabled = InstallMouseButtonHook();
        if (g_mouseButtons.enabled)
            LogMessage("MouseButtons: enabled (left click=fire, right click=visor; keyboard/controller preserved)");
    }

    if (iniReader.ReadBoolean(INI_KEY, "MouseLook", true))
    {
        g_mouseLook.invertX = iniReader.ReadBoolean(INI_KEY, "InvertMouseX", false);
        g_mouseLook.invertY = iniReader.ReadBoolean(INI_KEY, "InvertMouseY", true);
        g_mouseLook.sensitivity = iniReader.ReadFloat(INI_KEY, "MouseSensitivity", 4.0f);
        if (!std::isfinite(g_mouseLook.sensitivity) || g_mouseLook.sensitivity <= 0.0f)
            g_mouseLook.sensitivity = 4.0f;
        g_mouseLook.sensitivity = std::clamp(g_mouseLook.sensitivity, 0.1f, 32.0f);

        g_mouseLook.enabled = InstallMouseLookHooks();
        if (g_mouseLook.enabled)
        {
            LogMessage(format("MouseLook: camera-only orbit enabled (sensitivity=%.2f, invertX=%d, invertY=%d)",
                g_mouseLook.sensitivity, g_mouseLook.invertX, g_mouseLook.invertY));
        }
    }

    if (iniReader.ReadBoolean(INI_KEY, "FixFramerate", true)) {
        timeGetDevCaps(&tc, sizeof(tc));
        QueryPerformanceFrequency(&Frequency);

        pattern = hook::pattern("8B 0D ? ? ? ? 2B F1 3B"); //4011DF
        Variables.speedMultiplier = *(uint32_t**)pattern.get_first<uint32_t**>(2);
        pattern = hook::pattern("39 3D ? ? ? ? 75 27"); //403C3A
        Variables.isDemoMode = *(bool**)pattern.get_first<bool*>(2);

        pattern = hook::pattern("C7 05 ? ? ? ? 00 00 00 00 E8 ? ? ? ? E8 ? ? ? ? 33"); //49BBD8
        sub_490860_addr = ((uintptr_t)pattern.get_first(15) + *pattern.get_first<uintptr_t>(11));
        injector::MakeCALL(pattern.get_first(10), sub_490860);
        pattern = hook::pattern("83 C4 08 6A 01 E8 ? ? ? ?"); //441906
        injector::MakeCALL(pattern.get_first(5), sub_490860);
        pattern = hook::pattern("6A 00 E8 ? ? ? ? 6A 01 E8 ? ? ? ? 83"); //4419F4
        injector::MakeCALL(pattern.get_first(2), sub_490860);
    }


    /* Allow 32-bit modes regardless of registry settings - thanks hdc0 */
    if (iniReader.ReadBoolean(INI_KEY, "Allow32Bit", true)) {
        pattern = hook::pattern("74 0B 5E 5D B8 01 00 00 00"); //4ACA44
        injector::WriteMemory<uint8_t>(pattern.get_first(0), '\xEB', true);
    }

    /* Fix "Unable to enumerate a suitable device - thanks hdc0 */
    if (iniReader.ReadBoolean(INI_KEY, "IgnoreVRAM", true)) {
        pattern = hook::pattern("74 44 8B 8A 50 01 00 00 8B 91 64 03 00 00"); //4ACAC2
        injector::WriteMemory<uint8_t>(pattern.get_first(0), '\xEB', true);
    }

    /* Increase enemy render distance - thanks DavidJ75 */
    if (iniReader.ReadBoolean(INI_KEY, "IncreaseEnemyRenderDistance", true)) {
        pattern = hook::pattern("03 DA 03 CF 3B D9 7D 6D");

        unsigned char enemyRenderDistanceIncreaseFix[] = { 0x90, 0x90 };

        injector::WriteMemoryRaw(pattern.get_first(6), enemyRenderDistanceIncreaseFix, sizeof(enemyRenderDistanceIncreaseFix), true);
    }

    /* Make game portable */
    if (iniReader.ReadBoolean(INI_KEY, "PortableGame", false)) {
        // Bypass the original installation-registry lookup and CD validation-file check.
        pattern = hook::pattern("81 EC 10 04 00 00");

        unsigned char portableGameFix[] = { 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90 };

        injector::WriteMemoryRaw(pattern.get_first(0), portableGameFix, sizeof(portableGameFix), true);
    }

    /* Allow copyright/ESRB screen to be skipped immediately */
    if (iniReader.ReadBoolean(INI_KEY, "SkipSplash", true)) {
        pattern = hook::pattern("66 8B 3D ? ? ? ? 83 C4 1C"); //438586
        struct CopyrightHook
        {
            void operator()(injector::reg_pack& regs)
            {
                _asm mov di, 1
            }

        }; injector::MakeInline<CopyrightHook>(pattern.get_first(0), pattern.get_first(7));
    }

    /* Increase Render Distance to Max */
    if (iniReader.ReadBoolean(INI_KEY, "IncreaseRenderDistance", true)) {
        std::string rawValue = iniReader.ReadString(INI_KEY, "RenderDistanceValue", "SQRT_FLT_MAX");
        float renderDistanceValue = ParseRenderDistanceValue(rawValue);

        LogMessage(format("IncreaseRenderDistance: ini value \"%s\" parsed to %g", rawValue.c_str(), renderDistanceValue));

        pattern = hook::pattern("D9 44 24 04 D8 4C 24 04 D9 1D ? ? ? ? D9 44 24 08 D8 4C 24 08 D9 1D ? ? ? ? C3"); //4BC410
        if (pattern.size() == 1)
        {
            const auto threshold = *pattern.get_first<float*>(10);
            *threshold = renderDistanceValue;
            auto store = SceneRenderDistance::PreserveThresholdStore;
            injector::WriteMemoryRaw(pattern.get_first(8), store.data(), store.size(), true);
        }
        else
        {
            LogMessage("IncreaseRenderDistance: skipped safely (distance setter signature does not match uniquely)");
        }
    }

    /* Fix widescreen once game loop begins */
    if (iniReader.ReadBoolean(INI_KEY, "Widescreen", true)) {
        pattern = hook::pattern("8D 44 24 10 50 57 E8 ? ? ? ? 83"); //4317EC
        sub_49D910_addr = ((uintptr_t)pattern.get_first(11) + *pattern.get_first<uintptr_t>(7));
        injector::MakeCALL(pattern.get_first(6), sub_49D910);
    }

    /* Fix texture-mapping bugs */
    if (iniReader.ReadBoolean(INI_KEY, "TextureFix", true)) {
        pattern = hook::pattern("DD 45 F4 5B DD 58 10 A1 ?? ?? ?? ?? C9 C3"); //4DBD3D
        unsigned char textureFix[] = {0x8B, 0xD1,
                             0xC1, 0xE9, 0x02,
                             0x56,
                             0x51,
                             0xD9, 0x46, 0x14,
                             0xC7, 0x46, 0x14, 0x00, 0x00, 0x80, 0xB7,
                             0xD8, 0x46, 0x14,
                             0xD9, 0x5E, 0x14,
                             0xD9, 0x46, 0x1C,
                             0xC7, 0x46, 0x1C, 0x00, 0x00, 0x80, 0x37,
                             0xD8, 0x46, 0x1C,
                             0xD9, 0x5E, 0x1C,
                             0xD9, 0x46, 0x20,
                             0xC7, 0x46, 0x20, 0x00, 0x00, 0x80, 0x37,
                             0xD8, 0x46, 0x20,
                             0xD9, 0x5E, 0x20,
                             0x83, 0xC6, 0x24,
                             0x83, 0xE9, 0x09,
                             0x75, 0xC8,
                             0x59,
                             0x5E,
                             0xC3};
        injector::WriteMemoryRaw(pattern.get_first(0x1E), textureFix, sizeof(textureFix), true);

        auto textureJump = hook::pattern("8B D1 C1 E9 02 F3 A5 8B CA EB AB"); //4B300E
        injector::WriteMemory<uint8_t>(textureJump.get_first(0), '\xE8', true);
        injector::WriteMemory(textureJump.get_first(1), (int)pattern.get_first(0x1E) - (int)textureJump.get_first(5), true);
    }

    /* Fix broken disk launcher at 60 FPS */
    if (iniReader.ReadBoolean(INI_KEY, "DiskFix", true)) {
        pattern = hook::pattern("A1 ?? ?? ?? ?? 0F AF C1 99 F7 7C 24 18"); //411099
        unsigned char diskFix[] = {0xB8, 0x02, 0x00, 0x00, 0x00};
        injector::WriteMemoryRaw(pattern.get_first(0), diskFix, sizeof(diskFix), true);
    }

    /* Fix fast Zurg and other flying enemies at 60 FPS */
    if (iniReader.ReadBoolean(INI_KEY, "ZurgFix", true)) {
        pattern = hook::pattern("DD 45 F4 5B DD 58 10 A1 ?? ?? ?? ?? C9 C3"); //4DBD3D

        auto zurgXJump = hook::pattern("C1 FB 04 2B CB 8B 5C 24 24 89 0E"); //407F8E
        unsigned char zurgXFix[] = {0x0F, 0xAF, 0x1D, *zurgXJump.get_first<unsigned char>(0x88), *zurgXJump.get_first<unsigned char>(0x89), *zurgXJump.get_first<unsigned char>(0x8A), *zurgXJump.get_first<unsigned char>(0x8B),
                           0xC1, 0xFB, 0x05,
                           0x29, 0xD9,
                           0xC3,
                           0x90};

        auto zurgZJump = hook::pattern("C1 FA 04 2B CA 89 4E 08 0F BF 46 0E"); //407FB0
        unsigned char zurgZFix[] = {0x0F, 0xAF, 0x15, *zurgXJump.get_first<unsigned char>(0x88), *zurgXJump.get_first<unsigned char>(0x89), *zurgXJump.get_first<unsigned char>(0x8A), *zurgXJump.get_first<unsigned char>(0x8B),
                           0xC1, 0xFA, 0x05,
                           0x29, 0xD1,
                           0xC3,
                           0x90};

        injector::WriteMemoryRaw(pattern.get_first(0x70), zurgXFix, sizeof(zurgXFix), true);
        injector::WriteMemoryRaw(pattern.get_first(0x8E), zurgZFix, sizeof(zurgZFix), true);

        injector::WriteMemory<uint8_t>(zurgXJump.get_first(0), '\xE8', true);
        injector::WriteMemory(zurgXJump.get_first(1), (int)pattern.get_first(0x70) - (int)zurgXJump.get_first(5), true);

        injector::WriteMemory<uint8_t>(zurgZJump.get_first(0), '\xE8', true);
        injector::WriteMemory(zurgZJump.get_first(1), (int)pattern.get_first(0x8E) - (int)zurgZJump.get_first(5), true);
    }

    return 0;
}


BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Init(NULL);
    }
    return TRUE;
}
