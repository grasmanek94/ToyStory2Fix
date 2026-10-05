# Project state — 2026-10-05

## Current result

- Implementation branch: `feature/extended-object-draw-distance`; merged target: `master`; remote: `https://github.com/grasmanek94/ToyStory2Fix`. Use `git status`/`git log` to determine the current checkout and final commit IDs rather than assuming the prior session's branch.
- Grass extension is user-confirmed (`866ea05`; documentation `06cbd43`). Input/high-resolution/object changes reached master in `497268c`; the user-confirmed Alt-Tab correction reached master in `a1880a9` (feature implementation `68079ab`). Master was merged into this feature branch in `bf8bf17`.
- The disappearing persistent mole-hole effect is now identified as type `0x3A` smoke with an independent 4096-unit level-script emitter cutoff. The new `IncreaseEffectRenderDistance` implementation adds 64 isolated visual-only slots and extends only that emitter. See [the evidence/address map](mole-hole-smoke/README.md).
- **All 13 regression suites, actual-executable emitter replay, `/O2` emitter replay and Win32 Release build passed.** The user subsequently said **"awesome , works, merge to master and push"**; record that report as received. Broader in-game transition/driver coverage remains manual.
- The user earlier chose **commit/push only**, not installation. The agent did not replace installed ASI/INI or write live memory. At the last independent file check, the installed ASI still had the older scenery-enabled hash below; the user's report does not establish which binary was tested. Do not invent an agent-performed installation or measured smoke-build runtime log. Obtain permission before installing a test build or writing live memory.

## Preserve these requirements

- Keep the confirmed grass/scenery, input, object/high-resolution and Alt-Tab fixes. Preserve mouse orbit, keyboard/controller, left-click fire, right-click visor, target lock, portable-game behavior and timing.
- Keep the **8192-per-axis resolution ceiling** and desktop-before-launch high-resolution guidance. Automatic fullscreen-clipping work remains paused.
- Do not truncate the complete `PortableGame` INI comment or overwrite installed option values. Installed `PortableGame = true` must remain true if an installation is later authorized.
- Investigate separate culling/fade/allocation paths before extending another effect. Do not blindly raise `0054BEF0`, enlarge its fixed fade table, or increase gameplay activation/shared particle capacity.
- Use evidence-based Ghidra names; native motion spawner `0040FDF0` returns the allocator's EAX pointer even though it previously decompiled as `void`. See the epilogue evidence below.
- The user explicitly authorized this smoke/scenery merge and push to master. Working-branch commits/pushes are also authorized; obtain renewed permission for future unrelated master pushes.

## Local environment and installation state

- Repository: `C:\Users\Rafal\Desktop\Toy Story 2\scripts\ToyStory2Fix`.
- Game/executable: `C:\Users\Rafal\Desktop\Toy Story 2\toy2.exe`.
- Installed ASI/INI/log: `C:\Users\Rafal\Desktop\Toy Story 2\scripts` (outside the repository's `data/scripts`).
- Installed scenery-enabled ASI SHA-256 remains `318f3c33a239044d51979408b3f03fd0bacce0c6e37fb9b7b3dd335311f74949`.
- Previous installation backup: `%LOCALAPPDATA%\Temp\opencode\scenery-distance-backup-20261005-002243`.
- The smoke-enabled build is generated at `data/scripts/ToyStory2Fix.asi`; build products are ignored, not committed. Rebuild rather than relying on a temporary binary hash.
- Read-only snapshot helper and raw snapshots are temporary files under `%LOCALAPPDATA%\Temp\opencode`. Their relevant evidence is reproduced in this repository; do not rely on their continued presence. Process IDs and allocated world/camera pointers can change: revalidate before any new inspection.

## Next steps when installation is authorized

1. Ask the user to exit the game. Back up installed ASI/INI, preserve every existing setting, add only the effect toggle if necessary, and verify the copied binary and settings.
2. Confirm the log reports successful `IncreaseEffectRenderDistance` installation rather than a safe skip. It requires expanded object-renderer pools.
3. Compare three completed holes at the recorded near/far camera positions with the effect option on/off, restarting between changes. Check smoke appearance/transparency, the old cutoff boundary, pause/focus loss, death/respawn and level transitions.
4. Recheck fire/visor/target lock, mouse/keyboard/controller, timing, confirmed grass and Alt-Tab. Record actual user confirmation separately from automated results.

## Build/test reference

Use x86 Visual Studio tools. Full test commands are in `tests/README.md`.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe' build\ToyStory2Fix.sln /p:Configuration=Release /p:Platform=Win32 /p:PostBuildEventUseInBuild=false /verbosity:minimal
```

The disabled post-build event matters: it prevents a configured external game-copy step from replacing an installation during validation.
