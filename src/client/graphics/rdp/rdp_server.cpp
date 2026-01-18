#ifdef WITH_RDP

#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_peer_context.h"

#include <freerdp/freerdp.h>
#include <freerdp/constants.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/color.h>
#include <freerdp/update.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <freerdp/peer.h>
#include <freerdp/listener.h>

#include <winpr/ssl.h>
#include <winpr/winsock.h>
#include <winpr/stream.h>
#include <winpr/synch.h>

#include <iostream>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace EQT {
namespace Graphics {

// Forward declarations of static callbacks
static BOOL onPeerAccepted(freerdp_listener* instance, freerdp_peer* client);
static BOOL onPeerPostConnect(freerdp_peer* client);
static BOOL onPeerActivate(freerdp_peer* client);
static BOOL onSynchronizeEvent(rdpInput* input, UINT32 flags);
static BOOL onKeyboardEvent(rdpInput* input, UINT16 flags, UINT8 code);
static BOOL onMouseEvent(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
static BOOL onExtendedMouseEvent(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);

RDPServer::RDPServer()
    : listener_(nullptr)
    , running_(false)
    , initialized_(false)
    , port_(3389)
    , width_(1024)
    , height_(768)
    , frameWidth_(0)
    , frameHeight_(0)
    , framePitch_(0)
    , frameReady_(false)
    , frameSequence_(0)
{
}

RDPServer::~RDPServer() {
    stop();
}

bool RDPServer::initialize(uint16_t port) {
    if (initialized_.load()) {
        return true;
    }

    port_ = port;

    // Initialize WinPR SSL
    winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);

    // Initialize Winsock (cross-platform via WinPR)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[RDP] WSAStartup failed" << std::endl;
        return false;
    }

    // Create the listener
    listener_ = freerdp_listener_new();
    if (!listener_) {
        std::cerr << "[RDP] Failed to create FreeRDP listener" << std::endl;
        WSACleanup();
        return false;
    }

    // Set peer accepted callback and store server pointer
    listener_->PeerAccepted = onPeerAccepted;
    listener_->info = this;

    initialized_.store(true);
    std::cout << "[RDP] Server initialized on port " << port_ << std::endl;
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
    if (running_.load()) {
        return true;
    }

    if (!initialized_.load()) {
        std::cerr << "[RDP] Server not initialized" << std::endl;
        return false;
    }

    // Open the listener on all interfaces
    if (!listener_->Open(listener_, nullptr, port_)) {
        std::cerr << "[RDP] Failed to open listener on port " << port_ << std::endl;
        return false;
    }

    running_.store(true);
    listenerThread_ = std::thread(&RDPServer::listenerThread, this);

    std::cout << "[RDP] Server started, listening on port " << port_ << std::endl;
    return true;
}

void RDPServer::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Close the listener
    if (listener_) {
        listener_->Close(listener_);
    }

    // Wait for listener thread
    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }

    // Wait for all peer threads
    for (auto& thread : peerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    peerThreads_.clear();

    // Clear active peers
    {
        std::lock_guard<std::mutex> lock(peersMutex_);
        activePeers_.clear();
    }

    // Free the listener
    if (listener_) {
        freerdp_listener_free(listener_);
        listener_ = nullptr;
    }

    WSACleanup();
    initialized_.store(false);

    std::cout << "[RDP] Server stopped" << std::endl;
}

size_t RDPServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(peersMutex_));
    return activePeers_.size();
}

void RDPServer::updateFrame(const uint8_t* frameData, uint32_t width, uint32_t height, uint32_t pitch) {
    // Validate input - skip update if invalid
    if (!frameData || width == 0 || height == 0 || pitch == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(frameMutex_);

    // Resize buffer if needed
    size_t requiredSize = static_cast<size_t>(pitch) * height;
    if (frameBuffer_.size() < requiredSize) {
        frameBuffer_.resize(requiredSize);
    }

    // Copy frame data
    std::memcpy(frameBuffer_.data(), frameData, requiredSize);
    frameWidth_ = width;
    frameHeight_ = height;
    framePitch_ = pitch;
    frameReady_.store(true);
    frameSequence_.fetch_add(1);
}

void RDPServer::onPeerConnected(RDPPeerContext* context) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    activePeers_.push_back(context);
    std::cout << "[RDP] Client connected (total: " << activePeers_.size() << ")" << std::endl;
}

void RDPServer::onPeerDisconnected(RDPPeerContext* context) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    activePeers_.erase(
        std::remove(activePeers_.begin(), activePeers_.end(), context),
        activePeers_.end()
    );
    std::cout << "[RDP] Client disconnected (remaining: " << activePeers_.size() << ")" << std::endl;
}

