#ifdef WITH_RDP

#include <gtest/gtest.h>
#include "client/graphics/rdp/rdp_server.h"


using namespace EQT::Graphics;

// =============================================================================
// RDP Server Initialization Tests
// =============================================================================

class RDPServerInitTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Clean up any server instance
    }
};

TEST_F(RDPServerInitTest, ConstructorCreatesValidObject) {
    RDPServer server;
    EXPECT_FALSE(server.isRunning());
}

TEST_F(RDPServerInitTest, InitializeWithDefaultPort) {
    RDPServer server;
    EXPECT_TRUE(server.initialize());
    EXPECT_FALSE(server.isRunning());  // Not started yet
}

TEST_F(RDPServerInitTest, InitializeWithCustomPort) {
    RDPServer server;
    EXPECT_TRUE(server.initialize(13389));  // Non-privileged port
    EXPECT_FALSE(server.isRunning());
}

TEST_F(RDPServerInitTest, SetResolution) {
    RDPServer server;
    server.setResolution(1024, 768);
    EXPECT_EQ(server.getWidth(), 1024u);
    EXPECT_EQ(server.getHeight(), 768u);
}

TEST_F(RDPServerInitTest, SetResolutionDifferentSizes) {
    RDPServer server;

    server.setResolution(800, 600);
    EXPECT_EQ(server.getWidth(), 800u);
    EXPECT_EQ(server.getHeight(), 600u);

    server.setResolution(1920, 1080);
    EXPECT_EQ(server.getWidth(), 1920u);
    EXPECT_EQ(server.getHeight(), 1080u);
}

// =============================================================================
// RDP Server Lifecycle Tests
// =============================================================================

class RDPServerLifecycleTest : public ::testing::Test {
protected:
    RDPServer server_;

    void SetUp() override {
        server_.setResolution(800, 600);
        // NOTE: initialize() (with its listener port) is called per-TEST_F
        // below, not here. GTEST_DISCOVER_TESTS runs each TEST_F as its own
        // ctest test, i.e. its own OS process invocation of this binary
        // (`--gtest_filter=...`) -- so tests in this fixture that call
        // start() can run concurrently under `ctest -j`. A port shared
        // across those tests via a fixture-wide SetUp() collides
        // (`listener_->Open` EADDRINUSE) under parallel load; giving each
        // start()-calling test its own port avoids that.
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerLifecycleTest, StartAndStop) {
    server_.initialize(23389);
    EXPECT_FALSE(server_.isRunning());

    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());

    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, StopWithoutStart) {
    server_.initialize(23415);
    // Should not crash or throw
    EXPECT_FALSE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, MultipleStartStop) {
    // Start and stop - note that after stop, the server needs to be re-initialized
    EXPECT_TRUE(server_.initialize(23416));
    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());

    // Re-initialize (same port -- stop() already reset initialized_) and start again
    EXPECT_TRUE(server_.initialize(23416));
    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, ClientCountInitiallyZero) {
    server_.initialize(23417);
    EXPECT_EQ(server_.getClientCount(), 0u);

    EXPECT_TRUE(server_.start());
    EXPECT_EQ(server_.getClientCount(), 0u);

    server_.stop();
}

// =============================================================================
// RDP Server Callback Tests
// =============================================================================

class RDPServerCallbackTest : public ::testing::Test {
protected:
    RDPServer server_;
    bool keyboardCalled_ = false;
    bool mouseCalled_ = false;
    uint16_t lastKeyFlags_ = 0;
    uint8_t lastScancode_ = 0;
    uint16_t lastMouseFlags_ = 0;
    uint16_t lastMouseX_ = 0;
    uint16_t lastMouseY_ = 0;

    void SetUp() override {
        server_.initialize(23390);
        server_.setResolution(800, 600);

        server_.setKeyboardCallback([this](uint16_t flags, uint8_t scancode) {
            keyboardCalled_ = true;
            lastKeyFlags_ = flags;
            lastScancode_ = scancode;
        });

        server_.setMouseCallback([this](uint16_t flags, uint16_t x, uint16_t y) {
            mouseCalled_ = true;
            lastMouseFlags_ = flags;
            lastMouseX_ = x;
            lastMouseY_ = y;
        });
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerCallbackTest, KeyboardCallbackSet) {
    // Simulate keyboard event through internal method
    server_.onKeyboardEventInternal(0x0001, 0x1E);  // 'A' key

    EXPECT_TRUE(keyboardCalled_);
    EXPECT_EQ(lastKeyFlags_, 0x0001);
    EXPECT_EQ(lastScancode_, 0x1E);
}

TEST_F(RDPServerCallbackTest, MouseCallbackSet) {
    // Simulate mouse event through internal method
    server_.onMouseEventInternal(0x0800, 100, 200);  // Move

    EXPECT_TRUE(mouseCalled_);
    EXPECT_EQ(lastMouseFlags_, 0x0800);
    EXPECT_EQ(lastMouseX_, 100);
    EXPECT_EQ(lastMouseY_, 200);
}

TEST_F(RDPServerCallbackTest, NoCallbackDoesNotCrash) {
    RDPServer server;
    server.initialize(23391);
    // No callbacks set - should not crash
    server.onKeyboardEventInternal(0x0001, 0x1E);
    server.onMouseEventInternal(0x0800, 100, 200);
}

// =============================================================================
// RDP Server Frame Update Tests
// =============================================================================

class RDPServerFrameTest : public ::testing::Test {
protected:
    RDPServer server_;
    std::vector<uint8_t> testFrame_;

