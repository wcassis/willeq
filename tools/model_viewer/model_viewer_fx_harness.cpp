// tools/model_viewer/model_viewer_fx_harness.cpp -- B1138 (#458): the fable-side
// spell FX rig behind `model_viewer --spell-test`. See the header.

#include "model_viewer_fx_harness.h"
#include "model_viewer_state.h"
#include "fx_target_anchor.h"
#include "fx_target_fit.h"  // B1172 FIX leg: the b1130 target fit split out (pure move)

#include "client/bridge/spell_cast_fx.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/data_paths.h"
#include "client/graphics/first_person_camera.h"
#include "client/graphics/corpse_fade.h"
#include "client/graphics/spell_fx_bone_anchor.h"
#include "client/spell/spell_fx_attach_scale.h"
#include "client/graphics/spell_cast_animation.h"
#include "client/spell/spell_action_anim.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/spell_effects_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <unordered_map>

HarnessOptions harnessOpts;

namespace {

using Clock = eqt::bridge::CastGlowRoute::Clock;

std::unique_ptr<ConstrainedTextureCache> fxTextureCache;
EQE1Loader fxPack;
EQE1TextureIndex fxIndex;
std::vector<std::unique_ptr<EQE1Loader>> fxArchives;
EQT::Graphics::SimulationWorker fxWorker;
// Two outputs so --worker-latency 1 can draw the one computed a frame earlier.
EQT::Graphics::SimulationOutput fxOutputs[2];
int fxComputed = 0;  // the index computeOnce() last wrote
float fxSpellDensity = 1.0f;  // B1165: the Spell page density handed to the worker
bool fxTargetGone = false;  // --target-despawn: the target left the scene
std::unordered_map<uint16_t, glm::vec3> fxPendingPositions;
std::unordered_map<uint16_t, glm::vec3> fxPendingDirections;
eqt::bridge::CastGlowRoute fxRoute;
video::ITexture* fxBackdrop = nullptr;
scene::IMeshSceneNode* fxGroundNode = nullptr;  // addGroundPlane(); B1169 draws it first

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); } else if (c != '\r') { cur += c; }
    }
    out.push_back(cur);
    return out;
}

// The pinhole Irrlicht's buildProjectionMatrixPerspectiveFovLH draws: the
// pixel a world point lands on for a camera at `eye` looking at `look`.
struct Pinhole {
    core::vector3df eye, right, up, fwd;
    float fovY, aspect, w, h;
    core::vector2df project(const core::vector3df& p) const {
        const core::vector3df d = p - eye;
        const float zc = d.dotProduct(fwd);
        const float t = std::tan(fovY * 0.5f);
        return {w * 0.5f + d.dotProduct(right) / zc * (w * 0.5f) / (t * aspect),
                h * 0.5f - d.dotProduct(up) / zc * (h * 0.5f) / t};
    }
};

Pinhole makePinhole(const core::vector3df& eye, const core::vector3df& look, float fovY, float aspect,
                    float w, float h) {
    Pinhole ph;
    ph.eye = eye;
    ph.fwd = (look - eye).normalize();
    ph.right = core::vector3df(0, 1, 0).crossProduct(ph.fwd).normalize();  // left-handed
    ph.up = ph.fwd.crossProduct(ph.right).normalize();
    ph.fovY = fovY;
    ph.aspect = aspect;
    ph.w = w;
    ph.h = h;
    return ph;
}

// The caster's facing in the scripted presets (kHarnessFaceOnRotY): +Z, so
// the camera of a face-on preset sits on +Z looking back along -Z.
const core::vector3df kCasterFacing(0.0f, 0.0f, 1.0f);

core::vector3df targetRightOfCaster() {
    // Screen right for a camera looking along -kCasterFacing (left-handed).
    return core::vector3df(0, 1, 0).crossProduct(-kCasterFacing).normalize();
}

bool fitFaceOnCamera(const PresetGeometry& g, const char* name) {
    const float w = static_cast<float>(harnessOpts.width), h = static_cast<float>(harnessOpts.height);
    const float aspect = w / h;
    const float fovY = firstPersonVerticalFovRadians(aspect);
    // The client's createEntity: force the animated pose's bounding box
    // (the bind pose's is not the standing figure), then world space.
    animatedNode->forceAnimationUpdate();
    animatedNode->updateAbsolutePosition();
    core::aabbox3df box = animatedNode->getBoundingBox();
    animatedNode->getAbsoluteTransformation().transformBoxEx(box);
    const core::vector3df origin = animatedNode->getAbsolutePosition();
    const core::vector3df feet(origin.X, box.MinEdge.Y, origin.Z);
    const core::vector3df head(origin.X, box.MaxEdge.Y, origin.Z);
    const float pitch = g.pitchDeg * core::DEGTORAD;
    auto pose = [&](float dist, float lookH, core::vector3df& eye, core::vector3df& look) {
        look = feet + core::vector3df(0, lookH, 0);
        eye = look + kCasterFacing * (dist * std::cos(pitch)) + core::vector3df(0, dist * std::sin(pitch), 0);
    };
    float dist = 30.0f, lookH = 5.0f;
    for (int outer = 0; outer < 40; ++outer) {
        // look-at height for the feet row (feet y grows with lookH)
        float lo = -50.0f, hi = 50.0f;
        for (int i = 0; i < 60; ++i) {
            lookH = 0.5f * (lo + hi);
            core::vector3df eye, look;
            pose(dist, lookH, eye, look);
            const float fy = makePinhole(eye, look, fovY, aspect, w, h).project(feet).Y;
            (fy < g.feetY ? lo : hi) = lookH;
        }
        // distance for the caster's pixel height (shrinks with distance)
        core::vector3df eye, look;
        pose(dist, lookH, eye, look);
        const Pinhole ph = makePinhole(eye, look, fovY, aspect, w, h);
        const float px = ph.project(feet).Y - ph.project(head).Y;
        dist *= px / g.casterPx;
    }
    // The pinhole on the pose bounding box is only the first guess: B1118's
    // 235 px is the RENDERED figure, head top to feet, and the idle pose's
    // feet sit below its bounding box. So render the caster alone and correct
    // on her silhouette (any pixel off the clear colour) until both facts hold.
    //
    // The surfaceless EGL surface can be another size than the REQUESTED
    // frame (the ThinkPad's virtual display gave 3840x2160 for a 1280x800
    // request): the camera's aspect is the requested image's aspect, so the
    // surface is a stretched canvas -- requested pixel (x, y) is surface
    // pixel (x*Sx, y*Sy), where S = surface/requested. The fit therefore
    // measures the surface screenshot against the SCALED recorded facts, and
    // the written frames are downscaled to the requested size (the loop in
    // runScriptedHarness), where the recorded pixels hold.
    core::vector3df eye, look;
    pose(dist, lookH, eye, look);
    camera = smgr->addCameraSceneNode(nullptr, eye, look);
    camera->setAspectRatio(aspect);
    camera->setFOV(fovY);
    camera->setNearValue(fpTunable(FpTunable::NearPlaneUnits));
    const bool targetWasVisible = targetAnimatedNode && targetAnimatedNode->isVisible();
    if (targetAnimatedNode) targetAnimatedNode->setVisible(false);
    const video::SColor clear(255, 50, 50, 80);
    float Sx = 1.0f, Sy = 1.0f;  // surface / requested
    int top = -1, bottom = -1;
    float cx = g.feetX;
    for (int iter = 0; iter < 8; ++iter) {
        pose(dist, lookH, eye, look);
        camera->setPosition(eye);
        camera->setTarget(look);
        camera->updateAbsolutePosition();
        driver->beginScene(true, true, clear);
        smgr->drawAll();
        driver->endScene();
        video::IImage* img = driver->createScreenShot();
        if (!img) break;
        if (iter == 0) {
            const auto dim0 = img->getDimension();
            Sx = static_cast<float>(dim0.Width) / w;
            Sy = static_cast<float>(dim0.Height) / h;
            std::printf("[fx-harness] %s: surface %ux%u for a %dx%d frame (scale %.3fx%.3f); "
                        "the fit runs in surface space\n",
                        name, dim0.Width, dim0.Height, static_cast<int>(w), static_cast<int>(h), Sx, Sy);
        }
        const float feetYs = g.feetY * Sy, casterPxS = g.casterPx * Sy;  // the facts, in surface px
        top = bottom = -1;
        double sx = 0;
        int nx = 0;
        const auto dim = img->getDimension();
        for (u32 y = 0; y < dim.Height; ++y)
            for (u32 x = 0; x < dim.Width; ++x) {
                const video::SColor c = img->getPixel(x, y);
                if (std::abs(int(c.getRed()) - 50) + std::abs(int(c.getGreen()) - 50) +
                        std::abs(int(c.getBlue()) - 80) <= 12)
                    continue;
                if (top < 0) top = static_cast<int>(y);
                bottom = static_cast<int>(y);
                sx += x;
                ++nx;
            }
        img->drop();
        if (top < 0) break;
        cx = static_cast<float>(sx / nx);
        std::printf("[fx-harness]   fit %d: dist %.3f look %.3f -> rows %d..%d centroid %.1f (surface)\n", iter, dist,
                    lookH, top, bottom, cx);
        const float px = static_cast<float>(bottom - top + 1);
        const float unitsPerPx = dist * std::tan(fovY * 0.5f) / (h * 0.5f) / Sy;  // world units per SURFACE px
        if (std::fabs(px - casterPxS) < 0.5f * Sy && std::fabs(bottom - feetYs) < 0.5f * Sy) break;
        // the eye moves with the look point, so raising both moves the figure DOWN
        lookH += (feetYs - static_cast<float>(bottom)) * unitsPerPx;
        dist *= px / casterPxS;
    }
    if (targetAnimatedNode) targetAnimatedNode->setVisible(targetWasVisible);
    std::printf("[fx-harness] camera %s: dist %.3f look %.3f above the pose-box feet, pitch %.1f, "
                "pose-box height %.3f -> rendered caster rows %.0f..%.0f (%.0f px, feet row %.0f), "
                "column centroid %.0f (requested frame: %d..%d, %d px, feet %d at column %.0f)\n",
                name, dist, lookH, g.pitchDeg, head.Y - feet.Y, top / Sy, bottom / Sy, (bottom - top + 1) / Sy,
                bottom / Sy, cx / Sx, static_cast<int>(top / Sy), static_cast<int>(bottom / Sy),
                static_cast<int>((bottom - top + 1) / Sy), static_cast<int>(bottom / Sy), cx / Sx);
    std::fflush(stdout);
    return top >= 0;  // the fit rendered the caster (the caller's test asserts the pixels)
}

