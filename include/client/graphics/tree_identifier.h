#ifndef EQT_GRAPHICS_TREE_IDENTIFIER_H
#define EQT_GRAPHICS_TREE_IDENTIFIER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace EQT {
namespace Graphics {

/**
 * Identifies tree meshes and leaf materials from zone geometry.
 *
 * Trees are identified by matching patterns in mesh names and texture names.
 * Zone-specific overrides can be loaded to manually specify tree meshes
 * or exclude false positives.
 */
class TreeIdentifier {
public:
    TreeIdentifier();

    /**
     * Check if a mesh is a tree based on its name and texture.
     * @param meshName The name of the mesh from zone geometry
     * @param textureName Primary texture name (optional, can be empty)
     * @return true if the mesh should be treated as a tree
     */
    bool isTreeMesh(const std::string& meshName, const std::string& textureName = "") const;

    /**
     * Check if a material/texture is a leaf material that should sway.
     * Leaf materials get more wind influence than bark/trunk materials.
     * @param materialName The texture or material name
     * @return true if this is a leaf/foliage material
     */
    bool isLeafMaterial(const std::string& materialName) const;

    /**
     * Load zone-specific overrides from a configuration file.
     * Overrides can explicitly include or exclude certain mesh names.
     * @param zoneName The zone short name (e.g., "qeynos2")
     * @return true if overrides were loaded successfully
     */
    bool loadZoneOverrides(const std::string& zoneName);

    /**
     * Add a pattern to match tree mesh names.
     * @param pattern Case-insensitive pattern (supports * wildcard)
     */
    void addTreePattern(const std::string& pattern);

    /**
     * Add a pattern to match leaf/foliage material names.
     * @param pattern Case-insensitive pattern (supports * wildcard)
     */
    void addLeafPattern(const std::string& pattern);

    /**
     * Explicitly mark a mesh name as a tree for this zone.
     * @param meshName Exact mesh name to include
     */
    void addZoneTreeMesh(const std::string& meshName);

    /**
     * Explicitly exclude a mesh name from tree detection.
     * @param meshName Exact mesh name to exclude
     */
    void addExcludedMesh(const std::string& meshName);

    /**
     * Clear all zone-specific overrides.
     */
    void clearZoneOverrides();

    /**
     * Get statistics about identified trees (for debugging).
     */
    struct Stats {
        size_t treePatternCount = 0;
        size_t leafPatternCount = 0;
        size_t zoneTreeMeshCount = 0;
        size_t excludedMeshCount = 0;
    };
    Stats getStats() const;

private:
    // Initialize default patterns for common tree/leaf names
    void initDefaultPatterns();

    // Check if a name matches a pattern (case-insensitive, * wildcard)
    bool matchesPattern(const std::string& name, const std::string& pattern) const;

    // Check if name matches any pattern in the list
    bool matchesAnyPattern(const std::string& name, const std::vector<std::string>& patterns) const;

    // Convert string to lowercase for case-insensitive matching
    static std::string toLower(const std::string& str);

    // Default patterns for tree mesh names
    std::vector<std::string> treePatterns_;

    // Default patterns for leaf/foliage material names
    std::vector<std::string> leafPatterns_;

    // Zone-specific explicit tree mesh names
    std::unordered_set<std::string> zoneTreeMeshes_;

    // Explicitly excluded mesh names (false positives)
    std::unordered_set<std::string> excludedMeshes_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_TREE_IDENTIFIER_H
