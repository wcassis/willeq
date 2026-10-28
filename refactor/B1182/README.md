# B1182 (#501): eqclient.ini junk/absent value parity

Fable kept the DEFAULT when an `eqclient.ini` particle/options key held junk (the
B1165 rule). The official client's own readers do not: its int reader is a
`GetPrivateProfileInt`-style read (non-numeric -> 0, not the default), its float
reader returns the pre-seeded default on a failed `sscanf`, and `ShowSpellEffects`
uses `atol` (non-numeric -> 0 = off) and REWRITES an absent or out-of-range value
as 1 **in the file at load**. This card makes fable's per-key readers behave
identically, junk handling included (Will's rule), with a unit test per key and a
lane-5 before/after check on both clients.

Decompile = `review/B1141/ghidra/out/` (`eqgame.exe.c`, `eqgame.dis`); the lane's
own binaries (`md5sum`: eqgame.exe a9de1b8cc5c451b32084656fcacf1103, identical in
review/B1141/bin and /mnt/projects/EverQuestP1999). `eqgame.exe.c` line cites are
not checked by the cite checker (it knows no `.c`); every one below was re-read by
hand at the line given.

## 1. The three readers, and the per-key table (acceptance A)

The client reads every Options > Particles key with one of three bodies, all in
`eqgame.exe.c`:

* **Float** = `FUN_004ae67a` (132182-132197). A `GetPrivateProfileString` read into
  a buffer PRE-SEEDED with the default, then `sscanf %f` (0x610837) at 0x4ae705.
  On a missed parse (junk, or an empty value) the sscanf writes nothing and the
  PRE-SEEDED default is returned. The value is then CLAMPED by the loader (the
  clamp block under each read, 132608-132707). `sscanf %f` also parses
  `inf`/`nan` and overflows `1e400` to `+/-HUGE_VAL`; the clamp maps -inf to the
  LOWER bound and +inf/NaN to the UPPER one (a NaN fails every comparison, so it
  takes the `else` arm).
* **Int** = `FUN_004ae64c` (132167-132176). `GetPrivateProfileInt` on
  `.\defaults.ini` then `.\eqclient.ini`; its `atoi` is eqgame 0x60e31b (0x60e3a3
  is the thunk; `_atol` is the same body): skip leading whitespace, one optional
  sign, accumulate the decimal digits in 32 bits (WRAPPING), negate when signed.
  A value with NO digit prefix gives 0. The int is kept RAW, unvalidated, and
  decoded BY ITS BITS at use (the cast-filter test `FUN_00491cff`, 115163-115221).
* **ShowSpellEffects** = its own path, 126006-126011, NOT the bool reader.
  `GetPrivateProfileString` with the default `*NULL*`; `_atol` (junk -> 0); the
  value is KEPT when it is 0 or 1, and an ABSENT value (the buffer's first char is
  `*`) or an out-of-range value (< 0 or > 1) is set to 1 and WRITTEN BACK to the
  file with `WritePrivateProfileString` (126010-126011). `atol("TRUE")` is 0 there
  (the bool reader is not used).

Per key (all `[Defaults]` except the legacy key, which is in the `[Options]`-group
section `local_88`; reader line, clamp block, junk result, absent result, and
rewrite-on-load):

| key | reader (eqgame.exe.c) | junk -> | absent -> | rewrite-on-load |
|---|---|---|---|---|
| SpellParticleDensity | float, def 1.0, clamp [0,1] (132608-132617) | 1.0 (High) | 1.0 (High) | no |
| SpellParticleOpacity | float, def 1.0, clamp [0,1] (132618-132627) | 1.0 (100) | 1.0 (100) | no |
| SpellParticleNearClipPlane | float, def 2.0, clamp [1,8] (132628-132637) | 2.0 (Near) | 2.0 (Near) | no |
| SpellParticleCastFilter | int, def 24, RAW (132638) | 0 (bits: self only; box shows "All spells") | 24 (All spells) | no |
| EnvironmentParticleDensity | float, def 1.0, clamp [0,1] (132639-132649) | 1.0 (High) | 1.0 (High) | no |
| EnvironmentParticleOpacity | float, def 1.0, clamp [0,1] (132650-132660) | 1.0 (100) | 1.0 (100) | no |
| EnvironmentParticleNearClipPlane | float, def 2.0, clamp [1,8] (132661-132670) | 2.0 (Near) | 2.0 (Near) | no |
| EnvironmentParticleCastFilter | int, def 24, RAW (132671) | 0 | 24 (All spells) | no |
| ActorParticleDensity | float, def 1.0, clamp [0,1] (132672-132682) | 1.0 (High) | 1.0 (High) | no |
| ActorParticleOpacity | float, def 1.0, clamp [0,1] (132683-132693) | 1.0 (100) | 1.0 (100) | no |
| ActorParticleNearClipPlane | float, def 2.0, clamp [1,8] (132694-132704) | 2.0 (Near) | 2.0 (Near) | no |
| ActorParticleCastFilter | int, def 24, RAW (132705) | 0 | 24 (All spells) | no |
| ShowSpellEffects | atol, keep 0/1 (126006-126011) | 0 (off), NO rewrite | 1 (on), REWRITTEN to file | yes: absent or <0 or >1 -> write "1" at load |
| ParticleDensity (legacy) | int, def 3, RAW (132740) | 0 | 3 | no |

The density/opacity floats clamp to [0,1]; the near-clip floats clamp to
[1, `_DAT_00641fa0`] where `_DAT_00641fa0` = 8.0 and the default `_DAT_006410b4` =
2.0 (B1165, peread). None of the particle keys is rewritten at load: the loader
(132608-132707) only stores the clamped/read value. The one load-time rewrite is
ShowSpellEffects (126011). Every particle key IS written by the client's bulk
writer `FUN_004aed0e` (132526-132553) — the full `.\eqclient.ini` save — which runs
at the world shutdown (`FUN_00491c04`, 115098-115107, called from the shutdown body
FUN_0045b8f0 at 80556 and 0x45d48b). So a junk value is normalised in the file at
the next full save, not at load; only ShowSpellEffects is written at load.

## 2. The fix (acceptance B)

Fable's eqclient read is `ParseEqClientIni` (`src/client/graphics/ui/eqclient_ini.cpp:243`)
and its per-value parse is `ParseValue` (`src/client/graphics/ui/eqclient_ini.cpp:86`).
Two changes make the readers identical per key:

* **Int keys now give 0 for junk, kept RAW.** `ParseValue` routes the int rows
  (the cast filters) through `IniAtoi` (`src/client/graphics/ui/eqclient_ini.cpp:58`),
  a faithful re-implementation of the client's 0x60e31b `atoi`: whitespace, one
  optional sign, wrapping 32-bit decimal accumulation, 0 for a value with no digit
  prefix. This replaces the old "junk keeps the default" behaviour. A well-formed
  int is unchanged (it is the same decimal the client reads).
* **ShowSpellEffects keeps 0/1, rewrites absent/out-of-range in the file at load.**
  `ParseValue` (`src/client/graphics/ui/eqclient_ini.cpp:86`) takes the value through
  `IniAtoi` (junk -> 0, kept when it is 0/1) and sets `showSpellEffectsRewrite`
  (`include/client/graphics/ui/eqclient_ini.h:71`) when the value is out of [0,1];
  the ABSENT half is set after the line scan (`src/client/graphics/ui/eqclient_ini.cpp:279`),
  because an absent key never reaches `ParseValue`. The load path acts on the flag: when
  `showSpellEffectsRewrite` is set it runs the rewrite
  (`src/client/graphics/frame/renderer_frame_preferences.cpp:84`), calling
  `RewriteShowSpellEffectsInFile` (`src/client/graphics/ui/eqclient_ini.cpp:360`), the
  file wrapper around `RewriteShowSpellEffectsInText`
  (`src/client/graphics/ui/eqclient_ini.cpp:352`) — a SINGLE-KEY update that
  appends `ShowSpellEffects=1` to the end of `[Defaults]` (or replaces the value in
  place) and leaves every other line byte-identical. That is the client's own
  single-key `WritePrivateProfileString` (126010-126011): the live oracle's
  after-file still reads `SpellParticleDensity=banana`, so the load path must NOT
  use `SaveEqClientIni` (the full save), which would rewrite every key and normalise
  the junk the client never touches at load. The full save is still the shutdown
  path, which is what DOES normalise the junk.
* **Non-finite and ERANGE floats clamp to the client's endpoints.**
  `parseSettingIniFloat` (`src/client/graphics/ui/options_settings.cpp:422`)
  parses with `std::strtof` -- the client's `sscanf %f` (0x4ae705 -> 0x610837,
  eqgame.exe.c 132182-132197) IS the C float conversion, so the reader consumes
  exactly what the client's consumes: `inf`/`nan` text, `1e400` overflowing to
  `+/-HUGE_VAL` (folded to `+/-1e30f` so the clamp can pin it), and `1e-400`
  underflowing to 0.0 -- strtof's own ERANGE fold, measured on the build host,
  the value the client's sscanf hands its clamp. libstdc++'s `std::stof`, which
  the reader used, throws `out_of_range` for BOTH the overflow and the
  underflow, and a sign-only fold of that exception sent a positive underflow
  to the UPPER endpoint (MR !334 review, note 8311); folding by magnitude --
  what strtof does on its own -- lands all of inf/-inf/nan/1e400/-1e400/1e-400
  on the bound the client's clamp (132608-132637) gives it. A junk miss is a
  no-conversion as before, and a well-formed finite float is unchanged.

