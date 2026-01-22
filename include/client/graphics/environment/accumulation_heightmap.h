#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace EQT {
namespace Graphics {
namespace Environment {

/**
 * AccumulationHeightmap - Grid-based snow depth tracking.
 *
 * Maintains a 2D grid of snow accumulation depths that follows
 * the player. Each cell tracks the current snow depth at that
 * location, allowing for realistic buildup and melting.
 */
class AccumulationHeightmap {
public:
    // Grid configuration
    static constexpr int GRID_SIZE = 64;           // Grid dimensions (64x64 cells)
    static constexpr float CELL_SIZE = 4.0f;       // World units per cell
    static constexpr float GRID_EXTENT = GRID_SIZE * CELL_SIZE;  // Total coverage (256 units)

    AccumulationHeightmap();
    ~AccumulationHeightmap() = default;

    /**
     * Initialize the heightmap.
     * @param centerX Initial center X position
     * @param centerY Initial center Y position
     */
    void initialize(float centerX, float centerY);

    /**
     * Reset all accumulation to zero.
     */
    void reset();

    /**
     * Update the grid center position.
     * Shifts data when player moves significantly.
     * @param centerX New center X position
     * @param centerY New center Y position
     */
    void updateCenter(float centerX, float centerY);

    /**
     * Add snow to a specific world position.
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param amount Snow depth to add
     */
    void addSnow(float worldX, float worldY, float amount);

    /**
     * Add snow uniformly across the entire grid.
     * @param amount Snow depth to add per cell
     */
    void addSnowUniform(float amount);

    /**
     * Remove snow (melting) uniformly across the grid.
     * @param amount Snow depth to remove per cell
     */
    void meltUniform(float amount);

    /**
     * Get snow depth at a world position.
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @return Snow depth at position (0 if outside grid)
     */
    float getSnowDepth(float worldX, float worldY) const;

    /**
     * Set snow depth at a world position.
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param depth New snow depth
     */
    void setSnowDepth(float worldX, float worldY, float depth);

    /**
     * Get interpolated snow depth (bilinear).
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @return Interpolated snow depth
     */
    float getSnowDepthInterpolated(float worldX, float worldY) const;

    /**
     * Mark a cell as sheltered (no accumulation).
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param sheltered True if position is sheltered
     */
    void setSheltered(float worldX, float worldY, bool sheltered);

    /**
     * Check if a position is sheltered.
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @return True if sheltered
     */
    bool isSheltered(float worldX, float worldY) const;

    /**
     * Get current grid center position.
     */
    glm::vec2 getCenter() const { return gridCenter_; }

    /**
     * Get grid bounds in world coordinates.
     */
    void getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const;

    /**
     * Get maximum snow depth in the grid.
     */
    float getMaxDepth() const { return maxDepth_; }

    /**
     * Set maximum allowed snow depth.
     */
    void setMaxDepth(float maxDepth) { maxDepth_ = maxDepth; }

    /**
     * Get average snow depth across the grid.
     */
    float getAverageDepth() const;

    /**
     * Get number of cells with snow.
     */
    int getSnowCellCount() const;

    /**
     * Get debug info string.
     */
    std::string getDebugInfo() const;

private:
    /**
     * Convert world coordinates to grid indices.
     * @return True if position is within grid
     */
    bool worldToGrid(float worldX, float worldY, int& gridX, int& gridY) const;

    /**
     * Convert grid indices to world coordinates (cell center).
     */
    void gridToWorld(int gridX, int gridY, float& worldX, float& worldY) const;

    /**
     * Shift grid data when center moves.
     */
    void shiftGrid(int deltaX, int deltaY);

    /**
     * Get cell index in flat array.
     */
    int getCellIndex(int gridX, int gridY) const {
        return gridY * GRID_SIZE + gridX;
    }

    // Grid data
    std::vector<float> snowDepth_;      // Snow depth per cell
    std::vector<bool> sheltered_;       // Shelter flag per cell

    // Grid positioning
    glm::vec2 gridCenter_{0.0f, 0.0f};  // World position of grid center
    int gridOffsetX_ = 0;               // Grid origin offset (for shifting)
    int gridOffsetY_ = 0;

    // Configuration
    float maxDepth_ = 1.0f;             // Maximum snow depth
    float shiftThreshold_ = CELL_SIZE * 8;  // Distance before shifting grid

    bool initialized_ = false;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
