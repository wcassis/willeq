# T279 — complete ETC1 mip chains + fix the alpha companion; re-bless goldens (Will's decision, 2026-08-26)

Slot: first free. x86. NO eqemu (offline assets + gate). Class A, LARGE
(regenerates every EQE1 pack). Decision: docs/decisions.md 2026-08-26 EQE1
mip entry. Territory: tools/eqe1_builder.cpp, include/client/graphics/
eqe1_format.h (if the count/size helpers need it), tools/verify_eqe1.cpp,
tools/convert_all_to_eqe1.sh, src/client/graphics/constrained_texture_cache.cpp
(the ALPHA companion filter at ~1962-1969 ONLY — T275 already fixed the RGB
paths), data/eqe1_texture_index.json, the golden bless, tests. Coordinate:
whatever units are live in the other two slots — do not touch their files.

## Facts established before this unit (verify, do not assume)
- T275 `ad51cc3f`: an incomplete chain + a mipmap MIN_FILTER samples
  opaque black; RGB paths now use plain `GL_LINEAR`.
- `computeMipLevels(256,256)` in `eqe1_format.h:96` ALREADY returns 9, and
  `generateMipChain`'s loop (`eqe1_builder.cpp:260`) runs `level < outMipLevels`.
  **So the CURRENT builder may already emit complete chains and the shipped
  packs may simply predate it (`eqe1_format.h` last touched 2026-07-24).**
  STEP 1 IS TO FIND OUT — build one pack from one S3D with the current
  builder and run `verify_eqe1` on it. If it reports 9, the builder needs
  no change and this unit is a regeneration + alpha-filter unit.
- The alpha companion sets `GL_NEAREST_MIPMAP_NEAREST`
  (`constrained_texture_cache.cpp:1969`) and the driver never overwrites
  unit 1's filters (T275).

