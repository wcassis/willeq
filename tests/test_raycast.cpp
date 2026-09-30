#include <gtest/gtest.h>
#include <algorithm>
#include "client/raycast_mesh.h"
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

// Test fixture for raycast tests
class RaycastMeshTest : public ::testing::Test {
protected:
	// Simple triangle mesh for testing (a flat square)
	std::vector<float> squareVerts = {
		0.0f, 0.0f, 0.0f,   // v0
		10.0f, 0.0f, 0.0f,  // v1
		10.0f, 10.0f, 0.0f, // v2
		0.0f, 10.0f, 0.0f   // v3
	};

	std::vector<uint32_t> squareIndices = {
		0, 1, 2,  // Triangle 1
		0, 2, 3   // Triangle 2
	};

	// Box mesh for testing
	std::vector<float> boxVerts = {
		// Bottom face (z = 0)
		0.0f, 0.0f, 0.0f,
		10.0f, 0.0f, 0.0f,
		10.0f, 10.0f, 0.0f,
		0.0f, 10.0f, 0.0f,
		// Top face (z = 10)
		0.0f, 0.0f, 10.0f,
		10.0f, 0.0f, 10.0f,
		10.0f, 10.0f, 10.0f,
		0.0f, 10.0f, 10.0f
	};

	std::vector<uint32_t> boxIndices = {
		// Bottom
		0, 2, 1, 0, 3, 2,
		// Top
		4, 5, 6, 4, 6, 7,
		// Front
		0, 1, 5, 0, 5, 4,
		// Back
		2, 3, 7, 2, 7, 6,
		// Left
		0, 4, 7, 0, 7, 3,
		// Right
		1, 2, 6, 1, 6, 5
	};
};

TEST_F(RaycastMeshTest, CreateMesh_Basic) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);
	mesh->release();
}

TEST_F(RaycastMeshTest, CreateMesh_WithCustomParameters) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data(), 10, 2, 0.1f);
	ASSERT_NE(mesh, nullptr);
	mesh->release();
}

TEST_F(RaycastMeshTest, GetBounds) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	const float* minBound = mesh->getBoundMin();
	const float* maxBound = mesh->getBoundMax();

	EXPECT_NEAR(minBound[0], 0.0f, 0.001f);
	EXPECT_NEAR(minBound[1], 0.0f, 0.001f);
	EXPECT_NEAR(minBound[2], 0.0f, 0.001f);

	EXPECT_NEAR(maxBound[0], 10.0f, 0.001f);
	EXPECT_NEAR(maxBound[1], 10.0f, 0.001f);
	EXPECT_NEAR(maxBound[2], 0.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_Hit_FromAbove) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray from above pointing down
	float from[3] = {5.0f, 5.0f, 10.0f};
	float to[3] = {5.0f, 5.0f, -10.0f};
	float hitLocation[3] = {0};
	float hitNormal[3] = {0};
	float hitDistance = 0;

	bool hit = mesh->raycast(from, to, hitLocation, hitNormal, &hitDistance);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[0], 5.0f, 0.001f);
	EXPECT_NEAR(hitLocation[1], 5.0f, 0.001f);
	EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f);
	EXPECT_NEAR(hitDistance, 10.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_Hit_FromBelow) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray from below pointing up
	float from[3] = {5.0f, 5.0f, -10.0f};
	float to[3] = {5.0f, 5.0f, 10.0f};
	float hitLocation[3] = {0};
	float hitDistance = 0;

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, &hitDistance);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f);
	EXPECT_NEAR(hitDistance, 10.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_Miss_OutsideMesh) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray completely outside the mesh
	float from[3] = {100.0f, 100.0f, 10.0f};
	float to[3] = {100.0f, 100.0f, -10.0f};
	float hitLocation[3] = {0};

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, nullptr);

	EXPECT_FALSE(hit);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_Miss_ParallelToMesh) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray parallel to the mesh (horizontal)
	float from[3] = {-10.0f, 5.0f, 1.0f};
	float to[3] = {20.0f, 5.0f, 1.0f};
	float hitLocation[3] = {0};

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, nullptr);

	EXPECT_FALSE(hit);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_BoxMesh_FromInside) {
	RaycastMesh* mesh = createRaycastMesh(8, boxVerts.data(), 12, boxIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray from inside the box pointing up
	float from[3] = {5.0f, 5.0f, 5.0f};
	float to[3] = {5.0f, 5.0f, 20.0f};
	float hitLocation[3] = {0};
	float hitDistance = 0;

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, &hitDistance);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[2], 10.0f, 0.001f);
	EXPECT_NEAR(hitDistance, 5.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_BoxMesh_FromOutside) {
	RaycastMesh* mesh = createRaycastMesh(8, boxVerts.data(), 12, boxIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray from outside the box pointing at it
	float from[3] = {5.0f, 5.0f, 20.0f};
	float to[3] = {5.0f, 5.0f, -10.0f};
	float hitLocation[3] = {0};
	float hitDistance = 0;

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, &hitDistance);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[2], 10.0f, 0.001f);
	EXPECT_NEAR(hitDistance, 10.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, BruteForceRaycast_Hit) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray from above pointing down
	float from[3] = {5.0f, 5.0f, 10.0f};
	float to[3] = {5.0f, 5.0f, -10.0f};
	float hitLocation[3] = {0};
	float hitDistance = 0;

	bool hit = mesh->bruteForceRaycast(from, to, hitLocation, nullptr, &hitDistance);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f);
	EXPECT_NEAR(hitDistance, 10.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, BruteForceRaycast_Miss) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray completely outside the mesh
	float from[3] = {100.0f, 100.0f, 10.0f};
	float to[3] = {100.0f, 100.0f, -10.0f};
	float hitLocation[3] = {0};

	bool hit = mesh->bruteForceRaycast(from, to, hitLocation, nullptr, nullptr);

	EXPECT_FALSE(hit);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_VeryShortRay) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Very short ray (should not crash)
	float from[3] = {5.0f, 5.0f, 0.0001f};
	float to[3] = {5.0f, 5.0f, 0.0f};

	bool hit = mesh->raycast(from, to, nullptr, nullptr, nullptr);
	// Result doesn't matter, just checking it doesn't crash

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_ZeroLengthRay) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Zero-length ray
	float from[3] = {5.0f, 5.0f, 5.0f};
	float to[3] = {5.0f, 5.0f, 5.0f};

	bool hit = mesh->raycast(from, to, nullptr, nullptr, nullptr);

	EXPECT_FALSE(hit);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_EdgeHit) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Ray hitting the edge of the mesh
	float from[3] = {0.0f, 0.0f, 10.0f};
	float to[3] = {0.0f, 0.0f, -10.0f};
	float hitLocation[3] = {0};

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, nullptr);

	EXPECT_TRUE(hit);
	EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_DiagonalRay) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Diagonal ray
	float from[3] = {-5.0f, -5.0f, 10.0f};
	float to[3] = {15.0f, 15.0f, -10.0f};
	float hitLocation[3] = {0};

	bool hit = mesh->raycast(from, to, hitLocation, nullptr, nullptr);

	EXPECT_TRUE(hit);
	// Should hit somewhere on the mesh
	EXPECT_GE(hitLocation[0], 0.0f);
	EXPECT_LE(hitLocation[0], 10.0f);
	EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f);

	mesh->release();
}

