// B925 (issue #244/#258): the repeatable `live`-labelled zone-link check.
//
// This is the ctest half of the acceptance. The state machine itself is the
// zone_link_hold scenario (src/client/scenario/scenario_zone_link_hold.cpp),
// which is exercised offline by test_scenario_state_machines.cpp. HERE it is
// run against a REAL eqemu stack: we launch the shipped willeq binary in the
// scenario lane and let the process exit code be the verdict -- exactly the
// "a live verification that exits with a code" contract of T410.
//
//   exit 0  = held the zone link for --scenario-hold (the >=5 min bar)
//   exit 2  = timeout: the link dropped and never recovered (the R1 defect)
//   exit 3  = could-not-start: never zoned in (server down, bad login, ...)
//
// It is registered under -DEQT_LIVE_TESTS=ON and labelled "live", so it can
// never join the default suite (T164's rule). It GTEST_SKIPs when it cannot
// reach a stack, so an operator on a clean checkout sees a skip, not a red:
//   * no config at $WILLEQ_B925_CONFIG (default: the B909 iceclad rig path)
//   * the login port in the config does not answer a TCP connect
//
// Run it (stack up, watcher per the run under test):
//   cmake -S . -B build -DEQT_LIVE_TESTS=ON ...
//   WILLEQ_B925_CONFIG=/path/to/iceclad.json ctest --test-dir build \
//       -R ZoneLinkHold --output-on-failure
// or directly:
//   ./build/bin/test_zone_link_hold

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <json/json.h>

namespace {

// The B909 baseline rig's iceclad config, as the default. Overridable so the
// same test drives overthere on the Pi (issue #258) with a different file.
const char* defaultConfigPath = "/home/user/kb-evidence/b925-zone-link-drops/iceclad.json";

// Load host/port from the config's top-level "host"/"port" (login server).
struct HostPort {
    std::string host = "127.0.0.1";
    int port = 5998;
    bool loaded = false;
};

HostPort loadHostPort(const std::string& path) {
    HostPort hp;
    std::ifstream f(path);
    if (!f) {
        return hp;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!Json::parseFromStream(builder, f, &root, &errs)) {
        return hp;
    }
    if (root.isMember("host") && root["host"].isString()) {
        hp.host = root["host"].asString();
    }
    if (root.isMember("port") && root["port"].isInt()) {
        hp.port = root["port"].asInt();
    }
    hp.loaded = true;
    return hp;
}

// A TCP connect to the login port is the reachability probe. eqemu's login
// server listens on 5998; a live stack answers. (The client itself logs in
// over UDP, but a live listen on the TCP port is a reliable liveness signal
// for the whole stack, and it is what a down stack does NOT answer.)
bool portAnswers(const std::string& host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Try a name lookup too (the config may name the VM by hostname).
        hostent* he = ::gethostbyname(host.c_str());
        if (!he) {
            ::close(fd);
            return false;
        }
        addr.sin_addr = *reinterpret_cast<in_addr*>(he->h_addr_list[0]);
    }
    // A 2 s connect timeout so a hung stack cannot stall the test forever.
    timeval tv{};
    tv.tv_sec = 2;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
    return rc == 0;
}

// Resolve the willeq binary relative to the test executable: the test lives
// at <build>/bin/test_zone_link_hold and willeq at <build>/bin/willeq.
std::string willeqPath() {
    std::error_code ec;
    std::filesystem::path self = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec) {
        std::filesystem::path cand = self.parent_path() / "willeq";
        if (std::filesystem::exists(cand)) {
            return cand.string();
        }
    }
    return "willeq";
}

int runScenario(const std::string& config, double holdSeconds, std::string& stdout) {
    // fork + exec the shipped binary in the scenario lane. The child's stdout
    // goes to a temp file the parent reads afterwards; the exit code is the
    // verdict (T410's contract: a dead client can never return 0).
    std::string outFile = "/tmp/b925_zone_link_hold_" + std::to_string(getpid()) + ".out";
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // Two traps, both real:
        //  * argv must be a NUL-terminated array of separate flag/value
        //    strings -- execvp does NOT word-split, so "-c <path>" as ONE
        //    string is an unrecognized flag and the config silently defaults
        //    to willeq.json (which is how this looked like a missing binary).
        //  * every string is bound to a NAMED local first: building argv from
        //    temporary .c_str() calls leaves those pointers dangling by the
        //    time execvp runs (ENOSYS/ENOENT -> exit 127).
        const std::string willeq = willeqPath();
        const std::string holdVal = std::to_string((int)holdSeconds);
        const std::string timeoutVal =
            std::to_string((int)(holdSeconds + 120));
        FILE* out = std::fopen(outFile.c_str(), "w");
        if (out) {
            dup2(fileno(out), STDOUT_FILENO);
            dup2(fileno(out), STDERR_FILENO);
            std::fclose(out);
        }
        const char* argv[] = {
            willeq.c_str(),
            "--scenario", "zone_link_hold",
            "--scenario-hold", holdVal.c_str(),
            "--timeout", timeoutVal.c_str(),
            "-c", config.c_str(),
            "--log-level=INFO",
            nullptr,
        };
        execvp(argv[0], const_cast<char* const*>(argv));
        // exec failed -- a code the parent can distinguish from a real verdict.
        _exit(127);
    }
    // parent
    int status = 0;
    waitpid(pid, &status, 0);
    std::ifstream f(outFile);
    std::ostringstream oss;
    oss << f.rdbuf();
    stdout = oss.str();
    const bool keep = std::getenv("WILLEQ_B925_KEEP_OUT") != nullptr;
    if (!keep) {
        std::filesystem::remove(outFile);
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

}  // namespace

TEST(ZoneLinkHold, HoldsTheZoneLinkAgainstALiveStack) {
    const char* env = std::getenv("WILLEQ_B925_CONFIG");
    const std::string config = env ? env : defaultConfigPath;

    std::error_code ec;
    if (!std::filesystem::exists(config, ec)) {
        GTEST_SKIP() << "No B925 config at " << config
                     << " (set WILLEQ_B925_CONFIG to the rig's config)";
    }
    const HostPort hp = loadHostPort(config);
    if (!hp.loaded) {
        GTEST_SKIP() << "Could not parse host/port from " << config;
    }
    if (!portAnswers(hp.host, hp.port)) {
        GTEST_SKIP() << "No eqemu stack answering on " << hp.host << ":" << hp.port
                     << " -- start it (scripts/eqemu/up.sh) and re-run";
    }

    // The >=5 min acceptance bar, in seconds. Overridable so an operator can
    // run a shorter leg (e.g. 120) to smoke-test the lane before a full run.
    const char* holdEnv = std::getenv("WILLEQ_B925_HOLD");
    const double holdSeconds = holdEnv ? std::atof(holdEnv) : 300.0;

    std::string out;
    const int code = runScenario(config, holdSeconds, out);
    EXPECT_EQ(code, 0) << "zone_link_hold exited " << code
                       << " (0=held, 2=dropped-and-never-recovered, "
                          "3=never-zoned-in). The >=5 min iceclad/overthere "
                          "hold is the acceptance; a non-zero exit is the "
                          "B925/B940 defect still present.\n"
                       << out;
}
