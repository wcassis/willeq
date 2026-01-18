# WillEQ Native RDP Support Implementation Guide

**Status: IMPLEMENTED**

## Overview

This guide documents the native RDP (Remote Desktop Protocol) support in WillEQ. RDP provides an **additional display option** alongside the existing Xvfb + x11vnc approach, offering better integration with Windows clients and improved compression via RemoteFX/NSCodec.

**Display Options Available:**
1. **Direct X11** - Native display with GPU/software rendering
2. **Xvfb + x11vnc** - VNC-based remote access (existing)
3. **Native RDP** - Direct RDP streaming via FreeRDP (this feature)

## Architecture

WillEQ supports multiple display backends:
- **Xvfb + x11vnc** - Virtual framebuffer with VNC export (existing)
- **Irrlicht** - 3D rendering engine with software rendering
- **Native RDP** - Direct RDP streaming via FreeRDP (new)

## RDP Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      WillEQ Client                       │
├─────────────────────────────────────────────────────────┤
│  IrrlichtRenderer                                        │
│    ├── render() → framebuffer (BGRA)                    │
│    └── getFrameBuffer()                                  │
├─────────────────────────────────────────────────────────┤
│  RDPServer (NEW)                                         │
│    ├── freerdp_listener (accepts connections)           │
│    ├── Frame encoder (RFX/NSC codec)                    │
│    ├── Input handler (keyboard/mouse → game events)     │
│    └── Session manager (multi-client support)           │
└─────────────────────────────────────────────────────────┘
            │                           ▲
            ▼                           │
    ┌───────────────┐           ┌───────────────┐
    │  RDP Client   │           │  RDP Client   │
    │  (mstsc.exe)  │           │  (FreeRDP)    │
    └───────────────┘           └───────────────┘
```

## Dependencies

### Required Libraries

Add to `CMakeLists.txt`:

```cmake
# Find FreeRDP
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREERDP freerdp3 winpr3)

if(FREERDP_FOUND)
    option(WITH_RDP "Enable native RDP support" ON)
else()
    option(WITH_RDP "Enable native RDP support" OFF)
endif()

if(WITH_RDP)
    add_definitions(-DWITH_RDP)
    include_directories(${FREERDP_INCLUDE_DIRS})
    link_directories(${FREERDP_LIBRARY_DIRS})
    target_link_libraries(willeq ${FREERDP_LIBRARIES})
endif()
```

### Ubuntu Installation

```bash
# Ubuntu 24.04+ (FreeRDP 3.x)
sudo apt-get install -y freerdp3-dev libwinpr3-dev

# Older Ubuntu versions (FreeRDP 2.x)
sudo apt-get install -y libfreerdp-dev libfreerdp-server-dev libwinpr-dev
```

The build system automatically detects which version is installed and configures accordingly.

## Implementation Files

### File Structure

```
include/client/graphics/rdp/
├── rdp_server.h           # RDP server class header
├── rdp_peer_context.h     # Per-client context structure
└── rdp_input_handler.h    # Scancode translation declarations

src/client/graphics/rdp/
├── rdp_server.cpp         # Main RDP server implementation
├── rdp_peer_context.cpp   # Context initialization/cleanup
└── rdp_input_handler.cpp  # RDP scancode to Irrlicht translation

tests/
├── test_rdp_server.cpp           # 20 unit tests for RDP server
└── test_rdp_input_handler.cpp    # 35 input translation tests
```

### Modified Files

- `CMakeLists.txt` - FreeRDP detection and linking
- `include/client/graphics/irrlicht_renderer.h` - RDP member and methods
- `src/client/graphics/irrlicht_renderer.cpp` - RDP integration
- `src/client/main.cpp` - Command line and JSON config parsing

---

## Core Implementation

### 1. RDP Server Header (`include/client/graphics/rdp/rdp_server.h`)

```cpp
#pragma once

#ifdef WITH_RDP

#include <freerdp/freerdp.h>
#include <freerdp/listener.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/channels/wtsvc.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

