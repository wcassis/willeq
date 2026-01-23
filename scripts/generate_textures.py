#!/usr/bin/env python3
"""
Generate pre-built textures for WillEQ.

Generates:
- Particle atlas (particle_atlas.png)
- Storm cloud textures (storm_cloud_*.png)
"""

import os
import math
import argparse
from PIL import Image, ImageDraw

# ============================================================================
# Particle Atlas Generation
# ============================================================================

def draw_soft_circle(img, tile_x, tile_y, tile_size, softness, color):
    """Draw a soft-edged circle in a tile."""
    base_x = tile_x * tile_size
    base_y = tile_y * tile_size
    center = tile_size / 2.0
    radius = tile_size / 2.0 - 1.0

    pixels = img.load()
    r, g, b, a = color

    for y in range(tile_size):
        for x in range(tile_size):
            dx = x - center + 0.5
            dy = y - center + 0.5
            dist = math.sqrt(dx * dx + dy * dy)

            alpha = 0.0
            if dist < radius:
                edge_dist = radius - dist
                alpha = min(1.0, edge_dist / (radius * softness))
                alpha = alpha * alpha  # Quadratic falloff

            pixel_alpha = int(alpha * a)
            pixels[base_x + x, base_y + y] = (r, g, b, pixel_alpha)

def draw_star(img, tile_x, tile_y, tile_size, points, color):
    """Draw a star shape in a tile."""
    base_x = tile_x * tile_size
    base_y = tile_y * tile_size
    center = tile_size / 2.0

    pixels = img.load()
    r, g, b, a = color

    for y in range(tile_size):
        for x in range(tile_size):
            dx = x - center + 0.5
            dy = y - center + 0.5
            dist = math.sqrt(dx * dx + dy * dy)
            angle = math.atan2(dy, dx)

            star_radius = 3.0 + 3.0 * math.pow(math.cos(angle * points), 2.0)
            alpha = 0.0

            if dist < star_radius:
                alpha = 1.0 - dist / star_radius
                alpha = math.pow(alpha, 0.5)

            pixel_alpha = int(alpha * a)
            pixels[base_x + x, base_y + y] = (r, g, b, pixel_alpha)

def draw_ring(img, tile_x, tile_y, tile_size, color):
    """Draw a ring shape for water ripples."""
    base_x = tile_x * tile_size
    base_y = tile_y * tile_size
    center = tile_size / 2.0
    outer_radius = tile_size / 2.0 - 1.0
    inner_radius = outer_radius * 0.7
    ring_width = outer_radius - inner_radius

    pixels = img.load()
    r, g, b, a = color

    for y in range(tile_size):
        for x in range(tile_size):
            dx = x - center + 0.5
            dy = y - center + 0.5
            dist = math.sqrt(dx * dx + dy * dy)

            alpha = 0.0
            if inner_radius <= dist <= outer_radius:
                ring_pos = (dist - inner_radius) / ring_width
                alpha = 1.0 - abs(ring_pos * 2.0 - 1.0)
                alpha = math.pow(alpha, 0.7)

            pixel_alpha = int(alpha * a)
            pixels[base_x + x, base_y + y] = (r, g, b, pixel_alpha)

def pseudo_noise(x, y, seed):
    """Simple pseudo-random noise function."""
    n = x + y * 57 + seed
    n = (n << 13) ^ n
    return (1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0) * 0.5 + 0.5

def draw_snow_patch(img, tile_x, tile_y, tile_size, color):
    """Draw an irregular snow patch."""
    base_x = tile_x * tile_size
    base_y = tile_y * tile_size
    center = tile_size / 2.0
    base_radius = tile_size / 2.0 - 2.0

    pixels = img.load()
    r, g, b, a = color

    for y in range(tile_size):
        for x in range(tile_size):
            dx = x - center + 0.5
            dy = y - center + 0.5
            dist = math.sqrt(dx * dx + dy * dy)

            noise = pseudo_noise(x, y, 42) * 2.0
            radius = base_radius + noise

            alpha = 0.0
            if dist < radius:
                edge_dist = radius - dist
                alpha = min(1.0, edge_dist / (radius * 0.4))
                alpha *= 0.7 + 0.3 * pseudo_noise(x * 2, y * 2, 123)

            pixel_alpha = int(alpha * a)
            pixels[base_x + x, base_y + y] = (r, g, b, pixel_alpha)

