# B1202 -- CARD MANIFEST VOCABULARY: labels + `## Manifest` block, derivation rules, linter, templates

Issue #521 (class B, owner session). Step 2 of 4 of Will's 2026-10-29 deterministic
allocation stack (step 1 = B1201 #520, MR !350; step 4 = the B1204 allocator, which consumes
this). Tool build: python3 stdlib only, no C++, no labels created, no card touched.

## What landed

| file | what |
|---|---|
| `docs/hermes/card_manifest.md` | the spec: where the manifest lives (body block + labels), the six fields, the needs table, the derivation rules, the est defaults, how to check |
| `scripts/hermes/manifest/manifest.py` | the single implementation: `parse()` (`scripts/hermes/manifest/manifest.py:134`), `derived_tier()` (`:156`), `derived_model()` (`:167`), `consistency()` (`:177`), `labels_for()` (`:190`), `label_mismatch()` (`:198`), `from_labels()` (`:210`); vocabularies at the top (`NEEDS`, `scripts/hermes/manifest/manifest.py:29`; `EST_DEFAULTS`, `:40`) |
| `scripts/hermes/manifest/lint_manifest.py` | the checker: a body file, stdin, or `--issue <N>` (block AND labels); `--self-test` (15 fixture cases, no network) |
| `scripts/hermes/manifest/templates/ENV_BLOCK_dev.md`, `ENV_BLOCK_review.md` | the live hermes-ext `~/kb-bodies/` files as of 2026-10-29 (md5 `897e5aab190e7c666bf3c9f0f0e62068` / `20eddc7a395ded0ced1d4257c520eb99` before the edit) plus ONE new bullet each: the `phase:` signal (B1201) and the manifest rule |
| `scripts/hermes/manifest/templates/MANIFEST_{fix,diagnose,review,tool}.md` | manifest stubs per leg, each lints clean |

## Design decisions

- **Labels**: `leg::`, `tier::`, `model::` are SCOPED (GitLab keeps one label per scope, which is the rule: one value each). Needs are UNSCOPED `need:<name>` labels, because a card can carry several needs and scoped labels of one scope replace each other. `need:lane-<id>` instead of `lane::5`: the project already has unrelated scoped `lane::pi/vm/oracle/offline/mac` labels.
- **Counts only in the block** (`thinkpad-render:2`): a label cannot carry a number. A card with labels but no block reads as count 1 per need (`from_labels()`), and the allocator flags it.
- **The linter checks "declared >= derived"**, never "declared == derived": an author may ask for more (a heavier tier or a stronger model) but never less than the rules give.
- **est defaults** are the B1201 ledger medians per leg type (`EST_DEFAULTS` comments; the source table is `refactor/B1201/tables/by_leg_type.tsv` in MR !350, branch `task/kb-b1201-inference-ledger`): review 72 turns / 41 min (2431 s), fix 62 / 42 (2504 s), diagnose 262 / 192 (11520 s), infra and tool = the ledger's `dev` row 295 / 148 (8873 s).
- **step 4 carries an identical copy of `manifest.py`** (B1204 is cut from fable, not from this branch); the two adds are byte-identical, so they merge cleanly in either order.

## Checks run

