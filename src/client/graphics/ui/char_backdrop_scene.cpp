// B888-CC3 (GitLab #204) -- the 3D clz backdrop scene (the "3D slice"). What it
// is and the coordinate convention: include/client/graphics/ui/char_backdrop_scene.h.

#include "client/graphics/ui/char_backdrop_scene.h"

#include "client/data_paths.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/explore_screen.h"  // ExploreCameraVerb full definition
#include "common/logging.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace eqt {
namespace ui {

namespace {

constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
constexpr float kPiOver180 = 3.14159265358979323846f / 180.0f;
// A row's character-stand node: a small upright box on the pedestal (the click
// target). Its exact height/placement is ASSUMED (the README's "Left open"
// lists the per-row positions as assumed, not measured); it is a click target,
// not a skinned character (the race loader's character is the follow-up seam).
constexpr float kCharStandH = 6.0f;
constexpr float kCharStandW = 1.5f;
// The free camera's starting orbit state, looking into the showroom (the arch
// and central platform sit near the origin). The exact vantage is ASSUMED --
// the measured fact is only that the camera moves on a held key (forward),
// not where it starts.
constexpr float kCamElevationDeg0 = 10.0f;
constexpr float kCamDistance0 = 26.0f;
constexpr float kForwardSpeed = 12.0f;   // world units / s (assumed default)
constexpr float kStrafeSpeed = 12.0f;
constexpr float kOrbitSpeed = 1.0f;      // deg per rad of verb (the verb is radians)
// A click is a selection only when it lands within this many pixels of a
// stand's projected center (a backdrop hit is not a selection; a far miss keeps
// the cursor, the machine's own rule).
constexpr float kClickRadiusPx = 48.0f;

// Build a mesh from a parsed geometry: one buffer per texture (the
// equipment_model_loader idiom -- concrete SMesh/SMeshBuffer, per-textureIndex
// buffers, Vertices/Indices pushed by value). Vertices stay RAW local (Z-up);
// the node's rotation does the Z-up->Y-up swap.
irr::scene::IMesh* buildMesh(irr::video::IVideoDriver* driver,
                             const EQT::Graphics::ClzGeometry& g,
                             const std::unordered_map<std::string, std::vector<char>>& texData,
                             std::unordered_map<std::string, irr::video::ITexture*>& texCache) {
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    std::unordered_map<uint32_t, std::vector<uint32_t>> byMat;
    for (uint32_t t = 0; t < g.triangleCount; ++t) {
        byMat[g.triangleMaterials[t]].push_back(t);
    }
    for (const auto& [matId, tris] : byMat) {
        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();
        std::unordered_map<uint32_t, uint32_t> remap;
        for (uint32_t t : tris) {
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t vi = g.triangleVerts[static_cast<size_t>(t) * 3 + k];
                if (vi >= g.vertexCount || remap.count(vi)) continue;
                // Vertices stay RAW local (Z-up). The Z-up->Y-up swap and the
                // placement rotation are both carried by the NODE's rotation
                // (applyPlacement), which composes placement * zupToYup -- the
                // reference importer's own order. Swapping here would be wrong:
                // the placement rotation must act on the raw (pre-swap)
                // coordinates.
                irr::video::S3DVertex v;
                v.Pos = irr::core::vector3df(g.positions[static_cast<size_t>(vi) * 3 + 0],
                                             g.positions[static_cast<size_t>(vi) * 3 + 1],
                                             g.positions[static_cast<size_t>(vi) * 3 + 2]);
                v.Normal = irr::core::vector3df(g.normals[static_cast<size_t>(vi) * 3 + 0],
                                                g.normals[static_cast<size_t>(vi) * 3 + 1],
                                                g.normals[static_cast<size_t>(vi) * 3 + 2]);
                v.TCoords = irr::core::vector2df(g.uvs[static_cast<size_t>(vi) * 2 + 0],
                                                 g.uvs[static_cast<size_t>(vi) * 2 + 1]);
                v.Color = irr::video::SColor(255, 255, 255, 255);
                remap[vi] = static_cast<uint32_t>(buffer->Vertices.size());
                buffer->Vertices.push_back(v);
            }
        }
        for (uint32_t t : tris) {
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t vi = g.triangleVerts[static_cast<size_t>(t) * 3 + k];
                auto it = remap.find(vi);
                if (it != remap.end()) buffer->Indices.push_back(static_cast<irr::u16>(it->second));
            }
        }
        buffer->recalculateBoundingBox();
        irr::video::SMaterial& mat = buffer->Material;
        mat.Lighting = true;
        mat.BackfaceCulling = false;
        mat.MaterialType = irr::video::EMT_SOLID;
        if (driver && matId < g.textureNames.size() && !g.textureNames[matId].empty()) {
            const std::string& texName = g.textureNames[matId];
            auto it = texCache.find(texName);
            irr::video::ITexture* tex = (it != texCache.end()) ? it->second : nullptr;
            if (!tex && texData.count(texName)) {
                const auto& d = texData.at(texName);
                // Non-const: createImageFromData takes a non-const void* and
                // copies (ownForeignMemory=false), so no lifetime is needed.
                EQT::Graphics::DecodedImage img = EQT::Graphics::DDSDecoder::decode(d);
                if (img.isValid()) {
                    void* pixels = img.pixels.data();
                    irr::video::IImage* image =
                        driver->createImageFromData(irr::video::ECF_A8R8G8B8,
                                                    irr::core::dimension2d<irr::u32>(img.width, img.height),
                                                    pixels, false);
                    if (image) {
                        tex = driver->addTexture(texName.c_str(), image);
                        image->drop();
                    }
                }
            }
            if (tex) {
                mat.setTexture(0, tex);
                texCache[texName] = tex;
            }
        }
        mesh->addMeshBuffer(buffer);
        buffer->drop();
    }
    mesh->recalculateBoundingBox();
    return mesh;
}