// scene.tsv: each visible entity's screen box (its pose bounding box's eight
// corners projected through the frame's camera), so a consumer -- the
// FxHarness ctest, an ROI script -- knows where the caster and the target are
// drawn without re-deriving the projection.
//
// The projection is the harness's own pinhole in the REQUESTED frame
// (harnessOpts.width x harnessOpts.height), never the driver's: the
// surfaceless EGL surface can be another size (the ThinkPad's 3840x2160), and
// the written frames are downscaled to the requested one (runScriptedHarness).
void writeSceneBoxes(const std::string& path) {
    std::ofstream f(path);
    f << "entity\tid\tx0\ty0\tx1\ty1\n";
    const float w = static_cast<float>(harnessOpts.width), h = static_cast<float>(harnessOpts.height);
    const Pinhole ph = makePinhole(camera->getPosition(), camera->getTarget(), camera->getFOV(),
                                   camera->getAspectRatio(), w, h);
    const std::pair<const char*, EQAnimatedMeshSceneNode*> nodes[] = {{"caster", animatedNode},
                                                                      {"target", targetAnimatedNode}};
    for (const auto& [name, node] : nodes) {
        if (!node || !node->isVisible()) continue;
        node->forceAnimationUpdate();
        core::aabbox3df box = node->getBoundingBox();
        node->getAbsoluteTransformation().transformBoxEx(box);
        core::vector3df c[8];
        box.getEdges(c);
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
        for (const auto& p : c) {
            const core::vector2df s = ph.project(p);
            x0 = std::min(x0, static_cast<int>(s.X)); y0 = std::min(y0, static_cast<int>(s.Y));
            x1 = std::max(x1, static_cast<int>(s.X)); y1 = std::max(y1, static_cast<int>(s.Y));
        }
        f << name << '\t' << (node == animatedNode ? casterEntityId : targetEntityId) << '\t' << x0 << '\t'
          << y0 << '\t' << x1 << '\t' << y1 << '\n';
    }
}

const EQ::SpellData* spellOrNull(uint32_t id) {
    return spellDatabase ? spellDatabase->getSpell(id) : nullptr;
}

// B1174 (#493): FUN_0047f4af's inputs for the harness caster's OP_Action
// (spell_action_anim.h): the plan's spell id, its record (the plan only
// holds spells the db has), the record's field 98 target type, a standing
// source (the harness caster never sits, loots or feigns), field 123, and a
// class that is no Bard with instrument_mod 0.
EQ::ActionCastAnimInputs harnessActionInputs(uint32_t spellId, const EQ::SpellData& sp) {
    EQ::ActionCastAnimInputs in;
    in.spellId = spellId;
    in.recordExists = true;
    in.targetType = static_cast<uint8_t>(sp.target_type);
    in.sourceStandState = EQ::kStandStateStanding;
    in.spellAffectIndex = sp.spell_affect_index;
    in.sourceClass = 0;
    in.instrumentMod = 0;
    return in;
}

// The harness must write the REQUESTED frame (harnessOpts.width x height), and
// the surfaceless EGL surface can be another size (the ThinkPad's virtual
// display gave 3840x2160 for a 1280x800 request: 3x horizontally, 2.7x
// vertically). The camera's aspect is the requested image's aspect, so the
// surface is a stretched canvas: requested pixel (x, y) is surface pixel
// (x*Sx, y*Sy), exactly the affine this undoes (NDC is viewport-size
// independent). Bilinear, axis-wise.
video::IImage* scaleImageTo(video::IVideoDriver* drv, video::IImage* src, u32 tw, u32 th) {
    const auto sd = src->getDimension();
    video::IImage* dst = drv->createImage(src->getColorFormat(), core::dimension2d<u32>(tw, th));
    for (u32 y = 0; y < th; ++y) {
        const float fy = (float)(y + 0) * (float)sd.Height / (float)th - 0.5f;
        const int y0 = std::max(0, std::min<int>(sd.Height - 1, (int)std::floor(fy)));
        const int y1 = std::max(0, std::min<int>(sd.Height - 1, y0 + 1));
        const float ty = std::max(0.0f, std::min(1.0f, fy - std::floor(fy)));
        for (u32 x = 0; x < tw; ++x) {
            const float fx = (float)x * (float)sd.Width / (float)tw - 0.5f;
            const int x0 = std::max(0, std::min<int>(sd.Width - 1, (int)std::floor(fx)));
            const int x1 = std::max(0, std::min<int>(sd.Width - 1, x0 + 1));
            const float tx = std::max(0.0f, std::min(1.0f, fx - std::floor(fx)));
            const video::SColor c00 = src->getPixel(x0, y0), c10 = src->getPixel(x1, y0);
            const video::SColor c01 = src->getPixel(x0, y1), c11 = src->getPixel(x1, y1);
            auto mix = [](int a, int b, float t) { return (int)(a * (1.0f - t) + b * t + 0.5f); };
            dst->setPixel(x, y, video::SColor(255,
                                              mix(mix(c00.getRed(), c10.getRed(), tx), mix(c01.getRed(), c11.getRed(), tx), ty),
                                              mix(mix(c00.getGreen(), c10.getGreen(), tx), mix(c01.getGreen(), c11.getGreen(), tx), ty),
                                              mix(mix(c00.getBlue(), c10.getBlue(), tx), mix(c01.getBlue(), c11.getBlue(), tx), ty)));
        }
    }
    return dst;
}

}  // namespace

bool parseHarnessPlan(const std::string& path, std::vector<HarnessCast>& out, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open plan " + path; return false; }
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.empty() || line[0] == '#') continue;
        const auto c = splitTabs(line);
        if (c.size() < 4 || c.size() > 7) {
            err = path + ":" + std::to_string(n) +
                  ": want t_begin_s, spell_id, self|other, distance_units[, cast_ms[, caster_level[, begincast|action]]]";
            return false;
        }
        if (c[0] == "t_begin_s") continue;  // header row
        HarnessCast hc;
        try {
            hc.tBegin = std::stod(c[0]);
            hc.spellId = static_cast<uint32_t>(std::stoul(c[1]));
            if (c[2] != "self" && c[2] != "other") throw std::invalid_argument("target");
            hc.self = c[2] == "self";
            hc.distance = std::stof(c[3]);
            if (c.size() >= 5 && !c[4].empty() && c[4] != "-") hc.castMsOverride = std::stoi(c[4]);
            if (c.size() >= 6 && !c[5].empty() && c[5] != "-") hc.casterLevel = std::stoi(c[5]);
            if (c.size() >= 7) {
                if (c[6] != "begincast" && c[6] != "action") throw std::invalid_argument("packets");
                hc.beginCast = c[6] == "begincast";
            }
        } catch (const std::exception&) {
            err = path + ":" + std::to_string(n) + ": unparseable row '" + line + "'";
            return false;
        }
        out.push_back(hc);
    }
    if (out.empty()) { err = path + ": no casts"; return false; }
    return true;
}

