#!/usr/bin/env python3
"""
Generate seamless storm cloud textures for WillEQ.

Uses toroidal mapping for perfect seamless tiling.
"""

import os
import math
import argparse
from PIL import Image

def noise2d(x: float, y: float, seed: int) -> float:
    """Simple hash-based noise function."""
    xi = int(math.floor(x)) & 255
    yi = int(math.floor(y)) & 255
    n = xi + yi * 57 + seed
    n = (n << 13) ^ n
    return 1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0

def smooth_noise2d(x: float, y: float, seed: int) -> float:
    """Smooth noise with neighbor averaging."""
    corners = (noise2d(x - 1, y - 1, seed) + noise2d(x + 1, y - 1, seed) +
               noise2d(x - 1, y + 1, seed) + noise2d(x + 1, y + 1, seed)) / 16.0
    sides = (noise2d(x - 1, y, seed) + noise2d(x + 1, y, seed) +
             noise2d(x, y - 1, seed) + noise2d(x, y + 1, seed)) / 8.0
    center = noise2d(x, y, seed) / 4.0
    return corners + sides + center

def interpolated_noise2d(x: float, y: float, seed: int) -> float:
    """Interpolated noise for smooth gradients."""
    int_x = int(math.floor(x))
    int_y = int(math.floor(y))
    frac_x = x - int_x
    frac_y = y - int_y

    # Cosine interpolation
    fx = (1.0 - math.cos(frac_x * math.pi)) * 0.5
    fy = (1.0 - math.cos(frac_y * math.pi)) * 0.5

    v1 = smooth_noise2d(float(int_x), float(int_y), seed)
    v2 = smooth_noise2d(float(int_x + 1), float(int_y), seed)
    v3 = smooth_noise2d(float(int_x), float(int_y + 1), seed)
    v4 = smooth_noise2d(float(int_x + 1), float(int_y + 1), seed)

    i1 = v1 * (1.0 - fx) + v2 * fx
    i2 = v3 * (1.0 - fx) + v4 * fx

    return i1 * (1.0 - fy) + i2 * fy

def seamless_perlin_noise2d(x: float, y: float, seed: int, octaves: int, persistence: float) -> float:
    """
    Seamless tileable Perlin noise using toroidal mapping.
    Maps 2D coordinates onto a 4D torus for perfect seamless tiling.
    """
    PI2 = 2.0 * math.pi

    # Convert x,y (0-1) to angles (0-2π)
    angle_x = x * PI2
    angle_y = y * PI2

    # Map to points on two circles (4D torus)
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

        # Combine multiple 2D noise samples to simulate 4D noise
        n1 = interpolated_noise2d(fx + fz, fy + fw, seed + i * 1000)
        n2 = interpolated_noise2d(fx - fz, fy - fw, seed + i * 1000 + 500)
        n3 = interpolated_noise2d(fx + fw, fy + fz, seed + i * 1000 + 250)

        n = (n1 + n2 + n3) / 3.0

        total += n * amplitude
        max_value += amplitude
        amplitude *= persistence
        frequency *= 2.0

    # Normalize to 0-1 range
    return (total / max_value + 1.0) * 0.5

def generate_cloud_texture(size: int, seed: int, octaves: int = 4, persistence: float = 0.5,
                           color: tuple = (102, 107, 115)) -> Image.Image:
    """Generate a single seamless cloud texture."""
    img = Image.new('RGBA', (size, size))
    pixels = img.load()

    r, g, b = color

    for y in range(size):
        for x in range(size):
            # Normalized coordinates 0-1
            nx = x / size
            ny = y / size

            # Get seamless noise value
            noise = seamless_perlin_noise2d(nx, ny, seed, octaves, persistence)

            # Apply cloud shaping - enhance contrast
            noise = max(0.0, noise - 0.35) * 1.6
            noise = min(1.0, noise)

            # Alpha based on noise
            alpha = int(noise * 255)

            pixels[x, y] = (r, g, b, alpha)

    return img

def main():
    parser = argparse.ArgumentParser(description='Generate seamless storm cloud textures')
    parser.add_argument('--output-dir', '-o', default='data/textures',
                        help='Output directory for textures')
    parser.add_argument('--size', '-s', type=int, default=256,
                        help='Texture size (default: 256)')
    parser.add_argument('--frames', '-f', type=int, default=4,
                        help='Number of frames to generate (default: 4)')
    parser.add_argument('--octaves', type=int, default=4,
                        help='Perlin noise octaves (default: 4)')
    parser.add_argument('--persistence', type=float, default=0.5,
                        help='Perlin noise persistence (default: 0.5)')
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    print(f"Generating {args.frames} storm cloud textures ({args.size}x{args.size})...")

    for i in range(args.frames):
        seed = 12345 + i * 7919  # Same seeds as C++ code
        print(f"  Generating frame {i} (seed={seed})...")

        img = generate_cloud_texture(
            size=args.size,
            seed=seed,
            octaves=args.octaves,
            persistence=args.persistence
        )

        output_path = os.path.join(args.output_dir, f'storm_cloud_{i}.png')
        img.save(output_path)
        print(f"  Saved: {output_path}")

    print("Done!")

if __name__ == '__main__':
    main()
