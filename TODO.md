# Follow-up fixes

- **Grass and fog/special-effect draw distances:** User confirmed the extended coin/object draw-distance fix works well, but grass and special effects such as fog still disappear at the original short ranges. Investigate their separate culling, fade and allocation paths in a future task. Do not raise shared fade-table or gameplay activation limits without auditing/expanding their capacity.

## Completed

- **Alt-Tab black screen (user-confirmed):** The revised `FixAltTab` reclaims exclusive ownership using the original display instance/window/FPU flags, then recovers surfaces/textures. The user confirmed it resolves their black screen after returning from Alt-Tab. Broader driver, FMV and transition coverage remains in the manual checklist. See `tests/alt_tab_rendering_notes.md` and `tests/README.md`.
