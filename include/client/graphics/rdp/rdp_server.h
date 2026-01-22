#ifndef EQT_GRAPHICS_RDP_SERVER_H
#define EQT_GRAPHICS_RDP_SERVER_H

#ifdef WITH_RDP

#include <freerdp/freerdp.h>
#include <freerdp/listener.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/codec/audio.h>
#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <string>
#include <cstdint>

namespace EQT {
namespace Graphics {

// Forward declarations
struct RDPPeerContext;

/**
 * Callback type for keyboard events from RDP clients.
 *
 * @param flags RDP keyboard flags (KBD_FLAGS_RELEASE, KBD_FLAGS_EXTENDED, etc.)
 * @param scancode The keyboard scancode
 */
using RDPKeyboardCallback = std::function<void(uint16_t flags, uint8_t scancode)>;

/**
 * Callback type for mouse events from RDP clients.
 *
 * @param flags RDP pointer flags (PTR_FLAGS_MOVE, PTR_FLAGS_BUTTON1, etc.)
 * @param x Mouse X position
 * @param y Mouse Y position
 */
using RDPMouseCallback = std::function<void(uint16_t flags, uint16_t x, uint16_t y)>;

/**
 * Native RDP server for streaming WillEQ graphics.
 *
 * This class provides an RDP server that can stream the game's framebuffer
 * to RDP clients (like Windows mstsc.exe or FreeRDP). It runs alongside
 * the existing X11/Xvfb+VNC setup as an additional display option.
 *
 * Usage:
 *   RDPServer server;
 *   server.initialize(3389);
 *   server.setCertificate("server.crt", "server.key");
 *   server.setResolution(1024, 768);
 *   server.setKeyboardCallback([](uint16_t flags, uint8_t code) { ... });
 *   server.setMouseCallback([](uint16_t flags, uint16_t x, uint16_t y) { ... });
 *   server.start();
 *   // In render loop:
 *   server.updateFrame(frameData, width, height, pitch, format);
 *   // When done:
 *   server.stop();
 */
class RDPServer {
public:
    RDPServer();
    ~RDPServer();

    // Non-copyable
    RDPServer(const RDPServer&) = delete;
    RDPServer& operator=(const RDPServer&) = delete;

    /**
     * Initialize the RDP server.
     *
     * @param port The port to listen on (default: 3389)
     * @return true on success, false on failure
     */
    bool initialize(uint16_t port = 3389);

    /**
     * Set SSL certificate paths for RDP security.
     *
     * RDP requires TLS for secure connections. If not set, the server
     * will generate a self-signed certificate (clients will see a warning).
     *
     * @param certPath Path to the certificate file (PEM format)
     * @param keyPath Path to the private key file (PEM format)
     */
    void setCertificate(const std::string& certPath, const std::string& keyPath);

    /**
     * Set the desktop resolution advertised to clients.
     *
     * Should match the game's rendering resolution.
     *
     * @param width Desktop width in pixels
     * @param height Desktop height in pixels
     */
    void setResolution(uint32_t width, uint32_t height);

    /**
     * Start the RDP server.
     *
     * Begins listening for connections and starts the listener thread.
     *
     * @return true on success, false on failure
     */
    bool start();

    /**
     * Stop the RDP server.
     *
     * Disconnects all clients and stops the listener thread.
     */
    void stop();

    /**
     * Check if the server is running.
     */
    bool isRunning() const { return running_.load(); }

    /**
     * Get the number of connected clients.
     */
    size_t getClientCount() const;

    /**
     * Update the frame buffer to send to clients.
     *
     * Call this after each render to push the new frame to RDP clients.
     * The data is copied internally, so the caller can reuse the buffer.
     *
     * @param frameData Pointer to pixel data (BGRA format expected)
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param pitch Bytes per row (usually width * 4 for BGRA)
     */
    void updateFrame(const uint8_t* frameData, uint32_t width, uint32_t height, uint32_t pitch);

    /**
     * Set the callback for keyboard events.
     */
    void setKeyboardCallback(RDPKeyboardCallback callback) { keyboardCallback_ = std::move(callback); }

