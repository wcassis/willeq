#include "client/raycast_mesh.h"
#include "common/logging.h"  // T504
#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <new>
#include <utility>
#include <vector>

// This code snippet allows you to create an axis aligned bounding volume tree for a triangle mesh so that you can do
// high-speed raycasting.
//
// There are much better implementations of this available on the internet.  In particular I recommend that you use
// OPCODE written by Pierre Terdiman.
// @see: http://www.codercorner.com/Opcode.htm
//
// OPCODE does a whole lot more than just raycasting, and is a rather significant amount of source code.
//
// I am providing this code snippet for the use case where you *only* want to do quick and dirty optimized raycasting.
// I have not done performance testing between this version and OPCODE; so I don't know how much slower it is.  However,
// anytime you switch to using a spatial data structure for raycasting, you increase your performance by orders and orders
// of magnitude; so this implementation should work fine for simple tools and utilities.
//
// It also serves as a nice sample for people who are trying to learn the algorithm of how to implement AABB trees.
// AABB = Axis Aligned Bounding Volume trees.
//
// http://www.cgal.org/Manual/3.5/doc_html/cgal_manual/AABB_tree/Chapter_main.html
//
//
// This code snippet was written by John W. Ratcliff on August 18, 2011 and released under the MIT. license.
//
// mailto:jratcliffscarab@gmail.com
//
// The official source can be found at:  http://code.google.com/p/raycastmesh/

#ifdef _MSC_VER
#pragma warning(disable:4100)
#endif

namespace RAYCAST_MESH
{

typedef unsigned char RmUint8;
// B941: plain vectors. These were page-aligned (posix_memalign(4096) for every
// node's left/right triangle list during the build, for a KSM merge that never
// applies to a single client process): each small list cost a page-aligned
// chunk and left the arena fragmented after the build.
typedef std::vector<RmUint32> TriVector;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
*	A method to compute a ray-AABB intersection.
*	Original code by Andrew Woo, from "Graphics Gems", Academic Press, 1990
*	Optimized code by Pierre Terdiman, 2000 (~20-30% faster on my Celeron 500)
*	Epsilon value added by Klaus Hartmann. (discarding it saves a few cycles only)
*
*	Hence this version is faster as well as more robust than the original one.
*
*	Should work provided:
*	1) the integer representation of 0.0f is 0x00000000
*	2) the sign bit of the RmReal is the most significant one
*
*	Report bugs: p.terdiman@codercorner.com
*
*	\param		aabb		[in] the axis-aligned bounding box
*	\param		origin		[in] ray origin
*	\param		dir			[in] ray direction
*	\param		coord		[out] impact coordinates
*	\return		true if ray intersects AABB
*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define RAYAABB_EPSILON 0.00001f
//! Integer representation of a RmRealing-point value.
// B951: read through memcpy. The original `((RmUint32&)x)` cast read a float
// object through an integer lvalue -- undefined under strict aliasing, and
// once this function inlined into the new traversal GCC warned it could read
// MaxT "uninitialized" (the integer load was no longer ordered after the
// float stores). Same bits, defined behaviour.
static inline RmUint32 IR(RmReal x)
{
	RmUint32 u;
	memcpy(&u, &x, sizeof(u));
	return u;
}

bool intersectRayAABB(const RmReal MinB[3],const RmReal MaxB[3],const RmReal origin[3],const RmReal dir[3],RmReal coord[3])
{
	bool Inside = true;
	RmReal MaxT[3];
	MaxT[0]=MaxT[1]=MaxT[2]=-1.0f;

	// Find candidate planes.
	for(RmUint32 i=0;i<3;i++)
	{
		if(origin[i] < MinB[i])
		{
			coord[i]	= MinB[i];
			Inside		= false;

			// Calculate T distances to candidate planes
			if(IR(dir[i]))	MaxT[i] = (MinB[i] - origin[i]) / dir[i];
		}
		else if(origin[i] > MaxB[i])
		{
			coord[i]	= MaxB[i];
			Inside		= false;

			// Calculate T distances to candidate planes
			if(IR(dir[i]))	MaxT[i] = (MaxB[i] - origin[i]) / dir[i];
		}
	}

	// Ray origin inside bounding box
	if(Inside)
	{
		coord[0] = origin[0];
		coord[1] = origin[1];
		coord[2] = origin[2];
		return true;
	}

	// Get largest of the maxT's for final choice of intersection
	RmUint32 WhichPlane = 0;
	if(MaxT[1] > MaxT[WhichPlane])	WhichPlane = 1;
	if(MaxT[2] > MaxT[WhichPlane])	WhichPlane = 2;

	// Check final candidate actually inside box
	if(IR(MaxT[WhichPlane])&0x80000000) return false;

	for(RmUint32 i=0;i<3;i++)
	{
		if(i!=WhichPlane)
		{
			coord[i] = origin[i] + MaxT[WhichPlane] * dir[i];
			#ifdef RAYAABB_EPSILON
			if(coord[i] < MinB[i] - RAYAABB_EPSILON || coord[i] > MaxB[i] + RAYAABB_EPSILON)	return false;
			#else
			if(coord[i] < MinB[i] || coord[i] > MaxB[i])	return false;
			#endif
		}
	}
	return true;	// ray hits box
}

bool intersectLineSegmentAABB(const RmReal bmin[3],const RmReal bmax[3],const RmReal p1[3],const RmReal dir[3],RmReal &dist,RmReal intersect[3])
{
	bool ret = false;

	if ( dist > RAYAABB_EPSILON )
	{
		ret = intersectRayAABB(bmin,bmax,p1,dir,intersect);
		if ( ret )
		{
			RmReal dx = p1[0]-intersect[0];
			RmReal dy = p1[1]-intersect[1];
			RmReal dz = p1[2]-intersect[2];
			RmReal d = dx*dx+dy*dy+dz*dz;
			if ( d < dist*dist )
			{
				dist = sqrtf(d);
			}
			else
			{
				ret = false;
			}
		}
	}
	return ret;
}

/* a = b - c */
#define vector(a,b,c) \
	(a)[0] = (b)[0] - (c)[0];	\
	(a)[1] = (b)[1] - (c)[1];	\
	(a)[2] = (b)[2] - (c)[2];