namespace willeq {
namespace graphics {

// Forward declarations
class IrrlichtRenderer;

// Callback types for input events
using KeyboardCallback = std::function<void(uint16_t flags, uint8_t scancode)>;
using MouseCallback = std::function<void(uint16_t flags, uint16_t x, uint16_t y)>;

// Per-peer context (one per connected client)
struct RDPPeerContext {
    rdpContext _p;  // Must be first member (FreeRDP convention)
    
    RFX_CONTEXT* rfx_context;
    NSC_CONTEXT* nsc_context;
    wStream* encode_stream;
    
    HANDLE vcm;                    // Virtual channel manager
    uint32_t frame_id;
    bool activated;
    
    // Back-pointer to server
    class RDPServer* server;
};

class RDPServer {
public:
    RDPServer();
    ~RDPServer();
    
    // Configuration
    bool initialize(uint16_t port = 3389);
    void setCertificate(const std::string& certPath, const std::string& keyPath);
    void setResolution(uint32_t width, uint32_t height);
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_; }
    
    // Frame updates
    void updateFrame(const uint8_t* frameData, uint32_t width, uint32_t height, 
                     uint32_t pitch, uint32_t format);
    
    // Input callbacks
    void setKeyboardCallback(KeyboardCallback cb) { keyboardCallback_ = cb; }
    void setMouseCallback(MouseCallback cb) { mouseCallback_ = cb; }
    
    // Internal callbacks (called by FreeRDP)
    void onKeyboardEvent(uint16_t flags, uint8_t scancode);
    void onMouseEvent(uint16_t flags, uint16_t x, uint16_t y);
    
private:
    // FreeRDP callbacks (static, then forward to instance)
    static BOOL peerContextNew(freerdp_peer* client, rdpContext* ctx);
    static void peerContextFree(freerdp_peer* client, rdpContext* ctx);
    static BOOL peerPostConnect(freerdp_peer* client);
    static BOOL peerActivate(freerdp_peer* client);
    static BOOL peerAccepted(freerdp_listener* instance, freerdp_peer* client);
    
    // Input callbacks
    static BOOL onSynchronize(rdpInput* input, uint32_t flags);
    static BOOL onKeyboard(rdpInput* input, uint16_t flags, uint8_t code);
    static BOOL onMouse(rdpInput* input, uint16_t flags, uint16_t x, uint16_t y);
    static BOOL onExtendedMouse(rdpInput* input, uint16_t flags, uint16_t x, uint16_t y);
    
    // Worker threads
    void listenerThread();
    void peerThread(freerdp_peer* client);
    
    // Frame encoding and sending
    void sendFrame(RDPPeerContext* context, const uint8_t* data, 
                   uint32_t width, uint32_t height);
    
private:
    freerdp_listener* listener_;
    std::thread listenerThread_;
    std::vector<std::thread> peerThreads_;
    
    std::atomic<bool> running_;
    std::mutex peersMutex_;
    std::vector<RDPPeerContext*> activePeers_;
    
    // Configuration
    uint16_t port_;
    uint32_t width_;
    uint32_t height_;
    std::string certPath_;
    std::string keyPath_;
    
    // Frame buffer (latest frame to send)
    std::mutex frameMutex_;
    std::vector<uint8_t> frameBuffer_;
    uint32_t frameWidth_;
    uint32_t frameHeight_;
    bool frameReady_;
    
    // Input callbacks
    KeyboardCallback keyboardCallback_;
    MouseCallback mouseCallback_;
};

} // namespace graphics
} // namespace willeq

#endif // WITH_RDP
```

### 2. RDP Server Implementation (`src/client/graphics/rdp/rdp_server.cpp`)

```cpp
#ifdef WITH_RDP

#include "client/graphics/rdp/rdp_server.h"
#include <winpr/ssl.h>
#include <winpr/winsock.h>
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <iostream>

