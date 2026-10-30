# Card manifest -- the fixed vocabulary every willeq card and issue carries

B1202 (#521), step 2 of the deterministic allocation stack (Will, 2026-10-29). The B1204
allocator admits a card only when every resource its manifest names is free, and routes its
model by the manifest's tier/model; nothing in that loop is inferred. Implementation:
`scripts/hermes/manifest/manifest.py` (parser, rules), `scripts/hermes/manifest/lint_manifest.py`
(checker). Cost defaults come from the B1201 inference ledger
(`refactor/B1201/tables/by_leg_type.tsv`, once B1201 merges).

## Where it lives

1. **Card body / issue description**: a `## Manifest` block, one `key: value` per line
   (a leading `-` and backticks are tolerated). The block ends at the next `#` or `##` heading.
2. **GitLab labels** on the issue (derived from the block; the linter checks they agree):
   `leg::<leg>`, `tier::<tier>`, `model::<model>` (scoped: one of each), and one unscoped
   `need:<name>` per need (`need:lane-<id>` for a lane). Counts (`:N`) live only in the block;
   a card with labels but no block is read as count 1 per need and is flagged.

```
## Manifest
leg: fix
tier: heavy
needs: thinkpad-render:2, hermes-measure:2, cpu-build
model: strong
phases: code:strong, build:none, render:none, measure:none, doc:cheap
est: turns=62, wall_min=42
```

## Fields (all six required)

| field | values | meaning |
|---|---|---|
| `leg` | `diagnose` \| `fix` \| `review` \| `infra` \| `tool` | the one leg this card is (one leg per card). `review` covers cold reviews and re-reviews; `infra` = lanes/hosts/CI; `tool` = scripts/tooling |
| `tier` | `light` \| `medium` \| `heavy` | resource weight; must be at least what the derivation rules give |
| `needs` | comma list, or `none` | the non-inference resources the card takes while it runs (table below) |
| `model` | `any` \| `strong` \| `reasoning` | the minimum model class; must be at least the strongest phase quality |
| `phases` | ordered `phase:quality` list | phases from `trace code build render measure leg doc review` (the B1201 `phase:` signal vocabulary); quality from `none cheap any strong reasoning` = the inference the phase needs (`none` = a script runs, the model only waits) |
| `est` | `turns=<int>, wall_min=<int>` | expected assistant turns and wall minutes for ONE run; default = the B1201 median for the leg (below) |

### Needs

| need | argument | resource (capacity in `scripts/hermes/alloc/capacity.yaml`, B1204) |
|---|---|---|
| `thinkpad-render:N` | N >= 1 | N concurrent `model_viewer` renders on the ThinkPad (the gate runner, `render_gate.sh`) |
| `thinkpad-quiet` | -- | the ThinkPad with no other render running (timed / per-frame-cost legs); needs `thinkpad-render:N` too |
| `lane:5` / `lane:1`..`lane:4` / `lane:any` | lane id | an eqemu + oracle lane lease (`scripts/eqemu/leases`); lane 5 = the ThinkPad oracle |
| `arm-build` | -- | an ARM build slot |
| `cpu-build` | -- | an `rbuild` slot (configure / build / ctest / hygiene) |
| `oracle-vm` | -- | an r430/r540 Wine oracle VM |
| `hermes-measure:N` | N >= 1 | N cv2 measure processes on hermes-ext |

## Derivation rules (the linter enforces "declared >= derived")

Tier (`derived_tier()` in `scripts/hermes/manifest/manifest.py`):
1. any of `thinkpad-render`, `thinkpad-quiet`, `lane`, `oracle-vm` in needs -> **heavy**;
2. else `leg: review` -> **light** (reviews re-derive from committed files; a review that re-runs a build or render declares that need and becomes medium/heavy by rule 1/4);
3. else `leg: fix` whose phases are only `doc` / `review` / `trace` (a fix that touches only `refactor/` or `docs/` text) -> **light**;
4. else -> **medium**.

Model (`derived_model()`): the strongest phase quality (`none`/`cheap`/`any` -> any, `strong` -> strong, `reasoning` -> reasoning).

Other checks: `thinkpad-quiet` requires `thinkpad-render:N`; unknown needs/phases/qualities, a lane outside 1..5/any and non-integer `est` values are errors.

Guidance for authors (not enforced): a diagnose leg's `trace` phase is `reasoning` (decompile reading); `code` in a fix leg is `strong`; `build`, `render`, `measure`, `leg` are `none` (scripts; B1203 runs them as no-agent jobs where possible); `doc` is `cheap`; `review` is `any`.

## est defaults (B1201 ledger medians, 192 worker runs 2026-10-24..29)

| leg | turns | wall_min | ledger leg type it comes from |
|---|---|---|---|
| review | 72 | 41 | review (re-review median: 50 turns / 23 min) |
| fix | 62 | 42 | fix |
| diagnose | 262 | 192 | diagnose |
| infra, tool | 295 | 148 | dev |

## Checking

```
python3 -I scripts/hermes/manifest/lint_manifest.py <card-body.md>    # block only
python3 -I scripts/hermes/manifest/lint_manifest.py --issue <N>       # block + labels
python3 -I scripts/hermes/manifest/lint_manifest.py --self-test
```

`MANIFEST OK <source> <labels>` (exit 0) prints the labels the block implies, ready for
`glab issue update <N> --label ...`; `MANIFEST BAD <source>` lists every problem (exit 1).
A card without a manifest is admitted by the allocator as `tier: heavy, model: strong` and flagged.

Templates: `scripts/hermes/manifest/templates/MANIFEST_{fix,diagnose,review,tool}.md`
(stubs that lint clean) and the ENV_BLOCK copies with the phase-signal + manifest bullets.