#define innerProduct(v,q) \
	((v)[0] * (q)[0] + \
	(v)[1] * (q)[1] + \
	(v)[2] * (q)[2])

#define crossProduct(a,b,c) \
	(a)[0] = (b)[1] * (c)[2] - (c)[1] * (b)[2]; \
	(a)[1] = (b)[2] * (c)[0] - (c)[2] * (b)[0]; \
	(a)[2] = (b)[0] * (c)[1] - (c)[0] * (b)[1];


static inline bool rayIntersectsTriangle(const RmReal *p,const RmReal *d,const RmReal *v0,const RmReal *v1,const RmReal *v2,RmReal &t)
{
	RmReal e1[3],e2[3],h[3],s[3],q[3];
	RmReal a,f,u,v;

	vector(e1,v1,v0);
	vector(e2,v2,v0);
	crossProduct(h,d,e2);
	a = innerProduct(e1,h);

	if (a > -0.00001 && a < 0.00001)
		return(false);

	f = 1/a;
	vector(s,p,v0);
	u = f * (innerProduct(s,h));

	if (u < 0.0 || u > 1.0)
		return(false);

	crossProduct(q,s,e1);
	v = f * innerProduct(d,q);
	if (v < 0.0 || u + v > 1.0)
		return(false);
	// at this stage we can compute t to find out where
	// the intersection point is on the line
	t = f * innerProduct(e2,q);
	if (t > 0) // ray intersection
		return(true);
	else // this means that there is a line intersection
		// but not a ray intersection
		return (false);
}

static RmReal computePlane(const RmReal *A,const RmReal *B,const RmReal *C,RmReal *n) // returns D
{
	RmReal vx = (B[0] - C[0]);
	RmReal vy = (B[1] - C[1]);
	RmReal vz = (B[2] - C[2]);

	RmReal wx = (A[0] - B[0]);
	RmReal wy = (A[1] - B[1]);
	RmReal wz = (A[2] - B[2]);

	RmReal vw_x = vy * wz - vz * wy;
	RmReal vw_y = vz * wx - vx * wz;
	RmReal vw_z = vx * wy - vy * wx;

	RmReal mag = sqrt((vw_x * vw_x) + (vw_y * vw_y) + (vw_z * vw_z));

	if ( mag < 0.000001f )
	{
		mag = 0;
	}
	else
	{
		mag = 1.0f/mag;
	}

	RmReal x = vw_x * mag;
	RmReal y = vw_y * mag;
	RmReal z = vw_z * mag;

	RmReal D = 0.0f - ((x*A[0])+(y*A[1])+(z*A[2]));

	n[0] = x;
	n[1] = y;
	n[2] = z;

	return D;
}


#define TRI_EOF 0xFFFFFFFF

enum AxisAABB
{
	AABB_XAXIS,
	AABB_YAXIS,
	AABB_ZAXIS
};

enum ClipCode
{
	CC_MINX   =       (1<<0),
	CC_MAXX  =       (1<<1),
	CC_MINY   =       (1<<2),
	CC_MAXY	 =       (1<<3),
	CC_MINZ  =       (1<<4),
	CC_MAXZ   =       (1<<5),
};


class BoundsAABB
{
public:

	void setMin(const RmReal *v)
	{
		mMin[0] = v[0];
		mMin[1] = v[1];
		mMin[2] = v[2];
	}

	void setMax(const RmReal *v)
	{
		mMax[0] = v[0];
		mMax[1] = v[1];
		mMax[2] = v[2];
	}

	void setMin(RmReal x,RmReal y,RmReal z)
	{
		mMin[0] = x;
		mMin[1] = y;
		mMin[2] = z;
	}

	void setMax(RmReal x,RmReal y,RmReal z)
	{
		mMax[0] = x;
		mMax[1] = y;
		mMax[2] = z;
	}

	void include(const RmReal *v)
	{
		if ( v[0] < mMin[0] ) mMin[0] = v[0];
		if ( v[1] < mMin[1] ) mMin[1] = v[1];
		if ( v[2] < mMin[2] ) mMin[2] = v[2];

		if ( v[0] > mMax[0] ) mMax[0] = v[0];
		if ( v[1] > mMax[1] ) mMax[1] = v[1];
		if ( v[2] > mMax[2] ) mMax[2] = v[2];
	}

	void getCenter(RmReal *center) const
	{
		center[0] = (mMin[0]+mMax[0])*0.5f;
		center[1] = (mMin[1]+mMax[1])*0.5f;
		center[2] = (mMin[2]+mMax[2])*0.5f;
	}

	bool intersects(const BoundsAABB &b) const
	{
		if ((mMin[0] > b.mMax[0]) || (b.mMin[0] > mMax[0])) return false;
		if ((mMin[1] > b.mMax[1]) || (b.mMin[1] > mMax[1])) return false;
		if ((mMin[2] > b.mMax[2]) || (b.mMin[2] > mMax[2])) return false;
		return true;
	}

	bool containsTriangle(const RmReal *p1,const RmReal *p2,const RmReal *p3) const
	{
		BoundsAABB b;
		b.setMin(p1);
		b.setMax(p1);
		b.include(p2);
		b.include(p3);
		return intersects(b);
	}

	bool containsTriangleExact(const RmReal *p1,const RmReal *p2,const RmReal *p3,RmUint32 &orCode) const
	{
		bool ret = false;

		RmUint32 andCode;
		orCode = getClipCode(p1,p2,p3,andCode);
		if ( andCode == 0 )
		{
			ret = true;
		}

		return ret;
	}

	inline RmUint32 getClipCode(const RmReal *p1,const RmReal *p2,const RmReal *p3,RmUint32 &andCode) const
	{
		andCode = 0xFFFFFFFF;
		RmUint32 c1 = getClipCode(p1);
		RmUint32 c2 = getClipCode(p2);
		RmUint32 c3 = getClipCode(p3);
		andCode&=c1;
		andCode&=c2;
		andCode&=c3;
		return c1|c2|c3;
	}

