# B930 (#248): a stale EQE1 zone pack fails loudly by pack name + version

## The measured cause

Issue #248: soldunga FATALs 2 s into the zone load with

    FATAL Texture 'gnome_stronghold_floor_transition.bmp' not found in any EQE1 flat pack

which names a **texture**, not the cause. The orchestrator already answered
"stale pack or loader regression?" (2026-10-10, [measured]): the copy of
`/mnt/projects/EverQuest_ETC1/soldunga.eqe1` that dev-u24 and the Pi read is
header version **1** (`xxd -l 12` -> `4551 4531 0100 ...`, file dated
2026-03-22), while the hermes-ext copy is version **3** (2026-09-21). The
loader fix of MR !27 (EQE1 v3 name table, `include/client/graphics/
eqe1_format.h`) is fine; the data is stale on two of three hosts.

This host's survey confirms the shape of the staleness [measured,
`/mnt/projects/EverQuest_ETC1/`, 2026-10-10]:

- 1506 `.eqe1` packs: 1504 v1, 1 v2 (`willeq.eqe1`, 132 entries — the
  client's OWN pack, deliberately v1/v2 because every name fits 31 chars),
  1 v3 (the rebuilt `soldunga.eqe1`).
- Max entry name in any v1/v2 pack: **31 characters**, and **zero**
  v1/v2 entries with an unterminated 32-byte name field. The one
  truncation signature in the tree is v1 `frozenshadow.eqe1` holding a
  31-char name the old builder wrote as `strncpy(..., 31)` over a longer
  original.

That last point is why B930's failure CANNOT live at open: a 31-char name
followed by NUL is **byte-identical** to a genuine 31-char name. At open
time nothing distinguishes "the pack is stale and truncated
`gnome_stronghold_floor_transition`" from "the pack legitimately holds a
31-char texture". B910's unterminated-field check catches only the
un-NUL-padded corruption case; the stale-pack case is NUL-PADDED,
formally valid. (The old builder did `EQE1IndexEntry idx = {}` then
`strncpy(idx.name, name, 31)` — zero-init, so the truncation is
null-padded, not unterminated.) Rejecting v1/v2 at open would also break
`testV1PayloadFormatDefaultsToETC1` (which opens a real v1 pack) and the
B852 CI zone-content sweep (which loads qeynos2's v1 packs every pipeline),
and contradict the builder's documented intent ("a pack whose every name
fits in 31 chars stays v1/v2").

The signature is only decidable when the client meets a **>31-char name**:

- no v3 pack can miss a name it was told to store (the name table is
  full-length — B910), so
- a v1/v2 pack that holds the 31-char prefix of a missing >31-char name is,
  by construction, the pack that truncated it.

That check is what this change adds.

## What changed

**`include/client/graphics/eqe1_loader.h` / `src/client/graphics/
eqe1_loader.cpp`**

- `EQE1Loader::getFormatVersion()` — the header version `open()` read (0
  when closed). New member `formatVersion_`, maintained across
  open/close/move.
- `EQE1Loader::holdsTruncatedName(name)` — true iff this pack is v1/v2,
  `name` is longer than 31 chars, and the pack's texture index holds
  `name.substr(0, 31)`. No-op for v3 (a v3 pack cannot truncate) and for
  names that fit the 31-char inline field.
- `StaleEQE1PackDiagnosis` + `diagnoseStaleEQE1Pack(loaders, name)` — the
  pure half of the diagnosis, split from the FATAL site so it is testable
  without a GL context (`ConstrainedTextureCache` needs irr::video device
  headers the standalone loader test does not link). Scans the open-
  archive snapshot and returns the first (open order — zone-own packs open
  first) pack that holds the truncation, with its path, version, and the
  31-char key.

**`src/client/graphics/constrained_texture_cache.cpp`** (the FATAL site in
`getOrLoad()`)

The old FATAL named only the texture. Now: before the generic asset-gap
FATAL, run the diagnosis. If a stale pack is identified, the FATAL is:

    EQE1 pack '<path>' is format v1; this client needs v3 (rebuild with
    tools/convert_all_to_eqe1.sh). The pack's inline name field caps at 31
    chars, so it holds 'gnome_stronghold_floor_transiti' but not
    'gnome_stronghold_floor_transition'; ...

— one FATAL line naming the pack path, its version, and the required
version (criterion 1). Otherwise the original asset-gap FATAL is
unchanged, so a name genuinely absent from every pack (a real asset gap)
keeps its existing message. `std::exit(1)` is unchanged.

**`tools/verify_eqe1.cpp`** (criterion 2)

- Every line now prints the pack's header version: `OK (v3, 132 textures,
  ...)`.
- New `--require-v3` flag: a v1/v2 pack is reported as STALE only when it
  holds a 31-char entry name (the truncation signature) and the run exits
  non-zero — so a host's pack directory is one audit loop:

      for f in <dir>/*.eqe1; do ./build/bin/verify_eqe1 --require-v3 "$f"; done

  The real STALE line (B1017's shape; one 31-char name):
  `STALE: format v1 below v3, holds 1 31-char name(s) (truncation candidates: a v1/v2 pack stores exactly 31 chars inline; these are the ones a pre-B910 builder would have cut from a longer original). '<name>' — rebuild with tools/convert_all_to_eqe1.sh if any of them is truncated; the client FATAL names the exact pack when it bites (B930, #248)`
  [measured 2026-10-12 on this branch]. B1017 (#338) narrowed the flag:
  before, a CLEAN v1/v2 pack was also STALE (version-shaped, 1506/1506 on
  the NFS tree). Now a structurally-clean v1/v2 pack whose names all fit
  31 chars prints OK and exits 0; and a 31-char `_alpha` companion whose
  base entry is present and shorter is a complete name, not a candidate
  (frozenshadow's `bricktrimflorish&protrait_alpha` = 25-char base +
  `_alpha`).

  Deliberately a FLAG rather than a blanket failure: `willeq.eqe1` (the
  client's own pack, v2, all names ≤31) is a legitimate format that the
  pinned `PackReproducible` test rebuilds, and the B852 CI zone-content
  sweep loads qeynos2's v1 packs every pipeline. The audit flag is for
  hosts whose ZONE packs show the truncation signature — the soldunga case.
  (Survey at :20-27 confirmed against the live packs on 2026-10-12: the
  single 31-char name in the NFS tree is frozenshadow's
  `bricktrimflorish&protrait_alpha` — a COMPLETE companion name (25-char
  base + `_alpha`), which B1017 no longer flags; the only real truncation
  in the tree remains soldunga's.)

**Tests** (RED/GREEN)

- `tests/test_eqe1_stale_pack.cpp` (new, standalone — no GL, no GTest, same
  shape as `test_eqe1_loader`): the diagnosis on synthetic packs (header +
  one entry, the layout `eqe1_format.h` defines) —
  - a v1 pack with a NUL-padded 31-char truncation LOADS (by design — that
    is the silence), reports v1, misses the full name, and is diagnosed as
    stale with pack path + version + truncated key;
  - the rebuilt v3 twin loads, reports v3, round-trips the full name, and
    is NOT blamed for a name it genuinely doesn't hold;
  - negatives: a closed loader, a short name, and an empty open set all
    diagnose as "plain asset gap";
  - the zone shape: the zone-own v1 pack (opened first) plus a lazy v3
    global — the diagnosis names the v1 zone pack;
  - the end-to-end leg: a forked child runs the real diagnosis + the
    all-or-nothing `exit(1)`, and the parent asserts the diagnosis line carries
    `stalePack=1 version=1 path=... truncated=
    gnome_stronghold_floor_transiti`. On origin/fable this leg is RED the
    other way: the pre-B930 code names only the texture, so the child's
    diagnosis is absent and the parent's assertion fails.
- `scripts/ci/test_eqe1_verify_stale_pack.py` (new ctest leg, criterion 2;
  extended by B1017): writes synthetic packs in python and runs the BUILT
  `verify_eqe1` — a CLEAN v1/v2 (all names ≤31 chars) must print `OK` +
  its version and exit 0; a v1/v2 holding a 31-char name must print
  `STALE` + `31-char name` and exit 1; a 31-char `_alpha` companion whose
  base entry is present and shorter passes (complete name); a 31-char
  `_alpha` whose base was 31+ is STALE; a v3 pack passes. RED on
  origin/fable (no version in the line, no `--require-v3` flag at all, and
  the STALE shape was version-shaped, not name-shaped).

**Docs**: `docs/plans/full_etc1_textures.md` (the EQE1 version section)
carries the audit command and the v1/v2-are-legitimate note. The root
CLAUDE.md's pack-recipe section is the other spot; that edit was
consent-blocked in this headless run (agent-instruction files need user
approval) and is proposed verbatim in the MR description.

## Rebuilding the stale host's pack

`tools/convert_all_to_eqe1.sh` walks an S3D tree and emits one `.eqe1` per
S3D archive; it has no single-zone flag, so the rebuild of ONE zone on the
stale host is the builder directly:

    ./build/bin/eqe1_builder \
        --s3d <path-to-EQ-client>/soldunga.s3d \
        --out <pack-dir>/soldunga.eqe1

(the script's own form — `bash tools/convert_all_to_eqe1.sh --s3d-dir
<EQ-client> --out-dir <pack-dir>` — rebuilds the WHOLE tree in one loop.)

The rebuilt pack is v3 only if some name is >31 chars (soldunga is — that
is exactly why it was stale); a zone whose names all fit stays v1/v2, which
is correct. Verify the rebuild on the spot:

    ./build/bin/verify_eqe1 <pack-dir>/soldunga.eqe1

should print `OK (v3, ...)` for soldunga and exit 0.

## Not verified / left open

- **The NFS rebuild itself**: this host has no NFS. The dev-u24 / Pi copies
  of `soldunga.eqe1` (and any other zone pack on that tree) are still v1
  until the orchestrator rebuilds and re-lands the NFS tree. `verify_eqe1
  --require-v3` over that directory is the one-loop check that proves it
  when the rebuild lands.
- **The live soldunga zone load on a rebuilt pack**: not run here (no
  eqemu; zone sweeps are out of scope per the card). The diagnosis is
  exercised end-to-end (fork/exit) on a synthetic stale pack that matches
  the real stale file's byte shape (v1 header; 31-char NUL-padded name —
  the orchestrator's measurement of the stale copy).
- **The CLAUDE.md one-liner for the audit command**: the docs/plans/ edit
  is committed; the CLAUDE.md section edit is proposed in the MR
  description (consent-blocked here).

## Claims

- [measured] 1506 packs on this host: 1504 v1 / 1 v2 / 1 v3 (soldunga,
  rebuilt 2026-09-21). Max v1/v2 name length 31; zero unterminated name
  fields.
- [measured] Stale soldunga.eqe1 (the dev-u24/Pi copy) is v1, dated
  2026-03-22; the hermes-ext copy is v3, dated 2026-09-21
  (orchestrator's 2026-10-10 measurement, quoted above).
- [measured] `willeq.eqe1` is v2, 132 entries, max name length 31.
- [measured] Local ctest on this branch: `test_eqe1_stale_pack` 5/5 pass;
  `test_eqe1_loader` 19/19 pass (the existing B910 pins still hold — v1
  packs still load; the diagnosis changes nothing for any pack that does
  not exhibit the truncation signature); `test_eqe1_verify_stale_pack`
  passes against the built tool.
- [assumed] No zone besides soldunga currently carries a >31-char name in
  a v1/v2 pack (the survey found no other truncation signatures in the
  tree). A zone that does would have died the same way and now names its
  pack.
