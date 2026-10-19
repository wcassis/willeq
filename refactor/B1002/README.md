# B1002: per-window opacity (the measured value is 255)

Issue #322. Resolves B992's finding 1 (the uniform alpha-180 panel) by
measurement: the official client's `Style_Transparent` windows composite at
**255 — fully opaque** — so every fable window now draws at 255.

## The flag table (the issue's deliverable)

Measured from the official client's own UI definitions on the oracle package
(`uifiles/default_old/`, the same 120 `EQUI_*.xml` files the running client
renders). `grep -l "Style_Transparent>true"` gives 34 of 120; the table is
ported per window, not guessed:

- `include/client/graphics/ui/official_window_flags.tsv` — one row per official
  window (all 120 `EQUI_*.xml`), 34 translucent, 86 opaque.
- `scripts/ui_flags/gen_window_flags.py` — regenerates the header from the TSV.
- `include/client/graphics/ui/window_flags.h` — `windowIsTranslucent(w)` and
  `windowPanelAlpha(w)`; pinned by `tests/test_ui_window_flags.cpp`
  (`TheSnapshotCarriesTheOfficialCensus`, `EveryFableWindowNamesAnOfficialWindow
  WithItsFlag`, `ThePanelAlphaIsOneOfTheTwoWindowKinds`).

Fable renders 29 windows (the 29 rows of `kFableWindowFlags`, 28 unique
official names — `EQUI_PlayerWindow` maps two of them), 6 of them
translucent-flagged. The other 92 of the 120 official windows are **not
implemented in fable** (the header's accounting side):

EQUI_AAWindow, EQUI_ActionsWindow, EQUI_AdvancedDisplayOptionsWnd,
EQUI_AdventureLeaderboardWnd, EQUI_AdventureRequestWnd, EQUI_AdventureStatsWnd,
EQUI_AlarmWnd, EQUI_AltStorageWnd, EQUI_Animations,
EQUI_AudioTriggersWindow, EQUI_BandolierWnd, EQUI_BarterMerchantWnd,
EQUI_BarterSearchWnd, EQUI_BarterWnd, EQUI_BazaarSearchWnd, EQUI_BazaarWnd,
EQUI_Beta, EQUI_BigBankWnd, EQUI_BodyTintWnd, EQUI_BreathWindow,
EQUI_BugReportWnd, EQUI_CharacterCreate, EQUI_CharacterSelect,
EQUI_ColorPickerWnd, EQUI_CombatSkillsSelectWindow, EQUI_CompassWnd,
EQUI_CursorAttachment, EQUI_DynamicZoneWnd, EQUI_EditLabelWnd, EQUI_EQMainWnd,
EQUI_FacePick, EQUI_FeedbackWnd, EQUI_FileSelectionWnd, EQUI_FindLocationWnd,
EQUI_GemsGameWnd, EQUI_GMAttentionTextWnd, EQUI_GroupSearchWnd,
EQUI_GuildBankWnd, EQUI_GuildManagementWnd, EQUI_GuildTributeMasterWnd,
EQUI_HelpWnd, EQUI_InspectWnd, EQUI_ItemExpTransferWnd, EQUI_JournalCatWnd,
EQUI_JournalNPCWnd, EQUI_JournalTextWnd, EQUI_LargeDialogWnd,
EQUI_LeadershipWnd, EQUI_LFGuildWnd, EQUI_LoadskinWnd,
EQUI_MailAddressBookWindow, EQUI_MailCompositionWindow, EQUI_MailWindow,
EQUI_MapToolbarWnd, EQUI_MapViewWnd, EQUI_MeleeBuffWindow, EQUI_MIZoneSelectWnd,
EQUI_MusicPlayerWnd, EQUI_NewUserWalkthroughWnd, EQUI_NoteWindow,
EQUI_PlayerNotesWindow, EQUI_PointMerchantWnd, EQUI_PotionBeltWnd,
EQUI_ProgressionSelectionWnd, EQUI_PvPLeaderBoardWnd, EQUI_PvPStatsWnd,
EQUI_RaidOptionsWindow, EQUI_RaidWindow, EQUI_RespawnWnd, EQUI_Screens,
EQUI_SelectorWnd, EQUI_ServerListWnd, EQUI_ShortDurationBuffWindow,
EQUI_SkillsSelectWindow, EQUI_SocialEditWnd, EQUI_SpellBookWnd, EQUI_StoryWnd,
EQUI_SystemInfoWnd, EQUI_TargetOfTargetWindow, EQUI_TaskSelectWnd,
EQUI_TaskTemplateSelectWnd, EQUI_TaskWnd, EQUI_Templates, EQUI_TextEntryWnd,
EQUI_TextMessageWindow, EQUI_TicketCommentWindow, EQUI_TicketWindow,
EQUI_TitleWnd, EQUI_TributeBenefitWnd, EQUI_TributeMasterWnd,
EQUI_VideoModesWnd, EQUI_VoiceMacroWnd


## The alpha measurement (the issue's open decision)

The issue asks for the blend factor as a render-measurement, with a BLESS
ruling on which of the 34 translucent windows fable should render translucent
and at what alpha. Capture plans: `scripts/eqemu/oracle/plans/b1002_alpha*.tsv`.
The v4 inventory capture (`oracle-alpha4/` in the evidence directory) is the
decisive one: the **inventory window** (`EQUI_Inventory`, one of the 34
`Style_Transparent=true` windows) is opened and closed over two DIFFERENT
backdrops (camera turns), then solved per pixel. Two backdrops are required
because a single frame cannot separate the panel's own colour `f` from its
blend factor `a`; with `O1 = a*f + (1-a)*B1` and `O2 = a*f + (1-a)*B2`
(subtracting eliminates `f`):

    a = 1 - (O2-O1)/(B2-B1)

inside the window rect (located by `locate2.py`: the pixels that differ from
the closed frame in BOTH open frames). Backdrop check on the frames:
`|B2-B1|` inside the rect has 19,867 pixels over delta 20 (p95=112) — a real
backdrop change, not a uniform field; the open frame shows 148,184 window
pixels. The solver (`scripts/ui_flags/solve_alpha.py`, committed) and its
output (`solve_alpha_rerun.out`):

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
