# Irrlicht 1.8.5 — Draw Call Reduction Handoff

## Context

This project uses **Irrlicht 1.8.5**. Performance is degraded by excessive draw calls from terrain and entity rendering. This document outlines every viable mitigation strategy, ordered by priority. Irrlicht 1.8.5 has no native instancing or automatic batching — solutions range from using its limited built-in tools correctly to writing custom scene nodes that bypass the scene graph entirely.

---

## Root Cause Summary

Irrlicht's scene manager issues one draw call per scene node by default. With large numbers of terrain chunks or entity nodes, this overhead compounds fast. The engine also has no built-in draw call sorting beyond frustum culling, so material/texture switches between calls add further GPU stall cost.

---

## Strategy 1 — Static Mesh Merging (Do This First)

**Impact: High | Effort: Low**

Any geometry that does not move should be merged into as few meshes as possible before being added to the scene. One `IMeshSceneNode` = one draw call.

**Rule: group by material/texture before merging.** Merging meshes that use different materials produces no benefit and may make things worse.

Do this either in your asset pipeline (Blender, etc.) or at runtime using Irrlicht's `IMeshManipulator`:

```cpp
scene::IMeshManipulator* manip = smgr->getMeshManipulator();

// Collect SMesh objects that share the same material, then build
// one combined SMesh manually before calling addMeshSceneNode().
// The fewer scene nodes added to the scene manager, the better.
```

For static world geometry (buildings, rocks, props, ground cover), target **one node per material** across entire sectors or chunks. This alone can cut draw calls by an order of magnitude on typical scenes.

---

## Strategy 2 — Texture Atlases

**Impact: High | Effort: Medium**

Even after merging meshes, if merged geometry spans multiple textures you still get one draw call per material switch. Consolidate textures into atlases so that large swaths of merged geometry share a single material.

- Pack related textures (terrain tiles, prop variants, entity skins) into a single atlas texture
- Remap UVs at asset build time or on import
- One atlas → one material → one draw call for all geometry using it

This pairs directly with Strategy 1. Merge first, atlas second — together they are the highest-leverage combination available in Irrlicht 1.8.5.

---

## Strategy 3 — Custom Batch Scene Node for Entities

**Impact: Very High | Effort: High**

For dynamic or repeated entities (characters, projectiles, pickups, foliage), the correct solution is a single custom `ISceneNode` subclass that owns one large `SMeshBuffer` and renders all instances in a single draw call.

```cpp
class BatchNode : public scene::ISceneNode {
public:
    scene::SMeshBuffer* buffer;
    video::SMaterial material;

    BatchNode(scene::ISceneNode* parent, scene::ISceneManager* mgr, s32 id)
        : scene::ISceneNode(parent, mgr, id) {
        buffer = new scene::SMeshBuffer();
    }

    // Call this when entity positions change (each frame or on dirty)
    void rebuildBuffer(const core::array<EntityData>& entities) {
        buffer->Vertices.clear();
        buffer->Indices.clear();

        for (u32 i = 0; i < entities.size(); ++i) {
            // Append transformed quad/mesh geometry for each entity
            // into buffer->Vertices and buffer->Indices
            // Apply entity transform directly into vertex positions here
        }

        buffer->recalculateBoundingBox();
    }

    virtual void render() override {
        video::IVideoDriver* driver = SceneManager->getVideoDriver();
        driver->setMaterial(material);
        driver->setTransform(video::ETS_WORLD, AbsoluteTransformation);
        driver->drawMeshBuffer(buffer); // ONE draw call for all entities
    }

    virtual const core::aabbox3d<f32>& getBoundingBox() const override {
        return buffer->BoundingBox;
    }

    virtual void OnRegisterSceneNode() override {
        if (IsVisible)
            SceneManager->registerNodeForRendering(this);
        ISceneNode::OnRegisterSceneNode();
    }
};
```

**Key points:**
- Pre-bake entity world transforms directly into vertex data each frame (or only when dirty)
- Group entities by texture/material — one `BatchNode` per material bucket
- For billboards/sprites, compute camera-facing quads in `rebuildBuffer()` using the camera's view matrix
- This completely replaces individual scene nodes for those entity types — do not also add them to the scene manager as separate nodes

---

## Strategy 4 — Terrain Configuration

**Impact: Medium | Effort: Low**

If using Irrlicht's built-in `ITerrainSceneNode`:

