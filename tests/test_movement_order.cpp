// G06 (refactor/G06_update_player_movement.md): pin the ORDER of the player's
// per-frame movement statements.
//
// PlayerCameraComponent::updatePlayerMovement() was one 806-line straight-line
// body. G06 cut it into eight phases (five of them dispatched from the
// collision phase) at this file's own blank-line seams. Nothing about that cut
// is visible to the compiler: a phase called in the wrong place, a statement
// hoisted across a cut, or a phase left uncalled all compile perfectly and all
// change what the player does.
//
// WHY A TEXT PARSE AND NOT A RUNTIME OBSERVATION (G03 correction 9). There is
// no container to interrogate -- the order IS the source. updatePlayerMovement()
// cannot be driven in a unit test either: it needs a live Irrlicht device, an
// event receiver, a loaded zone with collision geometry, an entity renderer and
// a bridge. So the TU is the record, and this test reads it.
//
// The golden was extracted from the PRE-G06 body, before a line of it was
// touched, so it is a genuine before/after comparison and not a snapshot of
// whatever the split happened to produce.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;
const fs::path kTU =
    kRoot / "src" / "client" / "graphics" / "player" / "renderer_player_physics.cpp";
const fs::path kHeader =
    kRoot / "include" / "client" / "graphics" / "player" / "player_camera_component.h";
const fs::path kGolden = kRoot / "tests" / "goldens" / "movement_order_g06.txt";
const char* kDriver = "updatePlayerMovement";
// The eight phases updatePlayerMovement() is now the list of.
const std::vector<std::string> kPhases = {
    "readMovementInput",
    "updateHeadingAndPitch",
    "computeMovementVector",
    "applyProposedPositionAndJumpPhysics",
    "resolveMovementCollision",
    "publishPositionAndCamera",
    "sendPositionUpdateToServer",
    "updatePlayerMovementAnimation",
};

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur += c; }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> readLines(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return splitLines(ss.str());
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// The fields of PlayerCameraComponent::MovementFrame, parsed from the header
// rather than hard-coded: a field added or dropped there must move this list,
// or the normalisation below silently stops matching.
const std::vector<std::string>& frameFields() {
    static const std::vector<std::string> fields = [] {
        std::vector<std::string> out;
        const auto lines = readLines(kHeader);
        bool inStruct = false;
        static const std::regex field(R"(^(?:float|bool)\s+(\w+)\s*=)");
        for (const auto& raw : lines) {
            const std::string s = trim(raw);
            if (s.find("struct MovementFrame {") != std::string::npos) { inStruct = true; continue; }
            if (inStruct && s == "};") break;
            if (!inStruct) continue;
            std::smatch m;
            if (std::regex_search(s, m, field)) out.push_back(m[1].str());
        }
        return out;
    }();
    return fields;
}

bool isFrameField(const std::string& name) {
    const auto& f = frameFields();
    return std::find(f.begin(), f.end(), name) != f.end();
}

// THE NORMALISATION, mirrored exactly by the golden extractor
// (refactor/G06_update_player_movement.md Review):
//   1. strip surrounding whitespace;
//   2. drop blank, `//`-comment, preprocessor and brace-only lines;
//   3. drop the frame PLUMBING the split introduced -- the `MovementFrame frame;`
//      declaration, `frame.deltaTime = deltaTime;`, and the per-phase reference
//      bindings `<type>[&] X = frame.X;` (the alias name MUST equal the field
//      name, so a binding that renames a field is NOT dropped and shows up as a
//      diff);
//   4. drop a leading `float `/`bool ` from a declaration of a frame FIELD --
//      those twelve declarations became assignments when their storage moved
//      into MovementFrame, and that is the one difference between the pre- and
//      post-split text this golden deliberately does not police. A declaration
//      of anything else keeps its type, so `static` never disappears and a
//      genuine new local is still visible.
// Returns false when the line is dropped.
bool normalize(const std::string& raw, std::string* out) {
    std::string s = trim(raw);
    if (s.empty()) return false;
    if (s.rfind("//", 0) == 0) return false;
    if (s.rfind("#", 0) == 0) return false;
    if (s.find_first_not_of("{}); ") == std::string::npos) return false;
    if (s == "MovementFrame frame;" || s == "frame.deltaTime = deltaTime;") return false;

    static const std::regex alias(R"(^(?:const )?(?:float|bool)&? (\w+) = frame\.(\w+);$)");
    std::smatch m;
    if (std::regex_match(s, m, alias) && m[1] == m[2] && isFrameField(m[1].str())) return false;

    static const std::regex decl(R"(^(float|bool) (\w+)\b)");
    if (std::regex_search(s, m, decl) && m.position(0) == 0 && isFrameField(m[2].str())) {
        s = s.substr(s.find(' ') + 1);
    }
    *out = s;
    return true;
}

struct TU {
    std::vector<std::string> order;          // definition order in the file
    std::vector<std::vector<std::string>> bodies;
    int indexOf(const std::string& n) const {
        for (size_t i = 0; i < order.size(); ++i) if (order[i] == n) return static_cast<int>(i);
        return -1;
    }
};

// Every `void|bool PlayerCameraComponent::<name>(` at column 0, with its body
// delimited by brace depth. The signature may span several lines (phase 5c's
// does), so body collection starts only after the opening brace.
TU parseTU() {
    TU tu;
    const auto lines = readLines(kTU);
    static const std::regex def(R"(^(?:void|bool) PlayerCameraComponent::(\w+)\()");
    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (!std::regex_search(lines[i], m, def)) continue;
        int depth = 0;
        bool opened = false;
        std::vector<std::string> body;
        size_t j = i;
        for (; j < lines.size(); ++j) {
            const std::string code = lines[j].substr(0, lines[j].find("//"));
            const bool wasOpen = opened;
            for (char c : code) {
                if (c == '{') { depth++; opened = true; }
                else if (c == '}') { depth--; }
            }
            if (opened && depth == 0) break;
            if (wasOpen) body.push_back(lines[j]);
        }
        EXPECT_TRUE(opened) << "could not delimit " << m[1].str() << "()";
        tu.order.push_back(m[1].str());
        tu.bodies.push_back(body);
        i = j;
    }
    return tu;
}

