# GPU Agent PoC — Implementation Plan

## Overview

A GPU-native autonomous agent that uses GLES2 shader-based field dynamics for decision making on Mali400 hardware. Instead of explicit "if enemy then attack" logic, the agent maintains a 2D texture field where entities project signals that propagate, decay, and combine. The agent reads the resulting gradient to determine movement and actions — behavior emerges from field physics.

## Architecture Decision: Agent as a Bridge

The agent is implemented as a third bridge implementation (`AgentBridge`) alongside `IrrlichtBridge` and `ConsoleBridge`. This is the same pattern — it receives game state via bridge events and sends player actions via bridge intents.

- **No new API surface in EverQuest** — the agent consumes the existing 140+ event types and emits existing 50+ intent types
- **Thread-safe by design** — uses the existing GameStateBridge swap-vector pattern
- **Additive mode** — runs simultaneously with the Irrlicht renderer so a human can observe the agent playing
- **Zero EverQuest coupling** — agent never touches game internals, only the bridge

```
Game Thread                    Render Thread              Agent Thread
───────────                    ─────────────              ────────────
EverQuest tick loop            IrrlichtBridge             AgentBridge
pushEvent(EntitySpawned)  ──►  applyEvent() → render  ──► applyEvent() → seed field
pushEvent(EntityMoved)    ──►  applyEvent() → render  ──► propagation passes
pushEvent(HealthChanged)  ──►  applyEvent() → render  ──► gradient readout
                          ◄──  pushIntent(MoveIntent)
                          ◄──  pushIntent(AttackIntent)   pushIntent() ──►
```

## GPU Context Strategy

The agent thread gets its own EGL shared context (third context, same pattern as GPUUploadThread). This avoids GPU contention with the renderer. The Mali400 arbitrates GPU time between renderer and agent workloads.

- At 30fps full scene, GPU headroom is tight — contention is the primary risk
- At 60fps manual/placeholder mode, GPU is idle ~50% of the time — plenty of headroom
- If contention proves problematic: throttle agent cycle rate, reduce field to 256x256, or both

For PoC testing, run in manual loading mode with placeholder cubes / HCMap terrain to maximize GPU headroom.

### Constrained Preset

A `--constrained agent` preset that forces manual loading and simplified rendering to leave GPU budget for the agent thread. The current "skip" flags live in `config/debug.json` (not tied to constrained profiles), so for now these are toggled system-wide.

## Field Simulation

### Texture Layout

One 512x512 RGBA8 texture encodes four signal channels:

| Channel | Signal | Description |
|---------|--------|-------------|
| R | Threat | Hostile entities seed this; agent flees gradient |
| G | Goal | Items, objectives, friendly targets; agent follows gradient |
| B | Passability | 0 at walls, 1 in open space; modulates propagation |
| A | Urgency | Composite of R and G; used for gradient readout |

### Decision Cycle (target: 100ms, faster if Mali400 allows)

1. **Drain bridge events** — update internal world model from game state events
2. **Seed field** — GL_POINTS draw call, one point per entity. Fragment shader writes radial falloff into appropriate channel. Additive blend.
3. **Propagate** — 3-5 ping-pong FBO passes. Fragment shader reads 4 neighbors, applies decay (~0.92) and conductance (B channel). Threat and goal channels evolve independently.
4. **Read gradient** — Sample field at 3x3 around agent position (Sobel on A channel). Compute normalized movement direction + urgency scalar.
5. **Select action** — Gradient magnitude/direction determines: move toward goal, attack nearest threat, flee if health low, loot if threat clear.
6. **Push intents** — Send movement/combat/loot intents through bridge.

### Passability (B Channel)

Baked from existing HCMap/ZoneBSP zone geometry data. One-time rasterization of walkable area into the 512x512 B channel when zone loads. Cached to disk per zone for reuse.

### GLES2 Constraints (Mali400)

- No MRT — single FBO output, pack all channels into RGBA
- No 3D textures — zone is 2D field (height not needed for PoC)
- No integer textures — normalized RGBA8 only
- Dependent texture reads expensive — pre-compute UV offsets as varyings
- `gl_PointSize` max is implementation-defined — query `GL_ALIASED_POINT_SIZE_RANGE` at startup
- `glReadPixels` is slow — use `glGenerateMipmap` + sample top mip for CPU-readable summaries
- **No fragment shader branches** — Mali400 FS branches cost ~4.5ms even for trivial uniform `if`. Use branchless math or specialized programs.

### Shaders

Four shader programs:

1. **seed** (seed.vert/seed.frag) — GL_POINTS, maps entity world position to field UV, writes radial falloff per channel. Uses `step()` for channel selection (no branching).
2. **propagate** (propagate.vert/propagate.frag) — Fullscreen quad, 4-neighbor average with decay, conductance modulation from B channel. B channel preserved (does not propagate).
3. **readout** (readout.vert/readout.frag) — Optional GPU-side gradient computation into 1x1 FBO. Alternative: CPU-side Sobel after small `glReadPixels`.
4. **debug** (debug.vert/debug.frag) — Field visualization: R as red, G as green, agent position as white dot, movement vector as white line. PNG dump every N cycles.

