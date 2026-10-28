// tools/model_viewer/model_viewer_fx_harness.h -- B1138 (#458): the fable-side
// spell FX rig. `model_viewer --spell-test` drives the production
// EQ::SpellVisualFX -> ParticleManager -> SimulationWorker chain with the
// client's own cast sequence (src/client/bridge/spell_cast_fx.cpp, the body
// IrrlichtBridge runs), so a fable FX gate needs no eqemu lane; only ORACLE
// legs (the official client) do.
//
// Scripted mode (`--plan <tsv> --frames-dir <dir> --fps 30 --headless
// --seconds N`) steps a fixed dt = 1/sim-hz (default 1/fps) with no wall
// clock, draws every (sim-hz / fps)-th step and writes
// frame_%05d.png, timing.tsv (BeginCast/Action rows), scene.tsv (each
// entity's screen box) and slots.tsv (B1144: the spellsnew.eff slots each
// packet draws at the cast's gate level). It is a TOOL writing frames, not the client's
// `--dump-frames-dir`. `--camera b1118` is the lane-5 tour framing
// (review/B1118/README.md): 1280x800, caster feet at (640,605), 228 px tall
// (leg B's own Testcaster, B1139), with the lane rig's measured parity factors
// as defaults (tools/model_viewer/README.md). refactor/B1138/README.md and
// refactor/B1139/README.md are the reports; review/B1139/scripts/ the consumers.

#ifndef WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H
#define WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H

#include <cstdint>
#include <string>
#include <vector>

#include "client/bridge/cast_glow_route.h"
#include "client/spell/spell_fx_options.h"
#include "fx_capture_pose.h"
#include "fx_target_anchor.h"

namespace EQ { struct SpellData; }

// One row of a --plan file: t_begin_s, spell_id, target (self|other),
// distance_units, cast_ms (optional; default = the spell's spells_us.txt
// field 13 cast time via SpellData; '-' = default), caster_level (optional,
// B1144: the caster's spellsnew.eff slot-gate level; '-' = --caster-level),
// packets (optional, B1144: `begincast` = OP_BeginCast then OP_Action, the
// gem cast; `action` = OP_Action alone at t_begin_s + cast_ms, the /disc path
// B1134 captured: OP_CastSpell slot 10 -> OP_Action, no OP_BeginCast).
struct HarnessCast {
    double tBegin = 0.0;
    uint32_t spellId = 0;
    bool self = false;
    float distance = 0.0f;
    int castMsOverride = -1;
    int casterLevel = -1;     // -1 = harnessOpts.casterLevel
    bool beginCast = true;    // false = the action-only (/disc) packets
};

struct HarnessOptions {
    std::string planPath;
    std::string framesDir;
    std::string cameraPreset;          // "" (the interactive framing) or "b1118", or B1155's "b1130" / "b1130-self"
    std::string backdropPath;          // optional image drawn behind the scene
    std::string eqe1Path = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";
    std::string eqe1Dir = "/mnt/projects/EverQuest_ETC1";  // the model packs (T207 resolver)
    int fps = 30;
    double seconds = 0.0;
    bool headless = false;
    uint32_t seed = 1;                 // --fx-seed (alias --seed): particle RNG seed, fixed; 0 = random_device
    // B1144 (#464): the caster's spellsnew.eff slot-gate level (--caster-level).
    // 60 = the `#level 60` every oracle capture behind the measured rows ran at.
    int casterLevel = 60;
    int width = 800;
    int height = 600;
    uint16_t casterRace = 1;
    uint8_t casterGender = 0;
    float casterSize = 0.0f;           // server size; <= 0 = the race/gender default (actor_scale.h)
    uint16_t targetRace = 1;
    uint8_t targetGender = 0;
    float targetSize = 0.0f;
    float interactiveDistance = 5.0f;  // F6 TARGETED mode's target distance
    float casterPx = 235.0f;           // the preset's caster height in px (--caster-px)
    bool casterPxGiven = false;
    // Simulation steps per second (--sim-hz, a multiple of --fps; 0 = fps).
    // The b1118 preset defaults to 60: the lane client runs 60 fps
    // ("FPS: 60" on its frames) while x11grab writes 30 (B1139).
    int simHz = 0;
    bool ground = true;                // a preset's depth-only ground plane (--no-ground)
    // B1177 (#496): the b1130 target solve's iteration cap
    // (--target-fit-iterations, default 24 = B1166's loop). A cap the solve
    // cannot converge in (1: only the start point is measured) makes the
    // preset not solve -- the deterministic failure the exit-2 test runs.
    int targetFitIterations = 24;
    bool casterGiven = false;          // --caster / --target win over a preset
    bool targetGiven = false;
    // B1139 (#459) lane-parity factors, each measured in refactor/B1139/README.md.
    // Opaque screen rectangles drawn over the particles, as the lane client's
    // UI windows are (x0,y0,x1,y1 px, --ui-occluder, repeatable). None by
    // default: since B1142 the lane rig keeps the FX field unoccluded.
    struct Rect { int x0, y0, x1, y1; };
    std::vector<Rect> uiOccluders;
    bool uiOccludersGiven = false;     // --ui-occluder / --no-ui-occluder win over a preset
    // Seconds after OP_Action at which the target plays its death animation
    // (--target-death; < 0 = it stands; the B1132 leg-B pcap: OP_Death 0.000 s
    // after OP_Action) and at which it is despawned (--target-despawn; the pcap's
    // OP_DeleteSpawn 1.027 s after, which a B1133 client obeys).
    double targetDeathAfterAction = -1.0;
    double targetDespawnAfterAction = -1.0;
    // B1179 (#498): seconds after OP_Action at which the caster leaves the scene
    // (--caster-despawn; < 0 = it stays; the counterpart of --target-despawn for a
    // caster that zones out, e.g. the r40 602 group teleport whose caster's
    // OP_DeleteSpawn lands 0.36..0.40 s after OP_Action in review/B1130/oracle/r84).
    // It removes the caster's attached spell emitters (onSpawnDeleted, a plain
    // spawn) and hides the model, so a landing that lives on the caster ends when
    // the oracle's does instead of running the emitters' full file lifetime.
    double casterDespawnAfterAction = -1.0;
    // Frames between the worker's compute and the draw of its buffer
    // (--worker-latency): the client draws the output of the input it posted
    // the frame before (renderer_frame.cpp: applySimulationResults, then
    // postSimulationInput), the synchronous harness by default the same frame.
    int workerLatency = 0;
    // The caster's Spawn_Struct appearance (--caster-appearance
    // face,texture:e0,..,e8 -- the TextureProfile at offset 197: head, chest,
    // arms, wrist, hands, legs, feet, primary, secondary). The b1118 preset
    // defaults to Testcaster's own (B1132 leg-B pcap, OP_ZoneEntry S->C):
    // chest material 10 (the robe) and secondary IT72 (the orb in her left hand).
    bool casterAppearanceSet = false;
    uint8_t casterFace = 0;
    uint8_t casterTexture = 0;
    uint32_t casterEquipment[9] = {0};
    // B1165 (#484): the Options > Particles > Spell page + ShowSpellEffects
    // the run draws with. Default = the oracle capture's ini (1.0 / 1.0 /
    // 2.0 / 24 / 1, review/B1159/worker/oracle/m_b1159_rig/eqclient-after.ini);
    // --spell-particle-density/-opacity/-nearclip/-filter and
    // --show-spell-effects change it, and a run that differs from the capture
    // refuses to start (exit 65) unless --particle-options is passed.
    EQ::SpellFxOptions spellFx;
    bool particleOptions = false;
};