void applyHarnessPresetDefaults() {
    if (harnessOpts.cameraPreset != "b1118" && harnessOpts.cameraPreset != "b1130" &&
        harnessOpts.cameraPreset != "b1130-self")
        return;
    const bool b1130 = harnessOpts.cameraPreset == "b1130" || harnessOpts.cameraPreset == "b1130-self";
    harnessOpts.width = 1280;
    harnessOpts.height = 800;
    // All three presets share the b1118 char-67 caster: char 67 Testcaster
    // (lane 5): race 6 dark elf, gender 1 (fable.log
    // "Updating appearance for entity 258 (Testcaster) to race 6 gender 1").
    // B1172: size 0 -- her S->C OP_ZoneEntry Spawn_Struct carries size 0.000
    // in the oracle captures (r51, B1118 oracle_druid) and in fable's lane
    // legs (B1118 fable_A, B1142 fable_N93), and no AT_Size names her; the
    // client then draws its race/gender default 5.0 x 0.2 = 1.0
    // (actor_scale.h). The 6.0 here before was fable's own zone_entry
    // default, not a wire value.
    if (!harnessOpts.casterGiven) {
        harnessOpts.casterRace = 6;
        harnessOpts.casterGender = 1;
        harnessOpts.casterSize = 0.0f;
        // B1139: her Spawn_Struct appearance in that leg's OP_ZoneEntry (S->C):
        // face 3, TextureProfile {0, 10, 1, 23, 1, 1, 1, 0, 72} -- the red robe
        // (chest 10) and IT72, the orb her left hand holds. equip_chest2 reads
        // 255 there; the client passes it through as appearance.texture.
        harnessOpts.casterAppearanceSet = true;
        harnessOpts.casterFace = 3;
        harnessOpts.casterTexture = 255;
        const uint32_t equip[9] = {0, 10, 1, 23, 1, 1, 1, 0, 72};
        std::copy(equip, equip + 9, harnessOpts.casterEquipment);
    }
    if (!harnessOpts.targetGiven) {
        if (b1130) {
            // b1130 / b1130-self: B1130's target is `#spawn TrollF 1 1 0 30000`.
            // eqemu's #spawn arg order is [name][race][level][texture][health]
            // [gender]... (command.cpp :229; npc.cpp SpawnNPC), so that command
            // is race=1 (HUMAN), level=1, texture=0, hp=30000 with the GENDER
            // ARG ABSENT -- filled by Mob::GetDefaultGender(1) = MALE (the
            // PC-race default, mob.cpp :4033). The "TrollF" name is only a
            // label: the oracle spawned the SAME level-1 human male as b1118's
            // target (the settle still r51/21_stun_settle shows a clothed,
            // standing human male), NOT a race-9 troll -- r51's own OP_NewSpawn
            // reads race 1, gender 0, size 6.000 (refactor/B1166/README.md
            // sec. 1). b1130 renders that human male and lets the solver find
            // its depth from the two feet pixels (288 px in the oracle; 238 px in
            // FABLE at 1.0 (B1166), 285 px at the client's actor scale 1.2, B1172).
            harnessOpts.targetRace = 1;
            harnessOpts.targetGender = 0;
            harnessOpts.targetSize = 6.0f;
        } else {
            // b1118: `#spawn TrollF`: eqemu's default #spawn is a level-1
            // human male (the leg's chat: "Race | Human (1)", "Gender | Male
            // (0)").
            harnessOpts.targetRace = 1;
            harnessOpts.targetGender = 0;
            harnessOpts.targetSize = 6.0f;
        }
    }
    // B1142 (#462): no UI occluder by default. B1139 drew the fable Tip of the
    // Day window (920,243..1280,543) here because every lane-5 leg up to B1127
    // left it open over the particles; the lane rig now starts the client
    // with [Tips] ShowTOTD=0 (review/B1142/scripts/fable_leg_remote.sh), so
    // the FX field is unoccluded on lane 5 as on the oracle (whose rig closes
    // its window with a click before the first cast). `--ui-occluder
    // 920,243,1280,543` reproduces a pre-B1142 capture.
    // B1139: the lane client simulates at its 60 fps and the grab keeps every
    // 2nd frame (B1138 measured +2.6 % landing coverage for it), and in leg B
    // Testcaster stands 228 px head top to feet (rows 377..605 of that leg's
    // tail frame, review/B1138/backdrop/), not B1118's 235.
    if (harnessOpts.simHz <= 0) harnessOpts.simHz = 60;
    if (!harnessOpts.casterPxGiven)
        harnessOpts.casterPx =
            (harnessOpts.cameraPreset == "b1130" || harnessOpts.cameraPreset == "b1130-self") ? 209.0f : 228.0f;
}

bool initHarnessTextureCache() {
    // The renderer's ConstrainedTextureCache: willeq.eqe1 (the two particle
    // atlases, ParticleManager::loadParticleAtlas -- without it the manager
    // logs FATAL and exits) plus T207's name -> archive resolver over the
    // sibling packs for the race models' textures (eqe1_early_resolution.cpp;
    // the same shape as tools/hwcaps/zone_bench_loader.cpp).
    fxTextureCache = std::make_unique<ConstrainedTextureCache>(
        ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT));
    fxTextureCache->setDriver(driver);
    if (!fxPack.open(harnessOpts.eqe1Path)) {
        std::fprintf(stderr, "[fx-harness] cannot open %s (--eqe1)\n", harnessOpts.eqe1Path.c_str());
        return false;
    }
    fxTextureCache->addEQE1Loader(&fxPack);
    const std::string dir = harnessOpts.eqe1Dir;
    if (fxIndex.load(eqt::ResolveDataPath("data/eqe1_texture_index.json"))) {
        fxTextureCache->setEQE1ArchiveResolver([dir](const std::string& name) -> const EQE1Loader* {
            EQE1TextureIndex::Resolution r;
            if (!fxIndex.resolve(name, {}, r)) return nullptr;
            const std::string path = dir + "/" + r.archive + ".eqe1";
            for (const auto& l : fxArchives)
                if (l->getFilePath() == path) return l.get();
            auto loader = std::make_unique<EQE1Loader>();
            if (!loader->open(path)) return nullptr;
            fxTextureCache->addEQE1Loader(loader.get());
            fxArchives.push_back(std::move(loader));
            return fxArchives.back().get();
        });
    } else {
        std::fprintf(stderr, "[fx-harness] no data/eqe1_texture_index.json: model textures will not resolve\n");
    }
    // entity_mesh_build.cpp's setConstrainedTextureCache(): the loader and
    // its mesh builder (buildMeshFromGeometry's loadTextureFromBMP) both.
    raceModelLoader->setConstrainedTextureCache(fxTextureCache.get());
    if (raceModelLoader->getMeshBuilder())
        raceModelLoader->getMeshBuilder()->setConstrainedTextureCache(fxTextureCache.get());
    return true;
}

void uploadHarnessTextures() {
    if (fxTextureCache) fxTextureCache->uploadAllPending();
}

void shutdownHarness() {
#ifdef EQT_HAS_GLES2
    if (spellVisualFX) spellVisualFX->setParticleManager(nullptr);
    particleManager.reset();
#endif
    fxTextureCache.reset();
    fxArchives.clear();
    // B1139: the held items' textures (the caster's orb) live in the viewer's
    // equipment cache; a static unique_ptr destroyed after the device would
    // free them through a dead driver (SIGSEGV at exit), so drop them here.
    if (secondaryEquipNode) { secondaryEquipNode->remove(); secondaryEquipNode = nullptr; }
    if (primaryEquipNode) { primaryEquipNode->remove(); primaryEquipNode = nullptr; }
    equipmentModelLoader.reset();
    equipmentTextureCache.reset();
}

