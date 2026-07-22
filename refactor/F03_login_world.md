# F03 — Split login + world connection into eq_login_world.cpp

Move the login-server and world-server connection clusters out of
`src/client/eq.cpp` (16,460 lines after F02). Pure move — no behavior change,
no interface change, no reformatting.

## Scope (original line refs @ a0c02cd were 1479–1709 login, 1710–2155 world;
F02 removed 735 lines above them so current positions are ~735 lower.
Enumerate by NAME, not line number.)

Move to new `src/client/eq_login_world.cpp` every `EverQuest::` method whose
name matches the login/world connection families:
- Login cluster (~11 methods): `LoginOn*`, `LoginSend*`, `LoginProcess*`
  (connection lifecycle, credential exchange, server list).
- World cluster (~21 methods): `WorldOn*`, `WorldSend*`, `WorldProcess*`
  (character select, zone handoff).

Grep `EverQuest::Login` and `EverQuest::World` in eq.cpp for the definitive
set; list every moved method in the review. Methods outside these two
contiguous clusters (if any match the prefix elsewhere) need a judgment call —
if one sits outside the cluster range, leave it and document why.

## Steps

1. Locate both cluster boundaries by grep; confirm they are contiguous.
2. Create `src/client/eq_login_world.cpp`: brief header comment + include
   block (start from eq.cpp's includes, prune compiler-driven — this cluster
   is network/state code and should need no graphics/audio headers).
3. Move definitions verbatim.
4. File-local `static` helpers referenced by moved code: move if exclusively
   used by it; otherwise leave and document (de-static only if unavoidable;
   note `eqcrypt_block` near the top of eq.cpp is used by login crypto —
   check who references it and handle accordingly, documenting the choice).
5. Add the new file to `CLIENT_CORE_SOURCES` in CMakeLists.txt next to
   eq_strings.cpp.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the two documented exceptions in
  refactor/test_baseline.txt (RDP flake: re-run isolated; ALSA
  AudioManagerTest.PreloadCommonSounds: expected-fail on this host).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS or
  documented de-static-ing only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs path>/eq_login_world.cpp` — ok.
- eq.cpp shrinks by ~650+ lines.

## Review

(executor fills in)
