// RDP remote-desktop integration -- EQT::Graphics::RdpComponent.
// Split out of irrlicht_renderer.cpp (F14) -- verbatim move, no behavior change.
//
// L01 (Batch L, refactor/L_renderer_component_extraction.md): moved again,
// from src/client/graphics/renderer_rdp.cpp to here, and the bodies rebased
// from IrrlichtRenderer onto EQT::Graphics::RdpComponent. Still verbatim --
// the only edits inside a body are the four core-member substitutions
// RendererContext allows (driver_ -> ctx_.driver, device_ -> ctx_.device,
// config_ -> ctx_.config, eventReceiver_ -> ctx_.eventReceiver). No method
// that stayed on IrrlichtRenderer is called and no IrrlichtRenderer static or
// nested type is named, so nothing else changed. getRDPClientCount() had zero
// callers tree-wide and published no event; it was deleted per §Mechanism 5.
// See the L01 Review.

#include "client/graphics/rdp/rdp_component.h"
#include "client/graphics/renderer_event_receiver.h"  // L01: the moved bodies call ctx_.eventReceiver->isKeyDown()/isLeftButtonHeld()
#include "common/logging.h"
#include <fmt/format.h>

namespace EQT {
namespace Graphics {

// ============================================================================
// RDP Server Integration
// ============================================================================

#ifdef WITH_RDP

bool RdpComponent::initRDP(uint16_t port) {
    if (rdpServer_) {
        LOG_WARN(MOD_GRAPHICS, "RDP server already initialized");
        return true;
    }

    rdpServer_ = std::make_unique<RDPServer>();

    if (!rdpServer_->initialize(port)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to initialize RDP server on port {}", port);
        rdpServer_.reset();
        return false;
    }

    // Set resolution to match window size
    if (ctx_.driver) {
        auto screenSize = ctx_.driver->getScreenSize();
        rdpServer_->setResolution(screenSize.Width, screenSize.Height);
    } else {
        rdpServer_->setResolution(ctx_.config.width, ctx_.config.height);
    }

    // Set up input callbacks to route RDP input to the renderer
    rdpServer_->setKeyboardCallback([this](uint16_t flags, uint8_t scancode) {
        handleRDPKeyboard(flags, scancode);
    });

    rdpServer_->setMouseCallback([this](uint16_t flags, uint16_t x, uint16_t y) {
        handleRDPMouse(flags, x, y);
    });

    // T317: the client's keyboard lock state, sent once at connect and again
    // whenever it changes. Without this, a session that connects with caps
    // lock already on has no way to know.
    rdpServer_->setSyncCallback([this](uint32_t syncFlags) {
        handleRDPSynchronize(syncFlags);
    });

    LOG_INFO(MOD_GRAPHICS, "RDP server initialized on port {}", port);
    return true;
}

bool RdpComponent::startRDPServer() {
    if (!rdpServer_) {
        LOG_ERROR(MOD_GRAPHICS, "RDP server not initialized");
        return false;
    }

    if (!rdpServer_->start()) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to start RDP server");
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "RDP server started");
    return true;
}

void RdpComponent::stopRDPServer() {
    if (rdpServer_) {
        rdpServer_->stop();
        LOG_INFO(MOD_GRAPHICS, "RDP server stopped");
    }
}

bool RdpComponent::isRDPRunning() const {
    return rdpServer_ && rdpServer_->isRunning();
}

void RdpComponent::captureFrameForRDP() {
    if (!rdpServer_ || !rdpServer_->isRunning() || rdpServer_->getClientCount() == 0) {
        return;
    }

    if (!ctx_.driver) {
        return;
    }

    // Capture the current framebuffer
    irr::video::IImage* screenshot = ctx_.driver->createScreenShot();
    if (!screenshot) {
        return;
    }

    // Get image dimensions and data
    irr::core::dimension2d<irr::u32> size = screenshot->getDimension();
    uint32_t width = size.Width;
    uint32_t height = size.Height;

    // Convert to BGRA format if needed
    // Irrlicht's software renderer typically uses A8R8G8B8 (which is BGRA in memory)
    irr::video::ECOLOR_FORMAT format = screenshot->getColorFormat();

    if (format == irr::video::ECF_A8R8G8B8) {
        // Direct copy - format matches
        // Use lock() to get raw pixel data pointer
        const uint8_t* data = static_cast<const uint8_t*>(screenshot->lock());
        if (data) {
            uint32_t pitch = width * 4;  // 4 bytes per pixel for BGRA
            rdpServer_->updateFrame(data, width, height, pitch);
            screenshot->unlock();
        }
    } else {
        // Need to convert - create a temporary buffer
        std::vector<uint8_t> bgraData(width * height * 4);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                irr::video::SColor color = screenshot->getPixel(x, y);
                size_t offset = (y * width + x) * 4;
                bgraData[offset + 0] = color.getBlue();
                bgraData[offset + 1] = color.getGreen();
                bgraData[offset + 2] = color.getRed();
                bgraData[offset + 3] = color.getAlpha();
            }
        }

