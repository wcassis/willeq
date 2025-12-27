#include "client/pathfinder_waypoint.h"
#include "client/position.h"
#include "common/util/random.h"
#include "common/logging.h"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/astar_search.hpp>
#include <string>
#include <memory>
#include <iostream>
#include <stdio.h>
#include <cstring>

#pragma pack(1)
struct NeighbourNode {
	int16_t id;
	float distance;
	uint8_t Teleport;
	int16_t DoorID;
};

struct PathNode {
	uint16_t id;
	glm::vec3 v;
	float bestz;
	NeighbourNode Neighbours[50];
};

struct PathFileHeader {
	uint32_t version;
	uint32_t PathNodeCount;
};
#pragma pack()

struct Edge
{
	float distance;
	bool teleport;
	int door_id;
};

struct Node
{
	int id;
	glm::vec3 v;
	float bestz;
	std::map<int, Edge> edges;
};

template <class Graph, class CostType, class NodeMap>
class distance_heuristic : public boost::astar_heuristic<Graph, CostType>
{
public:
	typedef typename boost::graph_traits<Graph>::vertex_descriptor Vertex;

	distance_heuristic(NodeMap n, Vertex goal)
		: m_node(n), m_goal(goal) {}
	CostType operator()(Vertex u)
	{
		CostType dx = m_node[m_goal].v.x - m_node[u].v.x;
		CostType dy = m_node[m_goal].v.y - m_node[u].v.y;
		CostType dz = m_node[m_goal].v.z - m_node[u].v.z;
		return ::sqrt(dx * dx + dy * dy + dz * dz);
	}
private:
	NodeMap m_node;
	Vertex m_goal;
};

struct found_goal {};
template <class Vertex>
class astar_goal_visitor : public boost::default_astar_visitor
{
public:
	astar_goal_visitor(Vertex goal) : m_goal(goal) {}
	template <class Graph>
	void examine_vertex(Vertex u, Graph& g) {
		if (u == m_goal)
			throw found_goal();
	}
private:
	Vertex m_goal;
};

typedef boost::geometry::model::point<float, 3, boost::geometry::cs::cartesian> Point;
typedef std::pair<Point, unsigned int> RTreeValue;
typedef boost::adjacency_list<boost::listS, boost::vecS, boost::directedS, boost::no_property,
	boost::property<boost::edge_weight_t, float>> GraphType;
typedef boost::property_map<GraphType, boost::edge_weight_t>::type WeightMap;

struct PathfinderWaypoint::Implementation {
	bool PathFileValid;
	boost::geometry::index::rtree<RTreeValue, boost::geometry::index::quadratic<16>> Tree;
	GraphType Graph;
	std::vector<Node> Nodes;
	std::string FileName;
	EQ::Random random;
};

PathfinderWaypoint::PathfinderWaypoint(const std::string &path)
{
	m_impl.reset(new Implementation());
	m_impl->PathFileValid = false;
	m_impl->FileName = path;
	Load(path);
}

PathfinderWaypoint::~PathfinderWaypoint()
{
}

bool PathfinderWaypoint::IsLoaded() const
{
	return m_impl && m_impl->PathFileValid;
}

