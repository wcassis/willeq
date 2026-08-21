# T165 — Split `tests/test_command_processor.cpp` below the size ceiling

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi** (the Pi is offline as of
2026-08-21). Orchestrator executed this one directly, as with T164.

**Territory: `tests/test_command_processor*.cpp`,
`tests/command_processor_test_fixture.h`, `tests/CMakeLists.txt`,
`refactor/`. No production source touched at all.**

## Why

`tests/test_command_processor.cpp` was **3168 lines**, past
`scripts/refactor/check_file_sizes.sh`'s **3000-line FAIL ceiling**, and the
ceiling had begun deflecting real work rather than merely flagging it:

- **T153** could not put its `/appearance` cases in the natural file and
  opened `test_appearance_command.cpp` instead, saying so in its header
  comment and again in `tests/CMakeLists.txt`.
- **T156** did the same for `test_skill_reachability.cpp`.
- Every remaining action-layer unit on the zero-caller-sender arc hits it
  next.

Two units in a row routing around a ceiling is the ceiling doing harm. Same
shape as the `renderer_zone_load.cpp` problem, which is still open.

## What was done

**One header, five .cpp files, still ONE binary.**

`tests/command_processor_test_fixture.h` holds the two fakes that every case
needs — `RecordingOutputRenderer` and the `CommandProcessorTest` fixture —
moved verbatim out of the old file's anonymous namespace.

The cases were cut at existing section banners into five files, split by
family so that a unit adding a command knows which file to open:

| file | lines | tests | contents |
|---|---|---|---|
| `test_command_processor.cpp` | 656 | 49 | core mechanics: dispatch, registration, categories, completion, argument parsing, case, `/loot`, renderer echo, `cmdHail`, `cmdNpcState` |
| `test_command_processor_migration.cpp` | 633 | 56 | T98 survivor enumeration, Group (T99), Spells/Pet (T101), Chat (T100) |
| `test_command_processor_combat_guild.cpp` | 723 | 93 | Combat + Social (T102), Guild (T132) |
| `test_command_processor_world.cpp` | 654 | 81 | `/clickobject` (T144), split/save/bandage/rewind/bug/petition, vendor family, final migration rows |
| `test_command_processor_senders.cpp` | 547 | 52 | Social/Find (T150), the four recipe senders (T149) |

All five are sources of the **single** `test_command_processor` target. The
3000-line rule is per FILE, so splitting the file satisfies it without adding
four more ~80MB binaries and four more ctest discovery passes — which on this
NFS checkout is exactly the cost T163 measured and removed.

## Two things worth knowing before touching this again

1. **The fixture is in a NAMED namespace (`eqt::tests`), not an anonymous
   one, and that is load-bearing.** GTest requires every test in a suite to
   share one fixture TYPE. An anonymous-namespace fixture in a shared header
   gives each translation unit its own distinct type, and
   `TEST_F(CommandProcessorTest, ...)` spread across five files would abort at
   startup with *"All tests in the same test suite must use the same test
   fixture class"*. The header says so in place.
2. **The header must include `<gtest/gtest.h>` itself.** The first build
   failed on `'::testing' has not been declared` because the .cpp files
   included gtest *after* the fixture header. Caught by the compiler, not by
   review.

## Verification

- **Content preservation proven mechanically, not by eye.** The five files'
  bodies were re-concatenated and compared against the original's lines
  83–3167: **byte-identical**. The only new text is the per-file banner
  comments, the shared preamble, and the header.
- `TEST_F` count **331 before, 331 after** (49+56+93+81+52).
- Binary runs **331 tests from 1 test suite, all PASSED** — one suite, which
  is the direct evidence that the named-namespace fixture is shared correctly.
- Full build clean; `ctest` **2595/2596**, sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`.
- Registered total cross-checked at **2596** via `ctest --show-only=json-v1`
  (T163's silent-shrink hazard) — unchanged, as it must be for a pure split.
- `check_file_sizes.sh fable`: all six changed/added files **ok**, none above
  the 1500-line WARN line.

## Not done, deliberately

- `test_appearance_command.cpp` and `test_skill_reachability.cpp` were left as
  separate targets. The ceiling is no longer the reason they exist, and their
  CMake comments now say that, but merging them back buys nothing and would
  enlarge this diff for no gain.
- `src/client/graphics/renderer_zone_load.cpp` (exactly 3000 lines) has the
  same problem and is still unclaimed. It is production code, so it needs its
  own unit with a real behaviour-preservation check
  (`scripts/refactor/verify_move_only.py`), not this unit's shape.
