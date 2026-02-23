# PVS-Based Visibility Culling for WillEQ Software Renderer

## Executive Summary

The EverQuest S3D/WLD zone files already contain **pre-computed PVS (Potentially Visible Set) data** that the original 1999 client used for visibility culling. This data is embedded in the BSP tree structure and can be leveraged for efficient software rendering without expensive runtime calculations.

## The Problem

Irrlicht's software renderer doesn't support hardware occlusion culling, and frustum culling alone isn't sufficient for complex indoor zones. We need a way to quickly determine which parts of a zone are visible from the player's current position.

## The Solution: Use Existing PVS Data

The WLD format stores pre-computed visibility information:

> "EverQuest's use of a PVS (potentially visible set) stored inside the zone BSP tree. When baking zone geometry, it is determined which zone regions can be seen from other zone regions based on many factors including distance and occlusion. During rendering, the PVS is used in the first pass of determining which regions to consider for drawing. If the region is not in the PVS of the camera's region, it is discarded."
> — EQEmu Documentation

This means visibility was computed at zone compile time by the original developers, factoring in:
- Distance
- Occlusion by walls/geometry
- Zone architecture

## WLD Fragment Reference

### Fragment 0x21 - BSP Tree

The BSP tree spatially subdivides the zone. Each node contains:
- Split plane (normal + distance in Hessian normal form)
- Left/right child indices (for internal nodes)
- Region index (for leaf nodes, references a 0x22 fragment)

Structure:
```
struct BspTreeEntry {
    float normal_x, normal_y, normal_z;  // Split plane normal
    float distance;                       // Distance from origin
    uint32_t region_id;                   // If leaf: 1-indexed region ID, else 0
    uint32_t left_child;                  // Index of left child (0-indexed)
    uint32_t right_child;                 // Index of right child (0-indexed)
};
```

### Fragment 0x22 - BSP Region (Contains PVS!)

Each BSP region contains:
- Bounding box for the region
- Reference to 0x36 mesh fragment (if region has geometry)
- **RLE-encoded "nearby regions" array** — THIS IS THE PVS DATA

> "Also contains an RLE-encoded array of data that tells the client which regions are 'nearby'. That way, if a player enters one of the 'nearby' regions, the client knows that objects in the region are visible to the player."

The RLE-encoded visibility data is a compressed bitfield where each bit represents whether a particular region is visible from this region.

### Fragment 0x29 - Region Flag

Marks special region types:
- `WT_ZONE` — underwater regions
- `LA_ZONE` — lava regions  
- `DRP_ZONE` — PvP regions
- `DRNTP_ZONE` — zone transition points

## Implementation Plan

### Phase 1: Audit Current WLD Parsing

Check if we're currently parsing:
- [ ] 0x21 BSP Tree fragments
- [ ] 0x22 BSP Region fragments
- [ ] The RLE-encoded visibility data within 0x22

Files to examine:
- WLD loader code (likely in `src/client/graphics/eq/`)
- Any existing BSP-related structures

### Phase 2: Extract and Store PVS Data

For each BSP region, we need:
```cpp
struct BspRegion {
    BoundingBox bounds;
    uint32_t mesh_fragment_ref;  // Reference to 0x36 fragment, or 0
    std::vector<uint32_t> visible_regions;  // Decoded PVS - list of visible region IDs
};
```

The RLE decoding algorithm (similar to Quake):
```cpp
// Pseudocode for RLE-encoded visibility
std::vector<bool> DecodeVisibility(const uint8_t* data, size_t data_size, int num_regions) {
    std::vector<bool> visible(num_regions, false);
    int region = 0;
    size_t i = 0;
    
    while (i < data_size && region < num_regions) {
        if (data[i] == 0) {
            // Zero byte means skip N regions (they're not visible)
            i++;
            region += data[i];
            i++;
        } else {
            // Non-zero byte: each bit indicates visibility
            for (int bit = 0; bit < 8 && region < num_regions; bit++) {
                visible[region] = (data[i] & (1 << bit)) != 0;
                region++;
            }
            i++;
        }
    }
    return visible;
}
```

### Phase 3: Runtime Visibility Query

```cpp
class ZoneVisibility {
public:
    // Find which BSP region contains a point
    int FindRegion(const Vec3& position) const;
    
    // Get list of visible regions from a given region
    const std::vector<uint32_t>& GetVisibleRegions(int region_id) const;
    
    // Check if a specific region is visible from current region
    bool IsRegionVisible(int from_region, int to_region) const;
    
private:
    std::vector<BspTreeNode> bsp_tree_;
    std::vector<BspRegion> regions_;
};
```

### Phase 4: Integrate with Renderer

In the render loop:
```cpp
void RenderZone() {
    // 1. Find camera's current BSP region
    int camera_region = zone_visibility_.FindRegion(camera_position_);
    
    // 2. Get PVS for this region
    const auto& visible_regions = zone_visibility_.GetVisibleRegions(camera_region);
    
    // 3. Only render geometry from visible regions
    for (int region_id : visible_regions) {
        const auto& region = regions_[region_id];
        
        // Optional: additional frustum culling on region bounds
        if (!frustum_.Contains(region.bounds)) {
            continue;
        }
        
        // Render the region's geometry
        RenderMesh(region.mesh_fragment_ref);
    }
}
```

### Phase 5: Organize Geometry by Region

Currently, zone geometry might be loaded as one big mesh. For PVS culling to work efficiently, geometry needs to be organized by BSP region:

```cpp
struct ZoneGeometry {
    // Map from region ID to renderable mesh data
    std::unordered_map<uint32_t, RegionMesh> region_meshes;
};
```

## References

- EQEmu WLD File Reference: https://docs.eqemu.io/server/zones/customizing-zones/wld-file-reference/
- OpenEQ WLD Documentation: https://github.com/daeken/OpenEQ/wiki/WLD
- LanternExtractor (C# reference implementation): https://github.com/LanternEQ/LanternExtractor
- libeq (Rust reference): https://github.com/cjab/libeq
- EQEmu Fog/Clip Plane docs (mentions PVS): https://docs.eqemu.io/client/guides/fog-system-and-clip-plane/

## Testing Strategy

1. **Verify PVS data extraction**: Pick a known zone (e.g., gfaydark), dump the PVS data, verify region count matches expected
2. **Visual debugging**: Color-code regions, highlight which ones are marked visible
3. **Performance comparison**: Measure draw calls / triangles rendered with and without PVS culling
4. **Edge cases**: Test at zone boundaries, in water regions, near zone lines

## Notes

- The PVS data is intentionally conservative (may mark some non-visible regions as visible) to avoid popping
- Frustum culling should still be applied on top of PVS culling for best performance
- Some regions may have no geometry (just used for spatial queries like water detection)
- The BSP tree is also useful for collision detection and finding spawn points

