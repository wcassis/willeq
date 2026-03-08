# Environmental Interaction Effects — Design Document

## Overview

A unified system for interaction-driven visual effects and ambient creatures that replace ambient noise particles and per-node rendering with physically-motivated effects tied to game events, surface types, and weather. All effects use zero scene nodes, compute on the SimulationWorker thread, and render via a shared batched quad renderer in 1-2 draw calls per frame.

### Design Principles

1. **Every effect has a source.** Dust comes from feet churning dirt. Leaves come from trees. Snow drifts against walls. Nothing spawns in a cylinder around the player.
2. **Polygon headroom, not node headroom.** Typical scenes have 8-12K polys against an 80K budget but ~300-400 active scene nodes. All new effects must avoid adding scene nodes. Use batched rendering (single draw call for all quads/particles).
3. **Interaction over ambience.** Effects are caused by movement, combat, weather, and spells — not random emission. A still scene should be still.
4. **Settled debris tells a story.** Particles that land on the ground persist within budget, giving scenes visual history (fight debris, fallen leaves, snow cover).

### Performance Envelope (Orange Pi One, Mali 400)

- Frame budget: 33.3ms (30 FPS target)
- Current usage: ~17ms average, ~16ms headroom
- Typical scene: 8-12K polys, <10 entities, 23 nodes drawn
- GPU is idle most of the frame — bottleneck is draw call overhead, not vertex/fragment work
- Target: all new effects within 1-2ms total (batched renderer + worker thread physics)

---

## Foundation Systems

### 1. Ground Disturbance Accumulator

A spatial grid that tracks sustained ground disturbance intensity per cell. This is the core system that drives footstep/movement dust.

**Data structure:** Grid of cells (1-unit resolution), each storing:
- `float intensity` — accumulated disturbance (0.0 = undisturbed, 1.0+ = active dust emission)
- `float decayRate` — per-second drain (surface-dependent)
- `SurfaceType surfaceType` — cached from SurfaceMap at cell center

**Accumulation rules:**
- Each foot contact adds intensity to the cell at that position
- Walking through an area: low accumulation, drains faster than it builds, no visible effect
- Running: moderate accumulation, light trail possible
- Combat repositioning (strafing repeatedly in same area): same cells hit repeatedly, intensity builds past emission threshold
- Standing still swinging: no foot movement, no accumulation, no dust regardless of combat state
- Fall/landing impact: single-frame high-intensity spike proportional to fall velocity