// B1165: the exit code of a run whose particle options differ from the
// oracle capture's without --particle-options (sysexits EX_DATAERR).
constexpr int kHarnessExitParticleOptions = 65;
// B1155 / B1177: the exit code of a --camera preset that does not solve to its
// recorded geometry (setupSpellTestCamera false; refactor/B1155/README.md "The
// solve"). It exits through the normal teardown, never a signal (B1177).
constexpr int kHarnessExitPresetDidNotSolve = fxharness::kExitPresetDidNotSolve;
extern HarnessOptions harnessOpts;

// The Y rotation that turns a race model to face +Z, toward a face-on preset
// camera: the models' rest facing at rotation 0 is +X (seen on the first
// harness frames: a profile view from a +Z camera).
constexpr float kHarnessFaceOnRotY = -90.0f;

// B1167 (#486): the preset's capture pose is fxharness::poseForPreset in
// fx_capture_pose.h (non-null for all three presets b1118, b1130, b1130-self;
// null for the interactive framing, which keeps kHarnessFaceOnRotY).

// Parses a plan; false (with `err`) on a malformed row. '#' lines are comments.
bool parseHarnessPlan(const std::string& path, std::vector<HarnessCast>& out, std::string& err);

// The --camera preset's size and models (b1118: 1280x800, the lane-5
// Testcaster = dark elf female, the `#spawn` target = human male, the Tip of
// the Day window as a UI occluder, Testcaster's robe and orb).
void applyHarnessPresetDefaults();

// The renderer's texture cache: willeq.eqe1 plus the name -> archive resolver
// for the race models (call before the models are created).
bool initHarnessTextureCache();
void uploadHarnessTextures();
// Tears the chain down before the device goes (texture cache last).
void shutdownHarness();

// The particle manager,
// the spell particle pool and the worker -- the renderer's own init order
// (renderer_init.cpp). False when the pack or the atlases are missing.
bool initHarnessParticles();

// One worker frame: the ParticleManager's queued commands and last frame's
// entity positions in, the render buffer out (renderer_sim_bridge.cpp's
// particle half, synchronous).
void stepSpellParticles(float dt, uint32_t seed);

// Draws the worker's render buffer after smgr->drawAll().
void renderSpellParticles();

// The client's three SpellCastVisual* payloads for the local player's cast,
// through eqt::bridge::ApplySpellCast*Fx. Action publishes the cast-animation
// packet (effect_flag 0) and then the success repeat (effect_flag 4), the pair
// the caster receives 4 ms apart (review/B1132 pcap).
void harnessBeginCast(const EQ::SpellData& spell, bool self, uint32_t castMs, int casterLevel,
                      eqt::bridge::CastGlowRoute::Clock::time_point now);
void harnessAction(const EQ::SpellData& spell, bool self, int casterLevel,
                   eqt::bridge::CastGlowRoute::Clock::time_point now);
void harnessInterrupt(const EQ::SpellData& spell);

// Shows / hides and places the target for a cast: hidden for self, else at
// `distance` world units to the caster's screen right (0 = on the caster, as
// `#spawn` stands an NPC on the GM).
void placeHarnessTarget(bool self, float distance);

// The spell-test camera (the preset's fitted pose, else the default framing).
bool setupSpellTestCamera();

// The scripted run; returns the process exit code (0 ok, 2 bad plan/options,
// 3 could not start the FX chain, 4 a frame failed to write).
int runScriptedHarness();

#endif  // WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H