namespace willeq {
namespace graphics {

RDPServer::RDPServer()
    : listener_(nullptr)
    , running_(false)
    , port_(3389)
    , width_(1024)
    , height_(768)
    , frameWidth_(0)
    , frameHeight_(0)
    , frameReady_(false)
{
}

RDPServer::~RDPServer() {
    stop();
}

bool RDPServer::initialize(uint16_t port) {
    port_ = port;
    
    // Initialize WinPR/SSL
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
    
    // Initialize Winsock (cross-platform via WinPR)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
    
    // Create listener
    listener_ = freerdp_listener_new();
    if (!listener_) {
        std::cerr << "Failed to create FreeRDP listener" << std::endl;
        return false;
    }
    
    // Set callbacks
    listener_->PeerAccepted = peerAccepted;
    listener_->info = this;
    
    return true;
}

void RDPServer::setCertificate(const std::string& certPath, const std::string& keyPath) {
    certPath_ = certPath;
    keyPath_ = keyPath;
}

void RDPServer::setResolution(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
}

bool RDPServer::start() {
    if (running_) return true;
    
    if (!listener_) {
        std::cerr << "RDP server not initialized" << std::endl;
        return false;
    }
    
    // Open listener on all interfaces
    if (!listener_->Open(listener_, nullptr, port_)) {
        std::cerr << "Failed to open RDP listener on port " << port_ << std::endl;
        return false;
    }
    
    running_ = true;
    listenerThread_ = std::thread(&RDPServer::listenerThread, this);
    
    std::cout << "RDP server started on port " << port_ << std::endl;
    return true;
}

void RDPServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (listener_) {
        listener_->Close(listener_);
    }
    
    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }
    
    // Wait for all peer threads
    for (auto& t : peerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    peerThreads_.clear();
    
    if (listener_) {
        freerdp_listener_free(listener_);
        listener_ = nullptr;
    }
    
    WSACleanup();
    std::cout << "RDP server stopped" << std::endl;
}

void RDPServer::listenerThread() {
    HANDLE handles[32];
    DWORD count;
    
    while (running_) {
        count = listener_->GetEventHandles(listener_, handles, 32);
        if (count == 0) {
            std::cerr << "Failed to get listener event handles" << std::endl;
            break;
        }
        
        DWORD status = WaitForMultipleObjects(count, handles, FALSE, 100);
        
        if (status == WAIT_TIMEOUT) {
            continue;
        }
        
        if (status == WAIT_FAILED) {
            std::cerr << "WaitForMultipleObjects failed" << std::endl;
            break;
        }
        
        if (!listener_->CheckFileDescriptor(listener_)) {
            std::cerr << "Listener CheckFileDescriptor failed" << std::endl;
            break;
        }
    }
}

BOOL RDPServer::peerAccepted(freerdp_listener* instance, freerdp_peer* client) {
    RDPServer* server = static_cast<RDPServer*>(instance->info);
    
    // Set up peer context
    client->ContextSize = sizeof(RDPPeerContext);
    client->ContextNew = peerContextNew;
    client->ContextFree = peerContextFree;
    client->ContextExtra = server;
    
    if (!freerdp_peer_context_new(client)) {
        std::cerr << "Failed to create peer context" << std::endl;
        return FALSE;
    }
    
    // Start peer thread
    server->peerThreads_.emplace_back(&RDPServer::peerThread, server, client);
    
    return TRUE;
}

BOOL RDPServer::peerContextNew(freerdp_peer* client, rdpContext* ctx) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(ctx);
    RDPServer* server = static_cast<RDPServer*>(client->ContextExtra);
    
    context->server = server;
    context->frame_id = 0;
    context->activated = false;
    
    // Initialize RFX encoder
    context->rfx_context = rfx_context_new(TRUE);
    if (!context->rfx_context) {
        return FALSE;
    }
    
    if (!rfx_context_reset(context->rfx_context, server->width_, server->height_)) {
        rfx_context_free(context->rfx_context);
        return FALSE;
    }
    
    // Initialize NSC encoder
    context->nsc_context = nsc_context_new();
    if (!context->nsc_context) {
        rfx_context_free(context->rfx_context);
        return FALSE;
    }
    
    // Encoding stream
    context->encode_stream = Stream_New(nullptr, 65536);
    if (!context->encode_stream) {
        nsc_context_free(context->nsc_context);
        rfx_context_free(context->rfx_context);
        return FALSE;
    }
    