def draw_rain_streak(img, tile_x, tile_y, tile_size, color):
    """Draw a vertical rain streak."""
    base_x = tile_x * tile_size
    base_y = tile_y * tile_size
    center_x = tile_size / 2.0

    pixels = img.load()
    r, g, b, a = color

    for y in range(tile_size):
        for x in range(tile_size):
            dx = abs(x - center_x + 0.5)
            width = 1.5

            # Fade from top to bottom
            y_factor = y / tile_size
            brightness = 0.3 + 0.7 * y_factor

            alpha = 0.0
            if dx < width:
                alpha = 1.0 - dx / width
                alpha = math.pow(alpha, 0.8)
                alpha *= brightness
                # Taper at top
                if y < tile_size * 0.3:
                    alpha *= y / (tile_size * 0.3)

            pixel_alpha = int(alpha * a)
            pixel_r = int(r * brightness)
            pixel_g = int(g * brightness)
            pixel_b = int(b * brightness)
            pixels[base_x + x, base_y + y] = (pixel_r, pixel_g, pixel_b, pixel_alpha)

def generate_particle_atlas(tile_size=16):
    """Generate the particle atlas texture."""
    atlas_width = tile_size * 4  # 4 columns
    atlas_height = tile_size * 4  # 4 rows

    img = Image.new('RGBA', (atlas_width, atlas_height), (0, 0, 0, 0))

    # Row 0
    # Tile 0: Soft circle (dust) - white, very soft edges
    draw_soft_circle(img, 0, 0, tile_size, 0.8, (255, 255, 255, 200))

    # Tile 1: Star shape (firefly) - yellow-green glow
    draw_star(img, 1, 0, tile_size, 4, (200, 255, 100, 255))

    # Tile 2: Wispy cloud (mist) - white, very soft
    draw_soft_circle(img, 2, 0, tile_size, 0.9, (255, 255, 255, 100))

    # Tile 3: Spore shape (pollen) - yellow-green
    draw_soft_circle(img, 3, 0, tile_size, 0.5, (255, 255, 150, 220))

    # Row 1
    # Tile 4: Grain shape (sand) - tan/brown
    draw_soft_circle(img, 0, 1, tile_size, 0.3, (220, 180, 120, 200))

    # Tile 5: Leaf shape - green
    draw_soft_circle(img, 1, 1, tile_size, 0.4, (100, 180, 80, 200))

    # Tile 6: Snowflake - white, crisp
    draw_star(img, 2, 1, tile_size, 6, (255, 255, 255, 255))

    # Tile 7: Ember - orange-red glow
    draw_soft_circle(img, 3, 1, tile_size, 0.6, (255, 150, 50, 255))

    # Row 2
    # Tile 8: Foam spray - white wispy
    draw_soft_circle(img, 0, 2, tile_size, 0.7, (255, 255, 255, 230))

    # Tile 9: Water droplet - blue-white sphere
    draw_soft_circle(img, 1, 2, tile_size, 0.4, (200, 230, 255, 200))

    # Tile 10: Ripple ring
    draw_ring(img, 2, 2, tile_size, (220, 240, 255, 200))

    # Tile 11: Snow patch
    draw_snow_patch(img, 3, 2, tile_size, (245, 250, 255, 220))

    # Row 3
    # Tile 12: Rain streak
    draw_rain_streak(img, 0, 3, tile_size, (200, 220, 255, 255))

    # Tiles 13-15: Reserved (leave transparent)

    return img

# ============================================================================
# Storm Cloud Texture Generation
# ============================================================================

def noise2d(x, y, seed):
    """Simple hash-based noise function."""
    xi = int(math.floor(x)) & 255
    yi = int(math.floor(y)) & 255
    n = xi + yi * 57 + seed
    n = (n << 13) ^ n
    return 1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0

def smooth_noise2d(x, y, seed):
    """Smooth noise with neighbor averaging."""
    corners = (noise2d(x - 1, y - 1, seed) + noise2d(x + 1, y - 1, seed) +
               noise2d(x - 1, y + 1, seed) + noise2d(x + 1, y + 1, seed)) / 16.0
    sides = (noise2d(x - 1, y, seed) + noise2d(x + 1, y, seed) +
             noise2d(x, y - 1, seed) + noise2d(x, y + 1, seed)) / 8.0
    center = noise2d(x, y, seed) / 4.0
    return corners + sides + center

