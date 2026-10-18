# B1053 (#373): Options window geometry investigation

## Measurement

The stock Titanium source is
`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_OptionsWindow.xml`.
`include/client/graphics/ui/options_layout.h` quotes its 400x452 window and
387x402 pages. The table audit is
`/home/user/kb-evidence/kb-b1053-options-window/measure_layout.py`;
its saved output is `measurement.txt` in the same directory.

At the starting head `64e8cb118aec3f60c716cac6491802fee9118aa4`, that audit
found 130 table rows (`measurement.txt:1`), zero controls beyond the page or
window at 1280x720 and 1024x768 (`measurement.txt:5-6`), and zero
page-membership mismatches (same lines). It compared all 113 rows with
literal XML `<Location>` and `<Size>` against the source and found zero rect
mismatches (`measurement.txt:4`). The one XML page piece omitted from the table is
`ODP_SetFullscreenButton`: `options_layout.h` explains that it shares a rect
with the included `ODP_SetWindowedButton` and neither mode switch is available
on the supported rendering paths (`options_layout.h:203-206`,
`measurement.txt:2-3`).

The existing `OptionsLayoutT434.EveryControlIsInsideItsPage` test already walks
every table row at 800x600. The new `OptionsLayout.NoControlOutsideItsPage`
also checks every row against both page and window at the two requested
resolutions. `ui_panel_capture` now accepts `--size WxH --panel <name>` to
capture the actual renderer at either resolution.

The real GLES2 `ui_panel_capture` rendered all Options tabs at both sizes from
this worktree's freshly built binary. The stills are under
`/home/user/kb-evidence/kb-b1053-options-window/before-1280x720/` and
`before-1024x768/`, named `options_<tab>_<size>.png`. The General, Display,
Mouse, Keys, Filters, Colors and Mail stills were inspected at both sizes.
Those stills show panel content within the window.
`ctest --test-dir build -R '^OptionsLayout' --output-on-failure`
passed all layout tests (including the new one) against that build;
`ctest --test-dir build/hygiene -L hygiene --output-on-failure` passed as well.

## XU4 constrained DRM check

The XU4 run used the current fable publish's `bin/willeq` copied to a
temporary setcap path, with `--drm --renderer gles2 --constrained orangepi
-r 1280 720`. Its binary SHA-256 is recorded in
`xu4-retry/binary.sha256` under the evidence directory. The uinput driver
sent Alt+O after zone entry (`xu4-retry/keys.tsv:2`), and the Options
window appears in `xu4-retry/frames/frame_000010.png` (captured after
that key event). The General page shows its last checkbox, Auto Consent
Guild, within the lower window edge; the right column's Load UI Skin
button is also within the page. Its placement and control list match
`before-1280x720/options_general_1280x720.png` from x86. No XU4-only
overflow was reproduced in this page. The temporary client and uinput
processes ended and the device lock was released.

The first device attempt did not send Alt+O: `xu4/keys.tsv:2` records that
the helper's expected `[memledger] point=ready` line was absent, although
`xu4/frames/frame_000039.png` shows the character zoned in. It is retained
as a failed capture attempt, not counted as Options-window evidence.