// A plain unit box mesh (the character stand): 8 vertices, 12 triangles.
irr::scene::IMesh* buildBoxMesh(float w, float h, float d, irr::video::SColor color) {
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    const float x0 = -w / 2, x1 = w / 2, y0 = 0, y1 = h, z0 = -d / 2, z1 = d / 2;
    const float P[8][3] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
    };
    const irr::u16 F[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {2, 6, 7}, {2, 7, 3},
        {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5},
    };
    for (int i = 0; i < 8; ++i) {
        irr::video::S3DVertex v;
        v.Pos = irr::core::vector3df(P[i][0], P[i][1], P[i][2]);
        v.Color = color;
        buf->Vertices.push_back(v);
    }
    for (int t = 0; t < 12; ++t) {
        buf->Indices.push_back(F[t][0]);
        buf->Indices.push_back(F[t][1]);
        buf->Indices.push_back(F[t][2]);
    }
    buf->recalculateBoundingBox();
    buf->Material.Lighting = true;
    buf->Material.MaterialType = irr::video::EMT_SOLID;
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();
    return mesh;
}

// The node's transform: the placement rotation (about the model's local axes)
// FOLLOWED BY the Z-up->Y-up swap. Irrlicht composes a node's
// setRotation/setPosition as Rotation * Translation, and setRotationDegrees
// (x,y,z) builds R_y(y)*R_x(x)*R_z(z) (the reference importer's own order), so
// the node's rotation = placementRotation * zupToYup, applied to a raw local
// vertex v gives placeRot * zupToYup * v. zupToYup sends (x,y,z)->(x,z,y)
// (local z = world y = up). The translation is the raw placement position.
// The Z-up->Y-up matrix is built element-by-element (this Irrlicht fork has no
// 16-arg matrix constructor), then multiplied in with the placement rotation.
void applyPlacement(irr::scene::ISceneNode* node, const EQT::Graphics::ClzPlacement& pl) {
    irr::core::matrix4 zupToYup;  // identity by default
    // Rows of the (x,y,z)->(x,z,y) map: x'=x, y'=z, z'=y.
    zupToYup(0, 0) = 1; zupToYup(0, 1) = 0; zupToYup(0, 2) = 0;
    zupToYup(1, 0) = 0; zupToYup(1, 1) = 0; zupToYup(1, 2) = 1;
    zupToYup(2, 0) = 0; zupToYup(2, 1) = 1; zupToYup(2, 2) = 0;
    irr::core::matrix4 place;
    place.setRotationDegrees(irr::core::vector3df(pl.rx * kRadToDeg, pl.ry * kRadToDeg, pl.rz * kRadToDeg));
    irr::core::matrix4 m = place * zupToYup;
    node->setPosition(irr::core::vector3df(pl.x, pl.y, pl.z));
    node->setRotation(m.getRotationDegrees());
}

}  // namespace

CharBackdropScene::CharBackdropScene() = default;
CharBackdropScene::~CharBackdropScene() { clear(); }