// Walk a body in order; a statement that is a call to another method DEFINED IN
// THIS TU expands recursively to that method's normalised body. Everything else
// is emitted as itself.
void composeInto(const TU& tu, const std::string& name, std::vector<std::string>* out) {
    const int idx = tu.indexOf(name);
    ASSERT_GE(idx, 0) << name << "() is not defined in " << kTU;
    static const std::regex call(R"(^(\w+)\()");
    for (const auto& raw : tu.bodies[idx]) {
        const std::string s = trim(raw);
        std::smatch m;
        if (std::regex_search(s, m, call) && m.position(0) == 0 && tu.indexOf(m[1].str()) >= 0) {
            composeInto(tu, m[1].str(), out);
            continue;
        }
        std::string n;
        if (normalize(raw, &n)) out->push_back(n);
    }
}

std::vector<std::string> composedOrder() {
    const TU tu = parseTU();
    std::vector<std::string> out;
    composeInto(tu, kDriver, &out);
    return out;
}

std::vector<std::string> goldenOrder() {
    std::vector<std::string> out;
    for (const auto& line : readLines(kGolden)) {
        if (line.empty()) continue;
        const size_t sp = line.find(' ');
        EXPECT_NE(sp, std::string::npos) << "malformed golden line: " << line;
        out.push_back(line.substr(sp + 1));
    }
    return out;
}

