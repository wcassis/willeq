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
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

// T20: every count and length inside a V2 map's decompressed payload is
// untrusted file data, and the readers below advance a raw pointer. Before
// T20 none of them checked the buffer end, so a corrupt or hostile count
// (e.g. an oversized vert_count) walked off the heap and segfaulted. The
// readers now throw MapParseError, which HCMap::Load catches to fail the
// load cleanly.
struct MapParseError : public std::runtime_error {
	explicit MapParseError(const char* what) : std::runtime_error(what) {}
};

// Helper for safe unaligned reads from binary buffers (required for ARM)
template<typename T>
static inline T read_val(char*& buf, const char* end) {
	if (end - buf < static_cast<ptrdiff_t>(sizeof(T))) {
		throw MapParseError("read past end of decompressed map buffer");
	}
	T value;
	memcpy(&value, buf, sizeof(T));
	buf += sizeof(T);
	return value;
}

// NUL-terminated inline string; the terminator must lie inside the buffer.
static inline std::string read_str(char*& buf, const char* end) {
	const char* nul = static_cast<const char*>(memchr(buf, '\0', static_cast<size_t>(end - buf)));
	if (!nul) {
		throw MapParseError("unterminated string in decompressed map buffer");
	}
	std::string value(buf);
	buf += value.length() + 1;
	return value;
}

// Skip `bytes` of payload (non-collidable geometry we do not keep).
static inline void skip_bytes(char*& buf, const char* end, size_t bytes) {
	if (static_cast<size_t>(end - buf) < bytes) {
		throw MapParseError("skip past end of decompressed map buffer");
	}
	buf += bytes;
}

// B1009 (GitLab #330): a count read from the file is trusted only once the
// bytes it claims fit in what remains. Reserving on the count alone turned a
// malicious 0x7FFFFFFF vert_count into a 25 GB std::vector reservation: it
// threw std::bad_alloc out of Load() on a 15 GB host (gitlab-runner-1) and
// only "passed" on the 31 GB hermes-ext because overcommit let the reserve
// through until the reads ran off the buffer. Same failure path as the
// readers: MapParseError, which Load() catches.
static inline uint32_t bounded_count(uint32_t count, size_t bytes_each, const char* buf, const char* end,
                                     const char* what) {
	const size_t remain = end > buf ? static_cast<size_t>(end - buf) : 0;
	if (bytes_each != 0 && static_cast<size_t>(count) > remain / bytes_each) {
		throw MapParseError((std::string("map count exceeds the decompressed buffer: ") + what).c_str());
	}
	return count;
}

struct HCMap::impl {
	RaycastMesh* rm;

	// Vertices for debug visualization and the bounds queries, in Irrlicht
	// coordinates (Y-up). T504 (load-step audit): a VIEW onto the
	// RaycastMesh's own array, not a copy (the copy was 2.3-4.4 MiB in
	// iceclad / gfaydark / dreadlands). Valid while `rm` lives; bindViews() is
	// called right after it is created. B951: triangles are read from the mesh
	// one at a time (rm->getTriangle), in the mesh's STORED order (leaf order,
	// not file order), and the placeable flag is the mesh's per-triangle bit.
	template <typename T> struct ArrayView {
		const T* p = nullptr;
		size_t n = 0;
		size_t size() const { return n; }
		bool empty() const { return n == 0; }
		const T& operator[](size_t i) const { return p[i]; }
		const T* begin() const { return p; }
		const T* end() const { return p + n; }
	};
	ArrayView<glm::vec3> mesh_verts;  // Vertices in Y-up (Irrlicht) format
	size_t triangleCount = 0;
	void bindViews() {
		mesh_verts = {};
		triangleCount = 0;
		if (!rm) return;
		static_assert(sizeof(glm::vec3) == 3 * sizeof(RmReal), "vec3 must be three RmReal");
		mesh_verts.p = reinterpret_cast<const glm::vec3*>(rm->getVertices());
		mesh_verts.n = rm->getVertexCount();
		triangleCount = rm->getTriangleCount();
	}
	// Fills `tri` (vertices, normal, placeable) for stored triangle `i`;
	// false when an index is out of range (never for a mesh this file built).
	bool triangleAt(size_t i, Triangle& tri) const {
		RmUint32 idx[3];
		rm->getTriangle(static_cast<RmUint32>(i), idx);
		if (idx[0] >= mesh_verts.size() || idx[1] >= mesh_verts.size() || idx[2] >= mesh_verts.size()) {
			return false;
		}
		tri.v1 = mesh_verts[idx[0]];
		tri.v2 = mesh_verts[idx[1]];
		tri.v3 = mesh_verts[idx[2]];
		tri.isPlaceable = rm->getTriangleFlag(static_cast<RmUint32>(i));
		return true;
	}
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