namespace {
// WILLEQ_CLZ overrides the shipped path (the test's seam); otherwise the
// client-tree copy (data/eq_client/clz.eqg, resolved like every other
// shipped file) is the first one that exists. The absolute candidate is the
// dev VM's P1999 root, where the archive ships at the client tree's top
// level.
std::string clzPath() {
    const char* env = std::getenv("WILLEQ_CLZ");
    if (env && *env) return env;
    const std::string candidates[] = {
        "/mnt/projects/EverQuestP1999/clz.eqg",
        eqt::ResolveDataPath("data/eq_client/clz.eqg"),
    };
    for (const auto& c : candidates) {
        if (!c.empty() && std::filesystem::exists(c)) return c;
    }
    return "";
}
}  // namespace

bool CharBackdropScene::init(EQT::Graphics::IrrlichtRenderer& renderer) {
    return init(renderer.getSceneManager(), renderer.getDriver());
}

bool CharBackdropScene::init(irr::scene::ISceneManager* mainSmgr, irr::video::IVideoDriver* driver) {
    if (scene_) return true;  // idempotent
    if (!mainSmgr || !driver) return false;  // pre-init: the host retries next frame
    const std::string path = clzPath();
    if (path.empty()) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz.eqg not found (WILLEQ_CLZ unset)");
        return false;
    }
    if (!archive_.open(path)) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz.eqg failed to open: {}", path);
        return false;
    }
    if (!zone_.loadZone(archive_)) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz zone failed to parse: {}", zone_.error);
        archive_.close();
        return false;
    }
    // The CC-2 idiom: an OWN scene manager (the zone path's manager is the
    // production one and this must not touch its graph); the driver is
    // borrowed for image/texture upload.
    scene_ = mainSmgr->createNewSceneManager(false);
    driver_ = driver;
    if (!scene_) {
        archive_.close();
        return false;
    }
    root_ = scene_->addEmptySceneNode();
    // The free camera, at the showroom vantage, looking into the room (the
    // scene's own, not the pre-zone frame's active camera).
    camera_ = scene_->addCameraSceneNode(nullptr, camPos_, camTarget_, -1, false);
    if (camera_) {
        camera_->setTarget(camTarget_);
        camera_->setRotation(irr::core::vector3df(camElevationDeg_, camAzimuthDeg_, 0.0f));
        camera_->setAspectRatio(1280.0f / 1024.0f);
    }
    // The showroom's lights (three-point, the CharacterModelView idiom).
    scene_->setAmbientLight(irr::video::SColorf(0.24f, 0.24f, 0.31f, 1.0f));
    irr::scene::ILightSceneNode* key = scene_->addLightSceneNode(
        nullptr, irr::core::vector3df(100, 300, 100), irr::video::SColorf(1.0f, 1.0f, 1.0f), 300.0f);
    irr::scene::ILightSceneNode* fill = scene_->addLightSceneNode(
        nullptr, irr::core::vector3df(-100, 100, -100), irr::video::SColorf(0.47f, 0.47f, 0.63f), 300.0f);
    if (key) key->getLightData().Type = irr::video::ELT_POINT;
    if (fill) fill->getLightData().Type = irr::video::ELT_POINT;
    // One mesh per distinct model, placed per placement. The terrain is model 0.
    for (const auto& pl : zone_.placements) {
        if (pl.modelIndex < 0 || pl.modelIndex >= static_cast<int32_t>(zone_.modelNames.size())) {
            continue;
        }
        const std::string& model = zone_.modelNames[static_cast<size_t>(pl.modelIndex)];
        irr::scene::IMesh* mesh = nullptr;
        auto it = meshes_.find(model);
        if (it != meshes_.end()) {
            mesh = it->second;
        } else {
            EQT::Graphics::ClzGeometry g;
            if (zone_.loadModel(archive_, static_cast<size_t>(pl.modelIndex), g) && g.valid()) {
                for (const auto& tn : g.textureNames) {
                    if (tn.empty() || texData_.count(tn)) continue;
                    std::vector<char> d;
                    if (archive_.get(tn, d)) texData_[tn] = std::move(d);
                }
                mesh = buildMesh(driver_, g, texData_, texCache_);
                meshes_[model] = mesh;
            } else {
                LOG_WARN(MOD_GRAPHICS, "[BACKDROP] model {} failed: {}", model, zone_.error);
            }
        }
        if (!mesh) continue;
        irr::scene::ISceneNode* node = scene_->addMeshSceneNode(mesh, root_);
        if (node) {
            applyPlacement(node, pl);
            backdropNodes_.push_back(node);
        }
    }
    LOG_INFO(MOD_GRAPHICS, "[BACKDROP] clz showroom up: {} models, {} placed nodes",
             meshes_.size(), backdropNodes_.size());
    return !backdropNodes_.empty();
}

