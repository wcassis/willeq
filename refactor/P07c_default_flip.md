# P07c — Flip orangepi preset to area+floor; align harness with production defaults

Approved by Will 2026-07-24 ("go ahead with the flip") after P07b passed all
acceptance criteria (refactor/P07_pp_hybrid_floor.md review: anomaly poses
tie count, city -19 to -24%, whole-run -11.3%, geometry byte-identical).
Slot: wt/slot1, branch task/P07c-default-flip.

## Scope

1. **Preset flip (the production change):** in
   `src/client/graphics/constrained_renderer_config.cpp`, OrangePi preset
   block (~line 257-307, where `adaptiveLightingZoneBudget = 20` is set):
   set `adaptiveLightingBudgetMode = AdaptiveLightingBudgetMode::Area`.
   Leave `adaptiveLightingAreaBudgetFactor` (0.75) and
   `adaptiveLightingAreaPPFloor` (1) at their compiled defaults — do not
   restate them in the preset unless a comment aids clarity. Other presets
   unchanged (count remains the compiled-in default for non-orangepi).
2. **Harness alignment:** neither `scripts/pi/bench_pi.sh` (tier-2
   baseline) nor `scripts/ci/visual_gate.sh` currently passes adaptive-
   lighting flags — they measure/render unconditional full per-pixel,
   which no longer matches production. Add the production-equivalent
   flags to both:
   `--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode area
   --pp-area-factor 0.75 --pp-area-floor 1`
   (plus the entity budget default if the scripts don't already imply it —
   check what --adaptive-lighting alone defaults for perpixel-budget and
   keep production parity).
3. **Golden regeneration + contact sheet:** the gate config change will
   legitimately alter golden pixels wherever distant regions drop to
   lightweight. Regenerate goldens on llvmpipe, update
   `tests/goldens/manifest.json` (note: approval stamp must be REFRESHED
   by Will — mark the manifest entry "pending re-bless" with today's
   date; the orchestrator will present the contact sheet and record
   Will's approval post-merge). Produce an old-vs-new contact sheet for
   every golden view that changed (montage per zone, PNG) under
   `benchmarks/contact_sheets/p07c_flip/`.
4. **Pi tier-2 baseline re-capture** with the new harness config
   (P02 protocol): rerun the golden-zone baseline via scripts/pi/, commit
   the new CSVs + updated `benchmarks/pi_baseline/baseline.json`.
   Expect improvement vs the H04 numbers (which were full-PP);
   record old-vs-new per zone in the review. Keep the H04 CSVs in place
   (they are the historical reference).
5. **docs/decisions.md**: append a 2026-07-24 entry: production zone
   lighting budget is area+floor1 on orangepi (P05→P07b chain, numbers,
   count mode retained as config fallback).

## Verification

- Build + ctest (known host exceptions) + x86 GLES2 build + ARM build +
  file-size check.
- Visual gate must PASS against the REGENERATED goldens (and fail against
  the old ones for at least one changed view — negative check, H02b
  precedent).
- Tier-2 re-capture on the Pi under P02 protocol (Pi is up; if it goes
  unreachable, report blocked — do not improvise).
- git add -f for anything under benchmarks/ (gitignored).

## Out of scope

- Any further lighting tuning (k, floor, budgets) — config-only from here.
- The camera-path record/replay unit (separate, queued).

## Review

(executor fills in)