bool initHarnessParticles() {
#ifdef EQT_HAS_GLES2
    if (!fxTextureCache) return false;
    spellVisualFX->setConstrainedTextureCache(fxTextureCache.get());

    particleManager = std::make_unique<EQT::Graphics::Environment::ParticleManager>(smgr, driver);
    particleManager->setConstrainedTextureCache(fxTextureCache.get());
    if (!particleManager->init(clientPath)) return false;
    // B1123: the platform's spell particle pool (constrained_presets.json).
    const int pool = ConstrainedRendererConfig::readSpellParticlePool(
        "config/constrained_presets.json", "orangepi",
        ConstrainedRendererConfig::spellParticlePlatformKey());
    EQT::Graphics::Environment::SpellEffectsConfig::instance().applyPool(pool);
    std::printf("[fx-harness] spellParticlePool %d (%s)\n", pool,
                ConstrainedRendererConfig::spellParticlePlatformKey());
    if (!particleManager->initUnifiedRenderer()) return false;
    spellVisualFX->setParticleManager(particleManager.get());

    auto nodeFor = [](uint16_t id) -> EQAnimatedMeshSceneNode* {
        if (id == casterEntityId) return animatedNode;
        if (id == targetEntityId && !fxTargetGone) return targetAnimatedNode;  // a despawned entity resolves nothing
        return nullptr;
    };
    // The renderer's two callbacks (renderer_entities.cpp): a plain entity is
    // its node origin (lastZ + modelYOffset there), a hand/head pseudo-entity
    // the bone spell_fx_bone_anchor.h names.
    particleManager->setEntityPositionCallback([nodeFor](uint16_t id, glm::vec3& out) -> bool {
        if (isSpellFxBoneId(id)) {
            core::vector3df p;
            if (!resolveSpellFxBonePosition(nodeFor(EQ::SpellVisualFX::realEntityId(id)), id, p)) return false;
            out = glm::vec3(p.X, p.Y, p.Z);
            return true;
        }
        EQAnimatedMeshSceneNode* n = nodeFor(id);
        if (!n) return false;
        const auto p = n->getAbsolutePosition();
        out = glm::vec3(p.X, p.Y, p.Z);
        return true;
    });
    particleManager->setEntityDirectionCallback([nodeFor](uint16_t id, glm::vec3& out) -> bool {
        core::vector3df d;
        if (!resolveSpellFxBoneDirection(nodeFor(EQ::SpellVisualFX::realEntityId(id)), id, d)) return false;
        out = glm::vec3(d.X, d.Y, d.Z);
        return true;
    });
    spellVisualFX->setEntityPositionCallback([nodeFor](uint16_t id, core::vector3df& out) -> bool {
        if (isSpellFxBoneId(id))
            return resolveSpellFxBonePosition(nodeFor(EQ::SpellVisualFX::realEntityId(id)), id, out);
        EQAnimatedMeshSceneNode* n = nodeFor(id);
        if (!n) return false;
        out = n->getAbsolutePosition();
        return true;
    });
    // B1148 (#468): the caster model's CastingAnim track, as the renderer
    // reads it (spell_fx_bone_anchor.h).
    spellVisualFX->setCastAnimationLengthCallback([nodeFor](uint16_t id, const char* code) -> int32_t {
        return CastAnimationTrackMs(nodeFor(id), code);
    });
    // B1164 (#483): the attach scale s, the client's actor scale from the
    // entity's race and server size, as the renderer computes it.
    spellVisualFX->setEntityAttachScaleCallback([](uint16_t id) -> float {
        if (id == casterEntityId) return EQ::SpellFxAttachScale(harnessOpts.casterRace, harnessOpts.casterGender, harnessOpts.casterSize);
        if (id == targetEntityId) return EQ::SpellFxAttachScale(harnessOpts.targetRace, harnessOpts.targetGender, harnessOpts.targetSize);
        return 0.0f;
    });
    // B1152 (#472): the missile gate reads the launch/arrival times from the
    // SPELL module's log, so un-filter it (the model_viewer does not InitLogging,
    // so the module is at the default NONE).
    SetModuleLogLevel(MOD_SPELL, LOG_INFO);
    std::printf("[fx-harness] SpellVisualFX -> ParticleManager -> SimulationWorker ready\n");
    std::fflush(stdout);
    return true;
#else
    std::fprintf(stderr, "[fx-harness] needs a GLES2 build (EQT_HAS_GLES2)\n");
    return false;
#endif
}

void stepSpellParticles(float dt, uint32_t seed) {
#ifdef EQT_HAS_GLES2
    if (!particleManager) return;
    EQT::Graphics::SimulationInput input;
    auto& pi = input.particleInput;
    pi.deltaTime = dt;
    if (camera) {
        const auto cp = camera->getAbsolutePosition();
        pi.cameraPos = glm::vec3(cp.X, cp.Y, cp.Z);
    }
    pi.entityPositions = std::move(fxPendingPositions);
    pi.entityDirections = std::move(fxPendingDirections);
    pi.commands = particleManager->drainCommands();
    pi.unifiedRendererInitialized = true;
    pi.poolSize = EQT::Graphics::Environment::SpellEffectsConfig::instance().getGlobal().maxParticles;
    pi.rngSeed = seed;
    // B1165: the Spell page's density for an emitter that did not get it at
    // creation (EmitterConfig::densityAtCreate), as the client's sim bridge passes it.
    pi.density[static_cast<size_t>(EQT::Graphics::Environment::ParticleFamily::Spell)] = fxSpellDensity;
    fxComputed ^= 1;
    EQT::Graphics::SimulationOutput& fxOutput = fxOutputs[fxComputed];
    fxWorker.computeOnce(input, fxOutput);
    fxPendingPositions.clear();
    fxPendingDirections.clear();
    if (!fxOutput.particleOutput.valid) return;
    particleManager->setWorkerActiveCount(fxOutput.particleOutput.activeCount);
    for (uint16_t id : fxOutput.particleOutput.positionRequestEntities) {
        glm::vec3 p;
        if (particleManager->resolveEntityPosition(id, p)) fxPendingPositions[id] = p;
    }
    for (uint16_t id : fxOutput.particleOutput.directionRequestEntities) {
        glm::vec3 d;
        if (particleManager->resolveEntityDirection(id, d)) fxPendingDirections[id] = d;
    }
#else
    (void)dt;
    (void)seed;
#endif
}

void renderSpellParticles() {
#ifdef EQT_HAS_GLES2
    const EQT::Graphics::SimulationOutput& fxOutput =
        fxOutputs[harnessOpts.workerLatency > 0 ? fxComputed ^ 1 : fxComputed];
    if (!particleManager || !fxOutput.particleOutput.valid) return;
    const core::matrix4 viewMat = driver->getTransform(video::ETS_VIEW);
    const core::matrix4 projMat = driver->getTransform(video::ETS_PROJECTION);
    const core::vector3df camPos = camera ? camera->getAbsolutePosition() : core::vector3df(0, 0, 0);
    // qeynos2's fog starts at 250 units (lane fable.log "Fog: start=250"),
    // far past every harness framing, so the harness draws without fog.
    particleManager->renderUnified(viewMat, projMat, camPos, 999999.0f, 999999.0f, nullptr,
                                   static_cast<float>(driver->getScreenSize().Height),
                                   &fxOutput.particleOutput.renderBuffer);
#endif
}

void harnessBeginCast(const EQ::SpellData& spell, bool self, uint32_t castMs, int casterLevel,
                      Clock::time_point now) {
    eqt::state::SpellCastVisualStartedData d{};
    d.fxGateLevel = casterLevel;  // B1144: the local player's spawn level
    d.casterId = casterEntityId;
    d.spellId = spell.id;
    d.castTimeMs = castMs;
    d.spellName = spell.name;
    d.isPlayerCast = true;
    d.isTargetCast = false;
    d.spellVisual = spell.visualVocabulary();
    d.castAnim = spell.cast_anim;  // B1148
    // B1152 (#472): the missile flies to the spell's target (the NPC target
    // for an 'other' cast, the caster for a 'self' one) -- the same target
    // harnessAction() lands on. A Middle-stage spell then spawns a missile
    // here that flies to this id and fires the Target stage on arrival.
    d.targetId = self ? casterEntityId : targetEntityId;
    (void)self;
    eqt::bridge::ApplySpellCastStartedFx(fxRoute, spellVisualFX.get(), d, now);
}

void harnessAction(const EQ::SpellData& spell, bool self, int casterLevel, Clock::time_point now) {
    eqt::state::SpellCastVisualCompleteData d{};
    d.fxGateLevel = casterLevel;  // B1144
    d.casterId = casterEntityId;
    d.targetId = self ? casterEntityId : targetEntityId;
    d.spellId = spell.id;
    d.spellName = spell.name;
    d.isPlayerCast = true;
    d.isTargetCast = false;
    d.spellVisual = spell.visualVocabulary();
    // effect_flag 0: the cast-animation packet (arms the landing) ...
    d.isCastAnimation = true;
    d.isSuccess = false;
    eqt::bridge::ApplySpellCastCompleteFx(fxRoute, spellVisualFX.get(), d, now);
    // ... then the effect_flag 4 repeat the caster also receives, 4 ms later.
    d.isCastAnimation = false;
    d.isSuccess = true;
    eqt::bridge::ApplySpellCastCompleteFx(fxRoute, spellVisualFX.get(), d,
                                          now + std::chrono::milliseconds(4));
}

void harnessInterrupt(const EQ::SpellData& spell) {
    eqt::state::SpellCastVisualInterruptedData d{};
    d.casterId = casterEntityId;
    d.spellId = spell.id;
    d.isPlayerCast = true;
    d.isTargetCast = false;
    eqt::bridge::ApplySpellCastInterruptedFx(fxRoute, spellVisualFX.get(), d);
}

