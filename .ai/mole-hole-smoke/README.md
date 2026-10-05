# Persistent mole-hole smoke: addresses, evidence and patch

Investigation date: **2026-10-05**. Scope: Andy's neighbourhood, `level02`. Implementation and automated validation complete. The user subsequently reported **"awesome , works"** and authorized merge/push to master. Broader gameplay/transition coverage remains manual. No installation was performed by the agent after the user's earlier commit-only choice; see the installation/evidence distinction in `.ai/project-state.md`.

## Executable identity and units

- `toy2.exe`, x86; image base `00400000`, image size `00A7F000`, PE timestamp `381979B4`.
- SHA-256: `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`.
- All addresses below are absolute virtual addresses for this executable. RVA = address minus `00400000`; do not blindly reuse them for another release or relocated process.
- Particle/player/render-camera coordinates use **32x fixed-point**. Mole-hole list coordinates are renderer units and the emitter shifts X/Z left by five. Native distance tests shift fixed-point deltas right by eight, so their squared thresholds correspond to `(renderer distance / 8)^2`.

## Distinguish the two effects

The soldier's slam handler produces a **temporary dust burst**, but that cannot explain smoke disappearing with distance and reappearing when returning after the soldier has gone. Persistent smoke is generated separately from the level's completed-hole records.

| Address | Evidence-based name / role |
| --- | --- |
| `00406CD0` | Actor initializer dispatches creature type 13 (`ARMY` in `creatures.cfg`) to the handler below. |
| `00418610` | `UpdateArmySoldierActor`: state `0x66`, slam bit `0x200` at actor `+0x40`; increments `0052B7D8`, calls `00410410(x,y-0x2000,z,0x32)`, removes actor through `00405D20(actor,2)`. Also has a timed type-`0x79` projectile producer. |
| `00410410` | `SpawnArmySlamDustBurst`: five type-`0x29` particles through `0040FDF0`, randomized duration/spin, plus temporary light `0049EE50`. Sprite 5, texture `0x1F`, atlas origin `(128,0)`. **Not the persistent effect.** |
| `004190C0` | `UpdateNeighborhoodLevelScript`: includes completed-hole smoke emitter, plus unrelated level events. Only one exact emitter call is extended. |
| `00419696..00419782` | Native round-robin hole-emission loop; one hole considered per elapsed game tick. Cursor advances even for uncompleted/out-of-range holes. |
| `00419709` | `CMP EBX,0x40000`, followed by `JGE` at `0041970F`: independent smoke-emission radius of approximately **4096 renderer units**. |
| `0041971C` | Emits type `0x3A`, motion template 3, at `(hole.x*32,-2992,hole.z*32)` through `0040FDF0`. Return EAX is ignored then reloaded at `00419727`. Other type-`0x3A` callers remain unmodified. |
| `00559C78` | `g_pNeighborhoodMoleHoleList`: allocated list; `uint16 count`, `uint16 reserved`, then `{int32 x,y,z}` records. Observed count seven; `y == INT32_MIN` marks completed holes. Y is a completion marker, not emission height. |
| `0052F6D4` | `g_nNeighborhoodSmokeHoleCursor`: native round-robin cursor, wraps at list count. |
| `0052F2D4` | Native elapsed game ticks used for emission/update; do not substitute wall-clock time. |
| `0052ADC0/0052ADC4/0052ADC8` | Fixed-point render-camera X/Y/Z; both emitter distance and native general-particle culls use these. |
| `0088278C` | Current level number; gate level-specific pointer reads on value 2. |
| `00B62410` | Current loaded world pointer; extra smoke clears if source list/world identity changes. |
| `00508D0C` | Native detailed-pass projection far clip, normally **48000**, never increased by this patch. |

**Exact height warning:** `PUSH 0xFFFFF450` at `00419716` is **-2992** (`-0xBB0`), not -3000. Camera Y distance uses `camera.y + 0xBB0`. The first replay rejected every additional emission because a rounded -3000 guard was wrong; correcting it made the actual native loop pass. Do not reintroduce that approximation.

## Read-only near/far evidence

The user slammed three holes and paused with two effects visible, then moved farther away and paused with the effects missing. Snapshot helper opened the game with read/query rights only (`0x1010`); no game-memory writes, debugger stops, live calls or installed-file changes were used.

| Paused snapshot | Camera in renderer units | Type `0x3A` smoke records |
| --- | --- | ---: |
| Near, 01:01:48 | `(-12486.9375,-791.625,-7093.6875)` | 13, around completed holes 2 and 3 |
| Far, 01:05:45 | `(-14081.375,-916.75,-3090.8125)` | 0 |

List in both snapshots (zero-based indices):

