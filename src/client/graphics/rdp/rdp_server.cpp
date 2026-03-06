#ifdef WITH_RDP

#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_peer_context.h"

#include <freerdp/freerdp.h>
#include <freerdp/constants.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/audio.h>
#include <freerdp/update.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <freerdp/peer.h>
#include <freerdp/listener.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/channels/wtsvc.h>
#include <freerdp/channels/channels.h>
#include <freerdp/channels/drdynvc.h>

#include <winpr/ssl.h>
#include <winpr/wtsapi.h>
#include <winpr/winsock.h>
#include <winpr/stream.h>
#include <winpr/synch.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bn.h>

#include "common/logging.h"
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
static void onRdpsndActivated(RdpsndServerContext* context);

// Server-supported audio formats (16-bit stereo PCM at various sample rates)
AUDIO_FORMAT RDPServer::serverAudioFormats_[] = {
    { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0, nullptr },  // 44.1kHz stereo
    { WAVE_FORMAT_PCM, 2, 22050, 88200, 4, 16, 0, nullptr },   // 22.05kHz stereo
    { WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0, nullptr },   // 44.1kHz mono
    { WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0, nullptr },   // 22.05kHz mono
};
const size_t RDPServer::numServerAudioFormats_ = sizeof(serverAudioFormats_) / sizeof(serverAudioFormats_[0]);

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
        LOG_ERROR(MOD_GRAPHICS, "WSAStartup failed");
        return false;
    }

    // Register FreeRDP's WTS API functions with WinPR
    // This is required for WTSOpenServerA to work on non-Windows platforms
    if (!WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi())) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to register WTS API function table");
        WSACleanup();
        return false;
    }

    // Create the listener
    listener_ = freerdp_listener_new();
    if (!listener_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create FreeRDP listener");
        WSACleanup();
        return false;
    }

    // Set peer accepted callback and store server pointer
    listener_->PeerAccepted = onPeerAccepted;
    listener_->info = this;

    initialized_.store(true);
    LOG_INFO(MOD_GRAPHICS, "RDP server initialized on port {}", port_);
    return true;
}

void RDPServer::setCertificate(const std::string& certPath, const std::string& keyPath) {
    certPath_ = certPath;
    keyPath_ = keyPath;
}

bool RDPServer::generateSelfSignedCertificate() {
    std::lock_guard<std::mutex> lock(certMutex_);

    if (certGenerated_) {
        return true;
    }

    // Generate RSA key pair using OpenSSL 3.0+ API
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create EVP_PKEY_CTX");
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to init keygen");
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to set RSA bits");
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to generate RSA key");
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Create self-signed X509 certificate
    X509* x509 = X509_new();
    if (!x509) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create X509 certificate");
        EVP_PKEY_free(pkey);
        return false;
    }

    // Set certificate properties
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_getm_notBefore(x509), 0);
    X509_gmtime_adj(X509_getm_notAfter(x509), 365 * 24 * 60 * 60);  // Valid for 1 year
    X509_set_pubkey(x509, pkey);

    // Set subject/issuer
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"WillEQ RDP Server", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);  // Self-signed

    // Sign the certificate
    if (!X509_sign(x509, pkey, EVP_sha256())) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to sign certificate");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    // Convert to PEM strings
    BIO* certBio = BIO_new(BIO_s_mem());
    BIO* keyBio = BIO_new(BIO_s_mem());

    if (!certBio || !keyBio) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create BIO");
        if (certBio) BIO_free(certBio);
        if (keyBio) BIO_free(keyBio);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return false;
    }

    PEM_write_bio_X509(certBio, x509);
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    // Extract PEM data and store as strings
    char* certData = nullptr;
    char* keyData = nullptr;
    long certLen = BIO_get_mem_data(certBio, &certData);
    long keyLen = BIO_get_mem_data(keyBio, &keyData);

    certPem_.assign(certData, certLen);
    keyPem_.assign(keyData, keyLen);

    // Cleanup OpenSSL objects
    BIO_free(certBio);
    BIO_free(keyBio);
    X509_free(x509);
    EVP_PKEY_free(pkey);

    // Verify we can create FreeRDP objects from the PEM
    rdpCertificate* testCert = freerdp_certificate_new_from_pem(certPem_.c_str());
    rdpPrivateKey* testKey = freerdp_key_new_from_pem(keyPem_.c_str());

    if (!testCert || !testKey) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create FreeRDP certificate/key from PEM");
        if (testCert) freerdp_certificate_free(testCert);
        if (testKey) freerdp_key_free(testKey);
        certPem_.clear();
        keyPem_.clear();
        return false;
    }

    // Free the test objects
    freerdp_certificate_free(testCert);
    freerdp_key_free(testKey);

    certGenerated_ = true;
    LOG_INFO(MOD_GRAPHICS, "Generated self-signed certificate");
    return true;
}

