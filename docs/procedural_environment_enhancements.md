# Procedural Environment Enhancements

## Overview

This document explores procedural and programmatic graphical enhancements to add environmental life and immersion to the EverQuest client. These techniques build upon the existing detail object system (grass, plants, debris) and aim to make the world feel more alive through motion, ambient creatures, and reactive elements.

## Technical Context

### Current Infrastructure
- **Irrlicht Renderer**: Software rendering (no GPU shaders)
- **Detail System**: Chunk-based billboard rendering with wind animation
- **Wind Controller**: Already simulates wind direction/strength
- **Seasonal Controller**: Time-based environmental variation
- **Surface Maps**: Per-zone surface type detection (grass, sand, rock, water, etc.)

### Key Constraints
- No GPU shaders available (software rendering)
- Must maintain 30+ FPS on modest hardware
- EverQuest aesthetic should be preserved (enhanced, not replaced)
- Billboards are the primary rendering technique for small objects

---

## 1. Shoreline Waves

### Concept
Animated wave effects along the edges of water bodies - foam lines, gentle lapping motion, and occasional spray particles.

### Known Techniques

**A. UV-Scrolling Foam Lines** (Recommended)
The simplest approach uses a sine wave function on texture coordinates:
```
foam = sin(frequency * v + speed * time) + v
```
Where `v` is the distance from shore. This creates foam lines that wash up and fade as they recede. This technique was used in Far Cry, Assassin's Creed, and many classic games.

**B. Depth-Based Edge Detection**
Games like Far Cry detect where water meets terrain using depth comparisons and render foam/spray at the intersection. Since we don't have a depth buffer in software rendering, we'd need to pre-compute water edges from zone geometry.

**C. Animated Billboard Strips**
Place thin billboard strips along detected shorelines. Animate their UV coordinates to create wave motion. Multiple overlapping strips at different phases create natural variation.

### Implementation Approach
1. **Shoreline Detection**: During zone load, detect water/land boundaries from zone geometry or BSP regions
2. **Wave Strips**: Create billboard strips along shorelines, oriented perpendicular to the edge
3. **Animation**: Scroll UVs using sine functions with per-strip phase offsets
4. **Foam Particles**: Optional particle emitters at wave peaks for spray effect

### Complexity: Medium
### Visual Impact: High
### Performance Cost: Low-Medium