**Surface-type thresholds (intensity required before particles emit):**
- Sand: 0.2 (loose, kicks up easily)
- Dirt: 0.5
- Swamp/mud: 0.1 (wet surfaces react immediately, different effect — splatter not dust)
- Grass: 0.8 (mostly absorbed, only heavy activity produces visible effect)
- Snow: 0.3 (powder kicks up moderately easily)
- Stone/brick/wood: never (hard surfaces don't produce dust)

**Decay:** Intensity drains per second at a surface-dependent rate. When player leaves an area, disturbance fades over ~1-2 seconds. A camp spot with heavy fighting maintains elevated intensity from constant foot traffic.

**Particle emission:** Cells above threshold feed particle spawn rate to the unified particle system. Emission rate is proportional to `(intensity - threshold)`. Particles spawn at the cell's world position (ground-anchored, not player-anchored).

**Relationship to existing systems:** Follows the same pattern as FoliageDisturbanceManager (grid-based, residual state, fade over time) but stores scalar intensity rather than displacement direction. Could share or extend the same grid infrastructure.

### 2. Batched Quad Renderer

A single mesh buffer rebuilt each frame containing all active quads from every visual subsystem, submitted in 1-2 draw calls (one per blend mode). This is the universal renderer for all small visual objects that should not be scene nodes.

Implements the existing `BATCHED_QUAD` enum in the unified particle system's `UnifiedRendererType`.

**Per-quad data:**
- Position (world space, Y-up)
- Size
- Rotation angle (for tumbling shards, rolling tumbleweeds, creature facing)
- UV coordinates (atlas region)
- Color + alpha
- State flag: active (physics-driven) or settled (resting)

**Consumers (all rendered in the same 1-2 draw calls):**

| Consumer | Quad type | Current rendering | Problem |
|----------|-----------|-------------------|---------|
| Debris/shards | Spinning/tumbling quads | N/A (new) | — |
| Settled debris | Static ground quads | N/A (new) | — |
| Fallen leaves | Tumbling then settled | N/A (new) | — |
| Snow cover | Static ground quads | N/A (new) | — |
| Rain puddles | Static ground quads | N/A (new) | — |
| Wet surface overlays | Static ground quads | N/A (new) | — |
| **Boids** | Camera-facing billboards | 1 draw call per creature | 80 creatures = 80 draws |
| **Tumbleweeds** | 3-plane cross mesh | 1 scene node per instance | 10 nodes + material setup each |
| **Ground critters** | Camera-facing or oriented quads | N/A (new) | — |

**Rendering:** Single `glDrawElements` call for all quads per blend mode. Material-sorted (atlas texture + blend mode). Zero scene nodes. Replaces per-node rendering for boids, tumbleweeds, and all new visual object types.

**Cost model:** 50 quads = 200 triangles = 1 draw call = ~0.1ms on Mali 400. 500 quads = 2000 triangles = 1 draw call = ~0.3ms. At typical scene loads (8-12K polys), hundreds of quads are within budget.

**Migration path for existing systems:**
- **Boids:** SimulationWorker already computes flocking physics. Replace per-creature `drawVertexPrimitiveList()` with batch submission. Same simulation, different render path. Recovers 3-5ms at 80 creatures.
- **Tumbleweeds:** SimulationWorker already computes wind physics + ground following. Replace per-instance `IMeshSceneNode` with batch submission. Same simulation, different render path. Eliminates 10 scene nodes.

### 3. Tiered Priority Budget

Particle budget allocated by proximity/relevance tiers. Simple arithmetic — each tier gets a slice; when higher tiers consume more, lower tiers get squeezed.

| Tier | Scope | Priority | Examples |
|------|-------|----------|----------|
| 0 | Player | Always full | Own footstep dust, landing impacts, own spell casts |
| 1 | Target + group | High | Combat hit effects on target, group member movement |
| 2 | Nearby combat | Medium | NPC death bursts, nearby entity combat effects |
| 3 | Background/ambient | Low, first cut | Corpse debris, leaf fall, snow accumulation, critters, boids |

When Tier 0+1 consume most of the active particle budget (e.g., intense fight), Tier 2+3 get reduced spawn rates. When combat ends and Tier 0+1 usage drops, background effects recover.

**Settled debris has a separate, larger budget** since it costs only draw triangles (no per-frame physics). FIFO eviction within categories — oldest debris fades first. Combat start can trigger more aggressive settled debris fade rates to free visual headroom.

### 4. Active → Settled Particle Lifecycle

All ground-contact effects share a two-phase lifecycle:

**Phase 1 — Active:** Physics-driven particle. Tumbling leaf, flying shard, splashing mud, drifting snow. Consumes worker thread time for position/velocity updates. Competes in tiered active particle budget.

**Phase 2 — Settled:** Resting on ground. No physics updates. Exists as a static quad in the batched draw buffer. Near-zero per-frame cost (just included in the single batched draw call). Evicted by FIFO within its category or by budget pressure from higher tiers.

**Transition trigger:** Velocity drops below threshold → snap to ground height from SurfaceMap → stop physics updates → move to settled pool.

**Surface interactions for settled particles:**
- Water surface: don't settle — drift slowly and fade (leaves, debris)
- Stone/hard surfaces: settle but may re-enter active if wind picks up (leaves only)
- Soft surfaces (dirt, grass, snow): settle and persist until evicted

---

## Interaction-Driven Effects

### 5. Footstep Dust and Surface Spray

**Trigger:** Ground disturbance accumulator exceeds surface-type threshold at foot contact position.

**Surface-type effects:**
- **Dirt/sand:** Dust puff — small RADIAL_EXPAND burst (8-12 point sprite particles), tan/brown color, upward drift, moderate drag. Intensity scales particle count and spread.
- **Sand (windy):** Lower threshold than dirt. Sand particles hug the ground (low initial Y velocity, high drag), move with wind direction.
- **Swamp/mud:** Immediate splatter per step (low threshold). Upward BURST (4-6 particles), dark brown, high drag, fast settle. Settled mud splatter quads on nearby ground.
- **Snow:** Powder spray. White/blue-white particles, low gravity, drift with wind. Light and floaty compared to dirt.
- **Grass:** Very high threshold. Only heavy combat repositioning produces a faint dust effect.
- **Stone/brick/wood:** No dust. Ever.

**Data sources:** Footprint system provides foot position, heading, left/right alternation. SurfaceMap provides surface type. Ground disturbance accumulator provides intensity. Wind controller provides direction for particle drift.

### 6. Fall and Landing Impact

**Trigger:** Player Y-velocity exceeds threshold on ground contact.

**Effect:** Single-frame high-intensity burst at landing position. Particle count and spread proportional to fall velocity. Surface-type-aware (dust on dirt, powder on snow, splash on water/swamp, nothing on stone). Uses ground disturbance accumulator — landing is a one-frame intensity spike, no sustained contact needed.

**Data sources:** Player velocity (already tracked for movement), ground height from SurfaceMap.

### 7. Water Entry/Exit Splash

**Trigger:** Player crosses water surface boundary (entering or exiting water).

**Effect:** Directional splash burst at water surface plane. Entry angle determines splash direction (fast dive = forward spray, gentle wade = symmetric ring). Water surface height already computed for swimming state transitions and water sounds.

**Data sources:** Player position/velocity, water surface height (from swimming state system), entry angle from velocity vector.

### 8. Combat Hit Debris

**Trigger:** Melee hit, critical hit, parry/block/riposte events from the packet stream.

**Effects by event type:**
- **Melee hit:** Small burst toward attacker (3-5 shards/particles). Debris type from target race: bone fragments (undead/skeleton), metal chips (armored humanoid), organic matter (animals/beasts), cloth scraps (casters). Uses batched quad renderer for visible shards.
- **Critical hit:** Larger burst, more shards, wider spread.
- **Parry/block/riposte:** Spark burst (additive blend, short-lived). Metal-on-metal aesthetic. Directional — between attacker and defender positions.
- **NPC death:** Large burst at corpse position. Race/class-aware debris type. One-time effect, justified by rarity.

**Debris categories (small set of atlas regions, mapped from race/class):**
- Metal shards (armored NPCs, weapon impacts)
- Bone fragments (undead, skeletons)
- Cloth scraps (caster NPCs)
- Organic matter (animals, insects)
- Dust/dirt burst (generic fallback)

**Corpse debris persistence:** Death burst spawns 3-5 settled quad shards around corpse position. Persist until corpse is looted or despawns. Tier 3 priority — first to fade when budget is tight. Tells visual story of a camp spot (scattered debris around corpse pile).

### 9. Spell Impact Effects

**Trigger:** Spell lands on target (hit or resist).

**Effects by resist type:**
- **Fire:** Ember shards + orange/red particles, upward drift, additive blend
- **Cold:** Ice crystal shards, blue/white, slow tumble downward
- **Magic:** Arcane fragments, purple, brief orbital motion then fade
- **Poison:** Droplet splatter, green, downward, fast drag
- **Disease:** Spore puff, sickly yellow-green, slow radial expand, high drag

**Spell resist:** Fizzle particles at target — spell energy dissipating. Smaller/dimmer version of the hit effect.

**Data sources:** Spell resist type already in spell data. Caster/target positions already tracked. Spell effect system already triggers particle commands via the SimulationWorker command queue.

---

## Weather-Driven Effects

### 10. Leaf Detachment from Trees

**Trigger:** Wind strength exceeds threshold near a classified tree mesh.

**Behavior:** Leaves spawn near the top of a tree's bounding box. Initial velocity inherits wind direction. Drift/tumble downward with drag and rotation (batched quads, not point sprites — leaves have visible shape and tumble). Spawn rate proportional to wind strength with randomness. Calm day: rare single leaf. Windy day: several per tree.

**Settling:** Leaves land on ground and enter settled pool. On water, they drift slowly and fade. On stone, they may re-enter active pool if wind strengthens (blown along ground). On soft surfaces, they persist until evicted.

**Data sources:** AnimatedTreeManager already classifies tree meshes and knows positions/bounds. Wind controller provides strength/direction. Biome classification determines whether trees have detachable leaves (forest yes, desert no).

**Note:** No season data from server. Leaf fall is wind-driven and biome-driven, not seasonal. The detail system's `/season` command is a manual override only.

### 11. Snow Accumulation and Drifting

**Trigger:** Server sends weather type=2 (snow on) with intensity 1-10.

**Accumulation:** When snow particles hit the ground, they contribute to settled snow quads at that grid cell. Quads start small and grow as more particles land. Growth rate proportional to weather intensity.

**Wind-geometry drifting:** Snow accumulates faster on the downwind side of vertical surfaces (walls, buildings). Calculation: dot product between wind direction and nearby geometry normals. Cells sheltered from wind (geometry between cell and wind source) accumulate faster. Exposed cells accumulate slower or get blown clear.

**Height cap:** Maximum ankle height (~0.5 EQ units). Purely visual — no collision mesh changes, no navmesh updates, no entity pathing impact.

**Intensity mapping:**
- Intensity 1-3: light dusting, thin coverage on sheltered spots only
- Intensity 4-6: moderate coverage, visible drifts against walls
- Intensity 7-10: heavy coverage, drifts at max height, broad ground coverage

**Melt/fade:** When server sends snow off (type=1), accumulation stops. Existing snow fades gradually over time (configurable melt rate). Sheltered snow persists longer than exposed snow.

**Surface interaction:** Snow settles on all surfaces except water and lava. EQ zone geometry is flat 2D triangles — snow quads sit 0.05 units above the SurfaceMap height at each grid cell, no z-fighting with flat geometry.

### 12. Rain Puddles and Wet Surfaces

**Trigger:** Server sends weather type=0 with intensity > 0 (rain on).

**Puddle formation:** Rain pools in local minima of the SurfaceMap height grid. These are real geometric depressions in the zone floor (height data perfectly matches zone geometry). Puddle quads are flat, reflective planes at ground height. Size grows with rain duration/intensity.

**Puddle rendering:** Flat quad with subtle specular highlight from existing light sources (one extra dot product in fragment shader for puddle material). Slight blue-gray tint.

**Wet surface darkening:** On non-pooling surfaces (slopes, stone, elevated areas), rain applies a darkening tint overlay as settled quads. Simulates wet surface appearance without geometry change.

**Intensity mapping:**
- Intensity 1-3: small puddles in deepest depressions only, slight surface darkening
- Intensity 4-6: larger puddles, moderate wet surface coverage
- Intensity 7-10: large connected puddle areas, heavy surface darkening

**Drying:** When server sends rain off (type=0, intensity=0), puddles shrink gradually. Wet surface overlays fade. Sheltered areas (under overhangs, indoors) were never wet to begin with (covered by geometry above — same wind-shelter calculation as snow drifting).

---

## Ambient Creatures

### 13. Ground Critters

Small, untargetable, visual-only creatures that interact with geometry and respond to light. No server interaction, no spawn IDs — purely client-side decoration. Rendered through the batched quad renderer (zero scene nodes).

**Time-of-day behavior:**

**Night / dark scenes (dungeons, caves, outdoor night):**
Creatures that flee the player light. Only simulated near the light boundary — creatures in full darkness are invisible (no point rendering), creatures in full light have already fled. Active simulation zone is a thin ring around the light radius (radius ± ~10 units). When the player moves, new creatures appear at the leading edge and flee at the trailing edge. Typically 10-20 active critters at once.

**Day / outdoor:**
Small ground creatures that exist in the environment independently. Ants along a path, butterflies near flowers, squirrels on tree trunks, beetles in grass. Density driven by biome and surface type.

**Density gradient (biome + surface type):**

| Environment | Density | Creature types |
|-------------|---------|----------------|
| Deep forest (grass + trees) | High | Ants, beetles, squirrels on trees, insects near flowers |
| Forest edge / plains | Medium | Occasional insects, mice in grass |
| Urban / city edge | Very low | Rare rat, single ant trail along wall |
| Stone / brick / indoor | Minimal | Spider in corner, cockroach fleeing light |
| Desert | Low | Lizard, scorpion (shelter under rocks in heat) |
| Swamp | Medium-high | Frogs near water, insects, snakes |
| Cave / dungeon | Night rules always | Rats, spiders, bats (ground-based) |

**Movement patterns (short, predictable, no pathfinding needed):**
- **Scurry:** Point A to point B along ground (rat, mouse, lizard). Sample SurfaceMap height at intervals along a straight line. 1-2 seconds, despawn at destination.
- **Trail:** Follow a fixed path (ant line along wall edge). Precomputed at zone load from geometry features.
- **Climb:** Move up a tree trunk (squirrel). Start at tree base from AnimatedTreeManager, move upward to canopy. Simple vertical lerp.
- **Flutter:** Short erratic ground-level hops (insects). Random displacement within small radius, SurfaceMap height snap each hop.
- **Flee:** Move directly away from light source. Direction = normalize(critter_pos - light_pos). Sample height along flee direction. Fade out past light boundary + margin.

**Surface following:** Uses the same ground-following approach as the tumbleweed system — `HCMap::FindBestZ()` for exact ground height, `HCMap::CheckLOSWithHit()` for wall collision. Physics runs on SimulationWorker. Per-critter cost: ~40-60 float ops + 2 spatial queries per frame (same as tumbleweed).

**Wall-following for rats/ants:** `HCMap::CheckLOSWithHit()` cast in a few horizontal directions at spawn time to find the nearest wall. Critter path follows the wall at a fixed offset. Simple and sufficient for short-lived scurry animations.

**Tier 3 budget:** Ground critters are background ambience — first to be culled when combat effects need budget. During active combat, critter spawn rate drops to zero and existing critters complete their current path and despawn normally.

---

## Architecture

### Rendering Path

All effects render through two paths, both zero scene nodes:

1. **Point sprites (existing):** GL_POINTS via UnifiedParticleRenderer. 1-2 draw calls total (additive + alpha blend). Used for: dust particles, spray, sparks, small fire/spell particles.

2. **Batched quads (new, replaces per-node rendering):** Single mesh buffer rebuilt each frame. 1-2 draw calls total. Consolidates rendering for: spinning debris shards, tumbling leaves, settled debris, snow cover quads, puddle quads, wet surface overlays, boids, tumbleweeds, and ground critters. Implements existing `BATCHED_QUAD` enum in `UnifiedRendererType`.

### Compute Path

All physics computed on SimulationWorker thread (parallel with main render):

- Ground disturbance accumulator: grid update from foot positions
- Active particle physics: position/velocity/rotation integration
- Active→settled transitions: velocity threshold check, ground snap
- Settled pool management: FIFO eviction, budget enforcement
- Weather accumulation: snow/rain grid updates from weather state
- Boid flocking: existing simulation, unchanged
- Tumbleweed physics: existing wind + ground following, unchanged
- Ground critter movement: scurry/trail/climb/flee paths with HCMap ground following

Main thread only: reads render buffer snapshot, submits draw calls.

### Command Flow

Same pattern as existing spell/fire particle commands:

1. Main thread detects game event (foot contact, combat hit, spell impact, weather change)
2. Enqueues `ParticleCommandData` to SimulationWorker (lock-free queue)
3. Worker drains commands, updates accumulator/emitters/settled pool
4. Worker writes render buffer (active particles + settled quads + critters + boids + tumbleweeds)
5. Main thread reads snapshot, submits 1-2 draw calls via batched quad renderer

### Existing Systems Used

| System | Role in this design |
|--------|-------------------|
| SurfaceMap | O(1) surface type + ground height lookup |
| HCMap | Exact ground height (FindBestZ), wall collision (CheckLOSWithHit) for critters/tumbleweeds |
| FootprintManager | Foot position, heading, left/right, surface type per step |
| FoliageDisturbanceManager | Pattern for grid-based residual state with fade (extend or parallel) |
| WindController | Wind strength/direction for drift, leaf detachment, snow drifting |
| UnifiedParticleRenderer | Point sprite rendering (existing), batched quad rendering (new path) |
| SimulationWorker | All physics computation (existing thread, existing command queue) |
| AnimatedTreeManager | Tree positions/bounds for leaf detachment and squirrel spawning |
| ParticleManager | Command queue facade (existing API pattern) |
| Entity/spawn data | Race, class, equipment for combat debris type selection |
| Spell data | Resist type for spell impact effect selection |
| Weather packets | Type + intensity for snow/rain accumulation |
| Player light radius | 20-100 units (already tracked per-frame) for critter flee behavior |

### Data Available from Server (no season)

| Data | Source | Update frequency |
|------|--------|-----------------|
| Weather type (rain/snow/off) | OP_Weather packet | On change |
| Weather intensity (1-10) | OP_Weather packet | On change |
| Time of day (hour, minute) | OP_TimeOfDay packet | Periodic |
| Day, month, year | OP_TimeOfDay packet | Periodic |
| Zone biome | Client-side classification (static per zone) | Once at zone load |
| Wind | Client-side simulation | Continuous |

No reliable season information from server. Visual effects are driven by weather packets, wind state, zone biome, and time of day — not season.

---

## Budget Summary

**Active particle budget (shared with existing fire/weather/spell):**
- Tiered allocation: Tier 0 (player) → Tier 3 (background)
- Higher tiers squeeze lower tiers dynamically
- Worker thread physics cost scales with active count

**Settled debris budget (new, separate):**
- Single batched draw call regardless of count
- FIFO eviction within categories (oldest first)
- Categories: combat debris, leaves, snow cover, puddles, wet overlays, corpse debris
- Combat start triggers aggressive fade on lowest-priority settled debris
- Typical steady state: 50-200 settled quads = 200-800 triangles = ~0.1-0.2ms

**Batched quad renderer total (all consumers combined):**
- Boids (migrated): up to 80 billboard quads
- Tumbleweeds (migrated): up to 10 cross-mesh quads
- Ground critters: up to 20-50 quads
- Active debris/leaves/shards: up to 50 quads
- Settled debris/snow/puddles: up to 200 quads
- **Worst case total: ~400 quads = 1600 triangles = 1-2 draw calls = ~0.3-0.5ms**
- vs current boids alone: 80 draw calls = 3-5ms

**Total rendering cost target:** 1-2ms for all environmental interaction effects combined (point sprites + batched quads + accumulator reads). Well within the ~16ms headroom available. Migrating boids and tumbleweeds to the batched renderer recovers 3-5ms of existing cost, making the net impact of the entire system potentially **negative** (faster than current state).
