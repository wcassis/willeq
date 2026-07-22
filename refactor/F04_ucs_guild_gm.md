# F04 — Split UCS, guild, and GM clusters out of eq.cpp

Three small self-contained domains leave `src/client/eq.cpp` in one unit.
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; F01+F02 removed 3,216 lines above some of
these so positions have shifted. Enumerate by NAME.)

Three new files under `src/client/`:

**eq_ucs.cpp** (~240 lines) — the UCS chat-server connection cluster
(orig 10075–10313): `ConnectToUCS`, `UCSOn*`, `UCSSend*`, `UCSProcess*`.
Note: a commented-out `ConnectToUCS` block may sit near the old F01 seam —
moved code is the LIVE definitions; leave dead commented code where it lies.

**eq_guild.cpp** (~225 lines) — the guild cluster (orig 15801–16026):
guild invite/accept/remove/demote/leader/MOTD senders plus the guild
Process* packet handlers (~13 methods, names contain `Guild`).

**eq_gm.cpp** (~190 lines) — the GM command cluster (orig 16977–17169):
GM zone request/summon/goto/find/kick/kill/hideme/emotezone/lastname senders
plus their Process* handlers (~12 methods, names contain `GM` or are in the
contiguous GM block — enumerate by reading the block).

## Steps

1. Locate each cluster by grep; confirm contiguity; list the full method set
   per file in the review.
2. Create the three files: brief header comment + pruned include block each
   (network/state code — no graphics/audio headers expected).
3. Move definitions verbatim.
4. Shared file-local helpers: same rule as always — move if exclusive, else
   leave and document.
5. Add all three files to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the two documented exceptions in
  refactor/test_baseline.txt (RDP flake: re-run isolated; ALSA
  AudioManagerTest.PreloadCommonSounds: expected-fail on this host).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS or
  documented de-static-ing only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_ucs.cpp <abs>/eq_guild.cpp <abs>/eq_gm.cpp` — ok.
- eq.cpp shrinks by ~600+ lines.

## Review

(executor fills in)