```cpp
scene::ITerrainSceneNode* terrain = smgr->addTerrainSceneNode(...);

// Tune LOD update frequency — don't recalculate every frame
terrain->setLODChangeInterval(250); // ms between LOD updates

// Ensure only one terrain node exists — do not tile terrain
// as multiple nodes, each gets its own draw call(s)
```

If you have custom terrain split into many mesh nodes, apply Strategy 1 (merge by material) and Strategy 2 (atlas) to it.

---

## Strategy 5 — Visibility Culling Beyond Frustum

**Impact: Medium | Effort: Low–Medium**

Irrlicht does per-node frustum culling automatically. Make sure it is enabled on all nodes:

```cpp
node->setAutomaticCulling(scene::EAC_FRUSTUM_BOX);
```

For large open worlds, implement distance-based culling on top:

```cpp
// Each frame, compare node position to camera position
// and call setVisible(false) for nodes beyond your draw distance.
// setVisible(false) skips both rendering and frustum culling overhead.
f32 dist = cameraPos.getDistanceFromSQ(node->getAbsolutePosition());
node->setVisible(dist < MAX_DIST_SQ);
```

For scenes with hundreds of nodes, a simple quadtree or grid-based spatial partition dramatically reduces the per-frame cost of these visibility checks. Irrlicht does not provide this — implement it externally and feed visibility decisions back via `setVisible()`.

---

## Strategy 6 — Poor Man's Instancing via `drawVertexPrimitiveList`

**Impact: High (for repeated geometry) | Effort: High**

For cases where `SMeshBuffer` rebuild cost is a concern, you can build a large CPU-side vertex array each frame and submit it in one call:

```cpp
core::array<video::S3DVertex> verts;
core::array<u16> indices;

// Fill verts/indices with all instance geometry, transforms baked in
driver->setMaterial(sharedMaterial);
driver->setTransform(video::ETS_WORLD, core::IdentityMatrix);
driver->drawVertexPrimitiveList(
    verts.pointer(), verts.size(),
    indices.pointer(), indices.size() / 3,
    video::EVT_STANDARD,
    scene::EPT_TRIANGLES,
    video::EIT_16BIT
);
```

This is essentially the same as Strategy 3 but bypasses the scene node abstraction entirely — useful in a dedicated render pass outside the scene manager.

---

## Strategy 7 — Billboard / Sprite Batching

**Impact: Medium | Effort: Low–Medium**

`IBillboardSceneNode` is one draw call per billboard. Never use it for more than a handful of nodes.

Options:
- Use `IParticleSystemSceneNode` for large numbers of camera-facing sprites — it batches internally
- Include billboard quads in your Strategy 3 `BatchNode`, computing camera-facing orientation in `rebuildBuffer()`

---

## What Irrlicht 1.8.5 Does NOT Support

- **Hardware GPU instancing** — no API exposure in 1.8.5
- **Automatic mesh batching** — the scene manager will not merge nodes for you
- **Draw call sorting by material** — no automatic state sorting; you must control node/material grouping yourself
- **Indirect rendering or MDI** — not available

---

## Priority Checklist

Work through these in order. The first two steps alone typically yield the largest gains for static scenes.

- [ ] **1. Merge all static meshes by material** — one node per material per sector
- [ ] **2. Build texture atlases** — eliminate material switches within merged geometry
- [ ] **3. Replace per-entity scene nodes with a BatchNode** — one draw call per entity material bucket
- [ ] **4. Tune terrain LOD** — single terrain node, appropriate `setLODChangeInterval()`
- [ ] **5. Add distance-based `setVisible(false)` culling** — reduce scene manager overhead for far objects
- [ ] **6. Verify `EAC_FRUSTUM_BOX` on all nodes** — ensure culling is not accidentally disabled
- [ ] **7. Replace per-billboard nodes** with particle system or BatchNode quads

---

## Notes for Implementation

- Profile with a draw call counter before and after each step. Irrlicht exposes `IVideoDriver::getPrimitiveCountDrawn()` and you can wrap `drawMeshBuffer` calls to count manually.
- The BatchNode approach (Strategy 3) is the most invasive but also the most impactful for dynamic entity-heavy scenes. Build it as a standalone utility class so it can be reused across entity types.
- All geometry merged into a batch must use 16-bit indices (`u16`) unless you explicitly use `EIT_32BIT` — keep this in mind for large merged meshes that exceed 65535 vertices.
- Texture atlases must be generated offline or at startup — do not regenerate them per frame.
