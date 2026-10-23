// tools/model_viewer/model_viewer_fx_harness.h -- B1138 (#458): the fable-side
// spell FX rig. `model_viewer --spell-test` drives the production
// EQ::SpellVisualFX -> ParticleManager -> SimulationWorker chain with the
// client's own cast sequence (src/client/bridge/spell_cast_fx.cpp, the body
// IrrlichtBridge runs), so a fable FX gate needs no eqemu lane; only ORACLE
// legs (the official client) do.
//
// Scripted mode (`--plan <tsv> --frames-dir <dir> --fps 30 --headless
// --seconds N`) steps a fixed dt = 1/fps with no wall clock and writes
// frame_%05d.png, timing.tsv (BeginCast/Action rows) and scene.tsv (each
// entity's screen box). It is a TOOL writing frames, not the client's
// `--dump-frames-dir`. `--camera b1118` is the lane-5 tour framing
// (review/B1118/README.md): 1280x800, caster feet at (640,605), 235 px tall.
// refactor/B1138/README.md is the report; review/B1138/scripts/ the consumers.

#ifndef WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H
#define WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H

#include <cstdint>
#include <string>
#include <vector>

#include "client/bridge/cast_glow_route.h"

namespace EQ { struct SpellData; }

// One row of a --plan file: t_begin_s, spell_id, target (self|other),
// distance_units, cast_ms (optional; default = the spell's spells_us.txt
// field 13 cast time via SpellData).
struct HarnessCast {
    double tBegin = 0.0;
    uint32_t spellId = 0;
    bool self = false;
    float distance = 0.0f;
    int castMsOverride = -1;
};

struct HarnessOptions {
    std::string planPath;
    std::string framesDir;
    std::string cameraPreset;          // "" (the interactive framing) or "b1118"
    std::string backdropPath;          // optional image drawn behind the scene
    std::string eqe1Path = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";
    std::string eqe1Dir = "/mnt/projects/EverQuest_ETC1";  // the model packs (T207 resolver)
    int fps = 30;
    double seconds = 0.0;
    bool headless = false;
    uint32_t seed = 1;                 // particle RNG seed in scripted mode
    int width = 800;
    int height = 600;
    uint16_t casterRace = 1;
    uint8_t casterGender = 0;
    float casterSize = 0.0f;           // server size; 0 = the race scale alone
    uint16_t targetRace = 1;
    uint8_t targetGender = 0;
    float targetSize = 0.0f;
    float interactiveDistance = 5.0f;  // F6 TARGETED mode's target distance
    float casterPx = 235.0f;           // the preset's caster height in px (--caster-px)
    bool ground = true;                // a preset's depth-only ground plane (--no-ground)
    bool casterGiven = false;          // --caster / --target win over a preset
    bool targetGiven = false;
};
extern HarnessOptions harnessOpts;

// The Y rotation that turns a race model to face +Z, toward a face-on preset
// camera: the models' rest facing at rotation 0 is +X (seen on the first
// harness frames: a profile view from a +Z camera).
constexpr float kHarnessFaceOnRotY = -90.0f;

// Parses a plan; false (with `err`) on a malformed row. '#' lines are comments.
bool parseHarnessPlan(const std::string& path, std::vector<HarnessCast>& out, std::string& err);

// The --camera preset's size and models (b1118: 1280x800, the lane-5
// Testcaster = dark elf female, the `#spawn` target = human male).
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
void harnessBeginCast(const EQ::SpellData& spell, bool self, uint32_t castMs,
                      eqt::bridge::CastGlowRoute::Clock::time_point now);
void harnessAction(const EQ::SpellData& spell, bool self,
                   eqt::bridge::CastGlowRoute::Clock::time_point now);
void harnessInterrupt(const EQ::SpellData& spell);

// Shows / hides and places the target for a cast: hidden for self, else at
// `distance` world units to the caster's screen right (0 = on the caster, as
// `#spawn` stands an NPC on the GM).
void placeHarnessTarget(bool self, float distance);

// The spell-test camera (the preset's fitted pose, else the default framing).
void setupSpellTestCamera();

// The scripted run; returns the process exit code (0 ok, 2 bad plan/options,
// 3 could not start the FX chain, 4 a frame failed to write).
int runScriptedHarness();

#endif  // WILLEQ_TOOLS_MODEL_VIEWER_FX_HARNESS_H