def interpolated_noise2d(x, y, seed):
    """Interpolated noise for smooth gradients."""
    int_x = int(math.floor(x))
    int_y = int(math.floor(y))
    frac_x = x - int_x
    frac_y = y - int_y

    fx = (1.0 - math.cos(frac_x * math.pi)) * 0.5
    fy = (1.0 - math.cos(frac_y * math.pi)) * 0.5

    v1 = smooth_noise2d(float(int_x), float(int_y), seed)
    v2 = smooth_noise2d(float(int_x + 1), float(int_y), seed)
    v3 = smooth_noise2d(float(int_x), float(int_y + 1), seed)
    v4 = smooth_noise2d(float(int_x + 1), float(int_y + 1), seed)

    i1 = v1 * (1.0 - fx) + v2 * fx
    i2 = v3 * (1.0 - fx) + v4 * fx

    return i1 * (1.0 - fy) + i2 * fy

def seamless_perlin_noise2d(x, y, seed, octaves, persistence):
    """Seamless tileable Perlin noise using toroidal mapping."""
    PI2 = 2.0 * math.pi

    angle_x = x * PI2
    angle_y = y * PI2

    nx = math.cos(angle_x)
    ny = math.sin(angle_x)
    nz = math.cos(angle_y)
    nw = math.sin(angle_y)

    total = 0.0
    frequency = 2.0
    amplitude = 1.0
    max_value = 0.0

    for i in range(octaves):
        fx = nx * frequency
        fy = ny * frequency
        fz = nz * frequency
        fw = nw * frequency

        n1 = interpolated_noise2d(fx + fz, fy + fw, seed + i * 1000)
        n2 = interpolated_noise2d(fx - fz, fy - fw, seed + i * 1000 + 500)
        n3 = interpolated_noise2d(fx + fw, fy + fz, seed + i * 1000 + 250)

        n = (n1 + n2 + n3) / 3.0

        total += n * amplitude
        max_value += amplitude
        amplitude *= persistence
        frequency *= 2.0

    return (total / max_value + 1.0) * 0.5

def generate_storm_cloud_texture(size, seed, octaves=4, persistence=0.5, color=(102, 107, 115)):
    """Generate a single seamless cloud texture."""
    img = Image.new('RGBA', (size, size))
    pixels = img.load()

    r, g, b = color

    for y in range(size):
        for x in range(size):
            nx = x / size
            ny = y / size

            noise = seamless_perlin_noise2d(nx, ny, seed, octaves, persistence)

            noise = max(0.0, noise - 0.35) * 1.6
            noise = min(1.0, noise)

            alpha = int(noise * 255)
            pixels[x, y] = (r, g, b, alpha)

    return img

# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description='Generate pre-built textures for WillEQ')
    parser.add_argument('--output-dir', '-o', default='data/textures',
                        help='Output directory for textures')
    parser.add_argument('--cloud-size', type=int, default=256,
                        help='Storm cloud texture size (default: 256)')
    parser.add_argument('--cloud-frames', type=int, default=4,
                        help='Number of cloud frames (default: 4)')
    parser.add_argument('--cloud-octaves', type=int, default=4,
                        help='Cloud noise octaves (default: 4)')
    parser.add_argument('--particle-tile-size', type=int, default=16,
                        help='Particle atlas tile size (default: 16)')
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    # Generate particle atlas
    print(f"Generating particle atlas ({args.particle_tile_size * 4}x{args.particle_tile_size * 4})...")
    atlas = generate_particle_atlas(args.particle_tile_size)
    atlas_path = os.path.join(args.output_dir, 'particle_atlas.png')
    atlas.save(atlas_path)
    print(f"  Saved: {atlas_path}")

    # Generate storm cloud textures
    print(f"\nGenerating {args.cloud_frames} storm cloud textures ({args.cloud_size}x{args.cloud_size})...")
    for i in range(args.cloud_frames):
        seed = 12345 + i * 7919
        print(f"  Generating frame {i} (seed={seed})...")

        cloud = generate_storm_cloud_texture(
            size=args.cloud_size,
            seed=seed,
            octaves=args.cloud_octaves,
            persistence=0.5
        )

        cloud_path = os.path.join(args.output_dir, f'storm_cloud_{i}.png')
        cloud.save(cloud_path)
        print(f"  Saved: {cloud_path}")

    print("\nDone!")

if __name__ == '__main__':
    main()
