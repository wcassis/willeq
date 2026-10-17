// B888-CC3 (GitLab #204) -- the 3D clz backdrop scene (the "3D slice"). What it
// is and the coordinate convention: include/client/graphics/ui/char_backdrop_scene.h.

#include "client/graphics/ui/char_backdrop_scene.h"

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
constexpr float kPi = 3.14159265358979323846f;
// A row's character-stand node: a small upright box on the pedestal (the click
// target). Its exact height/placement is ASSUMED (the README's "Left open" lists
// the per-row positions as assumed, not measured); it is a click target, not a
// skinned character (the race loader's character is the follow-up seam).
constexpr float kCharStandH = 6.0f;
constexpr float kCharStandW = 1.5f;
// The free camera's starting vantage, looking into the showroom (the arch and
// central platform sit near the origin). The exact vantage is ASSUMED -- the
// measured fact is only that the camera moves on a held key (forward), not
// where it starts.
constexpr float kCamX = 0.0f, kCamY = 8.0f, kCamZ = 26.0f;
constexpr float kTargetX = 0.0f, kTargetY = 2.0f, kTargetZ = 0.0f;
constexpr float kForwardSpeed = 12.0f;   // world units / s (assumed default)
constexpr float kStrafeSpeed = 12.0f;
constexpr float kOrbitSpeed = 1.0f;      // deg / rad of verb (the verb is radians)
constexpr float kZoomSpeed = 6.0f;       // fov deg / (verb unit)
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
                // (applyPlacement), which composes rot_change * placement -- the
                // reference importer's own order (Rz*ry * R_y*rx * R_x*rz *
                // rot_change). Swapping here would be wrong: the placement
                // rotation must act on the raw (pre-swap) coordinates.
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
                    irr::video::IImage* image =
                        driver->createImageFromData(irr::video::ECF_A8R8G8B8,
                                                    irr::core::dimension2d<irr::u32>(img.width, img.height),
                                                    img.pixels.data(), false);
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

// The node's transform: the placement rotation (about the model's local axes,
// in world space) FOLLOWED BY the Z-up->Y-up swap. Irrlicht composes a node's
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
    irr::core::matrix4 m = zupToYup;
    irr::core::matrix4 place;
    place.setRotationDegrees(irr::core::vector3df(pl.rx * kRadToDeg, pl.ry * kRadToDeg, pl.rz * kRadToDeg));
    m = place * m;
    node->setPosition(irr::core::vector3df(pl.x, pl.y, pl.z));
    node->setRotation(m.getRotationDegrees());
}

}  // namespace

class CharBackdropScene::Impl {
public:
    static std::string clzPath();  // WILLEQ_CLZ overrides the shipped path
    bool init(EQT::Graphics::IrrlichtRenderer& renderer);
    void clear();
    void update(const EQT::Graphics::ExploreCameraVerb& verb, float dtMs);
    void setCharacter(int row, uint16_t race, uint8_t gender, const EQT::Graphics::EntityAppearance&);
    void clearCharacter(int row);
    int characterCount() const { return static_cast<int>(charNodes_.size()); }
    int nodeCount() const { return static_cast<int>(backdropNodes_.size()); }
    int rowForClick(int x, int y);

    irr::scene::ISceneManager* smgr = nullptr;
    irr::video::IVideoDriver* driver = nullptr;
    EQT::Graphics::PfsArchive archive;
    EQT::Graphics::ClzZone zone;
    std::unordered_map<std::string, std::vector<char>> texData;

    irr::scene::ISceneNode* root = nullptr;
    std::vector<irr::scene::ISceneNode*> backdropNodes_;
    std::unordered_map<std::string, irr::scene::IMesh*> meshes;  // model name -> shared mesh
    std::unordered_map<std::string, irr::video::ITexture*> texCache;
    std::vector<irr::scene::ISceneNode*> charNodes_;  // row -> node (sparse)
    irr::scene::ICameraSceneNode* camera = nullptr;
    // The free camera's orbit state (the fork's camera is target-based; there
    // is no moveForward/lookAt, so we own the azimuth/elevation/distance and
    // re-derive position and look each update).
    float camAzimuthDeg = 0.0f;
    float camElevationDeg = 10.0f;
    float camDistance = 26.0f;
    // Brace-init: the paren form (kTargetX, kTargetY, kTargetZ) is the
    // most-vexing-parse (GCC tries the function-declaration reading first),
    // so the camera state is brace-initialized.
    irr::core::vector3df camTarget{kTargetX, kTargetY, kTargetZ};
    irr::core::vector3df camPos{kCamX, kCamY, kCamZ};
};

