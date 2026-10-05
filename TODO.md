# Follow-up fixes

- **Broader mole-hole smoke coverage:** The user reported the fix works and authorized merge/push to master. Check the old cutoff boundary, pause, death/respawn, level transitions, input and Alt-Tab on the verified new binary. The agent did not replace the installed ASI/INI after the earlier commit-only choice; preserve installed settings and obtain installation permission before doing so. Addresses, evidence, safeguards and the report-versus-installation distinction are in `.ai/mole-hole-smoke/README.md` and `.ai/project-state.md`.

## Completed

- **Persistent mole-hole smoke cutoff:** Read-only near/far snapshots identified type `0x3A` and the independent 4096-unit emitter limit. `IncreaseEffectRenderDistance` extends only that emitter within the native far clip, adding 64 visual-only slots while preserving gameplay particle capacity/RNG and the fade table. All 13 regression suites, optimized native replay and Win32 Release build pass; the user subsequently reported "awesome , works". See `.ai/mole-hole-smoke/README.md` for remaining manual coverage.

- **Grass draw distance (user-confirmed):** The user confirmed grass is now visible with the guarded detailed-scenery split/grid extension. The implementation is committed and pushed as `866ea05` on `feature/extended-object-draw-distance`. Broader scenery/transition coverage remains in the manual checklist.
- **Alt-Tab black screen (user-confirmed):** The revised `FixAltTab` reclaims exclusive ownership using the original display instance/window/FPU flags, then recovers surfaces/textures. The user confirmed it resolves their black screen after returning from Alt-Tab. Broader driver, FMV and transition coverage remains in the manual checklist. See `tests/alt_tab_rendering_notes.md` and `tests/README.md`.
