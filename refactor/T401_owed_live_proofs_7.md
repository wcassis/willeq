# T401 — Owed live proofs 7 (sound audible, coins on screen, fire vs frost observed, vision with weather pinned)

Measurement only; slot3; eqemu lock (atomic; must be ABSENT — if present
read it and STOP). Reuse the T377/T386/T391 drivers under
`review/T377|T386|T391/scripts/`; `--script` two-client rig; `/camp`
cleanly (a hard-killed client leaves a linkdead session that overrides
`character_data` writes — T391); verify every chat line against the
client's `Chat submit:` line; video for transients. Rebuild the slot's
`build` and `build-gles2` from the tip (T392 sounds, T393 purse, T395
impact, T396 weather log, T397 removal, T398 coins).

1. **Sound audible (B200/B036/B172).** T328's tone-probe rig on fable:
   loot a corpse (T377's arena recipe) → `BuyItem.WAV` (138) actually
   heard; one combat round → `SwordBlk` (120); one gem cast → `Spell_*`.
   The log must show `Loaded sound from PFS` lines and zero `not found`.
   Fill the rows on B036/B172/B200.
2. **Coins on screen (B201/T393).** Open the inventory after the loot:
   fable screenshot with the four coin rows and the decals (T398) vs the
   official client's inventory window at the same purse — side by side.
   Note B219 (the rows land on fable's oversized paperdoll) with the
   crop so Will can rule.
3. **Fire vs frost observed (B160/B204/B220).** Two clients; the caster
   gem-casts (NOT `#castspell` — no OP_BeginCast) Burst of Flame (93) and
   Frost Bolt (54) on itself with the observer 15 units away, caster in
   frame; the observer's video must show two DIFFERENT-coloured impacts
   (T395 keyed the impact on effect_flag=0; T397 stopped the wipe; T389
   mapped the tiles; the atlas alpha (T400) may or may not have merged —
   say which tip). If nothing draws, that is B226 (style too small) — crop
   at 4x and report the changed-pixel count.
4. **Vision with weather pinned (B203).** Repeat T377 §2 exactly but
   FIRST prove weather clear from the log (T396's per-zone-in line and
   the modifier line must read 1.000 across the whole window; if rain
   arrives, `#weather 0 0` and wait for the 1.000 line). Per-channel and
   whole-frame ratios for Ultravision and Infravision vs the oracle's
   1.561 / 1.256 (oracle side re-captured in the same session, same
   weather proof). This is the baseline the per-channel re-solve starts
   from; do NOT tune anything here.
5. `review/T401/README.md` (PASS/FAIL first per item); strike/annotate
   rows; restore every server value (world clock too) with the world
   DOWN; `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board B250+, continue).

---

## Review — T401 (executor, slot3, 2026-09-05/06)

Branch `task/T401-owed-live-proofs-7`, base/tip **`4ff78bb4`** (verified at
checkout). Measurement only: **no file under `src/`, `include/` or `tests/`
changed.** Both slot3 trees rebuilt from the tip first —
`cmake --build build -j24` **EXIT=0**, `cmake --build build-gles2 -j24`
**EXIT=0** (logs `review/T401/logs/build_main.log`, `build_gles2.log`, exit
recorded to a unit-private `.exit` file per the shared-scratchpad rule).
Full evidence: **`/mnt/projects/fable/review/T401/README.md`**.

**⚠ THE SHARED PACK CHANGED UNDER THIS UNIT — read this before the results.**
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` was rewritten by another slot at
**23:09:57, 51 s into the first live session**: 2,811,102 → **2,830,206
bytes**, alpha companions **1 → 2** (T400's particle-atlas alpha, B225, whose
CODE is not merged and is in no binary here). The two clients' own logs carry
the two sizes, so items 1 and 2 ran on the pre-T400 pack and items 3 and 4 on
the T400 pack. **Item 3's colours are "this tip's code + T400's atlas asset"
and cannot separate the two.** It is also why `ctest` on this branch has two
reds this branch did not cause. Boarded as **B255**; full detail in
`review/T401/README.md` §0.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** |
| `cmake --build build-gles2 -j24` | **EXIT=0** |
| `ctest --test-dir build -j6` | **4,121 / 4,123** — `PackReproducible` and `visual_gate` red, **both from B255's shared-pack rewrite, neither from this branch**: the installed pack's sha256 no longer matches `data/willeq.eqe1.sha256`, and `eqe1MappedBytes` is off by **+19,343** in all three zones (NOT B240's +239, so B240's numbers are stale again). Every other test green; goldens untouched |
| `scripts/refactor/check_file_sizes.sh fable` | **34 checked, 34 ok, 0 warn, 0 fail** |
| visual gate | see `ctest` row — red on the shared asset only, no image golden moved, no CPU p95 ceiling missed |


| # | item | verdict |
|---|---|---|
| 1 | sound audible (B200/B036/B172) | **PASS** on 138, 140 and 108 — and two NEW class-A defects, **B250** and **B251** |
| 2 | coins on screen (B201/T393/B219) | **PASS**, and B219 is confirmed on screen |
| 3 | fire vs frost observed (B160/B204/B226) | **PASS** — orange vs cyan, and B226's "not visible in a capture" is refuted |
| 4 | vision with weather pinned (B203) | weather proof **PASS**; ratios **FAIL** and T377's fable baseline is **REFUTED** → **B252**, **B253** |

### 1 — the ear

21 `Loaded sound from PFS`, **zero** `not found on disk or in PFS` (T386 had
31 failures and zero successes). None of the five files involved exists loose,
so a play is a PFS load by construction. Audibility was proven by recording
fable through an ALSA loopback (1 kHz reference verified) and matching the
recording against the archives' own bytes:

| id | file | capture t | ncc | rate |
|---|---|---|---|---|
| 138 | `BuyItem.WAV`, corpse loot | 232.065 s | 0.8292 | **k=2.00** |
| 140 | `LevDn.WAV`, NPC death | 199.581 s | 0.9959 | **k=2.00** |
| 108 | `SpelCast.WAV`, gem cast x3 | 73.7 / 96.7 / 140.8 s | 0.762 / 0.728 / 0.768 | **k=2.00** |
| 120 | `SwordBlk.WAV`, 30 melee rounds | *(absent)* | 0.077 max | — |

The capture-clock offset was not assumed: it falls out of the first match and
every later row then lands on its own log line to 0.03 s.

**Deviation, flagged:** the plan asked for T328's tone probe. It was not used
and could not have been — a tone-probe client reads **loose** files, so it
would have bypassed the PFS path this item is about. Matched-filtering the
real archive bytes is a strictly stronger identification and keeps the code
path under test. Method + self-test (ncc 0.997) in README §1b.

**B250**: all three identified sounds correlate at **k = 2.00 and only there**
(scan 0.5–3.0). All three are 11,025 Hz assets; the 22,050 Hz case is
UNMEASURED and named as such. **B251**: `SwordBlk` is inaudible because it is
emitted at the player's **stale spawn position** — the same stale self-entity
that refuses a self-targeted cast as out-of-range (README §6.3).

### 2 — the purse

`character_currency` 4/6/1/8 → **9/13/2/16**, exactly the counts the client
printed, with both `CurrencyChanged` bridge lines in the log. The official
client was then logged in **at the same purse** (written before login) and the
two windows agree: four rows, platinum→gold→silver→copper, a round decal left,
count right, 9/13/2/16. `T401_fable_purse_coinrows_crop4x.png` vs
`T401_oracle_purse_coinrows_crop4x.png`.

**B219 is now visible rather than predicted** and confirms its own prediction:
the oracle's coin column clears its character view; fable's four rows sit on
the paperdoll panel's right margin. Legible, but inside the doll's box.
Nothing was moved — it is Will's ruling.

**Deviation, flagged:** `purse_00_before` is not an inventory shot (the `i`
went into the chat entry). The before/after evidence is the DB rows plus the
bridge lines; the `after` screenshot is real and is the deliverable.

### 3 — fire vs frost

(Ran on the T400 pack — see the warning above.)

Gem casts (`/cast`, never `#castspell`), caster and observer both graphical,
observer 15 units away with the caster in frame, every frame dumped (920
frames, **4.9188 measured fps**, video + `.txt` in `review/T401/fable/`).
The observer's log carries `SpellAction … effect_flag=0` and then `Enqueued
CreateSpellEffect 'Impact'` + `SimWorker: Created spell effect 'Impact'` for
both casts — T395's gate proven on a bystander, which never sees the 4.

Crop 180x160+600+330, each impact frame differenced against one ~2.5 s earlier:
**Burst of Flame 442 changed pixels at (255, 155, 63) — orange**; **Frost Bolt
438 at (64, 255, 241) — cyan**; idle control 389. **The changed-pixel COUNT
discriminates nothing** (the caster's model is drawn as a flat yellow outline
whose idle animation moves ~389 px on its own) — **the colour does**, and that
caveat is stated in the README rather than absorbed.

### 4 — vision

**The weather proof is the one unambiguous PASS here** and it is exactly what
T396 built for: `WeatherEffectsController::onZoneEnter('qeynos2'): weather
reset to clear, ambient modifier 1.000`, and **zero** `updateTimeOfDay:
weatherMod … -> …` lines in the whole session — a line that since T396 fires
on every change in both directions. This is the first vision measurement in
the project taken under a demonstrated-clear sky.

Both sides re-measured in this session, video-stepped (fable: 1,117 frames;
oracle: the plateaus off its own 824-frame video, not single stills). The
per-frame linear-light reduction moved to numpy for speed and was **checked
against ImageMagick on sample frames — agreement 5 parts in 10,000** (table in
README §4).

| | R | G | B | grey |
|---|---|---|---|---|
| oracle Ultravision (T401, clear) | x1.9422 | x1.2990 | x1.5466 | **x1.5419** |
| **fable Ultravision (T401, clear)** | **x1.2078** | **x1.1658** | **x1.0649** | **x1.1606** |
| oracle Infravision (T401, clear) | x1.8243 | x1.1869 | x1.1763 | **x1.3362** |
| **fable Infravision (T401, clear)** | **x1.2754** | **x1.1076** | **x1.0596** | **x1.1693** |

Two findings. **B252 — T377's fable baseline is refuted as weather-inflated**:
on the same T394 constants under a proven-1.000 modifier fable's Ultravision
is 1.1606, not the 1.4483 B203 tells the next unit to start from, and its
Infravision is 1.1693, not 1.2450. **B253 — the oracle's own per-channel table
did not reproduce**: its grey holds (1.5419 vs T377's 1.5612) but R came out
x1.94 against x1.46, because the two sessions' oracle frames are **10.3% RMSE
apart** where T386's composition control requires 2.7%. The camera heading, not
the weather, is the uncontrolled variable on the oracle side. **Nothing was
tuned**, per the plan.

Also re-confirmed verbatim from T377: the oracle's `#castspell` OCR check fails
all three attempts and the buff lands anyway — and each retry **re-casts and
expires the previous buff**, which is the two-level Ultravision plateau.

### Deviations and new board entries

- Tone probe not used for item 1 (reason above); matched filter instead.
- `purse_00_before` is not an inventory capture (reason above).
- Item 3 needed four runs; the three failures and their causes are recorded in
  the script headers and README §6, because each is a rig trap the next unit
  would otherwise repeat.
- New: **B250** (PFS sounds play at 2x), **B251** (stale self-entity: silent
  melee sound + out-of-range self-cast), **B252** (B203's baseline is
  weather-inflated), **B253** (the oracle's per-channel table is
  composition-dependent and did not reproduce), **B254** (`#memspell` into an
  occupied gem leaves fable casting the old spell), **B255** (the shared
  `willeq.eqe1` is rewritten under running units — measured, and it reached
  this unit's pixels).
- **Board-number collision risk, flagged:** `fable`'s BOARD.md already carries
  B260/B261 from merges not in this branch, and slot1/slot2 were adding entries
  concurrently. B250–B255 are free in `fable` at the time of writing, but the
  orchestrator should confirm before merging.
- Annotated: B036, B172, B200, B201, B219, B160, B204, B226, B203.

### Server and host

World/zone/loginserver killed (**and `/tmp/spire-launcher` first — it respawns
`bin/world`**), restore applied with only mariadb up, verified by `SELECT`:
`logs/server_snapshot_before.tsv` and `server_snapshot_after.tsv` are
**byte-identical**, `diff` exit 0, across `character_data`,
`character_currency`, `eqtime` and `character_corpses`. `down.sh` exit 0,
`docker ps` empty, no `willeq`/`eqgame`/`Xvfb` process left, `~/.asoundrc`
deleted, `snd-aloop` unloaded, **`/mnt/projects/fable/.eqemu-lock` deleted**.