IPathfinder::IPath PathfinderWaypoint::FindRoute(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, int flags)
{
	stuck = false;
	partial = false;

	if (!m_impl->PathFileValid) {
		return IPath();
	}

	std::vector<RTreeValue> result_start_n;
	m_impl->Tree.query(boost::geometry::index::nearest(Point(start.x, start.y, start.z), 1), std::back_inserter(result_start_n));
	if (result_start_n.size() == 0) {
		return IPath();
	}

	std::vector<RTreeValue> result_end_n;
	m_impl->Tree.query(boost::geometry::index::nearest(Point(end.x, end.y, end.z), 1), std::back_inserter(result_end_n));
	if (result_end_n.size() == 0) {
		return IPath();
	}

	auto &nearest_start = *result_start_n.begin();
	auto &nearest_end = *result_end_n.begin();

	if (nearest_start.second == nearest_end.second) {
		IPath Route;
		Route.push_back(start);
		Route.push_back(end);
		return Route;
	}

	std::vector<GraphType::vertex_descriptor> p(boost::num_vertices(m_impl->Graph));
	try {
		boost::astar_search(m_impl->Graph, nearest_start.second,
			distance_heuristic<GraphType, float, Node*>(&m_impl->Nodes[0], nearest_end.second),
			boost::predecessor_map(&p[0])
				.visitor(astar_goal_visitor<size_t>(nearest_end.second)));
	}
	catch (found_goal)
	{
		IPath Route;

		Route.push_front(end);
		for (size_t v = nearest_end.second;; v = p[v]) {
			if (p[v] == v) {
				Route.push_front(m_impl->Nodes[v].v);
				break;
			}
			else {
				auto &node = m_impl->Nodes[v];

				auto iter = node.edges.find((int)p[v + 1]);
				if (iter != node.edges.end()) {
					auto &edge = iter->second;
					if (edge.teleport) {
						Route.push_front(m_impl->Nodes[v].v);
						Route.push_front(true);
					}
					else {
						Route.push_front(m_impl->Nodes[v].v);
					}
				}
				else {
					Route.push_front(m_impl->Nodes[v].v);
				}
			}
		}

		Route.push_front(start);
		return Route;
	}

	return IPath();
}

IPathfinder::IPath PathfinderWaypoint::FindPath(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, const PathfinderOptions& opts)
{
	// Waypoint pathfinder ignores options and delegates to FindRoute
	return FindRoute(start, end, partial, stuck, opts.flags);
}

glm::vec3 PathfinderWaypoint::GetRandomLocation(const glm::vec3 &start, int flags)
{
	if (m_impl->Nodes.size() > 0) {
		auto idx = m_impl->random.Int(0, (int)m_impl->Nodes.size() - 1);
		auto &node = m_impl->Nodes[idx];

		return node.v;
	}

	return glm::vec3();
}

void PathfinderWaypoint::Load(const std::string &filename) {
	PathFileHeader Head;
	Head.PathNodeCount = 0;
	Head.version = 2;

	FILE *f = fopen(filename.c_str(), "rb");
	if (f) {
		char Magic[10];

		if (fread(&Magic, 9, 1, f) != 1) {
			fclose(f);
			return;
		}

		if (strncmp(Magic, "EQEMUPATH", 9))
		{
			LOG_ERROR(MOD_MAP, "Bad Magic String in .path file");
			fclose(f);
			return;
		}

		if (fread(&Head, sizeof(Head), 1, f) != 1) {
			fclose(f);
			return;
		}

		LOG_INFO(MOD_MAP, "Path File Header: Version {}, PathNodes {}", Head.version, Head.PathNodeCount);

		if (Head.version == 2)
		{
			LoadV2(f, Head);
			return;
		}
		else if (Head.version == 3) {
			LoadV3(f, Head);
			return;
		}
		else {
			LOG_ERROR(MOD_MAP, "Unsupported path file version");
			fclose(f);
			return;
		}
	}
}

void PathfinderWaypoint::LoadV2(FILE *f, const PathFileHeader &header)
{
	std::unique_ptr<PathNode[]> PathNodes(new PathNode[header.PathNodeCount]);

	if (fread(PathNodes.get(), sizeof(PathNode), header.PathNodeCount, f) != header.PathNodeCount) {
		fclose(f);
		return;
	}

	int MaxNodeID = header.PathNodeCount - 1;

	m_impl->PathFileValid = true;

	m_impl->Nodes.reserve(header.PathNodeCount);
	for (uint32_t i = 0; i < header.PathNodeCount; ++i)
	{
		auto &n = PathNodes[i];
		Node node;
		node.id = i;
		node.v = n.v;
		node.bestz = n.bestz;
		m_impl->Nodes.push_back(node);
	}

	auto weightmap = boost::get(boost::edge_weight, m_impl->Graph);
	for (uint32_t i = 0; i < header.PathNodeCount; ++i) {
		for (uint32_t j = 0; j < 50; ++j)
		{
			auto &node = m_impl->Nodes[i];
			if (PathNodes[i].Neighbours[j].id > MaxNodeID)
			{
				LOG_ERROR(MOD_MAP, "Path Node {}, Neighbour {} ({}) out of range", i, j, PathNodes[i].Neighbours[j].id);
				m_impl->PathFileValid = false;
			}

			if (PathNodes[i].Neighbours[j].id > 0) {
				Edge edge;
				edge.distance = PathNodes[i].Neighbours[j].distance;
				edge.door_id = PathNodes[i].Neighbours[j].DoorID;
				edge.teleport = PathNodes[i].Neighbours[j].Teleport;

				node.edges[PathNodes[i].Neighbours[j].id] = edge;
			}
		}
	}

	BuildGraph();
	fclose(f);
}