rdpCertificate* RDPServer::createPeerCertificate() {
    // Try to load PEM from file first if not already loaded
    if (certPem_.empty() && !certPath_.empty()) {
        // Read cert file into certPem_
        FILE* f = fopen(certPath_.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            certPem_.resize(size);
            fread(&certPem_[0], 1, size, f);
            fclose(f);
            LOG_INFO(MOD_GRAPHICS, "Loaded certificate from {}", certPath_);
        }
    }

    // Generate if not loaded
    if (certPem_.empty()) {
        if (!generateSelfSignedCertificate()) {
            return nullptr;
        }
    }

    // Create a fresh certificate object for this peer
    return freerdp_certificate_new_from_pem(certPem_.c_str());
}

rdpPrivateKey* RDPServer::createPeerKey() {
    // Try to load PEM from file first if not already loaded
    if (keyPem_.empty() && !keyPath_.empty()) {
        // Read key file into keyPem_
        FILE* f = fopen(keyPath_.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            keyPem_.resize(size);
            fread(&keyPem_[0], 1, size, f);
            fclose(f);
            LOG_INFO(MOD_GRAPHICS, "Loaded private key from {}", keyPath_);
        }
    }

    // Generate if not loaded
    if (keyPem_.empty()) {
        if (!generateSelfSignedCertificate()) {
            return nullptr;
        }
    }

    // Create a fresh key object for this peer
    return freerdp_key_new_from_pem(keyPem_.c_str());
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
        LOG_ERROR(MOD_GRAPHICS, "RDP server not initialized");
        return false;
    }

    // Open the listener on all interfaces
    if (!listener_->Open(listener_, nullptr, port_)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to open listener on port {}", port_);
        return false;
    }

    running_.store(true);
    listenerThread_ = std::thread(&RDPServer::listenerThread, this);

    LOG_INFO(MOD_GRAPHICS, "RDP server started, listening on port {}", port_);
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
    for (auto& entry : peerThreads_) {
        if (entry->thread.joinable()) {
            entry->thread.join();
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

    LOG_INFO(MOD_GRAPHICS, "RDP server stopped");
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
    LOG_INFO(MOD_GRAPHICS, "RDP client connected (total: {})", activePeers_.size());
    // Audio initialization is done in onPeerActivate when channels are ready
}

void RDPServer::onPeerDisconnected(RDPPeerContext* context) {
    std::lock_guard<std::mutex> lock(peersMutex_);
    activePeers_.erase(
        std::remove(activePeers_.begin(), activePeers_.end(), context),
        activePeers_.end()
    );
    LOG_INFO(MOD_GRAPHICS, "RDP client disconnected (remaining: {})", activePeers_.size());
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

void RDPServer::cleanupFinishedPeerThreads() {
    peerThreads_.erase(
        std::remove_if(peerThreads_.begin(), peerThreads_.end(),
            [](const std::unique_ptr<PeerThreadEntry>& entry) {
                if (entry->finished.load()) {
                    if (entry->thread.joinable()) {
                        entry->thread.join();
                    }
                    return true;
                }
                return false;
            }),
        peerThreads_.end()
    );
}

void RDPServer::addPeerThread(freerdp_peer* client) {
    cleanupFinishedPeerThreads();
    auto entry = std::make_unique<PeerThreadEntry>();
    auto* finishedPtr = &entry->finished;
    entry->thread = std::thread(&RDPServer::peerThreadImpl, this, client, finishedPtr);
    peerThreads_.push_back(std::move(entry));
}

void RDPServer::listenerThread() {
    HANDLE handles[32];
    DWORD count;

    while (running_.load()) {
        count = listener_->GetEventHandles(listener_, handles, 32);
        if (count == 0) {
            LOG_ERROR(MOD_GRAPHICS, "Failed to get listener event handles");
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
            LOG_ERROR(MOD_GRAPHICS, "WaitForMultipleObjects failed");
            break;
        }

        if (!listener_->CheckFileDescriptor(listener_)) {
            LOG_ERROR(MOD_GRAPHICS, "Listener CheckFileDescriptor failed");
            break;
        }
        FlushThreadLog();
    }
    FlushThreadLog();
}

void RDPServer::peerThreadImpl(freerdp_peer* client, std::atomic<bool>* finished) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    rdpSettings* settings = client->context->settings;
    rdpInput* input = nullptr;  // Declared early to avoid goto issues

    // Create fresh certificate and key for this peer (FreeRDP takes ownership)
    rdpCertificate* cert = createPeerCertificate();
    rdpPrivateKey* key = createPeerKey();

    if (!cert || !key) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to get/generate server certificate and key");
        goto cleanup;
    }

    // Set the certificate and key on peer settings
    // Note: We pass the pointers but FreeRDP doesn't take ownership,
    // so we must keep them alive for the duration of the server
    if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, cert, 1)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to set server certificate");
        goto cleanup;
    }

    if (!freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to set server RSA key");
        goto cleanup;
    }

    // Security mode - allow RDP and TLS, disable NLA for simplicity
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

    // Enable dynamic channels and audio
    freerdp_settings_set_bool(settings, FreeRDP_SupportDynamicChannels, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, TRUE);

    // Set callbacks
    client->PostConnect = onPeerPostConnect;
    client->Activate = onPeerActivate;

    // Set input callbacks (use static functions declared above)
    input = client->context->input;
    input->SynchronizeEvent = onSynchronizeEvent;
    input->KeyboardEvent = ::EQT::Graphics::onKeyboardEvent;
    input->MouseEvent = ::EQT::Graphics::onMouseEvent;
    input->ExtendedMouseEvent = ::EQT::Graphics::onExtendedMouseEvent;

    // Initialize the peer
    if (!client->Initialize(client)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to initialize peer");
        goto cleanup;
    }

    LOG_INFO(MOD_GRAPHICS, "RDP peer initialized: {}", client->hostname ? client->hostname : "unknown");

    // Create virtual channel manager for this peer after Initialize
    // WTSOpenServerA requires the peer to be initialized first
    context->vcm = WTSOpenServerA((LPSTR)client->context);
    if (!context->vcm || context->vcm == INVALID_HANDLE_VALUE) {
        LOG_INFO(MOD_GRAPHICS, "Virtual channel manager not available, audio disabled");
        context->vcm = nullptr;
    }

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
                LOG_ERROR(MOD_GRAPHICS, "Failed to get peer event handles");
                break;
            }

            // Add VCM event handle to process virtual channel data
            if (context->vcm && count < 32) {
                HANDLE vcmHandle = WTSVirtualChannelManagerGetEventHandle(context->vcm);
                if (vcmHandle) {
                    handles[count++] = vcmHandle;
                }
            }

            // Add RDPSND event handle if audio is initialized
            HANDLE rdpsndHandle = nullptr;
            if (context->rdpsndContext) {
                rdpsndHandle = rdpsnd_server_get_event_handle(context->rdpsndContext);
                if (rdpsndHandle && count < 32) {
                    handles[count++] = rdpsndHandle;
                } else if (!rdpsndHandle) {
                    static bool warned = false;
                    if (!warned) {
                        LOG_ERROR(MOD_GRAPHICS, "RDP audio: RDPSND event handle is NULL");
                        warned = true;
                    }
                }
            }

            DWORD status = WaitForMultipleObjects(count, handles, FALSE, 16);

            if (!running_.load()) {
                break;
            }

            if (status == WAIT_FAILED) {
                LOG_ERROR(MOD_GRAPHICS, "Peer WaitForMultipleObjects failed");
                break;
            }

            if (!client->CheckFileDescriptor(client)) {
                // Connection closed
                break;
            }

            // Drive the virtual channel manager to process incoming channel data
            if (context->vcm) {
                if (!WTSVirtualChannelManagerCheckFileDescriptor(context->vcm)) {
                    // Non-fatal, just log once
                    static bool warned = false;
                    if (!warned) {
                        LOG_ERROR(MOD_GRAPHICS, "VCM CheckFileDescriptor returned false");
                        warned = true;
                    }
                }
            }

            // Process RDPSND messages if audio is initialized
            if (context->rdpsndContext) {
                UINT rdpsndStatus = rdpsnd_server_handle_messages(context->rdpsndContext);
                if (rdpsndStatus == CHANNEL_RC_OK) {
                    static int msgCount = 0;
                    if (++msgCount <= 5) {
                        LOG_DEBUG(MOD_GRAPHICS, "RDP audio: processed RDPSND message (count={})", msgCount);
                    }
                } else if (rdpsndStatus != ERROR_NO_DATA) {
                    LOG_ERROR(MOD_GRAPHICS, "RDP audio: error handling RDPSND messages: {}", rdpsndStatus);
                }
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
    LOG_INFO(MOD_GRAPHICS, "RDP peer disconnecting: {}", client->hostname ? client->hostname : "unknown");
    FlushThreadLog();
    client->Disconnect(client);
    freerdp_peer_context_free(client);
    freerdp_peer_free(client);
    finished->store(true);
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
        LOG_ERROR(MOD_GRAPHICS, "Failed to create peer context");
        return FALSE;
    }

    // Start a thread to handle this peer
    server->addPeerThread(client);

    return TRUE;
}

