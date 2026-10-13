# B1019 (GitLab #340) — the hotkey dispatch census, and the wave's nets + docs

Three nets, each with a RED captured this wave, plus a documentation sweep.

## AC1c — `tests/test_hotkey_dispatch_census.cpp` (NEW)

`RendererEventReceiver::OnEvent()` (renderer_event_receiver.cpp) is the
single switch that turns a `RendererAction` into a renderer call. Before
this net, an action whose receiver arm had been deleted still passed every
test — the binding table, the shipped-file test and the user-file test all
proved the key RESOLVED to the action, none proved the action DISPATCHED.

`CheckEveryBindingIsConsumed()` walks the resolved `HotkeyManager`
(defaults AND the shipped `config/hotkeys.json`, the exact load sequence the
client runs) and asserts, for every held binding, that
`RendererEventReceiver::OnEvent`'s switch has a case for its action. The
census is pinned from `held.keys` (the live dispatch queue), so the held-key
poll that moved out of renderer_player_physics.cpp is covered too.

Five tests: `EveryDefaultBindingHasADispatchConsumer`,
`EveryShippedBindingHasADispatchConsumer`, `TabCyclesTargetsAndReturnOpensChat`
(the two bindings the switch handles inline, not by fall-through),
`ARetiredActionNameInASavedFileIsIgnored`, and
`AUserFileBindingTheDefaultlessActionsStillDispatches` — the net-proof case:
a user file that binds `MusicVolumeUp`/`MusicVolumeDown` (actions with no
default binding) still dispatches, because the switch carries the arms even
though no key reaches them.

RED (arm removed — `MusicVolumeUp`'s case deleted from the switch, rebuilt,
run; `ac1c_red.log`):

```
.../tests/test_hotkey_dispatch_census.cpp:185: Failure
user file binding the default-less actions: MusicVolumeUp = M has NO dispatch consumer: the key falls through RendererEventReceiver::OnEvent()'s default
[  FAILED  ] HotkeyDispatchCensus.AUserFileBindingTheDefaultlessActionsStillDispatches (6 ms)
1 FAILED TEST
```

GREEN (arm restored, rebuilt): `1/1 Test #244 ... Passed` (`ac1c_green.log`).

## AC1a — `tests/test_vendor_music_config_warn.cpp` (B1018's WARN)

`SetVendorMusic()` logged its "parsed but ignored" WARN unconditionally on
the default `"gl.xmi"` — every WITH_AUDIO run warned. The WARN moved to the
parse site (`application_config.cpp` / `bridge_component.h`), so an ABSENT
`audio.vendor_music` key now logs nothing and a PRESENT one logs exactly one.
Two tests pin both halves. RED captured by checking out the `origin/fable`
versions of the two fix files and rebuilding (`ac1a_red.py`, `ac1a_red.log`):
`VendorMusicWarnTest.NoVendorMusicKeyNoParsedButIgnoredWarn` —
`EXPECT_EQ(CountWarns(out), 0)` at test_vendor_music_config_warn.cpp:175 —
FAILED (the ignore WARN fired on the default `"gl.xmi"`); the present-half
(`VendorMusicKeyLogsExactlyOneWarn`, 187) passed both before and after.
Restored, rebuilt, both pass (`green_run2.log` / `green_run3.log`: 8/8).

## AC1d — B950's net, tightened

`tests/test_zone_geometry_atlas_layout.cpp` now measures the CORE side
INSIDE `willeq_core` (`ZoneMeshBuilder::layoutSize()`,
zone_geometry.cpp) instead of a sibling probe TU with a hand-placed define —
a define/TU divergence is visible either way, so the PUBLIC-define argument
cannot be hollowed out by editing the test. RED (define reverted to PRIVATE,
reconfigured, rebuilt, run; `ac1d_red.log`):

```
[ RUN      ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout
.../tests/test_zone_geometry_atlas_layout.cpp:52: Failure
Expected equality of these values:
  asConsumer
    Which is: 272
  asCore
    Which is: 280
...
.../tests/test_zone_geometry_atlas_layout.cpp:64: Failure
Value of: thisTuSawTheAtlasMacro
  Actual: false
Expected: true
[  FAILED  ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout (0 ms)
```

2 × `irr::s32` = 8 bytes (280-272). GREEN: both sides 280, define visible
(`green_run2.log`: test 315 `Passed`). See `refactor/B950/README.md` for the
MR !129 "no configuration builds `willeq_core` without the atlas" correction
(note https://gitlab.goldfarm.local/willeq/willeq/-/merge_requests/129#note_4235).

## GREEN, the full net

`green_run2.log` / `green_run3.log` (run twice, the second after the doc
sweep, to prove the sweep broke nothing): 8/8 — the five census tests,
`ZoneGeometryAtlasLayout`, and both vendor-music tests.

## F10/F11 documentation sweep

B582 moved the UI-hide key F11 -> F10, but ~10 present-tense F11
hide-UI references survived in live docs/comments. All fixed this wave;
every remaining `F11` in the tree is now correct (Batch-F unit name,
oracle-netstat meaning, key-scan/RDP code mapping, or explicit history):

- `include/client/graphics/ui/ui_renderer.h:172` — `(F10 / `/hideui` hides the UI)`
- `scripts/llmbench/make_timeline_segment.py:25` — `/hideui` is the scriptable half of F10 (T301 Leg D; the official client's F10 = Toggle UI Display, B582)
- `tests/test_ui_hide_command.cpp:154,177` — the two failure messages
- `tests/test_ui_hide_command.cpp:12,20` — header, now explicit history ("B582, 2026-10-10, moved the key to the official F10")
- `tests/cmake/ui_rendering.cmake:112` — the reach-path comment
- `src/client/input/hotkey_manager.cpp:475` — the receiver arms serve a key a user binds on the Options Keys page (options_keys.cpp:57); the sliders push intents directly (renderer_frame_options.cpp:67,77)
- `refactor/B582/README.md` — "no volume key on any oracle page" -> "on the Camera, UI or Commands pages" (T466's measurement scope); the user-copy sentence now says pre-B582 copies keep their old keys until the user resets to defaults — no migration is applied and there was no user base to migrate (T464's shipped file was the only table; `loadFromFile()` replaces, never merges)
- `refactor/B950/README.md` — the "no configuration builds willeq_core without the atlas" claim corrected (see AC1d), the RED block re-pasted at the current line numbers, the GREEN section updated to the B1019 test shape
- `refactor/BOARD.md` — B339/B341/B345/B490/B500 rows in past tense with correct issue references (B490 now names `targetIntensity_` as "B339 deleted it, 2026-10-11"; B345 deleted all three ConsoleRenderer members `m_curEnd`/`m_startTime`/`m_maxEnd`, confirmed in console_renderer.h)
- `src/client/graphics/overlays/overlays_component.cpp` — the dead "for backward compatibility" line deleted
- `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` — the `B340 deleted entitiesLoaded_/expectedEntityCount_` comment restored

The F11 census after the sweep: 164 raw hits, all categorized as one of the
five classes above (the base64 contact-sheet and `docs/reference/` BONE
names are false positives excluded by extension).

Hygiene fallout of the new `layoutSize()` seam: `ZeroCallers` classifies it
in `EXEMPT_TESTS_GROUPS` (`("ZoneMeshBuilder", "layoutSize"): "observer"` —
the test's observation window on the core's macro state), so the ratchet
stays green with the new function.