void PathfinderWaypoint::LoadV3(FILE *f, const PathFileHeader &header)
{
	m_impl->Nodes.reserve(header.PathNodeCount);

	uint32_t edge_count = 0;
	if (fread(&edge_count, sizeof(uint32_t), 1, f) != 1) {
		fclose(f);
		return;
	}

	for (uint32_t i = 0; i < header.PathNodeCount; ++i)
	{
		uint32_t id = 0;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float best_z = 0.0f;

		if (fread(&id, sizeof(uint32_t), 1, f) != 1) { fclose(f); return; }
		if (fread(&x, sizeof(float), 1, f) != 1) { fclose(f); return; }
		if (fread(&y, sizeof(float), 1, f) != 1) { fclose(f); return; }
		if (fread(&z, sizeof(float), 1, f) != 1) { fclose(f); return; }
		if (fread(&best_z, sizeof(float), 1, f) != 1) { fclose(f); return; }

		Node n;
		n.id = id;
		n.bestz = best_z;
		n.v.x = x;
		n.v.y = y;
		n.v.z = z;

		m_impl->Nodes.push_back(n);
	}

	for (uint32_t j = 0; j < edge_count; ++j) {
		uint32_t from = 0;
		uint32_t to = 0;
		int8_t teleport = 0;
		float distance = 0.0f;
		int32_t door_id = 0;

		if (fread(&from, sizeof(uint32_t), 1, f) != 1) { fclose(f); return; }
		if (fread(&to, sizeof(uint32_t), 1, f) != 1) { fclose(f); return; }
		if (fread(&teleport, sizeof(int8_t), 1, f) != 1) { fclose(f); return; }
		if (fread(&distance, sizeof(float), 1, f) != 1) { fclose(f); return; }
		if (fread(&door_id, sizeof(int32_t), 1, f) != 1) { fclose(f); return; }

		Edge e;
		e.teleport = teleport > 0 ? true : false;
		e.distance = distance;
		e.door_id = door_id;

		if (from < m_impl->Nodes.size()) {
			auto &n = m_impl->Nodes[from];
			n.edges[to] = e;
		}
	}

	m_impl->PathFileValid = true;

	BuildGraph();
	fclose(f);
}

Node *PathfinderWaypoint::FindPathNodeByCoordinates(float x, float y, float z)
{
	for (auto &node : m_impl->Nodes) {
		auto dist = Distance(glm::vec3(x, y, z), node.v);

		if (dist < 0.1) {
			return &node;
		}
	}

	return nullptr;
}

void PathfinderWaypoint::BuildGraph()
{
	m_impl->Graph = GraphType();
	m_impl->Tree = boost::geometry::index::rtree<RTreeValue, boost::geometry::index::quadratic<16>>();

	for (auto &node : m_impl->Nodes) {
		RTreeValue rtv;
		rtv.first = Point(node.v.x, node.v.y, node.v.z);
		rtv.second = node.id;
		m_impl->Tree.insert(rtv);
		boost::add_vertex(m_impl->Graph);
	}

	// Populate edges now that we've created all the nodes
	auto weightmap = boost::get(boost::edge_weight, m_impl->Graph);
	for (auto &node : m_impl->Nodes) {
		for (auto &edge : node.edges) {
			GraphType::edge_descriptor e;
			bool inserted;
			boost::tie(e, inserted) = boost::add_edge(node.id, edge.first, m_impl->Graph);
			weightmap[e] = edge.second.distance;
		}
	}
}
