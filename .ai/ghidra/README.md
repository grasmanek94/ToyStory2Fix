# Ghidra paths and reverse-engineering reference

## Local project and imported programs

Verified on 2026-10-05:

- Project **`ts2`** at `C:\Users\Rafal\Desktop\Game\GH\ts2`.
- Main program tool name **`toy2.exe`**, project domain-file path **`/toy2.exe.1`**. The `.1` suffix is significant when opening by project path; do not assume `/toy2.exe` exists.
- Source executable `C:\Users\Rafal\Desktop\Toy Story 2\toy2.exe`.
- Language `x86:LE:32:default`, Windows compiler, little-endian, one default `ram` address space, no overlays. Image base `00400000`, PE size `00A7F000`, timestamp `381979B4`.
- Game SHA-256 `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`.
- Imported native DLL programs **`/d3dim.dll`** (tool name `d3dim.dll`, Ghidra base `74000000`) and **`/ddraw.dll`** (tool name `ddraw.dll`, base `51000000`), from `C:\Windows\SysWOW64`.
- Local `d3dim.dll` SHA-256 `adba7bd0997aff8ce9d68522a3e08c804b8bcc4b2910d2deb4c790f5da02f764`; `ddraw.dll` SHA-256 `d24c374a10138e4ffdf7b2dd68cff5687d3dff28b2b15279a241788a12d51c55`.

DLL Ghidra bases are analysis addresses, **not guaranteed live load addresses**. Convert verified sites to RVAs and check the actual loaded module/version; runtime patches use signatures, not these DLL absolute addresses.

At discovery, local Ghidra MCP was at `http://127.0.0.1:8089`, socket `%LOCALAPPDATA%\Temp\ghidra-mcp-Rafal\ghidra-29864.sock`. PID/socket/connection availability are ephemeral. Re-discover instead of hard-coding. Explicitly select the target program on each analysis call because multiple programs are open. The game program was saved after the smoke function/global names, explanatory comments and corrected motion-spawner prototype were applied.

## Relevant game data paths

- Game directory: `C:\Users\Rafal\Desktop\Toy Story 2`.
- `level02\level.ngn`, `level02\level.dat`, `level02\Pconv.cfg`: neighbourhood scene/resources.
- `creatures.cfg` maps creature type 13 to `ARMY`; actor initialization `00406CD0` dispatches it to `00418610`.
- Read-only snapshots/helper were in `%LOCALAPPDATA%\Temp\opencode`; durable near/far observations and exact fixture values are in [the smoke README](../mole-hole-smoke/README.md), not dependent on those files.

## Important analysis pitfalls

1. **Decompiler types are hypotheses.** `0040FDF0` originally decompiled as `void`, but `0041007F..0041008B` forwards allocator EAX to callers. Corrected signature: `int * __cdecl SpawnGameplayParticleWithMotion(int x,int y,int z,int particleType,int motionTemplate)`.
2. **Exact immediates matter.** Smoke height `0xFFFFF450` is -2992, not a rounded -3000. Native replay caught this guard error.
3. **Not every referenced constant is an allocation base.** `004B5CF8` has value `0095C860`, but is the end of the adjacent triangle-bucket table. Do not relocate it with the entry pool.
4. **Not all functions are initially defined.** Sprite allocator `004B9020` and level script `004190C0` needed recovery; xrefs/function lists alone miss paths. Pool audit scans PE relocation operands as well as recovered functions.
5. **Respect x87 stack effects.** Scene distance setter `004BC410` pops on both stores. Replacing its first store with NOPs leaks an x87 value; retain the pop. Replay tests keep a live caller value for 512 calls.
6. **Separate gameplay and rendering.** Actor activation, renderer queues, emitter ranges, simulation pools, scenery split, hardware projection and the fade table are independent paths. A larger geometry threshold is not proof that another path is extended safely.
7. **Fixed-point versus renderer units.** Camera/particle positions use 32x coordinates; hole-list coordinates do not. Shifted integer distance thresholds are not raw renderer distances.
8. **Transient burst versus persistent emitter.** Soldier slam dust type `0x29` is not completed-hole smoke type `0x3A`. Do not label all particles/chains/rising sprites "fog".
9. **Preserve named neighbours.** Applying a large Ghidra data type can evict other symbols. Prefer narrow verified types/comments; do not clear user annotations blindly.
10. **Read-only live inspection is not debugging execution.** Snapshotting used `OpenProcess(0x1010)`/`ReadProcessMemory`; no live function calls, memory writes or stopping the game. Revalidate process path/base/fingerprint each time.

The topic READMEs document names, addresses, recovered layouts, evidence, hooks and remaining uncertainty without redistributing executable/DLL bytes or relying on the local project being available.
