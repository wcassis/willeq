// B1018 (GitLab #339, AUDIT-WAVE7 on MR !130): the "audio.vendor_music is
// parsed but ignored" WARN must fire only when a config actually sets
// `audio.vendor_music`.
//
// The defect (fable before this card): `SetVendorMusic()` in
// `include/client/bridge/bridge_component.h` carried the LOG_WARN in its body
// and `Application::initEverQuestClient()` called it UNCONDITIONALLY with
// `ApplicationConfig::audioVendorMusic`, whose default is "gl.xmi"
// (application.h:87). No tracked config sets the key, so EVERY WITH_AUDIO run
// logged the WARN even though the user never named a vendor music file. The
// fix moves the WARN to the PARSE site -- `application_config.cpp`'s
// `audio_config.isMember("vendor_music")` arm, which runs only when the key
// is present -- and reduces the setter to a no-op (the G04 golden entry 91
// still pins the CALL, BLESS-only).
//
// Both halves of the production path are exercised, as the card asks:
//   1. the config parse: `Application::loadConfigFile()` into a fresh
//      ApplicationConfig, with the key ABSENT (no WARN anywhere) and with the
//      key PRESENT (exactly one WARN, from the parse site);
//   2. Application's audio setter path: `eq.Bridge().SetVendorMusic(
//      config.audioVendorMusic)` on a fresh EverQuest client -- the line
//      application.cpp:356 runs -- which must log NOTHING in either half.
//
// How it observes the log: the same steady-state capture idiom as
// test_renderer_command_echo.cpp (redirect fd 1 to a temp file; the logging
// drain thread writes there; FlushThreadLog nudges the thread-local
// double-buffer; poll for the line). The WARN level is enabled on the global
// scale (LOG_WARN <= LOG_INFO) and -- because loadConfigFile's object-format
// path runs InitLoggingFromJson, which would otherwise re-derive the level
// from THIS config -- the fixture restores the pre-test global level in the
// dtor before the capture is torn down, so no other test in this binary sees
// a raised level.
//
// RED before the fix (paste in refactor/B1018/README.md): the key-ABSENT half
// finds the WARN -- the parse site had none (it was the INFO "Vendor music
// from config") and the setter logged unconditionally on the default "gl.xmi",
// so the absent half counts 1 where the fix allows 0. (The key-PRESENT half
// passes both pre- and post-fix: exactly one setter WARN either way -- the
// pre-fix defect is that the same WARN fired WITHOUT the key, which is what
// the absent half asserts.)
#ifdef WITH_AUDIO
#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "client/application.h"
#include "common/logging.h"
#include "zone_stream_test_utils.h"

namespace {

// The exact capture shape test_renderer_command_echo.cpp pins (T31 idiom):
// fd 1 to a temp file, poll the drain thread's output, restore in the dtor.
class StdoutCapture {
public:
    StdoutCapture() {
        char tmpl[] = "/tmp/willeq_test_vendor_music_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) close(fd);
        path_ = tmpl;
        fflush(stdout);
        std::cout.flush();
        int file_fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        saved_ = dup(1);
        dup2(file_fd, 1);
        close(file_fd);
    }

    ~StdoutCapture() {
        fflush(stdout);
        std::cout.flush();
        if (saved_ >= 0) {
            dup2(saved_, 1);
            close(saved_);
        }
        unlink(path_.c_str());
    }

    std::string output() const {
        std::ifstream in(path_);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void nudge() const { FlushThreadLog(); }

private:
    std::string path_ = "";
    int saved_ = -1;
};

class VendorMusicWarnTest : public ::testing::Test {
protected:
    // The capture outlives the level restore: members are destroyed in
    // reverse DECLARATION order, so `cap` (declared first) tears down AFTER
    // the fixture dtor has put the global level back.
    StdoutCapture cap;
    int savedLevel;

    VendorMusicWarnTest() {
        savedLevel = GetLogLevel();
        SetLogLevel(LOG_INFO);  // the WARN is logged at LOG_WARN <= LOG_INFO
    }
    ~VendorMusicWarnTest() override { SetLogLevel(savedLevel); }
};

// Writes a minimal object-format config (the shape loadConfigFile() parses);
// `vendor` adds `"vendor_music": "<file>"` to the audio block.
std::string WriteConfig(const std::string& path, const std::string& vendor) {
    std::ofstream out(path);
    out << "{\n";
    out << "  \"audio\": {\n";
    out << "    \"enabled\": true\n";
    if (!vendor.empty()) {
        out << ",\n    \"vendor_music\": \"" << vendor << "\"\n";
    }
    out << "  }\n}\n";
    return path;
}

std::string WarnLine() {
    return "audio.vendor_music is parsed but ignored";
}

int CountWarns(const std::string& text) {
    int n = 0;
    size_t pos = 0;
    while ((pos = text.find(WarnLine(), pos)) != std::string::npos) {
        ++n;
        pos += WarnLine().size();
    }
    return n;
}

// The whole production audio path for this key:
//   1. the exact config parse, Application::loadConfigFile() (the post-B1018
//      WARN lives in its `audio_config.isMember("vendor_music")` arm);
//   2. Application's audio setter, the line initEverQuestClient() runs
//      unconditionally -- application.cpp:356's
//      `m_eqClient->Bridge().SetVendorMusic(config.audioVendorMusic)` -- on a
//      fresh client, which is where the pre-B1018 WARN lived.
// False only if the parse itself fails (the caller asserts it away).
bool RunFullPath(const StdoutCapture& cap, const std::string& configPath,
                 std::string& out) {
    eqt::ApplicationConfig config;
    if (!eqt::Application::loadConfigFile(configPath, config)) return false;
    EverQuest eq = zonetest::MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.Bridge().SetVendorMusic(config.audioVendorMusic);
    eq.setBridge(nullptr);
    cap.nudge();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    cap.nudge();
    out = cap.output();
    return true;
}

}  // namespace

TEST_F(VendorMusicWarnTest, NoVendorMusicKeyNoParsedButIgnoredWarn) {
    const std::string path = ::testing::TempDir() + "b1018_vendor_absent.json";
    WriteConfig(path, "");
    std::string out;
    ASSERT_TRUE(RunFullPath(cap, path, out));
    EXPECT_EQ(CountWarns(out), 0)
        << "no config set audio.vendor_music, yet the ignore WARN fired "
           "(B1018: SetVendorMusic() logged unconditionally on the default "
           "\"gl.xmi\" on every WITH_AUDIO run). Captured output: "
        << out;
}

TEST_F(VendorMusicWarnTest, VendorMusicKeyLogsExactlyOneWarn) {
    const std::string path = ::testing::TempDir() + "b1018_vendor_present.json";
    WriteConfig(path, "my_music.xmi");
    std::string out;
    ASSERT_TRUE(RunFullPath(cap, path, out));
    EXPECT_EQ(CountWarns(out), 1)
        << "with audio.vendor_music set, exactly ONE \"parsed but ignored\" "
           "WARN is expected (the parse site). Captured output: "
        << out;
}
#endif  // WITH_AUDIO
