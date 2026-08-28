# T304 — Will's three rulings (2026-08-28): death state, race colours, GraphicalMode

**Status: WRITTEN, QUEUED.** Dispatch as slots free, AFTER the 300-commit branch
audit which Will asked for first. Legs are independent and may run in parallel.

---

## Leg 1 — THE DEATH STATE. Owns eqemu (schedule after T301 Leg A releases the lock).

**Will's ruling, and it corrects the orchestrator's proposal:** Titanium has **NO
"release to bind"**. HP reaching 0 puts the player **unconscious and prone with NO
control of the character**. If HP drops further (exact threshold unconfirmed) the
player **dies and is immediately relocated to the bind point BY THE SERVER**.

Consequences for the design, none of which may be re-litigated:
- A client death/unconscious state must **GATE INPUT**, not merely play an
  animation. The `AT_DIE` inbound handler being animation-only
  (`eq_entities.cpp:774-782`, pushes `EntityDeathAnimation` keyed by spawnId and
  nothing else) is therefore incomplete for the LOCAL player.
- The relocation is the **server's** to perform. The client never initiates it.

**Measure before implementing.** The unconscious/dead distinction is a wire fact,
not a guess: drive the official client on the oracle rig, take a character to 0 HP
and then past it, and record **what the server actually sends at each transition**
— `AT_DIE`? an HP update? a position/appearance change? a zone/relocate? Include
the exact HP threshold if the capture shows it. Video/packet capture, stepped;
stills are barred in this codebase (`dont-sample-transients-with-stills`).
`scripts/eqemu` + `/mnt/projects/eq-oracle` both honour the same lock.

Then implement:
1. **The non-sending internal setter** T234/T237 flagged (`deferred.md:138`) —
   every existing path that sets local position state also SENDS it, so applying a
   server-originated death would echo `AT_DIE` back to the server as if it were
   news. This same setter is what stun and GM-teleport need; build it once.
   Outbound `POS_DEAD` (`eq_movement.cpp:1522-1527`) must keep working unchanged.
2. Adopt the measured state locally for the LOCAL player and gate control.
3. Pin it: a test that a server-originated death sets the state and sends NOTHING,
   and a test that input is refused in that state. Reach the gate the way a USER
   does, per the pin-every-fix rule.

## Leg 2 — RACE COLOUR COLUMN. No server.

**Will RULED: add a colour column and populate it with invented values.** The
standing objection ("no colour column exists; filling it would be inventing data",
T271) is **OVERRULED** — invented placeholder tints are acceptable to him.

- Add the column to the race data (`race_models.json` + the willeq.db build), and
  populate every row. These are **placeholder-cube tints**, reached only when no
  model could be built (T213), so the bar is "plausible and distinguishable", not
  "authentic". Say in the Review that the values are invented, by whose ruling,
  and on what date — a future audit must not mistake them for sourced data.
- **Delete `EntityRenderer::getColorForRace()`'s hand-written switch**
  (`entity_renderer.cpp:113`) and read the column instead. **The stale pre-JSON
  numbering in its case comments dies with it** — 21 is BEH Evil Eye not Skeleton,
  26 FRO not Snake, 33 GHU not Ghost, 35 EEL not Rat, 42 WOL not Rat, 43 BEA not
  Bat, 46 IMP not Goblin, 48/49 KOB/DRA not Spiders, 85 SPE not Dragon. Every
  other copy of that numbering (T267, T270, T271) turned out to be LIVE AND WRONG;
  this is the last one.
- Rebuild the db with row-level proof, as T274 did. Goldens: check whether any
  golden frame contains a placeholder cube before assuming no re-bless is needed.

## Leg 3 — GRAPHICALMODE'S SILENT FALLTHROUGH. No server.

**Will (2026-08-28): "used at one point, not sure it's relevant any more."** The
use case he names is **scripting / non-graphical interaction with a running
client** — legitimate, but **not on the radar now**.

So the ruling is neither "delete the capability" nor "wire the adapter":
- **Do NOT delete console/headless interaction**, and do NOT build a roadmap for
  it. Out of scope.
- **Remove the misleading part**: `graphical_mode.cpp:36-50` is a switch where
  `IrrlichtSoftware`/`IrrlichtGPU` `[[fallthrough]]` to the console renderer
  behind a `// TODO: Create IrrlichtRendererAdapter when available`, so a
  GRAPHICAL run silently produces a `ConsoleRenderer` as the command processor's
  sink (`application.cpp:381-385`) while the real Irrlicht renderer is
  constructed and owned by `Application` separately. Make what happens explicit
  at the site instead of implied by a fallthrough, and delete the commented-out
  adapter construction — T241 already deleted its sibling
  `connectRendererCallbacks()`, so it is not coming back in this shape.
- **First establish where slash-command output actually goes in a graphical run**
  and say so in the Review. If it genuinely reaches only a ConsoleRenderer while
  the user is looking at an Irrlicht window, that is a LIVE user-visible defect
  and it is bigger than this leg — report it, do not silently fix or silently
  leave it.