    /**
     * Set the callback for mouse events.
     */
    void setMouseCallback(RDPMouseCallback callback) { mouseCallback_ = std::move(callback); }

    /**
     * Send audio samples to connected RDP clients.
     *
     * Call this from the audio mixing callback to stream audio.
     * Samples should be 16-bit signed PCM, stereo, 44100Hz.
     *
     * @param samples Pointer to sample data (interleaved stereo)
     * @param frameCount Number of frames (1 frame = 2 samples for stereo)
     * @param sampleRate Sample rate in Hz (e.g., 44100)
     * @param channels Number of channels (1=mono, 2=stereo)
     */
    void sendAudioSamples(const int16_t* samples, size_t frameCount,
                          uint32_t sampleRate, uint8_t channels);

    /**
     * Check if audio streaming is enabled and ready.
     */
    bool isAudioReady() const { return audioReady_.load(); }

    /**
     * Enable or disable audio streaming.
     */
    void setAudioEnabled(bool enabled) { audioEnabled_ = enabled; }
    bool isAudioEnabled() const { return audioEnabled_; }

    // Internal methods called by FreeRDP callbacks (public for C callback access)
    void onPeerConnected(RDPPeerContext* context);
    void onPeerDisconnected(RDPPeerContext* context);
    void onKeyboardEventInternal(uint16_t flags, uint8_t scancode);
    void onMouseEventInternal(uint16_t flags, uint16_t x, uint16_t y);

    // Audio callbacks (public for C callback access)
    void onAudioActivated(RDPPeerContext* context);
    void initAudioForPeer(RDPPeerContext* context);

    // Peer handling thread function (public for C callback)
    void peerThreadImpl(freerdp_peer* client);

    // Add peer thread to tracking
    void addPeerThread(std::thread&& thread);

    // Getters for peer initialization
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    const std::string& getCertPath() const { return certPath_; }
    const std::string& getKeyPath() const { return keyPath_; }

    // Create new certificate and key objects for a peer (caller takes ownership)
    // Each peer needs its own copy since FreeRDP frees them on disconnect
    rdpCertificate* createPeerCertificate();
    rdpPrivateKey* createPeerKey();

private:
    // Generate a self-signed certificate and key using OpenSSL
    bool generateSelfSignedCertificate();
    // Listener thread function
    void listenerThread();

    // Send a frame to a specific peer
    void sendFrameToPeer(RDPPeerContext* context);

    // FreeRDP listener
    freerdp_listener* listener_;

    // Threads
    std::thread listenerThread_;
    std::vector<std::thread> peerThreads_;

    // State
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;

    // Connected peers
    std::mutex peersMutex_;
    std::vector<RDPPeerContext*> activePeers_;

    // Configuration
    uint16_t port_;
    uint32_t width_;
    uint32_t height_;
    std::string certPath_;
    std::string keyPath_;

    // Certificate and key PEM data (stored as strings so we can create
    // fresh copies for each peer - FreeRDP takes ownership of the objects)
    std::string certPem_;
    std::string keyPem_;
    bool certGenerated_ = false;
    std::mutex certMutex_;  // Protects cert generation

    // Frame buffer (double-buffered for thread safety)
    std::mutex frameMutex_;
    std::vector<uint8_t> frameBuffer_;
    uint32_t frameWidth_;
    uint32_t frameHeight_;
    uint32_t framePitch_;
    std::atomic<bool> frameReady_;
    std::atomic<uint32_t> frameSequence_;  // Incremented on each new frame

    // Input callbacks
    RDPKeyboardCallback keyboardCallback_;
    RDPMouseCallback mouseCallback_;

    // Audio streaming
    bool audioEnabled_ = true;
    std::atomic<bool> audioReady_{false};
    std::mutex audioMutex_;
    uint16_t audioTimestamp_ = 0;

    // Server-supported audio formats
    static AUDIO_FORMAT serverAudioFormats_[];
    static const size_t numServerAudioFormats_;
};

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP

#endif // EQT_GRAPHICS_RDP_SERVER_H
