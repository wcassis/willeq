#pragma once

#include <irrlicht.h>
#include <string>

namespace EQT {
namespace Graphics {

/**
 * Free functions for generating procedural texture atlases.
 * Used by the offline generate_textures tool.
 * Each function takes an IVideoDriver* and returns an IImage* (caller must drop()).
 */

/**
 * Generate the particle effect atlas (64x64, 4x4 tiles of 16px each).
 * Contains dust, firefly, mist, pollen, sand, leaf, snowflake, ember,
 * foam, droplet, ripple ring, snow patch, rain streak shapes.
 */
irr::video::IImage* generateParticleAtlas(irr::video::IVideoDriver* driver);

/**
 * Generate the creature/boids atlas (64x64, 4x4 tiles of 16px each).
 * Contains bird, bat, butterfly, dragonfly, crow, seagull, firefly shapes.
 */
irr::video::IImage* generateCreatureAtlas(irr::video::IVideoDriver* driver);

/**
 * Generate the tumbleweed texture (64x64).
 * Contains radial branch pattern with sub-branches and connecting arcs.
 */
irr::video::IImage* generateTumbleweedTexture(irr::video::IVideoDriver* driver);

/**
 * Generate a single seamless storm cloud texture frame using Perlin noise.
 * @param driver Video driver
 * @param seed Random seed for this frame
 * @param size Texture size (default 256)
 * @param octaves Perlin noise octaves (default 4)
 * @param persistence Noise persistence (default 0.5)
 * @param cloudColorR Cloud color red component 0-1 (default 0.4)
 * @param cloudColorG Cloud color green component 0-1 (default 0.42)
 * @param cloudColorB Cloud color blue component 0-1 (default 0.45)
 * @return Generated image (caller must drop())
 */
irr::video::IImage* generateCloudFrame(irr::video::IVideoDriver* driver,
                                        int seed,
                                        int size = 256,
                                        int octaves = 4,
                                        float persistence = 0.5f,
                                        float cloudColorR = 0.4f,
                                        float cloudColorG = 0.42f,
                                        float cloudColorB = 0.45f);

/**
 * Generate a blended cloud frame by interpolating two source images.
 * @param driver Video driver
 * @param img1 First source image
 * @param img2 Second source image
 * @param t Blend factor (0.0 = img1, 1.0 = img2)
 * @return Blended image (caller must drop())
 */
irr::video::IImage* generateBlendedCloudFrame(irr::video::IVideoDriver* driver,
                                               irr::video::IImage* img1,
                                               irr::video::IImage* img2,
                                               float t);

/**
 * Generate the spell particle fallback texture (32x32 soft circle).
 * White with cosine-falloff alpha.
 */
irr::video::IImage* generateSpellParticleFallback(irr::video::IVideoDriver* driver);

/**
 * Generate the software mouse cursor texture (8x10 arrow).
 * Black outline, white fill, transparent background.
 */
irr::video::IImage* generateSoftwareCursor(irr::video::IVideoDriver* driver);

/**
 * Generate a rain streak texture (256x256).
 * Vertical streaks with gradient alpha, tileable vertically.
 * @param numStreaks Number of rain streaks
 * @param seed Random seed
 * @param minLength Minimum streak length in pixels
 * @param maxLength Maximum streak length in pixels
 */
irr::video::IImage* generateRainTexture(irr::video::IVideoDriver* driver,
                                         int numStreaks = 50, int seed = 42,
                                         int minLength = 10, int maxLength = 40);

/**
 * Generate a snow flake texture (256x256).
 * Random soft white dots/circles, tileable.
 * @param density Number of flakes
 * @param seed Random seed
 * @param minSize Minimum flake radius
 * @param maxSize Maximum flake radius
 * @param alphaBase Base alpha value (0-255)
 */
irr::video::IImage* generateSnowTexture(irr::video::IVideoDriver* driver,
                                          int density = 300, int seed = 42,
                                          float minSize = 1.0f, float maxSize = 3.0f,
                                          int alphaBase = 180);

} // namespace Graphics
} // namespace EQT