	inline RmUint32 getClipCode(const RmReal *p) const
	{
		RmUint32 ret = 0;

		if ( p[0] < mMin[0] )
		{
			ret|=CC_MINX;
		}
		else if ( p[0] > mMax[0] )
		{
			ret|=CC_MAXX;
		}

		if ( p[1] < mMin[1] )
		{
			ret|=CC_MINY;
		}
		else if ( p[1] > mMax[1] )
		{
			ret|=CC_MAXY;
		}

		if ( p[2] < mMin[2] )
		{
			ret|=CC_MINZ;
		}
		else if ( p[2] > mMax[2] )
		{
			ret|=CC_MAXZ;
		}

		return ret;
	}

	inline void clamp(const BoundsAABB &aabb)
	{
		if ( mMin[0] < aabb.mMin[0] ) mMin[0] = aabb.mMin[0];
		if ( mMin[1] < aabb.mMin[1] ) mMin[1] = aabb.mMin[1];
		if ( mMin[2] < aabb.mMin[2] ) mMin[2] = aabb.mMin[2];
		if ( mMax[0] > aabb.mMax[0] ) mMax[0] = aabb.mMax[0];
		if ( mMax[1] > aabb.mMax[1] ) mMax[1] = aabb.mMax[1];
		if ( mMax[2] > aabb.mMax[2] ) mMax[2] = aabb.mMax[2];
	}

	RmReal		mMin[3];
	RmReal		mMax[3];
};


class NodeAABB;

// B951: the BUILD-time tree. NodeAABB below is the original node and split,
// unchanged, so the finished mesh has exactly the original tree's nodes,
// bounds and leaf sets; MyRaycastMesh re-lays it out (a node word stream) and frees
// this pool. Nodes live in one pool and refer to each other by INDEX
// (T504d); the build allocates a node's left child immediately before
// recursing into it, so the pool is in PRE-ORDER and a left child is always
// the next node -- the packed layout relies on that.
class NodeInterface
{
public:
	virtual RmUint32 getNode(void) = 0;
	virtual NodeAABB * nodeAt(RmUint32 index) = 0;
};

static const RmUint32 NODE_NONE = 0xFFFFFFFF;


class NodeAABB
{
public:
	NodeAABB(void)
	{
		mLeft = NODE_NONE;
		mRight = NODE_NONE;
		mLeafTriangleIndex= TRI_EOF;
	}

	NodeAABB(RmUint32 vcount,const RmReal *vertices,RmUint32 tcount,RmUint32 *indices,
		RmUint32 maxDepth,
		RmUint32 minLeafSize,
		RmReal	minAxisSize,
		NodeInterface *callback,
		TriVector &leafTriangles)
	{
		mLeft = NODE_NONE;
		mRight = NODE_NONE;
		mLeafTriangleIndex = TRI_EOF;
		TriVector triangles;
		triangles.reserve(tcount);
		for (RmUint32 i=0; i<tcount; i++)
		{
			triangles.push_back(i);
		}
		if ( vcount == 0 )
		{
			// B941: an empty mesh (a zero-face map) has no vertex to seed the
			// bounds from; the old page-aligned copy handed a dangling pointer here.
			mBounds.setMin( 0.0f, 0.0f, 0.0f );
			mBounds.setMax( 0.0f, 0.0f, 0.0f );
		}
		else
		{
			mBounds.setMin( vertices );
			mBounds.setMax( vertices );
			const RmReal *vtx = vertices+3;
			for (RmUint32 i=1; i<vcount; i++)
			{
				mBounds.include( vtx );
				vtx+=3;
			}
		}
		split(triangles,vcount,vertices,tcount,indices,0,maxDepth,minLeafSize,minAxisSize,callback,leafTriangles);
	}

	NodeAABB(const BoundsAABB &aabb)
	{
		mBounds = aabb;
		mLeft = NODE_NONE;
		mRight = NODE_NONE;
		mLeafTriangleIndex = TRI_EOF;
	}

	~NodeAABB(void)
	{
	}

	// here is where we split the mesh..
	void split(const TriVector &triangles,
		RmUint32 vcount,
		const RmReal *vertices,
		RmUint32 tcount,
		const RmUint32 *indices,
		RmUint32 depth,
		RmUint32 maxDepth,
		RmUint32 minLeafSize,
		RmReal	minAxisSize,
		NodeInterface *callback,
		TriVector &leafTriangles)
	{
		// Find the longest axis of the bounding volume of this node
		RmReal dx = mBounds.mMax[0] - mBounds.mMin[0];
		RmReal dy = mBounds.mMax[1] - mBounds.mMin[1];
		RmReal dz = mBounds.mMax[2] - mBounds.mMin[2];

		AxisAABB axis = AABB_XAXIS;
		RmReal laxis = dx;

		if ( dy > dx )
		{
			axis = AABB_YAXIS;
			laxis = dy;
		}

		if ( dz > dx && dz > dy )
		{
			axis = AABB_ZAXIS;
			laxis = dz;
		}

		RmUint32 count = triangles.size();

		if ( count < minLeafSize || depth >= maxDepth || laxis < minAxisSize )
		{
			mLeafTriangleIndex = leafTriangles.size();
			leafTriangles.push_back(count);
			for (auto i = triangles.begin(); i != triangles.end(); ++i) {
				RmUint32 tri = *i;
				leafTriangles.push_back(tri);
			}
		}
		else
		{
			RmReal center[3];
			mBounds.getCenter(center);
			BoundsAABB b1,b2;
			splitRect(axis,mBounds,b1,b2,center);

			BoundsAABB leftBounds,rightBounds;

			TriVector leftTriangles;
			TriVector rightTriangles;

			for (auto i = triangles.begin(); i != triangles.end(); ++i) {

				RmUint32 tri = (*i);

				{
					RmUint32 i1 = indices[tri*3+0];
					RmUint32 i2 = indices[tri*3+1];
					RmUint32 i3 = indices[tri*3+2];

					const RmReal *p1 = &vertices[i1*3];
					const RmReal *p2 = &vertices[i2*3];
					const RmReal *p3 = &vertices[i3*3];

					RmUint32 addCount = 0;
					RmUint32 orCode=0xFFFFFFFF;
					if ( b1.containsTriangleExact(p1,p2,p3,orCode))
					{
						addCount++;
						if ( leftTriangles.empty() )
						{
							leftBounds.setMin(p1);
							leftBounds.setMax(p1);
						}
						leftBounds.include(p1);
						leftBounds.include(p2);
						leftBounds.include(p3);
						leftTriangles.push_back(tri);
					}
					if ( orCode && b2.containsTriangleExact(p1,p2,p3,orCode))
					{
						addCount++;
						if ( rightTriangles.empty() )
						{
							rightBounds.setMin(p1);
							rightBounds.setMax(p1);
						}
						rightBounds.include(p1);
						rightBounds.include(p2);
						rightBounds.include(p3);
						rightTriangles.push_back(tri);
					}
					assert( addCount );
					(void)addCount;
				}
			}

			if ( !leftTriangles.empty() )
			{
				leftBounds.clamp(b1);
				mLeft = callback->getNode();
				NodeAABB *left = callback->nodeAt(mLeft);
				new ( left ) NodeAABB(leftBounds);
				left->split(leftTriangles,vcount,vertices,tcount,indices,depth+1,maxDepth,minLeafSize,minAxisSize,callback,leafTriangles);
			}

			if ( !rightTriangles.empty() )
			{
				rightBounds.clamp(b2);
				mRight = callback->getNode();
				NodeAABB *right = callback->nodeAt(mRight);
				new ( right ) NodeAABB(rightBounds);
				right->split(rightTriangles,vcount,vertices,tcount,indices,depth+1,maxDepth,minLeafSize,minAxisSize,callback,leafTriangles);
			}
		}
	}

