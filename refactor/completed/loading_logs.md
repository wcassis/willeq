# Sequential Loader Logging Refactor

## Goal
Add DEBUG-level logging at every step of `loadZoneSequential()` in
`src/client/graphics/irrlicht_renderer.cpp` (lines ~3020-5002).

## Problem
The loading thread's log output was never appearing in glow1d4.log.
No `loadZoneSequential` messages at all — not even the INFO "starting for zone" line.
This makes it impossible to debug fire glow lighting, icosphere, and settings issues.

## Bug Context
- `enableFireGlowLighting` (default true in header) and `enableFireGlowIcospheres` (default false in header)
  are only set from display_settings.json via `applyEnvironmentalDisplaySettings()` which requires
  `windowManager_->getOptionsWindow()` to exist. If that returns null on the loading thread,
  the function returns early and defaults are used (icospheres=false despite JSON saying true).
- The `fireEffects` checkbox in the Options window controls `fireEffectsEnabled_` (particle flickering)
  but does NOT control `fireGlowLightingEnabled_` or `fireGlowIcospheresEnabled_`.
- Fire glow light collection is inside `if (particleManager_)` in Step 11 — need to verify
  this path runs and `fireSources` is populated.

## Steps & Logging Needed

All logs use `LOG_DEBUG(MOD_GRAPHICS, ...)`.

### Setup (line ~3036)
- [x] Log zone name, eqClientPath
- [x] Log state: particleManager_ null?, windowManager_ null?, zoneShader_ null?
- [x] Log fire glow flags: fireGlowLightingEnabled_, fireGlowIcospheresEnabled_, maxFireGlowLights_

### Step 1: S3D Parse (line ~3048)
- [x] Log zone path attempted
- [x] Log load options (loadCharacters, computeCombinedGeometry, loadObjects)
- [x] Log S3D result: success/fail, zone objects count, lights count, textures count

### Step 2: BSP Compute + Install (line ~3086)
- [x] Log wldLoader exists, bspTree exists, region count, hasPvsData
- [x] Log bounding boxes computed count
- [x] Log portal system: has portals, portal count, occlusion eligible
- [x] Log zone light regions count
- [x] Log deferred object entries count
- [x] Log prebuilt deferred objects count (after tree filtering)
- [x] BSP Install: log which path taken (no bsp, single mesh, pvs regions)
- [x] Log zoneBspTree_ installed, usePvsCulling_, regionBoundingBoxes_ count

### Step 3: Atlas (line ~3263)
- [x] Log enableAtlas, atlasPath, skipObjectBuild
- [x] Log zone atlas: preload valid?, page count, tile count
- [x] Log obj atlas: preload valid?, page count, tile count
- [x] Log shader page textures set

### Step 4: Regions (line ~3360)
- [x] Log total regions, regions built with meshes, front-to-back sorting enabled

### Step 5: Assets (line ~3464)
- [x] Log index builds (gfx, snd), subsystem creates

### Step 6: Objects (line ~3529)
- [x] Log total deferred objects, objects built, fire sources found from objectLights_
- [x] Log objectLights_ count, fire source count from objectLights_

### Step 7: Doors (line ~3809)
- [x] Log doors built count

### Step 8: Entities (line ~3863)
- [x] Log entity count, prep count, build count

### Step 9: Collision (line ~4228)
- [x] Log collision setup details

### Step 10: Sky (line ~4254)
- [x] Log sky type, fog params, weather setup

### Step 11: Env (line ~4503) **CRITICAL FOR FIRE GLOW**
- [x] Log treeManager_ exists, objects count
- [x] Log detailManager_ exists/created, detailObjectsEnabled
- [x] Log particleManager_ exists (gate for fire glow collection)
- [x] Log objectLights_ count
- [x] Log fire sources from objectLights_: count
- [x] Log currentZone_->lights count, fire sources from zone lights: count
- [x] Log total fireSources count before collection
- [x] Log fireGlowLights_ collected count
- [x] Log fireGlowIcospheresEnabled_ and whether icosphere build triggers
- [x] Log icosphere build result (VBO, IBO, program)
- [x] Log applyEnvironmentalDisplaySettings: windowManager_ null?, optionsWindow null?
- [x] Log after applyEnvironmentalDisplaySettings: fireGlowLightingEnabled_, fireGlowIcospheresEnabled_, maxFireGlowLights_

### Step 12: Lights (line ~4694)
- [x] Log zone light count, zoneLightData_ size

### Step 12b: SimulationWorker (line ~4733)
- [x] Log simulationWorker_ created
- [x] Log SimulationInput summary: entity count, particle fire enabled
- [x] Log SimulationOutput summary: region count, light count

### Step 13: Cleanup (line ~4978)
- [x] Log cleanup actions

### Completion (line ~5001)
- Already has INFO log with total time

## Progress
- [x] Step Setup + 1 + 2
- [x] Step 3 + 4 + 5
- [x] Step 6 + 7 + 8
- [x] Step 9 + 10 + 11
- [x] Step 12 + 12b + 13
- [ ] Build verification