| Hole | Renderer X / Z | Completed? | Near distance, approx. | Far distance, approx. |
| ---: | --- | --- | ---: | ---: |
| 0 | `-7500 / -11500` | No | 6691 | 10710 |
| 1 | `-13500 / -11500` | Yes | **4575** (not emitted natively) | 8469 |
| 2 | `-9980 / -8969` | Yes | **3208** (visible) | 7215 |
| 3 | `-9500 / -4860` | Yes | **3795** (visible) | 4980 |
| 4 | `-12500 / -5500` | No | 1740 | 2997 |
| 5 | `-13500 / -2500` | No | 4756 | 1168 |
| 6 | `-8500 / -1500` | No | 6905 | 5862 |

World/camera allocation identities, all three completion markers and shared fade range **1664** stayed unchanged. Near records had sprite **19**, type **58 (`0x3A`)**, behavior **9**, flags **`0x0220`**, with positions and lifetimes consistent with holes 2/3. At far, all completed holes exceeded 4096; their native emissions stopped and their short-lived records expired. This explains disappearance/reappearance without changing gameplay activation or the fade table.

The replay test uses exact fixed-point cameras `(-399582,-25332,-227000)` and `(-450604,-29336,-98906)`, and the same seven-hole coordinates/markers; it does not depend on temporary snapshot files.

## Particle path, layouts and independent limits

| Address | Evidence-based name / role |
| --- | --- |
| `0040FAE0` | `AllocateGameplayParticle`: 64 shared records, can replace occupied slots. Ordinary allocation radius approximately **6400**; types `0x37`, `0x43`, `0x50` use approximately **12800**. |
| `00529E58..0052AD57` | `g_abGameplayParticlePool`, 64 records of **`0x3C` bytes**, total `0xF00`. Shared with gameplay projectiles, collision, pickups and other effects; unchanged. |
| `0040FDF0` | `SpawnGameplayParticleWithMotion`: reads 24-byte motion template, consumes native RNG, forwards to allocator. Correct return type is `int * __cdecl (...five int arguments...)`; assembly `0041007F..0041008B` calls allocator, adjusts stack/pops nonvolatile registers and returns **without changing EAX**. Previous `void` decompilation was misleading. |
| `0052ADAC` | `g_pGameplayRandomCursor`, shared random-byte stream pointer. Additional distant smoke must not advance it. |
| `00410F40` | `UpdateGameplayParticlePool`: lifetime/motion, type-dependent collision/sound/child effects, camera-distance kill approximately **12800**, flag `0x300` check. For guarded type `0x3A`, behavior 9 only grows size and fades RGB. |
| `0049E1A9` | `UpdateGameplayFrame` (`0049DFE0`) calls native particle update; wrapper forwards it once, then advances private smoke with native ticks. |
| `00445980` | `RenderGameplayParticleSprites`: reads native live records, resolves texture/UVs, submits sprites, sets flag `0x200` unconditionally. Flag check alone does **not** prove persistent smoke is being killed by visibility. |
| `00441850` | `RenderGameplayScene` calls native particle renderer; wrapper forwards once, then submits private smoke. |
| `004CE2C0` | Logical texture-ID resolver, borrowed native texture handle. |
| `004BB5E0` | Texture dimensions getter (six arguments, nullable optional outputs). No texture ownership transferred. |
| `004B8E60` | `QueueBillboardWorldSprite`, nine `__cdecl` arguments; native exhaustion check and hardware projection retained. |
| `004B8A30` | Separate planar sprite queue; not used by this smoke. |

Native `0x3C` particle layout relevant to the effect:

```text
+00/+04/+08  int32 position XYZ (32x fixed-point)
+0C/+10/+14  int32 velocity XYZ; +18 acceleration
+24         int16 lifetime
+26/+28     int16 width/height
+2C         uint8 sprite index; +2D type; +2F update behavior
+32         uint16 flags; +34 rotation; +36 spin
+38..+3A    uint8 RGB
```

### Guarded smoke definitions

- Particle type `0x3A` definition at **`004EC500`**, 16 bytes: `13 64 18 00 01 00 6E 00 6E 00 20 00 09 60 50 40`.
  - Sprite 19, one frame, lifetime `24*2 = 48` ticks, width/height **110**, flags `0x20`, behavior 9, RGB **96/80/64**.
  - No collision flag, hit behavior, child producer or sound for this definition. Width/height grow by **2 per tick**; fade RGB during final **32** ticks.
- Motion template 3 at **`004EC990`**, 24 bytes: `10 10 00 00 1F 00 00 00 00 FE FF FF 1F 00 00 00 00 00 00 00 00 00 00 00`.
  - X/Z velocity `((random_byte & 31)-15)*8/2`; Y velocity `-512/2 = -256`, acceleration/spin zero.
