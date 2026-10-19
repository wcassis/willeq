# B1002: per-window opacity (the measured value is 255)

Issue #322. Resolves B992's finding 1 (the uniform alpha-180 panel) by
measurement: the official client's `Style_Transparent` windows composite at
**255 — fully opaque** — so every fable window now draws at 255.

## The flag table (the issue's deliverable)

Measured from the official client's own UI definitions on the oracle package
(`uifiles/default_old/`, the same 120 `EQUI_*.xml` files the running client
renders). `grep -l "Style_Transparent>true"` gives 34 of 120; the table is
ported per window, not guessed:

- `include/client/graphics/ui/official_window_flags.tsv` — one row per window
  fable renders (the 29 fable windows, 34 of the 120 flagged translucent in
  the official client), `official_name,transparent`.
- `scripts/ui_flags/gen_window_flags.py` — regenerates the header from the TSV.
- `include/client/graphics/ui/window_flags.h` — `windowIsTranslucent(w)` and
  `windowPanelAlpha(w)`; pinned by `tests/test_ui_window_flags.cpp`
  (`TheSnapshotCarriesTheOfficialCensus`, `EveryFableWindowNamesAnOfficialWindow
  WithItsFlag`, `ThePanelAlphaIsOneOfTheTwoWindowKinds`).

## The alpha measurement (the issue's open decision)

The issue asks for the blend factor as a render-measurement, with a BLESS
ruling on which of the 34 translucent windows fable should render translucent
and at what alpha. The capture plan is in
`scripts/eqemu/oracle/plans/b1002_alpha*.tsv` (v4: two-backdrop per-pixel
solve over the chat window — the only fable window the oracle shows with
`Style_Transparent=true` visible; v3's closed-frame approach failed because
the chat cannot be hidden and the night ground gave no signal).

The oracle capture (lane 5, ThinkPad, `oracle-alpha4/` in the evidence
directory) takes two frames with the window open over backdrop B1 (camera 1)
and two over B2 (camera 2, a time-of-day change, not a camera turn — v4's
finding), then solves per pixel:

    O[p] = a*f + (1-a)*B[p]    =>    a = 1 - (O2-O1)/(B2-B1)

inside the window rect (located by `locate2.py`: the pixels that differ from
the closed frame in BOTH open frames). The solver (`solve_alpha.py`, evidence
directory) and its output (`solve_alpha_rerun.out`):

    solve pixels (|B2-B1| max-channel > 20 inside window): 19867
    ch0: weighted a= 255.00/255  median= 255.00  p5= 255.0 p95= 255.0  n=19867
    ch1: weighted a= 255.00/255  median= 255.00  p5= 255.0 p95= 255.0  n=19867
    ch2: weighted a= 255.00/255  median= 255.00  p5= 255.0 p95= 255.0  n=19867
    fraction of solve pixels with any channel a<0.98 (translucent): 0.00%

**a = 255/255 over 19,867 pixels, 0.00% translucent.** The `Style_Transparent`
flag is carried by the window, but the client composites it fully opaque with
the stock rock-fill textures (`wnd_bg_dark_rock.tga` / `wnd_bg_light_rock.tga`
are 255/255 with 0 sub-opaque pixels, measured in the issue). The ini census
on the oracle host (`ini_alpha_census.py` / `.out`) confirms: 29 per-character
UI inis, 824 `Alpha=` keys, **0 non-255 values** — no user or zone overrides
the fill either.

## What changed

- `window_flags.h`: `kTranslucentPanelAlpha = 255` (was 180, B992's uniform).
  A `translucentPanelAlphaOverride` test seam (0 = the measured value) exists
  so the genuinely-translucent blend path stays exercised in the pin test.
- `ui_renderer.cpp`: `drawPanelFor(rect, window)` — the panel body is the
  atlas `white` sprite tinted to the panel colour `{10,10,15}` at
  `windowPanelAlpha(w)` (a tint multiplies the sprite's baked alpha, so the
  pack's alpha-180 `panel_background` cannot be tinted up to 255; the pack is
  untouched). Every static window routes through it (17 call sites across
  `static_panels.cpp` and the per-window panel TUs).
- `tests/test_ui_panel_z_order.cpp`:
  - **`TranslucentBuffBarOverTheOpaqueBag`** — the issue's re-based case,
    named. The buff bar (`EQUI_BuffWindow`, one of the 34) over a bag
    (`EQUI_Container`, one of the 86). Leg 1 at the measured 255: the lower
    window's content is **absent** in the region covered (0 pixels over
    `kMaxThrough=96`; the residual 67 is the buff decal's own translucency).
    Leg 2 at 180 (the override seam): the lower content is **present** at the
    blend (maxDiff 103, 34 pixels over the 50 tolerance). Negative control:
    the bag alone must show content in the overlap or the geometry cannot
    detect the bleed.
  - `kMaxThrough` re-explained: at 255 the lower content reaches 0; the bound
    is only headroom for the upper window's OWN text anti-aliasing (measured
    maxima 85 and 26, bound 96). The stale "kMaxThrough is 0 / 30 windows"
    comments are corrected (29 fable windows).
  - Fallback draw-cost pins re-taken: the body is now one opaque solid run at
    the flush instead of a 180-blend run; atlas pins unchanged.
- `tests/test_static_ui_frame_cost.cpp`: +4 commands every frame (221/118) —
  the spell gem window went from a bare `drawSprite(PanelBackground)` (no
  border) to `drawPanelFor` (fill + 4-edge frame).

## Verification

- `UiPanelZOrderTest` (6/6) and `WindowFlags` (3/3) pass at the tip; the new
  case FAILS with the pre-B1002 value 180 (the lower content is present where
  leg 1 requires it absent) — see `negative_run.log` in the evidence
  directory.
- Full suite + hygiene + visual gate: recorded in the evidence directory
  (`full_suite.log`, `hygiene.log`, `visual_gate.log`).

## BLESS NEEDED

**None.** The measurement answers the issue's open decision: the official
client's `Style_Transparent` windows composite at 255, so fable's uniform 180
is wrong for ALL 29 windows (including the 5 the issue called out as
opaque-flagged) and every window is correct at 255. No window needs a
per-window alpha, and the flag table stays for the record and for any window
fable adds later.

## Not verified

No device (Pi) run: the alpha is a per-window UI property, identical on both
platforms (the UI2D shader's tint multiply is the same path), and the pin
tests run on x86 llvmpipe. A rendered Pi confirmation is not in scope; if
Will wants one, it is a follow-up capture, not a blocker.
