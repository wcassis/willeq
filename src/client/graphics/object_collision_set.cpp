// object_collision_set.cpp -- see include/client/graphics/object_collision_set.h.
// B909b, GitLab #229.
#include "client/graphics/object_collision_set.h"

#include "client/graphics/moller_trumbore.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

namespace {

using irr::core::aabbox3df;
using irr::core::vector3df;

// The margin a world box is inflated by. Moller-Trumbore accepts a hit whose
// barycentrics pass in float, so a hit point can sit a rounding error outside
// the triangle's exact extent; the box must still contain it. Coordinates in
// EQ zones are below 1e5 and float's relative error is ~6e-8, so 0.01 plus
// 1e-4 of the largest coordinate is several orders above that and still far
// below any object's size.
float boxMargin(const aabbox3df& b) {
    const float m = std::max({std::fabs(b.MinEdge.X), std::fabs(b.MinEdge.Y), std::fabs(b.MinEdge.Z),
                              std::fabs(b.MaxEdge.X), std::fabs(b.MaxEdge.Y), std::fabs(b.MaxEdge.Z)});
    return 0.01f + 1e-4f * m;
}

// A ray prepared for slab tests: origin, 1/dir, and which axes are parallel.
struct SlabRay {
    float o[3];
    float inv[3];
    bool parallel[3];
    SlabRay(const vector3df& origin, const vector3df& dir) {
        const float d[3] = {dir.X, dir.Y, dir.Z};
        o[0] = origin.X; o[1] = origin.Y; o[2] = origin.Z;
        for (int k = 0; k < 3; ++k) {
            parallel[k] = (d[k] == 0.0f);
            inv[k] = parallel[k] ? 0.0f : 1.0f / d[k];
        }
    }
};

// Can the segment origin + dir * t, t in [0, tMax], touch `box`? Slab test.
// Conservative: a zero direction component only rejects when the origin is
// outside that slab.
bool segmentTouchesBox(const SlabRay& ray, float tMax, const aabbox3df& box) {
    float t0 = 0.0f, t1 = tMax;
    const float lo[3] = {box.MinEdge.X, box.MinEdge.Y, box.MinEdge.Z};
    const float hi[3] = {box.MaxEdge.X, box.MaxEdge.Y, box.MaxEdge.Z};
    for (int k = 0; k < 3; ++k) {
        if (ray.parallel[k]) {
            if (ray.o[k] < lo[k] || ray.o[k] > hi[k]) return false;
            continue;
        }
        float ta = (lo[k] - ray.o[k]) * ray.inv[k];
        float tb = (hi[k] - ray.o[k]) * ray.inv[k];
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}

}  // namespace

ObjectCollisionSet::~ObjectCollisionSet() { clear(); }

void ObjectCollisionSet::clear() {
    for (auto& m : meshes_) {
        if (m.mesh) m.mesh->drop();
    }
    std::vector<Mesh>().swap(meshes_);
    std::vector<Run>().swap(runs_);
    std::vector<Instance>().swap(instances_);
    std::vector<std::pair<size_t, uint32_t>>().swap(regions_);
    worldTriangles_ = 0;
    finished_ = false;
}

uint32_t ObjectCollisionSet::meshSlot(irr::scene::IMesh* mesh, bool snapshot) {
    if (!snapshot) {
        // A linear scan over the distinct meshes: load-time only, and zones
        // hold tens to a few hundred distinct object meshes.
        for (uint32_t i = 0; i < meshes_.size(); ++i) {
            if (meshes_[i].mesh == mesh) return i;
        }
    }
    Mesh m;
    m.firstRun = static_cast<uint32_t>(runs_.size());
    bool first = true;
    auto addPoint = [&](const vector3df& p) {
        if (first) { m.objectBox.reset(p); first = false; }
        else m.objectBox.addInternalPoint(p);
    };
    const irr::u32 bufCount = mesh->getMeshBufferCount();
    for (irr::u32 b = 0; b < bufCount; ++b) {
        const irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
        if (!buf) continue;
        // CTriangleSelector::createFromMesh() reads the indices as u16 and the
        // position through getPosition(); a 32-bit buffer would be misread by
        // it and is not produced by the object mesh builders.
        const uint32_t idxCnt = buf->getIndexCount();
        const uint32_t tris = idxCnt / 3;
        if (tris == 0) continue;
        const uint16_t* indices = buf->getIndices();
        const uint32_t stride = irr::video::getVertexPitchFromType(buf->getVertexType());
        const auto* verts = static_cast<const uint8_t*>(buf->getVertices());
        auto pos = [&](uint32_t i) -> const vector3df& {
            return *reinterpret_cast<const vector3df*>(verts + static_cast<size_t>(indices[i]) * stride);
        };
        for (uint32_t i = 0; i < tris * 3; ++i) {
            addPoint(pos(i));
            if (snapshot) m.snapshot.push_back(pos(i));
        }
        m.triangles += tris;
        if (!snapshot) {
            Run r;
            r.vertices = verts;
            r.indices = indices;
            r.stride = stride;
            r.indexCount = tris * 3;
            runs_.push_back(r);
        }
    }
    if (snapshot) {
        m.snapshot.shrink_to_fit();
        Run r;
        r.vertices = nullptr;   // = this mesh's snapshot, resolved at query time
        r.indices = nullptr;
        r.stride = sizeof(vector3df);
        r.indexCount = static_cast<uint32_t>(m.snapshot.size());
        if (r.indexCount) runs_.push_back(r);
    } else {
        mesh->grab();
        m.mesh = mesh;
    }
    m.runCount = static_cast<uint32_t>(runs_.size()) - m.firstRun;
    meshes_.push_back(std::move(m));
    return static_cast<uint32_t>(meshes_.size() - 1);
}

bool ObjectCollisionSet::addInstance(size_t bspRegion, irr::scene::IMesh* mesh,
                                     const irr::core::matrix4& world, bool snapshot) {
    if (!mesh) return false;
    const uint32_t slot = meshSlot(mesh, snapshot);
    if (meshes_[slot].triangles == 0) {
        // Only a slot made just now can be empty (an empty one is never
        // kept): a mesh with no triangle adds nothing, as in the old copy.
        if (meshes_.back().mesh) meshes_.back().mesh->drop();
        runs_.resize(meshes_.back().firstRun);
        meshes_.pop_back();
        return false;
    }
    Instance inst;
    inst.world = world;
    inst.region = bspRegion;
    inst.mesh = slot;
    aabbox3df wb = meshes_[slot].objectBox;
    world.transformBoxEx(wb);
    const float e = boxMargin(wb);
    wb.MinEdge -= vector3df(e, e, e);
    wb.MaxEdge += vector3df(e, e, e);
    inst.worldBox = wb;
    instances_.push_back(inst);
    worldTriangles_ += meshes_[slot].triangles;
    finished_ = false;
    return true;
}

void ObjectCollisionSet::finish() {
    std::stable_sort(instances_.begin(), instances_.end(),
                     [](const Instance& a, const Instance& b) { return a.region < b.region; });
    instances_.shrink_to_fit();
    runs_.shrink_to_fit();
    meshes_.shrink_to_fit();
    regions_.clear();
    for (uint32_t i = 0; i < instances_.size(); ++i) {
        if (regions_.empty() || regions_.back().first != instances_[i].region) {
            regions_.emplace_back(instances_[i].region, i);
        }
    }
    regions_.shrink_to_fit();
    finished_ = true;
}

bool ObjectCollisionSet::hasRegion(size_t bspRegion) const {
    auto it = std::lower_bound(regions_.begin(), regions_.end(), bspRegion,
                               [](const std::pair<size_t, uint32_t>& e, size_t r) { return e.first < r; });
    return it != regions_.end() && it->first == bspRegion;
}

size_t ObjectCollisionSet::meshTriangleCount() const {
    size_t n = 0;
    for (const auto& m : meshes_) n += m.triangles;
    return n;
}

size_t ObjectCollisionSet::ownedBytes() const {
    size_t n = meshes_.capacity() * sizeof(Mesh) + runs_.capacity() * sizeof(Run) +
               instances_.capacity() * sizeof(Instance) +
               regions_.capacity() * sizeof(std::pair<size_t, uint32_t>);
    for (const auto& m : meshes_) n += m.snapshot.capacity() * sizeof(vector3df);
    return n;
}

size_t ObjectCollisionSet::sharedMeshBytes() const {
    size_t n = 0;
    for (const auto& m : meshes_) {
        if (!m.mesh) continue;
        for (irr::u32 b = 0; b < m.mesh->getMeshBufferCount(); ++b) {
            const irr::scene::IMeshBuffer* buf = m.mesh->getMeshBuffer(b);
            if (!buf) continue;
            n += static_cast<size_t>(buf->getVertexCount()) *
                     irr::video::getVertexPitchFromType(buf->getVertexType()) +
                 static_cast<size_t>(buf->getIndexCount()) * sizeof(uint16_t);
        }
    }
    return n;
}

bool ObjectCollisionSet::rayIntersect(size_t bspRegion, const vector3df& rayOrigin,
                                      const vector3df& rayDir, float rayLen,
                                      vector3df& hitPoint, irr::core::triangle3df& hitTriangle,
                                      float& hitDistSq) const {
    if (!finished_) return false;
    auto it = std::lower_bound(regions_.begin(), regions_.end(), bspRegion,
                               [](const std::pair<size_t, uint32_t>& e, size_t r) { return e.first < r; });
    if (it == regions_.end() || it->first != bspRegion) return false;
    const uint32_t begin = it->second;
    const uint32_t end = (it + 1 == regions_.end()) ? static_cast<uint32_t>(instances_.size()) : (it + 1)->second;

    // ONE closestT across the whole region, as the old copy's single
    // RayIntersectTriangles() call over the region's concatenated vector had.
    const SlabRay slab(rayOrigin, rayDir);
    bool anyHit = false;
    float closestT = rayLen + 1.0f;
    for (uint32_t i = begin; i < end; ++i) {
        const Instance& inst = instances_[i];
        // A hit must have t < closestT, so a box the segment [0, closestT]
        // cannot reach holds no triangle that could change the answer.
        if (!segmentTouchesBox(slab, closestT, inst.worldBox)) continue;
        const Mesh& m = meshes_[inst.mesh];
        for (uint32_t r = m.firstRun; r < m.firstRun + m.runCount; ++r) {
            const Run& run = runs_[r];
            const uint8_t* base = run.vertices ? run.vertices
                                               : reinterpret_cast<const uint8_t*>(m.snapshot.data());
            const size_t stride = run.stride;
            // The selector's own transform: CTriangleSelector::getTriangles()
            // applies matrix4::transformVect(out, in) to each point.
            auto test = [&](size_t i0, size_t i1, size_t i2) {
                vector3df a, b, c;
                inst.world.transformVect(a, *reinterpret_cast<const vector3df*>(base + i0 * stride));
                inst.world.transformVect(b, *reinterpret_cast<const vector3df*>(base + i1 * stride));
                inst.world.transformVect(c, *reinterpret_cast<const vector3df*>(base + i2 * stride));
                MollerTrumboreStep(rayOrigin, rayDir, a, b, c, closestT, hitPoint, hitTriangle, anyHit);
            };
            if (run.indices) {
                for (uint32_t k = 0; k + 2 < run.indexCount; k += 3)
                    test(run.indices[k], run.indices[k + 1], run.indices[k + 2]);
            } else {
                for (uint32_t k = 0; k + 2 < run.indexCount; k += 3) test(k, k + 1, k + 2);
            }
        }
    }
    if (anyHit) {
        hitDistSq = hitPoint.getDistanceFromSQ(rayOrigin);
    }
    return anyHit;
}

}  // namespace Graphics
}  // namespace EQT