```
python3 -I scripts/hermes/manifest/lint_manifest.py --self-test          # SELF-TEST PASS (15 cases)
for f in scripts/hermes/manifest/templates/MANIFEST_*.md; do python3 -I scripts/hermes/manifest/lint_manifest.py $f; done
#   MANIFEST OK .../MANIFEST_diagnose.md leg::diagnose tier::heavy model::reasoning need:hermes-measure need:lane-5 need:thinkpad-render
#   MANIFEST OK .../MANIFEST_fix.md leg::fix tier::medium model::strong need:cpu-build
#   MANIFEST OK .../MANIFEST_review.md leg::review tier::light model::any
#   MANIFEST OK .../MANIFEST_tool.md leg::tool tier::medium model::strong
python3 -I scripts/hermes/manifest/lint_manifest.py --issue 520          # MANIFEST BAD issue#520 / no `## Manifest` block (exit 1)
```

Cite checker: `python3 -I scripts/refactor/cite_check.py refactor/B1202/README.md --tree . --extra /mnt/projects/fable` -> `checked 2 cites, 0 problems, 0 address warnings`

## For the orchestrator: install (NOT done by this MR)

1. Labels (23; run once from any checkout):

```
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "leg::diagnose" --color "#5843AD" --description "B1202 manifest leg: one diagnose leg (measure/trace, report-only MR)"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "leg::fix" --color "#5843AD" --description "B1202 manifest leg: one fix leg (implement + test + re-gate)"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "leg::review" --color "#5843AD" --description "B1202 manifest leg: cold review or re-review"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "leg::infra" --color "#5843AD" --description "B1202 manifest leg: lanes/hosts/CI work"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "leg::tool" --color "#5843AD" --description "B1202 manifest leg: scripts/tooling"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "tier::light" --color "#A8D695" --description "B1202 manifest tier: light"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "tier::medium" --color "#F0AD4E" --description "B1202 manifest tier: medium"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "tier::heavy" --color "#D9534F" --description "B1202 manifest tier: heavy"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "model::any" --color "#6699CC" --description "B1202 manifest minimum model class: any"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "model::strong" --color "#6699CC" --description "B1202 manifest minimum model class: strong"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "model::reasoning" --color "#6699CC" --description "B1202 manifest minimum model class: reasoning"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:thinkpad-render" --color "#999999" --description "B1202 manifest need: N ThinkPad model_viewer renders (count in the body block)"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:thinkpad-quiet" --color "#999999" --description "B1202 manifest need: exclusive quiet ThinkPad"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-1" --color "#999999" --description "B1202 manifest need: eqemu lane 1 lease"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-2" --color "#999999" --description "B1202 manifest need: eqemu lane 2 lease"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-3" --color "#999999" --description "B1202 manifest need: eqemu lane 3 lease"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-4" --color "#999999" --description "B1202 manifest need: eqemu lane 4 lease"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-5" --color "#999999" --description "B1202 manifest need: lane 5 (ThinkPad oracle + eqemu) lease"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:lane-any" --color "#999999" --description "B1202 manifest need: any free eqemu lane"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:arm-build" --color "#999999" --description "B1202 manifest need: an ARM build slot"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:cpu-build" --color "#999999" --description "B1202 manifest need: an rbuild slot"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:oracle-vm" --color "#999999" --description "B1202 manifest need: an r430/r540 Wine oracle VM"
glab label create -R gitlab.goldfarm.local/willeq/willeq --name "need:hermes-measure" --color "#999999" --description "B1202 manifest need: N cv2 measure processes on hermes-ext (count in the body block)"
```

2. Templates to hermes-ext (diff first: the live files may have changed since 2026-10-29):

```
ssh hermes-ext
cd ~/kb-bodies && git -C ~/src/willeq fetch -q origin
for f in ENV_BLOCK_dev.md ENV_BLOCK_review.md MANIFEST_fix.md MANIFEST_diagnose.md MANIFEST_review.md MANIFEST_tool.md; do
  git -C ~/src/willeq show origin/fable:scripts/hermes/manifest/templates/$f > $f.new; done
diff ENV_BLOCK_dev.md ENV_BLOCK_dev.md.new; diff ENV_BLOCK_review.md ENV_BLOCK_review.md.new   # expect ONE added bullet each
cp ENV_BLOCK_dev.md ENV_BLOCK_dev.md.bak-b1202; cp ENV_BLOCK_review.md ENV_BLOCK_review.md.bak-b1202
for f in *.md.new; do mv "$f" "${f%.new}"; done
```

3. The solo prompt then appends the leg's `MANIFEST_<leg>.md` stub (edited for the card's real needs) to every new card body and issue, and runs `lint_manifest.py` on the body before `kanban create`; issues get the `MANIFEST OK` labels via `glab issue update <N> --label <...>`.
