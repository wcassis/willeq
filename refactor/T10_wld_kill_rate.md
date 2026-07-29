# T10 — WLD parser: measure the mutation kill rate, then fix it

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

Prompted by mutation-testing intel the benchmark session routed to us
via `refactor/deferred.md` (2026-07-29): `wld_loader.cpp` has the most
covered lines in the repo (**952**) but killed only **1 of 8** injected
faults.

Will confirmed the benchmark clone reaches the EQ client files over the
same NFS share, so their WLD tests ran — the result is **not** an
artifact of skipped tests. The signal is real.

## Why the kill rate is that low — already diagnosed, don't re-derive

`WldIntegrationTest.WldLoader_ParseZone` (`tests/test_wld.cpp:317`)
drives the whole parser over a real zone and then asserts exactly two
things:

```cpp
ASSERT_TRUE(success) << "WldLoader failed to parse qeynos2.wld";
const auto& geometries = loader.getGeometries();
EXPECT_GT(geometries.size(), 0u) << "No geometries loaded";
```

"It didn't crash and produced at least one geometry." Nearly any
mutation inside the parser — a wrong vertex component, a flipped
winding, an off-by-one in an index table, a mis-resolved texture name,
a dropped fragment type — leaves both assertions true.

The file's other 296 assertions are real, but they are overwhelmingly
on **synthetic fragment structs** (skeleton, light, bsp, mesh, header,
texture suites), not on the loader's behavior over real data. Coverage
comes from the 5 integration tests; assertions come from the other 78.
That gap is the whole finding.

## Territory

**`tests/` only for anything committed.**

Part A requires temporarily injecting faults into
`src/client/graphics/eq/wld_loader.cpp` — a file that is off-limits by
path (P28 reclassified it as base data and moved it to
`CLIENT_CORE_SOURCES`, but it still lives under `src/client/graphics/`).

Those edits are a **measurement instrument, not a change**:

- Mutate, build, run, record, then `git checkout -- <file>` to revert.
- **Never commit a mutation.** Before your final commit,
  `git status --short` must show no modification to any file under
  `src/`. State that you checked.
- If a mutation somehow can't be cleanly reverted → STOP.

Nothing else under `src/` may be touched at all.

## Part A — measure the kill rate ourselves

Pick **8 representative mutations** spread across the parser's real
work, not clustered in one function. Suggested areas (adapt if the code
argues otherwise — read it first):

1. A vertex component decode (swap/scale/sign).
2. Triangle index assembly (off-by-one, or swap two indices).
3. Texture-name resolution (`resolveTextureName`, ~line 612).
4. A fragment-type dispatch case (drop or mis-route one type).
5. A bounding-box / extent computation.
6. Region/BSP association (`getGeometryForRegion`, ~line 1052).
7. A material or texture-index assignment.
8. `decodeZoneLineString` (~line 2510).

For each: apply it, build, run **`test_wld` only** (fast), record
KILLED (some test fails) or SURVIVED (all pass), then revert.

Record the result as a table. **This is the baseline and it is the
unit's primary deliverable** — even if Part B runs out of room, the
measured table is worth having.

Report the number honestly whatever it is. If our kill rate comes out
much better than 1/8, say so plainly — it would mean their mutation set
differed from ours, and that is useful intel to route back rather than
something to massage.

## Part B — strengthen the integration assertions

Target the 5 `WldIntegrationTest` cases, especially
`WldLoader_ParseZone`. Make them assert what the parser actually
*produced*, not merely that it produced something.

Use **`qeynos2.s3d`** as the primary fixture (already used, and present
on the NFS share). Assertions worth having:

- Exact geometry/mesh count, vertex count, triangle count.
- Bounding-box extents within a tight tolerance (`EXPECT_NEAR`).
- A specific known texture name resolving correctly, and the total
  distinct texture count.
- Material count and a spot-checked material→texture mapping.
- Vertex data sanity: no NaNs, coordinates inside the bounding box.
- Region/PVS counts where `hasPvsData()` is true.

**These are characterization assertions.** They pin *current* behavior,
which means if the parser is wrong today you are pinning the wrong
value. That is an acceptable trade — it catches regressions, which is
the point — but:

- Label them clearly as characterization, with the zone and the date.
- Where a value can be sanity-checked independently (a bounding box
  should be plausible for a city zone; a texture name should look like
  a real EQ texture), do that and say so.
- If a value looks **implausible**, that is a finding — report it, do
  not quietly bless it.

Keep them deterministic: same archive, same parse, no ordering
assumptions unless the loader guarantees order (check before relying
on it).

## Part C — re-measure

Re-run the same 8 mutations against the strengthened suite. Report the
before/after table. The unit succeeds if the kill rate goes up
substantially and honestly; it does **not** succeed by adding
assertions that happen not to catch anything.

If a mutation still survives after Part B, say which and why — that is
a precise statement about what the suite still can't see, and it is
more useful than a round number.

## STOP conditions

- A mutation can't be cleanly reverted → STOP.
- You want to change `src/` for real (fixing a parser bug you found) →
  STOP and report. Findings get pinned and escalated, not fixed.
- A "current" value looks implausible → report before pinning it.

## Acceptance criteria

1. Part A table: 8 mutations, KILLED/SURVIVED, with a one-line
   description of each.
2. Part B assertions added to `tests/test_wld.cpp`.
3. Part C table: same 8 mutations against the new suite, with the
   before/after kill rate stated plainly.
4. `git status --short` shows **no** `src/` modifications at commit
   time — confirm explicitly.
5. Build clean; `test_wld` runtime reported (it loads real archives, so
   it may exceed the usual 5s target — report the real number rather
   than trimming coverage to hit a budget).
6. Full suite ×3: documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` / `RDPServerLifecycleTest.*` = the known T03b
   use-after-free — name them, don't call the run clean).
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs. `test_wld.cpp` is already 3804 lines; if your additions push
   it past the limit, split into a new TU and say so.

Visual gate and ARM build: not applicable (tests-only commit) — say so.

## Environment notes

- GLES2 config is the only tree; relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `.../lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- EQ assets: `data/eq_client` symlink → `/mnt/projects/EverQuestP1999`.
  Create it in your slot if absent.
- Baselines moved with P32: GLES2 tree ~1361/1362 before our additions.

## Review

Per the protocol, plus:
- Both kill-rate tables, before and after.
- Any value you judged implausible.
- Which mutations still survive and what that says about the suite.
- Explicit confirmation that no `src/` change was committed.