void placeHarnessTarget(bool self, float distance) {
    if (!targetAnimatedNode || !animatedNode) return;
    // B1155: b1130-self is the caster alone (B1130's fixed_self stills); the
    // target stands where fitFixedNpcTarget solved it for b1130.
    if (harnessOpts.cameraPreset == "b1130-self") {
        targetAnimatedNode->setVisible(false);
        return;
    }
    targetAnimatedNode->setVisible(!self);
    if (self) return;
    if (harnessOpts.cameraPreset == "b1130") {
        targetAnimatedNode->setPosition(core::vector3df(b1130TargetX, b1130TargetY, b1130TargetZ));
    } else {
        const core::vector3df right = harnessOpts.cameraPreset.empty() ? core::vector3df(1, 0, 0)
                                                                       : targetRightOfCaster();
        core::vector3df pos = animatedNode->getPosition() + right * distance;
        if (!harnessOpts.cameraPreset.empty()) {
            // B1172 (#491): stand the target on the caster's ground, the rule
            // addGroundPlane applies to her (the client's collisionZOffset:
            // an entity's pose box stands on the floor). At the client's
            // actor scale the human's pose box reaches 0.85 units below the
            // dark elf's, so placed at her origin it sank through the
            // depth-writing plane and lost its feet in every frame. Measured
            // once, on the first placement (the frozen t = 0 pose, as the
            // ground plane is), so every later placement stands it the same.
            static bool measured = false;
            static float standY = 0.0f;
            if (!measured) {
                measured = true;
                targetAnimatedNode->setPosition(pos);
                targetAnimatedNode->updateAbsolutePosition();
                animatedNode->forceAnimationUpdate();
                targetAnimatedNode->forceAnimationUpdate();
                core::aabbox3df cb = animatedNode->getBoundingBox();
                animatedNode->getAbsoluteTransformation().transformBoxEx(cb);
                core::aabbox3df tb = targetAnimatedNode->getBoundingBox();
                targetAnimatedNode->getAbsoluteTransformation().transformBoxEx(tb);
                standY = cb.MinEdge.Y - tb.MinEdge.Y;
                std::printf("[fx-harness] %s target: pose box y %.3f..%.3f; caster pose box bottom %.3f -> "
                            "target stands at y %+.3f\n",
                            harnessOpts.cameraPreset.c_str(), tb.MinEdge.Y, tb.MaxEdge.Y, cb.MinEdge.Y, standY);
                std::fflush(stdout);
            }
            pos.Y += standY;
        }
        targetAnimatedNode->setPosition(pos);
    }
    targetAnimatedNode->updateAbsolutePosition();
}

// The zone floor the lane's particles fall behind: a plane at the caster's
// feet (the pose box's bottom, where the client's collisionZOffset stands her).
void addGroundPlane() {
    animatedNode->forceAnimationUpdate();
    core::aabbox3df box = animatedNode->getBoundingBox();
    animatedNode->getAbsoluteTransformation().transformBoxEx(box);
    scene::IMesh* plane = smgr->getGeometryCreator()->createPlaneMesh(
        core::dimension2df(20.0f, 20.0f), core::dimension2du(40, 40));
    scene::IMeshSceneNode* node = smgr->addMeshSceneNode(plane);
    plane->drop();
    if (!node) return;
    fxGroundNode = node;
    node->setPosition(core::vector3df(0.0f, box.MinEdge.Y, 0.0f));
    // A SOLID plane painted the clear colour: it writes depth (particles
    // below the ground are hidden as lane 5's terrain hides them) and shows
    // as background. Two invisible variants were tried and do not work in
    // this driver: ColorMask = ECP_NONE desyncs (UnifiedParticleRenderer's raw
    // GL resets the colour mask behind the driver's state cache, and the plane
    // drew solid white from the first particle frame on), and a vertex-alpha-0
    // transparent plane with ALLOW_ZWRITE_ON_TRANSPARENT wrote no depth (its
    // frames were byte-identical to --no-ground). So a backdrop shows above
    // the horizon only.
    // (Red and blue swapped: on this unlit, untextured solid the driver drew
    // SColor(255,50,50,80) as R80 B50 -- read off the first harness frames --
    // so SColor(255,80,50,50) lands on the clear colour 50,50,80.)
    smgr->getMeshManipulator()->setVertexColors(node->getMesh(), video::SColor(255, 80, 50, 50));
    for (u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = false;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).MaterialType = video::EMT_SOLID;
    }
    std::printf("[fx-harness] ground: solid clear-colour plane at y %.3f (800 x 800 units)\n", box.MinEdge.Y);
    std::fflush(stdout);
}

bool setupSpellTestCamera() {
    // Scripted mode freezes the scene clock BEFORE the preset's fit renders
    // anything: drawAll() animates every node off the device timer, and a
    // wall-clock pose in the fit's frames would leave the idle animation at a
    // different phase on every run.
    if (!harnessOpts.planPath.empty()) {
        device->getTimer()->stop();
        device->getTimer()->setTime(1000);
    }
    if (harnessOpts.cameraPreset == "b1118" || harnessOpts.cameraPreset == "b1130" ||
        harnessOpts.cameraPreset == "b1130-self") {
        PresetGeometry g;
        g.casterPx = harnessOpts.casterPx;
        if (harnessOpts.cameraPreset != "b1118") {
            // B1155: B1130's fixed stills (review/B1130/README.md "Geometry"):
            // the B1118 opening pose, caster 209 px with feet row 598 at
            // column 640 (the B1118 tour's own numbers are row 605 / 235 px).
            g.feetX = 640.0f;
            g.feetY = 598.0f;
        }
        if (!fitFaceOnCamera(g, harnessOpts.cameraPreset.c_str())) return false;
        if (harnessOpts.cameraPreset == "b1130") {
            // The fixed_npc target: the oracle r51 target's measured feet (B1166).
            g.targetFeetX = fxharness::kB1130TargetAnchor.feetX;
            g.targetFeetY = fxharness::kB1130TargetAnchor.feetY;
            g.targetPx = fxharness::kB1130TargetAnchor.px;
            if (!fitFixedNpcTarget(g)) return false;
        }
        if (harnessOpts.ground) addGroundPlane();
        return true;
    }
    // The interactive framing: the midpoint between caster (0,0,0) and target.
    const float mid = harnessOpts.interactiveDistance * 0.5f;
    camera = smgr->addCameraSceneNode(nullptr, core::vector3df(mid, 3.0f, -10.0f),
                                      core::vector3df(mid, 1.5f, 0));
    return true;
}

namespace {

// B1169 (#488) diagnostics, WILLEQ_FX_MESH_HASH=1 only: an FNV-1a hash of
// each model's posed vertex and index buffers (the bytes the GPU draws) per
// written frame, and the scene tree at frame 0 (each node's material-0
// texture pointer is its pre-B1169 solid sort key), so two runs show whether
// their CPU-side geometry and their node set agree before a frame difference
// is blamed on the GPU.
uint64_t meshBytesHash(scene::IMesh* m) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    if (!m) return 0;
    for (u32 i = 0; i < m->getMeshBufferCount(); ++i) {
        scene::IMeshBuffer* mb = m->getMeshBuffer(i);
        const size_t vsz = mb->getVertexType() == video::EVT_STANDARD ? sizeof(video::S3DVertex)
                         : mb->getVertexType() == video::EVT_2TCOORDS ? sizeof(video::S3DVertex2TCoords)
                                                                      : sizeof(video::S3DVertexTangents);
        mix(mb->getVertices(), mb->getVertexCount() * vsz);
        mix(mb->getIndices(), mb->getIndexCount() * (mb->getIndexType() == video::EIT_16BIT ? 2 : 4));
    }
    return h;
}

void logSceneNodes(scene::ISceneNode* n, int depth) {
    for (scene::ISceneNode* c : n->getChildren()) {
        std::printf("[fx-harness] scene-node %*s'%s' type %08x visible %d materials %u tex0 %p mt0 %d\n",
                    depth * 2, "", c->getName(), static_cast<unsigned>(c->getType()), c->isVisible() ? 1 : 0,
                    c->getMaterialCount(),
                    c->getMaterialCount() ? static_cast<const void*>(c->getMaterial(0).getTexture(0)) : nullptr,
                    c->getMaterialCount() ? static_cast<int>(c->getMaterial(0).MaterialType) : -1);
        logSceneNodes(c, depth + 1);
    }
}

void logMeshHashes(int frame) {
    static const bool on = std::getenv("WILLEQ_FX_MESH_HASH") != nullptr;
    if (!on) return;
    if (frame == 0) logSceneNodes(smgr->getRootSceneNode(), 0);
    std::printf("[fx-harness] mesh-hash frame %d caster %016llx target %016llx\n", frame,
                static_cast<unsigned long long>(animatedNode ? meshBytesHash(animatedNode->getInstanceMesh()) : 0),
                static_cast<unsigned long long>(targetAnimatedNode ? meshBytesHash(targetAnimatedNode->getInstanceMesh()) : 0));
}

