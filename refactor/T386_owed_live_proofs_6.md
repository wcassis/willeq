# T386 — Owed live proofs 6

Measurement only; slot2; eqemu lock (atomic; must be ABSENT — if present
read it and STOP). Reuse the T377 drivers (`review/T377/scripts/`,
`review/T361`, `review/T358`); verify every chat line against the client's
`Chat submit:` line; video for transients. Rebuild the slot's `build` and
`build-gles2` from the tip you are given (it carries T381/T382/T383; if
T384's willeq.db v7 has merged by then, rebuild all three trees).

1. **T382 corpse coin (B172/B036).** T377's recipe (`arena`,
   `#npctypespawn 113423`, `#damage 100000`, step back 20, F9 loot): on
   fable, open the loot window — expect the purse to move by the corpse's
   four denominations (SELECT `character_data` / the client's own currency
   line before and after), `BuyItem` (138) to play (the tone-probe rig
   from T328), and the chat line `You receive N platinum, N gold, N silver
   and N copper from the corpse.` Also engineer ONE zero denomination
   (`#give`/`#setcoin`-style GM command on the corpse or a mob whose loot
   table drops no platinum) on the OFFICIAL client and capture the line —
   T382 left the zero form UNMEASURED.
2. **T382 keys.** Press `Alt+P`, `Alt+F1`, `Alt+B` on fable: each toggles
   its window; `G` and `P` do nothing. Screenshot the empty group window
   (no group) on both clients side by side.
3. **T381 spell colours (B160).** Two-client pair: Testchar casts a Fire
   spell and a Cold spell (`#castspell` ids from T380's census — Frost
   Bolt 54 and a fire spell of similar level); the OBSERVING client's
   frame shows two different particle colours. Video both.
4. **T383 vision channels (B171).** Repeat T377 §2 exactly (same spot,
   crop, video-stepped): per-channel ratios for Ultravision and
   Infravision on fable vs the oracle table (R/G/B and whole-frame). The
   B×0.821 anomaly T383 could not explain from the source: does it
   reproduce? If yes, board it with the frames.
5. `review/T386/README.md` (PASS/FAIL first per item); strike the owed
   notes; restore every server value (snapshot the world clock);
   `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board B185+, continue).

---

## Review

Executor session 2026-09-05, slot2, branch `task/T386-owed-live-proofs-6`,
base/tip **`c0a76fea`**. **Measurement only — no source file changed.** Both
slot2 trees rebuilt from the tip first: `cmake --build build -j24` **exit 0**,
`cmake --build build-gles2 -j24` **exit 0**. eqemu lock taken atomically
(absent on arrival), server up via `up.sh --launcher`, every changed value
restored with the world DOWN and verified by `SELECT`, `down.sh` exit 0, lock
deleted.

Full evidence, every table and every frame: **`/mnt/projects/fable/review/T386/README.md`**.

| # | item | verdict |
|---|---|---|
| 1 | B172/B036 corpse coin | **PASS** on the purse and the chat line (2 trials); **FAIL** on the sound (**B185**); the purse is drawn nowhere (**B186**); the oracle's zero-denomination line **UNMEASURED** (blocked, method + trap recorded) |
| 2 | B170 window keys + empty group window | **PASS** (all five keys, plus the side-by-side crop pair) |
| 3 | B160 spell colours, two-client | **FAIL / NOT PROVEN** — nothing is drawn (**B189**), and the two-client session segfaults (**B187**) |
| 4 | B171 vision channels | **FAIL** — T383's re-solve is refuted and both scalars regressed (**B188**); the `B x0.82` anomaly did **NOT** reproduce |

### 1 — corpse coin

`arena`, Testmelee, T377's recipe, fable's own `/loot <name>` (no 3D-view
click, so T342/T377's right-click intermittency does not apply on this side).
Two independent trials, each `SELECT`ed after `#save`:

| trial | chat line | `character_currency` before → after | delta |
|---|---|---|---|
| 1 | `You receive 1 platinum, 9 gold, 2 silver and 3 copper from the corpse.` | 4/6/1/8 → 5/15/3/11 | +1/+9/+2/+3 |
| 2 | `You receive 3 platinum, 3 gold, 5 silver and 5 copper from the corpse.` | 5/15/3/11 → 8/18/8/16 | +3/+3/+5/+5 |