## Bridge Events Consumed

The agent needs a subset of the 140+ event types. Key events for PoC:

| Event | Agent Use |
|-------|-----------|
| EntitySpawned | Add entity to field seeding list |
| EntityDespawned | Remove from seeding list |
| EntityMoved | Update entity position for next seed pass |
| EntityDied | Switch corpse from threat to goal (loot), trigger entity_died logic |
| PlayerPositionChanged | Update agent's own field-center position |
| PlayerHealthChanged | Health threshold for retreat decisions |
| CombatStateChanged | Toggle combat-mode field weighting |
| ZoneLoadComplete | Bake passability map, start agent loop |

## Intents Emitted

| Intent | Agent Use |
|--------|-----------|
| PlayerMoveToIntent | Navigate toward gradient direction |
| AttackIntent | Engage hostile target |
| StopAttackIntent | Disengage (retreat) |
| LootCorpseIntent | Loot dead entity |
| TargetEntityIntent | Select target before attacking |

## PoC Tests

All tests run against a live eqemu server.

### Test 1 — Gradient Navigation
- Agent navigates to a static goal point using field gradient
- Pass: reaches within 5 units of goal within 60 seconds
- World: one agent, one item/friendly NPC as goal, no hostiles, flat terrain

### Test 2 — Threat Awareness
- Agent stops pursuing goal when hostile enters range
- Pass: movement vector shifts toward threat within 2 decision cycles
- World: Test 1 setup + one hostile NPC approaching

### Test 3 — Basic Combat Loop
- Agent engages hostile, attacks until dead, retreats if health < 30%
- Pass: hostile dies, agent survives
- World: one evenly matched hostile in melee range

### Test 4 — Loot and Reset
- After combat, agent navigates to corpse, loots, then seeks next goal
- Pass: inventory gains loot item, agent moves to next target
- World: Test 3 + lootable corpse + second NPC further away

### Test 5 — Survival (Don't Die)
- Agent retreats to safe point when outnumbered
- Pass: agent reaches safe point with health > 0
- World: three hostiles converging, agent at 40% health, safe point 30 units away

## Implementation Order

### Phase 1: AgentBridge Class
1. Define `AgentBridge` class implementing the same interface as IrrlichtBridge/ConsoleBridge
2. Wire into Application — created alongside (not instead of) the renderer bridge when `--agent` flag is passed
3. Give it its own GameStateBridge pointer pair (or share the existing one with a fan-out)
4. Implement `applyEvent()` to build an internal world model (entity positions, health, combat state)
5. Implement intent emission for movement and combat commands
6. Stub the GPU field sim — just log events received and intents sent

### Phase 2: Agent Thread + EGL Context
1. Agent runs on its own thread (same pattern as render thread in Application)
2. Create third EGL shared context (pattern from GPUUploadThread)
3. Verify context works on both x86_64 (Mesa) and Orange Pi (Lima)
4. Benchmark: confirm agent GL calls don't measurably impact renderer FPS in manual mode

### Phase 3: Field Simulation
1. Create 512x512 RGBA8 ping-pong FBO pair
2. Implement seed shader — GL_POINTS entity seeding with radial falloff
3. Implement propagation shader — 4-neighbor average with decay
4. Implement passability bake from HCMap data, cache to disk
5. Implement gradient readout (CPU-side Sobel on small glReadPixels)
6. Debug visualization — PNG dump of field state

### Phase 4: Decision Logic
1. Gradient-to-intent mapping: direction → PlayerMoveToIntent
2. Threat/goal weighting based on health, combat state, entity count
3. State transitions: seek → combat → loot → seek, retreat override
4. Health threshold retreat logic

### Phase 5: PoC Tests 1-5
1. Run each test sequentially on eqemu server
2. Log all events received, intents sent, field state per cycle
3. Tune field parameters (decay rate, signal strength, propagation passes) per test results

## Future Vision (Not PoC Scope)

The agent will eventually "see" the rendered game world — the renderer's framebuffer output becomes an input texture to the field simulation. Perception fidelity would be proportional to the agent's in-game capabilities (insect sees low-res, humanoid sees high-res). This closes the loop between rendering and agent perception but is not needed for the PoC.

## Open Questions

- `GL_ALIASED_POINT_SIZE_RANGE` on Mali400 via Lima — query at startup, design influence radius to stay within range
- Bridge fan-out: does the agent get its own GameStateBridge, or do we add a multiplexer that copies events to both renderer and agent bridges?
- Agent cycle rate tuning — start at 100ms, profile GPU contention, adjust
- Passability bake resolution — 512x512 may be too coarse for narrow corridors in some zones
