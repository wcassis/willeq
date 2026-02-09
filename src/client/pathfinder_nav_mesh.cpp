#include "client/pathfinder_nav_mesh.h"
#include "common/util/compression.h"
#include "common/logging.h"

#include <memory>
#include <stdio.h>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <DetourCommon.h>
#include <DetourNavMeshQuery.h>

// Helper function for distance calculation
inline float DistanceSquared(const glm::vec3& a, const glm::vec3& b) {
	float dx = a.x - b.x;
	float dy = a.y - b.y;
	float dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}

struct PathfinderNavmesh::Implementation
{
	dtNavMesh *nav_mesh;
	dtNavMeshQuery *query;
};

PathfinderNavmesh::PathfinderNavmesh(const std::string &path)
{
	m_impl = std::make_unique<Implementation>();
	m_impl->nav_mesh = nullptr;
	m_impl->query = nullptr;
	Load(path);
}

PathfinderNavmesh::~PathfinderNavmesh()
{
	Clear();
}

IPathfinder::IPath PathfinderNavmesh::FindRoute(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, int flags)
{
	partial = false;

	if (!m_impl->nav_mesh) {
		return IPath();
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
	}

	m_impl->query->init(m_impl->nav_mesh, 2048); // Default max navmesh nodes for HC
	// Convert EQ coords to navmesh coords: EQ (x, y, z) → Navmesh (y, z, x)
	// Navmesh has X↔Z swapped relative to zone geometry (discovered via visualization alignment)
	glm::vec3 current_location(start.y, start.z, start.x);
	glm::vec3 dest_location(end.y, end.z, end.x);

	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	filter.setAreaCost(0, 1.0f); //Normal
	filter.setAreaCost(1, 3.0f); //Water
	filter.setAreaCost(2, 5.0f); //Lava
	filter.setAreaCost(4, 1.0f); //PvP
	filter.setAreaCost(5, 2.0f); //Slime
	filter.setAreaCost(6, 2.0f); //Ice
	filter.setAreaCost(7, 4.0f); //V Water (Frigid Water)
	filter.setAreaCost(8, 1.0f); //General Area
	filter.setAreaCost(9, 0.1f); //Portal
	filter.setAreaCost(10, 0.1f); //Prefer

	dtPolyRef start_ref;
	dtPolyRef end_ref;
	glm::vec3 ext(5.0f, 100.0f, 5.0f);

	m_impl->query->findNearestPoly(&current_location[0], &ext[0], &filter, &start_ref, 0);
	m_impl->query->findNearestPoly(&dest_location[0], &ext[0], &filter, &end_ref, 0);

	if (!start_ref || !end_ref) {
		return IPath();
	}

	int npoly = 0;
	dtPolyRef path[1024] = { 0 };
	auto status = m_impl->query->findPath(start_ref, end_ref, &current_location[0], &dest_location[0], &filter, path, &npoly, 1024);

	if (npoly) {
		glm::vec3 epos = dest_location;
		if (path[npoly - 1] != end_ref) {
			m_impl->query->closestPointOnPoly(path[npoly - 1], &dest_location[0], &epos[0], 0);
			partial = true;

			auto dist = DistanceSquared(epos, current_location);
			if (dist < 10000.0f) {
				stuck = true;
			}
		}

		float straight_path[2048 * 3];
		unsigned char straight_path_flags[2048];

		int n_straight_polys;
		dtPolyRef straight_path_polys[2048];

		status = m_impl->query->findStraightPath(&current_location[0], &epos[0], path, npoly,
			straight_path, straight_path_flags,
			straight_path_polys, &n_straight_polys, 2048, DT_STRAIGHTPATH_AREA_CROSSINGS);

		if (dtStatusFailed(status)) {
			return IPath();
		}

		if (n_straight_polys) {
			IPath Route;
			for (int i = 0; i < n_straight_polys; ++i)
			{
				// Convert navmesh coords back to EQ coords: Navmesh (x, y, z) → EQ (z, x, y)
				glm::vec3 node;
				node.x = straight_path[i * 3 + 2];  // EQ X = Navmesh Z
				node.y = straight_path[i * 3];      // EQ Y = Navmesh X
				node.z = straight_path[i * 3 + 1];  // EQ Z = Navmesh Y

				Route.push_back(node);

				unsigned short flag = 0;
				if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &flag))) {
					if (flag & 512) {
						Route.push_back(true);
					}
				}
			}

			return Route;
		}
	}

	IPath Route;
	Route.push_back(end);
	return Route;
}

IPathfinder::IPath PathfinderNavmesh::FindPath(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, const PathfinderOptions &opts)
{
	partial = false;

	if (!m_impl->nav_mesh) {
		return IPath();
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
	}

	m_impl->query->init(m_impl->nav_mesh, 2048); // Default max navmesh nodes for HC
	// Convert EQ coords to navmesh coords: EQ (x, y, z) → Navmesh (y, z, x)
	// Navmesh has X↔Z swapped relative to zone geometry (discovered via visualization alignment)
	glm::vec3 current_location(start.y, start.z, start.x);
	glm::vec3 dest_location(end.y, end.z, end.x);

	dtQueryFilter filter;
	filter.setIncludeFlags(opts.flags);
	filter.setAreaCost(0, opts.flag_cost[0]); //Normal
	filter.setAreaCost(1, opts.flag_cost[1]); //Water
	filter.setAreaCost(2, opts.flag_cost[2]); //Lava
	filter.setAreaCost(4, opts.flag_cost[3]); //PvP
	filter.setAreaCost(5, opts.flag_cost[4]); //Slime
	filter.setAreaCost(6, opts.flag_cost[5]); //Ice
	filter.setAreaCost(7, opts.flag_cost[6]); //V Water (Frigid Water)
	filter.setAreaCost(8, opts.flag_cost[7]); //General Area
	filter.setAreaCost(9, opts.flag_cost[8]); //Portal
	filter.setAreaCost(10, opts.flag_cost[9]); //Prefer

	static const int max_polys = 256;
	dtPolyRef start_ref;
	dtPolyRef end_ref;
	glm::vec3 ext(10.0f, 200.0f, 10.0f);

	m_impl->query->findNearestPoly(&current_location[0], &ext[0], &filter, &start_ref, 0);
	m_impl->query->findNearestPoly(&dest_location[0], &ext[0], &filter, &end_ref, 0);

	if (!start_ref || !end_ref) {
		return IPath();
	}

	int npoly = 0;
	dtPolyRef path[max_polys] = { 0 };
	auto status = m_impl->query->findPath(start_ref, end_ref, &current_location[0], &dest_location[0], &filter, path, &npoly, max_polys);

	if (npoly) {
		glm::vec3 epos = dest_location;
		if (path[npoly - 1] != end_ref) {
			m_impl->query->closestPointOnPoly(path[npoly - 1], &dest_location[0], &epos[0], 0);
			partial = true;

			auto dist = DistanceSquared(epos, current_location);
			if (dist < 10000.0f) {
				stuck = true;
			}
		}

		int n_straight_polys;
		glm::vec3 straight_path[max_polys];
		unsigned char straight_path_flags[max_polys];
		dtPolyRef straight_path_polys[max_polys];

		auto status = m_impl->query->findStraightPath(&current_location[0], &epos[0], path, npoly,
			(float*)&straight_path[0], straight_path_flags,
			straight_path_polys, &n_straight_polys, 2048, DT_STRAIGHTPATH_AREA_CROSSINGS | DT_STRAIGHTPATH_ALL_CROSSINGS);

		if (dtStatusFailed(status)) {
			return IPath();
		}

		if (n_straight_polys) {
			if (opts.smooth_path) {
				IPath Route;

				//Add the first point
				{
					auto &flag = straight_path_flags[0];
					if (flag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) {
						auto &p = straight_path[0];

						Route.push_back(glm::vec3(p.z, p.x, p.y));
					}
					else {
						auto &p = straight_path[0];

						float h = 0.0f;
						if (dtStatusSucceed(GetPolyHeightOnPath(path, npoly, p, &h))) {
							p.y = h + opts.offset;
						}

						Route.push_back(glm::vec3(p.z, p.x, p.y));
					}
				}

				for (int i = 0; i < n_straight_polys - 1; ++i)
				{
					auto &flag = straight_path_flags[i];

					if (flag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) {
						auto &poly = straight_path_polys[i];

						auto &p2 = straight_path[i + 1];
						// Convert navmesh coords to EQ: Navmesh (x, y, z) → EQ (z, x, y)
						glm::vec3 node(p2.z, p2.x, p2.y);
						Route.push_back(node);

						unsigned short pflag = 0;
						if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &pflag))) {
							if (pflag & 512) {
								Route.push_back(true);
							}
						}
					}
					else {
						auto &p1 = straight_path[i];
						auto &p2 = straight_path[i + 1];
						auto dist = glm::distance(p1, p2);
						auto dir = glm::normalize(p2 - p1);
						float total = 0.0f;
						glm::vec3 previous_pt = p1;

						while (total < dist) {
							glm::vec3 current_pt;
							float dist_to_move = opts.step_size;
							float ff = opts.step_size / 2.0f;

							if (total + dist_to_move + ff >= dist) {
								current_pt = p2;
								total = dist;
							}
							else {
								total += dist_to_move;
								current_pt = p1 + dir * total;
							}

							float h = 0.0f;
							if (dtStatusSucceed(GetPolyHeightOnPath(path, npoly, current_pt, &h))) {
								current_pt.y = h + opts.offset;
							}

							// Convert navmesh coords to EQ: Navmesh (x, y, z) → EQ (z, x, y)
							Route.push_back(glm::vec3(current_pt.z, current_pt.x, current_pt.y));
							previous_pt = current_pt;
						}
					}
				}

				return Route;
			}
			else {
				IPath Route;
				for (int i = 0; i < n_straight_polys; ++i)
				{
					auto &current = straight_path[i];
					glm::vec3 node(current.x, current.z, current.y);
					Route.push_back(node);

					unsigned short flag = 0;
					if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &flag))) {
						if (flag & 512) {
							Route.push_back(true);
						}
					}
				}

				return Route;
			}
		}
	}

	return IPath();
}

