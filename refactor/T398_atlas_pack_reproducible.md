# T398 — the UI atlas pack is rebuilt from the tree (B230), then the coin sprites (B218)

Class B (build integrity — Will's EQE1 rule: assets complete and
reproducible), slot3, no server. Read B230 and B218, T396's §Review Leg
B, T279's Review (the EQE1 builder is deterministic; mip chains), the
`eqe1_builder` and `ui_atlas_builder` tools, `docs/` on the EQE1 pack,
and the `.eqe1` pack's directory (`eqe1` tooling can list entries).

## Leg A — B230(b): reproduce `willeq.eqe1`

1. List the shipped pack's entries (name, size, hash). Partition: entries
   explained by `data/textures/*.bmp` (64), entries explained by the
   procedural `ui_atlas_builder` output (build it, compare bytes), and
   the residue. For the residue, find the source: git history of
   `data/atlas/` (`git log --all -- data/atlas`), T2xx units that added
   pack entries (grep `willeq.eqe1` in refactor/), the Batch H/T279
   builder logs. Name each entry's origin or mark it UNEXPLAINED.
2. Write `scripts/build_willeq_pack.sh` (or a CMake target) that rebuilds
   the pack from checked-in inputs into a temp file and `mv`s it into
   place atomically; run it; the result must be byte-identical to the
   shipped pack (T279 says the builder is deterministic). If an entry is
   UNEXPLAINED and cannot be regenerated, check its source image into
   `data/atlas/` extracted from the shipped pack (say so — provenance
   lost, content preserved) so the rebuild is still byte-identical.
3. Test: a hygiene ctest (`PackReproducible`, label `hygiene`, slow —
   mark it so) that rebuilds into a temp path and compares hashes with
   the shipped pack; planted fault: one input pixel changed → FAIL.
4. Strike B230.

## Leg B — B218 (only after Leg A is byte-identical)

Lift the TGA reader out of `tools/dump_icon_sheet.cpp` into a shared
tool header; give `ui_atlas_builder` a `--uifiles-path` and pull the
72x18 strip at (90,140) of `window_pieces01.tga` (four ~16 px discs with
alpha) into four atlas tiles; draw them in `coinDecalRect()` (T393); the
static-panel seam pins the decal draw (pre-fix: the letter). Rebuild the
pack via Leg A's script — the pack changes deliberately here, so: (a)
the `PackReproducible` test's reference hash updates with the commit
that changes the inputs (say so), (b) the visual gate's goldens must NOT
move (the gate draws no inventory) — if one does, STOP with the contact
sheet. Strike B218.

## Proof

Build (all trees — the pack is read by the gate binary), `ctest -j6` (no
expected failures), pre-fix/post-fix runs, visual gate standalone + ARM,
file sizes. The pack write is atomic (temp + mv) because three worktrees
read it concurrently.

STOP: an entry cannot be reproduced OR extracted (report which — the
pack is then not rebuildable and B218 stays blocked).

## Review

Branch `task/T398-atlas-pack-reproducible`, base `abc62f91`. Two commits:
Leg A `457fa812`, Leg B this commit.

### Leg A — the pack partitions with ZERO residue, and B230(b)'s premise was wrong

The shipped pack has **67 entries**, and every one is explained:

| group | count | origin |
|---|---:|---|
| `ui_atlas`, `ui_atlas_alpha`, `ui_atlas.json` | 3 | `tools/ui_atlas_builder` (procedural; its JSON is byte-identical to the shipped raw entry, 1,087 B, sha `4851d668...`) |
| `creature_atlas` … `tumbleweed` | 64 | the 64 BMPs in `data/textures/` |
| residue | **0** | — |

