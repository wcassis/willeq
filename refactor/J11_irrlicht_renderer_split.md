# J11 — Split irrlicht_renderer.cpp (3,296 → 6 TUs): the last file over the hard cap

**Slot: slot3. Branch `task/J11-irrlicht-renderer-split` cut from `fable`.
x86 only. No Pi. No eqemu.**

**This is the last of the four files over the 3,000-line hard ceiling.** J02
took `eq.cpp` 3,923 → 1,022, J03 `entity_renderer.cpp` 3,244 → 540, J04
`command_processor.cpp` 3,024 → 284. This one clears the list.

**Its blockers are gone:** T312 (the `bridgeQueue_` thread-safety unit) and T313
(the static-UI scroll unit) have both merged, as has T319, which added ~22 lines
to this file. **Re-measure at your tip — it was 3,237 in the survey, 3,274 at
T319's base and 3,296 after.**

## Read first

`refactor/J_file_size_reclamation.md` — **especially the two correction sections
"⚠ TEMPLATE CORRECTIONS FROM J02" and "⚠ FURTHER CORRECTIONS FROM J04"** — and
the Reviews of `refactor/J01_inventory_manager_split.md`,
`refactor/J02_eq_cpp_split.md`, `refactor/J03_entity_renderer_split.md` and
`refactor/J04_command_processor_split.md`. Four units have already paid for the
lessons below; do not re-pay them.

## MOVE-ONLY. No interface changes, no behaviour changes, no method decomposition.

## The proposed split (from the survey — RE-DERIVE every range at your tip)

| cluster | target TU | ≈ lines |
|---|---|---|
| all of `RendererEventReceiver` (its `OnEvent` alone is ~205 lines) | `renderer_event_receiver.cpp` | ~340 |
| ctor/dtor, `initLoadingScreen` (~430), `loadGlobalAssets`, loading-screen show/hide | `renderer_init.cpp` | ~625 |
| the anon-ns camera-mode token parsers + `processSlashCommand` (~438) | `renderer_slash_commands.cpp` | ~560 |
| `setupFog`, `drawLoadingScreen`, `applyEnvironmentalDisplaySettings`, HUD setup/update, paperdoll (+ its anon-ns `appearanceEquals`) | `renderer_hud_env.cpp` | ~625 |
| camera projection, first-person pitch/FOV/dump/reposition, spawn id/race, vision type, object light colours, `setPlayerPosition`, swimming, camera mode, `getCameraTransform` | `renderer_camera.cpp` | ~540 |
| display settings, shutdown, cursor, lighting, time of day, PVS, sky/door/world-object façades, `run`, `saveScreenshot`, frame-timing setters | **stays** `irrlicht_renderer.cpp` | ~980 |

⚠ **`renderer_event_receiver.cpp` is board item 32** — T312 recommended exactly
this TU and deliberately left it to Batch J. Note it in your Review as closed.

## Hazards

- **Two anonymous namespaces** (the camera-mode token parsers, and
  `appearanceEquals` in the paperdoll block). Each must travel with its sole
  consumer — **verify each really has only one, at your tip**. If one is shared
  across your cluster boundaries, it needs a private internal header, and then
  **the verifier will exit 1 by construction** (see below).
- **The preamble is ~97 lines, the largest in the tree.** Each new TU carries
  the original's full include block.
- This file is the subject of T312's mutex invariant: **`bridgeQueue_` has no
  raw access anywhere, only the three locked accessors**. A move must not
  disturb that; say in your Review that you checked.

## The five corrections that cost earlier units real time