void CharBackdropScene::clear() {
    if (scene_) {
        for (auto* n : backdropNodes_) {
            if (n) n->remove();
        }
        for (auto* n : charNodes_) {
            if (n) n->remove();
        }
        if (camera_) camera_->remove();
        if (root_) root_->remove();
    }
    backdropNodes_.clear();
    charNodes_.clear();
    camera_ = nullptr;
    root_ = nullptr;
    if (scene_) {
        scene_->drop();  // drops its nodes/meshes/lights
        scene_ = nullptr;
    }
    texCache_.clear();  // the textures are the DRIVER's (the borrowed
    // driver's cache owns and deletes them on device teardown); dropping
    // them here would be a double-free in that teardown.
    meshes_.clear();  // the scene manager's drop destroyed them
    texData_.clear();
    archive_.close();
}

void CharBackdropScene::update(const EQT::Graphics::ExploreCameraVerb& verb, float dtMs) {
    if (!camera_) return;
    if (!verb.active()) return;
    const float dt = dtMs / 1000.0f;
    // The camera's axes: look = toTarget. The right axis (horizontal,
    // perpendicular to the look) is cross(look, worldUp) = (-look.Z, 0,
    // look.X). Forward moves the camera along -look (toward the target);
    // strafe along the right axis.
    const irr::core::vector3df toTarget = camTarget_ - camPos_;
    const float toLen2 = toTarget.X * toTarget.X + toTarget.Y * toTarget.Y + toTarget.Z * toTarget.Z;
    irr::core::vector3df look(0.0f, 0.0f, -1.0f);
    if (toLen2 > 1e-9f) {
        const float inv = 1.0f / std::sqrt(toLen2);
        look = irr::core::vector3df(toTarget.X * inv, toTarget.Y * inv, toTarget.Z * inv);
    }
    const irr::core::vector3df rgt(-look.Z, 0.0f, look.X);
    if (verb.forward != 0.0f) {
        camPos_.X += -look.X * verb.forward * kForwardSpeed * dt;
        camPos_.Y += -look.Y * verb.forward * kForwardSpeed * dt;
        camPos_.Z += -look.Z * verb.forward * kForwardSpeed * dt;
    }
    if (verb.strafe != 0.0f) {
        camPos_.X += rgt.X * verb.strafe * kStrafeSpeed * dt;
        camPos_.Z += rgt.Z * verb.strafe * kStrafeSpeed * dt;
    }
    if (verb.orbitX != 0.0f) camAzimuthDeg_ += verb.orbitX * kOrbitSpeed * kRadToDeg;
    if (verb.orbitY != 0.0f) {
        camElevationDeg_ = std::max(-89.0f, std::min(89.0f, camElevationDeg_ + verb.orbitY * kOrbitSpeed * kRadToDeg));
    }
    if (verb.zoom != 0.0f) {
        camDistance_ = std::max(3.0f, camDistance_ * (1.0f - verb.zoom * 0.05f));
    }
    // Re-derive the orbit state from where we now sit (forward/strafe moved
    // the camera; that motion is re-expressed as azimuth/elevation/distance
    // toward the target), then re-derive the position from it.
    if (verb.forward != 0.0f || verb.strafe != 0.0f) {
        const irr::core::vector3df d = camPos_ - camTarget_;
        camDistance_ = std::max(3.0f, std::sqrt(d.X * d.X + d.Y * d.Y + d.Z * d.Z));
        camAzimuthDeg_ = std::atan2(d.X, d.Z) * kRadToDeg;
        camElevationDeg_ = std::atan2(d.Y, std::sqrt(d.X * d.X + d.Z * d.Z)) * kRadToDeg;
    }
    const float azRad = camAzimuthDeg_ * kPiOver180;
    const float elRad = camElevationDeg_ * kPiOver180;
    camPos_.X = camTarget_.X + camDistance_ * std::cos(elRad) * std::sin(azRad);
    camPos_.Y = camTarget_.Y + camDistance_ * std::sin(elRad);
    camPos_.Z = camTarget_.Z + camDistance_ * std::cos(elRad) * std::cos(azRad);
    camera_->setPosition(camPos_);
    camera_->setTarget(camTarget_);
    camera_->setRotation(irr::core::vector3df(camElevationDeg_, camAzimuthDeg_, 0.0f));
}

