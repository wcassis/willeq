#ifndef EQT_GRAPHICS_DOOR_MANAGER_H
#define EQT_GRAPHICS_DOOR_MANAGER_H

#include <irrlicht.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EQT {
namespace Graphics {

// Forward declarations
struct S3DZone;

// Visual representation of a door in the scene
struct DoorVisual {
    irr::scene::ISceneNode* pivotNode = nullptr;      // Pivot point for rotation (at hinge)
    irr::scene::IMeshSceneNode* sceneNode = nullptr;  // Door mesh (offset from pivot)
    uint8_t doorId = 0;
    std::string modelName;

    // EQ coordinates (for interaction checks)
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // Rotation state
    float closedHeading = 0.0f;    // Heading when closed (degrees)
    float openHeading = 0.0f;      // Heading when open (degrees)
    uint16_t size = 100;           // Scale (100 = 1.0)
    uint8_t opentype = 0;          // Door behavior type

    // Animation state
    bool isOpen = false;
    bool isAnimating = false;
    float animProgress = 0.0f;     // 0.0 = closed, 1.0 = open

    // Spinning animation (for opentype 100/105)
    bool isSpinning = false;       // True for spinning objects
    float spinAngle = 0.0f;        // Current spin angle (degrees)

    // Bounding box for interaction
    irr::core::aabbox3df boundingBox;
};

// Manages door rendering and interaction
class DoorManager {
public:
    DoorManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~DoorManager();

    // Set the current zone data (for finding door meshes)
    void setZone(const std::shared_ptr<S3DZone>& zone);

    // Create a door visual from server data
    // Returns true if door was created successfully (or skipped for invisible types)
    bool createDoor(uint8_t doorId, const std::string& name,
                    float x, float y, float z, float heading,
                    uint32_t incline, uint16_t size, uint8_t opentype,
                    bool initiallyOpen);

    // Update door state (open/close animation)
    // userInitiated: true if triggered by user click/keypress, false if from server broadcast
    void setDoorState(uint8_t doorId, bool open, bool userInitiated = false);

    // Animation update (call each frame)
    void update(float deltaTime);

    // Find door at screen position (for click targeting)
    // Returns door_id or 0 if none found
    uint8_t getDoorAtScreenPos(int screenX, int screenY,
                               irr::scene::ICameraSceneNode* camera,
                               irr::scene::ISceneCollisionManager* collisionMgr) const;

    // Find nearest interactable door (for U-key)
    // Returns door_id or 0 if none in range
    uint8_t getNearestDoor(float playerX, float playerY, float playerZ,
                           float playerHeading, float maxDistance = 15.0f) const;

    // Check if a specific door exists
    bool hasDoor(uint8_t doorId) const;

    // Get door info (for debugging)
    const DoorVisual* getDoor(uint8_t doorId) const;

    // Remove all doors (zone change)
    void clearDoors();

    // Get door count
    size_t getDoorCount() const { return doors_.size(); }

    // Set visibility of all door nodes (for profiling)
    void setAllDoorsVisible(bool visible);

    // Get all door scene nodes for collision detection
    std::vector<irr::scene::IMeshSceneNode*> getDoorSceneNodes() const;

private:
    // Find matching mesh in zone objects by name
    irr::scene::IMesh* findDoorMesh(const std::string& doorName) const;

    // Calculate open heading from closed heading + incline
    // Uses default 90-degree rotation for standard doors (opentype 0, 5, 56) when incline=0
    float calculateOpenHeading(float closedHeading, uint32_t incline, uint8_t opentype) const;

    // Create a placeholder mesh for doors without models
    irr::scene::IMesh* createPlaceholderMesh() const;

    std::map<uint8_t, DoorVisual> doors_;
    std::set<uint8_t> invisibleDoors_;  // Track invisible doors to suppress state update warnings
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::shared_ptr<S3DZone> currentZone_;

    // Animation speed (complete animation in ~0.5 seconds)
    static constexpr float ANIM_SPEED = 2.0f;

    // Spinning speed (~180 degrees per 4.25 seconds = 42.35 deg/sec)
    static constexpr float SPIN_SPEED = 42.35f;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DOOR_MANAGER_H
