#ifndef EQT_GRAPHICS_ANIMATION_MAPPING_H
#define EQT_GRAPHICS_ANIMATION_MAPPING_H

#include <string>

namespace EQT {
namespace Graphics {

// Animation source map - maps race codes to the skeleton that has animation data
// Based on EQSage's animationMap from sage/lib/util/animation-helper.js
// This allows models to borrow animations from a "base" model that has the master animation set.
// Key behavior: model-specific animations are NEVER overwritten - only missing animations are added.
std::string getAnimationSourceCode(const std::string& raceCode);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ANIMATION_MAPPING_H