std::string CharBackdropScene::Impl::clzPath() {
    const char* env = std::getenv("WILLEQ_CLZ");
    if (env && *env) return env;
    const std::string candidates[] = {"/mnt/projects/EverQuestP1999/clz.eqg", "data/eq_client/clz.eqg"};
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return "";
}

bool CharBackdropScene::Impl::init(EQT::Graphics::IrrlichtRenderer& renderer) {
    if (root) return true;  // idempotent
    smgr = renderer.getSceneManager();
    driver = renderer.getDriver();
    if (!smgr) return false;  // pre-init: the host retries next frame
    const std::string path = clzPath();
    if (path.empty()) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz.eqg not found (WILLEQ_CLZ unset)");
        return false;
    }
    if (!archive.open(path)) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz.eqg failed to open: {}", path);
        return false;
    }
    if (!zone.loadZone(archive)) {
        LOG_WARN(MOD_GRAPHICS, "[BACKDROP] clz zone failed to parse: {}", zone.error);
        return false;
    }
    root = smgr->addEmptySceneNode();
    // The free camera, at the showroom vantage, looking into the room.
    camera = smgr->addCameraSceneNode(nullptr, camPos, camTarget, -1, true);
    camera->setTarget(camTarget);
    camera->setRotation(irr::core::vector3df(camElevationDeg, camAzimuthDeg, 0.0f));
    // One mesh per distinct model, placed per placement. The terrain is model 0.
    for (const auto& pl : zone.placements) {
        if (pl.modelIndex < 0 || pl.modelIndex >= static_cast<int32_t>(zone.modelNames.size())) {
            continue;
        }
        const std::string& model = zone.modelNames[static_cast<size_t>(pl.modelIndex)];
        irr::scene::IMesh* mesh = nullptr;
        auto it = meshes.find(model);
        if (it != meshes.end()) {
            mesh = it->second;
        } else {
            EQT::Graphics::ClzGeometry g;
            if (zone.loadModel(archive, static_cast<size_t>(pl.modelIndex), g) && g.valid()) {
                for (const auto& tn : g.textureNames) {
                    if (tn.empty() || texData.count(tn)) continue;
                    std::vector<char> d;
                    if (archive.get(tn, d)) texData[tn] = std::move(d);
                }
                mesh = buildMesh(driver, g, texData, texCache);
                meshes[model] = mesh;
            } else {
                LOG_WARN(MOD_GRAPHICS, "[BACKDROP] model {} failed: {}", model, zone.error);
            }
        }
        if (!mesh) continue;
        irr::scene::ISceneNode* node = smgr->addMeshSceneNode(mesh, root);
        if (node) {
            applyPlacement(node, pl);
            backdropNodes_.push_back(node);
        }
    }
    LOG_INFO(MOD_GRAPHICS, "[BACKDROP] clz showroom up: {} models, {} placed nodes",
             meshes.size(), backdropNodes_.size());
    return !backdropNodes_.empty();
}

void CharBackdropScene::Impl::clear() {
    for (auto* n : backdropNodes_) if (n) n->remove();
    backdropNodes_.clear();
    for (auto* n : charNodes_) if (n) n->remove();
    charNodes_.clear();
    if (root) { root->remove(); root = nullptr; }
    if (camera) { camera->remove(); camera = nullptr; }
    for (auto& [name, m] : meshes) if (m) m->drop();
    meshes.clear();
    texData.clear();
    for (auto& [name, t] : texCache) if (t) t->drop();
    texCache.clear();
}

