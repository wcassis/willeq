// B998 (GitLab #318, #204 Leg M) -- IrrlichtRenderer::drawPreZoneUI(), the
// pre-zone UI frame. Its own TU (M00). Before a zone exists the render thread
// draws only the network-handshake loading screen
// (LoadingThread::drawLoadingFrame, from Application::render()); this is the
// same slot drawing the static UI instead, for the character-select screen
// and the Character Creation screen it hosts. Nothing of the scene graph is
// touched: no zone, no camera, no entities -- a cleared screen, the UI
// renderer, the DRM software cursor.

#include "client/graphics/irrlicht_renderer.h"

#include "client/core/memory_ledger.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/renderer_event_receiver.h"  // B1099
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

bool IrrlichtRenderer::drawPreZoneUI(const std::function<void(UIRenderer&, const UILayout&)>& draw) {
    if (!device_ || !driver_) return false;
    if (!device_->run()) return false;

    // The oracle draws the 3D character-select scene here (OUT of this unit);
    // the clear is fable's, flagged in char_select_panel.h.
    driver_->beginScene(true, true, irr::video::SColor(255, 12, 12, 20));
    if (frame_ && frame_->uiRenderer_) {
        eqt::memory::MemoryScope scope(eqt::memory::Category::UI);
        frame_->uiRenderer_->beginFrame();
        draw(*frame_->uiRenderer_, frame_->uiLayout_);
        frame_->uiRenderer_->endFrame();
    }
    // The DRM software cursor, exactly as FrameComponent::renderFrameTail()
    // draws it in game (no hardware cursor on the Pi).
    if (softwareCursorTexture_) {
        auto* cursor = device_->getCursorControl();
        if (cursor && cursor->isVisible()) {
            const auto pos = cursor->getPosition();
            driver_->draw2DImage(softwareCursorTexture_, irr::core::position2di(pos.X, pos.Y),
                                 irr::core::rect<irr::s32>(0, 0, 8, 10), nullptr,
                                 irr::video::SColor(255, 255, 255, 255), true);
        }
    }
    driver_->endScene();
    return true;
}

// B1099 (GitLab #419): see loading_screen_quit.h. The ESC latch is the one
// RendererEventReceiver::OnEvent() already sets for an unfocused chat (no chat
// exists before a zone), so a loading-screen ESC is read, and spent, here.
LoadingScreenQuit IrrlichtRenderer::pollLoadingScreenQuit(bool loaderOwnsContext) {
    if (!device_) return LoadingScreenQuit::None;
    if (!loadingScreenMayPump(loaderOwnsContext, config_.constrainedConfig.useDRM)) {
        return LoadingScreenQuit::None;
    }
    const bool running = device_->run();
    const bool escape = eventReceiver_ && eventReceiver_->escapeKeyPressed();
    const bool flag = eventReceiver_ && eventReceiver_->quitRequested();
    return loadingScreenQuitDecision(running, escape, flag);
}

}  // namespace Graphics
}  // namespace EQT