B230 said the pack "holds far more than `data/textures`'s 64 BMPs
(`creature_atlas`, `detail_atlas`, `particle_atlas`, the rain/snow/storm
sets, `software_cursor`, …)". **Every name in that list is one of the 64.**
The entry was written from the pack's contents without checking them against
`data/textures`, and the whole "`data/atlas/` is absent everywhere" blocker
follows from that one mistake: `data/atlas/` never needed to exist, because
`ui_atlas.bmp`/`.json` are GENERATED, not checked in. Nothing had to be
extracted from the pack for provenance. **No STOP condition fired.**

`scripts/build_willeq_pack.sh`:
1. runs `ui_atlas_builder` into `$WORK/atlas`;
2. symlinks `data/textures` as `$WORK/textures` — `eqe1_builder` sorts every
   collected input path GLOBALLY, so the two staging names ("atlas" <
   "textures") are what reproduces the shipped entry order, with `ui_atlas`
   at index 0;
3. `eqe1_builder --bmp-dir … --bmp-dir … --name willeq`;
4. installs by `mktemp` **in the destination directory** then `mv -f` —
   atomic, same filesystem, so a concurrent slot's ctest or gate never reads
   a half-written pack. `--check` stops at step 3 and only compares.

**The first rebuild was byte-identical to the shipped pack** — sha256
`a59af18dab158a7bc1cc05702a266927343555d3ef74f14d06d47603a2c996aa`,
2,810,863 bytes, `cmp` exit 0. That is T279's determinism finding confirmed
end-to-end across 65 ETC1 encodes, not just re-asserted.

**Test.** `PackReproducible`, `tests/cmake/hygiene.cmake`, labels
`hygiene;slow`, `RUN_SERIAL`, TIMEOUT 900, measured 41 s. It rebuilds into a
temp path and compares the sha256 against **both** the pinned
`data/willeq.eqe1.sha256` and the installed pack — so an input drift and a
hand-edit of the shared file are separately caught.

**Planted fault.** XORed the first pixel byte of
`data/textures/tumbleweed.bmp` (offset 54, `0x00` → `0xFF`):

```
built:  dc30c6df...  pinned: a59af18d...
FAIL: rebuild does not match the pinned sha256.
FAIL: the installed pack differs from the rebuild.
1/1 Test #4100: PackReproducible ...***Failed  41.81 sec
```

Restored → `Passed 41.07 sec`.

**One deviation, absorbed and flagged.** `scripts/refactor/check_doc_citations.py`
had no TRAILING guard on its bare-path regex, so the new
`data/willeq.eqe1.sha256` matched as the path `data/willeq.eqe1.sh` (the
`.sh` extension, `a256` left unmatched) and `DocCitations` FAILed on a file
that exists. Added a guard excluding a following alnum/underscore/dash but
NOT a following `.`, so sentence-ending citations still match. Coverage is
unchanged, measured both ways: **726 OK before, 726 OK after.**

### Leg B — the coin decals are the oracle's own pixels

`tools/tga_reader.h` is the decoder lifted out of `tools/dump_icon_sheet.cpp`
(header-only, no logging dependency, always top-down RGBA8, with a `crop()`
that refuses a rectangle it would have to clamp). **`dump_icon_sheet.cpp` was
never a CMake target** — the tree's only TGA decoder compiled nowhere, which
is why nobody noticed. Both it and `ui_atlas_builder` now build.

`ui_atlas_builder --uifiles-path <dir>` crops the 72x18 strip at (90,140) of
`window_pieces01.tga` into four 18x18 tiles and blits them into **atlas row 4
(y=224) at x = 0/20/40/60**. The 20 px pitch is load-bearing: an 18 px tile at
a 20 px pitch never shares a 4x4 ETC1 block column with its neighbour, so no
coin bleeds into the next one when the atlas is compressed for the pack.
Verified source cells (top-down after the origin flip), centre pixel and
opaque coverage of 324:

| cell | animation | centre RGBA | opaque px |
|---|---|---|---:|
| (90,140) | `A_PlatinumCoin` | 171,174,194,255 | 235 |
| (108,140) | `A_GoldCoin` | 255,240,12,255 | 241 |
| (126,140) | `A_SilverCoin` | 142,143,148,255 | 226 |
| (144,140) | `A_CopperCoin` | 60,21,0,255 | 238 |

