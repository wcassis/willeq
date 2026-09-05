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