TEST_F(RaycastMeshTest, Raycast_Consistency) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);

	// Multiple raycasts should give consistent results
	float from[3] = {5.0f, 5.0f, 10.0f};
	float to[3] = {5.0f, 5.0f, -10.0f};
	float hitLocation1[3] = {0};
	float hitLocation2[3] = {0};
	float hitDistance1 = 0, hitDistance2 = 0;

	bool hit1 = mesh->raycast(from, to, hitLocation1, nullptr, &hitDistance1);
	bool hit2 = mesh->raycast(from, to, hitLocation2, nullptr, &hitDistance2);

	EXPECT_EQ(hit1, hit2);
	EXPECT_NEAR(hitLocation1[0], hitLocation2[0], 0.0001f);
	EXPECT_NEAR(hitLocation1[1], hitLocation2[1], 0.0001f);
	EXPECT_NEAR(hitLocation1[2], hitLocation2[2], 0.0001f);
	EXPECT_NEAR(hitDistance1, hitDistance2, 0.0001f);

	mesh->release();
}

// Test the HCMap class
#include "client/hc_map.h"

TEST(HCMapTest, CreateAndDestroy) {
	HCMap map;
	EXPECT_FALSE(map.IsLoaded());
}

TEST(HCMapTest, FindBestZ_NoMap) {
	HCMap map;
	glm::vec3 pos(100.0f, 200.0f, 300.0f);
	glm::vec3 result;

	float z = map.FindBestZ(pos, &result);

	// Without a loaded map, should return BEST_Z_INVALID
	EXPECT_EQ(z, BEST_Z_INVALID);
}

TEST(HCMapTest, CheckLOS_NoMap) {
	HCMap map;
	glm::vec3 start(0.0f, 0.0f, 0.0f);
	glm::vec3 end(100.0f, 100.0f, 100.0f);

	// Without a loaded map, should always return true (no obstacles)
	EXPECT_TRUE(map.CheckLOS(start, end));
}

TEST(HCMapTest, LoadMapFile_NonexistentFile) {
	HCMap* map = HCMap::LoadMapFile("nonexistent_zone", "/nonexistent/path");
	EXPECT_EQ(map, nullptr);
}

