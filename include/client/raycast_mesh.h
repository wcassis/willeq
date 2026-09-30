#ifndef EQT_RAYCAST_MESH_H
#define EQT_RAYCAST_MESH_H

// Raycast mesh implementation for WillEQ
// Based on John W. Ratcliff's AABB tree raycast implementation
// Original code released under MIT license (August 18, 2011)
// http://code.google.com/p/raycastmesh/
//
// This implementation provides optimized raycasting for triangle meshes
// using an axis-aligned bounding box (AABB) tree spatial data structure.
//
// B951 (GitLab #269): the tree is BUILT exactly as the original (same splits,
// same bounds, same leaf sets) and then stored compactly: triangles once, in
// leaf order, as uint16 offsets from a per-block base vertex; a leaf is a range
// of the triangles it owns plus references to the few an earlier leaf owns;
// 28-byte nodes. Triangle NUMBERING therefore follows the tree, not the input order:
// getTriangle(i) is the i-th stored triangle, and getTriangleFlag(i) carries
// the one per-triangle bit a caller asked for at creation. raycast() is
// re-entrant (no per-query state is written into the mesh).

#include <cstddef>
#include <vector>

typedef float RmReal;
typedef unsigned int RmUint32;

class RaycastMesh
{
public:
	// Perform a raycast from 'from' to 'to'
	// Returns true if the ray hits a triangle
	// hitLocation: the point where the ray hit (if not null)
	// hitNormal: the normal of the triangle at the hit point (if not null)
	// hitDistance: the distance from 'from' to the hit point (if not null)
	// On two triangles hit at exactly the same distance the location and
	// distance are the same whichever is reported; the normal is that of the
	// lower STORED triangle number.
	virtual bool raycast(const RmReal *from, const RmReal *to, RmReal *hitLocation, RmReal *hitNormal, RmReal *hitDistance) const = 0;

	// Brute force raycast - tests all triangles (slower but useful for debugging)
	virtual bool bruteForceRaycast(const RmReal *from, const RmReal *to, RmReal *hitLocation, RmReal *hitNormal, RmReal *hitDistance) const = 0;

	// Return the minimum bounding box corner
	virtual const RmReal * getBoundMin(void) const = 0;

	// Return the maximum bounding box corner
	virtual const RmReal * getBoundMax(void) const = 0;

	// T504 (load-step audit): read access to the mesh's OWN geometry, so a
	// caller (HCMap) need not keep a second copy. Valid until release().
	// B951: vertices are renumbered (first use in stored-triangle order; the
	// VALUES and the count are unchanged) and triangles are read one at a time.
	virtual const RmReal * getVertices(void) const = 0;   // vertexCount * 3 reals
	virtual RmUint32 getVertexCount(void) const = 0;
	virtual RmUint32 getTriangleCount(void) const = 0;
	virtual void getTriangle(RmUint32 tri, RmUint32 *indices3) const = 0;
	virtual bool getTriangleFlag(RmUint32 tri) const = 0;

	// B951: bytes held, by array (for the owner rows and the diagnostics).
	struct Bytes {
		size_t vertices = 0;      // 12 per vertex
		size_t triangles = 0;     // uint16 vertex offsets + block bases + escaped triangles
		size_t triangleRefs = 0;  // leaf references to triangles an earlier leaf owns
		size_t nodes = 0;         // 28 per node
		size_t leaves = 0;        // owned + reference range starts, 8 per leaf
		size_t flags = 0;         // one bit per triangle
		size_t total() const { return vertices + triangles + triangleRefs + nodes + leaves + flags; }
	};
	virtual Bytes getBytes(void) const = 0;

	// Release the raycast mesh and free all memory
	virtual void release(void) = 0;

protected:
	virtual ~RaycastMesh(void) { }
};

// Create a new raycast mesh from triangle data
// vcount: The number of vertices in the source triangle mesh
// vertices: The array of vertex positions in the format x1,y1,z1..x2,y2,z2.. etc.
// tcount: The number of triangles in the source triangle mesh
// indices: The triangle indices in the format of i1,i2,i3 ... i4,i5,i6, ...
// maxDepth: Maximum recursion depth for the triangle mesh (default 15)
// minLeafSize: Minimum triangles to treat as a 'leaf' node (default 4)
// minAxisSize: Once a particular axis is less than this size, stop sub-dividing (default 0.01f)
RaycastMesh * createRaycastMesh(RmUint32 vcount,
								const RmReal *vertices,
								RmUint32 tcount,
								const RmUint32 *indices,
								RmUint32 maxDepth = 15,
								RmUint32 minLeafSize = 4,
								RmReal minAxisSize = 0.01f);

// B941 (GitLab #259): the same mesh, built from arrays the caller MOVES in
// (the pointer form copies both). vertices.size() must be a multiple of 3,
// indices.size() a multiple of 3. B951: input triangles [flagBegin, flagEnd)
// (INPUT numbering) carry getTriangleFlag() == true; HCMap passes its
// placeable run.
RaycastMesh * createRaycastMesh(std::vector<RmReal> &&vertices,
								std::vector<RmUint32> &&indices,
								RmUint32 flagBegin = 0,
								RmUint32 flagEnd = 0,
								RmUint32 maxDepth = 15,
								RmUint32 minLeafSize = 4,
								RmReal minAxisSize = 0.01f);

// B951: memory-ledger owner labels for the mesh's arrays. The client installs
// a pair that opens / closes a `StartupScope`-style sub-owner (zone_entry.cpp);
// tools and tests leave them unset and nothing is labelled. `open` returns a
// token `close` restores.
typedef unsigned (*RmOwnerOpen)(const char *name);
typedef void (*RmOwnerClose)(unsigned token);
void setRaycastMeshOwnerHooks(RmOwnerOpen open, RmOwnerClose close);

#endif // EQT_RAYCAST_MESH_H
