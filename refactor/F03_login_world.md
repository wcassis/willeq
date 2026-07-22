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

### Boundaries found

Both clusters were fully contiguous in `eq.cpp` (base commit
`70c0115b885bbb1c1dcc4e59e428a701a7d43b8b`, the merge-base of `task/F03-login-world`
with `fable`):

- Login cluster: lines 744–973 (11 methods, `EverQuest::LoginOnNewConnection`
  through `EverQuest::LoginDisableReconnect`).
- World cluster: lines 992–1418 (21 methods, `EverQuest::WorldOnNewConnection`
  through `EverQuest::WorldProcessZoneServerInfo`).

Between the two clusters (lines 975–990) sits `EverQuest::ConnectToWorld`.
It does **not** match the `Login*`/`World*` method-name grep (`EverQuest::World`
only matches names starting with `World`, and `ConnectToWorld` doesn't), so per
the plan's exact-name scoping it was left in `eq.cpp`. Likewise
`EverQuest::ConnectToLogin` (line 358, not adjacent to either cluster) was left
in place. This matches the plan's own grep instruction (`EverQuest::Login`,
`EverQuest::World`), which excludes both `ConnectTo*` methods by construction.

### Moved methods (33 EverQuest:: methods + eqcrypt_block)

Login cluster (12 methods — plan estimated ~11), grep-confirmed set in file order:
`LoginOnNewConnection`, `LoginOnStatusChangeReconnectEnabled`,
`LoginOnStatusChangeReconnectDisabled`, `LoginOnPacketRecv`,
`LoginSendSessionReady`, `LoginSendLogin`, `LoginSendServerRequest`,
`LoginSendPlayRequest`, `LoginProcessLoginResponse`,
`LoginProcessServerPacketList`, `LoginProcessServerPlayResponse`,
`LoginDisableReconnect` — 12 methods.

World cluster (21): `WorldOnNewConnection`, `WorldOnStatusChangeReconnectEnabled`,
`WorldOnStatusChangeReconnectDisabled`, `WorldOnPacketRecv`, `WorldSendSessionReady`,
`WorldSendClientAuth`, `WorldSendEnterWorld`, `WorldProcessCharacterSelect`,
`WorldSendApproveWorld`, `WorldSendWorldClientCRC`, `WorldSendWorldClientReady`,
`WorldSendWorldComplete`, `WorldProcessGuildsList`, `WorldProcessLogServer`,
`WorldProcessApproveWorld`, `WorldProcessEnterWorld`, `WorldProcessPostEnterWorld`,
`WorldProcessExpansionInfo`, `WorldProcessMOTD`, `WorldProcessSetChatServer`,
`WorldProcessZoneServerInfo`.

Total moved: 12 login + 21 world = 33 `EverQuest::` method definitions, plus one
free function (`eqcrypt_block`) = 34 definitions moved verbatim.

### Helper analysis: `eqcrypt_block`

`eqcrypt_block` (free function, DES encrypt/decrypt helper, defined just above
`EverQuest::EverQuest(...)` at the old line 189) has no header declaration and
is called from exactly two places: `LoginSendLogin` and
`LoginProcessLoginResponse` — both moved. No other `.cpp` file references it
(confirmed via repo-wide grep). It was moved verbatim into
`eq_login_world.cpp`, ahead of the login cluster, with a comment explaining
why. No de-static-ing was needed (it was never `static`, just file-local via
lack of a header declaration — this is preserved, since it's not declared
`static` in either file and nothing else calls it).

### Line counts

- `eq.cpp`: 16,460 → 15,788 lines (−672).
- `eq_login_world.cpp`: 696 lines (new file: 12 header/include lines +
  eqcrypt_block (15 lines) + 33 moved method bodies + blank-line spacing).
- Net shrink matches expectation ("~650+ lines") plus the extra 15-line
  `eqcrypt_block` helper.

### Verifier summary

- `cmake --build build -j24`: clean (only pre-existing warnings: OpenSSL 3.0
  `DES_ncbc_encrypt` deprecation carried over unchanged from the original
  `eq.cpp` location, and unrelated pre-existing `void*` pointer-arithmetic
  warnings elsewhere in `eq.cpp`).
- `ctest --test-dir build`: 1191/1192 passed. The only failure was
  `AudioManagerTest.PreloadCommonSounds` (ALSA, expected-fail on this host per
  `refactor/test_baseline.txt`). No RDP flake observed this run.
- `verify_move_only.py <merge-base>..HEAD`: PASS — pure move, no de-static-ing,
  no substantive line changes outside the two touched files.
- `check_file_sizes.sh --files eq_login_world.cpp`: ok (696 lines, well under
  the 1,500-line target ceiling).

### Deviations

- `ConnectToWorld` and `ConnectToLogin` were intentionally left in `eq.cpp`
  despite being connection-lifecycle code, because their names don't match
  the `Login*`/`World*` prefix families the plan scoped this unit to (see
  "Boundaries found" above). This is a judgment call per the plan's own
  instruction to leave out-of-pattern methods and document the reason.
- No other deviations. No reformatting, no signature changes, no behavior
  changes.