// T504d: the per-triangle "already tested this raycast" mark was one byte whose
// frame counter wrapped every 255 raycasts. B951 (#269) removed the marks
// altogether (raycast() writes nothing into the mesh); the test stays as the
// net that a long run of identical rays returns an identical hit every time.
TEST_F(RaycastMeshTest, Raycast_OneByteFrameWrapsWithoutLosingHits) {
	RaycastMesh* mesh = createRaycastMesh(4, squareVerts.data(), 2, squareIndices.data());
	ASSERT_NE(mesh, nullptr);
	float from[3] = {5.0f, 5.0f, 10.0f};
	float to[3] = {5.0f, 5.0f, -10.0f};
	float firstNormal[3] = {0};
	for (int i = 0; i < 1000; ++i) {   // > 3 wraps of a byte frame counter
		float hitLocation[3] = {0}, hitNormal[3] = {0}, hitDistance = 0;
		ASSERT_TRUE(mesh->raycast(from, to, hitLocation, hitNormal, &hitDistance)) << "raycast " << i;
		EXPECT_NEAR(hitDistance, 10.0f, 0.001f) << "raycast " << i;
		EXPECT_NEAR(hitLocation[2], 0.0f, 0.001f) << "raycast " << i;
		if (i == 0) { std::copy(hitNormal, hitNormal + 3, firstNormal); }
		for (int k = 0; k < 3; ++k) EXPECT_FLOAT_EQ(hitNormal[k], firstNormal[k]) << "raycast " << i;
	}
	mesh->release();
}

// B951 / B953 (#271): the collision map is shared by the render thread (player
// collision) and the game thread (FixZ, spell LOS). raycast() used to write a
// per-mesh frame counter and per-triangle marks, so two concurrent rays raced
// and one could skip a triangle the other had marked. It is now const and
// writes nothing: two threads casting the same rays through a const pointer
// must each get exactly the single-threaded answers.
TEST(RaycastMeshConcurrency, TwoThreadsGetTheSingleThreadedAnswers) {
	// A 40x40 bumpy grid (3,200 triangles), so leaves share triangles.
	const int N = 40;
	std::vector<float> v;
	std::vector<uint32_t> idx;
	for (int y = 0; y <= N; ++y)
		for (int x = 0; x <= N; ++x) {
			v.push_back((float)x);
			v.push_back((float)y);
			v.push_back(0.3f * std::sin(0.7f * x) * std::cos(0.5f * y));
		}
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x) {
			const uint32_t a = y * (N + 1) + x, b = a + 1, c = a + N + 1, d = c + 1;
			idx.insert(idx.end(), {a, b, d, a, d, c});
		}
	RaycastMesh* mesh = createRaycastMesh((RmUint32)(v.size() / 3), v.data(), (RmUint32)(idx.size() / 3), idx.data());
	ASSERT_NE(mesh, nullptr);
	const RaycastMesh* cm = mesh;
	const int R = 20000;
	std::vector<float> rays(R * 6);
	for (int i = 0; i < R; ++i) {
		const float fx = (float)((i * 37) % (N * 100)) / 100.0f, fy = (float)((i * 91) % (N * 100)) / 100.0f;
		const float dx = (float)((i * 13) % 700) / 100.0f - 3.5f, dy = (float)((i * 29) % 700) / 100.0f - 3.5f;
		const float r[6] = {fx, fy, 2.0f, fx + dx, fy + dy, -2.0f};
		std::memcpy(&rays[i * 6], r, sizeof(r));
	}
	auto castAll = [&](std::vector<float>& out) {
		out.assign(R * 4, 0.0f);
		for (int i = 0; i < R; ++i) {
			float hit[3] = {0, 0, 0}, dist = 0;
			const bool h = cm->raycast(&rays[i * 6], &rays[i * 6 + 3], hit, nullptr, &dist);
			out[i * 4] = h ? 1.0f : 0.0f;
			out[i * 4 + 1] = hit[0]; out[i * 4 + 2] = hit[1]; out[i * 4 + 3] = hit[2];
		}
	};
	std::vector<float> ref, a, b;
	castAll(ref);
	int hits = 0;
	for (int i = 0; i < R; ++i) hits += ref[i * 4] != 0.0f;
	EXPECT_GT(hits, R / 2);  // not vacuous
	std::thread t1([&] { castAll(a); });
	std::thread t2([&] { castAll(b); });
	t1.join();
	t2.join();
	EXPECT_EQ(std::memcmp(ref.data(), a.data(), ref.size() * sizeof(float)), 0);
	EXPECT_EQ(std::memcmp(ref.data(), b.data(), ref.size() * sizeof(float)), 0);
	mesh->release();
}
