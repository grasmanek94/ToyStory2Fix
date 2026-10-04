#include "stdafx.h"
#include "MouseLookCamera.h"
#include "MouseButtonActions.h"
#include <MMSystem.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>

uintptr_t sub_490860_addr;
uintptr_t sub_49D910_addr;
uintptr_t sub_UpdateCameraController_addr;

struct MouseLookSettings
{
    bool enabled = false;
    bool invertX = false;
    bool invertY = false;
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
    Variables.nWidth = *(int*)pattern.get_first(2);
    Variables.nHeight = *((int*)pattern.get_first(2) + 4);
    Variables.fAspectRatio = float(Variables.nWidth) / float(Variables.nHeight);
    Variables.fScaleValue = 1.0f / Variables.fAspectRatio;
    Variables.f2DScaleValue = (4.0f / 3.0f) / Variables.fAspectRatio;

    /* Fix 3D stretch */
    pattern = hook::pattern("C7 40 44 00 00 40 3F"); //4CE80F
    struct Widescreen3DHook
    {
        void operator()(injector::reg_pack& regs)
        {
            float* ptrScaleValue = (float*)regs.eax + 0x44;
            *ptrScaleValue = Variables.fScaleValue;
        }
    }; injector::MakeInline<Widescreen3DHook>(pattern.get_first(0), pattern.get_first(6));


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

    if (iniReader.ReadBoolean(INI_KEY, "MouseButtons", true))
    {
        g_mouseButtons.enabled = InstallMouseButtonHook();
        if (g_mouseButtons.enabled)
            LogMessage("MouseButtons: enabled (left click=fire, right click=visor; keyboard/controller preserved)");
    }

    if (iniReader.ReadBoolean(INI_KEY, "MouseLook", true))
    {
        g_mouseLook.invertX = iniReader.ReadBoolean(INI_KEY, "InvertMouseX", false);
        g_mouseLook.invertY = iniReader.ReadBoolean(INI_KEY, "InvertMouseY", false);
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

    /* Increase enemy render distance - thanks DavidJ75 */
    if (iniReader.ReadBoolean(INI_KEY, "PortableGame", true)) {
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

        pattern = hook::pattern("D9 44 24 04 D8 4C 24 04 D9 1D"); //4BC410

        float** flt_5088B0_addr = (float**)pattern.get_first(10);
        **flt_5088B0_addr = renderDistanceValue;    // sqrtf(FLT_MAX)=1.84467e+19 | INFINITY=inf | 1.45e8f is the more similar to game's default value.
        injector::MakeNOP(pattern.get_first(8), 6);
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
