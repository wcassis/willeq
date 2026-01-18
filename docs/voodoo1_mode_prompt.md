# WillEQ Voodoo 1 Software Rendering Mode

## Project Context

This is for the willeq project: https://github.com/wcassis/willeq

WillEQ is an EverQuest client targeting the Titanium protocol with optional 3D rendering via Irrlicht. The goal is to add a "Voodoo 1 mode" that constrains the software renderer to match the memory and processing limitations of a 3dfx Voodoo 1 graphics card from 1996—the type of hardware EverQuest originally required in 1999.

## Why This Matters

The original EQ client (1999) required a Glide-compatible 3D card. The Voodoo 1 was the baseline, with 4MB total VRAM split between two chips. By constraining our software renderer to these limits, modern CPUs should be able to handle the rendering workload that originally required dedicated 3D hardware, creating an authentic retro experience.

## 3dfx Voodoo 1 Hardware Specifications

### Memory Architecture (4MB Total)

| Chip | Memory | Purpose |
|------|--------|---------|
| FBI (Frame Buffer Interface) | 2MB | Framebuffer, Z-buffer, command FIFO |
| TMU (Texture Mapping Unit) | 2MB | Texture storage only |

### FBI Memory Budget at 640x480

- Front buffer: 640 × 480 × 2 bytes = 614,400 bytes (~600KB)
- Back buffer: 640 × 480 × 2 bytes = 614,400 bytes (~600KB)
- Z-buffer (16-bit): 640 × 480 × 2 bytes = 614,400 bytes (~600KB)
- **Subtotal: ~1.8MB**, leaving ~200KB for command FIFO and work data

### TMU Constraints

- 2MB for all textures currently in use
- Maximum texture dimension: 256×256
- Texture formats: 16-bit (RGB 565, RGBA 1555, RGBA 4444)
- Single TMU = one texture per rendering pass
- 3dfx used FXT1 3:1 lossy compression to fit more textures

### Processing Budget

- Theoretical fill rate: 50 Mpixels/second at 50MHz clock
- Practical fill rate with texturing: ~25-35 Mpixels/second
- At 30fps: ~1 million textured pixels per frame budget
- Single-pass texturing only (two passes needed for lightmapped surfaces)

### Resolution Limits

| VRAM Config | Max Resolution |
|-------------|----------------|
| 4MB (stock) | 640×480 |
| 6MB | 800×600 |
| 8MB | 800×600 (more texture room) |

## Implementation Requirements

### 1. Memory Budget Manager

Create a class that tracks and enforces memory limits:

```cpp
class VoodooMemoryBudget {
public:
    static constexpr size_t FBI_TOTAL = 2 * 1024 * 1024;  // 2MB
    static constexpr size_t TMU_TOTAL = 2 * 1024 * 1024;  // 2MB
    
    // FBI breakdown for 640x480 16-bit
    static constexpr size_t FRAMEBUFFER_SIZE = 640 * 480 * 2;
    static constexpr size_t ZBUFFER_SIZE = 640 * 480 * 2;
    
    // What's left for double buffering and command FIFO
    static constexpr size_t FBI_AVAILABLE = FBI_TOTAL - FRAMEBUFFER_SIZE - ZBUFFER_SIZE;
    
    // Texture cache hard limit
    static constexpr size_t TEXTURE_CACHE_LIMIT = TMU_TOTAL;
    
    // Texture constraints
    static constexpr int MAX_TEXTURE_DIMENSION = 256;
    static constexpr int TEXTURE_BPP = 2;  // 16-bit textures
};
```

### 2. Texture Cache with LRU Eviction

Implement a texture cache that:
- Tracks total texture memory usage
- Enforces the 2MB limit
- Uses LRU (Least Recently Used) eviction when over budget
- Downsample any texture larger than 256×256
- Converts textures to 16-bit format

```cpp
class VoodooTextureCache {
private:
    size_t current_usage_ = 0;
    size_t max_usage_ = VoodooMemoryBudget::TEXTURE_CACHE_LIMIT;
    
    struct CachedTexture {
        uint32_t id;
        size_t size_bytes;
        // texture data...
    };
    
    std::unordered_map<uint32_t, CachedTexture> textures_;
    std::list<uint32_t> lru_order_;  // Front = oldest, back = newest
    
public:
    bool upload(uint32_t id, const TextureData& tex);
    void touch(uint32_t id);  // Move to back of LRU
    void evict_oldest();
    bool has_texture(uint32_t id) const;
    size_t get_usage() const { return current_usage_; }
    size_t get_available() const { return max_usage_ - current_usage_; }
};
```

### 3. Texture Processing Pipeline

Before uploading any texture:

1. **Size check**: If width > 256 or height > 256, downsample using box filter
2. **Format conversion**: Convert to 16-bit (RGB 565 for opaque, RGBA 1555 for transparent)
3. **Memory check**: Evict old textures if needed to fit new one

