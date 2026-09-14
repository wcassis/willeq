/**
 * T476 -- census of every IT actor in the seventeen *equip*.s3d archives
 * (boards B660, B661).
 *
 * For each actor it records which route resolves it (the WLD actor chain or
 * the pre-T472 name fallback), what each 0x14 reference points at, whether a
 * skeleton rest transform and a mesh centre apply, and the resolved mesh's
 * principal (longest-extent) axis with its sign.
 *
 * Set WILLEQ_T476_CENSUS_CSV=<path> to write the table.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/eq/equipment_actor_geometry.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"

using namespace EQT::Graphics;

namespace {

bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

std::string eqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string p = env ? env : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

// Priority order of EquipmentModelLoader::buildEquipmentIndex().
const std::vector<std::string> kEquipArchives = {
    "obequip_lexit.s3d", "frogequip.s3d", "lgequip_amr2.s3d", "lgequip_amr.s3d", "loyequip.s3d",
    "vequip.s3d", "lgequip2.s3d", "lgequip.s3d", "ldonequip.s3d", "gatesequip.s3d", "gequip8.s3d",
    "gequip6.s3d", "gequip5.s3d", "gequip4.s3d", "gequip3.s3d", "gequip2.s3d", "gequip.s3d"};

bool isIdentityRest(const SkeletonBone& b) {
    if (!b.orientation) return true;
    const auto& o = *b.orientation;
    const bool rot = std::fabs(o.quatX) < 1e-4f && std::fabs(o.quatY) < 1e-4f && std::fabs(o.quatZ) < 1e-4f;
    const bool shift = std::fabs(o.shiftX) < 1e-4f && std::fabs(o.shiftY) < 1e-4f && std::fabs(o.shiftZ) < 1e-4f;
    const bool scale = o.scale <= 0.0f || std::fabs(o.scale - 1.0f) < 1e-4f;
    return rot && shift && scale;
}

TEST(EquipmentItemCensus, EveryItActorIsClassified) {
    const std::string eq = eqClientPath();
    if (!fileExists(eq + "gequip.s3d")) GTEST_SKIP() << "no client data at " << eq;

    const char* csvPath = std::getenv("WILLEQ_T476_CENSUS_CSV");
    std::ofstream csv;
    if (csvPath) {
        csv.open(csvPath);
        csv << "archive,wld,actor,model_id,index_winner,ref_kinds,chain_draws,name_match,route,"
               "rest_nonidentity,centre_nonzero,verts,min_x,max_x,min_y,max_y,min_z,max_z,"
               "principal_axis,principal_sign,principal_ratio,fallback_meshes\n";
    }

    const std::regex itName("IT(\\d+)");
    std::set<int> seen;
    std::map<std::string, int> routeCount;
    for (const auto& archive : kEquipArchives) {
        PfsArchive pfs;
        if (!pfs.open(eq + archive)) continue;
        std::vector<std::string> wlds;
        pfs.getFilenames(".wld", wlds);
        for (const auto& wldName : wlds) {
            WldLoader wld;
            if (!wld.parseFromArchive(eq + archive, wldName)) continue;
            for (const auto& [name, def] : wld.getObjectDefs()) {
                std::smatch m;
                if (!std::regex_match(name, m, itName)) continue;
                const int id = std::stoi(m[1].str());
                const bool winner = seen.insert(id).second;

                std::ostringstream kinds;
                bool chainDraws = false, rest = false, centre = false, skinnedRoute = false;
                for (uint32_t ref : def.meshRefs) {
                    auto mr = wld.getModelRefs().find(ref);
                    if (mr != wld.getModelRefs().end()) {
                        auto g = wld.getGeometryByFragmentIndex(mr->second.geometryFragRef);
                        kinds << (g ? "2D>mesh" : "2D>nogeom(" + std::to_string(mr->second.geometryFragRef) + ")") << ";";
                        if (g) {
                            chainDraws = true;
                            if (g->centerX != 0 || g->centerY != 0 || g->centerZ != 0) centre = true;
                        }
                        continue;
                    }
                    auto sr = wld.getSkeletonRefs().find(ref);
                    if (sr != wld.getSkeletonRefs().end()) {
                        auto tr = wld.getSkeletonTracks().find(sr->second);
                        if (tr == wld.getSkeletonTracks().end() || !tr->second) {
                            kinds << "11>notrack;";
                            continue;
                        }
                        int meshBones = 0;
                        for (const auto& b : tr->second->allBones) {
                            if (!isIdentityRest(*b)) rest = true;
                            auto bm = wld.getModelRefs().find(b->modelRef);
                            if (b->modelRef && bm != wld.getModelRefs().end()) {
                                auto g = wld.getGeometryByFragmentIndex(bm->second.geometryFragRef);
                                if (g) {
                                    ++meshBones;
                                    if (g->centerX != 0 || g->centerY != 0 || g->centerZ != 0) centre = true;
                                }
                            }
                        }
                        int skinMeshes = 0;
                        for (uint32_t sref : tr->second->meshRefs) {
                            auto sm = wld.getModelRefs().find(sref);
                            if (sm == wld.getModelRefs().end()) continue;
                            if (auto g = wld.getGeometryByFragmentIndex(sm->second.geometryFragRef)) {
                                ++skinMeshes;
                                if (g->centerX != 0 || g->centerY != 0 || g->centerZ != 0) centre = true;
                            }
                        }
                        kinds << "11>10(bones=" << tr->second->allBones.size() << " mesh=" << meshBones
                              << " skin=" << skinMeshes << ");";
                        if (meshBones || skinMeshes) chainDraws = true;
                        if (skinMeshes) skinnedRoute = true;
                        continue;
                    }
                    kinds << "other(" << ref << ");";
                }

                const std::string prefix = name + "_";
                bool nameMatch = false;
                std::ostringstream fallbackMeshes;
                for (const auto& g : wld.getGeometries()) {
                    if (!g) continue;
                    std::string up = g->name;
                    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    if (up.find(prefix) != std::string::npos) {
                        nameMatch = true;
                        fallbackMeshes << g->name << " ";
                        if (!chainDraws && (g->centerX != 0 || g->centerY != 0 || g->centerZ != 0)) centre = true;
                    }
                }
                const std::string route =
                    skinnedRoute ? "chain_skinned" : chainDraws ? "chain" : (nameMatch ? "name_fallback" : "none");
                if (winner) ++routeCount[route];

                auto geom = resolveEquipmentActorGeometry(wld, name);
                float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
                size_t verts = 0;
                std::string axis = "-", sign = "-";
                float ratio = 0;
                if (geom) {
                    verts = geom->vertices.size();
                    mn[0] = geom->minX; mn[1] = geom->minY; mn[2] = geom->minZ;
                    mx[0] = geom->maxX; mx[1] = geom->maxY; mx[2] = geom->maxZ;
                    int best = 0;
                    float ext[3];
                    for (int a = 0; a < 3; ++a) ext[a] = mx[a] - mn[a];
                    for (int a = 1; a < 3; ++a) if (ext[a] > ext[best]) best = a;
                    float second = 0;
                    for (int a = 0; a < 3; ++a) if (a != best) second = std::max(second, ext[a]);
                    ratio = second > 0 ? ext[best] / second : 0;
                    axis = std::string(1, "XYZ"[best]);
                    sign = (std::fabs(mx[best]) >= std::fabs(mn[best])) ? "+" : "-";
                }
                if (csv.is_open()) {
                    csv << archive << "," << wldName << "," << name << "," << id << "," << (winner ? 1 : 0) << ","
                        << kinds.str() << "," << chainDraws << "," << nameMatch << "," << route << "," << rest << ","
                        << centre << "," << verts;
                    for (int a = 0; a < 3; ++a) csv << "," << mn[a] << "," << mx[a];
                    csv << "," << axis << "," << sign << "," << ratio << "," << fallbackMeshes.str() << "\n";
                }
            }
        }
    }
    for (const auto& [route, n] : routeCount) std::cout << "route " << route << " (index winners): " << n << "\n";
    EXPECT_GT(seen.size(), 700u);
    // T476 (B660): before the 0x10 mesh list was read, 266 index items
    // resolved only by NAME (no rest pose) and 79 not at all.
    EXPECT_EQ(routeCount["name_fallback"], 0) << "items drawn without their skeleton's rest pose";
    EXPECT_EQ(routeCount["none"], 0) << "IT actors whose reference chain draws nothing";
    EXPECT_GT(routeCount["chain_skinned"], 300);
}

}  // namespace