- Sprite table **`00557500`**, entry 19 pointer **`0055754C`**; descriptor **`004EBFA8`**, guarded first ten bytes `1F 00 1F 1F 00 00 00 00 C0 60`.
  - Logical texture **31**, atlas size **31x31**, origin **192/96**. Use actual native texture dimensions to normalize UVs. Flags `0x20` select **`0x4840`** billboard blend/render flags; RGB packed to ARGB with alpha 255.

## Deliberately unmodified alternative paths

- `0044EB90` chain renderer: eight allocated chains at `00559C20`; record `+0x60C` bit 1 gates rendering; `0044E620` initialization, `0044E710` update. Not identified as this smoke.
- `0044F010` rising renderer: 64 `0x10`-byte records via `0054F08C`, sprite index `00557AA4`; `0044ED90` spawn/update is camera-relative. Not identified as this smoke.
- `0043E6E0` (`BuildDistanceFadeLookup`): shared range `0054BEF0`, fixed short table **`00557C20..00559C1E`**. Legacy consumers `00449990`, `0044AA98`, `0044BD04`, `0044D3C2` do not establish capacity safety. Neither range nor table is raised.
- Atmospheric fog, native scenery materials, projection/depth limits, soldier AI/projectiles and all other type-`0x3A` callers remain unchanged.

## Implementation and safety contract

Files: `source/MoleHoleSmoke.h` (portable bounded pool/math), `source/NativeMoleHoleSmoke.h` (guarded hooks), `source/dllmain.cpp` (option/log integration), `tests/native_mole_hole_smoke.cpp` (unit/native replay).

1. Requires successful `NativeObjectRendering` arena installation: **16384 sprites, 8192 transforms, 32768 entries/triangles**. Original 64 gameplay actors/particles stay unchanged. Smoke pool is separate, fixed **64** slots; exhaustion drops only additional visual smoke, never replaces live gameplay slots.
2. Validates executable fingerprint and **12 patch/guard entries** before any write. Changes one cutoff operand and three relative calls only. Complete validation precedes code protection/write/cache flush. Byte mismatches skip safely.
3. The extended cutoff is finite, capped to configured `ObjectDrawDistance` and native far clip, never shrinks native 4096 emission. Maximum squared operand **36000000** stays below signed-int limits. Extra checks use wide arithmetic to avoid delta-square overflow.
4. At the exact guarded emitter call, native-near particles pass through `0040FDF0` unchanged. Only completed, matching X/Z holes at the exact native height create extra distant particles. Private X/Z random stream does not consume native RNG.
5. Native script cadence is retained. Private particles use native ticks, 48-tick lifetime, motion/size/fade/blend semantics. Pause/non-positive ticks do not advance them; large ticks expire them without arithmetic overflow.
6. Native particle update/render still run once. Extra rendering uses borrowed native texture/UVs and the expanded billboard queue; hardware clipping/fog/depth/exhaustion remain native. No texture allocation, COM ownership or display-mode changes.
7. Level != 2 is checked before reading level-specific data. Wrong list count/null world/source, changed world/list identity, reset completion marker or changed coordinates clear stale extra particles. Re-entry starts fresh; near/far boundary crossing allows old particles to finish their native-style short lifetime.

## Validation results and next checks

2026-10-05: **all 13 regression suites passed**, including native resolution pixel tests through the retained **8192/8193** boundary, input/Alt-Tab coverage, object/scenery executable replays and combined manifest compatibility. Win32 Release build passed. Mole-hole replay also passed with **`/O2` and assertions retained**.

The mole test covers finite/null/overflow bounds, exact -2992 height, pool fill/drop, isolated RNG, native-near passthrough, motion/size/color/lifetime, pause/large ticks, source reset/transition safety, missing texture/invalid dimensions, billboard arguments, every guard mismatch and patch isolation. Actual native emitter-loop replay proves **2 native near + 1 extra distant** at near camera, **0 native + 3 extra** at far camera, preserved seven-hole cursor cadence, unmodified native particle bytes over 512 distant iterations, and live caller x87 preservation.

**Remaining coverage:** transparency and continuity crossing 4096, pause/visor/focus/Alt-Tab presentation, death/respawn and level transitions. The user reported that it works and requested a master merge, but the agent did not install this build; its last independent installed-file check still identified the older scenery-enabled ASI. Record the report without inventing a measured new-build installation/log/pixel result. For further tests, preserve/back up installed ASI/INI, verify the tested binary/log, compare the same three completed holes near/far with the toggle on/off, and recheck confirmed input/scenery/Alt-Tab behavior.

Ghidra function/global names and explanatory comments from this table were saved locally; the corrected motion-spawner return prototype was applied after assembly verification. The table is the durable reconstruction reference for a fresh Ghidra project.