    // Virtual channel manager
    context->vcm = WTSOpenServerA(reinterpret_cast<LPSTR>(client->context));
    
    return TRUE;
}

void RDPServer::peerContextFree(freerdp_peer* client, rdpContext* ctx) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(ctx);
    
    if (context) {
        if (context->encode_stream) {
            Stream_Free(context->encode_stream, TRUE);
        }
        if (context->nsc_context) {
            nsc_context_free(context->nsc_context);
        }
        if (context->rfx_context) {
            rfx_context_free(context->rfx_context);
        }
        if (context->vcm) {
            WTSCloseServer(context->vcm);
        }
    }
}

void RDPServer::peerThread(freerdp_peer* client) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    rdpSettings* settings = client->context->settings;
    
    // Configure server settings
    if (!certPath_.empty()) {
        rdpCertificate* cert = freerdp_certificate_new_from_file(certPath_.c_str());
        if (cert) {
            freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert, 1);
        }
    }
    
    if (!keyPath_.empty()) {
        rdpPrivateKey* key = freerdp_key_new_from_file(keyPath_.c_str());
        if (key) {
            freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1);
        }
    }
    
    // Security settings
    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE);
    
    // Codec settings
    freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE);
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);
    
    // Set callbacks
    client->PostConnect = peerPostConnect;
    client->Activate = peerActivate;
    
    rdpInput* input = client->context->input;
    input->SynchronizeEvent = onSynchronize;
    input->KeyboardEvent = onKeyboard;
    input->MouseEvent = onMouse;
    input->ExtendedMouseEvent = onExtendedMouse;
    
    // Initialize client
    if (!client->Initialize(client)) {
        std::cerr << "Failed to initialize peer" << std::endl;
        goto cleanup;
    }
    
    std::cout << "RDP client connected: " << (client->hostname ? client->hostname : "local") 
              << std::endl;
    
    // Add to active peers
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        activePeers_.push_back(context);
    }
    
    // Main peer loop
    {
        HANDLE handles[32];
        DWORD count;
        
        while (running_) {
            count = client->GetEventHandles(client, handles, 32);
            if (count == 0) break;
            
            // Add VCM handle
            HANDLE vcmHandle = WTSVirtualChannelManagerGetEventHandle(context->vcm);
            handles[count++] = vcmHandle;
            
            DWORD status = WaitForMultipleObjects(count, handles, FALSE, 33); // ~30fps
            
            if (status == WAIT_FAILED) break;
            
            if (!client->CheckFileDescriptor(client)) break;
            
            if (!WTSVirtualChannelManagerCheckFileDescriptor(context->vcm)) break;
            
            // Send frame if ready and client activated
            if (context->activated && frameReady_) {
                std::lock_guard<std::mutex> lock(frameMutex_);
                if (frameReady_) {
                    sendFrame(context, frameBuffer_.data(), frameWidth_, frameHeight_);
                    frameReady_ = false;
                }
            }
        }
    }
    
    // Remove from active peers
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        activePeers_.erase(
            std::remove(activePeers_.begin(), activePeers_.end(), context),
            activePeers_.end()
        );
    }
    
cleanup:
    std::cout << "RDP client disconnected: " << (client->hostname ? client->hostname : "local") 
              << std::endl;
    client->Disconnect(client);
    freerdp_peer_context_free(client);
    freerdp_peer_free(client);
}

BOOL RDPServer::peerPostConnect(freerdp_peer* client) {
    rdpSettings* settings = client->context->settings;
    
    std::cout << "RDP client requested desktop: " 
              << freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth) << "x"
              << freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight) << "x"
              << freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth) << std::endl;
    
    return TRUE;
}

BOOL RDPServer::peerActivate(freerdp_peer* client) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    context->activated = true;
    std::cout << "RDP client activated" << std::endl;
    return TRUE;
}

// Input callbacks
BOOL RDPServer::onSynchronize(rdpInput* input, uint32_t flags) {
    return TRUE;
}

