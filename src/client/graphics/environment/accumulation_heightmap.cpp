#include "client/graphics/environment/accumulation_heightmap.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace EQT {
namespace Graphics {
namespace Environment {

AccumulationHeightmap::AccumulationHeightmap() {
    snowDepth_.resize(GRID_SIZE * GRID_SIZE, 0.0f);
    sheltered_.resize(GRID_SIZE * GRID_SIZE, false);
}

void AccumulationHeightmap::initialize(float centerX, float centerY) {
    gridCenter_ = glm::vec2(centerX, centerY);
    gridOffsetX_ = 0;
    gridOffsetY_ = 0;
    reset();
    initialized_ = true;
}

void AccumulationHeightmap::reset() {
    std::fill(snowDepth_.begin(), snowDepth_.end(), 0.0f);
    std::fill(sheltered_.begin(), sheltered_.end(), false);
}

void AccumulationHeightmap::updateCenter(float centerX, float centerY) {
    if (!initialized_) {
        initialize(centerX, centerY);
        return;
    }

    float deltaX = centerX - gridCenter_.x;
    float deltaY = centerY - gridCenter_.y;

    // Check if we need to shift the grid
    if (std::abs(deltaX) >= shiftThreshold_ || std::abs(deltaY) >= shiftThreshold_) {
        // Calculate cell shift amounts
        int shiftX = static_cast<int>(std::round(deltaX / CELL_SIZE));
        int shiftY = static_cast<int>(std::round(deltaY / CELL_SIZE));

        if (shiftX != 0 || shiftY != 0) {
            shiftGrid(shiftX, shiftY);

            // Update grid center
            gridCenter_.x += shiftX * CELL_SIZE;
            gridCenter_.y += shiftY * CELL_SIZE;
        }
    }
}

void AccumulationHeightmap::shiftGrid(int deltaX, int deltaY) {
    // If shifting more than the grid size, just reset
    if (std::abs(deltaX) >= GRID_SIZE || std::abs(deltaY) >= GRID_SIZE) {
        reset();
        return;
    }

    // Create temporary copies
    std::vector<float> newDepth(GRID_SIZE * GRID_SIZE, 0.0f);
    std::vector<bool> newSheltered(GRID_SIZE * GRID_SIZE, false);

    // Copy data with offset
    for (int y = 0; y < GRID_SIZE; ++y) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            int srcX = x + deltaX;
            int srcY = y + deltaY;

            if (srcX >= 0 && srcX < GRID_SIZE && srcY >= 0 && srcY < GRID_SIZE) {
                int dstIdx = getCellIndex(x, y);
                int srcIdx = getCellIndex(srcX, srcY);
                newDepth[dstIdx] = snowDepth_[srcIdx];
                newSheltered[dstIdx] = sheltered_[srcIdx];
            }
            // New cells (outside old grid) remain at default values (0, false)
        }
    }

    snowDepth_ = std::move(newDepth);
    sheltered_ = std::move(newSheltered);
}

void AccumulationHeightmap::addSnow(float worldX, float worldY, float amount) {
    int gridX, gridY;
    if (!worldToGrid(worldX, worldY, gridX, gridY)) {
        return;
    }

    int idx = getCellIndex(gridX, gridY);
    if (!sheltered_[idx]) {
        snowDepth_[idx] = std::min(maxDepth_, snowDepth_[idx] + amount);
    }
}

void AccumulationHeightmap::addSnowUniform(float amount) {
    for (int i = 0; i < GRID_SIZE * GRID_SIZE; ++i) {
        if (!sheltered_[i]) {
            snowDepth_[i] = std::min(maxDepth_, snowDepth_[i] + amount);
        }
    }
}

void AccumulationHeightmap::meltUniform(float amount) {
    for (int i = 0; i < GRID_SIZE * GRID_SIZE; ++i) {
        snowDepth_[i] = std::max(0.0f, snowDepth_[i] - amount);
    }
}

float AccumulationHeightmap::getSnowDepth(float worldX, float worldY) const {
    int gridX, gridY;
    if (!worldToGrid(worldX, worldY, gridX, gridY)) {
        return 0.0f;
    }

    return snowDepth_[getCellIndex(gridX, gridY)];
}

void AccumulationHeightmap::setSnowDepth(float worldX, float worldY, float depth) {
    int gridX, gridY;
    if (!worldToGrid(worldX, worldY, gridX, gridY)) {
        return;
    }

    snowDepth_[getCellIndex(gridX, gridY)] = std::clamp(depth, 0.0f, maxDepth_);
}

