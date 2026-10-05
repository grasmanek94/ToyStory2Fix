# AI investigation handoff

Start with [project state and constraints](project-state.md), then the topic READMEs:

- [Ghidra project paths, executable identity and reverse-engineering pitfalls](ghidra/README.md)
- [Rendering distances, pool layouts, scenery and gameplay isolation](rendering/README.md)
- [Persistent mole-hole smoke addresses, evidence and implementation](mole-hole-smoke/README.md)
- [Mouse/input/camera hooks and native action semantics](input/README.md)
- [High resolution, widescreen and Alt-Tab recovery](display/README.md)
- [Build/test workflow, portable installation and legacy fixes](build-and-installation/README.md)

These notes are checked into the repository so the findings do not depend on a previous chat, temporary process snapshots or one local Ghidra database. Addresses are **absolute virtual addresses for the fingerprinted x86 executable**, not portable offsets for another release. Verify fingerprints and byte guards before patching.

Other detailed address maps retained in the repository:

- [Object rendering, pool references and gameplay isolation](../tests/object_rendering_notes.md)
- [Scenery split, grids, portals and fade-table audit](../tests/scenery_rendering_notes.md)
- [Alt-Tab ownership/surface/texture recovery](../tests/alt_tab_rendering_notes.md)
- [Test commands and manual checklist](../tests/README.md)

Do not treat an automated replay as a user-confirmed gameplay result. Record new evidence and remaining checks here when continuing.
