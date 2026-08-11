#pragma once

#include <irrlicht.h>
#include <memory>
#include <cstdint>

// Forward declarations
namespace EQT {
namespace Graphics {
class RaceModelLoader;
class EquipmentModelLoader;
class EQAnimatedMeshSceneNode;
struct EntityAppearance;
}
}

namespace eqt {
namespace inventory {
class InventoryManager;
}
}

namespace eqt {
namespace ui {

// Renders a 3D character model preview for the inventory popup.
// Uses render-to-texture to display the character with current equipment,
// supporting mouse drag rotation and idle animation.
//
// T60 (restoration): deleted in 0f25cb57 as collateral damage when the old
// WindowManager-based UI was removed; restored to fill the centre of the
// inventory popup T57 reserved for it. Owner is now IrrlichtRenderer
// (constructed lazily, only when the popup first opens), not a
// WindowManager. Bone-attachment logic (attachWeapons/updateWeaponTransforms)
// now consumes the shared implementation in
// client/graphics/eq/bone_attachment.h instead of its own copy — see that
// header's comment and refactor/T60_paperdoll_restore.md's Review.
class CharacterModelView {
public:
    CharacterModelView();
    ~CharacterModelView();

    // Initialize with Irrlicht components and dimensions
    // Must be called before any other methods
    void init(irr::scene::ISceneManager* smgr,
              irr::video::IVideoDriver* driver,
              int width, int height);

    // Set the race model loader for accessing character models
    void setRaceModelLoader(EQT::Graphics::RaceModelLoader* loader);

    // Set the equipment model loader for weapon/shield models
    void setEquipmentModelLoader(EQT::Graphics::EquipmentModelLoader* loader);

    // Set the inventory manager. Retained for interface parity with the
    // rest of the UI wiring; the doll's appearance comes from setCharacter()
    // (fed the player's live EntityAppearance — see T60's Review for why),
    // not reconstructed from inventory item materials.
    void setInventoryManager(eqt::inventory::InventoryManager* invManager);

    // Update character appearance (creates/updates model)
    void setCharacter(uint16_t raceId, uint8_t gender,
                      const EQT::Graphics::EntityAppearance& appearance);

    // Mouse interaction for rotation
    // Coordinates are relative to the model view area
    void onMouseDown(int x, int y);
    void onMouseMove(int x, int y);
    void onMouseUp();

    // Check if currently dragging
    bool isDragging() const { return isDragging_; }

    // Animation update (call each frame with delta time in milliseconds)
    void update(float deltaTimeMs);

    // Render the character to internal texture
    // Call this before drawing the UI
    void renderToTexture();

    // Get the rendered texture for drawing in UI
    irr::video::ITexture* getTexture() const { return renderTarget_; }

    // Force RTT re-render next frame
    void markDirty();

    // Get/set current Y rotation (degrees, for persistence)
    float getRotationY() const { return rotationY_; }
    void setRotationY(float angle);

    // Check if initialized and ready
    bool isReady() const { return initialized_ && renderTarget_ != nullptr; }

    // Get dimensions
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Test/debug accessors
    bool hasCharacterModel() const { return characterNode_ != nullptr; }
    bool hasPrimaryWeapon() const { return primaryWeaponNode_ != nullptr; }
    bool hasSecondaryWeapon() const { return secondaryWeaponNode_ != nullptr; }
    uint16_t getCurrentRaceId() const { return currentRaceId_; }
    uint8_t getCurrentGender() const { return currentGender_; }
    uint32_t getPrimaryWeaponId() const { return currentPrimaryId_; }
    uint32_t getSecondaryWeaponId() const { return currentSecondaryId_; }

    // Check if model has textures applied (at least one material with a texture)
    bool hasTextures() const;

    // Check if model is animating (has animation playing)
    bool isAnimating() const;