	void splitRect(AxisAABB axis,const BoundsAABB &source,BoundsAABB &b1,BoundsAABB &b2,const RmReal *midpoint)
	{
		switch ( axis )
		{
			case AABB_XAXIS:
				{
					b1.setMin( source.mMin );
					b1.setMax( midpoint[0], source.mMax[1], source.mMax[2] );

					b2.setMin( midpoint[0], source.mMin[1], source.mMin[2] );
					b2.setMax(source.mMax);
				}
				break;
			case AABB_YAXIS:
				{
					b1.setMin(source.mMin);
					b1.setMax(source.mMax[0], midpoint[1], source.mMax[2]);

					b2.setMin(source.mMin[0], midpoint[1], source.mMin[2]);
					b2.setMax(source.mMax);
				}
				break;
			case AABB_ZAXIS:
				{
					b1.setMin(source.mMin);
					b1.setMax(source.mMax[0], source.mMax[1], midpoint[2]);

					b2.setMin(source.mMin[0], source.mMin[1], midpoint[2]);
					b2.setMax(source.mMax);
				}
				break;
		}
	}

	RmUint32		mLeft;      // NODE_NONE = no child
	RmUint32		mRight;
	BoundsAABB		mBounds;
	RmUint32		mLeafTriangleIndex;
};

// ---------------------------------------------------------------------------
// B951 (GitLab #269): the finished mesh's compact layout.
//
// Measured before (emeraldjungle, 618,240 triangles, 46,579 nodes): vertices
// 4.40 MB, indices 7.42 MB (3 x uint32 per triangle for 19-bit vertex
// numbers), leaf triangle lists 3.77 MB (a uint32 count per leaf plus a uint32
// per reference; 918,775 references = each triangle listed by 1.49 leaves on
// average, 2.1-2.7 in timorous/growthplane/gfaydark), nodes 1.68 MB (36 bytes)
// and a one-byte raycast mark per triangle 0.62 MB. Now:
//
//   * triangles are STORED ONCE, in leaf order: the first leaf (pre-order)
//     that lists a triangle owns it, so a leaf is the range of triangles it
//     owns plus a range of references to triangles an earlier leaf owns (the
//     ones that straddle a split);
//   * a triangle is three uint16 offsets from a base vertex number shared by
//     a block of 64 stored triangles (6 bytes; a triangle using a vertex
//     below its block's base is escaped to three plain uint32), and a
//     reference is ceil(bits(triangleCount-1)/8) bytes;
//   * vertices are renumbered in first-use order of the stored triangles
//     (values and count unchanged): a leaf's vertices sit together;
//   * a node is its exact float bounds plus ONE word (28 bytes): the left child
//     is always the next node (pre-order build), the word is the right child
//     or, for a leaf, its ordinal in two per-leaf range tables;
//   * no raycast marks. The old mark stopped a ray from re-testing a triangle
//     a previous leaf had tested; a re-test returns the same t, so it can never
//     change the nearest hit (a tie keeps the same location and distance).
//     Dropping it also makes raycast() re-entrant: the render thread (player
//     collision) and the game thread (FixZ, spell LOS) cast into the SAME map,
//     and the shared mark/frame counter was a data race that could skip a
//     triangle neither thread had tested.
//
// The tree itself (which nodes, their bounds, which triangles each leaf
// lists) is built by the unchanged NodeAABB code above, so a ray visits the
// same boxes in the same order and tests the same triangle set in each leaf.
//
// Measured on the Pi and kept OUT, because they made rays slower (numbers in
// refactor/B951/README.md): bit-packed per-triangle deltas (5 bytes instead of
// 6; the 64-bit unpacking made cache-warm rays 17 % slower in emeraldjungle)
// and child bounds derived from the parent's (nodes to ~40 %; the per-node
// decode made cache-warm rays 11-31 % slower).
// ---------------------------------------------------------------------------

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the leaf references are read as little-endian words");

struct PackedNode
{
	RmReal		mMin[3];
	RmReal		mMax[3];
	RmUint32	mData;
};
static_assert(sizeof(PackedNode) == 28, "PackedNode is 6 floats + one word");

static const RmUint32 PN_LEAF      = 0x80000000u;  // mData low 31 bits = leaf ordinal
static const RmUint32 PN_HAS_LEFT  = 0x40000000u;  // left child = this node + 1
static const RmUint32 PN_HAS_RIGHT = 0x20000000u;  // mData low 29 bits = right child
static const RmUint32 PN_INDEX     = 0x1FFFFFFFu;

static inline RmUint32 bitsFor(RmUint32 count)
{
	// bits needed to number `count` things (0..count-1); at least 1
	if (count <= 2) return 1;
	RmUint32 n = count - 1;
	RmUint32 b = 0;
	while (n) { ++b; n >>= 1; }
	return b;
}