		// B941: flat float triples, moved into the raycast mesh (no copy).
		std::vector<float> verts;
		std::vector<uint32_t> indices;
		// B1009: a V1 face is at least nine floats on disk; a face_count the
		// file cannot hold is a malformed map, not a reservation.
		if (file_size > 0 && static_cast<size_t>(face_count) > static_cast<size_t>(file_size) / (9 * sizeof(float))) {
			LOG_ERROR(MOD_MAP, "V1 map face_count {} exceeds the file ({} bytes)", face_count, file_size);
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}
		verts.reserve(static_cast<size_t>(face_count) * 9);
		indices.reserve(static_cast<size_t>(face_count) * 3);
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

			const uint32_t sz = (uint32_t)(verts.size() / 3);
			// Swap Y and Z to convert from EQ's Z-up to the internal Y-up format
			// (the transformation EQEmu's map.cpp does).
			for (const glm::vec3* v : {&a, &b, &c}) {
				verts.push_back(v->x);
				verts.push_back(v->z);
				verts.push_back(v->y);
			}
			indices.push_back(sz);
			indices.push_back(sz + 1);
			indices.push_back(sz + 2);
		}

		// Calculate Y range for color gradient (Y is vertical in Y-up format)
		m_impl->minZ = std::numeric_limits<float>::max();
		m_impl->maxZ = std::numeric_limits<float>::lowest();
		for (size_t i = 1; i < verts.size(); i += 3) {
			if (verts[i] < m_impl->minZ) m_impl->minZ = verts[i];
			if (verts[i] > m_impl->maxZ) m_impl->maxZ = verts[i];
		}

		const size_t vertexCount = verts.size() / 3;
		m_impl->rm = createRaycastMesh(std::move(verts), std::move(indices));  // no placeables
		m_impl->bindViews();  // T504
		if (m_impl->rm) {
			loaded = true;
			LOG_INFO(MOD_MAP, "Loaded V1 map: {} vertices, {} faces", vertexCount, face_count);
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

		std::vector<char> buffer(buffer_size);
		uint32_t decompressed_size = 0;
		{
			// B941: the compressed bytes live only as long as the inflate.
			std::vector<char> compressed_data(data_size);
			if (fread(&compressed_data[0], data_size, 1, f) != 1) {
				fclose(f);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}
			decompressed_size = EQ::InflateData(&compressed_data[0], data_size, &buffer[0], buffer_size);
		}

		if (decompressed_size == 0) {
			LOG_ERROR(MOD_MAP, "Failed to decompress V2 map data");
			fclose(f);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		// Parse decompressed data. T20: `end` bounds every read below;
		// MapParseError from any of them aborts the load cleanly instead of
		// running off the heap. Only the bytes InflateData actually produced
		// are in play, which may be fewer than buffer_size claimed.
		char *buf = &buffer[0];
		const char *end = buffer.data() + std::min<size_t>(decompressed_size, buffer.size());
		try {
		uint32_t vert_count = read_val<uint32_t>(buf, end);
		uint32_t ind_count = read_val<uint32_t>(buf, end);
		uint32_t nc_vert_count = read_val<uint32_t>(buf, end);
		uint32_t nc_ind_count = read_val<uint32_t>(buf, end);
		uint32_t model_count = read_val<uint32_t>(buf, end);
		uint32_t plac_count = read_val<uint32_t>(buf, end);
		uint32_t plac_group_count = read_val<uint32_t>(buf, end);
		uint32_t tile_count = read_val<uint32_t>(buf, end);
		uint32_t quads_per_tile = read_val<uint32_t>(buf, end);
		float units_per_vertex = read_val<float>(buf, end);

		LOG_DEBUG(MOD_MAP, "V2 map counts: verts={}, indices={}, models={}, placements={}, tiles={}",
			vert_count, ind_count, model_count, plac_count, tile_count);

		// B941 (GitLab #259): flat float triples, handed to the raycast mesh by
		// move. Every vertex is stored ONCE per placement: a placed model's
		// triangles index the placement's transformed copy of each model vertex
		// they use, instead of each triangle carrying three private vertices
		// (emeraldjungle: 1,683,062 -> see the [raycastmesh] line). Triangle
		// order and every vertex VALUE are unchanged -- each vertex goes through
		// exactly the per-vertex transform the old per-triangle loop applied --
		// so every query is bit-identical (tests/test_game_state_identity.cpp).
		std::vector<float> verts;
		verts.reserve(static_cast<size_t>(bounded_count(vert_count, 3 * sizeof(float), buf, end, "vert_count")) * 3);
		std::vector<uint32_t> indices;
		indices.reserve(bounded_count(ind_count, sizeof(uint32_t), buf, end, "ind_count"));
		auto vertexCount = [&verts]() { return static_cast<uint32_t>(verts.size() / 3); };
		auto pushVertex = [&verts](float x, float y, float z) {
			verts.push_back(x);
			verts.push_back(y);
			verts.push_back(z);
		};

		// Read main vertices
		// Note: Map file has INVERSEXY applied (X and Y are swapped), so we swap them back
		for (uint32_t i = 0; i < vert_count; ++i) {
			float x = read_val<float>(buf, end);
			float y = read_val<float>(buf, end);
			float z = read_val<float>(buf, end);
			pushVertex(y, x, z);  // Swap X/Y to undo INVERSEXY
		}

		// Read main indices
		for (uint32_t i = 0; i < ind_count; ++i) {
			indices.push_back(read_val<uint32_t>(buf, end));
		}

		// Main geometry triangles are terrain; the placeables start here.
		const size_t mainTerrainTriangles = ind_count / 3;
		size_t placeableTriangles = 0;

		// Skip non-collidable geometry
		skip_bytes(buf, end, static_cast<size_t>(nc_vert_count) * sizeof(float) * 3);
		skip_bytes(buf, end, static_cast<size_t>(nc_ind_count) * sizeof(uint32_t));

		// Read models
		std::map<std::string, std::unique_ptr<ModelEntry>> models;
		for (uint32_t i = 0; i < model_count; ++i) {
			std::unique_ptr<ModelEntry> me(new ModelEntry);
			std::string name = read_str(buf, end);

			uint32_t model_vert_count = read_val<uint32_t>(buf, end);
			uint32_t poly_count = read_val<uint32_t>(buf, end);

			me->verts.reserve(bounded_count(model_vert_count, 3 * sizeof(float), buf, end, "model_vert_count"));
			for (uint32_t j = 0; j < model_vert_count; ++j) {
				float x = read_val<float>(buf, end);
				float y = read_val<float>(buf, end);
				float z = read_val<float>(buf, end);
				me->verts.emplace_back(x, y, z);
			}

			me->polys.reserve(bounded_count(poly_count, 3 * sizeof(uint32_t), buf, end, "poly_count"));
			for (uint32_t j = 0; j < poly_count; ++j) {
				uint32_t v1 = read_val<uint32_t>(buf, end);
				uint32_t v2 = read_val<uint32_t>(buf, end);
				uint32_t v3 = read_val<uint32_t>(buf, end);
				uint8_t vis = read_val<uint8_t>(buf, end);

				// B941: a poly index outside its model used to read past the
				// model's vertex array; it now fails the load (T20's rule).
				if (v1 >= model_vert_count || v2 >= model_vert_count || v3 >= model_vert_count) {
					throw MapParseError("model poly references a vertex outside its model");
				}
				ModelEntry::Poly p;
				p.v1 = v1;
				p.v2 = v2;
				p.v3 = v3;
				p.vis = vis;
				me->polys.push_back(p);
			}

			// B941: many model vertex lists repeat a position (split per
			// texture/normal in the source mesh). Point every poly at the FIRST
			// vertex with the same bits: the transform is a pure function of
			// the bits, so the emitted value is identical and the placement
			// stores it once.
			{
				std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> firstWithBits;
				std::vector<uint32_t> canon(me->verts.size());
				for (uint32_t j = 0; j < me->verts.size(); ++j) {
					uint32_t b[3];
					std::memcpy(b, &me->verts[j], sizeof(b));
					canon[j] = firstWithBits.emplace(std::make_tuple(b[0], b[1], b[2]), j).first->second;
				}
				for (auto& p : me->polys) {
					p.v1 = canon[p.v1];
					p.v2 = canon[p.v2];
					p.v3 = canon[p.v3];
				}
			}

			models[name] = std::move(me);
		}

		// Emits one placement of `model`: each model vertex a visible poly uses
		// is transformed by `xform` once (first use) and indexed after that.
		std::vector<uint32_t> remap;
		auto emitPlacement = [&](const ModelEntry& model, const auto& xform) {
			remap.assign(model.verts.size(), UINT32_MAX);
			for (const auto& poly : model.polys) {
				if (poly.vis == 0)
					continue;
				for (const uint32_t mv : {poly.v1, poly.v2, poly.v3}) {
					if (remap[mv] == UINT32_MAX) {
						glm::vec3 v = model.verts[mv];
						xform(v);
						remap[mv] = vertexCount();
						// Placeables use original order (not X/Y swapped like terrain)
						// to align with S3D rendered objects
						pushVertex(v.x, v.y, v.z);
					}
					indices.push_back(remap[mv]);
				}
				++placeableTriangles;
			}
		};

		// Read placements - these are placeable objects
		for (uint32_t i = 0; i < plac_count; ++i) {
			std::string name = read_str(buf, end);

			float x = read_val<float>(buf, end);
			float y = read_val<float>(buf, end);
			float z = read_val<float>(buf, end);

			float x_rot = read_val<float>(buf, end);
			float y_rot = read_val<float>(buf, end);
			float z_rot = read_val<float>(buf, end);

			float x_scale = read_val<float>(buf, end);
			float y_scale = read_val<float>(buf, end);
			float z_scale = read_val<float>(buf, end);

			auto it = models.find(name);
			if (it == models.end())
				continue;

			emitPlacement(*it->second, [&](glm::vec3& v) {
				RotateVertex(v, x_rot, y_rot, z_rot);
				ScaleVertex(v, x_scale, y_scale, z_scale);
				TranslateVertex(v, x, y, z);
			});
		}

		// Read placement groups (complex object placements) - also placeables
		for (uint32_t i = 0; i < plac_group_count; ++i) {
			float x = read_val<float>(buf, end);
			float y = read_val<float>(buf, end);
			float z = read_val<float>(buf, end);

			float x_rot = read_val<float>(buf, end);
			float y_rot = read_val<float>(buf, end);
			float z_rot = read_val<float>(buf, end);

			float x_scale = read_val<float>(buf, end);
			float y_scale = read_val<float>(buf, end);
			float z_scale = read_val<float>(buf, end);

			float x_tile = read_val<float>(buf, end);
			float y_tile = read_val<float>(buf, end);
			float z_tile = read_val<float>(buf, end);

			uint32_t p_count = read_val<uint32_t>(buf, end);

			for (uint32_t j = 0; j < p_count; ++j) {
				std::string name = read_str(buf, end);

				float p_x = read_val<float>(buf, end);
				float p_y = read_val<float>(buf, end);
				float p_z = read_val<float>(buf, end);

				float p_x_rot = read_val<float>(buf, end) * 3.14159f / 180;
				float p_y_rot = read_val<float>(buf, end) * 3.14159f / 180;
				float p_z_rot = read_val<float>(buf, end) * 3.14159f / 180;

				float p_x_scale = read_val<float>(buf, end);
				float p_y_scale = read_val<float>(buf, end);
				float p_z_scale = read_val<float>(buf, end);

				auto it = models.find(name);
				if (it == models.end())
					continue;

				glm::vec3 correction(p_x, p_y, p_z);
				RotateVertex(correction, x_rot * 3.14159f / 180.0f, 0, 0);

				// The old per-triangle sequence, applied to one vertex.
				emitPlacement(*it->second, [&](glm::vec3& v) {
					ScaleVertex(v, p_x_scale, p_y_scale, p_z_scale);
					TranslateVertex(v, p_x, p_y, p_z);
					RotateVertex(v, x_rot * 3.14159f / 180.0f, 0, 0);
					RotateVertex(v, 0, y_rot * 3.14159f / 180.0f, 0);
					TranslateVertex(v, -correction.x, -correction.y, -correction.z);
					RotateVertex(v, p_x_rot, 0, 0);
					RotateVertex(v, 0, -p_y_rot, 0);
					RotateVertex(v, 0, 0, p_z_rot);
					TranslateVertex(v, correction.x, correction.y, correction.z);
					RotateVertex(v, 0, 0, z_rot * 3.14159f / 180.0f);
					ScaleVertex(v, x_scale, y_scale, z_scale);
					TranslateVertex(v, x_tile, y_tile, z_tile);
					TranslateVertex(v, x, y, z);
				});
			}
		}
		remap.clear();
		remap.shrink_to_fit();
		models.clear();  // B941: model templates are parse-time only

		// Read terrain tiles - these are NOT placeables
		uint32_t ter_quad_count = (quads_per_tile * quads_per_tile);
		uint32_t ter_vert_count = ((quads_per_tile + 1) * (quads_per_tile + 1));
		std::vector<uint8_t> flags;
		std::vector<float> floats;
		flags.resize(ter_quad_count);
		floats.resize(ter_vert_count);

		for (uint32_t i = 0; i < tile_count; ++i) {
			bool flat = read_val<bool>(buf, end);

			float tile_x = read_val<float>(buf, end);
			float tile_y = read_val<float>(buf, end);

			if (flat) {
				float tile_z = read_val<float>(buf, end);

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

				uint32_t current_vert = vertexCount() + 3;
				// Swap X/Y to undo INVERSEXY (same as main vertices)
				pushVertex(QuadVertex1Y, QuadVertex1X, QuadVertex1Z);
				pushVertex(QuadVertex2Y, QuadVertex2X, QuadVertex2Z);
				pushVertex(QuadVertex3Y, QuadVertex3X, QuadVertex3Z);
				pushVertex(QuadVertex4Y, QuadVertex4X, QuadVertex4Z);

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
					flags[j] = read_val<uint8_t>(buf, end);
				}

				// Read floats
				for (uint32_t j = 0; j < ter_vert_count; ++j) {
					floats[j] = read_val<float>(buf, end);
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

					// Note: Tuple keys use file coordinates for deduplication,
					// but the stored vertex swaps X/Y to undo INVERSEXY
					auto vertexFor = [&](float qx, float qy, float qz) {
						const std::tuple<float, float, float> t = std::make_tuple(qx, qy, qz);
						auto iter = cur_verts.find(t);
						if (iter != cur_verts.end()) {
							return iter->second;
						}
						const uint32_t idx = vertexCount();
						pushVertex(qy, qx, qz);
						cur_verts[t] = idx;
						return idx;
					};
					const uint32_t i1 = vertexFor(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
					const uint32_t i2 = vertexFor(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
					const uint32_t i3 = vertexFor(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
					const uint32_t i4 = vertexFor(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);

					indices.emplace_back(i4);
					indices.emplace_back(i2);
					indices.emplace_back(i3);

					indices.emplace_back(i4);
					indices.emplace_back(i1);
					indices.emplace_back(i2);
				}
			}
		}

		// B941: the inflated payload is fully consumed.
		std::vector<char>().swap(buffer);

		// Swap Y and Z coordinates to convert from EQ's Z-up to internal Y-up format
		// This matches the transformation done by EQEmu server in map.cpp
		for (size_t i = 0; i + 2 < verts.size(); i += 3) {
			std::swap(verts[i + 1], verts[i + 2]);
		}

		// Calculate Y range for color gradient (Y is vertical in Y-up format)
		m_impl->minZ = std::numeric_limits<float>::max();
		m_impl->maxZ = std::numeric_limits<float>::lowest();
		for (size_t i = 1; i < verts.size(); i += 3) {
			if (verts[i] < m_impl->minZ) m_impl->minZ = verts[i];
			if (verts[i] > m_impl->maxZ) m_impl->maxZ = verts[i];
		}

		uint32_t face_count = indices.size() / 3;
		const size_t vertCountTotal = verts.size() / 3;
		if (face_count > 0 && vertCountTotal > 0) {
			indices.resize(static_cast<size_t>(face_count) * 3);  // a trailing partial triangle is not a face
			// B941: the placeable triangles are ONE contiguous run -- a V2 map
			// emits its main terrain first, then every placement and
			// placement-group triangle, then the terrain tiles. B951: the mesh
			// keeps it as one bit per (reordered) triangle.
			const RmUint32 placeableBegin = static_cast<RmUint32>(mainTerrainTriangles);
			const RmUint32 placeableEnd = static_cast<RmUint32>(mainTerrainTriangles + placeableTriangles);
			m_impl->rm = createRaycastMesh(std::move(verts), std::move(indices), placeableBegin, placeableEnd);
			m_impl->bindViews();  // T504
			if (m_impl->rm) {
				loaded = true;
				LOG_INFO(MOD_MAP, "Loaded V2 map: {} vertices, {} faces ({} placeables, {} terrain)",
				         vertCountTotal, face_count, placeableTriangles, face_count - placeableTriangles);
			}
		}
		} catch (const MapParseError& e) {
			// T20: truncated or corrupt V2 payload. `loaded` stays false, so
			// the shared cleanup below frees m_impl and returns false.
			LOG_ERROR(MOD_MAP, "Malformed V2 map '{}': {}", filename, e.what());
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

	// T437 (board B436). There used to be an upward re-cast here -- "Try
	// casting ray up if nothing found below" -- whose hit z was returned with
	// NO marker distinguishing it from a real floor. It was the only mechanism
	// in the tree that literally resolved ground ABOVE the query point, and
	// not one of its callers could do anything with a ceiling:
	//   * ZoneComponent::GetBestZ() -> FixZ() wrote it straight into the
	//     player's position (`ctx_.self.SetPosLocal(...)`);
	//   * PlayerCameraComponent::findGroundZ() handed it to the jump landings
	//     and the swim floor clamp as ground;
	//   * SimBoids' three spawn/ground queries placed creatures on it.
	// A distinct "ceiling" return would therefore have had zero consumers --
	// the reachability defect this project keeps re-finding -- so the re-cast
	// is deleted instead. "Nothing below" is BEST_Z_INVALID, and every caller
	// already handles that by leaving the position alone.
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

	if (!m_impl || !m_impl->rm || m_impl->mesh_verts.empty() || m_impl->triangleCount == 0) {
		return result;
	}

	float radiusSq = radius * radius;
	Triangle tri;
	for (size_t i = 0; i < m_impl->triangleCount; ++i) {
		if (!m_impl->triangleAt(i, tri)) {
			continue;
		}

		// Calculate triangle center
		glm::vec3 triCenter = (tri.v1 + tri.v2 + tri.v3) / 3.0f;

		// Check if triangle center is within radius (using XZ distance in Y-up coords)
		float dx = triCenter.x - center.x;
		float dz = triCenter.z - center.z;
		float distSq = dx * dx + dz * dz;

		if (distSq <= radiusSq) {
			// Calculate face normal using cross product
			glm::vec3 edge1 = tri.v2 - tri.v1;
			glm::vec3 edge2 = tri.v3 - tri.v1;
			tri.normal = glm::normalize(glm::cross(edge1, edge2));
			result.push_back(tri);
		}
	}

	return result;
}

size_t HCMap::ForEachTerrainTriangle(const std::function<void(const Triangle&)>& visit) const {
	if (!m_impl || !m_impl->rm || m_impl->mesh_verts.empty() || m_impl->triangleCount == 0) {
		return 0;
	}

	size_t visited = 0;
	Triangle tri;
	for (size_t i = 0; i < m_impl->triangleCount; ++i) {
		// Skip placeables -- only terrain
		if (m_impl->rm->getTriangleFlag(static_cast<RmUint32>(i))) continue;
		if (!m_impl->triangleAt(i, tri)) {
			continue;
		}
		const glm::vec3 edge1 = tri.v2 - tri.v1;
		const glm::vec3 edge2 = tri.v3 - tri.v1;
		tri.normal = glm::normalize(glm::cross(edge1, edge2));
		++visited;
		visit(tri);
	}
	return visited;
}

std::vector<HCMap::Triangle> HCMap::GetAllTerrainTriangles() const {
	std::vector<Triangle> result;
	if (!m_impl || m_impl->triangleCount == 0) return result;
	result.reserve(m_impl->triangleCount);
	ForEachTerrainTriangle([&result](const Triangle& t) { result.push_back(t); });
	return result;
}

void HCMap::GetVertexBounds(float& minY, float& maxY) const {
	if (!m_impl || m_impl->mesh_verts.empty()) {
		minY = 0.0f;
		maxY = 0.0f;
		return;
	}

	minY = std::numeric_limits<float>::max();
	maxY = std::numeric_limits<float>::lowest();

	for (const auto& v : m_impl->mesh_verts) {
		if (v.y < minY) minY = v.y;
		if (v.y > maxY) maxY = v.y;
	}
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

HCMap::MemoryStats HCMap::GetMemoryStats() const {
	MemoryStats stats;
	if (!m_impl) return stats;

	stats.vertexCount = m_impl->mesh_verts.size();
	stats.faceCount = m_impl->triangleCount;
	// B951: every array the mesh holds (vertices, packed triangles, leaf
	// references, nodes, leaf tables, flag bits), not vertices + indices.
	stats.totalBytes = m_impl->rm ? m_impl->rm->getBytes().total() : 0;
	return stats;
}
