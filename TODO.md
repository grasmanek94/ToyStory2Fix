# Follow-up fixes

- **Grass and fog/special-effect gameplay verification:** Added a guarded detailed-scenery split/grid extension using the expanded renderer pools. Native world/grid replay passes, but Andy's neighbourhood grass and the reported disappearing fog/effects still need a same-position in-game comparison. If an effect remains missing, identify its emitter before extending spawning or allocating additional simulation slots. Atmospheric fog, particle spawning/lifetimes and the shared fade table are deliberately unchanged. See `tests/scenery_rendering_notes.md`; do not mark this task complete on automated tests alone.

## Completed

- **Alt-Tab black screen (user-confirmed):** The revised `FixAltTab` reclaims exclusive ownership using the original display instance/window/FPU flags, then recovers surfaces/textures. The user confirmed it resolves their black screen after returning from Alt-Tab. Broader driver, FMV and transition coverage remains in the manual checklist. See `tests/alt_tab_rendering_notes.md` and `tests/README.md`.