Well-formed values are byte-unchanged: the B1175 capture test still pins the lane
file, and `WellFormedValuesAreByteUnchanged` (below) pins a minimal junk-free file
round-tripping identically.

## 3. Lane-5 oracle check (acceptance C)

Lane 5 (the ThinkPad: the official client under Wine + the eqemu stack). The lane's
`eqclient.ini` was edited ONCE with a JUNK value (`SpellParticleDensity=banana`) and
an ABSENT key (`ShowSpellEffects` deleted), then each client was started, left to load
(the official client also ran a clean `/q`), and the file was read back. The whole-file
diff of the official client, before -> after, is exactly ONE appended line:

```
115a116
> ShowSpellEffects=1
```

(after `ScreenshotNumber=2`, the last `[Defaults]` entry, before `[VideoMode]`). The
junk density is untouched. That is the client's single-key `WritePrivateProfileString`
(126010-126011): the absent key is written as 1 and nothing else moves.

| key | before (edited) | official client after | fable after |
|---|---|---|---|
| SpellParticleDensity | `banana` (junk) | `banana` (unchanged) | `banana` (unchanged) |
| ShowSpellEffects | (absent) | `1` (appended to `[Defaults]`) | `1` (appended to `[Defaults]`) |
| every other `[Defaults]`/`[Options]` key | well-formed | unchanged | unchanged |