static BOOL onPeerPostConnect(freerdp_peer* client) {
    rdpSettings* settings = client->context->settings;

    LOG_INFO(MOD_GRAPHICS, "RDP client post-connect: {}x{}x{}",
             freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
             freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight),
             freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth));

    return TRUE;
}

static BOOL onPeerActivate(freerdp_peer* client) {
    RDPPeerContext* context = reinterpret_cast<RDPPeerContext*>(client->context);
    context->activated = true;
    LOG_INFO(MOD_GRAPHICS, "RDP client activated");

    // Initialize audio now that channels are ready
    if (context->server) {
        context->server->initAudioForPeer(context);
    }

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

// RDPSND activated callback - called when audio channel is ready
static void onRdpsndActivated(RdpsndServerContext* context) {
    LOG_DEBUG(MOD_GRAPHICS, "RDP audio: onRdpsndActivated callback called");
    if (!context || !context->data) {
        LOG_ERROR(MOD_GRAPHICS, "RDP audio: onRdpsndActivated: invalid context");
        return;
    }

    RDPPeerContext* peerContext = static_cast<RDPPeerContext*>(context->data);
    if (peerContext && peerContext->server) {
        peerContext->server->onAudioActivated(peerContext);
    } else {
        LOG_ERROR(MOD_GRAPHICS, "RDP audio: onRdpsndActivated: invalid peer context");
    }
}

void RDPServer::initAudioForPeer(RDPPeerContext* context) {
    if (!context || !audioEnabled_) {
        return;
    }

    // Check if VCM was successfully created during context initialization
    if (!context->vcm) {
        LOG_INFO(MOD_GRAPHICS, "RDP audio: virtual channel manager not available, audio disabled");
        return;
    }

    // Check if static audio channel is available
    bool hasStaticChannel = WTSVirtualChannelManagerIsChannelJoined(context->vcm, RDPSND_CHANNEL_NAME);

    LOG_DEBUG(MOD_GRAPHICS, "RDP audio: channel status: static(rdpsnd)={}", hasStaticChannel ? "yes" : "no");

    if (!hasStaticChannel) {
        LOG_INFO(MOD_GRAPHICS, "RDP audio: client did not join rdpsnd channel, audio disabled");
        return;
    }

    // Create RDPSND server context using the VCM from context initialization
    context->rdpsndContext = rdpsnd_server_context_new(context->vcm);
    if (!context->rdpsndContext) {
        LOG_ERROR(MOD_GRAPHICS, "RDP audio: failed to create RDPSND context");
        return;
    }

    // Configure audio formats - must allocate a copy because FreeRDP takes ownership and frees it
    AUDIO_FORMAT* formats = (AUDIO_FORMAT*)calloc(numServerAudioFormats_, sizeof(AUDIO_FORMAT));
    if (formats) {
        memcpy(formats, serverAudioFormats_, numServerAudioFormats_ * sizeof(AUDIO_FORMAT));
    }
    context->rdpsndContext->server_formats = formats;
    context->rdpsndContext->num_server_formats = numServerAudioFormats_;

    // Use static virtual channel (more compatible, DVC requires additional setup)
    context->rdpsndContext->use_dynamic_virtual_channel = FALSE;

    // Set source format (what we'll be sending)
    // We'll send 16-bit stereo PCM at 44.1kHz
    static AUDIO_FORMAT srcFormat = { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0, nullptr };
    context->rdpsndContext->src_format = &srcFormat;

    // Set latency (audio buffer size in milliseconds)
    context->rdpsndContext->latency = 50;  // 50ms buffer

    // Store peer context for callback access
    context->rdpsndContext->data = context;

    // Set the activated callback
    context->rdpsndContext->Activated = onRdpsndActivated;

    // Initialize the RDPSND channel (non-threaded mode, we'll drive it)
    LOG_DEBUG(MOD_GRAPHICS, "RDP audio: initializing RDPSND with {} server formats", numServerAudioFormats_);
    UINT status = context->rdpsndContext->Initialize(context->rdpsndContext, FALSE);
    if (status != CHANNEL_RC_OK) {
        LOG_ERROR(MOD_GRAPHICS, "RDP audio: failed to initialize RDPSND: {}", status);
        rdpsnd_server_context_free(context->rdpsndContext);
        context->rdpsndContext = nullptr;
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "RDP audio: RDPSND channel initialized for peer, waiting for client format response");
}

void RDPServer::onAudioActivated(RDPPeerContext* context) {
    if (!context || !context->rdpsndContext) {
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "RDP audio: audio channel activated, client has {} formats",
             context->rdpsndContext->num_client_formats);

    // Find a compatible format (prefer stereo 44.1kHz PCM)
    int16_t selectedFormat = -1;
    for (UINT16 i = 0; i < context->rdpsndContext->num_client_formats; i++) {
        const AUDIO_FORMAT* fmt = &context->rdpsndContext->client_formats[i];
        if (fmt->wFormatTag == WAVE_FORMAT_PCM &&
            fmt->nChannels == 2 &&
            fmt->wBitsPerSample == 16) {
            // Prefer 44.1kHz
            if (fmt->nSamplesPerSec == 44100) {
                selectedFormat = i;
                break;
            } else if (selectedFormat < 0) {
                selectedFormat = i;
            }
        }
    }

    // Fall back to first PCM format if no stereo found
    if (selectedFormat < 0) {
        for (UINT16 i = 0; i < context->rdpsndContext->num_client_formats; i++) {
            const AUDIO_FORMAT* fmt = &context->rdpsndContext->client_formats[i];
            if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
                selectedFormat = i;
                break;
            }
        }
    }

    if (selectedFormat >= 0) {
        const AUDIO_FORMAT* fmt = &context->rdpsndContext->client_formats[selectedFormat];
        LOG_INFO(MOD_GRAPHICS, "RDP audio: selected format {}: {}Hz, {}ch, {}bit",
                 selectedFormat, fmt->nSamplesPerSec, fmt->nChannels, fmt->wBitsPerSample);

        // Select the format
        UINT status = context->rdpsndContext->SelectFormat(context->rdpsndContext, selectedFormat);
        if (status == CHANNEL_RC_OK) {
            context->selectedAudioFormat = selectedFormat;
            context->audioActivated = true;
            audioReady_.store(true);
        } else {
            LOG_ERROR(MOD_GRAPHICS, "RDP audio: failed to select format: {}", status);
        }
    } else {
        LOG_ERROR(MOD_GRAPHICS, "RDP audio: no compatible audio format found");
    }
}

void RDPServer::sendAudioSamples(const int16_t* samples, size_t frameCount,
                                  uint32_t sampleRate, uint8_t channels) {
    if (!running_.load() || !audioEnabled_ || !audioReady_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(audioMutex_);

    // Send to all peers with active audio
    std::lock_guard<std::mutex> peersLock(peersMutex_);
    for (RDPPeerContext* context : activePeers_) {
        if (!context || !context->audioActivated || !context->rdpsndContext) {
            continue;
        }

        // Check if format matches (we expect stereo 44.1kHz)
        // TODO: Add sample rate conversion if needed

        // Send samples using SendSamples
        // The timestamp is used for synchronization
        context->rdpsndContext->SendSamples(
            context->rdpsndContext,
            samples,
            frameCount,
            audioTimestamp_
        );
    }

    // Update timestamp (in milliseconds worth of samples)
    // At 44.1kHz, 1ms = 44.1 samples
    audioTimestamp_ += static_cast<uint16_t>((frameCount * 1000) / sampleRate);
}

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP
