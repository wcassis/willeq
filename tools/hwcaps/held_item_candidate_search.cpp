// held_item_candidate_search -- T485 (boards B588, B724, B725): render every
// proper signed permutation of the held-item correction C through the ONE
// production placement (bone_attachment.h placeHeldItem, the function
// entity_equipment.cpp, the paperdoll and the model viewer all consume) and
// measure, for each candidate, where each test item's long axis points in the
// BODY frame -- so the correction is chosen by search against the official
// client's frames (review/T478/oracle), not by argument.
//
// The scene is built through EntityRenderer exactly as the in-world path builds
// it (createEntity + updateMainThreadEntityState, the HeldItemTest fixture's
// idiom): a Human male NPC at o01 idle with one item in the primary slot. After
// the production placement has run, the item node is re-placed with each
// candidate and captured from three cameras. Nothing here judges a picture;
// the numbers are the body-frame direction of the item's long axis, and the
// pictures are for Will.
//
// Usage: held_item_candidate_search <output_dir> [--eq-path <dir>] [--db <willeq.db>]
//                                   [--items 10649,10004,10] [--size WxH]
// Writes <output_dir>/c<NN>_<perm>_it<model>_<view>.png for every candidate,
// item and view, plus <output_dir>/candidates.tsv (one row per candidate and
// item: the long axis' forward/left/up components, the item Z axis' up
// component, and the angle to the oracle target) and rpoint_frames.txt (task
// 3 of the plan: R_POINT's world frame per o01 frame, its parent chain, and
// the forearm direction from the skeleton).

#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/bone_attachment.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/willeq_database.h"
#include "common/logging.h"

#include <irrlicht.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace EQT::Graphics;

namespace {

struct Candidate {
    std::string name;   // e.g. "X-Z_Y+Y_Z+X": the image of each item axis
    HeldItemCorrection corr;
};

// The 24 proper signed permutations, in a fixed order.
std::vector<Candidate> enumerateCandidates() {
    std::vector<Candidate> out;
    int perm[3] = {0, 1, 2};
    std::sort(perm, perm + 3);
    do {
        for (int signs = 0; signs < 8; ++signs) {
            HeldItemCorrection c{};
            for (int j = 0; j < 3; ++j) {
                for (int r = 0; r < 3; ++r) c.c[j][r] = 0.0f;
                c.c[j][perm[j]] = (signs & (1 << j)) ? -1.0f : 1.0f;
            }
            // det of the 3x3 with columns c[0], c[1], c[2]
            const float det = c.c[0][0] * (c.c[1][1] * c.c[2][2] - c.c[2][1] * c.c[1][2]) -
                              c.c[1][0] * (c.c[0][1] * c.c[2][2] - c.c[2][1] * c.c[0][2]) +
                              c.c[2][0] * (c.c[0][1] * c.c[1][2] - c.c[1][1] * c.c[0][2]);
            if (det < 0.0f) continue;
            const char axes[3] = {'X', 'Y', 'Z'};
            std::string name;
            for (int j = 0; j < 3; ++j) {
                if (j) name += '_';
                name += axes[j];
                name += (c.c[j][perm[j]] < 0.0f) ? '-' : '+';
                name += axes[perm[j]];
            }
            out.push_back({name, c});
        }
    } while (std::next_permutation(perm, perm + 3));
    return out;
}

bool sameCorrection(const HeldItemCorrection& a, const HeldItemCorrection& b) {
    for (int j = 0; j < 3; ++j)
        for (int r = 0; r < 3; ++r)
            if (std::fabs(a.c[j][r] - b.c[j][r]) > 1e-6f) return false;
    return true;
}

irr::core::vector3df eqToIrr(float x, float y, float z) { return irr::core::vector3df(x, z, y); }

irr::core::vector3df boneOriginEq(const BoneMat4& m) { return irr::core::vector3df(m.m[12], m.m[13], m.m[14]); }

irr::core::vector3df boneAxisEq(const BoneMat4& m, int axis) {
    return irr::core::vector3df(m.m[axis * 4 + 0], m.m[axis * 4 + 1], m.m[axis * 4 + 2]);
}

std::string v3(const irr::core::vector3df& v) {
    char buf[96];
    snprintf(buf, sizeof buf, "(%.3f, %.3f, %.3f)", v.X, v.Y, v.Z);
    return buf;
}

struct Options {
    std::string outDir = "held_item_search";
    std::string eqPath = "/mnt/projects/EverQuestP1999/";
    std::string dbPath = "data/willeq.db";
    std::vector<uint32_t> items = {10649, 10004, 10};
    irr::u32 width = 360, height = 520;
};

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eq-path") && i + 1 < argc) {
            o.eqPath = argv[++i];
            if (!o.eqPath.empty() && o.eqPath.back() != '/') o.eqPath += '/';
        } else if (!strcmp(argv[i], "--db") && i + 1 < argc) {
            o.dbPath = argv[++i];
        } else if (!strcmp(argv[i], "--items") && i + 1 < argc) {
            o.items.clear();
            std::string s = argv[++i];
            size_t pos = 0;
            while (pos < s.size()) {
                size_t comma = s.find(',', pos);
                if (comma == std::string::npos) comma = s.size();
                o.items.push_back(static_cast<uint32_t>(std::stoul(s.substr(pos, comma - pos))));
                pos = comma + 1;
            }
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            unsigned w = 0, h = 0;
            if (sscanf(argv[++i], "%ux%u", &w, &h) == 2 && w > 0 && h > 0) { o.width = w; o.height = h; }
        } else if (argv[i][0] != '-') {
            o.outDir = argv[i];
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

// Every material in the subtree drawn unlit, so the capture does not depend on
// a light rig this tool never sets up.
void unlit(irr::scene::ISceneNode* node) {
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = false;
        node->getMaterial(i).BackfaceCulling = false;
    }
    for (auto* child : node->getChildren()) unlit(child);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseOptions(argc, argv, opt)) return 2;
    mkdir(opt.outDir.c_str(), 0755);

    irr::SIrrlichtCreationParameters params;
#ifdef EQT_HAS_GLES2
    params.DriverType = irr::video::EDT_OGLES2;
#else
    params.DriverType = irr::video::EDT_OPENGL;
#endif
    params.WindowSize = irr::core::dimension2d<irr::u32>(opt.width, opt.height);
    params.Fullscreen = true;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: no Irrlicht device\n");
        return 1;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    irr::scene::ISceneManager* smgr = device->getSceneManager();

    EQT::WilleqDatabase db;
    if (!db.open(opt.dbPath)) {
        fprintf(stderr, "ERROR: could not open %s (run from the repository root or pass --db)\n", opt.dbPath.c_str());
        device->drop();
        return 1;
    }
    EQT::WilleqDatabase::setGlobalInstance(&db);

    int rc = 0;
    {
        auto cache = std::make_unique<ConstrainedTextureCache>(
            ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT));
        cache->setDriver(driver);
        auto ents = std::make_unique<EntityRenderer>(smgr, driver, device->getFileSystem());
        ents->setClientPath(opt.eqPath);
        ents->setConstrainedTextureCache(cache.get());
        if (!ents->loadGlobalCharacters()) {
            fprintf(stderr, "ERROR: global_chr.s3d would not load from %s\n", opt.eqPath.c_str());
            rc = 1;
        }
        if (rc == 0) ents->loadEquipmentModels();

        const std::vector<Candidate> candidates = enumerateCandidates();
        FILE* tsv = rc == 0 ? fopen((opt.outDir + "/candidates.tsv").c_str(), "w") : nullptr;
        FILE* frames = rc == 0 ? fopen((opt.outDir + "/rpoint_frames.txt").c_str(), "w") : nullptr;
        if (tsv)
            fprintf(tsv, "candidate\tname\tflags\titem\tlong_fwd\tlong_left\tlong_up\titemZ_up\ttarget\tangle_deg\n");

        irr::scene::ICameraSceneNode* camera = smgr->addCameraSceneNode();
        camera->setFOV(irr::core::PI / 5.0f);
        camera->setAspectRatio(static_cast<float>(opt.width) / static_cast<float>(opt.height));
        camera->setNearValue(0.5f);
        camera->setFarValue(300.0f);
        smgr->setAmbientLight(irr::video::SColorf(1.0f, 1.0f, 1.0f));

        // Prime the driver (same idiom as ui_inventory_capture).
        driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver->endScene();

        bool framesDumped = false;
        for (size_t itemIdx = 0; rc == 0 && itemIdx < opt.items.size(); ++itemIdx) {
            const uint32_t item = opt.items[itemIdx];
            constexpr uint16_t kSpawn = 7;
            EntityAppearance a;
            a.equipment[7] = item;
            ents->clearEntities();
            if (!ents->createEntity(kSpawn, /*race=*/1, "T485", 0.0f, 0.0f, 0.0f, /*heading=*/0.0f,
                                    /*isPlayer=*/false, /*gender=*/0, a, /*isNPC=*/true)) {
                fprintf(stderr, "ERROR: createEntity failed for item %u\n", item);
                rc = 1;
                break;
            }
            ents->setEntityAnimation(kSpawn, "o01", true);
            auto* node = static_cast<EQAnimatedMeshSceneNode*>(ents->getEntities().at(kSpawn).animatedNode);
            if (!node) {
                fprintf(stderr, "ERROR: no animated node for item %u\n", item);
                rc = 1;
                break;
            }
            irr::scene::IMeshSceneNode* equip = nullptr;
            for (int t = 0; t < 20 && !equip; ++t) {
                node->OnAnimate(200 + t * 16);
                ents->updateMainThreadEntityState(0.016f);
                equip = ents->getEntities().at(kSpawn).primaryEquipNode;
            }
            if (!equip) {
                fprintf(stderr, "ERROR: item %u never attached (no primaryEquipNode)\n", item);
                rc = 1;
                break;
            }
            node->updateAbsolutePosition();
            equip->updateAbsolutePosition();
            unlit(node);

            const auto& animator = node->getAnimator();
            auto skeleton = animator.getSkeleton();
            const auto& bones = animator.getBoneStates();
            const int rIdx = findBoneIndex(skeleton, "HUM", {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"});
            if (rIdx < 0 || rIdx >= static_cast<int>(bones.size())) {
                fprintf(stderr, "ERROR: no R_POINT on the Human male skeleton\n");
                rc = 1;
                break;
            }

            // Task 3: R_POINT's frame, its parent chain and the forearm direction.
            if (frames && !framesDumped) {
                framesDumped = true;
                fprintf(frames, "# R_POINT (bone %d '%s') on the Human male, EQ model space (x fwd, y left, z up)\n", rIdx,
                        skeleton->bones[rIdx].name.c_str());
                fprintf(frames, "# parent chain (name: world origin):\n");
                for (int b = rIdx; b >= 0; b = skeleton->bones[b].parentIndex) {
                    fprintf(frames, "#   %-24s %s%s\n", skeleton->bones[b].name.c_str(),
                            v3(boneOriginEq(bones[b].worldTransform)).c_str(),
                            skeleton->bones[b].animationTracks.count("o01") ? "  [has o01 track]" : "  [pose only]");
                }
                const BoneMat4& loc = bones[rIdx].localTransform;
                fprintf(frames, "# R_POINT local transform columns X %s Y %s Z %s origin %s\n",
                        v3(boneAxisEq(loc, 0)).c_str(), v3(boneAxisEq(loc, 1)).c_str(), v3(boneAxisEq(loc, 2)).c_str(),
                        v3(boneOriginEq(loc)).c_str());
                // Forearm: the two ancestors above R_POINT with real separation.
                std::vector<int> chain;
                for (int b = rIdx; b >= 0; b = skeleton->bones[b].parentIndex) chain.push_back(b);
                if (chain.size() >= 3) {
                    // chain[0]=R_POINT, [1]=hand, [2]=forearm ... report every consecutive pair.
                    for (size_t k = 0; k + 1 < chain.size() && k < 4; ++k) {
                        irr::core::vector3df d = boneOriginEq(bones[chain[k]].worldTransform) -
                                                 boneOriginEq(bones[chain[k + 1]].worldTransform);
                        const float len = d.getLength();
                        if (len > 1e-4f) d /= len;
                        fprintf(frames, "# segment %s -> %s: length %.3f, direction %s\n",
                                skeleton->bones[chain[k + 1]].name.c_str(), skeleton->bones[chain[k]].name.c_str(), len,
                                v3(d).c_str());
                    }
                }
                fprintf(frames, "# per o01 frame: frame  X-axis  Y-axis  Z-axis  origin\n");
                int lastFrame = -1;
                for (int ms = 0; ms <= 4000; ms += 25) {
                    node->OnAnimate(5000 + ms);
                    ents->updateMainThreadEntityState(0.025f);
                    const int f = animator.getCurrentFrame();
                    if (f == lastFrame) continue;
                    lastFrame = f;
                    const BoneMat4& w = bones[rIdx].worldTransform;
                    fprintf(frames, "%d\t%s\t%s\t%s\t%s\n", f, v3(boneAxisEq(w, 0)).c_str(), v3(boneAxisEq(w, 1)).c_str(),
                            v3(boneAxisEq(w, 2)).c_str(), v3(boneOriginEq(w)).c_str());
                }
                fflush(frames);
                // Back to the frame the captures use.
                for (int t = 0; t < 5; ++t) {
                    node->OnAnimate(200 + t * 16);
                    ents->updateMainThreadEntityState(0.016f);
                }
                node->updateAbsolutePosition();
                equip->updateAbsolutePosition();
            }

            // The item's long axis, in its own mesh frame: the vertex farthest
            // from the origin (the grip) -- and the item's EQ Z axis (Irrlicht Z
            // is EQ Y, so EQ Z is Irrlicht Y).
            irr::core::vector3df tipLocal;
            irr::scene::IMesh* mesh = equip->getMesh();
            for (irr::u32 b = 0; mesh && b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
                for (irr::u32 i = 0; i < buf->getVertexCount(); ++i)
                    if (buf->getPosition(i).getLengthSQ() > tipLocal.getLengthSQ()) tipLocal = buf->getPosition(i);
            }
            const irr::core::vector3df itemZLocal(0.0f, 1.0f, 0.0f);

            // Body frame in the world (NPC: the full absolute transform). EQ
            // model space: forward +X, left +Y, up +Z (T472: R_POINT's Y is
            // character-left); Irrlicht = (x, z, y).
            const irr::core::matrix4& bodyAbs = node->getAbsoluteTransformation();
            irr::core::vector3df fwd = eqToIrr(1, 0, 0), left = eqToIrr(0, 1, 0), up = eqToIrr(0, 0, 1);
            bodyAbs.rotateVect(fwd);
            bodyAbs.rotateVect(left);
            bodyAbs.rotateVect(up);
            fwd.normalize(); left.normalize(); up.normalize();
            irr::core::vector3df handWorld = eqToIrr(bones[rIdx].worldTransform.m[12], bones[rIdx].worldTransform.m[13],
                                                     bones[rIdx].worldTransform.m[14]);
            bodyAbs.transformVect(handWorld);
            const irr::core::vector3df origin = bodyAbs.getTranslation();
            // Bounding height of the body for the camera.
            const irr::core::aabbox3df box = node->getTransformedBoundingBox();
            const float height = box.MaxEdge.Y - box.MinEdge.Y;
            const irr::core::vector3df centre = origin + up * (height * 0.45f);
            printf("item IT%u: body height %.2f, R_POINT at %s (rel. origin %s)\n", item, height, v3(handWorld).c_str(),
                   v3(handWorld - origin).c_str());

            struct View { const char* name; irr::core::vector3df pos; irr::core::vector3df at; };
            const float d = std::max(12.0f, height * 2.1f);
            const View views[3] = {
                {"leftside", centre + left * d + up * (d * 0.28f), centre},
                {"frontleft", centre + fwd * (d * 0.75f) + left * (d * 0.75f) + up * (d * 0.5f), centre},
                {"rightside", centre - left * d + up * (d * 0.28f), centre},
            };

            for (size_t ci = 0; ci < candidates.size(); ++ci) {
                const Candidate& cand = candidates[ci];
                placeHeldItem(equip, bones[rIdx].worldTransform, nullptr, 1.0f, cand.corr);
                node->updateAbsolutePosition();
                equip->updateAbsolutePosition();
                const irr::core::matrix4& abs = equip->getAbsoluteTransformation();
                irr::core::vector3df tip = tipLocal;
                abs.transformVect(tip);
                irr::core::vector3df longAxis = tip - abs.getTranslation();
                longAxis.normalize();
                irr::core::vector3df itemZ = itemZLocal;
                abs.rotateVect(itemZ);
                itemZ.normalize();
                const float lf = longAxis.dotProduct(fwd), ll = longAxis.dotProduct(left), lu = longAxis.dotProduct(up);
                const float zu = itemZ.dotProduct(up);
                // Oracle targets (review/T478, README section 4): blades forward
                // and level; the arrow's long end straight down.
                const bool arrow = (item == 10);
                const irr::core::vector3df target = arrow ? -up : fwd;
                const float angle = std::acos(std::max(-1.0f, std::min(1.0f, longAxis.dotProduct(target)))) * 180.0f /
                                    3.14159265f;
                std::string flags;
                if (sameCorrection(cand.corr, kHeldItemCorrection)) flags += "SHIPPED ";
                {
                    HeldItemCorrection t472{{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}};
                    HeldItemCorrection ident{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
                    if (sameCorrection(cand.corr, t472)) flags += "T472 ";
                    if (sameCorrection(cand.corr, ident)) flags += "IDENTITY ";
                }
                if (tsv)
                    fprintf(tsv, "c%02zu\t%s\t%s\tIT%u\t%.3f\t%.3f\t%.3f\t%.3f\t%s\t%.1f\n", ci, cand.name.c_str(), flags.c_str(),
                            item, lf, ll, lu, zu, arrow ? "down" : "forward", angle);
                printf("c%02zu %-12s %-10s IT%-6u long=(fwd %+.2f left %+.2f up %+.2f) itemZ.up %+.2f angle-to-%s %.1f\n", ci,
                       cand.name.c_str(), flags.c_str(), item, lf, ll, lu, zu, arrow ? "down" : "forward", angle);

                for (const View& view : views) {
                    camera->setPosition(view.pos);
                    camera->setTarget(view.at);
                    camera->setUpVector(up);
                    smgr->setActiveCamera(camera);
                    // Two frames: the first content frame after a prime frame
                    // has dropped draws on this backend (ui_inventory_capture).
                    for (int f = 0; f < 2; ++f) {
                        driver->beginScene(true, true, irr::video::SColor(255, 110, 130, 150));
                        smgr->drawAll();
                        driver->endScene();
                    }
                    irr::video::IImage* img = driver->createScreenShot();
                    if (!img) {
                        fprintf(stderr, "ERROR: createScreenShot returned null\n");
                        rc = 1;
                        break;
                    }
                    char name[256];
                    snprintf(name, sizeof name, "%s/c%02zu_%s_it%u_%s.png", opt.outDir.c_str(), ci, cand.name.c_str(), item,
                             view.name);
                    if (!driver->writeImageToFile(img, name)) {
                        fprintf(stderr, "ERROR: could not write %s\n", name);
                        rc = 1;
                    }
                    img->drop();
                }
                if (rc) break;
            }
            if (tsv) fflush(tsv);
        }
        if (tsv) fclose(tsv);
        if (frames) fclose(frames);
        ents.reset();
        cache.reset();
    }
    EQT::WilleqDatabase::setGlobalInstance(nullptr);
    device->drop();
    return rc;
}
