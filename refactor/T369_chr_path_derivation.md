# T369 — character-archive path derived from the FILENAME, not the whole path (B105)

Class B, slot3, one commit. Read the B105 board text (T366 measured it).

`S3DLoader::loadCharacters` (`src/client/graphics/eq/s3d_loader.cpp`)
derives `<zone>_chr.s3d` by `find("_chr")` over the WHOLE archive path, so
a directory component containing `_chr` truncates the path and every
character load returns zero — silently, because the search path caches
"no characters" as a negative entry.

1. Derive from the filename component only (`std::filesystem::path`:
   parent_path / stem), and match `_chr` as a SUFFIX of the stem, not the
   first occurrence anywhere (a zone named `x_chr_y` is not a thing, but a
   stem ending in `_chr` is exactly the case). Keep the behaviour for the
   normal case byte-identical (same string out for every archive under
   `/mnt/projects/EverQuestP1999` — assert it in the test over the real
   directory listing).
2. The silence: a zero-character result from a `_chr` archive that EXISTS
   on disk is a defect signal, not a negative-cache entry — log it at WARN
   with the path once (find where `searchZoneChrFilesForModel` files the
   negative entry and add the log there; do not change the caching).
3. Tests, pre-fix failing: (a) an archive under a directory named
   `/tmp/.../foo_chr_bar/` resolves to `<that dir>/<zone>_chr.s3d`; (b) the
   real-directory identity assertion above (passes both sides — a pin);
   (c) the WARN line appears for an existing-but-empty `_chr` archive
   (T366's junk-archive fixture in `tests/test_chr_cache_lru.cpp` shows
   how).
4. Strike B105.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, file sizes;
`src/client/graphics` changes → visual gate standalone + ARM.

STOP: none expected.

## Review

Branch `task/T369-chr-path-derivation`, base `fable` tip `81765ef9`. The fix
is `bae6befc`; the board strike, the T366 back-reference and this Review are
the branch tip. No eqemu lock taken — nothing here needs a server. No STOP hit.

### The derivation now used

`S3DLoader::deriveChrArchivePath(archivePath)` — public and static, so it is
testable on its own — is now the single place the character archive is named,
and `loadCharacters()` calls it:

- `std::filesystem::path` splits the input into `parent_path()` and `stem()`;
  every directory component is left untouched, which is the whole of the fix;
- a **trailing** `_chr` is stripped from the stem, allowing an optional numeric
  variant (`_chr2`), i.e. the regex `_chr[0-9]*$`;
- the result is `<parent>/<stem>_chr.s3d`, or the bare filename when the input
  carried no directory at all.

**Deviation from the plan, flagged.** The plan said "match `_chr` as a SUFFIX
of the stem". A bare suffix match is wrong for this tree: `acrylia_chr2.s3d`,
`bothunder_chr2.s3d`, `cazicthule_chr2.s3d` and their siblings are real files,
their stems end in `_chr2`, and the old code folded them onto `<zone>_chr.s3d`
by truncating at the first `_chr`. Allowing trailing digits reproduces that;
without it the identity pin below fails on every `_chr2` archive. The plan's
intent (a suffix, not "anywhere in the path") is met.

### The identity pin — what it actually covered

`DerivationIsUnchangedForEveryFileInTheRealClientTree` iterates the real client
tree and compares the new derivation against the pre-fix algorithm,
reimplemented verbatim in the test as `legacyDeriveChrArchivePath()`.

| scope | count | divergences |
|---|---|---|
| `.s3d` archives under `/mnt/projects/EverQuestP1999` | **1,006** | **0** |
| all regular files seen while iterating | 2,338 | (asserted on archives only — see below) |

**Surprise.** Over ALL files there is exactly one divergence and it is a
DOTFILE: `.DS_Store`. `std::filesystem::path` gives a leading-dot filename no
extension (stem = `.DS_Store`), where the legacy `find_last_of('.')` over the
whole path treated the entire name as an extension and derived
`<dir>/_chr.s3d`. Nothing ever derives a character archive from a dotfile, and
the new reading is the correct one, so the pin asserts over `.s3d` archives and
the divergence is stated out loud in its own test —
`DotfilesKeepTheirNameInsteadOfBeingTreatedAsAnExtension` pins both sides
explicitly rather than leaving a future reader to think it was overlooked.

### The silence