// Stored triangles: kTriBlock per base vertex number, uint16 offsets from it;
// 0xFFFF as the first offset marks an escaped triangle.
static const RmUint32 kTriBlock = 64;
static const uint16_t kEscape = 0xFFFFu;
static const RmUint32 kOffsetMax = 0xFFFEu;

static RmOwnerOpen g_ownerOpen = nullptr;
static RmOwnerClose g_ownerClose = nullptr;

// Everything allocated while one of these is alive is charged to the named
// memory-ledger owner row (when the client installed the hooks). The ledger
// keeps 23 characters of a name, so the rows are gs.collision_map.{verts,
// tris,refs,nodes,leaves,flags}; what the mesh holds outside them (the object
// itself) stays in the caller's gs.collision_map row.
class OwnerLabel
{
public:
	explicit OwnerLabel(const char *name) : mOn(g_ownerOpen != nullptr && g_ownerClose != nullptr), mToken(0)
	{
		if (mOn) mToken = g_ownerOpen(name);
	}
	~OwnerLabel(void)
	{
		if (mOn) g_ownerClose(mToken);
	}
	OwnerLabel(const OwnerLabel&) = delete;
	OwnerLabel& operator=(const OwnerLabel&) = delete;
private:
	bool		mOn;
	unsigned	mToken;
};

// The build-time node pool (a full tree of maxDepth, freed after the re-layout).
class TreeBuilder : public NodeInterface
{
public:
	explicit TreeBuilder(RmUint32 maxDepth)
	{
		RmUint32 pow2Table[16] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 65536 };
		mMaxNodeCount = 0;
		for (RmUint32 i=0; i<=maxDepth; i++)
		{
			mMaxNodeCount+=pow2Table[i];
		}
		mNodes = static_cast<NodeAABB*>(calloc(mMaxNodeCount, sizeof(NodeAABB)));
		if (!mNodes) {
			throw std::bad_alloc();
		}
		mNodeCount = 0;
	}
	~TreeBuilder(void) { release(); }
	void release(void) { if (mNodes) { free(mNodes); mNodes = nullptr; } }
	virtual RmUint32 getNode(void)
	{
		assert( mNodeCount < mMaxNodeCount );
		return mNodeCount++;
	}
	virtual NodeAABB * nodeAt(RmUint32 index)
	{
		return &mNodes[index];
	}
	NodeAABB	*mNodes;
	RmUint32	mNodeCount;
	RmUint32	mMaxNodeCount;
};

class MyRaycastMesh : public RaycastMesh
{
public:

	MyRaycastMesh(std::vector<RmReal> &&vertices,std::vector<RmUint32> &&indices,RmUint32 flagBegin,RmUint32 flagEnd,RmUint32 maxDepth,RmUint32 minLeafSize,RmReal minAxisSize)
	{
		if ( maxDepth < 2 )
		{
			maxDepth = 2;
		}
		if ( maxDepth > 15 )
		{
			maxDepth = 15;
		}
		mVcount = static_cast<RmUint32>(vertices.size() / 3);
		mTcount = static_cast<RmUint32>(indices.size() / 3);

		// 1. The original tree, built by the original code.
		TreeBuilder builder(maxDepth);
		TriVector leafTriangles;   // per leaf: count, then that many triangle numbers
		{
			const RmUint32 root = builder.getNode();
			new ( builder.nodeAt(root) ) NodeAABB(mVcount,vertices.data(),mTcount,indices.data(),maxDepth,minLeafSize,minAxisSize,&builder,leafTriangles);
		}
		const RmUint32 nodeCount = builder.mNodeCount;

		// 2. Leaf ordinals (pre-order) and the stored triangle numbering: the
		//    first leaf that lists a triangle owns it.
		RmUint32 leafCount = 0;
		for (RmUint32 n = 0; n < nodeCount; ++n)
		{
			if (builder.mNodes[n].mLeafTriangleIndex != TRI_EOF) ++leafCount;
		}
		std::vector<RmUint32> newId(mTcount, TRI_EOF);
		RmUint32 refCount = 0;
		{
			OwnerLabel owner("gs.collision_map.leaves");
			mLeafRanges.assign(2 * (static_cast<size_t>(leafCount) + 1), 0);
		}
		{
			RmUint32 next = 0;
			RmUint32 leaf = 0;
			for (RmUint32 n = 0; n < nodeCount; ++n)
			{
				const NodeAABB &nd = builder.mNodes[n];
				if (nd.mLeafTriangleIndex == TRI_EOF) continue;
				mLeafRanges[2 * leaf] = next;
				mLeafRanges[2 * leaf + 1] = refCount;
				const RmUint32 *list = &leafTriangles[nd.mLeafTriangleIndex];
				const RmUint32 count = *list++;
				for (RmUint32 i = 0; i < count; ++i)
				{
					if (newId[list[i]] == TRI_EOF) newId[list[i]] = next++;
					else ++refCount;
				}
				++leaf;
			}
			mLeafRanges[2 * static_cast<size_t>(leafCount)] = next;
			mLeafRanges[2 * static_cast<size_t>(leafCount) + 1] = refCount;
			// A triangle no leaf lists (never the case for a finite mesh) is kept
			// after the last leaf's range: stored, never tested -- as before.
			for (RmUint32 t = 0; t < mTcount; ++t)
			{
				if (newId[t] == TRI_EOF) newId[t] = next++;
			}
		}

		// 3. Nodes: the exact float bounds plus ONE word (28 bytes, was 36).
		//    The build pool is pre-order, so a left child is always the next
		//    node; the word is the right child's index or the leaf's ordinal.
		{
			OwnerLabel owner("gs.collision_map.nodes");
			mNodes.resize(nodeCount);
		}
		{
			RmUint32 leaf = 0;
			for (RmUint32 n = 0; n < nodeCount; ++n)
			{
				const NodeAABB &src = builder.mNodes[n];
				PackedNode &dst = mNodes[n];
				for (int k = 0; k < 3; ++k)
				{
					dst.mMin[k] = src.mBounds.mMin[k];
					dst.mMax[k] = src.mBounds.mMax[k];
				}
				if (src.mLeafTriangleIndex != TRI_EOF)
				{
					dst.mData = PN_LEAF | leaf++;
					continue;
				}
				dst.mData = 0;
				if (src.mLeft != NODE_NONE)
				{
					assert(src.mLeft == n + 1);
					dst.mData |= PN_HAS_LEFT;
				}
				if (src.mRight != NODE_NONE)
				{
					assert(src.mRight <= PN_INDEX);
					dst.mData |= PN_HAS_RIGHT | src.mRight;
				}
			}
		}

		// 4. References, packed at the width the triangle count needs.
		{
			OwnerLabel owner("gs.collision_map.refs");
			mRefWidth = (bitsFor(mTcount) + 7) / 8;
			mRefMask = mRefWidth >= 4 ? 0xFFFFFFFFu : ((1u << (8 * mRefWidth)) - 1u);
			mRefBytes.assign(static_cast<size_t>(refCount) * mRefWidth + 3, 0);  // +3: a 4-byte load at the last one
		}
		{
			RmUint32 leaf = 0;
			RmUint32 r = 0;
			for (RmUint32 n = 0; n < nodeCount; ++n)
			{
				const NodeAABB &nd = builder.mNodes[n];
				if (nd.mLeafTriangleIndex == TRI_EOF) continue;
				const RmUint32 *list = &leafTriangles[nd.mLeafTriangleIndex];
				const RmUint32 count = *list++;
				for (RmUint32 i = 0; i < count; ++i)
				{
					const RmUint32 id = newId[list[i]];
					if (id >= mLeafRanges[2 * leaf] && id < mLeafRanges[2 * leaf + 2]) continue;  // owned here
					memcpy(&mRefBytes[static_cast<size_t>(r) * mRefWidth], &id, mRefWidth);
					++r;
				}
				++leaf;
			}
			assert(r == refCount);
			(void)r;
		}
		builder.release();
		TriVector().swap(leafTriangles);

		// 5. Vertex renumbering: first use in stored-triangle order; a vertex
		//    no triangle uses keeps a slot at the end (the count and the bounds
		//    are unchanged).
		std::vector<RmUint32> vmap(mVcount, TRI_EOF);
		{
			std::vector<RmUint32> order(mTcount);
			for (RmUint32 t = 0; t < mTcount; ++t) order[newId[t]] = t;
			RmUint32 nv = 0;
			for (RmUint32 k = 0; k < mTcount; ++k)
			{
				const RmUint32 *tri = &indices[static_cast<size_t>(order[k]) * 3];
				for (int j = 0; j < 3; ++j)
				{
					if (vmap[tri[j]] == TRI_EOF) vmap[tri[j]] = nv++;
				}
			}
			for (RmUint32 v = 0; v < mVcount; ++v)
			{
				if (vmap[v] == TRI_EOF) vmap[v] = nv++;
			}
		}

		// 6. Triangles, and the one caller flag bit each. Stored triangles come
		//    in blocks of kTriBlock (by stored number); a block has one base
		//    vertex number and each triangle three uint16 offsets from it.
		//    Vertices are in first-use order, so a block's triangles use the
		//    vertices just introduced plus shared ones from nearby leaves; the
		//    base is set so every index up to the block's highest fits, and a
		//    triangle with a vertex below the base (a vertex shared with a leaf
		//    far back in the order) is escaped: first offset 0xFFFF, the other
		//    two an index into mTriWide (three plain uint32).
		const RmUint32 blockCount = (mTcount + kTriBlock - 1) / kTriBlock;
		{
			OwnerLabel owner("gs.collision_map.tris");
			mTri16.assign(static_cast<size_t>(mTcount) * 3, 0);
			mBlockBase.assign(blockCount, 0);
		}
		{
			OwnerLabel owner("gs.collision_map.flags");
			mFlagBits.assign((static_cast<size_t>(mTcount) + 7) / 8, 0);
		}
		{
			// highest vertex number per block -> the block's base
			std::vector<RmUint32> order(mTcount);
			for (RmUint32 t = 0; t < mTcount; ++t) order[newId[t]] = t;
			RmUint32 wide = 0;
			for (RmUint32 blk = 0; blk < blockCount; ++blk)
			{
				RmUint32 hi = 0;
				const RmUint32 first = blk * kTriBlock;
				const RmUint32 last = std::min(mTcount, first + kTriBlock);
				for (RmUint32 id = first; id < last; ++id)
				{
					const RmUint32 *tri = &indices[static_cast<size_t>(order[id]) * 3];
					for (int j = 0; j < 3; ++j) hi = std::max(hi, vmap[tri[j]]);
				}
				const RmUint32 base = hi > kOffsetMax ? hi - kOffsetMax : 0;
				mBlockBase[blk] = base;
				for (RmUint32 id = first; id < last; ++id)
				{
					const RmUint32 *tri = &indices[static_cast<size_t>(order[id]) * 3];
					if (vmap[tri[0]] < base || vmap[tri[1]] < base || vmap[tri[2]] < base) ++wide;
				}
			}
			OwnerLabel owner("gs.collision_map.tris");
			mTriWide.reserve(static_cast<size_t>(wide) * 3);
		}
		for (RmUint32 t = 0; t < mTcount; ++t)
		{
			const RmUint32 a = vmap[indices[static_cast<size_t>(t) * 3 + 0]];
			const RmUint32 b = vmap[indices[static_cast<size_t>(t) * 3 + 1]];
			const RmUint32 c = vmap[indices[static_cast<size_t>(t) * 3 + 2]];
			const RmUint32 id = newId[t];
			const RmUint32 base = mBlockBase[id / kTriBlock];
			uint16_t *o = &mTri16[static_cast<size_t>(id) * 3];
			if (a < base || b < base || c < base)
			{
				const RmUint32 e = static_cast<RmUint32>(mTriWide.size() / 3);
				o[0] = kEscape;
				o[1] = static_cast<uint16_t>(e & 0xFFFFu);
				o[2] = static_cast<uint16_t>(e >> 16);
				mTriWide.push_back(a);
				mTriWide.push_back(b);
				mTriWide.push_back(c);
			}
			else
			{
				o[0] = static_cast<uint16_t>(a - base);
				o[1] = static_cast<uint16_t>(b - base);
				o[2] = static_cast<uint16_t>(c - base);
			}
			if (t >= flagBegin && t < flagEnd) mFlagBits[id >> 3] |= static_cast<unsigned char>(1u << (id & 7));
		}
		mWideCount = static_cast<RmUint32>(mTriWide.size() / 3);
		std::vector<RmUint32>().swap(newId);
		std::vector<RmUint32>().swap(indices);

		// 7. Vertices in the new numbering (same values).
		{
			OwnerLabel owner("gs.collision_map.verts");
			mVertices.resize(static_cast<size_t>(mVcount) * 3);
		}
		for (RmUint32 v = 0; v < mVcount; ++v)
		{
			const RmUint32 nv = vmap[v];
			mVertices[static_cast<size_t>(nv) * 3 + 0] = vertices[static_cast<size_t>(v) * 3 + 0];
			mVertices[static_cast<size_t>(nv) * 3 + 1] = vertices[static_cast<size_t>(v) * 3 + 1];
			mVertices[static_cast<size_t>(nv) * 3 + 2] = vertices[static_cast<size_t>(v) * 3 + 2];
		}
		std::vector<RmReal>().swap(vertices);

		const Bytes b = getBytes();
		LOG_INFO(MOD_MAP, "[raycastmesh] verts={} tris={} nodes={} leaves={} triangle_refs={} escaped_triangles={} "
		         "| bytes: vertices={} triangles={} triangle_refs={} nodes={} leaves={} flags={} total={} (B951 packed layout)",
		         mVcount, mTcount, nodeCount, leafCount, refCount, mWideCount,
		         b.vertices, b.triangles, b.triangleRefs, b.nodes, b.leaves, b.flags, b.total());
		(void)b;
	}