BOOL RDPServer::onKeyboard(rdpInput* input, uint16_t flags, uint8_t code) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(input->context);
    if (context && context->server) {
        context->server->onKeyboardEvent(flags, code);
    }
    return TRUE;
}

BOOL RDPServer::onMouse(rdpInput* input, uint16_t flags, uint16_t x, uint16_t y) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(input->context);
    if (context && context->server) {
        context->server->onMouseEvent(flags, x, y);
    }
    return TRUE;
}

BOOL RDPServer::onExtendedMouse(rdpInput* input, uint16_t flags, uint16_t x, uint16_t y) {
    return onMouse(input, flags, x, y);
}

void RDPServer::onKeyboardEvent(uint16_t flags, uint8_t scancode) {
    if (keyboardCallback_) {
        keyboardCallback_(flags, scancode);
    }
}

void RDPServer::onMouseEvent(uint16_t flags, uint16_t x, uint16_t y) {
    if (mouseCallback_) {
        mouseCallback_(flags, x, y);
    }
}

void RDPServer::updateFrame(const uint8_t* frameData, uint32_t width, uint32_t height,
                             uint32_t pitch, uint32_t format) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    
    // Copy frame data
    size_t size = pitch * height;
    if (frameBuffer_.size() != size) {
        frameBuffer_.resize(size);
    }
    memcpy(frameBuffer_.data(), frameData, size);
    frameWidth_ = width;
    frameHeight_ = height;
    frameReady_ = true;
}

void RDPServer::sendFrame(RDPPeerContext* context, const uint8_t* data,
                           uint32_t width, uint32_t height) {
    freerdp_peer* client = context->_p.peer;
    rdpSettings* settings = context->_p.settings;
    rdpUpdate* update = context->_p.update;
    
    bool useRemoteFx = freerdp_settings_get_bool(settings, FreeRDP_RemoteFxCodec);
    
    // Clear stream
    Stream_Clear(context->encode_stream);
    Stream_SetPosition(context->encode_stream, 0);
    
    // Begin frame
    SURFACE_FRAME_MARKER fm = {};
    fm.frameAction = SURFACECMD_FRAMEACTION_BEGIN;
    fm.frameId = context->frame_id;
    update->SurfaceFrameMarker(update->context, &fm);
    
    // Encode frame
    SURFACE_BITS_COMMAND cmd = {};
    cmd.destLeft = 0;
    cmd.destTop = 0;
    cmd.destRight = width;
    cmd.destBottom = height;
    cmd.bmp.bpp = 32;
    cmd.bmp.width = width;
    cmd.bmp.height = height;
    
    RFX_RECT rect = { 0, 0, (uint16_t)width, (uint16_t)height };
    
    if (useRemoteFx) {
        rfx_context_set_pixel_format(context->rfx_context, PIXEL_FORMAT_BGRA32);
        rfx_compose_message(context->rfx_context, context->encode_stream, &rect, 1,
                           data, width, height, width * 4);
        
        cmd.bmp.codecID = freerdp_settings_get_uint32(settings, FreeRDP_RemoteFxCodecId);
        cmd.cmdType = CMDTYPE_STREAM_SURFACE_BITS;
    } else {
        nsc_context_set_parameters(context->nsc_context, NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRA32);
        nsc_compose_message(context->nsc_context, context->encode_stream,
                           data, width, height, width * 4);
        
        cmd.bmp.codecID = freerdp_settings_get_uint32(settings, FreeRDP_NSCodecId);
        cmd.cmdType = CMDTYPE_SET_SURFACE_BITS;
    }
    
    cmd.bmp.bitmapDataLength = Stream_GetPosition(context->encode_stream);
    cmd.bmp.bitmapData = Stream_Buffer(context->encode_stream);
    
    update->SurfaceBits(update->context, &cmd);
    
    // End frame
    fm.frameAction = SURFACECMD_FRAMEACTION_END;
    fm.frameId = context->frame_id++;
    update->SurfaceFrameMarker(update->context, &fm);
}

} // namespace graphics
} // namespace willeq

