// tools/model_viewer/model_viewer_fx_harness.cpp -- B1138 (#458): the fable-side
// spell FX rig behind `model_viewer --spell-test`. See the header.

#include "model_viewer_fx_harness.h"
#include "model_viewer_state.h"

#include "client/bridge/spell_cast_fx.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/data_paths.h"
#include "client/graphics/first_person_camera.h"
#include "client/graphics/spell_fx_bone_anchor.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/spell_effects_config.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_map>

HarnessOptions harnessOpts;

namespace {

using Clock = eqt::bridge::CastGlowRoute::Clock;

std::unique_ptr<ConstrainedTextureCache> fxTextureCache;
EQE1Loader fxPack;
EQE1TextureIndex fxIndex;
std::vector<std::unique_ptr<EQE1Loader>> fxArchives;
EQT::Graphics::SimulationWorker fxWorker;
EQT::Graphics::SimulationOutput fxOutput;
std::unordered_map<uint16_t, glm::vec3> fxPendingPositions;
std::unordered_map<uint16_t, glm::vec3> fxPendingDirections;
eqt::bridge::CastGlowRoute fxRoute;
video::ITexture* fxBackdrop = nullptr;

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

// B1118 tour framing (review/B1118/README.md, review/B1132/gate): chase
// camera `az 180 pitch 12`, i.e. in front of the caster looking back at her,
// 12 degrees down; the caster's feet at (640,605) and 235 px head top to feet
// on a 1280x800 grab. The distance and the look-at height are FITTED here to
// those two pixel facts with the client's projection (62.8 degree horizontal
// FOV, T466); the fit is printed so the report can quote it.
struct PresetGeometry {
    float pitchDeg = 12.0f;
    float feetX = 640.0f, feetY = 605.0f, casterPx = 235.0f;
};

void fitFaceOnCamera(const PresetGeometry& g) {
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
    core::vector3df eye, look;
    pose(dist, lookH, eye, look);
    camera = smgr->addCameraSceneNode(nullptr, eye, look);
    camera->setAspectRatio(aspect);
    camera->setFOV(fovY);
    camera->setNearValue(fpTunable(FpTunable::NearPlaneUnits));
    const bool targetWasVisible = targetAnimatedNode && targetAnimatedNode->isVisible();
    if (targetAnimatedNode) targetAnimatedNode->setVisible(false);
    const video::SColor clear(255, 50, 50, 80);
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
        std::printf("[fx-harness]   fit %d: dist %.3f look %.3f -> rows %d..%d centroid %.1f\n", iter, dist,
                    lookH, top, bottom, cx);
        const float px = static_cast<float>(bottom - top + 1);
        const float unitsPerPx = dist * std::tan(fovY * 0.5f) / (h * 0.5f);
        if (std::fabs(px - g.casterPx) < 0.5f && std::fabs(bottom - g.feetY) < 0.5f) break;
        // the eye moves with the look point, so raising both moves the figure DOWN
        lookH += (g.feetY - static_cast<float>(bottom)) * unitsPerPx;
        dist *= px / g.casterPx;
    }
    if (targetAnimatedNode) targetAnimatedNode->setVisible(targetWasVisible);
    std::printf("[fx-harness] camera b1118: dist %.3f look %.3f above the pose-box feet, pitch %.1f, "
                "pose-box height %.3f -> rendered caster rows %d..%d (%d px, feet row %d), "
                "column centroid %.1f\n",
                dist, lookH, g.pitchDeg, head.Y - feet.Y, top, bottom, bottom - top + 1, bottom, cx);
    std::fflush(stdout);
}

