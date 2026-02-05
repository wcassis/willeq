#include "client/hc_map.h"
#include "client/raycast_mesh.h"
#include "common/util/compression.h"
#include "common/util/types.h"
#include "common/logging.h"
#include <glm/glm.hpp>
#include <fmt/format.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <cstdio>
#include <vector>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <limits>
#include <map>

struct HCMap::impl {
	RaycastMesh* rm;

	// Store vertices and indices for debug visualization
	// These are in Irrlicht coordinates (Y-up) for direct rendering
	std::vector<glm::vec3> mesh_verts;  // Vertices in Y-up (Irrlicht) format
	std::vector<uint32_t> indices;
	std::vector<bool> triangleIsPlaceable;  // Per-triangle flag: true if from placeable object
	float minZ = 0.0f;
	float maxZ = 0.0f;

	impl() : rm(nullptr) {}
	~impl() {
		if (rm) {
			rm->release();
		}
	}
};

HCMap::HCMap() : m_impl(nullptr) {
}

HCMap::~HCMap() {
	delete m_impl;
}

// Helper functions for V2 map loading
static void RotateVertex(glm::vec3 &v, float rx, float ry, float rz) {
	glm::vec3 nv = v;

	nv.y = (std::cos(rx) * v.y) - (std::sin(rx) * v.z);
	nv.z = (std::sin(rx) * v.y) + (std::cos(rx) * v.z);

	v = nv;

	nv.x = (std::cos(ry) * v.x) + (std::sin(ry) * v.z);
	nv.z = -(std::sin(ry) * v.x) + (std::cos(ry) * v.z);

	v = nv;

	nv.x = (std::cos(rz) * v.x) - (std::sin(rz) * v.y);
	nv.y = (std::sin(rz) * v.x) + (std::cos(rz) * v.y);

	v = nv;
}

static void ScaleVertex(glm::vec3 &v, float sx, float sy, float sz) {
	v.x = v.x * sx;
	v.y = v.y * sy;
	v.z = v.z * sz;
}

static void TranslateVertex(glm::vec3 &v, float tx, float ty, float tz) {
	v.x = v.x + tx;
	v.y = v.y + ty;
	v.z = v.z + tz;
}

struct ModelEntry {
	struct Poly {
		uint32_t v1, v2, v3;
		uint8_t vis;
	};
	std::vector<glm::vec3> verts;
	std::vector<Poly> polys;
};

