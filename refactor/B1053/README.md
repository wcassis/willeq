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

MR !174 pipeline 1017 passed at head
`929aa6dcfe14bc1bc7b927231f2eb9d8f22716c0` (including hygiene and
build-test) before this documentation-only update. A new MR-head pipeline
is needed for the final documentation commit.

## Not verified / left open

- The reported XU4 Options-window overflow was not reproduced in the
  General-page still on constrained DRM, the measured control rectangles,
  or the x86 GLES2 stills. Other XU4 tabs were not exercised with uinput;
  a page-specific or text-rendering defect may remain. No layout coordinates
  or page assignments were changed without a failing measurement. The
  original XU4 still or a precise tab/control description is still needed.
- There is no after-fix still because no layout fix has been made. The
  before/after acceptance row therefore remains open.
- MR CI is pending.
