// The two-method renderer surface that zone-load game logic actually uses.
//
// K16 (board B006, Will-approved 2026-09-03). BridgeComponent::InitGraphics
// was handed an EQT::Graphics::IrrlichtRenderer*, but K16 re-measured the body
// at its tip and the pointer reaches exactly two calls --
// initLoadingScreen(const RendererConfig&) and initDeferredGraphics(). It is
// not stored, not passed on, not compared against anything else. So the game
// side names this interface instead of a concrete renderer class, and
// IrrlichtRenderer implements it.
//
// RendererConfig comes from renderer_types.h deliberately: that is the small
// free-standing types header (J17's split), not the 1,8xx-line umbrella
// renderer header. Implementers and test stubs both need the complete type.
#ifndef EQT_GRAPHICS_ZONE_LOAD_SURFACE_H
#define EQT_GRAPHICS_ZONE_LOAD_SURFACE_H

#include "client/graphics/renderer_types.h"

namespace EQT {
namespace Graphics {

class IZoneLoadSurface {
public:
    virtual ~IZoneLoadSurface() = default;

    // Bring up the window/device far enough to show progress. False on failure.
    virtual bool initLoadingScreen(const RendererConfig& config) = 0;

    // One-time deferred init (render distance, spell VFX) after the first
    // successful initLoadingScreen().
    virtual void initDeferredGraphics() = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_LOAD_SURFACE_H
