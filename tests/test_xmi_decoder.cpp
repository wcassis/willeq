#ifdef WITH_AUDIO

#include <gtest/gtest.h>
#include "client/audio/xmi_decoder.h"

#include <filesystem>
#include <fstream>

using namespace EQT::Audio;

// Path to EQ client files for testing
static std::string getEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}
static const std::string EQ_PATH = getEqClientPath();

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
// Synthetic byte-exact decode test (T22)
//
// The real-file tests above only check the MIDI header and that *some*
// non-empty, valid-looking output was produced — they never pin the actual
// decoded event bytes, so a mutation that corrupts the XMI variable-length
// quantity decode (readXmiVarLen, used for both meta-event lengths and
// note-off durations) can silently produce a differently-timed but still
// "successfully decoded" MIDI stream. This builds a minimal, fully-known
// synthetic XMI (one EVNT chunk: a note-on with a 2-byte-VLQ-encoded
// duration of 200 ticks — producing a note-off at t=200 — followed by a
// second note-on 50 ticks later, at t=50) and checks the exact track bytes.
// Sorting by absoluteTime reorders these to t=0, 50, 200, so the note-off's
// delta-time (200-50=150) depends on both readXmiVarLen (the duration) and
// on writeMidiOutput's delta computation (evt.absoluteTime - lastTime) —
// both are wrong under their respective mutants even though decode
// "succeeds" and produces plausible-looking, non-empty output.
// =============================================================================
TEST_F(XmiDecoderTest, SyntheticNoteDurationVlqIsExact) {
    std::vector<uint8_t> xmi;
    // FORM header (contents scanned for EVNT chunks regardless of container
    // structure past the initial FORM/XDIR|CAT check in parseHeader()).
    xmi.insert(xmi.end(), {'F', 'O', 'R', 'M'});
    xmi.insert(xmi.end(), {0, 0, 0, 0});  // FORM size (unused by parser)
    xmi.insert(xmi.end(), {'X', 'D', 'I', 'R'});

    // EVNT chunk: "EVNT" + big-endian size + payload.
    std::vector<uint8_t> payload;
    payload.push_back(0x00);        // delay 0 before note-on
    payload.push_back(0x90);        // note-on, channel 0
    payload.push_back(60);          // note
    payload.push_back(100);         // velocity
    // Duration = 200 ticks, XMI VLQ (7-bit groups, MSB-first): 0x81 0x48
    payload.push_back(0x81);
    payload.push_back(0x48);
    payload.push_back(50);          // delay 50 before the second note-on
    payload.push_back(0x90);        // note-on, channel 0
    payload.push_back(62);          // note
    payload.push_back(90);          // velocity
    payload.push_back(0x00);        // duration = 0 -> no note-off emitted
    // End of track meta event (consumed, not emitted as a real event —
    // the decoder synthesizes its own EOT).
    payload.push_back(0xFF);
    payload.push_back(0x2F);
    payload.push_back(0x00);        // metaLen = 0

    xmi.insert(xmi.end(), {'E', 'V', 'N', 'T'});
    uint32_t sizeBE =
        ((static_cast<uint32_t>(payload.size()) & 0xFF) << 24) |
        ((static_cast<uint32_t>(payload.size()) & 0xFF00) << 8) |
        ((static_cast<uint32_t>(payload.size()) & 0xFF0000) >> 8) |
        ((static_cast<uint32_t>(payload.size()) & 0xFF000000) >> 24);
    xmi.push_back(static_cast<uint8_t>(sizeBE & 0xFF));
    xmi.push_back(static_cast<uint8_t>((sizeBE >> 8) & 0xFF));
    xmi.push_back(static_cast<uint8_t>((sizeBE >> 16) & 0xFF));
    xmi.push_back(static_cast<uint8_t>((sizeBE >> 24) & 0xFF));
    xmi.insert(xmi.end(), payload.begin(), payload.end());

    auto midi = decoder_.decode(xmi);
    ASSERT_FALSE(midi.empty()) << decoder_.getError();

    // MThd (14 bytes) + "MTrk" + 4-byte length = 22-byte header before
    // track data starts.
    ASSERT_GE(midi.size(), 22u);
    std::vector<uint8_t> track(midi.begin() + 22, midi.end());

    // Events, sorted by absoluteTime: note-on@0, note-on2@50, note-off@200
    // (note-off's absoluteTime = currentTime-at-note-on(0) + duration(200),
    // NOT relative to the second note-on's delay).
    //
    // Expected track bytes:
    //   00 90 3C 64          -- delta=0,   note-on  ch0, note=60, vel=100
    //   32 90 3E 5A          -- delta=50,  note-on  ch0, note=62, vel=90
    //   81 16 80 3C 00       -- delta=150 (200-50, VLQ 0x81 0x16), note-off note=60, vel=0
    //   00 FF 2F 00          -- delta=0, synthesized End-Of-Track
    std::vector<uint8_t> expected = {
        0x00, 0x90, 0x3C, 0x64,
        0x32, 0x90, 0x3E, 0x5A,
        0x81, 0x16, 0x80, 0x3C, 0x00,
        0x00, 0xFF, 0x2F, 0x00
    };
    EXPECT_EQ(track, expected);
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