    // Get number of materials on the character model
    uint32_t getMaterialCount() const;

private:
    // Setup helpers
    void setupScene();
    void setupCamera();
    void setupLighting();
    void updateCharacterModel();
    void adjustCameraForModel();
    void attachWeapons();
    void updateWeaponTransforms();

    // Irrlicht components (not owned)
    irr::scene::ISceneManager* parentSmgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    // Our own scene manager for isolated rendering
    irr::scene::ISceneManager* modelSmgr_ = nullptr;
    irr::scene::ICameraSceneNode* camera_ = nullptr;

    // Render target texture
    irr::video::ITexture* renderTarget_ = nullptr;

    // Character model node (owned by modelSmgr_)
    EQT::Graphics::EQAnimatedMeshSceneNode* characterNode_ = nullptr;

    // Race model loader (not owned)
    EQT::Graphics::RaceModelLoader* raceModelLoader_ = nullptr;

    // Equipment model loader for weapons (not owned)
    EQT::Graphics::EquipmentModelLoader* equipmentModelLoader_ = nullptr;

    // Inventory manager (not owned; see setInventoryManager() comment)
    eqt::inventory::InventoryManager* inventoryManager_ = nullptr;

    // Weapon nodes (owned by modelSmgr_)
    irr::scene::IMeshSceneNode* primaryWeaponNode_ = nullptr;
    irr::scene::IMeshSceneNode* secondaryWeaponNode_ = nullptr;
    uint32_t currentPrimaryId_ = 0;
    uint32_t currentSecondaryId_ = 0;

    // Cached bone indices and weapon offsets (for efficient updates)
    int primaryBoneIndex_ = -1;
    int secondaryBoneIndex_ = -1;
    float primaryWeaponOffset_ = 0.0f;
    float secondaryWeaponOffset_ = 0.0f;
    bool secondaryIsShield_ = false;

    // Current character state
    uint16_t currentRaceId_ = 0;
    uint8_t currentGender_ = 0;
    bool hasAppearance_ = false;

    // Stored appearance (we need the full definition for storage)
    // Using a separate flag since we can't include entity_renderer.h in header
    uint8_t storedFace_ = 0;
    uint8_t storedHaircolor_ = 0;
    uint8_t storedHairstyle_ = 0;
    uint8_t storedBeardcolor_ = 0;
    uint8_t storedBeard_ = 0;
    uint8_t storedTexture_ = 0;
    uint8_t storedHelm_ = 0;
    uint32_t storedEquipment_[9] = {0};
    uint32_t storedEquipmentTint_[9] = {0};

    // Rotation state
    // T62 (Will, 2026-08-11): default changed from 0.0f (renders in
    // profile, facing right -- see refactor/T62_held_item_alignment.md)
    // to 90.0f, which faces the camera (measured empirically against this
    // view's fixed camera at (0,8,-25) looking at (0,5,0) -- see the
    // plan's Review for the before/after capture). Drag rotation
    // (onMouseMove()) still works from this starting angle; only the
    // initial value changed.
    float rotationY_ = 90.0f;  // Current Y rotation in degrees

    // Zoom state
    float cameraDistance_ = 20.0f;  // Current camera distance
    float minCameraDistance_ = 5.0f;
    float maxCameraDistance_ = 100.0f;
    float modelCenterY_ = 0.0f;  // Cached model center for zoom

    // Mouse drag state
    bool isDragging_ = false;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    float dragStartRotation_ = 0.0f;
    float dragStartDistance_ = 0.0f;

    // Dimensions
    int width_ = 128;
    int height_ = 256;

    // Initialization state
    bool initialized_ = false;

    // RTT throttling - skip re-render when nothing changed
    bool dirty_ = true;                    // Needs re-render
    float animationTimer_ = 0.0f;          // Accumulated time since last animation render
    float animationInterval_ = 200.0f;     // ms between animation RTT updates (~5 FPS)

    // Background color for render target
    irr::video::SColor backgroundColor_{255, 30, 30, 35};
};

} // namespace ui
} // namespace eqt