1. **`python3 scripts/refactor/verify_move_only.py "$(git merge-base fable HEAD)..HEAD" --repo /mnt/projects/fable/wt/slot3`** — omitting `--repo` silently verifies `main/` and produces a failure **whose signature is identical to the wrong-base-rev failure**. J02 lost time to exactly this.
2. **Check every cut boundary against block-comment and `#if` nesting before cutting.** J02's UCS range cut inside a `/* */` and would not have compiled. **J03's better rule, which also catches that case: cut at a blank line whose predecessor closes a method, and verify comment depth 0 at the cut.** The "take ranges from the leading banner" rule has no purchase in files without banners.
3. **The line-multiset check is a required second proof** — the verifier compares only *distinct* normalised lines, so a duplicated or dropped range is invisible to it. J03's form (re-concatenate the extracted ranges and `cmp` byte-identical against the base file) is the strongest and is what to copy.
4. **Budget TWO `cmake --build` invocations.** In a three-wide wave the first reliably blows the 600 s tool ceiling; **exit 143 means re-invoke, not breakage.** Same for `build-arm-noble.sh`. J03 measured the build, not `ctest`, as the slowest step.
5. **If a shared helper forces a private internal header, the verifier exits 1 by construction** (`BOILERPLATE` allows `#include`/`#pragma`/`namespace`/`}`/comments, not `inline` or `using`). Paste the full output, enumerate every flagged line, and ask for orchestrator sign-off. **Do not contort the code to force a green exit** — J04 correctly refused.

Also: `grep -n` for the CMake add-site (root variable is `CLIENT_CORE_SOURCES`);
`chmod 644` anything you create.

## Acceptance

Build clean by exit code · `ctest -j6` (**NOT -j24**) zero new failures vs
`refactor/test_baseline.txt` · verifier per correction 1 · the multiset/byte-identical
proof · **visual gate PASS with goldens untouched** — this file owns the frame
loop and the camera, so a moved golden means the split changed behaviour: STOP
and report · `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` ·
`check_file_sizes.sh --files` 0 warn 0 fail on everything changed and created.

⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a known
**load-dependent flake** under concurrent slots — re-run it alone before
reporting it.

## Territory

`src/client/graphics/irrlicht_renderer.cpp`, `CMakeLists.txt`, this file.
**The header `include/client/graphics/irrlicht_renderer.h` is NOT in scope** —
it is J17's, and J17 is gated on this unit.
⚠ Concurrent: slot1 T320 (live group/zone, `eq_chat.cpp`), slot2 T321
(`src/common/net/`, the CRC flag line in `eq.cpp`).

---

## Review (slot3, 2026-09-02, branch `task/J11-irrlicht-renderer-split`, commit `26dc25f6`)

**Done as specified. Move-only in `src/`. Zero method decomposition, zero
interface change.** Base tip verified `61b2232defd4c850e4952a97c46d0dc9f0927332`
at checkout — the hash named at dispatch. **One deviation from the plan's
territory, declared in §6: four source-text-scanning tests had to be repointed
at the TU that now holds the text they assert on.** The verifier exits 1, for a
reason no earlier J unit hit — see §5.

**The file was 3,296 lines at my tip**, exactly as the plan predicted after T319.

### 1. The partition — 14 ranges, contiguous and exhaustive over body 110-3293

Preamble = **lines 1-109** (through the blank after `namespace Graphics {`);
footer = **3294-3296**. Both are replicated **byte-identical into all six TUs**
(machine-checked, §4).

| range | lines | → |
|---|---|---|
| 110-145 | 36 | `renderer_init.cpp` — `loadDisplaySettingsFromFile()` (file-static) |
| 146-149 | 4 | **stays** — `getDisplaySettings()` |
| 150-227 | 78 | `renderer_init.cpp` — `logDriverDetails()` (file-static) |
| 228-500 | 273 | `renderer_event_receiver.cpp` — all of `RendererEventReceiver` |
| 501-1031 | 531 | `renderer_init.cpp` — ctor/dtor, `initLoadingScreen`, `loadGlobalAssets`, show/hide |
| 1032-1511 | 480 | `renderer_slash_commands.cpp` — camera-mode anon-ns + `processSlashCommand` |
| 1512-1897 | 386 | **stays** — shutdown, isRunning, requestQuit, cursor, `setupCamera`, lighting, time of day, PVS |
| 1898-2327 | 430 | `renderer_hud_env.cpp` — fog, `drawLoadingScreen`, `applyEnvironmentalDisplaySettings`, HUD setup/update |
| 2328-2554 | 227 | **stays** — sky façades, `createEntityRenderer`, doors, world objects |
| 2555-2925 | 371 | `renderer_camera.cpp` — projection, pitch clamp, FOV, `/fpcam` dump, reposition, spawn id/race, vision, object lights, `setPlayerPosition`, swimming |
| 2926-2943 | 18 | **stays** — `handleActionResult` (T319) |
| 2944-3031 | 88 | `renderer_camera.cpp` — `setCameraMode`, `cycleCameraMode`, `getCameraModeString`, `getCameraTransform` |
| 3032-3192 | 161 | **stays** — `run`, `saveScreenshot`, frame-timing setters, clip distance |
| 3193-3293 | 101 | `renderer_hud_env.cpp` — paperdoll banner + `appearanceEquals` anon-ns + 3 methods |