void CharBackdropScene::Impl::update(const EQT::Graphics::ExploreCameraVerb& verb, float dtMs) {
    if (!camera) return;
    if (!verb.active()) return;
    const float dt = dtMs / 1000.0f;
    const irr::core::vector3df target = camTarget;
    const irr::core::vector3df toTarget = target - camPos;
    const float toLen = toTarget.X * toTarget.X + toTarget.Y * toTarget.Y + toTarget.Z * toTarget.Z;
    // The camera's axes: look = toTarget. The right axis (horizontal,
    // perpendicular to the look) is cross(look, worldUp) = (-look.Z, 0,
    // look.X). Forward moves the camera along -look (toward the target);
    // strafe along the right axis.
    const irr::core::vector3df look(toLen > 1e-9f ? toTarget : irr::core::vector3df(0, 0, -1));
    const irr::core::vector3df rgt(-look.Z, 0.0f, look.X);
    if (verb.forward != 0.0f) {
        camPos.X += -look.X * verb.forward * kForwardSpeed * dt;
        camPos.Y += -look.Y * verb.forward * kForwardSpeed * dt;
        camPos.Z += -look.Z * verb.forward * kForwardSpeed * dt;
    }
    if (verb.strafe != 0.0f) {
        camPos.X += rgt.X * verb.strafe * kStrafeSpeed * dt;
        camPos.Z += rgt.Z * verb.strafe * kStrafeSpeed * dt;
    }
    if (verb.orbitX != 0.0f) camAzimuthDeg += verb.orbitX * kOrbitSpeed * kRadToDeg;
    if (verb.orbitY != 0.0f) {
        camElevationDeg = std::max(-89.0f, std::min(89.0f, camElevationDeg + verb.orbitY * kOrbitSpeed * kRadToDeg));
    }
    if (verb.zoom != 0.0f) {
        camDistance = std::max(3.0f, camDistance * (1.0f - verb.zoom * 0.05f));
    }
    // Re-derive position from the orbit state (orbit/zoom change where we sit;
    // forward/strafe already moved us, which is then re-expressed as a new
    // azimuth/elevation/distance toward the target).
    const irr::core::vector3df d = camPos - target;
    const float dist = std::sqrt(d.X * d.X + d.Y * d.Y + d.Z * d.Z);
    if (verb.forward != 0.0f || verb.strafe != 0.0f) {
        camDistance = dist;
        camAzimuthDeg = std::atan2(d.X, d.Z) * kRadToDeg;
        camElevationDeg = std::atan2(d.Y, std::sqrt(d.X * d.X + d.Z * d.Z)) * kRadToDeg;
    }
    const float azRad = camAzimuthDeg * 3.14159265358979323846f / 180.0f;
    const float elRad = camElevationDeg * 3.14159265358979323846f / 180.0f;
    camPos.X = target.X + camDistance * std::cos(elRad) * std::sin(azRad);
    camPos.Y = target.Y + camDistance * std::sin(elRad);
    camPos.Z = target.Z + camDistance * std::cos(elRad) * std::cos(azRad);
    camera->setPosition(camPos);
    camera->setTarget(target);
    camera->setRotation(irr::core::vector3df(camElevationDeg, camAzimuthDeg, 0.0f));
    camera->setAspectRatio(1280.0f / 1024.0f);
}

void CharBackdropScene::Impl::setCharacter(int row, uint16_t, uint8_t, const EQT::Graphics::EntityAppearance&) {
    if (!root || row < 0) return;
    if (static_cast<size_t>(row) < charNodes_.size() && charNodes_[row]) {
        charNodes_[row]->remove();
        charNodes_[row] = nullptr;
    }
    // A click stand: a small upright box on the pedestal. The exact per-row
    // position is assumed (README "Left open"); the race loader's skinned
    // character is the follow-up seam, not this slice.
    irr::scene::IMesh* box = buildBoxMesh(kCharStandW, kCharStandH, kCharStandW,
                                          irr::video::SColor(255, 220, 120, 120));
    irr::scene::ISceneNode* node = smgr->addMeshSceneNode(box, root);
    if (node) {
        // The central pedestal is row 1; the side pedestals are rows 0 and 2.
        const float x = static_cast<float>(row - 1) * 6.0f;
        node->setPosition(irr::core::vector3df(x, 0.0f, 0.0f));
        if (static_cast<size_t>(row) >= charNodes_.size()) charNodes_.resize(static_cast<size_t>(row) + 1);
        charNodes_[row] = node;
    }
    box->drop();
}

