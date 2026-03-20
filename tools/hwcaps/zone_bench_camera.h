#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace ZoneBench {

// A deterministic camera path through a zone, generated from navmesh waypoints
// or a simple orbit fallback.
class CameraPath {
public:
    struct Waypoint {
        glm::vec3 pos;      // EQ coordinates (Z-up)
        float cumDist;      // Cumulative distance from start
    };

    // Generate path by sampling navmesh waypoints and pathfinding between them.
    // navmeshPath: path to compiled .nav file
    // numWaypoints: number of random waypoints to sample
    // seed: deterministic random seed
    // seedPos: a known valid position in the zone (EQ coords) to seed navmesh queries
    // Returns false if navmesh couldn't be loaded.
    bool generateFromNavmesh(const std::string& navmeshPath,
                             int numWaypoints, uint32_t seed,
                             const glm::vec3& seedPos);

    // Fallback: circular orbit around a center point.
    void generateOrbit(float centerX, float centerY, float centerZ,
                       float radius, float heightOffset, int steps = 360);

    // Straight-line approach toward a target point from distance.
    // Camera starts at (targetX + distX, targetY + distY, targetZ + eyeHeight)
    // and walks toward the target. Good for train stress tests.
    void generateApproach(float targetX, float targetY, float targetZ,
                          float approachDist, float eyeHeight, int steps = 300);

    // Get interpolated position at distance t along path (wraps).
    glm::vec3 getPositionAt(float t) const;

    // Get forward direction at distance t (tangent to path).
    glm::vec3 getDirectionAt(float t) const;

    float totalLength() const { return waypoints_.empty() ? 0 : waypoints_.back().cumDist; }
    bool empty() const { return waypoints_.size() < 2; }
    size_t waypointCount() const { return waypoints_.size(); }

private:
    std::vector<Waypoint> waypoints_;

    // Find the segment containing distance t (wrapped).
    size_t findSegment(float t) const;
};

} // namespace ZoneBench