**Three judgement calls the plan left open, all declared rather than absorbed:**

- **The two file-statics (`loadDisplaySettingsFromFile`, `logDriverDetails`)
  travel to `renderer_init.cpp`,** because grep shows each has exactly one
  caller and both callers are inside `initLoadingScreen`. That splits the
  110-227 region around `getDisplaySettings()` (4 lines), which the plan puts
  in the stays cluster — hence three ranges where a naive read gives one.
- **`setupCamera()` stays.** It appears in neither cluster row; it is
  device/scene setup sitting between the cursor and lighting setup, not part of
  the first-person/mode camera family, so it stayed with its neighbours.
- **`handleActionResult()` (T319) stays.** It landed inside the camera cluster's
  line span but is a chat/action façade, so the camera cluster is two ranges
  rather than one. It is 18 lines; the alternative was mis-filing it.

⚠ **Cut-boundary check, J03's rule, machine-run, not eyeballed.** A
character-by-character scanner tracking `/* */`, `//` and string literals
confirmed **all 14 cut points plus the footer boundary are at block-comment
depth 0 AND `#if` depth 0**. J02's failure mode does not recur. Additionally
`grep -n '^#if\|^#else\|^#endif'` shows every conditional in the body
(`:593/:601/:789`, `:1532`, `:3062-:3124`) is wholly inside one method, so no
range straddles one. Every cut is at a blank line whose predecessor closes a
method or a namespace.

### 2. Hazards, re-checked at my tip

1. **Both anonymous namespaces have exactly one consumer each. NO internal
   header was needed.** Verified by grep at my tip, not assumed:
   - `NormalizeCameraModeToken` — def `:1038`, sole call `:1049` (inside
     `ParseCameraModeName`); `ParseCameraModeName` — def `:1048`, sole call
     `:1125` (inside `processSlashCommand`). Whole chain is one range.
   - `appearanceEquals` — def `:3236`, sole call `:3275` (inside
     `updatePaperdollForFrame`). Same range.
   - The same check on the two file-statics found one caller each (§1).
2. **`bridgeQueue_` invariant SURVIVES — checked, not assumed.**
   `grep -rn 'bridgeQueue_' src/ include/ tests/` returns **zero raw accesses
   anywhere in any `.cpp`**: the only member accesses in the tree are the three
   locked accessors in `irrlicht_renderer.h:422/427/431` (swap / size /
   push_back) plus the declaration at `:528`; every other hit is a comment or a
   test comment. **`irrlicht_renderer.cpp` did not touch it before the split and
   none of the six TUs touches it after.** T312's rule is intact and the header
   is untouched.
3. **The 109-line preamble** (the plan said ~97; it is 109 counting the two
   namespace opens and the `WindowManager` stub) is replicated 5 extra times —
   545 lines, which is the entire +567-line growth of the split.

⚠ **Board item 32 is CLOSED.** `renderer_event_receiver.cpp` exists, 385 lines,
holding `RendererEventReceiver`'s ctor, the 222-line `OnEvent`, and its six
accessors — exactly the TU T312 recommended and deferred to Batch J.

### 3. Final line counts

| file | lines | plan estimate |
|---|---|---|
| `src/client/graphics/irrlicht_renderer.cpp` | **908** (was 3,296) | ~980 |
| `src/client/graphics/renderer_init.cpp` | **757** | ~625 |
| `src/client/graphics/renderer_hud_env.cpp` | **643** | ~625 |
| `src/client/graphics/renderer_slash_commands.cpp` | **592** | ~560 |
| `src/client/graphics/renderer_camera.cpp` | **571** | ~540 |
| `src/client/graphics/renderer_event_receiver.cpp` | **385** | ~340 |