## Scope
1. **Determine the truth** (above). If `encodeETC1_RGB/_Alpha` cannot encode
   a level below 4x4, fix it: ETC1 stores ceil(w/4)*ceil(h/4) blocks, so a
   2x2 or 1x1 level is one 8-byte block with the source padded. Confirm the
   READER (`eqe1_format.h` size helpers, the loader's per-level offset walk)
   agrees byte-for-byte with what the builder writes — a size mismatch here
   corrupts every subsequent level.
2. **Alpha companion:** with complete chains its mipmap filter becomes
   legal. Decide between keeping `GL_NEAREST_MIPMAP_NEAREST` (now valid) and
   plain `GL_NEAREST` — the comment at :1962 explains why NEAREST was chosen
   for alpha; keep that intent, only the completeness changes. State the
   choice and why. Verify unit 1 is actually bound with a complete texture.
3. **Regenerate all packs** with `tools/convert_all_to_eqe1.sh` into the
   real asset tree (`/mnt/projects/EverQuest_ETC1` — this is SHARED with the
   Pi via `PI_EQE1_PATH`; build to a STAGING dir first, verify, then swap,
   and keep the old tree until the gate passes). Run `verify_eqe1` over
   every pack: expect 0 `mipLevels` complaints. Record before/after entry
   counts and total bytes (complete chains add ~1 block per extra level —
   quantify it; the orangepi budget is the constraint, decision #2).
4. Rebuild `data/eqe1_texture_index.json` if any offset moved (T207's
   `eqe1_texture_index_builder`), with the T262-style diff proof.
5. **Re-bless the goldens** (Will approved) — only after 1-4 are green.
   Capture before/after crops of anything that changes, especially entity
   alpha edges (foliage, hair, capes). If NOTHING changes visually, say so
   and do not re-bless.
6. Pixel proof: extend `tests/test_ui_atlas_pixels.cpp` (T275) or add a
   sibling asserting an alpha-companion texture samples correctly at a
   reduced mip level. Red/green measured.

## STOP
If regenerating changes the on-disk format version or the loader's offset
math in a way that would break the Pi's existing packs, STOP and report —
that is a compatibility decision, not an executor call.

## Verification
Full ctest; visual gate (re-blessed, with the before/after evidence);
build-gles2-x86; build-arm-noble; `verify_eqe1` clean over every pack;
Review with the size delta; evidence review/T279/. Commit:
`T279: EQE1 packs ship complete mip chains; alpha companion fixed; goldens re-blessed`.

---

## Review (executor, 2026-08-26, slot3, branch `task/T279-mip-chains` off `85237091`)

Evidence: `/mnt/projects/fable/review/T279/` (README.md first).

### Step 1 — did the builder need a change? NO.

Answered before touching any encoder code, exactly as the plan required.

- `computeMipLevels()`/`etc1MipChainSize()`/`downsample2x()` were all fixed to
  run to 1x1 by `9189ad69` (2026-03-22, "Fix ETC1 mipmap completeness"). The
  current builder emits complete chains.
- Rebuilt `akanon.eqe1` (112 textures, 3 raw) and `gfaydark_chr.eqe1` (756
  textures, 376 alpha) from S3D with today's builder: `cmp` against the live
  tree returns **exit 0, byte-identical** for both. The S3D path is already
  current, and deterministic.
- `verify_eqe1 --all` over all 1506 packs: **1504 pass, 2 fail, 130 errors** —
  and the two failures are `willeq.eqe1` and `textures.eqe1`, the client's own
  BMP-directory packs, not S3D-derived ones.
- Corroborated independently by mtime: exactly two `.eqe1` files in the tree
  predate `9189ad69`, and they are exactly those two.

So this was a **regeneration + alpha-filter unit**, and the regeneration was
two files, not 1006. `encodeETC1_RGB/_Alpha` handle sub-4x4 levels correctly
(Etc2Comp pads to `extendedWidth/Height`), and the reader agrees with the
writer byte-for-byte — proven by `verify_eqe1`'s `dataSize` check passing on
every entry of every pack afterwards.

### On-disk / offset compatibility — no STOP condition triggered

Format version unchanged (v1, magic unchanged, 67/64 entries before and after).
The loader's per-level offset walk (`constrained_texture_cache.cpp` upload
loops) already clamps `w/h` at 1, matching the writer. The Pi reads the same
NFS tree, so it gets the same complete packs; no split-brain is possible.

### The prefix-superset diff proof (T262 style)

`review/T279/pack_diff_proof.txt`, re-runnable via `pack_diff_proof.py`:

```
=== willeq ===   entries: 67  violations: [('software_cursor','not-prefix')]
 mip (old->new): [(0,0)x1, (3,4)x1, (4,6)x1, (5,7)x3, (7,9)x60, (8,10)x1]
 appended data bytes: 1048
=== textures === entries: 64  violations: [('software_cursor','not-prefix')]
 mip (old->new): [(3,4)x1, (4,6)x1, (5,7)x3, (7,9)x58, (8,10)x1]
 appended data bytes: 1016
```

Same name set, same header, same per-entry `width/height/hasAlpha/entryType/
payloadFormat`; every entry's new data is a byte-exact **prefix** of the old
with the missing tail levels appended. Nothing was re-encoded. The one
exception is `software_cursor` (8x10) and it is correct: the old builder
clamped at 4x4 so its level 2 was a 4x4 block, where a complete chain wants
2x2 then 1x1. `ui_atlas.json` (the raw entry) is byte-identical, and
`ui_atlas_builder` reproduced it bit-for-bit, which is what let the pack be
rebuilt from source at all.

### Size delta

| | before | after | delta |
|---|---|---|---|
| `willeq.eqe1` | 2,809,823 | 2,810,863 | **+1,040 B** |
| `textures.eqe1` | 2,721,192 | 2,722,200 | **+1,008 B** |
| whole 4.8 GB tree | | | **+2,048 B** |

Two extra ETC1 blocks (8 B each) per texture, plus one for the 8x10 cursor.
Irrelevant to the orangepi budget (decision #2): the 1504 packs that dominate
the tree were already carrying their complete chains and did not change at all.

### Step 2 — the alpha companion filter, and why the guard, not the constant

`constrained_texture_cache.cpp:~1985`. **Kept `GL_NEAREST_MIPMAP_NEAREST`**;
the plan's intent (never linear-interpolate a binary alpha mask; still select a
mip level at distance) is unchanged and correct. What was wrong was the
*precondition*: the guard was `entry.mipLevels > 1`, which is not the
mipmap-completeness predicate. A pre-`9189ad69` pack has `mipLevels > 1` and is
still incomplete, and sampling an incomplete texture returns opaque black
(T275) — on an alpha companion that is alpha==0 everywhere, i.e. every
alpha-tested fragment discarded. Unit 0 is saved by the driver rewriting its
filters per material bind; unit 1 is not.

Guard is now `entry.mipLevels == computeMipLevels(entry.width, entry.height)`,
falling back to plain `GL_NEAREST` otherwise. Deliberately checked against the
entry's own dimensions rather than trusting the asset tree to be fresh: the
fix then holds for any pack, including an old one on a Pi that missed a sync.

Verified unit 1 really is bound with a complete texture: the new test uploads
through the real `ConstrainedTextureCache` ETC1 path, pulls the companion back
out with `getAlphaTexture()`, and samples it — `nullptr` at any step fails the
test.

### Step 3 — regeneration and the swap

Built into `slot3/build/t279/stage/`, verified there, then `mv`'d into
`/mnt/projects/EverQuest_ETC1` (shared with the Pi via `PI_EQE1_PATH`) — the
old bytes survive in the pre-existing hardlinked `EverQuest_ETC1_staging` tree
and in `build/t279/backup/` (md5s recorded in the review README).

`verify_eqe1 --all` after: **1506/1506 passed, 0 errors, 0 warnings.**

### Step 4 — `data/eqe1_texture_index.json`: correctly NOT rebuilt

No offsets moved (the index stores name→archive membership, not byte offsets),
no archive or entry name changed. Proven rather than asserted: ran
`eqe1_texture_index_builder` over a directory holding only the two OLD packs
and one holding only the two NEW packs — output **byte-identical**
(`idx_old.json` / `idx_new.json`). The committed file is untouched.

**Deviation flagged:** running the index builder over the whole live tree
produces a *different* file from the committed one — 1506 archives / 50,972
names vs the committed 1008 / 50,447. The committed index predates the ~498
equipment-sheet packs P16/P48 added to the tree. That is a real, pre-existing
gap, but it is unrelated to mip chains and expanding the index's scope is not
this unit's call; the working copy was reverted and the question is handed
back. (No FATALs result: T266's `EQE1TextureIndex::resolve` R2 path late-opens
an unindexed name's owner.)

### Step 5 — goldens NOT re-blessed

The re-bless was pre-approved and turned out **not to be warranted**, which the
plan explicitly provides for ("If NOTHING changes visually, say so and do not
re-bless").

- The golden-image runs never pass `--eqe1-path` (see `visual_gate.py`'s
  docstring) — they render from S3D, so no EQE1 asset can move them.
- The only alpha companion in a stale pack was `ui_atlas_alpha`, and nothing
  binds it: every `getAlphaTexture()` consumer is a 3D zone/entity path whose
  packs were already complete. So old guard and new guard were behaviourally
  identical on the shipped tree.
- `ui_inventory_capture` before/after (same tool, same flags, only the two
  packs differ): **all 14 captures byte-identical**.
- Visual gate: **PASS** before the swap and **PASS** after, `tests/goldens/`
  byte-untouched.

Nothing to show Will as a before/after difference; the identical pairs are the
no-regression evidence and are kept as such.

### Step 6 — pixel proof, red/green measured

New `tests/test_eqe1_alpha_companion_mip.cpp` (3 tests). Builds two synthetic
EQE1 packs of solid-white ETC1 blocks — one complete to 1x1, one truncated at
4x4 the way the pre-`9189ad69` builder wrote them — uploads both through the
real cache, and draws the **alpha companion** minified 256→32 into a real
headless GLES2 framebuffer, reading the pixel back.

| guard | truncated-chain readback | result |
|---|---|---|
| old `mipLevels > 1` | **(0, 0, 0)** | FAIL — opaque black, the T275 signature |
| new completeness predicate | (255, 255, 255) | PASS |

The complete-chain case passes under both, which is the point: the fix costs
nothing where the chain is already complete.

### Verification

| step | result |
|---|---|
| `cmake --build slot3/build -j24` | PASS (exit 0) |
| `ctest --test-dir slot3/build` | **3087/3088** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | PASS (exit 0) |
| visual gate | **PASS**, goldens untouched, **not re-blessed** |
| `scripts/build-arm-noble.sh` | PASS (exit 0) |
| `check_file_sizes.sh fable` | no new FAILs (`eq.cpp` FAIL, `constrained_texture_cache.cpp` WARN both pre-existing) |
| `verify_eqe1 --all` over all 1506 packs | **1506/1506, 0 errors** (was 1504/1506, 130 errors) |

### Notes for the orchestrator

- `data/eqe1_client` (gitignored symlink to `/mnt/projects/EverQuest_ETC1`) was
  missing in slot3, which silently makes the gate skip its memory-coverage run.
  Created it. Those baselines are still `NOT YET BLESSED`, so nothing asserted.
- The RGB EQE1 upload paths still use plain `GL_LINEAR` (T275's resolution).
  Now that every pack is complete that is a conservative choice rather than a
  necessary one, and mipmapped minification could be restored there. Out of
  this unit's territory; noted, not done.
