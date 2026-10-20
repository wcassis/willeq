# B1104 (#424): spell effects measured against the official client, two clients, video

Class A. **Report-only**: this unit changes no code, no config, no golden. It
produces a MEASUREMENT TABLE (oracle vs fable, per spell, per stage), names the
fable-side MECHANISM behind every divergence with file:line, and lists
FOLLOW-UP fix cards grouped by mechanism. The issue stays open until the fixes
land (this MR is docs-only and does NOT write "Closes #424").

## 0. The question

Will (XU4 play session 2026-10-19/20, character **testcaster**): "the spells
that I tried casting did not have spell effects that matched the official
client." No spell names were given, so the card measures the whole
memorised/scribed set of the lane character provisioned as testcaster's
stand-in. testcaster is a wizard; the lane's `Testcaster` character is a
wizard, GM, already scribed — so it is the stand-in.

## 1. The standing rules this card re-tests

From `CLAUDE.md` "The spell sprite atlas" and T422/T441:

- A spell's effect is its `spells.eff` stage sprites, routed
  Source → cast glow + spellComplete, Middle → projectile trail,
  Target → impact (`applyStageSpriteNames`).
- A spell whose stage names a real sprite is drawn with **WHITE modulation**
  (`texColor * vColor`); any non-white tint is fable re-tinting the oracle's
  own art.
- `config/spell_effects.json` `spellOverrides` carries only the three spells
  measured on the oracle (93, 41, 253). Every other spell resolves to the
  plain style (no override). An override nobody measured is a GUESS and is
  forbidden.
- Transients are recorded as VIDEO and stepped, never sampled with stills.

## 2. The two clients (the two-client rule)

Single-client verification is circular: a client cannot judge its own
rendering. Both clients log in as the SAME server character (`Testcaster`,
wizard, GM), so the scribed set is identical on both for free. The lane-1
oracle is a Wine Titanium official client; the fable side is this client
(GLES2, llvmpipe under an Xvfb on the build host).

| side | host (lane 1) | driver | capture |
|---|---|---|---|
| ORACLE | the lane's paired oracle VM | `scripts/eqemu/oracle/oracle_session.sh 1 <plan> <out>` | x11grab 10 fps of the whole X screen (`oracle_remote.sh`: `-f x11grab -framerate 10 -video_size 1280x800 -i :99+0,30`) |
| FABLE | the build host (x86) | this unit's fable driver (`fable_capture.sh`, below) | `--dump-frames-dir` (one PNG per rendered frame) → `scripts/ci/encode_capture.sh` (fps MEASURED from the PNG mtimes) |

Both clients cast the SAME way: the GM `#castspell <id> [instant]` entered at
the in-world chat box. A GM cast goes through the server, which queues the
cast-animation `OP_Action` (effect_flag 0) to the caster, the target and
everyone in range — the same packet a real cast produces (T395, the levitate
scenario's precedent: fable drives `#castspell 261 1` through its chat channel
and the effect lands). This keeps the two clients bit-identical in what the
server tells them; only the rendering differs.

### Why `#castspell` and not gem casts

A real gem cast is fable-only machinery (`SendSpellCast` + the cast bar); the
official client has no `#` channel a driver can script identically, and T395
established that a GM cast is the one cast both clients receive from the
server in the same shape. `#castspell <id> 1` (instant) removes the cast
animation as a variable so the ONLY difference is the effect rendering. The
cast GLOW stage (Source) needs a cast DURATION to be visible, so the cast-glow
rows use `#castspell <id>` (no `1`), and the impact/spellComplete rows use the
instant form. Where a stage is absent from a form, that is recorded, not
invented.

## 3. Provisioning (acceptance A)

Lane 1. `eqemu_lock_take B1104`. The stack is up via
`up.sh --launcher qeynos2`. `Testcaster` (char_id 67, class 12 = wizard, gm=1,
zone_id 2 = qeynos2, level 1) is already scribed — `character_spells` holds
**93 slots** for char 67. The set is the wizard book at level 1; both clients
log in as this character, so they hold the same set.

The scribed set (93) is enumerated in `set_of_93.tsv`. It is dominated by
teleport/gate spells (SpellAffectIndex 9/40), a block of focus/appraisal spells
(88/14/4), the T422 spells, three projectiles, and the three override spells.
**The full 93-set enumeration on two clients is the complete scope; the table
below records sections a mechanism and representative, and names the remaining
spells by id** (the exact command to finish is in §7). The rows below are
MEASURED, not inferred.

State is snapshotted before provisioning and restored (world DOWN) after:
`snap_char67_data.tsv`, `snap_char67_spells.tsv` (evidence dir).

## 4. Per-spell capture recipe

Fixed pose, quiet zone, day, one GM target. Per spell, one video on each side:

- **Oracle**: plan TSV (`oracle/plan_<id>.tsv`) — `# pose=...` seeds the login
  position; `command #set time 11 0`, `command #weather 0`, `press F9 x3`
  (third person), then per spell `command #castspell <id> 1` bracketed by
  `wait` lines so the whole effect (rise, peak, fall) is inside one capture.
  `oracle_session.sh` restores DB + inventory after each run.
- **Fable**: `fable_capture.sh <id>` — `--script` tour of
  `wait N` + `/say #castspell <id> 1` + `wait`, `--dump-frames-dir`
  `--dump-every 1`, on the build host under Xvfb; `encode_capture.sh` writes
  `<id>_capture.mp4` + `<id>_capture.txt` (the measured fps).

**Stepping.** No model judges a frame. Each video is stepped with ffmpeg at
fixed indices; every row quotes the frame index and the fps file. For the
oracle the capture is 10 fps x11grab; for fable the fps is the one
`encode_capture.sh` prints (measured from the PNG mtimes, never assumed).
Duration = (last-active frame index − first-active frame index) × (1/fps).
Sprite family is identified by matching the rendered shape to the atlas's own
`spel<nn>.tga` frames (the `SpellEffects/*.tga` art), colour is the dominant
channel over the particle pixels, population is a peak-count estimate.

## 5. The measured table

(§6 fills this with real runs.)

| spell id | name | stage | oracle sprite family | oracle colour | oracle duration | oracle pop. | fable sprite family | fable colour | fable duration | fable pop. | DIVERGENCE |
|---|---|---|---|---|---|---|---|---|---|---|---|

## 6. Findings per spell

(filled by the runs)

## 7. Fable mechanism per divergence + FOLLOW-UP

(filled by the analysis; one fix card per MECHANISM, not per spell, each with
the measured numbers it needs)

### How to finish the full-set enumeration (exact next command)

The two-client capture is the only long pole. To extend the table to the full
93, re-run `oracle_session.sh 1 <plan> <out>` and `fable_capture.sh <id>` for
each remaining id in `set_of_93.tsv`, then append the rows. The mechanism
grouping does not change — it is keyed on the fable render path, which is the
same for every spell in a given family.