glm::vec3 PathfinderNavmesh::GetRandomLocation(const glm::vec3 &start, int flags)
{
	if (start.x == 0.0f && start.y == 0.0)
		return glm::vec3(0.f);

	if (!m_impl->nav_mesh) {
		return glm::vec3(0.f);
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
		m_impl->query->init(m_impl->nav_mesh, 2048); // Default max navmesh nodes for HC
	}

	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	filter.setAreaCost(0, 1.0f); //Normal
	filter.setAreaCost(1, 3.0f); //Water
	filter.setAreaCost(2, 5.0f); //Lava
	filter.setAreaCost(4, 1.0f); //PvP
	filter.setAreaCost(5, 2.0f); //Slime
	filter.setAreaCost(6, 2.0f); //Ice
	filter.setAreaCost(7, 4.0f); //V Water (Frigid Water)
	filter.setAreaCost(8, 1.0f); //General Area
	filter.setAreaCost(9, 0.1f); //Portal
	filter.setAreaCost(10, 0.1f); //Prefer

	dtPolyRef randomRef;
	float point[3];

	dtPolyRef start_ref;
	glm::vec3 current_location(start.x, start.z, start.y);
	glm::vec3 ext(5.0f, 100.0f, 5.0f);

	m_impl->query->findNearestPoly(&current_location[0], &ext[0], &filter, &start_ref, 0);

	if (!start_ref)
	{
		return glm::vec3(0.f);
	}

	// Use simple random function for HC
	auto random_func = []() { return (float)rand() / (float)RAND_MAX; };
	if (dtStatusSucceed(m_impl->query->findRandomPointAroundCircle(start_ref, &current_location[0], 100.f, &filter, random_func, &randomRef, point)))
	{
		return glm::vec3(point[0], point[2], point[1]);
	}

	return glm::vec3(0.f);
}

void PathfinderNavmesh::Clear()
{
	if (m_impl->nav_mesh) {
		dtFreeNavMesh(m_impl->nav_mesh);
	}

	if (m_impl->query) {
		dtFreeNavMeshQuery(m_impl->query);
	}
}

