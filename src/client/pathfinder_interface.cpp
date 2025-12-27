#include "client/pathfinder_interface.h"
#include "client/pathfinder_null.h"
#include "common/logging.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include <fmt/format.h>
#include <sys/stat.h>
#include <iostream>

IPathfinder *IPathfinder::Load(const std::string &zone, const std::string &custom_navmesh_path) {
#ifdef EQT_HAS_NAVMESH
	struct stat statbuffer;
	std::string navmesh_file_path;

	if (!custom_navmesh_path.empty()) {
		// Use custom navmesh path
		navmesh_file_path = fmt::format("{}/{}.nav", custom_navmesh_path, zone);
		LOG_DEBUG(MOD_MAP, "IPathfinder::Load: Using custom navmesh path: {}", custom_navmesh_path);
	} else {
		// Use default path (current directory + maps/nav)
		navmesh_file_path = fmt::format("maps/nav/{}.nav", zone);
		LOG_DEBUG(MOD_MAP, "IPathfinder::Load: Using default path");
	}

	LOG_DEBUG(MOD_MAP, "IPathfinder::Load: Looking for navmesh at: {}", navmesh_file_path);

	if (stat(navmesh_file_path.c_str(), &statbuffer) == 0) {
		LOG_DEBUG(MOD_MAP, "IPathfinder::Load: Found navmesh file, loading PathfinderNavmesh");
		return new PathfinderNavmesh(navmesh_file_path);
	}

	LOG_DEBUG(MOD_MAP, "IPathfinder::Load: No navmesh file found, returning PathfinderNull");
#else
	(void)zone;
	(void)custom_navmesh_path;
	LOG_DEBUG(MOD_MAP, "IPathfinder::Load: NavMesh support not compiled in, returning PathfinderNull");
#endif
	return new PathfinderNull();
}
