# Input, camera orbit and native action logic

Files: `source/MouseLookCamera.h`, `source/MouseButtonActions.h`, input/camera hooks in `source/dllmain.cpp`. Hook installation derives pointers from unique signatures rather than assuming these addresses for every regional executable.

## Native functions and hook sites

| Address | Role |
| --- | --- |
| `00452180` | `UpdateRawInputActions`: copies current raw actions to previous, polls DirectInput, then stores new raw actions. |
| `00414AF0` | `PollDirectInputDevices`. |
| `00452191..00452198` | Mouse-button hook replaces only seven-byte `MOV CX,[00529B00]`, ORs mouse actions, preserves ECX upper half/EFLAGS; native store/snapshot retain hold/edge semantics. |
| `0049EBA0` | `MapRawInputToGameplayActions`. Raw and gameplay masks are not interchangeable. |
| `00436220` | `UpdatePlayerMovementAndAiming`: original visor transition/action-edge owner. |
| `00405860` | `UpdateCameraController`; wrapped call at **`0049E141`** samples mouse before native update, ends ownership frame afterward. |
| `004045E0` | `UpdateThirdPersonCamera`; separate idle/moving branches and final placement. |
| `00404AAA..00404AB4` | Idle orbit hook: replay signed pitch and native camera distance after applying camera-only angles. |
| `00404EB0..00404EB8` | Moving orbit hook: refresh ECX pitch; replay signed yaw/distance registers. |
| `004053B1..004053B9` | Final placement hook: keep mouse-selected pitch without bypassing native obstacle avoidance, replay original registers. |
| `004038E0` | `UpdateVisorCamera`. |

## State addresses and fields

| Address | Name / role |
| --- | --- |
| `00529B00` | `g_wRawInputActions`, polled raw mask (before current-frame snapshot). |
| `0088279C` / `00882794` | Current / previous raw actions. |
| `0052AD88` / `0052F2FE` | Current / previous mapped gameplay actions. |
| `0050A13C` | `g_dwVisorCameraMode`: 0 third person, 4 settled visor; intermediate transitions remain native. |
| `0052F3A0` | Camera-state base; yaw uint16 **`+0x28`** (`0052F3C8`), pitch int16 **`+0x2E`** (`0052F3CE`), collision flags **`+0x32`** (`0052F3D2`). |
| `0052F3AC` | Visor pitch uint32, camera **`+0x0C`**. |
| `0052F30E` / `0052F348` | Player facing / desired yaw. **Never write these for ordinary third-person mouse orbit.** Visor aiming intentionally changes player aim. |
| `0050A118` | Scripted look-at X; normal mouse orbit yields unless it is `INT32_MIN`. |
| `0050A128` | Native third-person camera distance. |
| `0052B816` | Scripted camera flags; cinematic bit 1 disables mouse orbit. |

## Design invariants

- Left mouse = raw **`0x8000`** (fire, default Left Ctrl); right mouse = **`0x0400`** (visor, default Tab). OR into original input, do not replace it. Native previous/current masks provide press edges and charge/hold behavior; do not repeatedly toggle visor manually.
- Sample only when a foreground window belongs to this process. Use `GetAsyncKeyState` held bit `0x8000`, not the unreliable pressed-since-last-call bit.
- Focus loss resets arming; a held button must be released after refocus before firing/toggling. Sampling gaps >250 ms reset state; this also avoids stale input across pauses.
- Mouse cursor is recentered within the game window only while focused. First sample, mode transitions, cinematic/look-at states and focus changes reset baseline, preventing jumps.
- Third-person mode owns **camera angles only**, applied after native follow logic but before collision rays. Keyboard camera bits `0x300` take yaw priority; recenter raw action edge `0x1000` releases ownership. Do not erase keyboard/controller masks.
- Third-person yaw wraps 12-bit; pitch clamped **-0x200..0x300**. Moving-branch pitch corrections with collision bits `0x0C` are retained; otherwise manual pitch is preserved against automatic reset.
- Visor yaw changes player aiming consistently; pitch uses signed 12-bit conversion and clamp **-0x338..0x320**. Modes other than 0/4 and cinematic transitions remain native.
- Sensitivity default **4.0**, finite clamp **0.1..32**; `InvertMouseY = true` default. `MouseButtons` and `MouseLook` independently toggle.
- Validate every required orbit/input signature before changing any site. Replaying overwritten instructions/registers is essential; do not shrink hook spans or leave residual opcodes.

Tests: `mouse_look_camera`, `mouse_button_actions`, `mouse_button_hooks`; hooks are also compiled into high-resolution and Alt-Tab forwarding tests. Camera feel, charge/fire, visor transitions, Ctrl/Tab/controller coexistence and focus behavior remain part of manual regression coverage.