void PathfinderNavmesh::Load(const std::string &path)
{
	Clear();

	FILE *f = fopen(path.c_str(), "rb");
	if (f) {
		char magic[9] = { 0 };
		if (fread(magic, 9, 1, f) != 1) {
			fclose(f);
			return;
		}

		if (strncmp(magic, "EQNAVMESH", 9) != 0)
		{
			fclose(f);
			return;
		}

		uint32_t version = 0;
		if (fread(&version, sizeof(uint32_t), 1, f) != 1) {
			fclose(f);
			return;
		}

		if (version != 2) {
			fclose(f);
			return;
		}

		uint32_t data_size;
		if (fread(&data_size, sizeof(data_size), 1, f) != 1) {
			fclose(f);
			return;
		}

		uint32_t buffer_size;
		if (fread(&buffer_size, sizeof(buffer_size), 1, f) != 1) {
			fclose(f);
			return;
		}

		std::vector<char> data;
		data.resize(data_size);
		if (fread(&data[0], data_size, 1, f) != 1) {
			fclose(f);
			return;
		}

		std::vector<char> buffer;
		buffer.resize(buffer_size);
		uint32_t v = EQ::InflateData(&data[0], data_size, &buffer[0], buffer_size);
		fclose(f);

		char *buf = &buffer[0];
		m_impl->nav_mesh = dtAllocNavMesh();

		uint32_t number_of_tiles;
		memcpy(&number_of_tiles, buf, sizeof(number_of_tiles));
		buf += sizeof(uint32_t);

		dtNavMeshParams params;
		memcpy(&params, buf, sizeof(params));
		buf += sizeof(dtNavMeshParams);

		dtStatus status = m_impl->nav_mesh->init(&params);
		if (dtStatusFailed(status))
		{
			dtFreeNavMesh(m_impl->nav_mesh);
			m_impl->nav_mesh = nullptr;
			return;
		}

		for (unsigned int i = 0; i < number_of_tiles; ++i)
		{
			uint32_t tile_ref;
			memcpy(&tile_ref, buf, sizeof(tile_ref));
			buf += sizeof(uint32_t);

			int32_t data_size;
			memcpy(&data_size, buf, sizeof(data_size));
			buf += sizeof(uint32_t);

			if (!tile_ref || !data_size) {
				dtFreeNavMesh(m_impl->nav_mesh);
				m_impl->nav_mesh = nullptr;
				return;
			}

			unsigned char* data = (unsigned char*)dtAlloc(data_size, DT_ALLOC_PERM);
			memcpy(data, buf, data_size);
			buf += data_size;

			m_impl->nav_mesh->addTile(data, data_size, DT_TILE_FREE_DATA, tile_ref, 0);
		}

		LOG_INFO(MOD_MAP, "Loaded Navmesh V[{}] file [{}]", version, path.c_str());
	}
}

dtStatus PathfinderNavmesh::GetPolyHeightNoConnections(dtPolyRef ref, const float *pos, float *height) const
{
	auto *m_nav = m_impl->nav_mesh;

	if (!m_nav) {
		return DT_FAILURE;
	}

	const dtMeshTile* tile = 0;
	const dtPoly* poly = 0;
	if (dtStatusFailed(m_nav->getTileAndPolyByRef(ref, &tile, &poly))) {
		return DT_FAILURE | DT_INVALID_PARAM;
	}

	if (poly->getType() != DT_POLYTYPE_OFFMESH_CONNECTION) {
		const unsigned int ip = (unsigned int)(poly - tile->polys);
		const dtPolyDetail* pd = &tile->detailMeshes[ip];
		for (int j = 0; j < pd->triCount; ++j)
		{
			const unsigned char* t = &tile->detailTris[(pd->triBase + j) * 4];
			const float* v[3];
			for (int k = 0; k < 3; ++k)
			{
				if (t[k] < poly->vertCount)
					v[k] = &tile->verts[poly->verts[t[k]] * 3];
				else
					v[k] = &tile->detailVerts[(pd->vertBase + (t[k] - poly->vertCount)) * 3];
			}
			float h;
			if (dtClosestHeightPointTriangle(pos, v[0], v[1], v[2], h))
			{
				if (height)
					*height = h;
				return DT_SUCCESS;
			}
		}
	}

	return DT_FAILURE | DT_INVALID_PARAM;
}

dtStatus PathfinderNavmesh::GetPolyHeightOnPath(const dtPolyRef *path, const int path_len, const glm::vec3 &pos, float *h) const
{
	if (!path || !path_len) {
		return DT_FAILURE;
	}

	for (int i = 0; i < path_len; ++i) {
		dtPolyRef ref = path[i];

		if (dtStatusSucceed(GetPolyHeightNoConnections(ref, &pos[0], h))) {
			return DT_SUCCESS;
		}
	}

	return DT_FAILURE;
}

bool PathfinderNavmesh::IsLoaded() const
{
	return m_impl && m_impl->nav_mesh != nullptr;
}