void RDPServer::onKeyboardEventInternal(uint16_t flags, uint8_t scancode) {
    if (keyboardCallback_) {
        keyboardCallback_(flags, scancode);
    }
}

void RDPServer::onMouseEventInternal(uint16_t flags, uint16_t x, uint16_t y) {
    if (mouseCallback_) {
        mouseCallback_(flags, x, y);
    }
}

void RDPServer::addPeerThread(std::thread&& thread) {
    peerThreads_.push_back(std::move(thread));
}

void RDPServer::listenerThread() {
    HANDLE handles[32];
    DWORD count;

    while (running_.load()) {
        count = listener_->GetEventHandles(listener_, handles, 32);
        if (count == 0) {
            std::cerr << "[RDP] Failed to get listener event handles" << std::endl;
            break;
        }

        DWORD status = WaitForMultipleObjects(count, handles, FALSE, 100);

        if (!running_.load()) {
            break;
        }

        if (status == WAIT_TIMEOUT) {
            continue;
        }

        if (status == WAIT_FAILED) {
            std::cerr << "[RDP] WaitForMultipleObjects failed" << std::endl;
            break;
        }

        if (!listener_->CheckFileDescriptor(listener_)) {
            std::cerr << "[RDP] Listener CheckFileDescriptor failed" << std::endl;
            break;
        }
    }
}

void RDPServer::peerThreadImpl(freerdp_peer* client) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    rdpSettings* settings = client->context->settings;

    // Security mode - allow RDP and TLS, disable NLA for simplicity
    // Note: FreeRDP 3 generates a self-signed certificate automatically
    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE);

    // Desktop settings
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, width_);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, height_);
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);

    // Codec settings
    freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE);

    // Set callbacks
    client->PostConnect = onPeerPostConnect;
    client->Activate = onPeerActivate;

    // Set input callbacks (use static functions declared above)
    rdpInput* input = client->context->input;
    input->SynchronizeEvent = onSynchronizeEvent;
    input->KeyboardEvent = ::EQT::Graphics::onKeyboardEvent;
    input->MouseEvent = ::EQT::Graphics::onMouseEvent;
    input->ExtendedMouseEvent = ::EQT::Graphics::onExtendedMouseEvent;

    // Initialize the peer
    if (!client->Initialize(client)) {
        std::cerr << "[RDP] Failed to initialize peer" << std::endl;
        goto cleanup;
    }

    std::cout << "[RDP] Peer initialized: " << (client->hostname ? client->hostname : "unknown") << std::endl;

    // Add to active peers
    onPeerConnected(context);

    // Main peer loop
    {
        HANDLE handles[32];
        DWORD count;
        uint32_t lastFrameSeq = 0;
        auto lastFrameTime = std::chrono::steady_clock::now();
        const auto frameInterval = std::chrono::milliseconds(33);  // ~30fps

        while (running_.load()) {
            count = client->GetEventHandles(client, handles, 32);
            if (count == 0) {
                std::cerr << "[RDP] Failed to get peer event handles" << std::endl;
                break;
            }

            DWORD status = WaitForMultipleObjects(count, handles, FALSE, 16);

            if (!running_.load()) {
                break;
            }

            if (status == WAIT_FAILED) {
                std::cerr << "[RDP] Peer WaitForMultipleObjects failed" << std::endl;
                break;
            }

            if (!client->CheckFileDescriptor(client)) {
                // Connection closed
                break;
            }

            // Send frame if activated and frame is ready
            if (context->activated) {
                auto now = std::chrono::steady_clock::now();
                uint32_t currentSeq = frameSequence_.load();

                if (currentSeq != lastFrameSeq && (now - lastFrameTime) >= frameInterval) {
                    sendFrameToPeer(context);
                    lastFrameSeq = currentSeq;
                    lastFrameTime = now;
                }
            }
        }
    }

    // Remove from active peers
    onPeerDisconnected(context);

cleanup:
    std::cout << "[RDP] Peer disconnecting: " << (client->hostname ? client->hostname : "unknown") << std::endl;
    client->Disconnect(client);
    freerdp_peer_context_free(client);
    freerdp_peer_free(client);
}

