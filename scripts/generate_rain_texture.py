#!/usr/bin/env python3
"""
Generate rain streak texture for screen-space rain overlay.
Creates a tileable texture with vertical rain streaks.
"""

import os
import random
from PIL import Image, ImageDraw

def generate_rain_texture(width=256, height=256, num_streaks=50, seed=42,
                          min_length=10, max_length=40):
    """Generate a rain streak texture with transparent background.

    Args:
        width, height: Texture dimensions
        num_streaks: Number of rain streaks to draw
        seed: Random seed for reproducibility
        min_length, max_length: Range for streak lengths in pixels
    """
    random.seed(seed)

    # Create RGBA image with transparent background
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for _ in range(num_streaks):
        # Random position
        x = random.randint(0, width - 1)
        y_start = random.randint(0, height - 1)

        # Random streak length
        length = random.randint(min_length, max_length)

        # Random brightness for the head (220-255, bright raindrop)
        head_brightness = random.randint(220, 255)

        # Random base alpha (150-220)
        head_alpha = random.randint(150, 220)

        # Draw streak (wraps vertically for seamless tiling)
        # Gradient: tail at top (faded) -> head at bottom (bright)
        # Rain falls down, so leading edge (head) is at bottom of streak
        for j in range(length):
            y = (y_start + j) % height

            # Progress along streak (0 = tail/top, 1 = head/bottom)
            progress = j / max(1, length - 1)

            # Exponential ramp-up: tail fades in, head is bright
            fade = progress ** 1.5

            # Brightness fades from head to tail
            pixel_bright = int(head_brightness * (0.3 + 0.7 * fade))

            # Alpha fades more dramatically toward tail
            pixel_alpha = int(head_alpha * fade)

            # Skip nearly invisible pixels
            if pixel_alpha < 5:
                continue

            # Set pixel
            color = (pixel_bright, pixel_bright, pixel_bright, pixel_alpha)
            img.putpixel((x, y), color)

            # Some streaks are 2 pixels wide near the head (bottom)
            if progress > 0.7 and random.random() < 0.3 and x + 1 < width:
                color2 = (pixel_bright, pixel_bright, pixel_bright, pixel_alpha // 2)
                img.putpixel((x + 1, y), color2)

    return img

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    output_dir = os.path.join(project_dir, 'data', 'textures')

    os.makedirs(output_dir, exist_ok=True)

    # Generate 10 intensity-based foreground textures
    # Close rain = long streaks (motion blur), density increases with intensity
    print("Generating 10 intensity-based foreground textures (long streaks)...")
    for intensity in range(1, 11):
        # Density scales from ~20 at intensity 1 to ~100 at intensity 10
        num_streaks = 20 + int((intensity - 1) * (100 - 20) / 9)
        filename = f"rain_intensity_{intensity:02d}.png"
        print(f"  {filename}: {num_streaks} streaks (long, 30-70px)")

        tex = generate_rain_texture(256, 256, num_streaks=num_streaks,
                                    seed=100 + intensity,
                                    min_length=30, max_length=70)
        tex.save(os.path.join(output_dir, filename), 'PNG')

    # Generate mid-layer texture (medium distance)
    # Medium streaks, moderate density
    print("\nGenerating rain_layer_mid.png (medium streaks, 20-50px)...")
    rain_mid = generate_rain_texture(256, 256, num_streaks=40, seed=200,
                                     min_length=20, max_length=50)
    rain_mid.save(os.path.join(output_dir, 'rain_layer_mid.png'), 'PNG')

    # Generate far-layer texture (background)
    # Short streaks (distant rain appears as small drops), lower density
    print("Generating rain_layer_far.png (short streaks, 10-35px)...")
    rain_far = generate_rain_texture(256, 256, num_streaks=35, seed=201,
                                     min_length=10, max_length=35)
    rain_far.save(os.path.join(output_dir, 'rain_layer_far.png'), 'PNG')

    # Keep legacy textures for backwards compatibility
    print("\nGenerating legacy textures for compatibility...")
    rain_tex = generate_rain_texture(256, 256, num_streaks=50, seed=42,
                                     min_length=10, max_length=40)
    rain_tex.save(os.path.join(output_dir, 'rain_streaks.png'), 'PNG')
    print("  rain_streaks.png")

    print("\nDone! Generated 13 rain textures.")

if __name__ == '__main__':
    main()