3,296 → 3,856 across six TUs; **+560 = 5 × the 112-line preamble+footer**, minus
nothing. Largest TU 908, well under the 1,500 soft limit.

### 4. Both proofs

`BASE=$(git merge-base fable HEAD)` = **`61b2232defd4c850e4952a97c46d0dc9f0927332`**.
Note `fable` **had moved** by the time I ran the bar (slot1's T320 merged —
`check_file_sizes.sh fable` picked up `eq_chat.cpp`, `eq.h`, `eq_group.cpp`,
`eq_zone.cpp`, `test_zone_chat.cpp`, none of them mine); `merge-base` gave the
right fork point regardless, which is exactly why the correction exists.

**Proof A (byte-identical re-concatenation) and Proof B (multiset), both from
the NEW files, not from the ranges I cut with:** each of the six TUs is stripped
of its 109-line preamble and 3-line footer, and the 14 ranges are reassembled in
original order by pulling successive chunk-lengths out of the correct TU's body;
every TU's body must be fully consumed (asserted).

```
PROOF A byte-identical: OK (3184 vs 3184 lines)
PROOF B multiset: MULTISET-OK
preamble+footer replicated verbatim in all 6: True
```

Script: `scratchpad/cut.py` (the cut + depth scanner) and `scratchpad/proof.py`,
both reproducible from `$BASE`. The third line is the check J04's correction 7
asks for in the header case: it proves the 5 × 112 replicated lines are
character-for-character the original's, so the +560 is fully accounted for.

### 5. ⚠ The verifier exits 1 — a NEW instance of the "by construction" case