std::vector<PathfinderNavmesh::Triangle> PathfinderNavmesh::GetTrianglesInRadius(const glm::vec3& center, float radius) const
{
	std::vector<Triangle> result;

	if (!m_impl || !m_impl->nav_mesh) {
		return result;
	}

	dtNavMesh* navMesh = m_impl->nav_mesh;
	float radiusSq = radius * radius;

	// Navmesh coordinates need transformation to match Irrlicht/zone geometry
	// The navmesh uses a different horizontal axis convention than the zone geometry
	// Transform: navmesh (x, y, z) → Irrlicht (z, y, x) - swap X and Z
	// This is equivalent to: mirror X, then rotate 270° around Y (left-handed)
	// The inverse transform (Irrlicht → navmesh) is the same: swap X and Z
	float detourCenter[3] = { center.z, center.y, center.x };

	// Get navmesh parameters to understand tile layout
	const dtNavMeshParams* params = navMesh->getParams();
	float tileWidth = params->tileWidth;
	float tileHeight = params->tileHeight;

	// Calculate which tiles might contain polygons within radius
	// In Detour Y-up coords, tiles are indexed by X and Z (the horizontal plane)
	int minTileX = (int)floorf((detourCenter[0] - radius - params->orig[0]) / tileWidth);
	int maxTileX = (int)floorf((detourCenter[0] + radius - params->orig[0]) / tileWidth);
	int minTileZ = (int)floorf((detourCenter[2] - radius - params->orig[2]) / tileHeight);
	int maxTileZ = (int)floorf((detourCenter[2] + radius - params->orig[2]) / tileHeight);

	// Iterate through potentially relevant tiles
	for (int tz = minTileZ; tz <= maxTileZ; ++tz) {
		for (int tx = minTileX; tx <= maxTileX; ++tx) {
			// Get all tiles at this grid position (can have multiple layers)
			const int maxTiles = 32;
			const dtMeshTile* tiles[maxTiles];
			int tileCount = navMesh->getTilesAt(tx, tz, tiles, maxTiles);

			for (int t = 0; t < tileCount; ++t) {
				const dtMeshTile* tile = tiles[t];
				if (!tile || !tile->header) continue;

				// Iterate through polygons in this tile
				for (int i = 0; i < tile->header->polyCount; ++i) {
					const dtPoly* poly = &tile->polys[i];

					// Skip off-mesh connections
					if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
						continue;

					// Get detail mesh for this polygon
					const dtPolyDetail* pd = &tile->detailMeshes[i];

					// Extract triangles from detail mesh
					for (int j = 0; j < pd->triCount; ++j) {
						const unsigned char* triIndices = &tile->detailTris[(pd->triBase + j) * 4];

						// Get the 3 vertices of this triangle (in Detour Y-up coords)
						float v[3][3];
						for (int k = 0; k < 3; ++k) {
							if (triIndices[k] < poly->vertCount) {
								// Vertex from polygon
								const float* pv = &tile->verts[poly->verts[triIndices[k]] * 3];
								v[k][0] = pv[0];
								v[k][1] = pv[1];
								v[k][2] = pv[2];
							} else {
								// Vertex from detail mesh
								const float* pv = &tile->detailVerts[(pd->vertBase + (triIndices[k] - poly->vertCount)) * 3];
								v[k][0] = pv[0];
								v[k][1] = pv[1];
								v[k][2] = pv[2];
							}
						}

						// Calculate triangle center in Detour Y-up coords
						float triCenterX = (v[0][0] + v[1][0] + v[2][0]) / 3.0f;
						float triCenterZ = (v[0][2] + v[1][2] + v[2][2]) / 3.0f;

						// Check if triangle center is within radius (XZ horizontal distance in Y-up coords)
						float dx = triCenterX - detourCenter[0];
						float dz = triCenterZ - detourCenter[2];
						float distSq = dx * dx + dz * dz;

						if (distSq > radiusSq)
							continue;

						// Create triangle - transform navmesh coords to Irrlicht coords
						// Navmesh (x, y, z) → Irrlicht (z, y, x) - swap X and Z
						Triangle tri;
						tri.v1 = glm::vec3(v[0][2], v[0][1], v[0][0]);
						tri.v2 = glm::vec3(v[1][2], v[1][1], v[1][0]);
						tri.v3 = glm::vec3(v[2][2], v[2][1], v[2][0]);

						// Calculate normal in Irrlicht Y-up coords
						glm::vec3 edge1 = tri.v2 - tri.v1;
						glm::vec3 edge2 = tri.v3 - tri.v1;
						tri.normal = glm::normalize(glm::cross(edge1, edge2));

						// Get area type from polygon
						tri.areaType = poly->getArea();

						result.push_back(tri);
					}
				}
			}
		}
	}

	return result;
}