```cpp
TextureData process_for_voodoo(const TextureData& input) {
    TextureData output = input;
    
    // Downsample if needed
    while (output.width > 256 || output.height > 256) {
        output = downsample_2x(output);
    }
    
    // Convert to 16-bit
    if (output.format != FORMAT_RGB565 && output.format != FORMAT_RGBA1555) {
        output = convert_to_16bit(output);
    }
    
    return output;
}
```

### 4. Two-Pass Rendering for Lightmaps

The Voodoo 1 had only one TMU, so multitextured surfaces (base + lightmap) required two passes:

```cpp
void render_surface_voodoo1(const Surface& surf) {
    // Pass 1: Base texture
    bind_texture(surf.base_texture_id);
    set_blend_mode(BlendMode::Replace);
    draw_triangles(surf.triangles);
    
    // Pass 2: Lightmap (if present)
    if (surf.has_lightmap && voodoo1_mode_enabled) {
        bind_texture(surf.lightmap_id);
        set_blend_mode(BlendMode::Modulate);  // Multiply with framebuffer
        draw_triangles(surf.triangles);  // Same geometry again
    }
}
```

### 5. Fill Rate Budget (Optional)

Track pixels rendered per frame to simulate fill rate limits:

```cpp
class FillRateBudget {
    uint64_t pixels_this_frame_ = 0;
    
    // ~50M pixels/sec at 50MHz, targeting 30fps = ~1.6M pixels/frame
    // With overdraw and two-pass, budget conservatively
    static constexpr uint64_t PIXELS_PER_FRAME = 1'000'000;
    
public:
    bool can_draw(int pixel_count) {
        if (pixels_this_frame_ + pixel_count > PIXELS_PER_FRAME) {
            return false;  // Budget exceeded
        }
        pixels_this_frame_ += pixel_count;
        return true;
    }
    
    void end_frame() { pixels_this_frame_ = 0; }
    float get_utilization() const { 
        return static_cast<float>(pixels_this_frame_) / PIXELS_PER_FRAME; 
    }
};
```

### 6. Configuration

Add to the config system:

```json
{
    "rendering": {
        "voodoo1_mode": true,
        "voodoo_config": {
            "texture_memory_kb": 2048,
            "framebuffer_memory_kb": 2048,
            "max_texture_size": 256,
            "color_depth": 16,
            "max_resolution": [640, 480],
            "two_pass_lightmaps": true,
            "enforce_fill_rate": false,
            "simulate_texture_thrashing": false
        }
    }
}
```

### 7. Optional: Texture Upload Delay Simulation

For authenticity, simulate PCI bandwidth limitations when textures must be swapped:

```cpp
void simulate_texture_upload(size_t bytes) {
    if (!config.simulate_texture_thrashing) return;
    
    // Voodoo 1 PCI: ~50MB/s practical throughput
    // 64KB texture = ~1.3ms upload time
    double seconds = bytes / (50.0 * 1024 * 1024);
    std::this_thread::sleep_for(
        std::chrono::microseconds(static_cast<int>(seconds * 1'000'000))
    );
}
```

## Implementation Tasks

1. [ ] Create `VoodooMemoryBudget` constants class
2. [ ] Implement `VoodooTextureCache` with LRU eviction
3. [ ] Add texture downsample function (box filter to 256×256 max)
4. [ ] Add 16-bit texture format conversion (RGB 565 / RGBA 1555)
5. [ ] Modify renderer to use two-pass for lightmapped surfaces when in Voodoo 1 mode
6. [ ] Add configuration options to config parser
7. [ ] Add optional fill rate tracking and budget enforcement
8. [ ] Add debug overlay showing texture memory usage, cache hits/misses, fill rate
9. [ ] Test with various zones to ensure memory limits are respected

## Files Likely to Modify

Based on the project structure:
- `src/client/graphics/irrlicht_renderer.cpp` - Main rendering logic
- `include/client/graphics/irrlicht_renderer.h` - Add new classes/config
- `src/client/eq.cpp` - Config parsing
- New files for texture cache implementation

## Testing

1. Load a texture-heavy zone and verify texture memory stays under 2MB
2. Verify textures larger than 256×256 are downsampled
3. Confirm two-pass rendering is used for lightmapped surfaces
4. Check that LRU eviction works correctly when cache is full
5. Optional: Compare rendering output between normal and Voodoo 1 modes

## References

- 3dfx Voodoo 1: 2MB FBI + 2MB TMU = 4MB total
- 50MHz clock, 50 Mpixel/sec theoretical fill rate
- Max texture size: 256×256
- Texture formats: RGB 565, RGBA 1555, RGBA 4444 (all 16-bit)
- Single TMU = single texture per pass
- Max resolution with 2MB FBI: 640×480 (16-bit color + 16-bit Z)