        rdpServer_->updateFrame(bgraData.data(), width, height, width * 4);
    }

    screenshot->drop();
}

void RdpComponent::handleRDPKeyboard(uint16_t flags, uint8_t scancode) {
    if (!ctx_.device || !ctx_.eventReceiver) {
        return;
    }

    // Translate RDP scancode to Irrlicht key code
    bool extended = (flags & 0x0100) != 0;  // KBD_FLAGS_EXTENDED
    bool released = (flags & 0x8000) != 0;  // KBD_FLAGS_RELEASE

    irr::EKEY_CODE keyCode = rdpScancodeToIrrlicht(scancode, extended);
    if (keyCode == irr::KEY_KEY_CODES_COUNT) {
        // Unknown key
        return;
    }

    // Get character for text input
    bool shift = ctx_.eventReceiver->isKeyDown(irr::KEY_LSHIFT) ||
                 ctx_.eventReceiver->isKeyDown(irr::KEY_RSHIFT);
    // T317: caps lock was hardcoded false here, so typing over RDP with caps
    // lock on produced the wrong case. It is a TOGGLE, not a held modifier --
    // isKeyDown() cannot answer for it -- so rdpLockKeys_ remembers it, fed by
    // this key stream and by the client's KBD_SYNC message. Update BEFORE
    // translating: pressing caps lock takes effect from that keystroke on.
    rdpLockKeys_.applyKeyEvent(flags, scancode);
    wchar_t character = rdpScancodeToChar(scancode, shift, rdpLockKeys_.capsLock());

    // Create Irrlicht key event
    irr::SEvent event;
    event.EventType = irr::EET_KEY_INPUT_EVENT;
    event.KeyInput.Key = keyCode;
    event.KeyInput.Char = character;
    event.KeyInput.PressedDown = !released;
    event.KeyInput.Shift = shift;
    event.KeyInput.Control = ctx_.eventReceiver->isKeyDown(irr::KEY_LCONTROL) ||
                             ctx_.eventReceiver->isKeyDown(irr::KEY_RCONTROL);

    // Post event to Irrlicht device
    ctx_.device->postEventFromUser(event);
}

void RdpComponent::handleRDPSynchronize(uint32_t syncFlags) {
    rdpLockKeys_.applySynchronize(syncFlags);
}

void RdpComponent::handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y) {
    if (!ctx_.device || !ctx_.eventReceiver) {
        return;
    }

    // Determine mouse event type
    irr::EMOUSE_INPUT_EVENT eventType = rdpMouseFlagsToIrrlicht(flags);

    // Create Irrlicht mouse event
    irr::SEvent event;
    event.EventType = irr::EET_MOUSE_INPUT_EVENT;
    event.MouseInput.X = x;
    event.MouseInput.Y = y;
    event.MouseInput.Event = eventType;

    // Handle wheel delta
    if (eventType == irr::EMIE_MOUSE_WHEEL) {
        event.MouseInput.Wheel = rdpGetWheelDelta(flags);
    } else {
        event.MouseInput.Wheel = 0.0f;
    }

    // Set button states
    event.MouseInput.ButtonStates = 0;
    if (ctx_.eventReceiver->isLeftButtonHeld()) {  // B1066: physical, not the look view
        event.MouseInput.ButtonStates |= irr::EMBSM_LEFT;
    }
    if (ctx_.eventReceiver->isRightButtonHeld()) {  // B1066
        event.MouseInput.ButtonStates |= irr::EMBSM_RIGHT;
    }

    // Post event to Irrlicht device
    ctx_.device->postEventFromUser(event);
}

#endif // WITH_RDP

} // namespace Graphics
} // namespace EQT