void RDPServer::sendFrameToPeer(RDPPeerContext* context) {
    if (!context || !context->activated) {
        return;
    }

    std::lock_guard<std::mutex> lock(frameMutex_);

    if (!frameReady_.load() || frameBuffer_.empty()) {
        return;
    }

    freerdp_peer* client = context->_p.peer;
    rdpSettings* settings = context->_p.settings;
    rdpUpdate* update = context->_p.update;

    if (!update || !update->SurfaceBits || !update->SurfaceFrameMarker) {
        return;
    }

    // Check if client supports RemoteFX
    bool useRemoteFx = freerdp_settings_get_bool(settings, FreeRDP_RemoteFxCodec);

    // Reset the encode stream
    Stream_SetPosition(context->encodeStream, 0);

    // Begin frame marker
    SURFACE_FRAME_MARKER frameMarker = {};
    frameMarker.frameAction = SURFACECMD_FRAMEACTION_BEGIN;
    frameMarker.frameId = context->frameId;

    if (!update->SurfaceFrameMarker(update->context, &frameMarker)) {
        return;
    }

    // Prepare surface bits command
    SURFACE_BITS_COMMAND cmd = {};
    cmd.destLeft = 0;
    cmd.destTop = 0;
    cmd.destRight = frameWidth_;
    cmd.destBottom = frameHeight_;
    cmd.bmp.bpp = 32;
    cmd.bmp.width = frameWidth_;
    cmd.bmp.height = frameHeight_;

    // Encode the frame
    RFX_RECT rect = { 0, 0, static_cast<UINT16>(frameWidth_), static_cast<UINT16>(frameHeight_) };

    if (useRemoteFx && context->rfxContext) {
        // Use RemoteFX codec
        rfx_context_set_pixel_format(context->rfxContext, PIXEL_FORMAT_BGRA32);

        if (!rfx_compose_message(context->rfxContext, context->encodeStream,
                                 &rect, 1,
                                 frameBuffer_.data(),
                                 frameWidth_, frameHeight_, framePitch_)) {
            return;
        }

        cmd.bmp.codecID = freerdp_settings_get_uint32(settings, FreeRDP_RemoteFxCodecId);
        cmd.cmdType = CMDTYPE_STREAM_SURFACE_BITS;
    } else if (context->nscContext) {
        // Use NSCodec
        nsc_context_set_parameters(context->nscContext, NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRA32);

        if (!nsc_compose_message(context->nscContext, context->encodeStream,
                                 frameBuffer_.data(),
                                 frameWidth_, frameHeight_, framePitch_)) {
            return;
        }

        cmd.bmp.codecID = freerdp_settings_get_uint32(settings, FreeRDP_NSCodecId);
        cmd.cmdType = CMDTYPE_SET_SURFACE_BITS;
    } else {
        // No codec available
        return;
    }

    cmd.bmp.bitmapDataLength = Stream_GetPosition(context->encodeStream);
    cmd.bmp.bitmapData = Stream_Buffer(context->encodeStream);

    // Send the surface bits
    if (!update->SurfaceBits(update->context, &cmd)) {
        return;
    }

    // End frame marker
    frameMarker.frameAction = SURFACECMD_FRAMEACTION_END;
    frameMarker.frameId = context->frameId++;
    update->SurfaceFrameMarker(update->context, &frameMarker);
}

// Static FreeRDP callbacks

static BOOL onPeerAccepted(freerdp_listener* instance, freerdp_peer* client) {
    RDPServer* server = static_cast<RDPServer*>(instance->info);

    if (!server) {
        return FALSE;
    }

    // Set up peer context
    client->ContextSize = sizeof(RDPPeerContext);
    client->ContextNew = rdpPeerContextNew;
    client->ContextFree = rdpPeerContextFree;
    client->ContextExtra = server;

    if (!freerdp_peer_context_new(client)) {
        std::cerr << "[RDP] Failed to create peer context" << std::endl;
        return FALSE;
    }

    // Start a thread to handle this peer
    server->addPeerThread(std::thread(&RDPServer::peerThreadImpl, server, client));

    return TRUE;
}

static BOOL onPeerPostConnect(freerdp_peer* client) {
    rdpSettings* settings = client->context->settings;

    std::cout << "[RDP] Client post-connect: "
              << freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth) << "x"
              << freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight) << "x"
              << freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth) << std::endl;

    return TRUE;
}

static BOOL onPeerActivate(freerdp_peer* client) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    context->activated = true;
    std::cout << "[RDP] Client activated" << std::endl;
    return TRUE;
}

static BOOL onSynchronizeEvent(rdpInput* input, UINT32 flags) {
    // Synchronize keyboard state (num lock, caps lock, etc.)
    return TRUE;
}

static BOOL onKeyboardEvent(rdpInput* input, UINT16 flags, UINT8 code) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(input->context);
    if (context && context->server) {
        context->server->onKeyboardEventInternal(flags, code);
    }
    return TRUE;
}

static BOOL onMouseEvent(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(input->context);
    if (context && context->server) {
        context->server->onMouseEventInternal(flags, x, y);
    }
    return TRUE;
}

static BOOL onExtendedMouseEvent(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y) {
    // Extended mouse events (extra buttons) - forward as regular mouse events
    return onMouseEvent(input, flags, x, y);
}

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP
