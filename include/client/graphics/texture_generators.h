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

} // namespace Graphics
} // namespace EQT