bool HCMap::Load(const std::string& filename) {
	LOG_DEBUG(MOD_MAP, "HCMap::Load: Attempting to load: {}", filename);
	FILE *f = fopen(filename.c_str(), "rb");
	if (!f) {
		LOG_ERROR(MOD_MAP, "Unable to open map file: {}", filename);
		LOG_ERROR(MOD_MAP, "errno: {} - {}", errno, strerror(errno));
		return false;
	}

	// Get file size for debugging
	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	LOG_DEBUG(MOD_MAP, "Map file size: {} bytes", file_size);

	// Read version
	uint32_t version;
	if (fread(&version, sizeof(version), 1, f) != 1) {
		fclose(f);
		return false;
	}

	LOG_DEBUG(MOD_MAP, "Map version: 0x{:08x}", version);

	if (m_impl) {
		delete m_impl;
	}
	m_impl = new impl;

	bool loaded = false;

	if (version == 0x01000000) {
		// V1 format - simple triangle mesh
		uint32_t face_count;
		uint16_t node_count;
		uint32_t facelist_count;

		if (fread(&face_count, sizeof(face_count), 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		if (fread(&node_count, sizeof(node_count), 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		if (fread(&facelist_count, sizeof(facelist_count), 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		std::vector<glm::vec3> verts;
		std::vector<uint32_t> indices;
		for (uint32_t i = 0; i < face_count; ++i) {
			glm::vec3 a;
			glm::vec3 b;
			glm::vec3 c;
			float normals[4];
			if (fread(&a, sizeof(glm::vec3), 1, f) != 1) {
				fclose(f);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}

			if (fread(&b, sizeof(glm::vec3), 1, f) != 1) {
				fclose(f);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}

			if (fread(&c, sizeof(glm::vec3), 1, f) != 1) {
				fclose(f);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}

			if (fread(normals, sizeof(normals), 1, f) != 1) {
				fclose(f);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}

			size_t sz = verts.size();
			verts.push_back(a);
			indices.push_back((uint32_t)sz);

			verts.push_back(b);
			indices.push_back((uint32_t)sz + 1);

			verts.push_back(c);
			indices.push_back((uint32_t)sz + 2);
		}

		// Swap Y and Z coordinates to convert from EQ's Z-up to internal Y-up format
		// This matches the transformation done by EQEmu server in map.cpp
		for (auto &v : verts) {
			float t = v.y;
			v.y = v.z;
			v.z = t;
		}

		// Store vertices for debug visualization AFTER Y↔Z swap (now in Irrlicht Y-up format)
		m_impl->mesh_verts = verts;
		m_impl->indices = indices;

		// V1 format has no placeables - all triangles are terrain
		m_impl->triangleIsPlaceable.resize(face_count, false);

		// Calculate Y range for color gradient (Y is vertical in Y-up format)
		m_impl->minZ = std::numeric_limits<float>::max();
		m_impl->maxZ = std::numeric_limits<float>::lowest();
		for (const auto& v : verts) {
			if (v.y < m_impl->minZ) m_impl->minZ = v.y;
			if (v.y > m_impl->maxZ) m_impl->maxZ = v.y;
		}

		m_impl->rm = createRaycastMesh((RmUint32)verts.size(), (const RmReal*)&verts[0], face_count, &indices[0]);
		if (m_impl->rm) {
			loaded = true;
			LOG_INFO(MOD_MAP, "Loaded V1 map: {} vertices, {} faces", verts.size(), face_count);
		}
	}
	else if (version == 0x02000000) {
		// V2 format - compressed with models and placements
		LOG_DEBUG(MOD_MAP, "Processing V2 map format (compressed)");

		uint32_t data_size;
		uint32_t buffer_size;

		if (fread(&data_size, sizeof(data_size), 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		if (fread(&buffer_size, sizeof(buffer_size), 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		LOG_DEBUG(MOD_MAP, "V2 map: compressed size = {}, uncompressed size = {}", data_size, buffer_size);

		std::vector<char> compressed_data(data_size);
		if (fread(&compressed_data[0], data_size, 1, f) != 1) {
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		std::vector<char> buffer(buffer_size);
		uint32_t decompressed_size = EQ::InflateData(&compressed_data[0], data_size, &buffer[0], buffer_size);

		if (decompressed_size == 0) {
			LOG_ERROR(MOD_MAP, "Failed to decompress V2 map data");
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		// Parse decompressed data
		char *buf = &buffer[0];
		uint32_t vert_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t ind_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t nc_vert_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t nc_ind_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t model_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t plac_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t plac_group_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t tile_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
		uint32_t quads_per_tile = *(uint32_t*)buf; buf += sizeof(uint32_t);
		float units_per_vertex = *(float*)buf; buf += sizeof(float);

		LOG_DEBUG(MOD_MAP, "V2 map counts: verts={}, indices={}, models={}, placements={}, tiles={}",
			vert_count, ind_count, model_count, plac_count, tile_count);

		std::vector<glm::vec3> verts;
		verts.reserve(vert_count);
		std::vector<uint32_t> indices;
		indices.reserve(ind_count);
		std::vector<bool> triangleIsPlaceable;  // Track which triangles are from placeables

		// Read main vertices
		// Note: Map file has INVERSEXY applied (X and Y are swapped), so we swap them back
		for (uint32_t i = 0; i < vert_count; ++i) {
			float x = *(float*)buf; buf += sizeof(float);
			float y = *(float*)buf; buf += sizeof(float);
			float z = *(float*)buf; buf += sizeof(float);
			verts.emplace_back(y, x, z);  // Swap X/Y to undo INVERSEXY
		}

		// Read main indices
		for (uint32_t i = 0; i < ind_count; ++i) {
			indices.push_back(*(uint32_t*)buf);
			buf += sizeof(uint32_t);
		}

		// Mark main geometry triangles as terrain (not placeables)
		size_t mainTerrainTriangles = ind_count / 3;
		triangleIsPlaceable.resize(mainTerrainTriangles, false);

		// Skip non-collidable geometry
		for (uint32_t i = 0; i < nc_vert_count; ++i) {
			buf += sizeof(float) * 3;
		}
		for (uint32_t i = 0; i < nc_ind_count; ++i) {
			buf += sizeof(uint32_t);
		}

		// Read models
		std::map<std::string, std::unique_ptr<ModelEntry>> models;
		for (uint32_t i = 0; i < model_count; ++i) {
			std::unique_ptr<ModelEntry> me(new ModelEntry);
			std::string name = buf;
			buf += name.length() + 1;

			uint32_t model_vert_count = *(uint32_t*)buf; buf += sizeof(uint32_t);
			uint32_t poly_count = *(uint32_t*)buf; buf += sizeof(uint32_t);

			me->verts.reserve(model_vert_count);
			for (uint32_t j = 0; j < model_vert_count; ++j) {
				float x = *(float*)buf; buf += sizeof(float);
				float y = *(float*)buf; buf += sizeof(float);
				float z = *(float*)buf; buf += sizeof(float);
				me->verts.emplace_back(x, y, z);
			}

			me->polys.reserve(poly_count);
			for (uint32_t j = 0; j < poly_count; ++j) {
				uint32_t v1 = *(uint32_t*)buf; buf += sizeof(uint32_t);
				uint32_t v2 = *(uint32_t*)buf; buf += sizeof(uint32_t);
				uint32_t v3 = *(uint32_t*)buf; buf += sizeof(uint32_t);
				uint8_t vis = *(uint8_t*)buf; buf += sizeof(uint8_t);

				ModelEntry::Poly p;
				p.v1 = v1;
				p.v2 = v2;
				p.v3 = v3;
				p.vis = vis;
				me->polys.push_back(p);
			}

			models[name] = std::move(me);
		}

		// Read placements - these are placeable objects
		for (uint32_t i = 0; i < plac_count; ++i) {
			std::string name = buf;
			buf += name.length() + 1;

			float x = *(float*)buf; buf += sizeof(float);
			float y = *(float*)buf; buf += sizeof(float);
			float z = *(float*)buf; buf += sizeof(float);

			float x_rot = *(float*)buf; buf += sizeof(float);
			float y_rot = *(float*)buf; buf += sizeof(float);
			float z_rot = *(float*)buf; buf += sizeof(float);

			float x_scale = *(float*)buf; buf += sizeof(float);
			float y_scale = *(float*)buf; buf += sizeof(float);
			float z_scale = *(float*)buf; buf += sizeof(float);

			if (models.count(name) == 0)
				continue;

			auto &model = models[name];
			auto &mod_polys = model->polys;
			auto &mod_verts = model->verts;
			for (uint32_t j = 0; j < mod_polys.size(); ++j) {
				auto &current_poly = mod_polys[j];
				if (current_poly.vis == 0)
					continue;
				auto v1 = mod_verts[current_poly.v1];
				auto v2 = mod_verts[current_poly.v2];
				auto v3 = mod_verts[current_poly.v3];

				RotateVertex(v1, x_rot, y_rot, z_rot);
				RotateVertex(v2, x_rot, y_rot, z_rot);
				RotateVertex(v3, x_rot, y_rot, z_rot);

				ScaleVertex(v1, x_scale, y_scale, z_scale);
				ScaleVertex(v2, x_scale, y_scale, z_scale);
				ScaleVertex(v3, x_scale, y_scale, z_scale);

				TranslateVertex(v1, x, y, z);
				TranslateVertex(v2, x, y, z);
				TranslateVertex(v3, x, y, z);

				// Placeables use original order (not X/Y swapped like terrain)
				// to align with S3D rendered objects
				verts.emplace_back(v1.x, v1.y, v1.z);
				verts.emplace_back(v2.x, v2.y, v2.z);
				verts.emplace_back(v3.x, v3.y, v3.z);

				indices.emplace_back((uint32_t)verts.size() - 3);
				indices.emplace_back((uint32_t)verts.size() - 2);
				indices.emplace_back((uint32_t)verts.size() - 1);

				// Mark this triangle as a placeable
				triangleIsPlaceable.push_back(true);
			}
		}

		// Read placement groups (complex object placements) - also placeables
		for (uint32_t i = 0; i < plac_group_count; ++i) {
			float x = *(float*)buf; buf += sizeof(float);
			float y = *(float*)buf; buf += sizeof(float);
			float z = *(float*)buf; buf += sizeof(float);

			float x_rot = *(float*)buf; buf += sizeof(float);
			float y_rot = *(float*)buf; buf += sizeof(float);
			float z_rot = *(float*)buf; buf += sizeof(float);

			float x_scale = *(float*)buf; buf += sizeof(float);
			float y_scale = *(float*)buf; buf += sizeof(float);
			float z_scale = *(float*)buf; buf += sizeof(float);

			float x_tile = *(float*)buf; buf += sizeof(float);
			float y_tile = *(float*)buf; buf += sizeof(float);
			float z_tile = *(float*)buf; buf += sizeof(float);

			uint32_t p_count = *(uint32_t*)buf; buf += sizeof(uint32_t);

			for (uint32_t j = 0; j < p_count; ++j) {
				std::string name = buf;
				buf += name.length() + 1;

				float p_x = *(float*)buf; buf += sizeof(float);
				float p_y = *(float*)buf; buf += sizeof(float);
				float p_z = *(float*)buf; buf += sizeof(float);

				float p_x_rot = *(float*)buf * 3.14159f / 180; buf += sizeof(float);
				float p_y_rot = *(float*)buf * 3.14159f / 180; buf += sizeof(float);
				float p_z_rot = *(float*)buf * 3.14159f / 180; buf += sizeof(float);

				float p_x_scale = *(float*)buf; buf += sizeof(float);
				float p_y_scale = *(float*)buf; buf += sizeof(float);
				float p_z_scale = *(float*)buf; buf += sizeof(float);

				if (models.count(name) == 0)
					continue;

				auto &model = models[name];

				for (size_t k = 0; k < model->polys.size(); ++k) {
					auto &poly = model->polys[k];
					if (poly.vis == 0)
						continue;
					glm::vec3 v1, v2, v3;

					v1 = model->verts[poly.v1];
					v2 = model->verts[poly.v2];
					v3 = model->verts[poly.v3];

					ScaleVertex(v1, p_x_scale, p_y_scale, p_z_scale);
					ScaleVertex(v2, p_x_scale, p_y_scale, p_z_scale);
					ScaleVertex(v3, p_x_scale, p_y_scale, p_z_scale);

					TranslateVertex(v1, p_x, p_y, p_z);
					TranslateVertex(v2, p_x, p_y, p_z);
					TranslateVertex(v3, p_x, p_y, p_z);

					RotateVertex(v1, x_rot * 3.14159f / 180.0f, 0, 0);
					RotateVertex(v2, x_rot * 3.14159f / 180.0f, 0, 0);
					RotateVertex(v3, x_rot * 3.14159f / 180.0f, 0, 0);

					RotateVertex(v1, 0, y_rot * 3.14159f / 180.0f, 0);
					RotateVertex(v2, 0, y_rot * 3.14159f / 180.0f, 0);
					RotateVertex(v3, 0, y_rot * 3.14159f / 180.0f, 0);

					glm::vec3 correction(p_x, p_y, p_z);
					RotateVertex(correction, x_rot * 3.14159f / 180.0f, 0, 0);

					TranslateVertex(v1, -correction.x, -correction.y, -correction.z);
					TranslateVertex(v2, -correction.x, -correction.y, -correction.z);
					TranslateVertex(v3, -correction.x, -correction.y, -correction.z);

					RotateVertex(v1, p_x_rot, 0, 0);
					RotateVertex(v2, p_x_rot, 0, 0);
					RotateVertex(v3, p_x_rot, 0, 0);

					RotateVertex(v1, 0, -p_y_rot, 0);
					RotateVertex(v2, 0, -p_y_rot, 0);
					RotateVertex(v3, 0, -p_y_rot, 0);

					RotateVertex(v1, 0, 0, p_z_rot);
					RotateVertex(v2, 0, 0, p_z_rot);
					RotateVertex(v3, 0, 0, p_z_rot);

					TranslateVertex(v1, correction.x, correction.y, correction.z);
					TranslateVertex(v2, correction.x, correction.y, correction.z);
					TranslateVertex(v3, correction.x, correction.y, correction.z);

					RotateVertex(v1, 0, 0, z_rot * 3.14159f / 180.0f);
					RotateVertex(v2, 0, 0, z_rot * 3.14159f / 180.0f);
					RotateVertex(v3, 0, 0, z_rot * 3.14159f / 180.0f);

					ScaleVertex(v1, x_scale, y_scale, z_scale);
					ScaleVertex(v2, x_scale, y_scale, z_scale);
					ScaleVertex(v3, x_scale, y_scale, z_scale);

					TranslateVertex(v1, x_tile, y_tile, z_tile);
					TranslateVertex(v2, x_tile, y_tile, z_tile);
					TranslateVertex(v3, x_tile, y_tile, z_tile);

					TranslateVertex(v1, x, y, z);
					TranslateVertex(v2, x, y, z);
					TranslateVertex(v3, x, y, z);

					// Placeables use original order (not X/Y swapped like terrain)
					// to align with S3D rendered objects
					verts.emplace_back(v1.x, v1.y, v1.z);
					verts.emplace_back(v2.x, v2.y, v2.z);
					verts.emplace_back(v3.x, v3.y, v3.z);

					indices.emplace_back((uint32_t)verts.size() - 3);
					indices.emplace_back((uint32_t)verts.size() - 2);
					indices.emplace_back((uint32_t)verts.size() - 1);

					// Mark this triangle as a placeable
					triangleIsPlaceable.push_back(true);
				}
			}
		}

		// Read terrain tiles - these are NOT placeables
		size_t triangleCountBeforeTiles = triangleIsPlaceable.size();
		uint32_t ter_quad_count = (quads_per_tile * quads_per_tile);
		uint32_t ter_vert_count = ((quads_per_tile + 1) * (quads_per_tile + 1));
		std::vector<uint8_t> flags;
		std::vector<float> floats;
		flags.resize(ter_quad_count);
		floats.resize(ter_vert_count);

		for (uint32_t i = 0; i < tile_count; ++i) {
			bool flat = *(bool*)buf; buf += sizeof(bool);

			float tile_x = *(float*)buf; buf += sizeof(float);
			float tile_y = *(float*)buf; buf += sizeof(float);

			if (flat) {
				float tile_z = *(float*)buf; buf += sizeof(float);

				float QuadVertex1X = tile_x;
				float QuadVertex1Y = tile_y;
				float QuadVertex1Z = tile_z;

				float QuadVertex2X = QuadVertex1X + (quads_per_tile * units_per_vertex);
				float QuadVertex2Y = QuadVertex1Y;
				float QuadVertex2Z = QuadVertex1Z;

				float QuadVertex3X = QuadVertex2X;
				float QuadVertex3Y = QuadVertex1Y + (quads_per_tile * units_per_vertex);
				float QuadVertex3Z = QuadVertex1Z;

				float QuadVertex4X = QuadVertex1X;
				float QuadVertex4Y = QuadVertex3Y;
				float QuadVertex4Z = QuadVertex1Z;

				uint32_t current_vert = (uint32_t)verts.size() + 3;
				// Swap X/Y to undo INVERSEXY (same as main vertices)
				verts.emplace_back(QuadVertex1Y, QuadVertex1X, QuadVertex1Z);
				verts.emplace_back(QuadVertex2Y, QuadVertex2X, QuadVertex2Z);
				verts.emplace_back(QuadVertex3Y, QuadVertex3X, QuadVertex3Z);
				verts.emplace_back(QuadVertex4Y, QuadVertex4X, QuadVertex4Z);

				indices.emplace_back(current_vert);
				indices.emplace_back(current_vert - 2);
				indices.emplace_back(current_vert - 1);

				indices.emplace_back(current_vert);
				indices.emplace_back(current_vert - 3);
				indices.emplace_back(current_vert - 2);
			}
			else {
				// Read flags
				for (uint32_t j = 0; j < ter_quad_count; ++j) {
					flags[j] = *(uint8_t*)buf;
					buf += sizeof(uint8_t);
				}

				// Read floats
				for (uint32_t j = 0; j < ter_vert_count; ++j) {
					floats[j] = *(float*)buf;
					buf += sizeof(float);
				}

				int row_number = -1;
				std::map<std::tuple<float, float, float>, uint32_t> cur_verts;
				for (uint32_t quad = 0; quad < ter_quad_count; ++quad) {
					if ((quad % quads_per_tile) == 0) {
						++row_number;
					}

					if (flags[quad] & 0x01)
						continue;

					float QuadVertex1X = tile_x + (row_number * units_per_vertex);
					float QuadVertex1Y = tile_y + (quad % quads_per_tile) * units_per_vertex;
					float QuadVertex1Z = floats[quad + row_number];

					float QuadVertex2X = QuadVertex1X + units_per_vertex;
					float QuadVertex2Y = QuadVertex1Y;
					float QuadVertex2Z = floats[quad + row_number + quads_per_tile + 1];

					float QuadVertex3X = QuadVertex1X + units_per_vertex;
					float QuadVertex3Y = QuadVertex1Y + units_per_vertex;
					float QuadVertex3Z = floats[quad + row_number + quads_per_tile + 2];

					float QuadVertex4X = QuadVertex1X;
					float QuadVertex4Y = QuadVertex1Y + units_per_vertex;
					float QuadVertex4Z = floats[quad + row_number + 1];

					uint32_t i1, i2, i3, i4;
					// Note: Tuple keys use file coordinates for deduplication,
					// but emplace uses swapped X/Y to undo INVERSEXY
					std::tuple<float, float, float> t = std::make_tuple(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
					auto iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i1 = iter->second;
					}
					else {
						i1 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex1Y, QuadVertex1X, QuadVertex1Z);
						cur_verts[t] = i1;
					}

					t = std::make_tuple(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i2 = iter->second;
					}
					else {
						i2 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex2Y, QuadVertex2X, QuadVertex2Z);
						cur_verts[t] = i2;
					}

					t = std::make_tuple(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i3 = iter->second;
					}
					else {
						i3 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex3Y, QuadVertex3X, QuadVertex3Z);
						cur_verts[t] = i3;
					}

					t = std::make_tuple(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i4 = iter->second;
					}
					else {
						i4 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex4Y, QuadVertex4X, QuadVertex4Z);
						cur_verts[t] = i4;
					}

					indices.emplace_back(i4);
					indices.emplace_back(i2);
					indices.emplace_back(i3);

					indices.emplace_back(i4);
					indices.emplace_back(i1);
					indices.emplace_back(i2);
				}
			}
		}

		// Mark terrain tile triangles as non-placeables
		size_t totalTriangles = indices.size() / 3;
		size_t tileTriangles = totalTriangles - triangleIsPlaceable.size();
		for (size_t i = 0; i < tileTriangles; ++i) {
			triangleIsPlaceable.push_back(false);
		}

		// Swap Y and Z coordinates to convert from EQ's Z-up to internal Y-up format
		// This matches the transformation done by EQEmu server in map.cpp
		for (auto &v : verts) {
			float t = v.y;
			v.y = v.z;
			v.z = t;
		}

		// Store vertices for debug visualization AFTER Y↔Z swap (now in Irrlicht Y-up format)
		m_impl->mesh_verts = verts;
		m_impl->indices = indices;
		m_impl->triangleIsPlaceable = triangleIsPlaceable;

		// Calculate Y range for color gradient (Y is vertical in Y-up format)
		m_impl->minZ = std::numeric_limits<float>::max();
		m_impl->maxZ = std::numeric_limits<float>::lowest();
		for (const auto& v : verts) {
			if (v.y < m_impl->minZ) m_impl->minZ = v.y;
			if (v.y > m_impl->maxZ) m_impl->maxZ = v.y;
		}

		uint32_t face_count = indices.size() / 3;
		size_t placeableCount = std::count(triangleIsPlaceable.begin(), triangleIsPlaceable.end(), true);
		if (face_count > 0 && verts.size() > 0) {
			m_impl->rm = createRaycastMesh((RmUint32)verts.size(), (const RmReal*)&verts[0], face_count, &indices[0]);
			if (m_impl->rm) {
				loaded = true;
				LOG_INFO(MOD_MAP, "Loaded V2 map: {} vertices, {} faces ({} placeables, {} terrain)",
				         verts.size(), face_count, placeableCount, face_count - placeableCount);
			}
		}
	}

	fclose(f);

	if (!loaded) {
		delete m_impl;
		m_impl = nullptr;
		return false;
	}

	return true;
}

float HCMap::FindBestZ(const glm::vec3 &start, glm::vec3 *result) const {
	if (!m_impl || !m_impl->rm) {
		return BEST_Z_INVALID;
	}

	glm::vec3 tmp;
	if (!result) {
		result = &tmp;
	}

	// Convert EQ coords (Z-up) to mesh coords (Y-up): EQ(x,y,z) -> Mesh(x,z,y)
	// Cast ray down from start position (down is -Y in mesh coords)
	glm::vec3 from(start.x, start.z + 10.0f, start.y); // Start slightly above
	glm::vec3 to(start.x, BEST_Z_INVALID, start.y);    // Cast down in Y direction
	glm::vec3 mesh_result;
	float hit_distance;
	bool hit = false;

	hit = m_impl->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)&mesh_result, nullptr, &hit_distance);

	if (hit) {
		// Convert mesh coords back to EQ coords: Mesh(x,y,z) -> EQ(x,z,y)
		result->x = mesh_result.x;
		result->y = mesh_result.z;
		result->z = mesh_result.y;
		return result->z;
	}

	// Try casting ray up if nothing found below
	to.y = -BEST_Z_INVALID;
	hit = m_impl->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)&mesh_result, nullptr, &hit_distance);

	if (hit) {
		// Convert mesh coords back to EQ coords
		result->x = mesh_result.x;
		result->y = mesh_result.z;
		result->z = mesh_result.y;
		return result->z;
	}

	return BEST_Z_INVALID;
}

bool HCMap::CheckLOS(const glm::vec3& start, const glm::vec3& end) const {
	if (!m_impl || !m_impl->rm) {
		return true; // No map means no obstacles
	}

	// Convert EQ coords (Z-up) to mesh coords (Y-up): EQ(x,y,z) -> Mesh(x,z,y)
	glm::vec3 mesh_start(start.x, start.z, start.y);
	glm::vec3 mesh_end(end.x, end.z, end.y);

	// Return true if there is NO intersection (clear line of sight)
	return !m_impl->rm->raycast((const RmReal*)&mesh_start, (const RmReal*)&mesh_end, nullptr, nullptr, nullptr);
}

bool HCMap::CheckLOSWithHit(const glm::vec3& start, const glm::vec3& end, glm::vec3* hitLocation) const {
	if (!m_impl || !m_impl->rm) {
		return true; // No map means no obstacles
	}

	// Convert EQ coords (Z-up) to mesh coords (Y-up): EQ(x,y,z) -> Mesh(x,z,y)
	glm::vec3 mesh_start(start.x, start.z, start.y);
	glm::vec3 mesh_end(end.x, end.z, end.y);

	glm::vec3 mesh_hit;
	bool hasHit = m_impl->rm->raycast((const RmReal*)&mesh_start, (const RmReal*)&mesh_end, (RmReal*)&mesh_hit, nullptr, nullptr);

	if (hasHit && hitLocation) {
		// Convert mesh coords back to EQ coords: Mesh(x,y,z) -> EQ(x,z,y)
		hitLocation->x = mesh_hit.x;
		hitLocation->y = mesh_hit.z;
		hitLocation->z = mesh_hit.y;
	}

	// Return true if there is NO intersection (clear line of sight)
	return !hasHit;
}

HCMap* HCMap::LoadMapFile(const std::string& zone_name, const std::string& maps_path) {
	std::string filename = fmt::format("{}/{}.map", maps_path, zone_name);

	HCMap* map = new HCMap();
	if (map->Load(filename)) {
		LOG_INFO(MOD_MAP, "Successfully loaded map for zone: {}", zone_name);
		return map;
	}

	delete map;
	return nullptr;
}

std::vector<HCMap::Triangle> HCMap::GetTrianglesInRadius(const glm::vec3& center, float radius) const {
	std::vector<Triangle> result;

	if (!m_impl || m_impl->mesh_verts.empty() || m_impl->indices.empty()) {
		return result;
	}

	float radiusSq = radius * radius;
	size_t numTriangles = m_impl->indices.size() / 3;

	for (size_t i = 0; i < numTriangles; ++i) {
		uint32_t i1 = m_impl->indices[i * 3];
		uint32_t i2 = m_impl->indices[i * 3 + 1];
		uint32_t i3 = m_impl->indices[i * 3 + 2];

		if (i1 >= m_impl->mesh_verts.size() || i2 >= m_impl->mesh_verts.size() || i3 >= m_impl->mesh_verts.size()) {
			continue;
		}

		const glm::vec3& v1 = m_impl->mesh_verts[i1];
		const glm::vec3& v2 = m_impl->mesh_verts[i2];
		const glm::vec3& v3 = m_impl->mesh_verts[i3];

		// Calculate triangle center
		glm::vec3 triCenter = (v1 + v2 + v3) / 3.0f;

		// Check if triangle center is within radius (using XZ distance in Y-up coords)
		float dx = triCenter.x - center.x;
		float dz = triCenter.z - center.z;
		float distSq = dx * dx + dz * dz;

		if (distSq <= radiusSq) {
			Triangle tri;
			tri.v1 = v1;
			tri.v2 = v2;
			tri.v3 = v3;

			// Calculate face normal using cross product
			glm::vec3 edge1 = v2 - v1;
			glm::vec3 edge2 = v3 - v1;
			tri.normal = glm::normalize(glm::cross(edge1, edge2));

			// Set placeable flag if we have the data
			tri.isPlaceable = (i < m_impl->triangleIsPlaceable.size()) ? m_impl->triangleIsPlaceable[i] : false;

			result.push_back(tri);
		}
	}

	return result;
}

void HCMap::GetZRange(float& minZ, float& maxZ) const {
	if (!m_impl) {
		minZ = 0.0f;
		maxZ = 0.0f;
		return;
	}

	minZ = m_impl->minZ;
	maxZ = m_impl->maxZ;
}
