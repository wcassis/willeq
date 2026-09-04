#ifndef EQT_GRAPHICS_RENDER_PASS_TIMER_H
#define EQT_GRAPHICS_RENDER_PASS_TIMER_H

// L13 (refactor/L13_pairwise_ownership.md): hoisted out of class
// IrrlichtRenderer, where it was a nested class. It is a nested CLASS with a
// virtual base (irr::scene::ILightManager), so unlike the three plain structs
// it does not belong in renderer_types.h -- that header is included by
// renderer_context.h, which must stay light.
//
// class IrrlichtRenderer keeps `using RenderPassTimer = ...` so every existing
// spelling (including IrrlichtRenderer::RenderPassTimer:: on the out-of-line
// OnRenderPassPreRender body) still compiles. As a nested class it had
// implicit access to IrrlichtRenderer's private members; hoisted, it needs
// the friend line IrrlichtRenderer now declares for it.

#include <irrlicht.h>
#include <chrono>
#include <cstdint>

// P52: GPU-sync per-phase timer-query hooks -- see irrlicht_renderer.h for
// why these are declared extern locally rather than via gpu_sync_probe.h.
extern void eqtGPUPhaseBegin(int phase);
extern void eqtGPUPhaseEnd(int phase);

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;

// Render pass timer - hooks into Irrlicht's ILightManager to time individual render passes
class RenderPassTimer : public irr::scene::ILightManager {
public:
    // Per-pass accumulated times (reset each frame via reset())
    int64_t solidTime = 0;
    int64_t transparentTime = 0;  // TRANSPARENT + TRANSPARENT_EFFECT
    int64_t skyboxTime = 0;
    int64_t otherTime = 0;        // CAMERA + LIGHT + SHADOW
    int nodeCount = 0;

    void reset() {
        solidTime = 0; transparentTime = 0; skyboxTime = 0; otherTime = 0;
        nodeCount = 0; firstPassSeen_ = false;
    }

    void OnPreRender(irr::core::array<irr::scene::ISceneNode*>& lightList) override {
        preRenderTime_ = std::chrono::steady_clock::now();
    }
    void OnPostRender() override {}
    void OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) override;
    // Implementation in renderer_zone_draw.cpp (needs access to IrrlichtRenderer)
    void OnRenderPassPostRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) override {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - passStart_).count();
        switch (renderPass) {
            case irr::scene::ESNRP_SOLID:
                solidTime += elapsed;
                // P52: close the entity-pass GPU query if OnRenderPassPreRender opened
                // one (only happens when manualZoneDrawEnabled_ split zone from entity
                // draw this frame — see OnRenderPassPreRender in renderer_zone_draw.cpp).
                if (entityGPUQueryActive_) {
                    eqtGPUPhaseEnd(1 /* GPUSyncPhase::EntityPass */);
                    entityGPUQueryActive_ = false;
                }
                break;
            case irr::scene::ESNRP_TRANSPARENT:
            case irr::scene::ESNRP_TRANSPARENT_EFFECT:
                transparentTime += elapsed; break;
            case irr::scene::ESNRP_SKY_BOX:
                skyboxTime += elapsed; break;
            default:
                otherTime += elapsed; break;
        }
    }
    void OnNodePreRender(irr::scene::ISceneNode* node) override { nodeCount++; }
    void OnNodePostRender(irr::scene::ISceneNode* node) override {}

    // Time between OnPreRender and first pass = animate+register overhead
    int64_t getAnimateTime() const {
        if (!firstPassSeen_) return 0;
        return std::chrono::duration_cast<std::chrono::microseconds>(
            firstPassStart_ - preRenderTime_).count();
    }

    // Set renderer for manual zone draw hook
    void setRenderer(IrrlichtRenderer* r) { renderer_ = r; }

private:
    IrrlichtRenderer* renderer_ = nullptr;
    std::chrono::steady_clock::time_point passStart_;
    std::chrono::steady_clock::time_point firstPassStart_;
    std::chrono::steady_clock::time_point preRenderTime_;
    bool firstPassSeen_ = false;
    irr::scene::E_SCENE_NODE_RENDER_PASS currentPass_ = irr::scene::ESNRP_NONE;
    // P52: tracks whether a GPU timer query for the entity pass (GPUSyncPhase::EntityPass)
    // was opened this SOLID pass, so OnRenderPassPostRender only closes it when it was
    // actually opened (manualZoneDrawEnabled_ gates whether zone/entity are split at all).
    bool entityGPUQueryActive_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDER_PASS_TIMER_H
