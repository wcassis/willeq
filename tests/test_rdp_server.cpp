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
        // Use a high port number to avoid privilege issues
        server_.initialize(23389);
        server_.setResolution(800, 600);
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerLifecycleTest, StartAndStop) {
    EXPECT_FALSE(server_.isRunning());

    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());

    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, StopWithoutStart) {
    // Should not crash or throw
    EXPECT_FALSE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, MultipleStartStop) {
    // Start and stop - note that after stop, the server needs to be re-initialized
    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());

    // Re-initialize and start again
    EXPECT_TRUE(server_.initialize(23389));
    EXPECT_TRUE(server_.start());
    EXPECT_TRUE(server_.isRunning());
    server_.stop();
    EXPECT_FALSE(server_.isRunning());
}

TEST_F(RDPServerLifecycleTest, ClientCountInitiallyZero) {
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
        server_.initialize(23392);
        server_.setResolution(100, 100);

        // Create a test frame (100x100 BGRA)
        testFrame_.resize(100 * 100 * 4, 0xFF);
    }

    void TearDown() override {
        if (server_.isRunning()) {
            server_.stop();
        }
    }
};

TEST_F(RDPServerFrameTest, UpdateFrameBeforeStart) {
    // Should not crash even if server not started
    server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);
}

TEST_F(RDPServerFrameTest, UpdateFrameAfterStart) {
    EXPECT_TRUE(server_.start());

    // Should not crash
    server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);

    server_.stop();
}

TEST_F(RDPServerFrameTest, UpdateFrameMultipleTimes) {
    EXPECT_TRUE(server_.start());

    // Simulate multiple frame updates
    for (int i = 0; i < 10; ++i) {
        server_.updateFrame(testFrame_.data(), 100, 100, 100 * 4);
    }

    server_.stop();
}

TEST_F(RDPServerFrameTest, UpdateFrameWithNullData) {
    // Note: This test checks that null data doesn't crash when server is NOT started
    // (no clients connected means no actual frame processing)
    // The updateFrame function should handle null gracefully
    server_.updateFrame(nullptr, 100, 100, 100 * 4);
    // If we got here without crashing, the test passes
}

TEST_F(RDPServerFrameTest, UpdateFrameWithZeroDimensions) {
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

#else

// Provide a dummy test when RDP is not enabled
#include <gtest/gtest.h>

TEST(RDPServerDisabledTest, RDPNotEnabled) {
    GTEST_SKIP() << "RDP support not enabled in build";
}

#endif // WITH_RDP