Client side: four `UISprite` enumerators in denomination order (so
`CoinPlatinum + denom` is the whole lookup), four `mapSprite` names, and
`renderInventoryPopup` calls `drawSprite(coinDecalRect(layout, denom), …)`
where T393 wrote a letter.

**All-or-nothing is enforced at both levels** (Will's EQE1 rule):
`ui_atlas_builder` exits 1 if `--uifiles-path` is given and the TGA cannot be
read, and `build_willeq_pack.sh` refuses to pack at all if
`window_pieces01.tga` is unreadable. A coinless atlas is the exact regression
B218 predicted — `getSprite()` returns the empty rect and `drawSprite` emits
a degenerate quad — so it must never be packed silently.

**The path from the TGA to the panel**, end to end:

```
uifiles/default/window_pieces01.tga  (90,140) 72x18, 32-bit type-2 TGA
  -> EQT::ToolTGA::load()  (tools/tga_reader.h)  -> top-down RGBA8, flipped
  -> Image::crop(90+18i, 140, 18, 18)            -> four tiles
  -> blitRGBA(20i, 224, ...) + reg("coin_*")     -> ui_atlas.bmp + .json
  -> eqe1_builder --bmp-dir                      -> ui_atlas / _alpha / .json
  -> build_willeq_pack.sh (temp + mv)            -> willeq.eqe1
  -> UIAtlas::load() -> mapSprite("coin_gold")   -> UISprite::CoinGold
  -> renderInventoryPopup -> drawSprite(coinDecalRect(layout, denom))
  -> the pixel at the centre of the gold row's decal box
```

**Pre-fix / post-fix.** Five new device-backed tests
(`UIAtlasCoinDecalTest`, `tests/test_ui_atlas_pixels.cpp`, against the LIVE
pack). Pre-fix — new code, shipped pack — **all five fail**:

```
[  FAILED  ] UIAtlasCoinDecalTest.TheAtlasHasARectForEveryCoinDenomination
[  FAILED  ] UIAtlasCoinDecalTest.GoldIsYellow
[  FAILED  ] UIAtlasCoinDecalTest.CopperIsADarkRedBrown
[  FAILED  ] UIAtlasCoinDecalTest.PlatinumAndSilverAreBrightNeutralsAndPlatinumIsBluer
[  FAILED  ] UIAtlasCoinDecalTest.TheInventoryPopupPaintsTheGoldDecalGold
   gold decal at (491,244) sampled (69,69,71)
```

Post-fix, after the repack: `[  PASSED  ] 5 tests.` The last of those is the
end-to-end pin — it renders the whole popup and samples the centre of the
gold row's decal box, so it fails for a missing sprite, a wrong sprite, a
wrong rect, or a draw-order regression alike.

The static-panel seam moved the other way and is pinned:
`PurseT393Render.TheInventoryPopupEmitsTwoTextEntriesPerCoinRow` →
`…EmitsOneTextEntryPerCoinRow`, **32 → 28** text quads, because the letter is
gone and only the count remains. History in the comment: 24 before T393, 32
after T393, 28 after T398.

**The pack change.** Rebuilt and reinstalled by Leg A's script, atomically,
with no `ctest`, `visual_gate`, `gles2_zone_benchmark` or `willeq` process
running (checked by `ps -eo pid,comm` immediately before). Entry-by-entry
diff of old vs new:

- **3 entries changed**: `ui_atlas`, `ui_atlas_alpha`, `ui_atlas.json`.
- **64 entries byte-identical**, same order, same offsets, same count (67).
- `ui_atlas.json` grew 1,087 → 1,326 B = **+239**, which is the whole file's
  growth (2,810,863 → 2,811,102). The four new sprite records, nothing else.
- sha256 `a59af18d…` → `e60962fb…`; `data/willeq.eqe1.sha256` updated in the
  same commit, and `--check` re-passes against the new pack.
- The original is preserved at `review/T398/willeq.eqe1.orig-abc62f91` (and
  again at `review/T398/_beforepack/willeq.eqe1`), the new one at
  `review/T398/willeq.eqe1.after-T398`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | EXIT=0 |
| `ctest --test-dir build -j6` | **4,104 / 4,105**; sole failure `visual_gate` (see below) |
| `PackReproducible` | PASS (41 s); planted fault FAILs |
| `bash scripts/build-gles2-x86.sh` | EXIT=0 |
| `bash scripts/build-arm-noble.sh` | EXIT=0, `build-arm-noble/bin/willeq` produced |
| visual gate standalone | **image goldens all pass, CPU p95 all under ceiling**; 3 `eqe1MappedBytes` metrics off by +239 |
| `check_file_sizes.sh fable` | see below |

**No golden pixel moved, so the plan's STOP did not fire.** What failed is
three numbers in the `memory-coverage` block of `tests/goldens/manifest.json`
— `eqe1MappedBytes`, the mapped size of the pack set — each off by **exactly
+239**, the pack's byte growth:

| zone | manifest | measured | delta |
|---|---:|---:|---:|
| qeynos2 | 119,944,431 | 119,944,670 | +239 |
| gfaydark | 118,610,523 | 118,610,762 | +239 |
| befallen | 113,095,019 | 113,095,258 | +239 |

Two consecutive gate runs gave bit-identical numbers. **Not self-blessed.**
That block's own note records it was blessed by Will in chat for T303, from
the SAME cause (a rebuilt `willeq.eqe1`), applied by hand rather than by
`--bless` (which would also rewrite the T69 per-zone memory block). Boarded
as **B240** with the exact edit; it needs Will's word.

### Deviations and surprises

1. **B230(b)'s central claim was false.** The pack had no unexplained
   entries at all; the "residue" was `data/textures` misread. The recipe was
   generate-then-pack, and it reproduced byte-for-byte on the first attempt.
2. **The tree's only TGA decoder was never compiled.** `dump_icon_sheet.cpp`
   had no CMake target — B218 and B230 both cited it as "the only TGA decoder
   in the tree" without noticing it was dead code. It builds now.
3. **`check_doc_citations.py` FAILed on a file that exists.** Fixed with a
   trailing guard; coverage measured identical (726 OK) both sides.
4. **The pack now depends on a path outside the repo.** Boarded as **B241**
   with two proposed fixes (SKIP the test without the client tree, or pin the
   TGA's sha256 too). Neither is urgent — every host running this suite has
   the tree — but "reproducible from the tree" is now "from the tree plus the
   official client tree", and that should be said out loud rather than
   discovered.
5. **A `build --target` invocation hit the 600 s ceiling (exit 143) and the
   post-143 sweep found real damage**: `build/lib/libwilleq_core.a` was
   **gone** (killed `ar`), plus an 8-byte `build/lib/stkOUBYP` temp. The
   glob-based ELF scan reported `bad=0` over 289 binaries while the archive
   was missing entirely — exactly the "absence is invisible to a scan"
   hole the protocol warns about. Deleted the temp, rebuilt, re-swept:
   `libwilleq_core.a` back with 323 members, `bad=0`. The cause was CPU
   starvation from a concurrently-running ARM docker build; the two should
   not be overlapped.
6. **The before/after capture pair isolates the ASSET, not the unit.** Both
   images are the same post-fix binary, one against the original pack and one
   against the new one — which shows the empty decal box B218 predicted, not
   T393's letter. Producing the true letter-before needed a second full
   rebuild of reverted code, and that was the build that hit exit 143. Said
   plainly in `review/T398/README.md` rather than papered over.

**Still open, untouched:** B219 (fable's paperdoll rect overlaps the
oracle-placed purse — needs Will's eye) and T393's owed live oracle
screenshot pair (no eqemu lock here; T389 holds it).