void CharBackdropScene::setCharacter(int row, uint16_t, uint8_t, const EQT::Graphics::EntityAppearance&) {
    if (!root_ || row < 0) return;
    clearCharacter(row);
    // A click stand: a small upright box on the pedestal. The exact per-row
    // position is assumed (README "Left open"); the race loader's skinned
    // character is the follow-up seam, not this slice.
    irr::scene::IMesh* box = buildBoxMesh(kCharStandW, kCharStandH, kCharStandW,
                                          irr::video::SColor(255, 220, 120, 120));
    irr::scene::ISceneNode* node = scene_->addMeshSceneNode(box, root_);
    box->drop();
    if (node) {
        // The central pedestal is row 1; the side pedestals are rows 0 and 2.
        const float x = static_cast<float>(row - 1) * 6.0f;
        node->setPosition(irr::core::vector3df(x, 0.0f, 0.0f));
        if (static_cast<size_t>(row) >= charNodes_.size()) charNodes_.resize(static_cast<size_t>(row) + 1);
        charNodes_[row] = node;
    }
}

void CharBackdropScene::clearCharacter(int row) {
    if (row < 0 || static_cast<size_t>(row) >= charNodes_.size()) return;
    if (charNodes_[row]) {
        charNodes_[row]->remove();
        charNodes_[row] = nullptr;
    }
}

int CharBackdropScene::characterCount() const {
    int n = 0;
    for (auto* nd : charNodes_) {
        if (nd) ++n;
    }
    return n;
}

int CharBackdropScene::rowForClick(int sx, int y) {
    if (!camera_ || !driver_) return -1;
    // Project each stand's center to screen space and pick the nearest to the
    // click (a backdrop hit is NOT a selection; a far miss keeps the cursor,
    // the machine's rule). The projection is name_tag_batch's: the ONE
    // projection x view product, multiplyWith1x4Matrix, divide by the fourth
    // (w) component, NDC -> pixel.
    const irr::core::rect<irr::s32>& vp = driver_->getViewPort();
    const irr::f32 halfW = static_cast<irr::f32>(vp.getWidth()) * 0.5f;
    const irr::f32 halfH = static_cast<irr::f32>(vp.getHeight()) * 0.5f;
    irr::core::matrix4 projView = camera_->getProjectionMatrix();
    projView *= camera_->getViewMatrix();
    int best = -1;
    float bestDist = 1e30f;
    for (size_t row = 0; row < charNodes_.size(); ++row) {
        irr::scene::ISceneNode* node = charNodes_[row];
        if (!node) continue;
        const irr::core::vector3df c =
            node->getAbsolutePosition() + irr::core::vector3df(0, kCharStandH * 0.5f, 0);
        irr::f32 p[4] = {c.X, c.Y, c.Z, 1.0f};
        projView.multiplyWith1x4Matrix(p);
        if (p[3] < 0.0f) continue;  // behind the camera
        const irr::f32 zDiv = p[3] == 0.0f ? 1.0f : irr::core::reciprocal(p[3]);
        const float px = halfW + halfW * (p[0] * zDiv);
        const float py = halfH - halfH * (p[1] * zDiv);
        if (px < 0 || px > vp.getWidth() || py < 0 || py > vp.getHeight()) continue;
        const float dist = (px - sx) * (px - sx) + (py - y) * (py - y);
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<int>(row);
        }
    }
    if (best >= 0 && bestDist > kClickRadiusPx * kClickRadiusPx) return -1;
    return best;
}

void CharBackdropScene::renderScene(EQT::Graphics::IrrlichtRenderer& renderer) {
    renderScene(renderer.getDriver());
}

void CharBackdropScene::renderScene(irr::video::IVideoDriver* driver) {
    if (!scene_ || !camera_) return;  // not stood up
    if (!driver) return;
    // The camera's own projection aspect must match what is actually on
    // screen (the host draws the backdrop full-window; the headless test's
    // null viewport is 64x64). The camera's matrices are built by its
    // render() during drawAll -- which also means the first click projection
    // is valid only after the first frame, exactly as in the host (a click is
    // a post-frame event).
    const irr::core::rect<irr::s32>& vp = driver->getViewPort();
    if (vp.getHeight() > 0) {
        camera_->setAspectRatio(static_cast<irr::f32>(vp.getWidth()) /
                                static_cast<irr::f32>(vp.getHeight()));
    }
    // The scene's own camera renders the showroom; the host's UI draws over it
    // in the same frame (the oracle's layering: 3D select scene, then the
    // UI). The pre-zone frame's active camera is swapped back, so the rest of
    // the frame (the UI is 2D) and the next zone are untouched.
    prevActiveCamera_ = scene_->getActiveCamera();
    scene_->setActiveCamera(camera_);
    scene_->drawAll();
    scene_->setActiveCamera(prevActiveCamera_);
    prevActiveCamera_ = nullptr;
}

}  // namespace ui
}  // namespace eqt