	virtual const RmReal * getVertices(void) const { return mVertices.data(); }
	virtual RmUint32 getVertexCount(void) const { return mVcount; }
	virtual RmUint32 getTriangleCount(void) const { return mTcount; }

	inline void triangleAt(RmUint32 tri, RmUint32 &a, RmUint32 &b, RmUint32 &c) const
	{
		const uint16_t *o = &mTri16[static_cast<size_t>(tri) * 3];
		if (o[0] == kEscape)
		{
			const RmUint32 *e = &mTriWide[(static_cast<size_t>(o[1]) | (static_cast<size_t>(o[2]) << 16)) * 3];
			a = e[0];
			b = e[1];
			c = e[2];
			return;
		}
		const RmUint32 base = mBlockBase[tri / kTriBlock];
		a = base + o[0];
		b = base + o[1];
		c = base + o[2];
	}

	inline RmUint32 refAt(RmUint32 r) const
	{
		RmUint32 v;
		memcpy(&v, mRefBytes.data() + static_cast<size_t>(r) * mRefWidth, 4);
		return v & mRefMask;
	}

	virtual void getTriangle(RmUint32 tri, RmUint32 *indices3) const
	{
		triangleAt(tri, indices3[0], indices3[1], indices3[2]);
	}

	virtual bool getTriangleFlag(RmUint32 tri) const
	{
		return (mFlagBits[tri >> 3] >> (tri & 7)) & 1u;
	}

	virtual Bytes getBytes(void) const
	{
		Bytes b;
		b.vertices = mVertices.capacity() * sizeof(RmReal);
		b.triangles = mTri16.capacity() * sizeof(uint16_t) + mTriWide.capacity() * sizeof(RmUint32)
		            + mBlockBase.capacity() * sizeof(RmUint32);
		b.triangleRefs = mRefBytes.capacity();
		b.nodes = mNodes.capacity() * sizeof(PackedNode);
		b.leaves = mLeafRanges.capacity() * sizeof(RmUint32);
		b.flags = mFlagBits.capacity();
		return b;
	}

	~MyRaycastMesh(void)
	{
	}

	// The original NodeAABB::raycast walk, iteratively: a node's box against
	// the segment shortened to the nearest hit so far, then the leaf's
	// triangles, or the left subtree and then the right one (the right child
	// waits on a stack). Same nodes,
	// same order, same per-node test as the recursive original; the query
	// state lives in locals rather than being threaded through every call.
	virtual bool raycast(const RmReal *from,const RmReal *to,RmReal *hitLocation,RmReal *hitNormal,RmReal *hitDistance) const
	{
		RmReal dir[3];
		dir[0] = to[0] - from[0];
		dir[1] = to[1] - from[1];
		dir[2] = to[2] - from[2];
		RmReal distance = sqrtf( dir[0]*dir[0] + dir[1]*dir[1]+dir[2]*dir[2] );
		if ( distance < 0.0000000001f ) return false;
		RmReal recipDistance = 1.0f / distance;
		dir[0]*=recipDistance;
		dir[1]*=recipDistance;
		dir[2]*=recipDistance;

		RmReal nearestDistance = distance;
		RmUint32 nearestTriIndex = TRI_EOF;
		bool hit = false;
		const RmReal *vertices = mVertices.data();
		const PackedNode *nodes = mNodes.data();
		const RmUint32 *ranges = mLeafRanges.data();

		RmUint32 stack[40];   // pending right children; depth <= 16 (maxDepth 15)
		RmUint32 top = 0;
		RmUint32 n = 0;
		for (;;)
		{
			const PackedNode &nd = nodes[n];
			RmReal sect[3];
			RmReal dist = nearestDistance;
			bool descend = false;
			if ( intersectLineSegmentAABB(nd.mMin,nd.mMax,from,dir,dist,sect) )
			{
				if ( nd.mData & PN_LEAF )
				{
					const RmUint32 *range = &ranges[2 * static_cast<size_t>(nd.mData & ~PN_LEAF)];  // owned, refs, next owned, next refs
					RmUint32 tri = range[0];
					const RmUint32 ownedEnd = range[2];
					RmUint32 r = range[1];
					const RmUint32 refEnd = range[3];
					for (;;)
					{
						RmUint32 t;
						if ( tri < ownedEnd ) t = tri++;
						else if ( r < refEnd ) t = refAt(r++);
						else break;
						RmUint32 i1, i2, i3;
						triangleAt(t, i1, i2, i3);
						const RmReal *p1 = &vertices[static_cast<size_t>(i1) * 3];
						const RmReal *p2 = &vertices[static_cast<size_t>(i2) * 3];
						const RmReal *p3 = &vertices[static_cast<size_t>(i3) * 3];
						RmReal tt;
						if ( rayIntersectsTriangle(from,dir,p1,p2,p3,tt) )
						{
							bool accept = false;
							if ( tt == nearestDistance && t < nearestTriIndex )
							{
								accept = true;
							}
							if ( tt < nearestDistance || accept )
							{
								nearestDistance = tt;
								if ( hitLocation )
								{
									hitLocation[0] = from[0]+dir[0]*tt;
									hitLocation[1] = from[1]+dir[1]*tt;
									hitLocation[2] = from[2]+dir[2]*tt;
								}
								if ( hitNormal )
								{
									getFaceNormal(t,hitNormal);
								}
								if ( hitDistance )
								{
									*hitDistance = tt;
								}
								nearestTriIndex = t;
								hit = true;
							}
						}
					}
				}
				else
				{
					if ( nd.mData & PN_HAS_RIGHT )
					{
						stack[top++] = nd.mData & PN_INDEX;
					}
					if ( nd.mData & PN_HAS_LEFT )
					{
						++n;
						descend = true;
					}
				}
			}
			if ( descend ) continue;
			if ( top == 0 ) break;
			n = stack[--top];
		}
		return hit;
	}