// B1169 (#488): THE HARNESS'S FIXED SOLID DRAW ORDER.
// Irrlicht's solid pass sorts its nodes by their material-0 texture POINTER
// (CSceneManager::drawAll: SolidNodeList.sort(), DefaultNodeEntry::operator<
// on TextureValue = getMaterial(0).getTexture(0)) -- a heap address, so under
// ASLR the order of the caster, her held item (IT72, the orb) and a
// distance-0 target changed from run to run. Those models share pixels whose
// depths tie (the ThinkPad's GBM context is 16-bit depth), and the LEQUAL
// depth test keeps the LATER draw there: the pre-B1169 frame differences
// (refactor/B1169/README.md). This node takes the ground plane and the
// models out of the sorted list and draws them, inside the same solid pass,
// in ONE fixed order: ground, target, the caster's held items, caster. There is
// NO client rule to follow: the production client goes through the same
// Irrlicht texture-pointer sort (CSceneManager.cpp:1485, patched Irrlicht)
// and no oracle draw order has been established. This is a HARNESS
// CONVENTION, the majority order (13 of 20 pre-B1169 runs of 93; the other 7
// split the other way by frame md5), so those runs' frames are this order's
// frames bit for bit. Harness-only: compiled only into the model_viewer
// target (CMakeLists.txt:1313); the production client is untouched. A node or held item with a
// transparent material registers for the transparent pass exactly as its own
// OnRegisterSceneNode would (AUTOMATIC: the whole model; a mesh node: its
// transparent buffers), so only the solid order changes.
class HarnessSolidOrderNode : public scene::ISceneNode {
public:
    explicit HarnessSolidOrderNode(scene::ISceneManager* mgr) : scene::ISceneNode(mgr->getRootSceneNode(), mgr, -1) {
        setAutomaticCulling(scene::EAC_OFF);
        setName("B1169 solid order");
    }
    // The managed nodes, in draw order; each is reparented under this node
    // (identity transform), so the scene manager never registers them itself.
    void adopt(scene::ISceneNode* n, bool heldItemsFirst) {
        if (!n) return;
        n->setParent(this);
        order_.push_back({n, heldItemsFirst});
    }
    void OnRegisterSceneNode() override {
        solid_.clear();
        if (!IsVisible) return;
        for (const auto& e : order_) collect(e.node, e.heldItemsFirst);
        if (!solid_.empty()) SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
        // No ISceneNode::OnRegisterSceneNode(): the children are drawn here.
    }
    void render() override {
        for (scene::ISceneNode* n : solid_) n->render();
    }
    const core::aabbox3d<f32>& getBoundingBox() const override { return box_; }
    // The names in the order the last frame drew them.
    std::string describe() const {
        std::string out;
        for (scene::ISceneNode* n : solid_) {
            if (!out.empty()) out += " < ";
            out += n->getName();
        }
        return out;
    }

private:
    struct Entry { scene::ISceneNode* node; bool heldItemsFirst; };
    void collect(scene::ISceneNode* n, bool heldItemsFirst) {
        if (!n->isVisible()) return;
        if (heldItemsFirst)
            for (scene::ISceneNode* c : n->getChildren()) collect(c, true);
        bool solid = false, transparent = false;
        for (u32 i = 0; i < n->getMaterialCount(); ++i) {
            video::IMaterialRenderer* r =
                SceneManager->getVideoDriver()->getMaterialRenderer(n->getMaterial(i).MaterialType);
            (r && r->isTransparent() ? transparent : solid) = true;
        }
        if (n->getType() == scene::ESNT_MESH) {  // CMeshSceneNode: each pass its own buffers
            if (transparent) SceneManager->registerNodeForRendering(n, scene::ESNRP_TRANSPARENT);
            if (solid) solid_.push_back(n);
        } else if (transparent) {  // ESNRP_AUTOMATIC: one transparent material = a transparent node
            SceneManager->registerNodeForRendering(n, scene::ESNRP_TRANSPARENT);
        } else if (solid) {
            solid_.push_back(n);
        }
        if (!heldItemsFirst)
            for (scene::ISceneNode* c : n->getChildren()) collect(c, false);
    }
    std::vector<Entry> order_;
    std::vector<scene::ISceneNode*> solid_;
    core::aabbox3d<f32> box_{0, 0, 0, 0, 0, 0};
};

HarnessSolidOrderNode* fxSolidOrder = nullptr;

// Built once the preset's fits are done (they read the models' boxes through
// the normal scene); names the nodes for the log.
void fixHarnessSolidOrder() {
    fxSolidOrder = new HarnessSolidOrderNode(smgr);
    if (fxGroundNode) fxGroundNode->setName("ground");
    if (targetAnimatedNode) targetAnimatedNode->setName("target");
    if (animatedNode) {
        animatedNode->setName("caster");
        for (scene::ISceneNode* c : animatedNode->getChildren()) c->setName("caster-held-item");
    }
    fxSolidOrder->adopt(fxGroundNode, false);
    fxSolidOrder->adopt(targetAnimatedNode, true);
    fxSolidOrder->adopt(animatedNode, true);
    fxSolidOrder->drop();  // the scene graph holds it
}

// Logged at frame 0 and whenever it changes (the target hides for a self
// cast and after its despawn).
void logSolidDrawOrder(int frame) {
    static std::string last;
    if (!fxSolidOrder) return;
    const std::string order = fxSolidOrder->describe();
    if (order == last) return;
    last = order;
    std::printf("[fx-harness] solid draw order (fixed, B1169) at frame %d: %s\n", frame, order.c_str());
}

}  // namespace