    void SetUp() override {
        server_.setResolution(100, 100);

        // Create a test frame (100x100 BGRA)
        testFrame_.resize(100 * 100 * 4, 0xFF);

        // NOTE: initialize() is called per-TEST_F below, not here -- see
        // the comment in RDPServerLifecycleTest::SetUp for why a port
        // shared across this fixture's TEST_Fs collides under `ctest -j`
        // (each TEST_F is its own process via GTEST_DISCOVER_TESTS).
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerFrameTest, UpdateFrameBeforeStart) {
    server_.initialize(23392);
    // Should not crash even if server not started
    server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);
}

TEST_F(RDPServerFrameTest, UpdateFrameAfterStart) {
    server_.initialize(23418);
    EXPECT_TRUE(server_.start());

    // Should not crash
    server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);

    server_.stop();
}

TEST_F(RDPServerFrameTest, UpdateFrameMultipleTimes) {
    server_.initialize(23419);
    EXPECT_TRUE(server_.start());

    // Simulate multiple frame updates
    for (int i = 0; i < 10; ++i) {
        server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);
    }

    server_.stop();
}

TEST_F(RDPServerFrameTest, UpdateFrameWithNullData) {
    server_.initialize(23420);
    // Note: This test checks that null data doesn't crash when server is NOT started
    // (no clients connected means no actual frame processing)
    // The updateFrame function should handle null gracefully
    server_.updateFrame(nullptr, 100, 100, 100 * 4);
    // If we got here without crashing, the test passes
}

TEST_F(RDPServerFrameTest, UpdateFrameWithZeroDimensions) {
    server_.initialize(23421);
    // Note: Zero dimensions should be handled gracefully
    server_.updateFrame(testFrame_.data(), 0, 0, 0);
    // If we got here without crashing, the test passes
}

// =============================================================================
// RDP Server Certificate Tests
// =============================================================================

class RDPServerCertificateTest : public ::testing::Test {
protected:
    RDPServer server_;

    void SetUp() override {
        server_.initialize(23393);
    }
};

TEST_F(RDPServerCertificateTest, SetCertificatePaths) {
    server_.setCertificate("/path/to/cert.pem", "/path/to/key.pem");

    EXPECT_EQ(server_.getCertPath(), "/path/to/cert.pem");
    EXPECT_EQ(server_.getKeyPath(), "/path/to/key.pem");
}

TEST_F(RDPServerCertificateTest, SetCertificateEmptyPaths) {
    server_.setCertificate("", "");

    EXPECT_EQ(server_.getCertPath(), "");
    EXPECT_EQ(server_.getKeyPath(), "");
}

TEST_F(RDPServerCertificateTest, GenerateSelfSignedCertificate) {
    // With no cert paths set, createPeerCertificate should generate one
    rdpCertificate* cert = server_.createPeerCertificate();
    rdpPrivateKey* key = server_.createPeerKey();

    ASSERT_NE(cert, nullptr) << "Should generate a self-signed certificate";
    ASSERT_NE(key, nullptr) << "Should generate a private key";

    // Clean up - caller owns the objects
    freerdp_certificate_free(cert);
    freerdp_key_free(key);
}

TEST_F(RDPServerCertificateTest, GeneratedCertificateIsRSA) {
    rdpCertificate* cert = server_.createPeerCertificate();
    rdpPrivateKey* key = server_.createPeerKey();

    ASSERT_NE(cert, nullptr);
    ASSERT_NE(key, nullptr);

    // Verify the key is RSA
    EXPECT_TRUE(freerdp_key_is_rsa(key)) << "Generated key should be RSA";
    EXPECT_EQ(freerdp_key_get_bits(key), 2048) << "Key should be 2048 bits";

    // Clean up - caller owns the objects
    freerdp_certificate_free(cert);
    freerdp_key_free(key);
}

TEST_F(RDPServerCertificateTest, MultiplePeersGetIndependentCopies) {
    // Create certificates for two "peers"
    rdpCertificate* cert1 = server_.createPeerCertificate();
    rdpPrivateKey* key1 = server_.createPeerKey();
    rdpCertificate* cert2 = server_.createPeerCertificate();
    rdpPrivateKey* key2 = server_.createPeerKey();

    ASSERT_NE(cert1, nullptr);
    ASSERT_NE(key1, nullptr);
    ASSERT_NE(cert2, nullptr);
    ASSERT_NE(key2, nullptr);

    // They should be different pointers (independent copies)
    EXPECT_NE(cert1, cert2) << "Each peer should get its own certificate copy";
    EXPECT_NE(key1, key2) << "Each peer should get its own key copy";

    // Free first peer's objects
    freerdp_certificate_free(cert1);
    freerdp_key_free(key1);

    // Second peer's objects should still be valid
    EXPECT_TRUE(freerdp_key_is_rsa(key2)) << "Second peer's key should still be valid";

    // Clean up second peer
    freerdp_certificate_free(cert2);
    freerdp_key_free(key2);
}

