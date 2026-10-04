# Mouse input regression tests

From the repository root in an **x86 Visual Studio Developer PowerShell**:

```powershell
New-Item -ItemType Directory -Force build/tests | Out-Null

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/mouse_look_camera.cpp /Fobuild/tests/mouse_look_camera.obj /Febuild/tests/mouse_look_camera.exe
./build/tests/mouse_look_camera.exe

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/mouse_button_actions.cpp /Fobuild/tests/mouse_button_actions.obj /Febuild/tests/mouse_button_actions.exe
./build/tests/mouse_button_actions.exe

cl /nologo /std:c++latest /EHsc /W3 /WX /MT /Iincludes /Iexternal/hooking /Iexternal/injector/include /Iexternal/inireader tests/mouse_button_hooks.cpp includes/stdafx.cpp external/hooking/Hooking.Patterns.cpp /Fobuild/tests/ /Febuild/tests/mouse_button_hooks.exe /link winmm.lib user32.lib
./build/tests/mouse_button_hooks.exe
```

The tests cover camera-only orbit angles, keyboard camera priority, pitch collision corrections, native fire/visor hold and press-edge behavior, keyboard coexistence, focus loss and refocus, and register replay. The button hook test replaces focus/button APIs with deterministic samples; it does not click or move the desktop cursor.

In-game checks are still required for camera feel, left-click firing/charging, right-click visor entry/exit (including holding right click through transitions), Ctrl/Tab coexistence, and Alt-Tab while buttons are held.
