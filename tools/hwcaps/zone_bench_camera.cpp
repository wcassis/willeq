#include "zone_bench_camera.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif

namespace ZoneBench {

bool CameraPath::generateFromNavmesh(const std::string& navmeshPath,
                                      int numWaypoints, uint32_t seed,
                                      const glm::vec3& seedPos) {
    return generateFromNavmesh(navmeshPath, numWaypoints, seed,
                               std::vector<glm::vec3>{seedPos});
}

bool CameraPath::generateFromNavmesh(const std::string& navmeshPath,
                                      int numWaypoints, uint32_t seed,
                                      const std::vector<glm::vec3>& seedCandidates) {
#ifdef EQT_HAS_NAVMESH
    PathfinderNavmesh nav(navmeshPath);
    if (!nav.IsLoaded()) {
        fprintf(stderr, "WARNING: Failed to load navmesh: %s\n", navmeshPath.c_str());
        return false;
    }

    printf("Navmesh loaded: %s\n", navmeshPath.c_str());
    printf("Generating camera path with %d waypoints (seed=%u)...\n", numWaypoints, seed);

    // Sample random waypoints from the navmesh.
    // GetRandomLocation requires a start position within (5,100,5) EQ units
    // of a navmesh polygon; try each candidate seed in rank order (T242).
    // The PRNG is re-seeded per candidate so the chosen path depends only on
    // (seed, winning candidate), not on how many candidates failed first.
    std::vector<glm::vec3> randomPoints;
    for (size_t c = 0; c < seedCandidates.size(); ++c) {
        const glm::vec3& seedPos = seedCandidates[c];
        srand(seed);
        randomPoints.clear();
        glm::vec3 current = seedPos;
        for (int i = 0; i < numWaypoints; ++i) {
            glm::vec3 pt = nav.GetRandomLocation(current);
            // Skip points at origin (failed samples)
            if (std::abs(pt.x) > 0.1f || std::abs(pt.y) > 0.1f || std::abs(pt.z) > 0.1f) {
                randomPoints.push_back(pt);
                current = pt;  // Chain from successful samples
            }
        }
        if (randomPoints.size() >= 2) {
            printf("  Navmesh seed position: (%.1f, %.1f, %.1f) [candidate %zu of %zu]\n",
                   seedPos.x, seedPos.y, seedPos.z, c + 1, seedCandidates.size());
            break;
        }
        fprintf(stderr, "WARNING: seed candidate %zu of %zu (%.1f, %.1f, %.1f): only %zu valid "
                "navmesh samples, need at least 2\n",
                c + 1, seedCandidates.size(), seedPos.x, seedPos.y, seedPos.z,
                randomPoints.size());
    }

    if (randomPoints.size() < 2) {
        fprintf(stderr, "WARNING: no seed candidate (%zu tried) produced 2 navmesh samples\n",
                seedCandidates.size());
        return false;
    }

    printf("  Sampled %zu valid waypoints\n", randomPoints.size());

    // Pathfind between consecutive waypoints and concatenate
    waypoints_.clear();
    float cumDist = 0;
    const float eyeHeight = 6.0f;  // EQ eye height above ground

    int segmentsUsed = 0, segmentsSkipped = 0;
    for (size_t i = 0; i + 1 < randomPoints.size(); ++i) {
        bool partial = false, stuck = false;
        auto path = nav.FindRoute(randomPoints[i], randomPoints[i + 1], partial, stuck);

        if (path.empty()) { segmentsSkipped++; continue; }

        // Check for large gap between end of previous segment and start of this one.
        // Small gaps are normal (navmesh poly snapping). Large gaps mean the path
        // would teleport, and linear interpolation would fly through walls.
        if (!waypoints_.empty() && !path.empty()) {
            auto firstIt = path.begin();
            while (firstIt != path.end() && firstIt->teleport) ++firstIt;
            if (firstIt != path.end()) {
                // waypoints_ have eye height added, path nodes don't — compare XY only
                float gx = firstIt->pos.x - waypoints_.back().pos.x;
                float gy = firstIt->pos.y - waypoints_.back().pos.y;
                float gapSq = gx * gx + gy * gy;
                if (gapSq > 2500.0f) {  // > 50 EQ units horizontal gap
                    segmentsSkipped++;
                    continue;
                }
            }
        }

        for (const auto& pt : path) {
            if (pt.teleport) continue;
            glm::vec3 eyePos(pt.pos.x, pt.pos.y, pt.pos.z + eyeHeight);

            if (!waypoints_.empty()) {
                float dx = eyePos.x - waypoints_.back().pos.x;
                float dy = eyePos.y - waypoints_.back().pos.y;
                float dz = eyePos.z - waypoints_.back().pos.z;
                float segLen = std::sqrt(dx * dx + dy * dy + dz * dz);
                // Skip near-duplicate points
                if (segLen < 0.5f) continue;
                cumDist += segLen;
            }

            waypoints_.push_back({eyePos, cumDist});
        }
        segmentsUsed++;
    }

    printf("  Path segments: %d used, %d skipped (partial/gap)\n",
           segmentsUsed, segmentsSkipped);

    printf("  Camera path: %zu waypoints, %.0f EQ units total length\n",
           waypoints_.size(), totalLength());
    return waypoints_.size() >= 2;
#else
    (void)navmeshPath; (void)numWaypoints; (void)seed;
    fprintf(stderr, "WARNING: Built without EQT_HAS_NAVMESH, cannot generate navmesh path\n");
    return false;
#endif
}

void CameraPath::generateOrbit(float centerX, float centerY, float centerZ,
                                float radius, float heightOffset, int steps) {
    waypoints_.clear();
    float cumDist = 0;
    const float angleStep = 2.0f * 3.14159265f / steps;

    for (int i = 0; i <= steps; ++i) {
        float angle = i * angleStep;
        float x = centerX + radius * std::cos(angle);
        float y = centerY + radius * std::sin(angle);
        float z = centerZ + heightOffset;

        glm::vec3 pos(x, y, z);
        if (!waypoints_.empty()) {
            float dx = pos.x - waypoints_.back().pos.x;
            float dy = pos.y - waypoints_.back().pos.y;
            float dz = pos.z - waypoints_.back().pos.z;
            cumDist += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        waypoints_.push_back({pos, cumDist});
    }

    printf("  Orbit path: %zu waypoints, radius=%.0f, %.0f EQ units total\n",
           waypoints_.size(), radius, totalLength());
}

void CameraPath::generateApproach(float targetX, float targetY, float targetZ,
                                    float approachDist, float eyeHeight, int steps) {
    waypoints_.clear();
    float cumDist = 0;

    // Walk from far away straight toward the target
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / steps;
        // Start far, end at target
        float x = targetX + (1.0f - t) * approachDist;
        float y = targetY;
        float z = targetZ + eyeHeight;

        glm::vec3 pos(x, y, z);
        if (!waypoints_.empty()) {
            float dx = pos.x - waypoints_.back().pos.x;
            float dy = pos.y - waypoints_.back().pos.y;
            float dz = pos.z - waypoints_.back().pos.z;
            cumDist += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        waypoints_.push_back({pos, cumDist});
    }

    // Walk back out (so the path loops)
    for (int i = steps - 1; i >= 0; --i) {
        float t = static_cast<float>(i) / steps;
        float x = targetX + (1.0f - t) * approachDist;
        float y = targetY;
        float z = targetZ + eyeHeight;

        glm::vec3 pos(x, y, z);
        float dx = pos.x - waypoints_.back().pos.x;
        float dy = pos.y - waypoints_.back().pos.y;
        float dz = pos.z - waypoints_.back().pos.z;
        cumDist += std::sqrt(dx * dx + dy * dy + dz * dz);
        waypoints_.push_back({pos, cumDist});
    }

    printf("  Approach path: %zu waypoints, %.0f EQ units, target=(%.0f,%.0f,%.0f)\n",
           waypoints_.size(), totalLength(), targetX, targetY, targetZ);
}

size_t CameraPath::findSegment(float t) const {
    // Binary search for segment containing t
    size_t lo = 0, hi = waypoints_.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (waypoints_[mid].cumDist <= t)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

glm::vec3 CameraPath::getPositionAt(float t) const {
    if (waypoints_.empty()) return glm::vec3(0);
    if (waypoints_.size() == 1) return waypoints_[0].pos;

    // Wrap
    float len = totalLength();
    if (len <= 0) return waypoints_[0].pos;
    t = std::fmod(t, len);
    if (t < 0) t += len;

    size_t seg = findSegment(t);
    size_t next = seg + 1;
    if (next >= waypoints_.size()) return waypoints_.back().pos;

    float segStart = waypoints_[seg].cumDist;
    float segEnd = waypoints_[next].cumDist;
    float segLen = segEnd - segStart;
    float frac = (segLen > 0) ? (t - segStart) / segLen : 0;

    return waypoints_[seg].pos + frac * (waypoints_[next].pos - waypoints_[seg].pos);
}

glm::vec3 CameraPath::getDirectionAt(float t) const {
    if (waypoints_.size() < 2) return glm::vec3(1, 0, 0);

    // Look slightly ahead for direction
    float lookAhead = 5.0f;  // 5 EQ units ahead
    glm::vec3 here = getPositionAt(t);
    glm::vec3 ahead = getPositionAt(t + lookAhead);
    glm::vec3 dir = ahead - here;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 0.001f) return glm::vec3(1, 0, 0);
    return dir / len;
}

} // namespace ZoneBench