float AccumulationHeightmap::getSnowDepthInterpolated(float worldX, float worldY) const {
    // Get fractional grid position
    float halfExtent = GRID_EXTENT * 0.5f;
    float localX = (worldX - gridCenter_.x + halfExtent) / CELL_SIZE;
    float localY = (worldY - gridCenter_.y + halfExtent) / CELL_SIZE;

    // Get integer grid coordinates
    int x0 = static_cast<int>(std::floor(localX));
    int y0 = static_cast<int>(std::floor(localY));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Get fractional parts
    float fx = localX - x0;
    float fy = localY - y0;

    // Clamp to grid bounds
    x0 = std::clamp(x0, 0, GRID_SIZE - 1);
    y0 = std::clamp(y0, 0, GRID_SIZE - 1);
    x1 = std::clamp(x1, 0, GRID_SIZE - 1);
    y1 = std::clamp(y1, 0, GRID_SIZE - 1);

    // Sample four corners
    float d00 = snowDepth_[getCellIndex(x0, y0)];
    float d10 = snowDepth_[getCellIndex(x1, y0)];
    float d01 = snowDepth_[getCellIndex(x0, y1)];
    float d11 = snowDepth_[getCellIndex(x1, y1)];

    // Bilinear interpolation
    float d0 = d00 * (1.0f - fx) + d10 * fx;
    float d1 = d01 * (1.0f - fx) + d11 * fx;
    return d0 * (1.0f - fy) + d1 * fy;
}

void AccumulationHeightmap::setSheltered(float worldX, float worldY, bool sheltered) {
    int gridX, gridY;
    if (!worldToGrid(worldX, worldY, gridX, gridY)) {
        return;
    }

    sheltered_[getCellIndex(gridX, gridY)] = sheltered;
}

bool AccumulationHeightmap::isSheltered(float worldX, float worldY) const {
    int gridX, gridY;
    if (!worldToGrid(worldX, worldY, gridX, gridY)) {
        return true;  // Outside grid considered sheltered
    }

    return sheltered_[getCellIndex(gridX, gridY)];
}

void AccumulationHeightmap::getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const {
    float halfExtent = GRID_EXTENT * 0.5f;
    minX = gridCenter_.x - halfExtent;
    minY = gridCenter_.y - halfExtent;
    maxX = gridCenter_.x + halfExtent;
    maxY = gridCenter_.y + halfExtent;
}

float AccumulationHeightmap::getAverageDepth() const {
    float total = 0.0f;
    int count = 0;
    for (int i = 0; i < GRID_SIZE * GRID_SIZE; ++i) {
        if (snowDepth_[i] > 0.0f) {
            total += snowDepth_[i];
            ++count;
        }
    }
    return count > 0 ? total / count : 0.0f;
}

int AccumulationHeightmap::getSnowCellCount() const {
    int count = 0;
    for (int i = 0; i < GRID_SIZE * GRID_SIZE; ++i) {
        if (snowDepth_[i] > 0.01f) {
            ++count;
        }
    }
    return count;
}

std::string AccumulationHeightmap::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Snow: " << getSnowCellCount() << "/" << (GRID_SIZE * GRID_SIZE)
       << " cells, avg=" << std::fixed << std::setprecision(2) << getAverageDepth()
       << ", max=" << maxDepth_;
    return ss.str();
}

bool AccumulationHeightmap::worldToGrid(float worldX, float worldY, int& gridX, int& gridY) const {
    float halfExtent = GRID_EXTENT * 0.5f;

    // Convert to local grid coordinates
    float localX = worldX - gridCenter_.x + halfExtent;
    float localY = worldY - gridCenter_.y + halfExtent;

    // Convert to grid indices
    gridX = static_cast<int>(localX / CELL_SIZE);
    gridY = static_cast<int>(localY / CELL_SIZE);

    // Check bounds
    if (gridX < 0 || gridX >= GRID_SIZE || gridY < 0 || gridY >= GRID_SIZE) {
        return false;
    }

    return true;
}

void AccumulationHeightmap::gridToWorld(int gridX, int gridY, float& worldX, float& worldY) const {
    float halfExtent = GRID_EXTENT * 0.5f;

    // Convert grid index to local position (cell center)
    float localX = (gridX + 0.5f) * CELL_SIZE;
    float localY = (gridY + 0.5f) * CELL_SIZE;

    // Convert to world coordinates
    worldX = gridCenter_.x - halfExtent + localX;
    worldY = gridCenter_.y - halfExtent + localY;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
