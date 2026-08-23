# T186 — Pi zone-in re-measured: the target has not moved

**Slot: orchestrator-direct. Pi + x86, live eqemu.** T109 was opened on the
grounds that "the real cost is load time" — Pi **37.3s** zone-request-to-ready
against x86 **10.3s** — and T109/T111/T112 landed the entity-distance gate.
The number was never re-measured. It is now.

## Result

Cold befallen zone-in, two runs per machine, paired against one server:

| | T109 baseline | run 1 | run 2 | mean |
|---|---|---|---|---|
| **Pi** | 37.3s | 38.65s | 36.66s | **~37.7s** |
| **x86** | 10.3s | 11.98s | 12.15s | **~12.1s** |

**The Pi is unchanged.** Whatever the distance gate bought (T105 predicted a
30-56% cut in race-model loads, and T112 measured its frame cost), it did not
show up in zone-request-to-ready. The standing perf target stands.

Warm re-zone into the same zone: Pi **23.39s**, x86 **7.98s** — recorded
because it separates first-touch asset work (~14s on the Pi) from the zone
load proper.

Evidence, with timestamps and method, in `review/T186/`.

## What this unit had to establish first — none of it existed

**1. There was no full-client Pi deployment, ever.** `scripts/pi/pi_common.py`
deploys `gles2_zone_benchmark` and nothing else; every prior Pi unit
(H04, T107, T113–T128) used the benchmark harness. Running `willeq` itself on
the Pi needed: the stripped ARM binary (**378MB → 11MB**), `data/willeq.db`,
`config/` (`constrained_presets.json`, `race_models.json`), `data/textures`
(12MB) and `data/item_models.json`. That is ~27MB of the Pi's 242MB tmpfs.
**Deploy `config/` and `data/` wholesale** — discovering them one FATAL at a
time cost several round trips.

**2. The instrument is the `[LOADING][Phase N/15]` lines.** The client's own
`Zoning time` counter reports **`0 ms`** — on x86 too, where the client
connects in seconds. It is not where T109's numbers came from and must not be
used. Measure Phase 5/15 → Phase 15/15.

**3. `--no-graphics` measures nothing here.** It skips the renderer zone load,
which is the bulk of the time. The Pi must run
`--drm --renderer gles2 --constrained orangepi -r 800 600`.

## Traps hit, so the next run does not

- **`willeq_buddy`'s password is not `WillEqH03Pass`.** It fails with
  `Error logging in response code: 105`, which surfaces as a silent stall at
  `[LOADING][Phase 2/15]` — it looks like a network problem and is not.
  `willeq_rogue` and `willeq_test` with `WillEqH03Pass` work.
- **`up.sh` alone is not enough for zone-to-zone work.** It starts ONE static
  zone; world dynamically boots only a couple more. `#zone befallen` then hangs
  at Phase 5 forever because no befallen process exists. **Use
  `up.sh --launcher`** (the sanctioned spire:launcher path, 25 zone processes).
- **Do not hand-start `./bin/zone <name>` to patch around that.** Doing so
  broke zone connectivity for *every* client, including initial zone-in —
  almost certainly a port collision with the existing process. The launcher
  assigns ports; a hand-started zone does not.
- **A DB `UPDATE character_data SET zone_id` does not stick.** It was back to
  its old value after the run. Drive zoning with `#zone` from a GM account.
- Pi credentials are **`user@10.0.30.192`** (`PI_USER` in `scripts/pi/env.sh`),
  not `orangepi@`. The Pi reaches eqemu at `10.0.30.23:5998`.
- The Pi's clock runs at a different UTC offset from the VM's. Compare deltas,
  never absolute timestamps across the two logs.

## What this does NOT say

Two runs per machine is enough to say the number has not moved; it is not a
distribution, and a 1–2s difference either way means nothing here. It also
says nothing about *why* — this unit measured, it did not profile. The next
step, if the target is still wanted, is attribution: which phase of the Pi's
~37s is asset decode, which is GPU upload, which is entity prep.

## Server state

Restored: eqemu brought down, lock released. One deviation made and undone
during diagnosis — world's `address` was changed `0.0.0.0` → `10.0.30.23`
while chasing a wrong theory, then restored. It was not the cause and its
necessity for a remote client remains untested.
