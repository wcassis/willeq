#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/xmi_decoder.h"

#include <filesystem>
#include <fstream>

using namespace EQT::Audio;

// Path to EQ client files for testing
static const char* EQ_PATH = "/home/user/projects/claude/EverQuestP1999";

// =============================================================================
// XMI Decoder Unit Tests
// =============================================================================

class XmiDecoderTest : public ::testing::Test {
protected:
    XmiDecoder decoder_;
};

TEST_F(XmiDecoderTest, EmptyDataReturnsEmpty) {
    std::vector<uint8_t> emptyData;
    auto result = decoder_.decode(emptyData);
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(decoder_.getError().empty());
}

TEST_F(XmiDecoderTest, TooSmallDataReturnsEmpty) {
    std::vector<uint8_t> smallData = {0x46, 0x4F, 0x52, 0x4D};  // Just "FORM"
    auto result = decoder_.decode(smallData);
    EXPECT_TRUE(result.empty());
}

TEST_F(XmiDecoderTest, InvalidMagicReturnsEmpty) {
    std::vector<uint8_t> invalidData(100, 0);  // All zeros
    auto result = decoder_.decode(invalidData);
    EXPECT_TRUE(result.empty());
    EXPECT_TRUE(decoder_.getError().find("FORM") != std::string::npos);
}

// =============================================================================
// XMI File Loading Tests (require EQ files)
// =============================================================================

class XmiFileTest : public ::testing::Test {
protected:
    XmiDecoder decoder_;
    std::string xmiDir_;

    void SetUp() override {
        xmiDir_ = std::string(EQ_PATH);
        if (!std::filesystem::exists(xmiDir_)) {
            GTEST_SKIP() << "EQ client path not found at: " << xmiDir_;
        }
    }

    // Helper to find an XMI file for testing
    std::string findXmiFile() {
        for (const auto& entry : std::filesystem::directory_iterator(xmiDir_)) {
            if (entry.path().extension() == ".xmi") {
                return entry.path().string();
            }
        }
        return "";
    }
};

TEST_F(XmiFileTest, DecodeRealXmiFile) {
    std::string xmiPath = findXmiFile();
    if (xmiPath.empty()) {
        GTEST_SKIP() << "No XMI files found in: " << xmiDir_;
    }

    auto midiData = decoder_.decodeFile(xmiPath);

    // Should produce non-empty output
    ASSERT_FALSE(midiData.empty()) << "Failed to decode: " << decoder_.getError();

    // Verify MIDI header
    ASSERT_GE(midiData.size(), 14u);
    EXPECT_EQ(midiData[0], 'M');
    EXPECT_EQ(midiData[1], 'T');
    EXPECT_EQ(midiData[2], 'h');
    EXPECT_EQ(midiData[3], 'd');

    // Header length should be 6
    EXPECT_EQ(midiData[4], 0);
    EXPECT_EQ(midiData[5], 0);
    EXPECT_EQ(midiData[6], 0);
    EXPECT_EQ(midiData[7], 6);

    // Format type 0 (single track)
    EXPECT_EQ(midiData[8], 0);
    EXPECT_EQ(midiData[9], 0);

    // Number of tracks should be 1
    EXPECT_EQ(midiData[10], 0);
    EXPECT_EQ(midiData[11], 1);

    // MTrk chunk should follow
    EXPECT_EQ(midiData[14], 'M');
    EXPECT_EQ(midiData[15], 'T');
    EXPECT_EQ(midiData[16], 'r');
    EXPECT_EQ(midiData[17], 'k');
}

TEST_F(XmiFileTest, DecodeQeynosMusic) {
    std::string xmiPath = xmiDir_ + "/qeynos.xmi";
    if (!std::filesystem::exists(xmiPath)) {
        // Try qeynos2
        xmiPath = xmiDir_ + "/qeynos2.xmi";
    }
    if (!std::filesystem::exists(xmiPath)) {
        GTEST_SKIP() << "Qeynos XMI not found";
    }

    auto midiData = decoder_.decodeFile(xmiPath);
    ASSERT_FALSE(midiData.empty()) << "Failed to decode: " << decoder_.getError();

    // Should be a reasonable size for zone music
    EXPECT_GT(midiData.size(), 100u);
}

