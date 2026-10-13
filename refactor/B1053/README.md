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

## Not verified / left open

- The reported XU4 Options-window overflow has not been reproduced by the
  measured control rectangles or the x86 GLES2 stills. A device-specific
  visual or text-rendering defect may remain; no layout coordinates or page
  assignments have been changed without a failing measurement. The original
  XU4 still or a reproducible control/tab/resolution description is needed.
- There is no after-fix still because no layout fix has been made. The
  before/after acceptance row therefore remains open.
- MR CI is pending.
