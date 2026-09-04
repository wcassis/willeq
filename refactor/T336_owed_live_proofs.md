# T336 — The owed live proofs: navmesh overlay ON/OFF pair (B048), and the listener pan check (B045)

Measurement-only live session; slot1; needs the eqemu lock (free after
T332). No code change.

1. Lock atomically; `up.sh`; `build-gles2/bin/willeq` from the slot at the
   CURRENT tip (`cmake --build` first; the slot must contain T333's fix —
   `git log --oneline -20 | grep T333`), account willeq_test / `Testchar`
   (T331/T332's slot-2 selection script — read `review/T332/`), qeynos2,
   force daylight, `--eqe1-path`.
2. **B048 pair:** capture OFF, press Ctrl+N (T329's input findings: use
   the working input route from `review/T329/README.md`), capture ON; the
   ON frame must show navmesh lines AND the health/mana/stamina bars, FPS
   counter, chat panel and hotbar. Two PNGs + README to `review/T336/`
   naming what to look at. Then Ctrl+M (map overlay) ON/OFF the same way
   if T335 has merged by your tip (check `git log`); else note it owed.
3. **B045 check (optional, cheap):** with the audio backend the T328 rig
   used (`snd-aloop` capture), stand facing a positioned sound source
   (a door: open it with `/say #door open`-style GM or just a click; door
   sounds are positioned per `entities_component_world.cpp`), record 2 s,
   turn 180°, record 2 s; the left/right channel balance must swap. Two
   short WAVs + the channel RMS numbers in the README. If the rig cannot
   be set up in 20 minutes, skip it and say so — it is a listening test.
4. Restore the character as found (SELECT first); `down.sh`; delete the
   lock. Strike the "owed" notes on B048 (and B045 if step 3 ran) in
   `refactor/BOARD.md`; commit the board + plan Review only.

STOP: lock held; the client cannot log in; Ctrl+N does not toggle (then
the fix is wrong — report the log lines, do not touch code).

---

## Review

Measurement only; no source file changed. Base/tip `429716b5`, branch
`task/T336-owed-live-proofs`. T333's fix (`eaefcaf8`) confirmed on the tip and
the client rebuilt from this slot before the session
(`scripts/build-gles2-x86.sh`, exit 0).

**Media: `/mnt/projects/fable/review/T336/` (README names what to look at).**

### 1. B048 — the owed ON/OFF pair: CAPTURED

Two sessions, one per overlay state (T329's input findings force the split), same
character, same qeynos2 spot, same camera, same forced 11:00 daylight; the only
difference is one Ctrl+N.

The overlay was ON by the client's own account, not the harness's:

| line from `logs/live_session_on.log` | |
|---|---|
| `[MAP] Loaded Navmesh V[2] file [/mnt/projects/maps/nav/qeynos2.nav]` | navmesh-enabled zone |
| `[INPUT] Key pressed: N (ctrl=true, ...) action=ToggleNavmeshOverlay` | the chord arrived |
| `[GRAPHICS] Navmesh overlay: ON` | the toggle took |
| `[GRAPHICS] Navmesh overlay: Updated with 112 triangles` | same 112 T329 measured |

**What the ON frame shows:** green navmesh lines drawn across the ground, AND
the health/mana/stamina bars, the FPS counter, the chat panel, the hotbar and
the A1-A8 bar all still present. A second grab from the same session shows the
same, so neither is one lucky frame. That is both halves of B048 — "draws
nothing" and "wipes the entire 2D UI" — no longer reproducing on a real device.
Compare `review/T329/`, the same two shots before the fix: no lines, blank UI.

Per protocol the visuals are not self-assessed beyond the presence of those
elements; Will judges.

### 2. Ctrl+M (map overlay) — OWED, not attempted

T335 has **not** merged by this tip: `a61d52bb` is its plan commit only. B054
boards the map overlay, `collisionDebug` and the frustum debug draw as
reproducing B048's defect verbatim, so a pair captured now would record a
known-open bug rather than prove anything. Owed until T335 lands; the driver
`review/T336/t336_live.sh` takes it with a one-line change.

### 3. B045 — SKIPPED inside the 20-minute box (permitted by the plan)

The rig was not the blocker: `snd-aloop` loads and `ffmpeg` is present. **There
was no positioned sound to pan.** The client loads qeynos2 with 13 sound
emitters and across both sessions none fired — the only playback was
`qeynos2.xmi`, which is music and unpositioned. Testchar's surveyed spot
`(-74, 428)` is out of range of all 13, so the test needs the character
navigated blind under Xvfb to an emitter (or a door found and clicked): a
session of its own. B045 is already CLOSED by T334 and pinned numerically by
`tests/test_spatial_audio.cpp` (`ListenerForwardTest`), so a listening test
would be confirmation, not closure. Host audio state restored (`snd-aloop`
unloaded; no `~/.asoundrc` created).

### 4. Incidental: T332's z fix is holding

Testchar read `z=3.75` after two graphical login/camp cycles in the very rig
where T329 recorded `+0.625` per login and T331 fitted the same step. No
compounding. Two more cycles on top of T332's three.

### STOP conditions

None triggered. The lock was free and taken atomically (`T336 2026-09-04`); the
client logged in on the first attempt in both sessions; Ctrl+N toggled on the
first press.

### Housekeeping

- eqemu: `up.sh qeynos2` (exit 0), `down.sh` (exit 0), **lock deleted**.
- `Testchar` SELECTed on arrival — found at `zone_id=2, x=-74, y=428, z=3.751,
  h=160` (not the protocol's arena pose) — and restored to exactly that.
  `Testcaster` and `Testmelee` never written.
- Xvfb `:96` killed.