### References
- [Shoreline Shader Breakdown - Cyanilux](https://www.cyanilux.com/tutorials/shoreline-shader-breakdown/)
- [3D Water Effects - Game Developer](https://www.gamedeveloper.com/programming/3d-water-effects-here-s-what-s-involved-)
- [Deep Water Animation - Game Developer](https://www.gamedeveloper.com/programming/deep-water-animation-and-rendering)

---

## 2. Ambient Flying Creatures (Birds, Insects)

### Concept
Flocks of birds in the sky, swarms of insects near swamps/water, fireflies at night, bats in dungeons.

### Known Techniques

**A. Boids Algorithm** (Recommended)
Developed by Craig Reynolds in 1986, this is the industry standard for flocking behavior. Each "boid" follows three simple rules:
1. **Separation**: Steer away from nearby flockmates
2. **Alignment**: Match velocity with nearby flockmates
3. **Cohesion**: Steer toward center of nearby flockmates

This creates emergent flocking behavior without scripting individual paths. Used in Half-Life (1998) for flying creatures.

**B. Waypoint Paths with Variation**
Pre-defined flight paths with random perturbation. Simpler but less natural. Good for predictable "background" birds.

**C. Hybrid Approach**
Use boids for nearby creatures (interactive), waypoint paths for distant atmospheric birds (cheaper).

### Implementation Approach
1. **Creature Types**:
   - Birds (outdoor zones, daytime)
   - Bats (dungeons, nighttime outdoor)
   - Insects/flies (swamps, near corpses)
   - Fireflies (night, near water/forests)
   - Dragonflies (near water, daytime)

2. **Spawn Zones**: Define areas where creatures can spawn based on:
   - Zone biome (forest, swamp, desert)
   - Time of day
   - Indoor/outdoor
   - Proximity to water

3. **Rendering**: Billboard sprites with 2-4 frame wing animation
4. **LOD**: Reduce flock size or switch to simpler movement at distance

### Complexity: Medium
### Visual Impact: High
### Performance Cost: Low (billboard sprites, simple math per creature)

### References
- [Boids - Craig Reynolds](https://www.red3d.com/cwr/boids/)
- [Boids - Wikipedia](https://en.wikipedia.org/wiki/Boids)
- [AI for Game Developers - Flocking](https://www.oreilly.com/library/view/ai-for-game/0596005555/ch04.html)

---

## 3. Building Vegetation (Vines, Flowers, Moss)

### Concept
Procedurally place vegetation on building surfaces - ivy climbing walls, moss on old stones, flowers in window boxes, vines hanging from overhangs.

### Known Techniques

**A. Surface-Based Procedural Placement** (Recommended)
Similar to the detail object system but for vertical/angled surfaces:
1. Identify building surfaces from zone geometry
2. Classify surfaces (wall, roof, overhang, corner)
3. Place appropriate vegetation based on surface type and biome

**B. Growth Simulation**
Simulate ivy "growing" from ground up walls using pathfinding on surfaces. More complex but creates natural-looking coverage patterns. Tools like "Crazy Ivy" (Unreal) use this approach.

**C. Pre-authored Attachment Points**
Define attachment points in zone data where vegetation can spawn. Most control but requires per-zone setup.

### Implementation Approach
1. **Surface Classification**: Extend surface map system to identify vertical surfaces
2. **Placement Rules**:
   - Ivy: Starts from ground, grows upward on walls
   - Moss: North-facing surfaces, damp areas, old structures
   - Hanging vines: Overhangs, arches, tree branches
   - Flowers: Window ledges, planters, ground-wall junctions
3. **Biome Matching**: Only place vegetation appropriate to zone climate
4. **Age/Decay Factor**: Ruins get more vegetation than maintained buildings

### Complexity: Medium-High
### Visual Impact: Medium-High
### Performance Cost: Low (static billboards after placement)

### References
- [Vegetation Creation Techniques - 80 Level](https://80.lv/articles/vegetation-creation-techniques-for-video-games)
- [Procedural Vegetation Placement UE5 - 80 Level](https://80.lv/articles/this-procedural-tool-grows-vegetation-on-any-visible-surface)
- [Foliage - Polycount Wiki](http://wiki.polycount.com/wiki/Foliage)

---

## 4. Environmental Particle Effects

### Concept
Atmospheric particles that add depth and mood - dust motes in light beams, pollen in forests, ash near volcanoes, snow flurries, mist in swamps.

### Known Techniques

**A. Billboard Particle Systems** (Standard Approach)
The classic technique dating back to early 3D games:
- Spawn particles in defined volumes
- Apply simple physics (gravity, wind drift)
- Render as camera-facing billboards
- Fade based on lifetime

**B. Volumetric Zones**
Define 3D volumes where particles exist. Particles only render when camera is inside or near the volume. Good for localized effects (light beams, fog patches).

**C. Weather-Tied Systems**
Particles that respond to zone weather state:
- Rain → water droplets, puddle ripples
- Wind → leaves, dust clouds
- Clear → pollen, butterflies

### Implementation Approach
1. **Particle Types by Biome**:
   - Forest: Pollen, floating seeds, leaves, fireflies (night)
   - Swamp: Mist, insects, marsh gas bubbles
   - Desert: Dust, sand particles, heat shimmer (if possible)
   - Snow: Snowflakes, ice crystals, breath vapor
   - Dungeon: Dust motes, cobweb strands, dripping water
   - Volcanic: Ash, embers, smoke wisps

2. **Light Interaction**: Particles near light sources appear brighter (simulate light scattering)
3. **Wind Response**: Tie particle drift to existing wind controller
4. **Density Control**: User setting to adjust particle density for performance

### Complexity: Low-Medium
### Visual Impact: High (adds atmosphere)
### Performance Cost: Low-Medium (depends on particle count)

### References
- [Particle System - Wikipedia](https://en.wikipedia.org/wiki/Particle_system)
- [Creating Dust Particles - Unity Tutorial](https://dennisse-pd.medium.com/creating-dust-particles-in-unity-3d-f8a59d871555)
- [Atmospheric Effects - Creative Shrimp](https://www.creativeshrimp.com/lighting-tutorial-atmospheric-effects-dust-book-10.html)

---

## 5. Reactive Foliage (Grass/Plant Deformation)

### Concept
Vegetation responds to entity movement - grass bends as NPCs walk through, bushes rustle when disturbed, leaves scatter.

### Known Techniques

**A. Radial Displacement** (Recommended for Software Rendering)
For each entity near vegetation:
1. Calculate distance from entity to each vegetation billboard
2. Apply outward displacement based on inverse distance
3. Blend displacement with wind animation

This is the standard technique and works well without shaders.

**B. Render Target Method** (Requires GPU)
Paint entity positions to a render target texture, sample in vertex shader to displace grass. Not suitable for software rendering.

**C. Influence Grid**
Maintain a low-resolution grid tracking "disturbance" values. Entities write to grid, vegetation reads from grid. Efficient for many entities.

### Implementation Approach
1. **Disturbance Sources**:
   - Player character
   - NPCs and mobs
   - Pets
   - Projectiles (optional)

2. **Response Types**:
   - Grass: Bend away from movement direction
   - Tall plants: Sway with delayed return
   - Bushes: Rustle animation trigger

3. **Recovery**: Vegetation gradually returns to neutral state
4. **Audio Feedback**: Optional rustling sounds when disturbed

### Complexity: Medium
### Visual Impact: Very High (world feels reactive)
### Performance Cost: Medium (distance checks per frame)

### References
- [Interactive Grass - Alan Zucconi](https://www.alanzucconi.com/2018/07/28/shader-showcase-saturday-3/)
- [Dynamic Grass Ecosystem - Medium](https://medium.com/gametextures/dynamic-grass-builds-an-ecosystem-in-your-game-that-is-alive-and-reactive-f71b685f93fb)
- [Interactive Grass Tutorial - 80 Level](https://80.lv/articles/tutorial-interactive-grass-in-unreal)

---

## 6. Rolling/Tumbling Objects

### Concept
Wind-driven objects that roll across terrain - tumbleweeds in deserts, rolling leaves in autumn, bouncing debris in storms.

### Known Techniques

**A. Physics-Lite Simulation** (Recommended)
Simplified physics for rolling objects:
- Apply wind force as acceleration
- Raycast to keep object on terrain
- Simple collision with major obstacles
- Despawn when out of view or after distance traveled

**B. Pre-computed Paths**
Define paths tumbleweeds can follow, randomly select and add variation. Less realistic but guaranteed not to get stuck.

**C. Full Rigid Body Physics**
Use actual physics simulation. Most realistic but potentially expensive and objects can get stuck in geometry.

### Implementation Approach
1. **Object Types**:
   - Tumbleweeds (desert zones)
   - Fallen leaves (autumn forests)
   - Paper/debris (urban areas)
   - Snow clumps (blizzard conditions)

2. **Spawn System**:
   - Spawn at zone edges upwind
   - Maximum active count per zone
   - Despawn when far from player or past zone bounds

3. **Movement**:
   - Base velocity from wind direction/strength
   - Random lateral wobble
   - Terrain following via raycast
   - Rotation matches movement speed

4. **Visual**: Billboard or simple low-poly mesh with rotation

### Complexity: Medium
### Visual Impact: Medium (adds life to open areas)
### Performance Cost: Low (few active objects at a time)

### References
- [Wind Simulation - GameDev.net](https://www.gamedev.net/forums/topic/188457-how-do-i-simulate-wind/)
- [TumbleWeed Game - Itch.io](https://nicolasmey.itch.io/tumbleweed)

---

## 7. Water Surface Effects

### Concept
Beyond shoreline waves - ripples from rain, fish jumping, water striders, lily pad motion.

### Known Techniques

**A. Animated Normal Maps**
Scroll normal map textures across water surface to create wave motion. Already partially implemented in many engines.

**B. Procedural Ripples**
Spawn circular ripple textures at points of disturbance (rain drops, entity entry, fish activity). Ripples expand and fade.

**C. Floating Object Animation**
Lily pads, logs, and debris that bob and drift with wave motion. Simple sine-based vertical animation plus slow drift.

### Implementation Approach
1. **Rain Ripples**: Spawn ripple billboards on water surface during rain
2. **Fish Splashes**: Occasional splash effects on water surface
3. **Floating Debris**: Animated billboard objects that drift slowly
4. **Water Striders**: Small insects that skate across water surface

### Complexity: Low-Medium
### Visual Impact: Medium
### Performance Cost: Low

---

## Priority Recommendations

Based on visual impact, implementation complexity, and synergy with existing systems:

### Phase 1: High Impact, Lower Complexity
1. **Environmental Particles** - Immediate atmosphere improvement
2. **Ambient Creatures (Boids)** - Brings sky and swamps to life
3. **Shoreline Waves** - Major visual upgrade for water areas

### Phase 2: Medium Complexity, High Polish
4. **Reactive Foliage** - Makes the detail system interactive
5. **Rolling Objects** - Dynamic environmental motion
6. **Water Surface Effects** - Completes water environment

### Phase 3: Higher Complexity
7. **Building Vegetation** - Requires surface classification work

---

## Architecture Considerations

### Shared Systems
Several features can share infrastructure:

1. **Particle Manager**: Unified system for all particle effects (dust, rain, fireflies, etc.)
2. **Wind Integration**: All motion effects should respond to the existing wind controller
3. **Biome Awareness**: Use surface maps to determine appropriate effects per location
4. **LOD System**: Reduce/disable effects at distance for performance

### Suggested Module Structure
```
include/client/graphics/environment/
├── particle_manager.h      # Unified particle system
├── particle_emitter.h      # Individual emitter types
├── boid_flock.h            # Flocking creature system
├── shoreline_waves.h       # Water edge effects
├── reactive_foliage.h      # Entity-responsive vegetation
├── rolling_object.h        # Wind-driven rolling objects
└── environment_config.h    # Per-zone effect configuration
```

### Performance Budget
Suggested limits per frame:
- Max 500 particles active
- Max 50 boid creatures active
- Max 10 rolling objects active
- Reactive foliage checks only for nearby detail chunks

---

## Classic Game References

### EverQuest Original
- Weather effects (rain, snow)
- Zone-specific ambient sounds
- Spell particle effects

### World of Warcraft Classic
- Strong weather systems that affect visibility
- Predator/prey animal behaviors
- Detailed environment sounds per zone

### Half-Life (1998)
- Boid flocking for flying creatures (ichthyosaurs, boids)
- Environmental particles (dust, sparks)

---

## Questions for Discussion

1. **Performance Target**: What's the minimum acceptable FPS? This affects particle counts and creature numbers.

2. **Effect Density**: Should all zones have all applicable effects, or should we start with select "showcase" zones?

3. **User Control**: Should effects have individual toggles, or just a global "environment effects" quality slider?

4. **Sound Integration**: Should reactive foliage, waves, and creatures trigger sound effects?

5. **Seasonal Variation**: Should effects change with the seasonal controller (e.g., more fireflies in summer)?

6. **Priority Override**: Any particular effect you'd like to see first?

---

## Conclusion

These enhancements would transform static zones into living environments. The key insight from researching classic techniques is that simple, well-tuned effects often outperform complex simulations. Billboard sprites, sine-wave animations, and the boids algorithm have stood the test of time precisely because they're efficient and effective.

The existing detail system provides excellent infrastructure - the wind controller, seasonal system, and surface maps can all be leveraged for these new features. Starting with environmental particles and ambient creatures would provide the highest visual impact for moderate implementation effort.
