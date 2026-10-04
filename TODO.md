# Follow-up fixes

- **Fog/special-effect draw distance (paused):** The user reports that the fog effect still disappears after hitting the soldier with a slam in the mole holes in Andy's neighbourhood. The scenery patch fixes grass but does not resolve this effect. Wait for the user's command before starting further investigation. Identify the effect's rendering/activation path before extending spawning or allocating simulation slots; do not raise shared fade-table or gameplay activation limits without auditing their capacity. See `tests/scenery_rendering_notes.md`.

## Completed

- **Grass draw distance (user-confirmed):** The user confirmed grass is now visible with the guarded detailed-scenery split/grid extension. The implementation is committed and pushed as `866ea05` on `feature/extended-object-draw-distance`. Broader scenery/transition coverage remains in the manual checklist.
- **Alt-Tab black screen (user-confirmed):** The revised `FixAltTab` reclaims exclusive ownership using the original display instance/window/FPU flags, then recovers surfaces/textures. The user confirmed it resolves their black screen after returning from Alt-Tab. Broader driver, FMV and transition coverage remains in the manual checklist. See `tests/alt_tab_rendering_notes.md` and `tests/README.md`.