`python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot3`
(`--repo` passed from the outset, per J02's correction 1) — **exit 1**, full
output:

```
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt
  tests/CMakeLists.txt

Files verified: 10
Boilerplate: +463 / -1 lines (allowed)

SUBSTANTIVE ADDED lines (70):
  +1x  const std::string kHelperTU = "renderer_camera.cpp";  // J11: was irrlicht_renderer.cpp
  +2x  const std::string src = readFile(kRoot / "src/client/graphics/renderer_event_receiver.cpp");  // J11
  +5x  extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
  +5x  extern size_t gles2GetHWBufferCount(void* driver);
  +5x  extern size_t gles2GetHWBufferMemoryUsage(void* driver);
  +5x  extern void gles2DeleteAllStaticHWBuffers(void* driver);
  +5x  extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
  +5x  extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
  +5x  extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
  +1x  if (fnEnd == std::string::npos) fnEnd = src.size();
  +5x  int vertexType);
  +5x  namespace eqt { namespace ui { class WindowManager { public: virtual ~WindowManager() = default; }; } }
  +1x  size_t fnEnd = src.find("\nvoid IrrlichtRenderer::", fnStart + 1);
  +5x  unsigned int gpuBytes = 0);
  +5x  unsigned int vbo, unsigned int ebo,
  +5x  unsigned int vertexCount, unsigned int indexCount,
  +5x  unsigned int width, unsigned int height,

SUBSTANTIVE REMOVED lines (5):
  -1x  ASSERT_NE(fnEnd, std::string::npos);
  -1x  const size_t fnEnd = src.find("\nvoid IrrlichtRenderer::", fnStart + 1);
  -1x  const std::string kHelperTU = "irrlicht_renderer.cpp";
  -2x  const std::string src = readFile(kRoot / "src/client/graphics/irrlicht_renderer.cpp");

FAIL: substantive line changes detected. ...
```

**Every one of the 70 is enumerated, and they fall into exactly two groups.
Neither is a code change and neither can be avoided without contorting the
code, which J04's correction 5 forbids.**

**Group A — 65 of 70 (13 distinct lines × the 5 new TUs): the MANDATED verbatim
preamble replication.** `irrlicht_renderer.cpp`'s preamble contains 12 lines of
`extern` GLES2 declarations (original `:72-:84`) and the one-line
`WindowManager` stub definition (original `:105`). §Process requires each new
TU to carry the original's **full** include block verbatim; the verifier's
`BOILERPLATE` allowlist covers `#include`/`#pragma`/`namespace`/`}`/comments but
not `extern` and not a single-line `namespace ... { class ... };`. **These 65
lines are proven byte-identical to the base file's preamble by proof A's third
check** ("preamble+footer replicated verbatim in all 6: True"). Zero new code.

⚠ **This is a NEW trigger for the exit-1-by-construction case, distinct from
J04's.** J04's came from promoting a helper to a private header; this one comes
from a **preamble containing anything the allowlist does not name** — no shared
helper and no internal header is involved. **Any future J unit splitting a file
whose preamble has `extern`, `using`, a forward-declared class body, or a
file-scope constant will hit it, and there is nothing it can do about it.**

**Group B — 5 of 70: the four repointed source-text tests** (§6). Four are
one-token path/filename changes; the fifth is the two-line end-of-file fallback.
**No production line is in either group.**

**Requesting orchestrator sign-off on all 70.**

### 6. ⚠ DEVIATION: four source-text-scanning tests had to be repointed

The plan's territory names `irrlicht_renderer.cpp`, `CMakeLists.txt` and this
file. **It missed that four tests read `src/client/graphics/irrlicht_renderer.cpp`
as TEXT and assert on strings that this split relocates.** Not repointing them
is not an option: three would have failed outright and one would have silently
started asserting nothing. The changes are the minimum that preserves each
test's meaning; **no assertion was weakened or deleted.**

| test | scanned for | now reads | change |
|---|---|---|---|
| `test_merged_help.cpp` (T223 registry-3 drift guard, path via `RENDERER_COMMAND_SOURCE` in `tests/CMakeLists.txt:464`) | every `cmd == "/x"` literal in `processSlashCommand` | `renderer_slash_commands.cpp` | CMake define repointed **plus** a 2-line source change |
| `test_first_person_camera_single_formula.cpp:51` (T300/T301 single-formula guard) | `kHelperTU` = the one TU allowed to write `camera_` in a first-person branch and to read the tunables | `renderer_camera.cpp` | one-token |
| `test_static_ui_scroll.cpp:690` (T313) | `case irr::EMIE_MOUSE_WHEEL:` / `mouseWheelDelta_ +=` in `OnEvent` | `renderer_event_receiver.cpp` | one-token |
| `test_ui_hide_command.cpp:146` (T301) | `case HA::ToggleAllUI: actionQueue_.push_back(...)` in `OnEvent` | `renderer_event_receiver.cpp` | one-token |

**Only `test_merged_help` needed real logic.** It bounded the if/else chain with
`src.find("\nvoid IrrlichtRenderer::", fnStart + 1)` and `ASSERT_NE(..., npos)`
— but `processSlashCommand` is now the **last** definition in its TU, so there
is no following member and the assert would fire. It now falls back to
`src.size()`. The literal-extraction regex, the `> 30` sanity gate and the
two-way set equality are untouched; the parser still found the full literal set
and the test passes.

⚠ **The single-formula guard got STRICTER for free**, which is worth noting as
a genuine improvement rather than a wash: post-split, of the six TUs
`renderer_camera.cpp` is the only one that mentions `CameraMode::FirstPerson`
(11×) *and* writes `camera_` (2×) — the other five score 0/0, 0/0, 0/0, 1/0 and
0/0. All six first-person tunables now resolve to `renderer_camera.cpp` alone
(plus `renderer_player_physics.cpp` for the sanctioned pitch-floor second read).
Before the split, one 3,296-line file was exempted wholesale.

### 7. CMake

`grep -n 'irrlicht_renderer\.cpp' CMakeLists.txt tests/CMakeLists.txt` →
**`CMakeLists.txt:446` in `CLIENT_CORE_SOURCES`** (one site; the plan's
`CLIENT_CORE_SOURCES` naming from J02's correction 4 is right) and
**`tests/CMakeLists.txt:464`**, the `RENDERER_COMMAND_SOURCE` compile
definition. All five new TUs added at `:447-:451`; the test define repointed.
No per-target test source list names the file — every test links `willeq_core`.
`chmod 644` applied by the cut script; `git diff --stat` records no mode change
and the commit shows `create mode 100644` for all five.

### 8. Acceptance

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0, first invocation** (~7 min; did not hit the ceiling — see §10) |
| `ctest -j6` | **3,477 / 3,478 pass**, 196 s. Sole failure `AudioManagerTest.PreloadCommonSounds`, the ALSA host quirk in `refactor/test_baseline.txt`. **Zero new failures.** `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` **Passed, 5.07 s** — no re-run needed |
| `verify_move_only.py "$BASE..HEAD" --repo <slot>` | **exit 1 by construction — 70 lines enumerated in §5, sign-off requested** |
| line-multiset / byte-identical | **PROOF A OK (3184 vs 3184) · PROOF B MULTISET-OK · preambles verbatim** |
| visual gate | **PASS**, and `git status --porcelain` after the run showed **no golden change** — only the 7 modified and 5 new sources. The frame loop and camera behave identically |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/build-arm-noble.sh` | **EXIT=0, first invocation** |
| `check_file_sizes.sh --files` (all 12) | **10 ok, 2 warn, 0 fail** — the warns are `CMakeLists.txt` (1,919, grandfathered by §Process) and `tests/CMakeLists.txt` (2,778, correction 8's boarded item). **Every source file ok** |

**The FAIL list is empty: `irrlicht_renderer.cpp` was the last file over 3,000
and is now 908.**

### 9. Wall clock and concurrency

**~50 min end to end**, ~15 of it reading contracts and surveying. **The cut,
CMake, the test repoints and both proofs together: under 6 min.** The bar was
~28 min of build/test: x86 build ~7 min, `ctest -j6` **196 s (3.3 min)**,
`build-arm-noble.sh` ~6 min, `build-gles2-x86.sh` ~4 min, visual gate ~2 min.
**Neither the x86 build nor the ARM build needed a second invocation** — unlike
J03 and J04. slot1's T320 merged mid-run and slot2's T321 was live, so this was
a real three-wide wave; the contention simply landed lighter than J03's.

### 10. What the template still gets wrong

1. ⚠ **Correction 5 is stated too narrowly.** It says the verifier exits 1 "if a
   shared helper forces a private internal header". **J11 needed no header at
   all and still exits 1**, because the mandated verbatim preamble replication
   multiplies every preamble line the `BOILERPLATE` allowlist does not name —
   here 12 `extern` declarations and a one-line class definition, 65 lines. The
   correction should read: **any unit whose preamble contains something other
   than `#include`/`#pragma`/`namespace`/`}`/comments will exit 1**, and the
   remedy is the same (enumerate, prove byte-identity, ask for sign-off). Two of
   the four hard-cap files hit this by two different routes; it is the rule, not
   the exception. Consider adding `extern` to `BOILERPLATE`, or a
   `--replicated-preamble <file>:<n>` flag that subtracts a known verbatim block.
2. ⚠ **The territory section must include source-text-scanning tests, and the
   plan must tell the executor to find them.** Four tests read this file's TEXT
   by path (one through a CMake compile definition, three through
   `WILLEQ_REPO_ROOT`), and one of them additionally depended on
   `processSlashCommand` **not** being last in its file. None was named in the
   plan. The one-line survey every J unit should run before cutting is:
   `grep -rn '<basename>' tests/ scripts/ --include=*.cpp --include=*.txt`.
   J05, J06, J09 and J13 are all likely to have such guards.
3. **The plan's per-TU estimates were 20-130 lines LOW here** — the opposite of
   J03's systematic overshoot — because the survey used ~97 for the preamble
   when it is 109, ×6. Cluster membership was right in every case.
4. **`--repo` + `merge-base` worked exactly as documented**, and `merge-base`
   earned its keep concretely: `fable` moved under this unit mid-bar.
5. **J03's cutting rule is the right one and should replace the banner rule in
   §Process outright.** This file has exactly two `// ====` banners in 3,296
   lines; the banner rule would have given no guidance at 12 of the 14 cuts,
   while "blank line whose predecessor closes a method, verified at comment and
   `#if` depth 0" decided all 14 mechanically. Worth scripting once for the
   remaining units rather than re-writing the scanner per unit.
