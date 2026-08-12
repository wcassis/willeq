# T79 — Four small defects that have each cost multiple units a workaround

**Slot: t3. x86 only. No Pi. NO EQEMU.**

**Territory: `scripts/refactor/check_file_sizes.sh`,
`tools/model_viewer_spell_bar.h`, `tests/CMakeLists.txt` (test-discovery
config only), `src/client/graphics/renderer_zone_merge.h` /
wherever `protectedRegions_` lives, and tests.**

Do not touch the first-person camera path (T76 owns it), `eq_commands.cpp`
(T77 just landed), or the spell reader. `refactor/deferred.md` is
append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the board entries named per
part below.

**Four independent parts. Do them in order, commit-ready at each
boundary. If one stalls, land the others and report the stall** — none of
these depends on another.

---

## Part 1 — `check_file_sizes.sh` has bitten four units

Board: `## check_file_sizes.sh has no handling for deleted files in the
diff (T67 finding, 2026-08-11)`.

Two separate defects, both confirmed by units working around them:

- **No working-directory anchor.** The script resolves paths against the
  ambient cwd, so running it as the protocol instructs
  (`bash <slot>/scripts/refactor/check_file_sizes.sh fable`) mis-resolves.
  T67, T69, T73 and T75 each independently worked around it — with
  `bash -c 'cd … && …'`, `env -C`, or `--files` with absolute paths.
  **Add a `-C`/`--repo <path>` option, or anchor to the script's own
  location**, so it works when invoked the way the protocol documents.
- **Deleted files in the diff.** A file removed in the branch appears in
  the change list and the script tries to measure it. T67 hit this
  removing `constrained_mesh_cache.{h,cpp}`. Skip deleted paths.

**Verify by running it the way the protocol tells executors to run it**,
from a slot on a task branch, and show it works without a wrapper. This
script is the file-size gate; a gate everyone works around is not a gate.

## Part 2 — `model_viewer_spell_bar.h`'s `spellsnew.eff` record size

Board: recorded by T70's entry.

`tools/model_viewer_spell_bar.h:274` has `const size_t ENTRY_SIZE = 256;`
for `spellsnew.eff`. The correct value is **268**: the file is 112,024
bytes and `112024 / 268 = 418` exactly, while `/256` gives 437.59 and
drifts.

That drift is why the surrounding code carries hacks like *"Skip entries
that look like fragments (common issue with this format)"* — the
"fragments" (`"ct 2"`, `"pell Effect"`, `"l Effect"`) are pure artifacts
of reading 12 bytes short per record.

Fix the constant. **Then remove the fragment-skipping workarounds it
motivated** and confirm the parser produces clean, whole entry names —
leaving them in place would hide a future regression the same way they hid
this one. Report the entry count before and after.

Note the companion EDD parser at `:129-135` is **already correct**
(416-byte entries, header handled) — do not "fix" it.

## Part 3 — `protectedRegions_` is permanently all-false

Board: `## protectedRegions_ bitvector is permanently all-false in
production (T67 side-finding, 2026-08-11)`.

Its only writer was gated on `ConstrainedMeshCache`, which T67 removed. It
is now a bitvector that is allocated, passed around, and always false.

**Establish what reads it before deciding.** If every consumer is now
dead, remove it with them. If something live still consults it, then it is
silently answering "nothing is protected" to a question that used to have
a real answer — say what that changes, and whether the consumer needs a
different signal or should also go.

Rendering must be byte-identical either way: the visual gate is the proof.

## Part 4 — `gtest_discover_tests` timeout under load

Not yet on the board; add it.

`cmake --build` failed three times today with
`GoogleTestAddTests.cmake: Error running test executable` while
discovering tests — `test_spell_manager` and `test_graphics` — each time
under parallel load, each time clean on retry. It is a **discovery-time
timeout, not a compile error**, and it makes builds spuriously red.

Raise the discovery timeout (CMake's `DISCOVERY_TIMEOUT` on
`gtest_discover_tests`), or move the affected targets to `PRE_TEST`
discovery mode so enumeration happens at test time rather than build time.
Pick one, say why, and note the trade-off.

A spuriously-failing build teaches people to re-run without reading, which
is how a real failure gets missed.

---

## STOP conditions

- Part 3's consumers turning out to need a real protection signal — report
  rather than inventing one.
- Any part requiring changes outside this unit's territory.
- Goldens or the visual gate moving. Parts 1, 2 and 4 cannot affect
  rendering at all; Part 3 must not.

## Verification

Full protocol bar. Additionally, per part: Part 1 run the documented way
without a wrapper; Part 2's before/after entry counts with the workarounds
removed; Part 3's consumer census plus visual gate; Part 4 demonstrated
under parallel load if you can provoke it, and stated honestly if you
cannot reproduce on demand.

## Review

(executor fills: per-part outcome; Part 1's invocation proof; Part 2's
entry counts and which workarounds were removed; Part 3's census and
decision; Part 4's mechanism chosen and trade-off; deviations)

Board: re-title the `check_file_sizes.sh` and `protectedRegions_` entries;
record Part 2 against T70's entry; add a new entry for Part 4 with the
mechanism chosen.
