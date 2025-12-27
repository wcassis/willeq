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
#include <map>

struct HCMap::impl {
	RaycastMesh* rm;

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

		// Read main vertices
		for (uint32_t i = 0; i < vert_count; ++i) {
			float x = *(float*)buf; buf += sizeof(float);
			float y = *(float*)buf; buf += sizeof(float);
			float z = *(float*)buf; buf += sizeof(float);
			verts.emplace_back(x, y, z);
		}

		// Read main indices
		for (uint32_t i = 0; i < ind_count; ++i) {
			indices.push_back(*(uint32_t*)buf);
			buf += sizeof(uint32_t);
		}

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

		// Read placements
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

				verts.emplace_back(v1.y, v1.x, v1.z); // x/y swapped
				verts.emplace_back(v2.y, v2.x, v2.z);
				verts.emplace_back(v3.y, v3.x, v3.z);

				indices.emplace_back((uint32_t)verts.size() - 3);
				indices.emplace_back((uint32_t)verts.size() - 2);
				indices.emplace_back((uint32_t)verts.size() - 1);
			}
		}

		// Read placement groups (complex object placements)
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

					verts.emplace_back(v1.y, v1.x, v1.z); // x/y swapped
					verts.emplace_back(v2.y, v2.x, v2.z);
					verts.emplace_back(v3.y, v3.x, v3.z);

					indices.emplace_back((uint32_t)verts.size() - 3);
					indices.emplace_back((uint32_t)verts.size() - 2);
					indices.emplace_back((uint32_t)verts.size() - 1);
				}
			}
		}

		// Read terrain tiles
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
				verts.emplace_back(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
				verts.emplace_back(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
				verts.emplace_back(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
				verts.emplace_back(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);

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
					std::tuple<float, float, float> t = std::make_tuple(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
					auto iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i1 = iter->second;
					}
					else {
						i1 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
						cur_verts[t] = i1;
					}

					t = std::make_tuple(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i2 = iter->second;
					}
					else {
						i2 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
						cur_verts[t] = i2;
					}

					t = std::make_tuple(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i3 = iter->second;
					}
					else {
						i3 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
						cur_verts[t] = i3;
					}

					t = std::make_tuple(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);
					iter = cur_verts.find(t);
					if (iter != cur_verts.end()) {
						i4 = iter->second;
					}
					else {
						i4 = (uint32_t)verts.size();
						verts.emplace_back(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);
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

		uint32_t face_count = indices.size() / 3;
		if (face_count > 0 && verts.size() > 0) {
			m_impl->rm = createRaycastMesh((RmUint32)verts.size(), (const RmReal*)&verts[0], face_count, &indices[0]);
			if (m_impl->rm) {
				loaded = true;
				LOG_INFO(MOD_MAP, "Loaded V2 map: {} vertices, {} faces", verts.size(), face_count);
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

	// Cast ray down from start position
	glm::vec3 from(start.x, start.y, start.z + 10.0f); // Start slightly above
	glm::vec3 to(start.x, start.y, BEST_Z_INVALID);
	float hit_distance;
	bool hit = false;

	hit = m_impl->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);

	if (hit) {
		return result->z;
	}

	// Try casting ray up if nothing found below
	to.z = -BEST_Z_INVALID;
	hit = m_impl->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);

	if (hit) {
		return result->z;
	}

	return BEST_Z_INVALID;
}

bool HCMap::CheckLOS(const glm::vec3& start, const glm::vec3& end) const {
	if (!m_impl || !m_impl->rm) {
		return true; // No map means no obstacles
	}

	// Return true if there is NO intersection (clear line of sight)
	return !m_impl->rm->raycast((const RmReal*)&start, (const RmReal*)&end, nullptr, nullptr, nullptr);
}

bool HCMap::CheckLOSWithHit(const glm::vec3& start, const glm::vec3& end, glm::vec3* hitLocation) const {
	if (!m_impl || !m_impl->rm) {
		return true; // No map means no obstacles
	}

	glm::vec3 hit;
	bool hasHit = m_impl->rm->raycast((const RmReal*)&start, (const RmReal*)&end, (RmReal*)&hit, nullptr, nullptr);

	if (hasHit && hitLocation) {
		*hitLocation = hit;
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