// 471 entries as extracted from the pre-G06 body, plus the ONE reviewed
// insertion below. The golden is never regenerated from the tip; a statement
// enters it only by being named here.
//
// T337: T332 (B049) insertion, two statements, at 410 and 440.
//   410 `playerPositionIsSeed_ = false;`      -- publishPositionAndCamera(),
//       inside `if (positionChanged)`, immediately after `ctx_.playerZ = newZ;`
//   440 `intent.seededPosition = playerPositionIsSeed_;` -- in
//       sendPositionUpdateToServer(), immediately after `intent.heading = ...`
// T332 branched before G06 merged, so its two lines were absent from the
// golden and this case failed on the tip at statement 410. Both changes are
// correct; the golden learned the insertion (see
// refactor/T337_movement_golden_learns_t332.md).
//
// T343: B002 (autorun) insertion, two statements, at 27 and 28, in
// readMovementInput() immediately BEFORE
// `playerMovement_.moveForward = forward || playerMovement_.autorun;`
//   27 `if (playerMovement_.autorun && (forward || backward || strafeLeft || strafeRight)) {`
//   28 `playerMovement_.autorun = false;`
// The autorun flag had no writer at all until T343 wired the hotkey arm; this
// is its cancel half, and it is a BEHAVIOUR change stated here, not a
// regeneration from the tip.
//
// T437: B435 (the ceiling sentinel) and B436. The golden LEARNED this edit in
// place -- every statement below is an in-place rewrite of the statement that
// was already there, at the same position; NOTHING was inserted and nothing
// moved. Two statements were DELETED (475 -> 473), and they are named here:
//   * `float maxReasonableGround = ctx_.playerZ + 100.0f;  // Ground can't ...`
//     in resolveIrrlichtClearPath(), and
//   * `float maxReasonableGround = ctx_.playerZ + 100.0f;`
//     in resolveIrrlichtBlockedPath().
// Both existed only to keep findGroundZBspFiltered()'s `feetZ + 1000` ceiling
// sentinel out of the swim floor clamp. The ground queries now answer a
// GroundZ (include/client/graphics/player/ground_query.h): a ceiling is
// hasFloor() == false and cannot reach a clamp or a landing at all, so the
// heuristic had nothing left to exclude -- while still wrongly excluding a
// GENUINE floor more than 100 units above the player.
// The rewrites, all in place, all of the same statement:
//   * five `float groundZ = ...` / `float targetGroundZ = ...` /
//     `float xGroundZ = ...` etc. declarations became `GroundZ ...`;
//   * the FOUR jump landings' `feetZ <= groundZ && verticalVelocity <= 0`
//     became `ShouldLandJump(groundZ, feetZ, playerMovement_.verticalVelocity)`
//     -- the B435 fix;
//   * the FOUR swim floor clamps became `ShouldClampSwimToFloor(...)`;
//   * the step-height and wall-slide sites gained an explicit
//     `groundZ.hasFloor() &&`, and read `.z()` / `.zOr(currentFeetZ)` where
//     they used to read the bare float.
// The `!= BEST_Z_INVALID` guards that vanish were DEAD: findGroundZ() rewrote
// every failure to the caller's own z, so they never fired. They are now live,
// spelled hasFloor(), and that is a stated behaviour change (T437 Review).
//
// T440: B438 (a total ray miss is NoFloor) and the deletion of the
// findGroundZIrrlicht() stub. The golden LEARNED this edit in place: FIVE
// three-line ternaries
//     `GroundZ groundZ = useBspFiltered`
//     `? findGroundZBspFiltered(<args>)`
//     `: findGroundZIrrlicht(<args>);`
// each collapsed to the one statement
//     `GroundZ groundZ = findGroundZBspFiltered(<args>);`
// at the SAME position -- ten statements deleted (473 -> 463), nothing
// inserted, nothing moved, no other line touched. findGroundZIrrlicht() was a
// LOG_WARN plus "the floor is under your feet", and its `useBspFiltered`
// selector can never be false inside resolveIrrlichtCollision(): that body is
// entered only when ctx_.regionWorldTriangles is non-empty, and region
// triangles exist only when the BSP tree was installed. The remaining
// `useBspFiltered` ternaries (the checkCollisionIrrlicht ones) are untouched.
constexpr size_t kGoldenSize = 463;

}  // namespace

// The empty-file trap (Batch G correction 5 / K11 ee): a truncated or
// regenerated golden must not let everything below pass vacuously.
TEST(MovementOrder, GoldenIsPresentAndTheRightSize) {
    const auto golden = goldenOrder();
    ASSERT_FALSE(golden.empty()) << kGolden << " is empty -- nothing below proves anything";
    EXPECT_EQ(golden.size(), kGoldenSize)
        << "the movement-order golden changed size. It was extracted from the PRE-G06\n"
           "body and is not to be regenerated to match a change: if a movement statement\n"
           "was genuinely added or removed, that is a BEHAVIOUR change and belongs in a\n"
           "unit that says so -- and is INSERTED into the golden at its position, named in\n"
           "the comment above kGoldenSize, never regenerated from the tip.";
}

