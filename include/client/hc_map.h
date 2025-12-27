#pragma once

#include <string>
#include <glm/glm.hpp>

#define BEST_Z_INVALID -99999

// Simplified Map class for headless client
// Compatible with zone server map files (V1 and V2 formats)
class HCMap
{
public:
	HCMap();
	~HCMap();

	// Load a map file from the specified path
	bool Load(const std::string& filename);

	// Find the best Z coordinate at the given X,Y position
	// Returns BEST_Z_INVALID if no valid Z found
	float FindBestZ(const glm::vec3 &start, glm::vec3 *result = nullptr) const;

	// Check line of sight between two points
	// Returns true if there is clear line of sight
	bool CheckLOS(const glm::vec3& start, const glm::vec3& end) const;

	// Check line of sight and return hit location if blocked
	// Returns true if there is clear line of sight
	// If blocked and hitLocation is non-null, stores the hit point
	bool CheckLOSWithHit(const glm::vec3& start, const glm::vec3& end, glm::vec3* hitLocation) const;

	// Check if map is loaded
	bool IsLoaded() const { return m_impl != nullptr; }

	// Static helper to load a map file with proper path
	static HCMap* LoadMapFile(const std::string& zone_name, const std::string& maps_path);

private:
	struct impl;
	impl *m_impl;
};
