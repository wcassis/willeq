#ifndef EQT_RAYCAST_MESH_H
#define EQT_RAYCAST_MESH_H

// Raycast mesh implementation for WillEQ
// Based on John W. Ratcliff's AABB tree raycast implementation
// Original code released under MIT license (August 18, 2011)
// http://code.google.com/p/raycastmesh/
//
// This implementation provides optimized raycasting for triangle meshes
// using an axis-aligned bounding box (AABB) tree spatial data structure.

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
	virtual bool raycast(const RmReal *from, const RmReal *to, RmReal *hitLocation, RmReal *hitNormal, RmReal *hitDistance) = 0;

	// Brute force raycast - tests all triangles (slower but useful for debugging)
	virtual bool bruteForceRaycast(const RmReal *from, const RmReal *to, RmReal *hitLocation, RmReal *hitNormal, RmReal *hitDistance) = 0;

	// Return the minimum bounding box corner
	virtual const RmReal * getBoundMin(void) const = 0;

	// Return the maximum bounding box corner
	virtual const RmReal * getBoundMax(void) const = 0;

	// Release the raycast mesh and free all memory
	// T504 (load-step audit): read access to the mesh's OWN vertex/index arrays,
	// so a caller (HCMap) need not keep a second copy of the same geometry.
	// Valid until release().
	virtual const RmReal * getVertices(void) const = 0;   // vertexCount * 3 reals
	virtual RmUint32 getVertexCount(void) const = 0;
	virtual const RmUint32 * getIndices(void) const = 0;  // triangleCount * 3 indices
	virtual RmUint32 getTriangleCount(void) const = 0;

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

// B941 (GitLab #259): the same mesh, built from arrays the caller MOVES in.
// The pointer form above copies both arrays, so a caller that built them for
// the mesh (HCMap) held the collision geometry twice at the load peak. The
// mesh keeps these vectors (trimmed to size) as its own storage; the tree and
// every query are identical to the pointer form's. vertices.size() must be a
// multiple of 3, indices.size() a multiple of 3.
RaycastMesh * createRaycastMesh(std::vector<RmReal> &&vertices,
								std::vector<RmUint32> &&indices,
								RmUint32 maxDepth = 15,
								RmUint32 minLeafSize = 4,
								RmReal minAxisSize = 0.01f);

#endif // EQT_RAYCAST_MESH_H