TEST(MovementOrder, EveryPhaseIsCalledExactlyOnceAndNoneIsOrphaned) {
    const TU tu = parseTU();
    const int driver = tu.indexOf(kDriver);
    ASSERT_GE(driver, 0);

    // The driver calls the eight top-level phases, in order, once each.
    std::vector<std::string> called;
    static const std::regex call(R"(^(\w+)\(frame\);$)");
    for (const auto& raw : tu.bodies[driver]) {
        std::smatch m;
        const std::string s = trim(raw);
        if (std::regex_match(s, m, call)) called.push_back(m[1].str());
    }
    EXPECT_EQ(called, kPhases)
        << "updatePlayerMovement() no longer calls the eight movement phases in order.\n"
           "The order is the behaviour: input before intent, intent before collision,\n"
           "collision before the position is published, the publish before the camera\n"
           "follows it, and the server sync after all of it.";

    // No method defined in this TU is left unreached: every definition other
    // than the driver must be called from some OTHER body in the file.
    for (const auto& name : tu.order) {
        if (name == kDriver) continue;
        bool found = false;
        for (size_t i = 0; i < tu.bodies.size() && !found; ++i) {
            if (tu.order[i] == name) continue;
            for (const auto& raw : tu.bodies[i]) {
                if (trim(raw).rfind(name + "(", 0) == 0) { found = true; break; }
            }
        }
        EXPECT_TRUE(found)
            << name << "() is defined in " << kTU << " but is never called from another\n"
               "body in it. A movement phase nothing calls is the reachability defect this\n"
               "project has now hit ten times.";
    }
}

// The anti-regrowth clause (G04 correction 16): the driver may be the list and
// nothing else, or the 806 lines simply grow back one statement at a time.
TEST(MovementOrder, UpdatePlayerMovementIsOnlyTheOrderedList) {
    const TU tu = parseTU();
    const int driver = tu.indexOf(kDriver);
    ASSERT_GE(driver, 0);
    static const std::regex call(R"(^(\w+)\(frame\);$)");
    int statements = 0;
    for (const auto& raw : tu.bodies[driver]) {
        const std::string s = trim(raw);
        if (s.empty() || s.rfind("//", 0) == 0) continue;
        if (s.find_first_not_of("{}); ") == std::string::npos) continue;
        ++statements;
        if (s == "MovementFrame frame;" || s == "frame.deltaTime = deltaTime;") continue;
        std::smatch m;
        EXPECT_TRUE(std::regex_match(s, m, call) && tu.indexOf(m[1].str()) >= 0)
            << "updatePlayerMovement() has grown a statement that is not a phase call:\n"
            << "  " << s;
    }
    EXPECT_LT(statements, 20)
        << "updatePlayerMovement() is growing back; it had " << statements << " statements.";
}

TEST(MovementOrder, ComposedMovementOrderMatchesThePreG06Golden) {
    const auto composed = composedOrder();
    const auto golden = goldenOrder();
    ASSERT_FALSE(golden.empty());

    const size_t n = std::min(composed.size(), golden.size());
    for (size_t i = 0; i < n; ++i) {
        if (composed[i] == golden[i]) continue;
        std::string ctx;
        for (size_t k = (i >= 3 ? i - 3 : 0); k < std::min(i + 4, n); ++k) {
            ctx += "\n    " + std::string(k == i ? "->  " : "    ") + "got: " + composed[k];
            ctx += "\n        " + std::string(k == i ? "    " : "") + "exp: " + golden[k];
        }
        FAIL() << "movement order diverges at statement " << (i + 1) << ":" << ctx;
    }
    EXPECT_EQ(composed.size(), golden.size())
        << "movement statement count changed: " << golden.size() << " -> " << composed.size();
}