Applied values (Options > Particles position, from the reader table, sec. 1): the junk
density APPLIES as 1.0 (High) in memory even though the file keeps `banana`; the absent
ShowSpellEffects APPLIES as 1 (on). Both clients apply the same values and write the
same file.

* **Official client (live):** measured. Artifacts in
  `kb-evidence/kb-b1182-ini-junk/oracle/out3/` — `eqclient-before.ini`,
  `eqclient-after.ini`, the per-step stills (`06_opt_display.png`,
  `07_opt_display_settle.png`) and `*.ini.diff`. The driver's ledger is `summary.md`.
* **Fable (the transform, unit-pinned):** `ParseEqClientIni` on the same before-file sets
  `showSpellEffectsRewrite` for the absent key, and `RewriteShowSpellEffectsInText` — the
  exact body the load path calls via `RewriteShowSpellEffectsInFile` — appends
  `ShowSpellEffects=1` after the section's last entry and leaves every other line
  byte-identical (the junk density stays `banana`). That is pinned by
  `TheLoadRewriteWritesOnlyShowSpellEffects`, so fable's after-file is the identical
  transform of the identical before-file.
  * **The LIVE fable leg is MEASURED** (2026-10-28 22:30-22:34 UTC, lane 5):
    `scripts/b1182_fable_run.sh` (in the repo) seeds the cache dir with the before-ini,
    runs fable as a PLAIN GRAPHICS run against the lane, and reads the ini back.
    **It needs no working world**: the load path (`FrameComponent::syncEqClientIni`,
    `src/client/graphics/frame/renderer_frame_preferences.cpp:70`) runs on the first
    `processFrame()` frame — which starts only when the loading progress reaches 50 %,
    i.e. zone data arriving (before that the loading screen is drawn directly,
    `src/client/application_frame.cpp:180`) — AND again on EVERY clean shutdown
    (`IrrlichtRenderer::shutdown()` calls `syncUiPreferences`). So a run that never
    connects still loads and rewrites on the way out: in graphics mode a pre-zone
    connect failure keeps the loading screen up (B1099) and SIGTERM turns it into a
    clean shutdown. (A `--scenario` run would never run the load path at all:
    Automated mode is headless — no renderer is created, `Application::initGraphics`
    only initialises in GraphicalInteractive mode.) This run could not connect
    (22:18 the loginserver answered but was not advertising the world "WillEQ Private
    Server" — the world container was not registered with the freshly started
    loginserver, an eqemu server-state issue outside this card's code; by 22:30 the
    login itself sat in `LOGIN_CONNECTING` 30 s and gave up), so it exited 3 on the
    pre-zone connect failure and the rewrite ran on the SIGTERM clean shutdown —
    the log's one `eqclient.ini loaded from` line is the shutdown's, 2 ms after
    "Shutting down application...". The after-ini is the oracle's own diff,
    `115a116 > ShowSpellEffects=1`, the junk density byte-identical. Artifacts:
    `kb-evidence/kb-b1182-ini-junk/live-leg/` (`eqclient-fable-before.ini`,
    `eqclient-fable-after.ini`, the run log).

