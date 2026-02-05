#pragma once

#include "client/pathfinder_interface.h"
#include <string>
#include <memory>
#include <vector>
#include <DetourNavMesh.h>

class PathfinderNavmesh : public IPathfinder
{
public:
	// Triangle structure for debug visualization
	// Coordinates are in EQ format (Z-up)
	struct Triangle {
		glm::vec3 v1, v2, v3;
		glm::vec3 normal;  // Face normal
		uint8_t areaType;  // Navmesh area type (0=Normal, 1=Water, 2=Lava, etc.)
	};

	PathfinderNavmesh(const std::string &path);
	virtual ~PathfinderNavmesh();

	virtual IPath FindRoute(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, int flags = PathingNotDisabled);
	virtual IPath FindPath(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, const PathfinderOptions& opts);
	virtual glm::vec3 GetRandomLocation(const glm::vec3 &start, int flags = PathingNotDisabled);

	// Get triangles within a radius of a point (for debug visualization)
	// Center is in Irrlicht Y-up coordinates, returned triangles are also in Irrlicht Y-up
	// Internally converts to/from EQ Z-up format used by navmesh
	std::vector<Triangle> GetTrianglesInRadius(const glm::vec3& center, float radius) const;

	// Check if navmesh is loaded
	bool IsLoaded() const;

private:
	void Clear();
	void Load(const std::string &path);
	dtStatus GetPolyHeightNoConnections(dtPolyRef ref, const float *pos, float *height) const;
	dtStatus GetPolyHeightOnPath(const dtPolyRef *path, const int path_len, const glm::vec3 &pos, float *h) const;

	struct Implementation;
	std::unique_ptr<Implementation> m_impl;
};