#endif // WITH_RDP
```

---

## Integration with IrrlichtRenderer

### Modified IrrlichtRenderer Class

Add RDP support to the existing renderer:

```cpp
// In irrlicht_renderer.h, add:

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#endif

class IrrlichtRenderer {
public:
    // ... existing methods ...
    
#ifdef WITH_RDP
    // RDP support
    bool initRDP(uint16_t port = 3389);
    void setRDPCertificate(const std::string& cert, const std::string& key);
    bool startRDPServer();
    void stopRDPServer();
    bool isRDPRunning() const;
#endif

private:
    // ... existing members ...
    
#ifdef WITH_RDP
    std::unique_ptr<RDPServer> rdpServer_;
#endif
};

// In irrlicht_renderer.cpp, add:

#ifdef WITH_RDP

bool IrrlichtRenderer::initRDP(uint16_t port) {
    rdpServer_ = std::make_unique<RDPServer>();
    
    if (!rdpServer_->initialize(port)) {
        rdpServer_.reset();
        return false;
    }
    
    rdpServer_->setResolution(screenWidth_, screenHeight_);
    
    // Set input callbacks
    rdpServer_->setKeyboardCallback([this](uint16_t flags, uint8_t scancode) {
        // Convert RDP scancode to Irrlicht key code
        // Route to input system
        handleRDPKeyboard(flags, scancode);
    });
    
    rdpServer_->setMouseCallback([this](uint16_t flags, uint16_t x, uint16_t y) {
        handleRDPMouse(flags, x, y);
    });
    
    return true;
}

void IrrlichtRenderer::setRDPCertificate(const std::string& cert, const std::string& key) {
    if (rdpServer_) {
        rdpServer_->setCertificate(cert, key);
    }
}

bool IrrlichtRenderer::startRDPServer() {
    return rdpServer_ && rdpServer_->start();
}

void IrrlichtRenderer::stopRDPServer() {
    if (rdpServer_) {
        rdpServer_->stop();
    }
}

bool IrrlichtRenderer::isRDPRunning() const {
    return rdpServer_ && rdpServer_->isRunning();
}

// Add to render loop (after drawing):
void IrrlichtRenderer::endScene() {
    driver_->endScene();
    
#ifdef WITH_RDP
    if (rdpServer_ && rdpServer_->isRunning()) {
        // Get framebuffer and send to RDP
        video::IImage* screenshot = driver_->createScreenShot();
        if (screenshot) {
            void* data = screenshot->getData();
            rdpServer_->updateFrame(
                static_cast<uint8_t*>(data),
                screenshot->getDimension().Width,
                screenshot->getDimension().Height,
                screenshot->getDimension().Width * 4,
                PIXEL_FORMAT_BGRA32
            );
            screenshot->drop();
        }
    }
#endif
}

#endif // WITH_RDP
```

---

## Input Translation

### RDP to Irrlicht Key Mapping

```cpp
// rdp_input_handler.cpp

#include <irrlicht.h>
#include <freerdp/input.h>

using namespace irr;

EKEY_CODE rdpScancodeToIrrlicht(uint8_t scancode) {
    // RDP uses Windows virtual key codes
    static const EKEY_CODE scancodeMap[] = {
        // Common keys
        [RDP_SCANCODE_ESCAPE] = KEY_ESCAPE,
        [RDP_SCANCODE_KEY_1] = KEY_KEY_1,
        [RDP_SCANCODE_KEY_2] = KEY_KEY_2,
        // ... etc
        [RDP_SCANCODE_KEY_W] = KEY_KEY_W,
        [RDP_SCANCODE_KEY_A] = KEY_KEY_A,
        [RDP_SCANCODE_KEY_S] = KEY_KEY_S,
        [RDP_SCANCODE_KEY_D] = KEY_KEY_D,
        [RDP_SCANCODE_SPACE] = KEY_SPACE,
        [RDP_SCANCODE_RETURN] = KEY_RETURN,
        [RDP_SCANCODE_TAB] = KEY_TAB,
        [RDP_SCANCODE_F1] = KEY_F1,
        // ... complete mapping
    };
    
    if (scancode < sizeof(scancodeMap)/sizeof(scancodeMap[0])) {
        return scancodeMap[scancode];
    }
    return KEY_KEY_CODES_COUNT;
}