// =============================================================================
// RDP Server Thread Safety Tests
// =============================================================================

class RDPServerThreadTest : public ::testing::Test {
protected:
    RDPServer server_;

    void SetUp() override {
        server_.initialize(23394);
        server_.setResolution(800, 600);
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerThreadTest, UpdateFrameFromMultipleIterations) {
    EXPECT_TRUE(server_.start());

    std::vector<uint8_t> frame(800 * 600 * 4, 0x80);

    // Simulate rapid frame updates
    for (int i = 0; i < 100; ++i) {
        server_.updateFrame(frame.data(), 800, 600, 800 * 4);
    }

    server_.stop();
}

// =============================================================================
// T306: resource-release gaps on the two non-happy lifecycle paths
// (ported from the audited branch task/eqcpp-zoneprocess-track-b-remaining)
// =============================================================================

// Occupy a port with a real, successfully started RDPServer so that a second
// RDPServer::start() on the same port hits a failing listener_->Open(). Using
// an RDPServer (rather than a hand-rolled socket) guarantees the blocker binds
// exactly the address family set freerdp_listener_open() tries, so the second
// Open() cannot partially succeed on an unblocked family.
class PortBlocker {
public:
    explicit PortBlocker(uint16_t port) {
        ok_ = server_.initialize(port) && server_.start();
    }
    ~PortBlocker() {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
    bool ok() const { return ok_; }
private:
    RDPServer server_;
    bool ok_ = false;
};

class RDPServerCleanupTest : public ::testing::Test {
protected:
    void TearDown() override {}
};

// start() used to log-and-return on a failed listener_->Open(), keeping
// initialized_ == true and the listener alive. stop() then early-returned on
// !running_ and a retried initialize() early-returned on initialized_, so the
// server was permanently wedged on the port that had already failed: the
// listener leaked and every recovery attempt silently reused it.
TEST_F(RDPServerCleanupTest, FailedStartReleasesResourcesAndAllowsRecovery) {
    PortBlocker blocker(23461);
    ASSERT_TRUE(blocker.ok()) << "could not bind the blocker socket";

    RDPServer server;
    ASSERT_TRUE(server.initialize(23461));
    ASSERT_TRUE(server.isInitialized());

    EXPECT_FALSE(server.start());
    EXPECT_FALSE(server.isRunning());

    // The failed start must have released the listener and the Winsock
    // reference, i.e. dropped back to the uninitialized state.
    EXPECT_FALSE(server.isInitialized());

    // Which is what makes the failure recoverable: a fresh initialize() on a
    // free port must actually re-create the listener and bind THAT port,
    // instead of early-returning on a stale initialized_ and re-opening the
    // blocked one.
    ASSERT_TRUE(server.initialize(23462));
    EXPECT_TRUE(server.isInitialized());
    EXPECT_TRUE(server.start());
    EXPECT_TRUE(server.isRunning());
    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST_F(RDPServerCleanupTest, RepeatedFailedStartIsSafe) {
    PortBlocker blocker(23463);
    ASSERT_TRUE(blocker.ok()) << "could not bind the blocker socket";

    RDPServer server;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(server.initialize(23463)) << "iteration " << i;
        EXPECT_FALSE(server.start()) << "iteration " << i;
        EXPECT_FALSE(server.isRunning()) << "iteration " << i;
        EXPECT_FALSE(server.isInitialized()) << "iteration " << i;
    }
}

// ~RDPServer() calls stop(). stop()'s guard used to be `if (!running_) return;`
// alone, so a server that was initialized but never started freed nothing:
// the freerdp listener and the WSAStartup reference leaked at destruction.
// The guard is now `!initialized_ && !running_`.
TEST_F(RDPServerCleanupTest, InitWithoutStartIsSafeAndReleases) {
    RDPServer server;
    ASSERT_TRUE(server.initialize(23464));
    ASSERT_TRUE(server.isInitialized());
    EXPECT_FALSE(server.isRunning());

    // This is exactly what ~RDPServer() does.
    server.stop();

    EXPECT_FALSE(server.isRunning());
    EXPECT_FALSE(server.isInitialized())
        << "stop() left the listener and the Winsock reference owned";

    // Idempotent: a second stop() (destructor after an explicit stop()) is a
    // no-op, not a double free.
    server.stop();
    EXPECT_FALSE(server.isInitialized());
}

#else

// Provide a dummy test when RDP is not enabled
#include <gtest/gtest.h>

TEST(RDPServerDisabledTest, RDPNotEnabled) {
    GTEST_SKIP() << "RDP support not enabled in build";
}

#endif // WITH_RDP
