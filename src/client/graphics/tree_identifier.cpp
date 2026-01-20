#include "client/graphics/tree_identifier.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {

TreeIdentifier::TreeIdentifier() {
    initDefaultPatterns();
}

void TreeIdentifier::initDefaultPatterns() {
    // Tree mesh name patterns - common naming conventions in EQ zones
    // Format: lowercase pattern with * as wildcard
    treePatterns_ = {
        "*tree*",
        "*pine*",
        "*oak*",
        "*palm*",
        "*willow*",
        "*birch*",
        "*maple*",
        "*cedar*",
        "*fir*",
        "*spruce*",
        "*redwood*",
        "*elm*",
        "*ash*",
        "*cypress*",
        "*sequoia*",
        "*evergreen*",
        "*conifer*",
        "*deciduous*",
        "*sapling*",
        "*trunk*",
        "*treebig*",
        "*treemed*",
        "*treesm*",
        "*tree_*",
        "*_tree",
        "t_*tree*",      // Common EQ prefix
        "obj_tree*",
        "plant_tree*"
    };

    // Leaf/foliage material patterns - these get maximum wind influence
    leafPatterns_ = {
        "*leaf*",
        "*leaves*",
        "*foliage*",
        "*frond*",
        "*needle*",
        "*pine*",
        "*branch*",
        "*canopy*",
        "*crown*",
        "*top*tree*",
        "*tree*top*",
        "*greenery*",
        "*vegetation*"
    };
}

std::string TreeIdentifier::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool TreeIdentifier::matchesPattern(const std::string& name, const std::string& pattern) const {
    std::string lowerName = toLower(name);
    std::string lowerPattern = toLower(pattern);

    // Simple wildcard matching with *
    // Pattern like "*tree*" matches any string containing "tree"

    if (lowerPattern.empty()) {
        return lowerName.empty();
    }

    // Handle patterns without wildcards - exact match
    if (lowerPattern.find('*') == std::string::npos) {
        return lowerName == lowerPattern;
    }

    // Split pattern by * and check if all parts appear in order
    size_t patternPos = 0;
    size_t namePos = 0;

    while (patternPos < lowerPattern.size()) {
        // Find next wildcard
        size_t nextWild = lowerPattern.find('*', patternPos);

        if (nextWild == std::string::npos) {
            // No more wildcards - rest of pattern must match end of name
            std::string remainder = lowerPattern.substr(patternPos);
            if (lowerName.size() < remainder.size()) {
                return false;
            }
            return lowerName.compare(lowerName.size() - remainder.size(),
                                     remainder.size(), remainder) == 0;
        }

        if (nextWild == patternPos) {
            // Wildcard at current position - skip it
            patternPos++;
            continue;
        }

        // Extract the literal part before the wildcard
        std::string literal = lowerPattern.substr(patternPos, nextWild - patternPos);

        // Find this literal in the name starting from current position
        size_t found = lowerName.find(literal, namePos);
        if (found == std::string::npos) {
            return false;
        }

        // If pattern starts with literal (no leading *), it must match start
        if (patternPos == 0 && found != 0) {
            return false;
        }

        namePos = found + literal.size();
        patternPos = nextWild + 1;
    }

    return true;
}

bool TreeIdentifier::matchesAnyPattern(const std::string& name,
                                       const std::vector<std::string>& patterns) const {
    for (const auto& pattern : patterns) {
        if (matchesPattern(name, pattern)) {
            return true;
        }
    }
    return false;
}

bool TreeIdentifier::isTreeMesh(const std::string& meshName, const std::string& textureName) const {
    std::string lowerMesh = toLower(meshName);

    // Check explicit exclusions first
    if (excludedMeshes_.find(lowerMesh) != excludedMeshes_.end()) {
        return false;
    }

    // Check zone-specific explicit inclusions
    if (zoneTreeMeshes_.find(lowerMesh) != zoneTreeMeshes_.end()) {
        return true;
    }

    // Check mesh name against tree patterns
    if (matchesAnyPattern(meshName, treePatterns_)) {
        return true;
    }

    // If texture name provided, check if it suggests a tree
    if (!textureName.empty()) {
        // A leaf texture strongly suggests this is a tree
        if (isLeafMaterial(textureName)) {
            return true;
        }

        // Also check texture against tree patterns
        if (matchesAnyPattern(textureName, treePatterns_)) {
            return true;
        }
    }

    return false;
}

bool TreeIdentifier::isLeafMaterial(const std::string& materialName) const {
    return matchesAnyPattern(materialName, leafPatterns_);
}

bool TreeIdentifier::loadZoneOverrides(const std::string& zoneName) {
    // Clear previous zone overrides
    clearZoneOverrides();

    if (zoneName.empty()) {
        return true;
    }

    // Try zone-specific config file
    std::string configPath = "data/config/zones/" + zoneName + "/tree_overrides.json";
    std::ifstream file(configPath);

    if (!file.is_open()) {
        // No zone overrides - this is not an error
        LOG_DEBUG(MOD_GRAPHICS, "TreeIdentifier: No zone overrides for {}", zoneName);
        return true;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "TreeIdentifier: JSON parse error in {}: {}", configPath, errors);
        return false;
    }

    // Load explicit tree meshes
    const Json::Value& trees = root["tree_meshes"];
    if (trees.isArray()) {
        for (const auto& tree : trees) {
            if (tree.isString()) {
                addZoneTreeMesh(tree.asString());
            }
        }
    }

    // Load excluded meshes (false positives)
    const Json::Value& excluded = root["excluded_meshes"];
    if (excluded.isArray()) {
        for (const auto& excl : excluded) {
            if (excl.isString()) {
                addExcludedMesh(excl.asString());
            }
        }
    }

    // Load additional tree patterns for this zone
    const Json::Value& patterns = root["tree_patterns"];
    if (patterns.isArray()) {
        for (const auto& pattern : patterns) {
            if (pattern.isString()) {
                addTreePattern(pattern.asString());
            }
        }
    }

    // Load additional leaf patterns for this zone
    const Json::Value& leafPatterns = root["leaf_patterns"];
    if (leafPatterns.isArray()) {
        for (const auto& pattern : leafPatterns) {
            if (pattern.isString()) {
                addLeafPattern(pattern.asString());
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "TreeIdentifier: Loaded zone overrides for {} "
             "(trees: {}, excluded: {})",
             zoneName, zoneTreeMeshes_.size(), excludedMeshes_.size());

    return true;
}

void TreeIdentifier::addTreePattern(const std::string& pattern) {
    treePatterns_.push_back(pattern);
}

void TreeIdentifier::addLeafPattern(const std::string& pattern) {
    leafPatterns_.push_back(pattern);
}

void TreeIdentifier::addZoneTreeMesh(const std::string& meshName) {
    zoneTreeMeshes_.insert(toLower(meshName));
}

void TreeIdentifier::addExcludedMesh(const std::string& meshName) {
    excludedMeshes_.insert(toLower(meshName));
}

void TreeIdentifier::clearZoneOverrides() {
    zoneTreeMeshes_.clear();
    excludedMeshes_.clear();
}

TreeIdentifier::Stats TreeIdentifier::getStats() const {
    Stats stats;
    stats.treePatternCount = treePatterns_.size();
    stats.leafPatternCount = leafPatterns_.size();
    stats.zoneTreeMeshCount = zoneTreeMeshes_.size();
    stats.excludedMeshCount = excludedMeshes_.size();
    return stats;
}

} // namespace Graphics
} // namespace EQT
