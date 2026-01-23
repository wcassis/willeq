#!/usr/bin/env python3
"""
Generate snow overlay textures for the screen-space snow effect.

Creates:
- snow_intensity_01.png through snow_intensity_10.png (varying density)
- snow_layer_mid.png (medium distance flakes)
- snow_layer_far.png (distant small flakes)

Snow differs from rain in that snowflakes are:
- Small dots/circles instead of streaks
- Slightly blurred edges for soft appearance
- Random distribution (not vertical lines)
- Varying sizes within each texture
"""

import os
import sys
import random
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    print("Error: PIL/Pillow not installed. Install with: pip install Pillow")
    sys.exit(1)


def create_snow_texture(width: int, height: int, density: int,
                        min_size: float = 1.0, max_size: float = 4.0,
                        blur_radius: float = 0.5, alpha_base: int = 180) -> Image.Image:
    """
    Create a snow texture with random white dots/flakes.

    Args:
        width: Texture width in pixels
        height: Texture height in pixels
        density: Number of flakes to generate
        min_size: Minimum flake radius
        max_size: Maximum flake radius
        blur_radius: Gaussian blur radius for soft edges
        alpha_base: Base alpha value for flakes (0-255)

    Returns:
        RGBA Image with snow flakes
    """
    # Create transparent image
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    random.seed(42 + density)  # Deterministic but different per density

    for _ in range(density):
        # Random position
        x = random.uniform(0, width)
        y = random.uniform(0, height)

        # Random size (smaller flakes more common)
        size = random.uniform(min_size, max_size)
        # Bias toward smaller sizes
        size = min_size + (max_size - min_size) * (random.random() ** 1.5)

        # Random alpha variation
        alpha = int(alpha_base * random.uniform(0.6, 1.0))

        # Snow is white with slight variations
        brightness = random.randint(240, 255)
        color = (brightness, brightness, brightness, alpha)

        # Draw ellipse (slightly oval for natural look)
        aspect = random.uniform(0.8, 1.2)
        rx = size
        ry = size * aspect

        bbox = [x - rx, y - ry, x + rx, y + ry]
        draw.ellipse(bbox, fill=color)

    # Apply slight blur for soft edges
    if blur_radius > 0:
        img = img.filter(ImageFilter.GaussianBlur(radius=blur_radius))

    return img


def generate_intensity_textures(output_dir: Path, size: int = 256):
    """Generate snow_intensity_01.png through snow_intensity_10.png"""

    # Density increases with intensity
    # Start sparse, get progressively denser
    base_density = 50
    density_per_level = 60

    for intensity in range(1, 11):
        density = base_density + (intensity - 1) * density_per_level

        # Higher intensity = slightly larger flakes too
        min_size = 1.0 + (intensity - 1) * 0.1
        max_size = 3.0 + (intensity - 1) * 0.2

        # Alpha increases with intensity
        alpha = 140 + intensity * 8

        img = create_snow_texture(
            width=size,
            height=size,
            density=density,
            min_size=min_size,
            max_size=max_size,
            blur_radius=0.5,
            alpha_base=alpha
        )

        filename = output_dir / f"snow_intensity_{intensity:02d}.png"
        img.save(filename, "PNG")
        print(f"Generated {filename} (density={density}, size={min_size:.1f}-{max_size:.1f})")


def generate_layer_textures(output_dir: Path, size: int = 256):
    """Generate snow_layer_mid.png and snow_layer_far.png"""

    # Mid layer: medium density, medium size flakes
    mid = create_snow_texture(
        width=size,
        height=size,
        density=200,
        min_size=1.5,
        max_size=3.5,
        blur_radius=0.3,
        alpha_base=160
    )
    mid_path = output_dir / "snow_layer_mid.png"
    mid.save(mid_path, "PNG")
    print(f"Generated {mid_path}")

    # Far layer: sparser, smaller flakes (distant snow)
    far = create_snow_texture(
        width=size,
        height=size,
        density=150,
        min_size=0.8,
        max_size=2.0,
        blur_radius=0.2,
        alpha_base=120
    )
    far_path = output_dir / "snow_layer_far.png"
    far.save(far_path, "PNG")
    print(f"Generated {far_path}")

    # Also create a generic fallback texture
    fallback = create_snow_texture(
        width=size,
        height=size,
        density=300,
        min_size=1.0,
        max_size=3.0,
        blur_radius=0.4,
        alpha_base=180
    )
    fallback_path = output_dir / "snow_flakes.png"
    fallback.save(fallback_path, "PNG")
    print(f"Generated {fallback_path} (fallback)")


def main():
    # Determine output directory
    script_dir = Path(__file__).parent.resolve()
    project_root = script_dir.parent
    output_dir = project_root / "data" / "textures"

    # Create output directory if needed
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating snow textures in {output_dir}")
    print()

    # Generate all textures
    generate_intensity_textures(output_dir)
    print()
    generate_layer_textures(output_dir)

    print()
    print("Snow texture generation complete!")


if __name__ == "__main__":
    main()