A second, independent XU4 capture for the fix leg (2026-10-14) used the
same publish and flags, `--dump-frames-dir /tmp/b1053 --dump-every 30
--dump-max-frames 40` (the orchestrator's 17:25Z command). Alt+O was sent
`xu4-fix/out/keys.tsv:2` 3 s after zone entry; the Options window (General
page) is open in `xu4-fix/out/frames/frame_000004.png` through
`frame_000009.png`, and `/q` ended the run at `keys.tsv:3`. Compared with
`before-1280x720/options_general_1280x720.png` (x86, same 1280x720 render):
the window sits at the same position, the tab row and both control columns
are the same set of controls, and no control is clipped at the lower or
right edge on either platform. (The two stills' 39.4 % pixel difference over
the 600x580 window crop is the 3D zone background and chat/tip windows, not
the Options panel; the panel regions match side by side in
`xu4-fix/zoom_right_sidebyside.png`.) Finding for both platforms: NOT
REPRODUCED.

MR !174 pipeline 1159 passed at head
`5b75317646059a9a46151ab9979346ba520b594a` (hygiene, build-headless,
build-test and gate success; the arm/ledger/prune jobs are manual by
design). The full pipeline record is in
`/home/user/kb-evidence/kb-b1053-options-window/ci-final.json`. This line
names the head the QA leg graded; if a later commit moves the MR head, this
citation and a new MR-head pipeline are required.

## Acceptance (contract reconciled 2026-10-13 18:00Z)

The pre-reconciliation "before/after stills" row was replaced by two checks:

- `stills-two-resolutions-reported`: MET — the x86 GLES2 stills at 1280x720
  and 1024x768 are under `before-1280x720/` and `before-1024x768/` in the
  evidence directory, plus the XU4 constrained-DRM still from the "XU4
  constrained DRM check" section.
- `overflow-finding-reported`: MET — the overflow list is EMPTY (zero
  controls outside their page or window at 1280x720 and 1024x768,
  `measurement.txt:5-6`), so the finding is NOT REPRODUCED. NOT REPRODUCED
  is a reportable outcome of this check.

## Close-out (2026-10-18): all tabs on the XU4 beside the official client

This leg exercises **every** Options tab on the XU4 (Will's Orange Pi, the
`--constrained orangepi` play box) at **1280x720** and **1024x768**, beside
the official Titanium client's same tab, and runs the mechanical clip check
on every frame. It supersedes the "Not verified / left open" note above (only
the General tab was previously exercised on the XU4).

### Official-client stills (lane 5, ThinkPad)

The B1048 driver (`scripts/eqemu/oracle/oracle_session.sh`) ran the official
client on lane 5, which is a 1280x1024 Xorg rig; the stills are a 1280x800
crop at +0+30 (`oracle_remote.sh:44`). The Options window is a fixed 400x452
window (the XML's size, quoted in `options_layout.h`), so the rig's screen
size does not change the window's geometry or its control layout -- the stills
are the official client's Options window at its own native placement (measured
crop origin (410,349), root (410,380); `oracle-tabs/summary.md`). One settled
still per tab: `oracle-tabs/05` General, `07` Display, `09` Mouse, `11` Keys,
`13` Filters, `15` Colors, `17` Mail.

### fable frames (XU4, uinput-driven)

`b1053_drive.py` (evidence dir) unbound the physical USB HIDs, created
uinput keyboard+mouse devices the published by-branch/fable binary grabbed
(`CIrrDeviceFB::initEvdev` takes the first KEY_A + REL_X devices), gated on
`[memledger] point=ready`, dismissed the Tip-of-the-Day (3x ESC), sent Alt+O
(`ToggleOptions`, Global hotkey), clicked all 7 tabs in turn (REL mouse
deltas, 2 s hold each), and `/q`'d; teardown rebinds the USB HIDs. Frame
dumps every rendered frame. The client log shows exactly one
"Options window opened" per run. Verified tab identity: a 7-row tab-strip
montage per resolution, vision-checked (correct active tab in every row).

Named frames (evidence dir): `xu4-1280/xu4_720_<Tab>.png` and
`xu4-1024/xu4_768_<Tab>.png` for all seven tabs; `side-by-side/manifest.txt`
maps each to its source frame. Side-by-sides (official left, fable right,
window crops): `side-by-side/<Tab>_720.png` and `<Tab>_768.png`.

### Per-tab table (both resolutions)

| tab | controls (layout == XML) | 1280x720 frame | 1024x768 frame | clip check | verdict |
|-----|--------------------------|----------------|----------------|------------|---------|
| General | 39 | xu4_720_General.png | xu4_768_General.png | PASS | OK |
| Display | 63 | xu4_720_Display.png | xu4_768_Display.png | PASS | OK |
| Mouse | 8 | xu4_720_Mouse.png | xu4_768_Mouse.png | PASS | OK |
| Keys | 4 | xu4_720_Keys.png | xu4_768_Keys.png | PASS | OK |
| Filters | 2 | xu4_720_Filters.png | xu4_768_Filters.png | PASS | OK |
| Colors | 3 | xu4_720_Colors.png | xu4_768_Colors.png | PASS | OK |
| Mail | 11 | xu4_720_Mail.png | xu4_768_Mail.png | PASS | OK |

Control counts are per `options_layout.h`; the 130-row audit against
`EQUI_OptionsWindow.xml` found zero rect mismatches (the table IS the XML),
so the per-tab counts are identical in both clients. The listbox pages
(Keys/Filters/Colors) show their full list plus scrollbar on both clients.
Display's 63 = 39 direct page pieces (the XML's own `OptionsDisplayPage`
census) + 24 nested particle-sub-page pieces (9 Spell + 7 Environment +
9 Actor) inside `ODP_ParticlesTabs` — the same split the XML defines, so
the oracle's tab carries the identical 63.

### Mechanical check (refactor/B1053/check_frame.py)

Per frame: (1) frame is the screen size and the window rect is fully on it;
(2) the window's bottom 8px band is uniform panel (no control reaches the
window's bottom edge on any tab, so a non-uniform band is 3D scene bleed =
clipped); (3) every control rect from `emit_rects.py` is fully on the frame;
(4) the bottom-right 32x32 corner is non-blank panel. All **14 frames pass**
(7 tabs x 2 resolutions), exit 0 each. Measured: at 1280x720 (window
440,134) the bottom band is mean 31.2 std 10.9 on six tabs and 37.9 std
15.4 on Keys (the list's bottom rows reach lower, still inside the page);
corner 32x32 mean 20.8-21.3. At 1024x768 (window 312,158) bottom band mean
27.9-28.0 std 10.8 (35.6 std 16.2 on Keys), corner mean 18.4-19.1. All
well inside the std 40 / blank-5 bounds. The right edge band is
deliberately not a uniformity rule: the listbox tabs' scrollbar + list text
edge live there legitimately (measured: bright rows only on those tabs,
inside the Listbox y-span).

Tab identity is checked mechanically, not only by eye: the seven named
frames per resolution are pairwise distinct in their window crop (smallest
mean abs diff 4.20-5.36, the sparse Mouse/Mail pair; the largest is the
General/Colors 15.75-17.91 pair, the two most different pages). A failed
tab click would make two crops near-identical; none is (script: the
pairwise-window-crop comparison over the evidence dir's named frames).

### Verdict

No control is missing, clipped, or cut off on any tab at either resolution,
on the XU4 constrained-DRM render or the official client. **CLOSE #373: NOT
REPRODUCED.** The General-tab-only evidence from MR !174 now extends to all
seven tabs at both resolutions.