// scene.tsv: each visible entity's screen box (its pose bounding box's eight
// corners projected through the frame's camera), so a consumer -- the
// FxHarness ctest, an ROI script -- knows where the caster and the target are
// drawn without re-deriving the projection.
void writeSceneBoxes(const std::string& path) {
    std::ofstream f(path);
    f << "entity\tid\tx0\ty0\tx1\ty1\n";
    auto* coll = smgr->getSceneCollisionManager();
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
            const core::position2di s = coll->getScreenCoordinatesFrom3DPosition(p, camera);
            x0 = std::min(x0, s.X); y0 = std::min(y0, s.Y);
            x1 = std::max(x1, s.X); y1 = std::max(y1, s.Y);
        }
        f << name << '\t' << (node == animatedNode ? casterEntityId : targetEntityId) << '\t' << x0 << '\t'
          << y0 << '\t' << x1 << '\t' << y1 << '\n';
    }
}

const EQ::SpellData* spellOrNull(uint32_t id) {
    return spellDatabase ? spellDatabase->getSpell(id) : nullptr;
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
        if (c.size() < 4 || c.size() > 5) {
            err = path + ":" + std::to_string(n) + ": want t_begin_s, spell_id, self|other, distance_units[, cast_ms]";
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
            if (c.size() == 5 && !c[4].empty() && c[4] != "-") hc.castMsOverride = std::stoi(c[4]);
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
    if (harnessOpts.cameraPreset != "b1118") return;
    harnessOpts.width = 1280;
    harnessOpts.height = 800;
    // char 67 Testcaster (lane 5): race 6 dark elf, gender 1 (fable.log
    // "Updating appearance for entity 258 (Testcaster) to race 6 gender 1"),
    // server size 6 (buildEntityMesh[Testcaster] ... (6.00/6) = 1.00).
    if (!harnessOpts.casterGiven) {
        harnessOpts.casterRace = 6;
        harnessOpts.casterGender = 1;
        harnessOpts.casterSize = 6.0f;
    }
    // `#spawn TrollF`: eqemu's default #spawn is a level-1 human male
    // (the leg's chat: "Race | Human (1)", "Gender | Male (0)").
    if (!harnessOpts.targetGiven) {
        harnessOpts.targetRace = 1;
        harnessOpts.targetGender = 0;
        harnessOpts.targetSize = 6.0f;
    }
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
        if (id == targetEntityId) return targetAnimatedNode;
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

void harnessBeginCast(const EQ::SpellData& spell, bool self, uint32_t castMs, Clock::time_point now) {
    eqt::state::SpellCastVisualStartedData d{};
    d.casterId = casterEntityId;
    d.spellId = spell.id;
    d.castTimeMs = castMs;
    d.spellName = spell.name;
    d.isPlayerCast = true;
    d.isTargetCast = false;
    d.spellVisual = spell.visualVocabulary();
    (void)self;
    eqt::bridge::ApplySpellCastStartedFx(fxRoute, spellVisualFX.get(), d, now);
}

void harnessAction(const EQ::SpellData& spell, bool self, Clock::time_point now) {
    eqt::state::SpellCastVisualCompleteData d{};
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
    targetAnimatedNode->setVisible(!self);
    if (self) return;
    const core::vector3df right = harnessOpts.cameraPreset.empty() ? core::vector3df(1, 0, 0)
                                                                   : targetRightOfCaster();
    targetAnimatedNode->setPosition(animatedNode->getPosition() + right * distance);
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

void setupSpellTestCamera() {
    // Scripted mode freezes the scene clock BEFORE the preset's fit renders
    // anything: drawAll() animates every node off the device timer, and a
    // wall-clock pose in the fit's frames would leave the idle animation at a
    // different phase on every run.
    if (!harnessOpts.planPath.empty()) {
        device->getTimer()->stop();
        device->getTimer()->setTime(1000);
    }
    if (harnessOpts.cameraPreset == "b1118") {
        PresetGeometry g;
        g.casterPx = harnessOpts.casterPx;
        fitFaceOnCamera(g);
        if (harnessOpts.ground) addGroundPlane();
        return;
    }
    // The interactive framing: the midpoint between caster (0,0,0) and target.
    const float mid = harnessOpts.interactiveDistance * 0.5f;
    camera = smgr->addCameraSceneNode(nullptr, core::vector3df(mid, 3.0f, -10.0f),
                                      core::vector3df(mid, 1.5f, 0));
}

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
        events.push_back({plan[i].tBegin, static_cast<int>(i), false, castMs});
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
    timing << "frame\tt\tevent\tspell\ttarget\tdistance\tcast_ms\tt_event\n";

    if (!harnessOpts.backdropPath.empty()) {
        fxBackdrop = driver->getTexture(harnessOpts.backdropPath.c_str());
        if (!fxBackdrop) std::fprintf(stderr, "[fx-harness] backdrop %s did not load\n", harnessOpts.backdropPath.c_str());
    }
    const double dt = 1.0 / harnessOpts.fps;
    const int frames = static_cast<int>(std::lround(harnessOpts.seconds * harnessOpts.fps));
    const Clock::time_point epoch{};  // the scripted clock: no wall time anywhere
    size_t next = 0;
    placeHarnessTarget(plan[0].self, plan[0].distance);
    device->getTimer()->stop();  // (already stopped by setupSpellTestCamera)
    for (int f = 0; f < frames; ++f) {
        if (!device->run()) break;
        const double t = f * dt;
        while (next < events.size() && events[next].t <= t + 1e-9) {
            const Event& e = events[next++];
            const HarnessCast& c = plan[e.cast];
            const EQ::SpellData& sp = *spellOrNull(c.spellId);
            const auto now = epoch + std::chrono::microseconds(std::llround(e.t * 1e6));
            if (!e.action) {
                placeHarnessTarget(c.self, c.distance);
                harnessBeginCast(sp, c.self, e.castMs, now);
            } else {
                harnessAction(sp, c.self, now);
            }
            char row[256];
            std::snprintf(row, sizeof(row), "%d\t%.6f\t%s\t%u\t%s\t%.3f\t%u\t%.6f\n", f, t,
                          e.action ? "Action" : "BeginCast", c.spellId, c.self ? "self" : "other",
                          c.distance, e.castMs, e.t);
            timing << row;
        }
        // The scene's own clock: drawAll() animates every node off the device
        // timer, so the timer is stopped and set to the scripted time (+1 s:
        // EQAnimatedMeshSceneNode reads lastTimeMs_ == 0 as "first frame").
        const u32 ms = 1000u + static_cast<u32>(std::llround(t * 1000.0));
        device->getTimer()->setTime(ms);
        animatedNode->OnAnimate(ms);
        if (targetAnimatedNode) targetAnimatedNode->OnAnimate(ms);
        spellVisualFX->update(static_cast<float>(dt));
        stepSpellParticles(static_cast<float>(dt), harnessOpts.seed);
        uploadHarnessTextures();

        driver->beginScene(true, true, video::SColor(255, 50, 50, 80));
        if (fxBackdrop) {
            const auto ts = fxBackdrop->getOriginalSize();
            driver->draw2DImage(fxBackdrop, core::rect<s32>(0, 0, harnessOpts.width, harnessOpts.height),
                                core::rect<s32>(0, 0, ts.Width, ts.Height));
        }
        smgr->drawAll();
        renderSpellParticles();
        driver->endScene();

        video::IImage* shot = driver->createScreenShot();
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%05d.png", f);
        const bool ok = shot && driver->writeImageToFile(shot, (harnessOpts.framesDir + name).c_str());
        if (shot) shot->drop();
        if (ok && f == 0) writeSceneBoxes(harnessOpts.framesDir + "/scene.tsv");
        if (!ok) {
            std::fprintf(stderr, "[fx-harness] frame %d did not write\n", f);
            return 4;
        }
        char row[96];
        std::snprintf(row, sizeof(row), "%d\t%.6f\t-\t-\t-\t-\t-\t-\n", f, t);
        timing << row;
    }
    std::printf("[fx-harness] wrote %d frames at %d fps to %s\n", frames, harnessOpts.fps,
                harnessOpts.framesDir.c_str());
    return 0;
}