Both negative-cache branches in `searchZoneChrFilesForModel` now log at WARN,
naming the full path: "`'<path>' is a _chr.s3d archive that yielded NO
characters (…) -- caching the miss`". The caching is untouched, so the line
appears once per path (the negative entry stops the scan re-reaching it).

**Deviation, flagged.** The plan asked for the warning on the existing-but-empty
branch. It is there, but that branch turned out to be **unreachable with any
real archive once the derivation is correct** — measured, not assumed. With
`zznochars_chr.s3d` symlinked in turn at `arena.s3d`, `arena_obj.s3d`,
`gfaydark_obj.s3d`, `qeynos2_obj.s3d`, `gequip.s3d`, `gequip2.s3d`,
`qeynos2.s3d` and `lavastorm.s3d`, `loadZone()` succeeded every time and
`getCharacterCount()` was 1, 10, 85, 52, 194, 84, 1 and 1 respectively — never
0. The fixed derivation resolves a file named `*_chr.s3d` to ITSELF, so any
archive holding a skeleton fragment yields characters. **T366's Review calls
`zznochars_chr.s3d` (arena.s3d) an archive that "loads clean with zero
characters" — that was true only because the broken derivation sent it at a
path that did not exist.** The warning therefore sits on both branches and the
test drives the deterministic one, T366's 64-byte junk archive
`zzbroken_chr.s3d`, which is the fixture the plan named.

### Tests — `tests/test_chr_path_derivation.cpp` (new TU, registered in `tests/cmake/ui_rendering.cmake`)

Pre-fix runs used a shim that kept the new public signature with the OLD body,
so the tests compile and fail on behaviour rather than on a missing symbol.
Staged with exported patches and `git apply -R`; no `git stash`.

| test | pre-fix | post-fix |
|---|---|---|
| `DirectoryComponentContainingChrDoesNotTruncateThePath` | **FAIL** — `/tmp/willeq_chrscan_1185004/beholder_chr.s3d` -> `/tmp/willeq_chr.s3d`; `/tmp/a_chr_b/acrylia_chr2.s3d` -> `/tmp/a_chr.s3d` | PASS |
| `CharactersLoadFromAnArchiveUnderAChrNamedDirectory` (end-to-end `S3DLoader::loadZone` out of `/tmp/willeq_chrscan_<pid>/`) | **FAIL** — derived `/tmp/willeq_chr.s3d`, `getCharacterCount()==0` | PASS |
| `DerivationIsUnchangedForEveryFileInTheRealClientTree` (the identity pin) | PASS — 1,006 archives, 0 divergences | PASS — same |
| `DotfilesKeepTheirNameInsteadOfBeingTreatedAsAnExtension` | **FAIL** — `/eq/.DS_Store` -> `/eq/_chr.s3d` | PASS |
| `EmptyChrArchiveIsReportedRatherThanSilentlyCached` | **FAIL** — captured log EMPTY; the negative entry was filed in silence | PASS |

Pre-fix: 4 failed, 1 passed (the pin, by design). Post-fix: 5 passed.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (no exit-143 anywhere in this unit; no post-143 sweep needed) |
| `ctest --test-dir build -j6 --output-on-failure` | **3,937 of 3,938 passed** (exit 8). Sole failure `AudioManagerTest.PreloadCommonSounds`, the waived ALSA host quirk. No load artefacts |
| `scripts/build-gles2-x86.sh` | EXIT 0 |
| `scripts/ci/visual_gate.sh` | **PASS** (qeynos2, gfaydark p95 5.508 / ceiling 10.672, befallen 6.140 / 10.948). `git status tests/goldens` clean |
| `scripts/build-arm-noble.sh` | EXIT 0 |
| `scripts/refactor/check_file_sizes.sh fable` | **14 checked, 14 ok, 0 warn, 0 fail** (`s3d_loader.cpp` 1,336; `model_loading_search.cpp` 607; `s3d_loader.h` 349) |

### Board and docs

- **B105 struck** with this commit, the derivation, the 1,006-archive identity
  count and the both-branches warning.
- `refactor/T366_chr_cache_sites_handle_residue.md` — its "B105 added" line now
  says "closed by T369", so the trail does not dead-end on an open item.

### Deviations

Two, both above and both flagged: the trailing-digit allowance in the suffix
strip (`_chr2` archives are real), and the warning going on BOTH negative-cache
branches because the existing-but-empty one is unreachable with any real
archive once the derivation is correct.