## 4. Tests and verification (acceptance D)

Unit tests, `tests/test_options_particles.cpp` (`OptionsParticlesB1182`):
- `JunkGivesTheClientsZeroOrDefault` — per key: junk int -> 0, junk float -> the
  default, junk ShowSpellEffects -> 0 with NO rewrite.
- `AnAbsentKeyGivesTheDefault` — every key keeps its default; the absent
  ShowSpellEffects takes 1 and is marked for the file rewrite.
- `ShowSpellEffectsKeepsRewritesAndJunk` — the full value matrix (absent, 0, 1,
  00, whitespace, banana, -1, 2, 10, 12x) with the client's keep/rewrite decision.
- `TheRewriteWritesOneInTheFile` — the FULL save (shutdown path): out-of-range and
  absent both land as ShowSpellEffects=1, and a junk ShowSpellEffects is normalised
  to its kept value (0). This is SaveEqClientIni, NOT the load path.
- `TheLoadRewriteWritesOnlyShowSpellEffects` — the load path's targeted rewrite: it
  writes ONLY ShowSpellEffects and leaves the junk density byte-identical (the
  oracle's after-file: `banana` + appended `ShowSpellEffects=1`); in-range is a no-op.
- `TheIntReaderWrapsLikeTheClientsAtoi` — the wrapping atoi (junk 0, prefix,
  0x stops, sign, whitespace, mod 2^32 wrap).
- `NonFiniteFloatsClampToTheClientsEndpoints` — inf/-inf/nan/1e400 AND the
  underflow rows 1e-400/-1e-400 land on the client's clamp bounds through the
  applied knobs, one row per value, per float key (spell and environment
  density and near-clip), each the value the client's own reader + clamp
  applies (the underflow half is the finding at MR !334 note 8311).
- `WellFormedValuesAreByteUnchanged` — a junk-free file parses to the same values
  and writes back byte-identically (the B1175 slider tests stay green).

Targeted ctest (gitlab-runner, Release GLES2): `OptionsParticlesB1182` 8/8 and the
wider `Options|EqClient|UiPreferences|FrameComponent|SpellFx|SpellSlot|CameraZoneIn`
selection 51/51 at this head (29/29 at 7cc396d3; other units' tests matching the
selection landed on fable since). Hygiene: 49/49 (`rbuild hygiene`, the full `hygiene` label).
CI: green — the MR pipeline passes every auto stage (hygiene, build-test:
Release GLES2 build + full ctest, gate: visual gate across the 5 manifest
zones; the arm jobs are manual/opt-in). First full run: pipeline #2445,
success.

## 5. Hand-off

* Cite checker on this README: `checked 15 cites, 0 problems, 0 address warnings`
  (the full command with `--tree . --extra /mnt/projects/fable --dis .../eqgame.dis
  --dis .../EQGraphicsDX9.dis`; the `eqgame.exe.c` line cites are re-read by hand,
  as noted in sec. 1).
* Live fable leg (measured, one command): `scripts/b1182_fable_run.sh` — see sec. 3.