	virtual void release(void)
	{
		delete this;
	}

	virtual const RmReal * getBoundMin(void) const
	{
		return mNodes[0].mMin;
	}
	virtual const RmReal * getBoundMax(void) const
	{
		return mNodes[0].mMax;
	}

	void getFaceNormal(RmUint32 tri,RmReal *faceNormal) const
	{
		RmUint32 i1, i2, i3;
		triangleAt(tri, i1, i2, i3);
		const RmReal *vertices = mVertices.data();
		computePlane(&vertices[static_cast<size_t>(i3)*3],&vertices[static_cast<size_t>(i2)*3],&vertices[static_cast<size_t>(i1)*3],faceNormal);
	}

	virtual bool bruteForceRaycast(const RmReal *from,const RmReal *to,RmReal *hitLocation,RmReal *hitNormal,RmReal *hitDistance) const
	{
		bool ret = false;

		RmReal dir[3];

		dir[0] = to[0] - from[0];
		dir[1] = to[1] - from[1];
		dir[2] = to[2] - from[2];

		RmReal distance = sqrtf( dir[0]*dir[0] + dir[1]*dir[1]+dir[2]*dir[2] );
		if ( distance < 0.0000000001f ) return false;
		RmReal recipDistance = 1.0f / distance;
		dir[0]*=recipDistance;
		dir[1]*=recipDistance;
		dir[2]*=recipDistance;
		const RmReal *vertices = mVertices.data();
		RmReal nearestDistance = distance;

		for (RmUint32 tri=0; tri<mTcount; tri++)
		{
			RmUint32 i1, i2, i3;
			triangleAt(tri, i1, i2, i3);

			const RmReal *p1 = &vertices[static_cast<size_t>(i1)*3];
			const RmReal *p2 = &vertices[static_cast<size_t>(i2)*3];
			const RmReal *p3 = &vertices[static_cast<size_t>(i3)*3];

			RmReal t;
			if ( rayIntersectsTriangle(from,dir,p1,p2,p3,t))
			{
				if ( t < nearestDistance )
				{
					nearestDistance = t;
					if ( hitLocation )
					{
						hitLocation[0] = from[0]+dir[0]*t;
						hitLocation[1] = from[1]+dir[1]*t;
						hitLocation[2] = from[2]+dir[2]*t;
					}

					if ( hitNormal )
					{
						getFaceNormal(tri,hitNormal);
					}

					if ( hitDistance )
					{
						*hitDistance = t;
					}
					ret = true;
				}
			}
		}
		return ret;
	}

	std::vector<RmReal>			mVertices;    // 3 per vertex, first-use order
	std::vector<uint16_t>		mTri16;       // 3 offsets per stored triangle (from its block's base)
	std::vector<RmUint32>		mBlockBase;   // one base vertex number per kTriBlock stored triangles
	std::vector<RmUint32>		mTriWide;     // 3 per escaped triangle
	std::vector<unsigned char>	mRefBytes;    // mRefWidth bytes per leaf reference
	std::vector<PackedNode>		mNodes;       // pre-order; node 0 is the root
	std::vector<RmUint32>		mLeafRanges;  // leaf L owns stored triangles [r[2L], r[2L+2]) and references [r[2L+1], r[2L+3])
	std::vector<unsigned char>	mFlagBits;    // one caller flag bit per stored triangle
	RmUint32		mWideCount;
	RmUint32		mRefWidth;
	RmUint32		mRefMask;
	RmUint32		mVcount;
	RmUint32		mTcount;
};

}; // namespace RAYCAST_MESH

using namespace RAYCAST_MESH;

RaycastMesh * createRaycastMesh(RmUint32 vcount,
								const RmReal *vertices,
								RmUint32 tcount,
								const RmUint32 *indices,
								RmUint32 maxDepth,
								RmUint32 minLeafSize,
								RmReal	minAxisSize)
{
	std::vector<RmReal> v(vertices, vertices + 3 * (size_t)vcount);
	std::vector<RmUint32> t(indices, indices + 3 * (size_t)tcount);
	auto m = new MyRaycastMesh(std::move(v), std::move(t), 0, 0, maxDepth, minLeafSize, minAxisSize);
	return static_cast<RaycastMesh*>(m);
}

RaycastMesh * createRaycastMesh(std::vector<RmReal> &&vertices,
								std::vector<RmUint32> &&indices,
								RmUint32 flagBegin,
								RmUint32 flagEnd,
								RmUint32 maxDepth,
								RmUint32 minLeafSize,
								RmReal	minAxisSize)
{
	auto m = new MyRaycastMesh(std::move(vertices), std::move(indices), flagBegin, flagEnd, maxDepth, minLeafSize, minAxisSize);
	return static_cast<RaycastMesh*>(m);
}

void setRaycastMeshOwnerHooks(RmOwnerOpen open, RmOwnerClose close)
{
	g_ownerOpen = open;
	g_ownerClose = close;
}