Both deltas are exactly the printed counts. The sound intent reaches the audio
manager and the asset then fails to load — `Sound failed: id=138
file=BuyItem.WAV (not found)` — which proves T382's chain live AND that nothing
is audible; 31 such failures and 0 PFS sound loads in one session (**B185**).
The purse itself is drawn nowhere: the `CurrencyChanged` bridge arm is a
`LOG_TRACE` and `updateCharacterStats` voids the four values (**B186**).

**Deviation, flagged:** the T328 tone-probe rig was NOT used on the fable side.
It exists because the official client has no log; fable names the sound id and
the resolved filename itself, and once the asset fails to load there is nothing
for a microphone to hear.

### 2 — window keys

Six `OnEvent: action Toggle…` lines against six `Group/Pet/Buff window
opened|closed` lines for `Alt+P`, `Alt+F1`, `Alt+B`; **zero** input events for
bare `G` and bare `P`. Both clients' empty group window is a bare title strip.
**The trap:** the first run pressed every key with the chat field focused
(`chatFocused=true`) and every renderer-local action was dropped silently —
`Alt+F1` survived only because `F1` is not a text character. Send `Escape`
before each key.

### 3 — spell colours

Six two-client attempts. The blocker is a reproducible SIGSEGV on the render
thread in `SimBridgeComponent::applySimulationOutput` whenever a second player
is in the zone (five crashes in seven client-instances; backtrace captured
under gdb) — **B187**. In the one run that survived both casts with the caster
in frame, the observing client logged both `SpellAction` packets and its pixels
did not move at all (crop channel means agree to the fourth decimal across the
impact) — **B189**, and the standing "the particle manager is null" explanation
is removed, because it is initialised in that very log.

Two rig facts worth keeping: **a GM is invulnerable and `#castspell` on self is
then refused** (`… has failed to land on yourself because you are
invulnerable`, `effect_flag` never reaches 4) — `#set invulnerable off` first;
and **`--script` is the right driver for a two-client run**, not xdotool (it is
deterministic, needs no window focus, and works with `--no-graphics`).

### 4 — vision channels

T377 §2 repeated exactly, 882 dumped frames, five flat windows taken from the
client's own vision log lines, composition controlled (the two sessions' night
stills differ by RMSE 2.7%).

| | R | G | B | grey |
|---|---|---|---|---|
| oracle Ultravision | x1.4581 | x1.4633 | x1.8507 | **x1.5612** |
| T383 predicted | x1.4569 | x1.4640 | x1.8503 | x1.5561 |
| fable pre-T383 | x1.4609 | x1.2814 | x1.6629 | x1.4483 |
| **fable post-T383 (measured)** | **x0.9674** | **x0.9723** | **x1.6377** | **x1.1686** |
| oracle Infravision | x1.3284 | x1.2111 | x1.2079 | **x1.2555** |
| T383 predicted | x1.3298 | x1.2125 | x1.2085 | x1.2582 |
| fable pre-T383 | x1.6002 | x1.1264 | **x0.8209** | x1.2450 |
| **fable post-T383 (measured)** | **x1.0688** | **x1.0345** | **x1.1274** | **x1.0757** |

`B x0.8209` did **not** reproduce. T383's forward model is refuted (R and G were
predicted to rise ~46% and instead fell ~3%). Both scalars regressed. Also
recorded: fable's night-no-buff base moved between the two tips on the same
crop, blue by **+55%**, which nothing in T383's stated changes touches.

### Board

New: **B185** (A, PFS sounds never load), **B186** (A, purse never drawn),
**B187** (A, two-client render-thread SIGSEGV), **B188** (A, B171 reopened),
**B189** (B, no spell impact visual), **B190** (B, hex blob in the
spell-refusal eqstr). Amended in place: **B170** proven live, **B171**'s
provisional strike reversed, **B172**'s owed proof taken and split.

### Server state

`review/T386/logs/server_snapshot_before.tsv` and `…_after.tsv` are
**byte-identical** on the columns snapshotted (`diff` exit 0): character
positions/zones for Testchar, Testmelee, Testcaster, Testbuddy and Oraclealpha,
Testmelee's purse (4/6/1/8), and the `eqtime` world clock (26 / 24 / 6 / 4 /
4004 / 1788601087) all restored with the **world down and only mariadb up**,
then verified by `SELECT`. The one player corpse this session created
(Oraclealpha, arena) was deleted. Testmelee and Oraclealpha gained experience
from the kills; both were set back to level 1 / exp 0 — **the pre-session `exp`
value was not in the snapshot, so `exp=0` is the restoration, flagged here
rather than absorbed.**
