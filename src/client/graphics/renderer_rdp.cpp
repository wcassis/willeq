// IrrlichtRenderer RDP remote-desktop integration.
// Split out of irrlicht_renderer.cpp (F14) — verbatim move, no behavior change.

#include "client/graphics/irrlicht_renderer.h"
#include "common/logging.h"
#include <fmt/format.h>

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif

namespace EQT {
namespace Graphics {

// ============================================================================
// RDP Server Integration
// ============================================================================

#ifdef WITH_RDP

bool IrrlichtRenderer::initRDP(uint16_t port) {
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
    if (driver_) {
        auto screenSize = driver_->getScreenSize();
        rdpServer_->setResolution(screenSize.Width, screenSize.Height);
    } else {
        rdpServer_->setResolution(config_.width, config_.height);
    }

    // Set up input callbacks to route RDP input to the renderer
    rdpServer_->setKeyboardCallback([this](uint16_t flags, uint8_t scancode) {
        handleRDPKeyboard(flags, scancode);
    });

    rdpServer_->setMouseCallback([this](uint16_t flags, uint16_t x, uint16_t y) {
        handleRDPMouse(flags, x, y);
    });

    LOG_INFO(MOD_GRAPHICS, "RDP server initialized on port {}", port);
    return true;
}

bool IrrlichtRenderer::startRDPServer() {
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

void IrrlichtRenderer::stopRDPServer() {
    if (rdpServer_) {
        rdpServer_->stop();
        LOG_INFO(MOD_GRAPHICS, "RDP server stopped");
    }
}

bool IrrlichtRenderer::isRDPRunning() const {
    return rdpServer_ && rdpServer_->isRunning();
}

size_t IrrlichtRenderer::getRDPClientCount() const {
    return rdpServer_ ? rdpServer_->getClientCount() : 0;
}

void IrrlichtRenderer::captureFrameForRDP() {
    if (!rdpServer_ || !rdpServer_->isRunning() || rdpServer_->getClientCount() == 0) {
        return;
    }

    if (!driver_) {
        return;
    }

    // Capture the current framebuffer
    irr::video::IImage* screenshot = driver_->createScreenShot();
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

void IrrlichtRenderer::handleRDPKeyboard(uint16_t flags, uint8_t scancode) {
    if (!device_ || !eventReceiver_) {
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
    bool shift = eventReceiver_->isKeyDown(irr::KEY_LSHIFT) ||
                 eventReceiver_->isKeyDown(irr::KEY_RSHIFT);
    bool capsLock = false;  // TODO: track caps lock state
    wchar_t character = rdpScancodeToChar(scancode, shift, capsLock);

    // Create Irrlicht key event
    irr::SEvent event;
    event.EventType = irr::EET_KEY_INPUT_EVENT;
    event.KeyInput.Key = keyCode;
    event.KeyInput.Char = character;
    event.KeyInput.PressedDown = !released;
    event.KeyInput.Shift = shift;
    event.KeyInput.Control = eventReceiver_->isKeyDown(irr::KEY_LCONTROL) ||
                             eventReceiver_->isKeyDown(irr::KEY_RCONTROL);

    // Post event to Irrlicht device
    device_->postEventFromUser(event);
}

void IrrlichtRenderer::handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y) {
    if (!device_ || !eventReceiver_) {
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
    if (eventReceiver_->isLeftButtonDown()) {
        event.MouseInput.ButtonStates |= irr::EMBSM_LEFT;
    }
    if (eventReceiver_->isRightButtonDown()) {
        event.MouseInput.ButtonStates |= irr::EMBSM_RIGHT;
    }

    // Post event to Irrlicht device
    device_->postEventFromUser(event);
}

#endif // WITH_RDP

} // namespace Graphics
} // namespace EQT
