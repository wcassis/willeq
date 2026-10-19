#pragma once

#include <string>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
class ConstrainedTextureCache;

// B1096 (#416): install the EQE1 name -> archive resolution that
// loadZoneStep0_EQE1 (renderer_zone_load.cpp) used to install, EARLY --
// once, in initLoadingScreen, right after willeq.eqe1 opens (the UI-atlas
// block) and before anything pre-zone can ask the constrained cache for a
// name whose owner is not willeq.eqe1.
//
// Why the renderer, not the test, is the owner of this function: the defect
// was an ORDERING one -- the player's race model (the paperdoll) is built on
// the render thread before Step0 runs, and with only willeq.eqe1 open the
// all-or-nothing rule (ConstrainedTextureCache::getOrLoad's
// "EQE1 is configured but texture not found" branch, a LOG_FATAL + exit(1))
// took the process down. The fix is one statement's worth of production
// code; it must be the SAME statement the test drives, so it is factored
// into this TU and both the renderer's init and tests/
// test_pre_zone_eqe1_resolution.cpp call into it.
//
// No-op when there is no EQE1 tree configured (eqe1Path empty -- the
// --eqe1-path-less / headless runs) or when the texture cache is not wired
// (it is set by Application before initLoadingScreen). Idempotent:
// loadZoneStep0_EQE1 still re-registers (the index's isLoaded() guard makes
// the second load a no-op, and re-setting the resolver is harmless).
void initializeEQE1EarlyResolution(IrrlichtRenderer& renderer,
                                   ConstrainedTextureCache* cache);

} // namespace Graphics
} // namespace EQT