void CharBackdropScene::Impl::clearCharacter(int row) {
    if (row < 0 || static_cast<size_t>(row) >= charNodes_.size()) return;
    if (charNodes_[row]) { charNodes_[row]->remove(); charNodes_[row] = nullptr; }
}

int CharBackdropScene::Impl::rowForClick(int sx, int y) {
    if (!camera || !driver) return -1;
    const size_t n = charNodes_.size();
    if (n == 0) return -1;
    // Project each stand's center to screen space and pick the nearest to the
    // click (a backdrop hit is NOT a selection; a far miss keeps the cursor,
    // the machine's rule). The projection is name_tag_batch's: the ONE
    // projection x view product, multiplyWith1x4Matrix, divide by the fourth
    // (w) component, NDC -> pixel.
    const irr::core::rect<irr::s32>& vp = driver->getViewPort();
    const irr::s32 halfW = static_cast<irr::s32>(vp.getWidth() / 2);
    const irr::s32 halfH = static_cast<irr::s32>(vp.getHeight() / 2);
    irr::core::matrix4 projView = camera->getProjectionMatrix();
    projView *= camera->getViewMatrix();
    int best = -1;
    float bestDist = 1e30f;
    for (size_t row = 0; row < n; ++row) {
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
        if (px < 0 || px > halfW * 2 || py < 0 || py > halfH * 2) continue;
        const float dist = (px - sx) * (px - sx) + (py - y) * (py - y);
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<int>(row);
        }
    }
    if (best >= 0 && bestDist > kClickRadiusPx * kClickRadiusPx) return -1;
    return best;
}

CharBackdropScene::CharBackdropScene() : impl_(std::make_unique<Impl>()) {}
CharBackdropScene::~CharBackdropScene() = default;

bool CharBackdropScene::init(EQT::Graphics::IrrlichtRenderer& renderer) { return impl_->init(renderer); }
void CharBackdropScene::clear() { impl_->clear(); }
void CharBackdropScene::update(const EQT::Graphics::ExploreCameraVerb& v, float dtMs) { impl_->update(v, dtMs); }
void CharBackdropScene::setCharacter(int row, uint16_t race, uint8_t gender, const EQT::Graphics::EntityAppearance& a) {
    impl_->setCharacter(row, race, gender, a);
}
void CharBackdropScene::clearCharacter(int row) { impl_->clearCharacter(row); }
int CharBackdropScene::characterCount() const { return impl_->characterCount(); }
int CharBackdropScene::nodeCount() const { return impl_->nodeCount(); }
int CharBackdropScene::rowForClick(int x, int y) { return impl_->rowForClick(x, y); }

void CharBackdropScene::renderScene(EQT::Graphics::IrrlichtRenderer& renderer) {
    if (!impl_->root) return;  // not stood up
    irr::scene::ISceneManager* smgr = renderer.getSceneManager();
    irr::video::IVideoDriver* driver = renderer.getDriver();
    if (!smgr || !driver) return;
    // The scene's own camera renders the showroom; the host's UI draws over it
    // in the same frame (the oracle's layering: 3D select scene, then the UI).
    driver->setTextureFilter(irr::video::ETF_BILINEAR);
    smgr->setCamera(impl_->camera);
    smgr->setDrawDistance(-1.0f);
    smgr->removeAllLights();
    smgr->addLightSceneNode(irr::core::vector3df(100, 300, 100), irr::core::vector3df(255, 255, 255, 255), -1);
    smgr->addLightSceneNode(irr::core::vector3df(-100, 100, -100), irr::core::vector3df(120, 120, 160, 255), -1);
    smgr->setAmbientLight(irr::video::SColor(255, 60, 60, 80));
    smgr->drawAndRenderScene(true, true);
}

}  // namespace ui
}  // namespace eqt