void IrrlichtRenderer::handleRDPKeyboard(uint16_t flags, uint8_t scancode) {
    SEvent event;
    event.EventType = EET_KEY_INPUT_EVENT;
    event.KeyInput.Key = rdpScancodeToIrrlicht(scancode);
    event.KeyInput.PressedDown = !(flags & KBD_FLAGS_RELEASE);
    event.KeyInput.Shift = false;  // Could track modifier state
    event.KeyInput.Control = false;
    event.KeyInput.Char = 0;
    
    if (eventReceiver_) {
        eventReceiver_->OnEvent(event);
    }
}

void IrrlichtRenderer::handleRDPMouse(uint16_t flags, uint16_t x, uint16_t y) {
    SEvent event;
    event.EventType = EET_MOUSE_INPUT_EVENT;
    event.MouseInput.X = x;
    event.MouseInput.Y = y;
    
    if (flags & PTR_FLAGS_MOVE) {
        event.MouseInput.Event = EMIE_MOUSE_MOVED;
    } else if (flags & PTR_FLAGS_BUTTON1) {
        event.MouseInput.Event = (flags & PTR_FLAGS_DOWN) ? 
            EMIE_LMOUSE_PRESSED_DOWN : EMIE_LMOUSE_LEFT_UP;
    } else if (flags & PTR_FLAGS_BUTTON2) {
        event.MouseInput.Event = (flags & PTR_FLAGS_DOWN) ? 
            EMIE_RMOUSE_PRESSED_DOWN : EMIE_RMOUSE_LEFT_UP;
    } else if (flags & PTR_FLAGS_WHEEL) {
        event.MouseInput.Event = EMIE_MOUSE_WHEEL;
        event.MouseInput.Wheel = (flags & PTR_FLAGS_WHEEL_NEGATIVE) ? -1.0f : 1.0f;
    }
    
    if (eventReceiver_) {
        eventReceiver_->OnEvent(event);
    }
}
```

---

## Configuration

### Command Line Options

```bash
--rdp, --enable-rdp   Enable native RDP server
--rdp-port <port>     RDP port (default: 3389)
```

**Note:** Certificate options are not required - FreeRDP 3 auto-generates self-signed certificates.

### JSON Configuration

```json
{
    "eq_client_path": "/path/to/EverQuest",
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

Command line options override JSON configuration.

---

## SSL Certificates

FreeRDP 3 automatically generates self-signed certificates, so no manual certificate setup is required for basic usage.

For custom certificates (optional):

```bash
# Generate private key and certificate
openssl req -x509 -newkey rsa:4096 \
    -keyout server.key \
    -out server.crt \
    -days 365 \
    -nodes \
    -subj "/CN=WillEQ RDP Server"
```

Custom certificate support can be added via `RDPServer::setCertificate()` if needed.

---

## Usage

### Starting with RDP

```bash
# With RDP enabled (requires X11 display for Irrlicht rendering)
./build/bin/willeq -c willeq.json --rdp

# With RDP on custom port
./build/bin/willeq -c willeq.json --rdp --rdp-port 13389

# With Xvfb for headless operation + RDP streaming
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 ./build/bin/willeq -c willeq.json --rdp
```

**Note:** RDP requires graphics to be enabled (cannot use `--no-graphics` with `--rdp`). The RDP server captures frames from the Irrlicht renderer.

### Connecting

```bash
# Using Windows Remote Desktop
mstsc.exe /v:your-server:3389

# Using FreeRDP client (Linux)
xfreerdp /v:your-server:3389 /cert:ignore

# Using FreeRDP with RemoteFX codec
xfreerdp /v:your-server:3389 /cert:ignore /rfx

# Using Remmina (Linux GUI client)
# Add new RDP connection with server address
```

### Running RDP and VNC Simultaneously

Both RDP and VNC can serve the same display:

```bash
# Start with Xvfb
Xvfb :99 -screen 0 1024x768x24 &
export DISPLAY=:99

# Start x11vnc for VNC access
x11vnc -display :99 -forever -shared -rfbport 5999 &

# Start WillEQ with RDP
./build/bin/willeq -c willeq.json --rdp --rdp-port 3389

# Now connect via either:
# - VNC: vnc://hostname:5999
# - RDP: mstsc.exe /v:hostname:3389
```

---

## Performance Considerations

### Frame Rate Limiting

```cpp
// In peer thread, throttle frame updates
auto lastFrame = std::chrono::steady_clock::now();
const auto frameInterval = std::chrono::milliseconds(33); // ~30fps

// In loop:
auto now = std::chrono::steady_clock::now();
if (now - lastFrame >= frameInterval) {
    sendFrame(...);
    lastFrame = now;
}
```

### Dirty Rectangles (Optimization)

Instead of sending full frames, track changed regions:

```cpp
struct DirtyRegion {
    uint16_t x, y, width, height;
};

void RDPServer::updateRegion(const DirtyRegion& region, const uint8_t* data) {
    // Only encode and send the changed rectangle
    // Significantly reduces bandwidth for mostly-static scenes
}
```

### Codec Selection

- **RemoteFX (RFX)**: Better for detailed images, higher CPU usage
- **NSCodec**: Lower CPU, good for video content
- **Bitmap**: Fallback, uncompressed

---

## Testing

### Unit Tests

55 unit tests are included covering RDP functionality:

```bash
# Run all RDP tests
cd build && ctest -R rdp --output-on-failure

# Run specific test suites
./bin/test_rdp_input_handler   # 35 tests - scancode/mouse translation
./bin/test_rdp_server          # 20 tests - server lifecycle
```

**test_rdp_input_handler.cpp** covers:
- Scancode translation (letters, numbers, function keys, modifiers)
- Character translation with Shift/CapsLock
- Mouse event translation (move, click, wheel)
- Wheel delta extraction

**test_rdp_server.cpp** covers:
- Server initialization with default/custom ports
- Start/stop lifecycle
- Callback registration and invocation
- Frame update handling (including null/zero dimension safety)
- Certificate path configuration

### Integration Testing

Manual testing with RDP clients:

```bash
# Start WillEQ with RDP
DISPLAY=:99 ./build/bin/willeq -c willeq.json --rdp --rdp-port 3390

# Connect with FreeRDP and verify display
xfreerdp /v:localhost:3390 /cert:ignore

# Test keyboard input (WASD movement, hotkeys)
# Test mouse input (targeting, camera control)
# Test disconnect/reconnect handling
```

---

## Troubleshooting

### Common Issues

1. **Connection refused**
   - Check firewall allows the RDP port
   - Verify port is not in use: `netstat -tlnp | grep 3389`
   - Ensure WillEQ started with `--rdp` flag

2. **Certificate errors**
   - Use `/cert:ignore` with xfreerdp
   - Windows mstsc.exe may prompt to accept self-signed cert

3. **Black screen**
   - Verify DISPLAY environment variable is set
   - Check Irrlicht renderer is initialized
   - Look for errors in console output

4. **High latency**
   - Try RemoteFX codec: `xfreerdp /v:host /rfx`
   - Reduce game resolution
   - Check network bandwidth

5. **Input not working**
   - Verify keyboard/mouse callbacks are registered
   - Check console for input event logging
   - Ensure client window has focus

6. **Build errors with FreeRDP**
   - Verify correct packages installed (freerdp3-dev for Ubuntu 24.04+)
   - Check cmake output for FreeRDP detection
   - Rebuild after installing dependencies

### Debug Logging

Enable verbose logging to diagnose issues:

```bash
# Set debug level
./build/bin/willeq -c willeq.json --rdp --debug 5
```

---

## Future Enhancements

1. **Audio streaming** - RDPSND channel for game audio
2. **Clipboard sharing** - Clipboard virtual channel
3. **Multi-monitor** - Support for multiple virtual displays  
4. **GPU acceleration** - Use GPU for RFX encoding if available
5. **Session recording** - Record RDP sessions for replay