int runScriptedHarness() {
    std::vector<HarnessCast> plan;
    std::string err;
    if (!parseHarnessPlan(harnessOpts.planPath, plan, err)) {
        std::fprintf(stderr, "[fx-harness] %s\n", err.c_str());
        return 2;
    }
    if (harnessOpts.fps <= 0 || harnessOpts.seconds <= 0.0 || harnessOpts.framesDir.empty()) {
        std::fprintf(stderr, "[fx-harness] --plan needs --frames-dir <dir>, --fps > 0 and --seconds > 0\n");
        return 2;
    }
    if (!particleManager || !animatedNode) {
        std::fprintf(stderr, "[fx-harness] the FX chain or the caster model did not start\n");
        return 3;
    }
    // B1165 (#484): the Spell page the run draws with, printed every run and
    // pinned to the oracle capture's ini (1.0 / 1.0 / 2.0 / 24, ShowSpellEffects
    // 1) so a gate always compares like with like.
    {
        const EQ::SpellFxOptions& o = harnessOpts.spellFx;
        const bool capture = EQ::IsOracleCaptureSpellFxOptions(o);
        std::printf("[fx-harness] particle options: SpellParticleDensity=%f SpellParticleOpacity=%f "
                    "SpellParticleNearClipPlane=%f SpellParticleCastFilter=%d ShowSpellEffects=%d (%s)\n",
                    static_cast<double>(o.density), static_cast<double>(o.opacity), static_cast<double>(o.nearClip),
                    static_cast<int>(o.castFilter), o.showSpellEffects ? 1 : 0,
                    capture ? "the oracle capture's ini" : "NOT the oracle capture's ini 1.0/1.0/2.0/24/1");
        std::fflush(stdout);
        if (!capture && !harnessOpts.particleOptions) {
            std::fprintf(stderr, "[fx-harness] the particle options differ from the oracle capture's ini "
                                 "(1.0/1.0/2.0/24/1); pass --particle-options to run anyway\n");
            return kHarnessExitParticleOptions;
        }
        if (spellVisualFX) spellVisualFX->setSpellFxOptions(o);
        auto knobs = EQT::Graphics::Environment::identityParticleKnobs();
        auto& sk = knobs[static_cast<size_t>(EQT::Graphics::Environment::ParticleFamily::Spell)];
        sk.density = o.density;
        sk.opacity = o.opacity;
        sk.nearClip = o.nearClip;
        particleManager->setParticleKnobs(knobs);
        fxSpellDensity = o.density;
    }
    struct Event { double t; int cast; bool action; uint32_t castMs; };
    std::vector<Event> events;
    for (size_t i = 0; i < plan.size(); ++i) {
        const EQ::SpellData* sp = spellOrNull(plan[i].spellId);
        if (!sp) {
            std::fprintf(stderr, "[fx-harness] spell %u is not in willeq.db\n", plan[i].spellId);
            return 2;
        }
        const uint32_t castMs = plan[i].castMsOverride >= 0 ? static_cast<uint32_t>(plan[i].castMsOverride)
                                                            : sp->cast_time_ms;
        // B1171 (#490): the caster level this cast is drawn at, and where it came
        // from, in the run log -- a sweep's comparison against an oracle capture
        // is refused when this differs from the capture's `#level`
        // (scripts/fx/capture_level.py).
        std::printf("[fx-harness] cast %zu: spell %u caster_level %d (%s)\n", i, plan[i].spellId,
                    plan[i].casterLevel >= 0 ? plan[i].casterLevel : harnessOpts.casterLevel,
                    plan[i].casterLevel >= 0 ? "plan" : "--caster-level");
        if (plan[i].beginCast) events.push_back({plan[i].tBegin, static_cast<int>(i), false, castMs});
        events.push_back({plan[i].tBegin + castMs / 1000.0, static_cast<int>(i), true, castMs});
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });

    std::error_code ec;
    std::filesystem::create_directories(harnessOpts.framesDir, ec);
    std::ofstream timing(harnessOpts.framesDir + "/timing.tsv");
    if (!timing) {
        std::fprintf(stderr, "[fx-harness] cannot write %s/timing.tsv\n", harnessOpts.framesDir.c_str());
        return 4;
    }
    // B1171 (#490): caster_level (last column, so the index readers keep
    // working) = the slot-gate level each packet was drawn at.
    timing << "frame\tt\tevent\tspell\ttarget\tdistance\tcast_ms\tt_event\tcaster_level\n";
    // B1165 (#484): the Spell page the run drew with, for the gate scripts
    // (scripts/tools/fx_particle_options.py pins it to the capture's ini).
    {
        const EQ::SpellFxOptions& o = harnessOpts.spellFx;
        std::ofstream po(harnessOpts.framesDir + "/particle_options.tsv");
        po << "key\tvalue\n"
           << "SpellParticleDensity\t" << o.density << "\n"
           << "SpellParticleOpacity\t" << o.opacity << "\n"
           << "SpellParticleNearClipPlane\t" << o.nearClip << "\n"
           << "SpellParticleCastFilter\t" << o.castFilter << "\n"
           << "ShowSpellEffects\t" << (o.showSpellEffects ? 1 : 0) << "\n";
    }
    // B1144: the spellsnew.eff slots each packet draws (SpellVisualFX::stageSlots,
    // the rule createCastGlow / createLanding draw by): BeginCast -> Source,
    // the cast-animation Action -> Target. No BeginCast = no Source row.
    std::ofstream slotsOut(harnessOpts.framesDir + "/slots.tsv");
    slotsOut << "t_event\tevent\tspell\tanim\tlevel\tstage\tslot\temitter\tmin_level\tanchor\tattach\ttexture\n";

    // B1157 (#476) step 2: the Source-stage emitter ORIGIN per frame (the
    // hand-anchor strip) -- scene.tsv carries the pose boxes, this carries
    // where the emitter sits in world and screen space while the caster holds
    // the cast pose, so it shows the origin tracking the hand bone through
    // the raised pose.
    std::ofstream emitterOut(harnessOpts.framesDir + "/emitter_origin.tsv");
    emitterOut << "frame\tt\tfxid\tkind\twx\tyw\twz\tsx\tsy\n";
    // B1174 (#493): per written frame, the live spell effects and missiles
    // (SpellVisualFX's own lists) and the caster's track -- what a /disc
    // draws is the caster's CastingAnim and no effect, which a whole-frame
    // pixel diff cannot tell apart.
    std::ofstream activityOut(harnessOpts.framesDir + "/fx_activity.tsv");
    activityOut << "frame\tt\tactive_effects\tmissiles\tcaster_anim\n";

    if (!harnessOpts.backdropPath.empty()) {
        fxBackdrop = driver->getTexture(harnessOpts.backdropPath.c_str());
        if (!fxBackdrop) std::fprintf(stderr, "[fx-harness] backdrop %s did not load\n", harnessOpts.backdropPath.c_str());
    }
    const int simHz = harnessOpts.simHz > 0 ? harnessOpts.simHz : harnessOpts.fps;
    if (simHz % harnessOpts.fps != 0) {
        std::fprintf(stderr, "[fx-harness] --sim-hz %d is not a multiple of --fps %d\n", simHz, harnessOpts.fps);
        return 2;
    }
    const int sub = simHz / harnessOpts.fps;  // simulation steps per written frame
    const double dt = 1.0 / simHz;
    const int frames = static_cast<int>(std::lround(harnessOpts.seconds * harnessOpts.fps));
    const Clock::time_point epoch{};  // the scripted clock: no wall time anywhere
    // B1169 (#488): what makes two runs of one plan pixel-identical
    // (tools/model_viewer/README.md "Determinism").
    std::printf("[fx-harness] determinism (B1169): fx-seed %u%s, step 1/%d s on the scripted clock, "
                "synchronous worker, fixed solid draw order\n",
                harnessOpts.seed, harnessOpts.seed == 0 ? " (random_device: NOT reproducible)" : "", simHz);
    size_t next = 0;
    double lastAction = -1.0;  // scripted time of the latest OP_Action
    bool targetDied = false;
    bool targetDespawned = false;
    double corpseFadeStart = -1.0;  // B1148: scripted time of a corpse's OP_DeleteSpawn
    float corpseFadeAlpha = 1.0f;
    // B1157 (#476): the Source-stage emitter ids of the in-flight cast (the
    // caster's own hands for a hand-anchored spell) -- dumped to
    // emitter_origin.tsv every frame, cleared at the cast's Action; and the
    // cast track its BeginCast started ("" when the model has none) -- the
    // Action only ends a pose that began, the client's guard
    // (endEntityCastAnim runs on a non-empty castAnimCode). A /disc is
    // OP_Action with no OP_BeginCast: its swing is the OP_Action handler's
    // own CastingAnim request (B1174, actionCastTrack below).
    std::vector<uint16_t> activeSourceEmitters;
    std::string activeCastTrack;
    // B1174 (#493): the track an OP_Action started with no cast in progress
    // (a /disc; EntityRenderer::actionEntityCastAnim's rule), "" = none.
    std::string actionCastTrack;
    placeHarnessTarget(plan[0].self, plan[0].distance);
    fixHarnessSolidOrder();  // B1169 (#488)
    device->getTimer()->stop();  // (already stopped by setupSpellTestCamera)
    // Step g runs at t = g / simHz; every sub-th step is drawn and written as
    // frame g / sub (with sub = 2, exactly review/B1138/scripts/decimate.py's
    // even frames of a 60 fps run).
    for (int g = 0; g < frames * sub; ++g) {
        if (!device->run()) break;
        const double t = g * dt;
        const int f = g / sub;
        while (next < events.size() && events[next].t <= t + 1e-9) {
            const Event& e = events[next++];
            const HarnessCast& c = plan[e.cast];
            const EQ::SpellData& sp = *spellOrNull(c.spellId);
            const auto now = epoch + std::chrono::microseconds(std::llround(e.t * 1e6));
            const int level = c.casterLevel >= 0 ? c.casterLevel : harnessOpts.casterLevel;
            if (!e.action) {
                placeHarnessTarget(c.self, c.distance);
                harnessBeginCast(sp, c.self, e.castMs, level, now);
                // B1157 (#476): the caster model does the client's OP_BeginCast
                // rule through the client's own code (spell_cast_animation.h,
                // the body EntityRenderer::startEntityCast runs): the spell's
                // CastingAnim track, one shot at speed 1.0, held (or looped,
                // per the model's own looped flag) through the channel. The
                // o01 idle that looped during the channel before is gone.
                const std::string track = playCastAnim(animatedNode, sp.cast_anim);
                activeCastTrack = track;
                actionCastTrack.clear();
                std::printf("[fx-harness] BeginCast %u: caster cast-anim %s (CastingAnim %d)\n", c.spellId,
                            track.empty() ? "(no track; current stands)" : track.c_str(), sp.cast_anim);
                // The Source-stage emitter origins to dump (step 2): the
                // slots' attach points on the caster, the way the FX resolves
                // them (B1144's attachPointEntityId).
                activeSourceEmitters.clear();
                for (const auto& d : spellVisualFX->stageSlots(c.spellId, EQ::kSpellsNewEffSource, level).drawn) {
                    const uint16_t fxId =
                        EQ::SpellVisualFX::attachPointEntityId(casterEntityId, d.slot.attachPoint);
                    if (std::find(activeSourceEmitters.begin(), activeSourceEmitters.end(), fxId) ==
                        activeSourceEmitters.end())
                        activeSourceEmitters.push_back(fxId);
                }
            } else {
                if (!c.beginCast) placeHarnessTarget(c.self, c.distance);
                harnessAction(sp, c.self, level, now);
                // B1157: OP_Action ends the cast state; the model takes the
                // idle (the guessed t04/t05/t06 played once here before is
                // gone). Only when a BeginCast started the track; a /disc
                // (OP_Action without OP_BeginCast) takes the B1174 branch.
                if (!activeCastTrack.empty()) {
                    endCastAnim(animatedNode, "p01");
                } else if (EQ::ActionPlaysSourceCastAnim(harnessActionInputs(c.spellId, sp))) {
                    // B1174 (#493): eqgame's OP_Action handler (0x47f4af)
                    // plays the spell's CastingAnim on its standing source --
                    // a /disc's only visible effect (the observer's t06
                    // swing, refactor/B1174/README.md). Played out, the idle
                    // returns (below, per step).
                    actionCastTrack = playCastAnim(animatedNode, sp.cast_anim);
                    std::printf("[fx-harness] Action %u: caster action cast-anim %s (CastingAnim %d)\n", c.spellId,
                                actionCastTrack.empty() ? "(no track; current stands)" : actionCastTrack.c_str(),
                                sp.cast_anim);
                }
                activeCastTrack.clear();
                activeSourceEmitters.clear();
                lastAction = e.t;
            }
            {
                const int stage = e.action ? EQ::kSpellsNewEffTarget : EQ::kSpellsNewEffSource;
                const auto ss = spellVisualFX->stageSlots(c.spellId, stage, level);
                for (const auto& d : ss.drawn)
                    slotsOut << e.t << '\t' << (e.action ? "Action" : "BeginCast") << '\t' << c.spellId << '\t'
                             << spellVisualFX->spellVocabulary(c.spellId).spellAnim << '\t' << level << '\t'
                             << (e.action ? "Target" : "Source") << '\t' << d.index << '\t' << d.slot.emitter
                             << '\t' << d.slot.minLevel << '\t' << d.slot.anchor << '\t' << d.slot.attachPoint
                             << '\t' << d.texture << '\n';
            }
            char row[256];
            std::snprintf(row, sizeof(row), "%d\t%.6f\t%s\t%u\t%s\t%.3f\t%u\t%.6f\t%d\n", f, t,
                          e.action ? "Action" : "BeginCast", c.spellId, c.self ? "self" : "other",
                          c.distance, e.castMs, e.t, level);
            timing << row;
        }
        if (!actionCastTrack.empty() && actionCastAnimEnded(animatedNode, actionCastTrack)) {
            const bool playedOut = animatedNode->getAnimator().getCurrentAnimation() == actionCastTrack;
            if (playedOut && animatedNode->hasAnimation("p01"))
                animatedNode->playAnimation("p01", /*loop=*/true, /*playThrough=*/false);
            std::printf("[fx-harness] action cast-anim %s %s at t %.3f\n", actionCastTrack.c_str(),
                        playedOut ? "played out" : "replaced", t);
            actionCastTrack.clear();
        }
        // B1139: the lane NPC the tour kills (OP_Death, then OP_DeleteSpawn).
        // The death is ApplyCorpseDeathPose's witnessed branch: stop, then d05
        // (d02/d01 fallback) once, play-through, resting on its last frame.
        if (lastAction >= 0.0 && targetAnimatedNode && !targetDied && harnessOpts.targetDeathAfterAction >= 0.0 &&
            t + 1e-9 >= lastAction + harnessOpts.targetDeathAfterAction) {
            targetDied = true;
            for (const char* anim : {"d05", "d02", "d01"}) {
                if (!targetAnimatedNode->hasAnimation(anim)) continue;
                targetAnimatedNode->stopAnimation();
                targetAnimatedNode->playAnimation(anim, false, true);
                std::printf("[fx-harness] target death %s at t %.3f\n", anim, t);
                break;
            }
        }
        if (lastAction >= 0.0 && targetAnimatedNode && !targetDespawned && harnessOpts.targetDespawnAfterAction >= 0.0 &&
            t + 1e-9 >= lastAction + harnessOpts.targetDespawnAfterAction) {
            targetDespawned = true;
            // B1145: the production OP_DeleteSpawn hook (a corpse once the
            // target died: its attached spell emitters go 3.5 s later).
            spellVisualFX->onSpawnDeleted(targetEntityId, targetDied, 0);
            // B1148 (#468): the model the way EntityRenderer::beginCorpseFade
            // takes it -- a corpse on the fade list (corpse_fade.h), any other
            // spawn at the packet.
            if (CorpseUsesFadeList(targetDied, harnessOpts.targetRace)) {
                corpseFadeStart = t;
                std::printf("[fx-harness] target despawned at t %.3f: corpse on the fade list\n", t);
            } else {
                fxTargetGone = true;
                targetAnimatedNode->setVisible(false);
                std::printf("[fx-harness] target despawned at t %.3f\n", t);
            }
        }
        if (corpseFadeStart >= 0.0 && !fxTargetGone) {
            const auto elapsedMs = static_cast<uint32_t>(std::llround((t - corpseFadeStart) * 1000.0));
            if (CorpseFadeExpired(elapsedMs)) {
                fxTargetGone = true;
                targetAnimatedNode->setVisible(false);
                std::printf("[fx-harness] corpse removed at t %.3f (%u ms after despawn)\n", t, elapsedMs);
            } else if (const float a = CorpseFadeAlpha(elapsedMs); a < corpseFadeAlpha) {
                corpseFadeAlpha = a;
                ApplyCorpseFadeAlpha(targetAnimatedNode, a, CorpseFadeMaterials{});
            }
        }
        // The scene's own clock: drawAll() animates every node off the device
        // timer, so the timer is stopped and set to the scripted time (+1 s:
        // EQAnimatedMeshSceneNode reads lastTimeMs_ == 0 as "first frame").
        const u32 ms = 1000u + static_cast<u32>(std::llround(t * 1000.0));
        device->getTimer()->setTime(ms);
        animatedNode->OnAnimate(ms);
        if (targetAnimatedNode) targetAnimatedNode->OnAnimate(ms);
        updateEquipmentPositions();  // B1139: the caster's held items on their bones
        spellVisualFX->update(static_cast<float>(dt));
        stepSpellParticles(static_cast<float>(dt), harnessOpts.seed);
        uploadHarnessTextures();
        if (g % sub != 0) continue;  // simulated, not drawn

        driver->beginScene(true, true, video::SColor(255, 50, 50, 80));
        if (fxBackdrop) {
            const auto ts = fxBackdrop->getOriginalSize();
            driver->draw2DImage(fxBackdrop, core::rect<s32>(0, 0, harnessOpts.width, harnessOpts.height),
                                core::rect<s32>(0, 0, ts.Width, ts.Height));
        }
        smgr->drawAll();
        logSolidDrawOrder(f);
        logMeshHashes(f);
        renderSpellParticles();
        for (const auto& r : harnessOpts.uiOccluders)  // the lane client's UI, drawn last
            driver->draw2DRectangle(video::SColor(255, 24, 24, 30), core::rect<s32>(r.x0, r.y0, r.x1, r.y1));
        driver->endScene();

        video::IImage* shot = driver->createScreenShot();
        // The surfaceless EGL surface can be another size than the REQUESTED
        // frame (the ThinkPad's virtual display gave 3840x2160 for a 1280x800
        // request): downscale so the written PNG IS the requested frame, where
        // the preset's recorded pixels (and the scene.tsv boxes) hold.
        if (shot) {
            const auto dim = shot->getDimension();
            if (dim.Width != static_cast<u32>(harnessOpts.width) ||
                dim.Height != static_cast<u32>(harnessOpts.height)) {
                video::IImage* scaled =
                    scaleImageTo(driver, shot, static_cast<u32>(harnessOpts.width),
                                 static_cast<u32>(harnessOpts.height));
                shot->drop();
                shot = scaled;
            }
        }
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%05d.png", f);
        const bool ok = shot && driver->writeImageToFile(shot, (harnessOpts.framesDir + name).c_str());
        // B1166 (#485): the b1130 target self-check on the WRITTEN frame 0 (pre-cast:
        // caster + target on the clear colour), against the one anchor the solve
        // used (fx_target_anchor.h), so a fit that places another body than the
        // oracle's cannot silently pass again.
        if (ok && f == 0 && harnessOpts.cameraPreset == "b1130" && !b1130TargetSelfCheck(shot)) {
            shot->drop();
            return 5;
        }
        if (shot) shot->drop();
        if (ok && f == 0) writeSceneBoxes(harnessOpts.framesDir + "/scene.tsv");
        if (!ok) {
            std::fprintf(stderr, "[fx-harness] frame %d did not write\n", f);
            return 4;
        }
        char row[96];
        std::snprintf(row, sizeof(row), "%d\t%.6f\t-\t-\t-\t-\t-\t-\t-\n", f, t);
        timing << row;
        activityOut << f << '\t' << std::fixed << std::setprecision(6) << t << '\t'
                    << spellVisualFX->getActiveEffectCount() << '\t' << spellVisualFX->missileCount() << '\t'
                    << animatedNode->getAnimator().getCurrentAnimation() << '\n';
        if (!activeSourceEmitters.empty()) {
            // The emitter origin = the slot's attach point on the caster:
            // the hand/head bone for a pseudo-id (B1144), the node origin for
            // a plain-entity slot. Screen space through the same pinhole the
            // scene.tsv boxes use.
            animatedNode->forceAnimationUpdate();
            const Pinhole ph = makePinhole(camera->getPosition(), camera->getTarget(), camera->getFOV(),
                                           camera->getAspectRatio(),
                                           static_cast<float>(harnessOpts.width),
                                           static_cast<float>(harnessOpts.height));
            for (const uint16_t fxId : activeSourceEmitters) {
                const char* kind = "entity";
                core::vector3df wp(animatedNode->getAbsolutePosition());
                if (EQ::SpellVisualFX::isHandBoneId(fxId)) {
                    kind = EQ::SpellVisualFX::isRightHand(fxId) ? "RHAND" : "LHAND";
                    resolveSpellFxBonePosition(animatedNode, fxId, wp);
                } else if (EQ::SpellVisualFX::isHeadBoneId(fxId)) {
                    kind = "HEAD";
                    resolveSpellFxBonePosition(animatedNode, fxId, wp);
                }
                const core::vector2df s = ph.project(wp);
                emitterOut << f << '\t' << std::fixed << std::setprecision(6) << t << '\t' << fxId << '\t'
                           << kind << '\t' << std::setprecision(3) << wp.X << '\t' << wp.Y << '\t' << wp.Z
                           << '\t' << std::setprecision(1) << s.X << '\t' << s.Y << '\n';
            }
        }
    }
    std::printf("[fx-harness] wrote %d frames at %d fps (simulated at %d Hz) to %s\n", frames, harnessOpts.fps,
                simHz, harnessOpts.framesDir.c_str());
    return 0;
}
