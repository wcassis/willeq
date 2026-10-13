# B1017 — the EQE1 audit flags the truncation signature, not the version (GitLab #338)

Branch `task/kb-b1016-b1017-gldriver-prefix-eqe1-verify` (the B1016 half shares
this branch — `refactor/B1016/README.md`).

## The problem

Two defects sat in the B930 stale-pack net:

1. **The audit was version-shaped.** `verify_eqe1 --require-v3` reported
   STALE on ANY v1/v2 file. On `origin/fable` it flagged **1,505 of the
   1,506** packs in the host's data directory (1,504 v1 + 1 v2 —
   soldunga.eqe1 was already a rebuilt v3 when the paired audit ran, so it
   passed even the version-shaped flag) — including `willeq.eqe1`, which is v2
   BY THE BUILDER'S DESIGN and whose longest name is 26 chars (nothing in it
   could ever be truncated). A net that flags every legitimate pack teaches
   no one anything; the acceptance criterion ("a clean v1/v2 must NOT be
   flagged") fails outright.
2. **The client-side diagnosis could be skipped by the request's spelling.**
   `diagnoseStaleEQE1Pack(loaders, name)` compared the RAW requested name
   against the pack's 31-char inline keys, but the lookup itself
   (`findCompressedEntry`) normalizes (lowercase + strip `.bmp`/`.dds`).
   `willeq.eqe1`'s keys are lowercase; the zone loader asks for
   `Gnome_Stronghold_Floor_Transition.BMP` (mixed case + suffix). The
   normalized form reaches the pack's 31-char truncation, the raw form does
   not — so a mixed-case request fell straight to the GENERIC asset-gap
   FATAL, the same wrong message soldunga died with in #248. And B930's test
   called `diagnoseStaleEQE1Pack()` directly and `_exit(1)`ed unconditionally,
   so deleting the diagnosis CALL at the FATAL site would have left it green.

## What changed

- **`verify_eqe1 --require-v3` flags the signature, not the version**
  (`tools/verify_eqe1.cpp`): a v1/v2 pack is STALE only when it HOLDS A
  31-CHAR NAME — the only shape a pre-B910 builder's silent `strncpy` produces
  for a longer original. A structurally-clean v1/v2 pack whose names all fit
  prints OK. The result struct carries `truncationCandidates` (the offending
  names) and the message names them.
  One named exception: a 31-char `_alpha` companion is a truncation candidate
  only when its base name (the name minus the `_alpha` suffix) is ABSENT from
  the same pack or was itself 31+ chars. A base that is present and shorter is
  a real entry, so the companion's 31-char name is a COMPLETE name, not a
  chopped base. This is not a cosmetic filter: the live pack audit found the
  single 31-char name in the tree is frozenshadow's
  `bricktrimflorish&protrait_alpha` (25-char base + `_alpha`, both entries in
  the pack) — a genuine companion that the flag must NOT report; without the
  exception the audit would find one STALE pack and a re-audit on any host
  would have to learn to ignore it by name, and the moment a REAL truncation
  landed in the same pack the exception would have hidden it too.
- **The diagnosis normalizes** (`src/client/graphics/eqe1_loader.cpp`):
  `diagnoseStaleEQE1Pack` applies the same lowercase + suffix-strip
  normalization `findCompressedEntry` uses before the 31-char-prefix check,
  so every request spelling that can reach the miss path gets the diagnosis.
- **The FATAL names both** (`src/client/graphics/constrained_texture_cache.cpp`):
  the stale message reports the 31-char key the pack holds AND the full
  requested name, so a future reader sees the truncation without the tool.
- **The real-miss test** (`tests/test_eqe1_real_miss.cpp`): drives the REAL
  `ConstrainedTextureCache::getOrLoad()` miss in a fork child (the FATAL
  site is `std::exit(1)` — all-or-nothing, it cannot run in the test
  process), one open v1 pack holding the truncation, a fresh
  `ConstrainedTextureCache`. The parent greps the child's stderr (LOG_FATAL
  goes through the logger's ERROR path) for the stale FATAL and asserts the
  generic message is absent. Two cases: the exact 32-char truncation and the
  mixed-case `.BMP` request (the audit's (d) spelling).

## Evidence

- **Red on `origin/fable`** (base worktree at `06852f31`, new tests seeded
  without the fixes):
  - `test_eqe1_verify_stale_pack` **FAILED**: "legitimate v1 pack (no
    31-char name) must pass --require-v3, got exit 1" — fable's verifier
    prints `STALE: format v1 below v3` on the clean pack. Output:
    `red_c_verify_audit.txt`.
  - `Eqe1RealMiss.MixedCaseBmpRequestStillGetsTheStaleDiagnosis` **FAILED**:
    the child logged `Texture 'Gnome_Stronghold_Floor_Transition.BMP' not
    found in any EQE1 flat pack.` (the generic message) — fable's diagnosis
    does not normalize. Output: `red_de_real_miss_mixedcase.txt`.
    (The exact-truncation case already passed on fable: lowercase requests
    were the B930 path.)
- **Green (branch tip):** `test_eqe1_verify_stale_pack` PASS (four
  synthetic packs: clean v1/v2 OK, 31-char v1 STALE naming the candidate,
  31-char `_alpha` with a present shorter base OK (the frozenshadow shape),
  31-char `_alpha` whose base was 31+ STALE, v3-with-33-char-name OK);
  `test_eqe1_real_miss` 2/2;
  `test_eqe1_stale_pack` (the B930 leg) still green.
- **Live pack audit (2026-10-12, `/mnt/projects/EverQuest_ETC1`, 1,506
  packs):** `--require-v3` reports **0** STALE candidates — the pre-fix
  version-shaped flag reported 1,505 (1,504 v1 + 1 v2; soldunga.eqe1 was
  already a rebuilt v3, so it passed even the version-shaped flag). The
  tree's single 31-char name is
  frozenshadow's `bricktrimflorish&protrait_alpha` (25-char base + `_alpha`,
  both entries present), which the `_alpha` exception correctly passes;
  soldunga.eqe1 is already a rebuilt v3 (144 textures). Re-derivable via
  `qa_audit_both.py` in the evidence dir.
- **The mutation that proves the (d)/(e) net** (acceptance 8): with the
  diagnosis call neutered at the FATAL site
  (`diagnoseStaleEQE1Pack(std::vector<const EQE1Loader*>{}, name)`), both
  `test_eqe1_real_miss` cases FAIL with the generic message present —
  `red_real_miss_mutation.txt` [measured]. Restored: 2/2 PASS.

## Not verified / left open

- The acceptance criterion's "a zone that NEEDS a longer name" half is
  exercised by the synthetic 32-char pack and by the client-side real miss,
  not by a live zone load: no shipped zone currently has a texture whose
  name exceeds 31 chars (the `willeq.eqe1` census: max 26). The net exists
  for the next one.
- `verify_eqe1`'s exit code is 1 for a STALE pack and 0 for a clean one under
  `--require-v3`; without the flag the version is reported, never an error.
