#pragma once

#include "pathfinder_interface.h"
#include <memory>
#include <cstdio>

struct PathFileHeader;
struct Node;

class PathfinderWaypoint : public IPathfinder
{
public:
	PathfinderWaypoint(const std::string &path);
	virtual ~PathfinderWaypoint();

	virtual IPath FindRoute(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, int flags = PathingNotDisabled) override;
	virtual IPath FindPath(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, const PathfinderOptions& opts) override;
	virtual glm::vec3 GetRandomLocation(const glm::vec3 &start, int flags = PathingNotDisabled) override;

	bool IsLoaded() const;

private:
	void Load(const std::string &filename);
	void LoadV2(FILE *f, const PathFileHeader &header);
	void LoadV3(FILE *f, const PathFileHeader &header);
	Node *FindPathNodeByCoordinates(float x, float y, float z);
	void BuildGraph();

	struct Implementation;
	std::unique_ptr<Implementation> m_impl;
};