TEST_F(XmiFileTest, DecodeFreportMusic) {
    std::string xmiPath = xmiDir_ + "/freporte.xmi";
    if (!std::filesystem::exists(xmiPath)) {
        xmiPath = xmiDir_ + "/freportn.xmi";
    }
    if (!std::filesystem::exists(xmiPath)) {
        GTEST_SKIP() << "Freeport XMI not found";
    }

    auto midiData = decoder_.decodeFile(xmiPath);
    ASSERT_FALSE(midiData.empty()) << "Failed to decode: " << decoder_.getError();
    EXPECT_GT(midiData.size(), 100u);
}

TEST_F(XmiFileTest, DecodeAkanonMusic) {
    std::string xmiPath = xmiDir_ + "/akanon.xmi";
    if (!std::filesystem::exists(xmiPath)) {
        GTEST_SKIP() << "Akanon XMI not found";
    }

    auto midiData = decoder_.decodeFile(xmiPath);
    ASSERT_FALSE(midiData.empty()) << "Failed to decode: " << decoder_.getError();
    EXPECT_GT(midiData.size(), 100u);
}

TEST_F(XmiFileTest, DecodeFelwitheaMusic) {
    std::string xmiPath = xmiDir_ + "/felwithea.xmi";
    if (!std::filesystem::exists(xmiPath)) {
        GTEST_SKIP() << "Felwithea XMI not found";
    }

    auto midiData = decoder_.decodeFile(xmiPath);
    ASSERT_FALSE(midiData.empty()) << "Failed to decode: " << decoder_.getError();
    EXPECT_GT(midiData.size(), 100u);
}

TEST_F(XmiFileTest, NonexistentFileReturnsEmpty) {
    auto midiData = decoder_.decodeFile("/nonexistent/path/music.xmi");
    EXPECT_TRUE(midiData.empty());
    EXPECT_FALSE(decoder_.getError().empty());
}

TEST_F(XmiFileTest, DecodeAllXmiFiles) {
    size_t successCount = 0;
    size_t failCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(xmiDir_)) {
        if (entry.path().extension() == ".xmi") {
            auto midiData = decoder_.decodeFile(entry.path().string());
            if (!midiData.empty()) {
                // Verify valid MIDI header
                if (midiData.size() >= 4 &&
                    midiData[0] == 'M' && midiData[1] == 'T' &&
                    midiData[2] == 'h' && midiData[3] == 'd') {
                    successCount++;
                } else {
                    failCount++;
                }
            } else {
                failCount++;
            }
        }
    }

    // Most files should decode successfully
    EXPECT_GT(successCount, 0u) << "No XMI files decoded successfully";
    // Allow some failures for edge cases
    EXPECT_LT(failCount, successCount) << "Too many decoding failures";

    if (successCount > 0) {
        double successRate = 100.0 * successCount / (successCount + failCount);
        std::cout << "XMI decode success rate: " << successRate << "% ("
                  << successCount << "/" << (successCount + failCount) << ")\n";
    }
}

// =============================================================================
// MIDI Output Validation Tests
// =============================================================================

TEST_F(XmiFileTest, MidiOutputHasEndOfTrack) {
    std::string xmiPath = findXmiFile();
    if (xmiPath.empty()) {
        GTEST_SKIP() << "No XMI files found";
    }

    auto midiData = decoder_.decodeFile(xmiPath);
    ASSERT_FALSE(midiData.empty());

    // MIDI should end with FF 2F 00 (End of Track meta event)
    ASSERT_GE(midiData.size(), 3u);
    bool hasEndOfTrack = false;

    // Search for End of Track marker (may have delta time before it)
    for (size_t i = 0; i + 2 < midiData.size(); ++i) {
        if (midiData[i] == 0xFF && midiData[i+1] == 0x2F && midiData[i+2] == 0x00) {
            hasEndOfTrack = true;
            break;
        }
    }

    EXPECT_TRUE(hasEndOfTrack) << "MIDI output missing End of Track marker";
}

#else

// Provide a dummy test when audio is not enabled
#include <gtest/gtest.h>

TEST(AudioDisabledTest, XmiDecoderNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif // WITH_AUDIO
