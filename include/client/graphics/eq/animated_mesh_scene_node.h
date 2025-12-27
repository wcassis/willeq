#ifndef EQT_GRAPHICS_ANIMATED_MESH_SCENE_NODE_H
#define EQT_GRAPHICS_ANIMATED_MESH_SCENE_NODE_H

#include <irrlicht.h>
#include <memory>
#include <string>
#include <vector>
#include "skeletal_animator.h"
#include "s3d_loader.h"

namespace EQT {
namespace Graphics {

// Mapping from original vertex index to buffer location
struct VertexMapping {
    irr::u32 bufferIndex;    // Which mesh buffer this vertex is in
    irr::u32 localIndex;     // Index within that buffer
};

// Custom animated mesh that supports EQ skeletal animation
// This mesh stores vertex data that can be transformed by bone matrices
class EQAnimatedMesh : public irr::scene::IAnimatedMesh {
public:
    EQAnimatedMesh();
    virtual ~EQAnimatedMesh();

    // IAnimatedMesh interface
    virtual irr::u32 getFrameCount() const override;
    virtual irr::f32 getAnimationSpeed() const override;
    virtual void setAnimationSpeed(irr::f32 fps) override;
    virtual irr::scene::IMesh* getMesh(irr::s32 frame, irr::s32 detailLevel = 255,
                                        irr::s32 startFrameLoop = -1, irr::s32 endFrameLoop = -1) override;

    // IMesh interface
    virtual irr::u32 getMeshBufferCount() const override;
    virtual irr::scene::IMeshBuffer* getMeshBuffer(irr::u32 nr) const override;
    virtual irr::scene::IMeshBuffer* getMeshBuffer(const irr::video::SMaterial& material) const override;
    virtual const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override;
    virtual void setBoundingBox(const irr::core::aabbox3df& box) override;
    virtual void setMaterialFlag(irr::video::E_MATERIAL_FLAG flag, bool newvalue) override;
    virtual void setHardwareMappingHint(irr::scene::E_HARDWARE_MAPPING newMappingHint,
                                         irr::scene::E_BUFFER_TYPE buffer = irr::scene::EBT_VERTEX_AND_INDEX) override;
    virtual void setDirty(irr::scene::E_BUFFER_TYPE buffer = irr::scene::EBT_VERTEX_AND_INDEX) override;

    // EQ-specific methods
    void setBaseMesh(irr::scene::IMesh* mesh);
    void setSkeleton(std::shared_ptr<CharacterSkeleton> skeleton);
    void setVertexPieces(const std::vector<VertexPiece>& pieces);

    // Set original vertices (in order for bone transforms) and vertex-to-buffer mapping
    void setOriginalVertices(const std::vector<irr::video::S3DVertex>& verts);
    void setVertexMapping(const std::vector<VertexMapping>& mapping);

    // Get the animator for this mesh (shared - use scene node's animator for per-instance animation)
    SkeletalAnimator& getAnimator() { return animator_; }
    const SkeletalAnimator& getAnimator() const { return animator_; }

    // Apply current animation frame to mesh vertices (uses shared animator - deprecated for per-instance)
    void applyAnimation();

    // Accessors for shared data (used by scene nodes to create per-instance copies)
    irr::scene::IMesh* getBaseMesh() const { return baseMesh_; }
    std::shared_ptr<CharacterSkeleton> getSkeleton() const { return animator_.getSkeleton(); }
    const std::vector<VertexPiece>& getVertexPieces() const { return vertexPieces_; }
    const std::vector<irr::video::S3DVertex>& getOriginalVertices() const { return originalVertices_; }
    const std::vector<VertexMapping>& getVertexMapping() const { return vertexMapping_; }

private:
    void copyBaseMesh();
    void updateBoundingBox();

    irr::scene::IMesh* baseMesh_;                    // Original untransformed mesh (multi-buffer, textured)
    irr::scene::SMesh* animatedMesh_;                // Mesh with animated vertices
    std::vector<VertexPiece> vertexPieces_;          // Vertex-to-bone mapping
    std::vector<irr::video::S3DVertex> originalVertices_;  // Original vertices in order
    std::vector<VertexMapping> vertexMapping_;       // Maps original index -> buffer location
    SkeletalAnimator animator_;
    irr::core::aabbox3df boundingBox_;
    mutable int lastAppliedFrame_;
};

// Custom scene node for EQ animated characters
// Each scene node has its OWN animator and animated mesh copy to allow
// independent animation states for entities sharing the same base model.
class EQAnimatedMeshSceneNode : public irr::scene::IAnimatedMeshSceneNode {
public:
    EQAnimatedMeshSceneNode(EQAnimatedMesh* mesh,
                            irr::scene::ISceneNode* parent,
                            irr::scene::ISceneManager* mgr,
                            irr::s32 id = -1,
                            const irr::core::vector3df& position = irr::core::vector3df(0, 0, 0),
                            const irr::core::vector3df& rotation = irr::core::vector3df(0, 0, 0),
                            const irr::core::vector3df& scale = irr::core::vector3df(1, 1, 1));
    virtual ~EQAnimatedMeshSceneNode();

    // ISceneNode interface
    virtual void OnRegisterSceneNode() override;
    virtual void render() override;
    virtual const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override;
    virtual irr::video::SMaterial& getMaterial(irr::u32 i) override;
    virtual irr::u32 getMaterialCount() const override;
    virtual void OnAnimate(irr::u32 timeMs) override;

    // IAnimatedMeshSceneNode interface
    virtual void setCurrentFrame(irr::f32 frame) override;
    virtual bool setFrameLoop(irr::s32 begin, irr::s32 end) override;
    virtual void setAnimationSpeed(irr::f32 framesPerSecond) override;
    virtual irr::f32 getAnimationSpeed() const override;
    virtual irr::f32 getFrameNr() const override;
    virtual irr::s32 getStartFrame() const override;
    virtual irr::s32 getEndFrame() const override;
    virtual void setLoopMode(bool playAnimationLooped) override;
    virtual bool getLoopMode() const override;
    virtual void setAnimationEndCallback(irr::scene::IAnimationEndCallBack* callback = 0) override;
    virtual void setReadOnlyMaterials(bool readonly) override;
    virtual bool isReadOnlyMaterials() const override;
    virtual void setMesh(irr::scene::IAnimatedMesh* mesh) override;
    virtual irr::scene::IAnimatedMesh* getMesh(void) override;
    virtual const irr::scene::SMD3QuaternionTag* getMD3TagTransformation(const irr::core::stringc& tagname) override;
    virtual void setJointMode(irr::scene::E_JOINT_UPDATE_ON_RENDER mode) override;
    virtual void setTransitionTime(irr::f32 Time) override;
    virtual void animateJoints(bool CalculateAbsolutePositions = true) override;
    virtual void setRenderFromIdentity(bool On) override;
    virtual irr::scene::ISceneNode* clone(irr::scene::ISceneNode* newParent = 0,
                                           irr::scene::ISceneManager* newManager = 0) override;
    virtual irr::scene::IShadowVolumeSceneNode* addShadowVolumeSceneNode(
        const irr::scene::IMesh* shadowMesh = 0,
        irr::s32 id = -1,
        bool zfailmethod = true,
        irr::f32 infinity = 1000.0f) override;
    virtual irr::scene::IBoneSceneNode* getJointNode(const irr::c8* jointName) override;
    virtual irr::scene::IBoneSceneNode* getJointNode(irr::u32 jointID) override;
    virtual irr::u32 getJointCount() const override;
    virtual bool setMD2Animation(irr::scene::EMD2_ANIMATION_TYPE anim) override;
    virtual bool setMD2Animation(const irr::c8* animationName) override;

    // EQ-specific methods
    // playThrough: if true, animation must complete before next can start (for jumps, attacks, emotes)
    // When loop=false, animation holds on last frame automatically
    bool playAnimation(const std::string& animCode, bool loop = true, bool playThrough = false);
    void stopAnimation();
    bool hasAnimation(const std::string& animCode) const;
    std::vector<std::string> getAnimationList() const;
    const std::string& getCurrentAnimation() const;

    // Check if a playThrough animation is currently active
    bool isPlayingThrough() const;

    // Get the underlying EQ mesh (shared base mesh data)
    EQAnimatedMesh* getEQMesh() { return eqMesh_; }

    // Get the per-instance animator
    SkeletalAnimator& getAnimator() { return animator_; }
    const SkeletalAnimator& getAnimator() const { return animator_; }

    // Force immediate application of current animation frame to mesh
    // Call this after setToLastFrame() to immediately update mesh vertices
    void forceAnimationUpdate() { applyAnimation(); }

    // Get bone world position by index (returns false if bone doesn't exist)
    // Position is in Irrlicht coordinate system (Y-up)
    bool getBoneWorldPosition(int boneIndex, irr::core::vector3df& outPosition) const;

    // Get bone index by name using skeleton (returns -1 if not found)
    int getBoneIndexByName(const std::string& boneName) const;

    // Find hand bone indices (convenience method for spell effects)
    // Returns -1 if hand bone not found
    int findRightHandBoneIndex() const;
    int findLeftHandBoneIndex() const;

private:
    // Apply animation to the per-instance mesh buffer
    void applyAnimation();
    // Create per-instance animated mesh from base mesh
    void createInstanceMesh();

    EQAnimatedMesh* eqMesh_;                          // Shared base mesh data (skeleton, vertex pieces, original verts)
    irr::scene::ISceneManager* sceneManager_;
    std::vector<irr::video::SMaterial> materials_;

    // Per-instance animation state (NOT shared between entities)
    SkeletalAnimator animator_;                       // This node's own animator
    irr::scene::SMesh* instanceMesh_;                 // This node's own animated vertex buffer
    irr::core::aabbox3df boundingBox_;                // Per-instance bounding box

    irr::u32 lastTimeMs_;
    irr::f32 animationSpeed_;
    irr::s32 startFrame_;
    irr::s32 endFrame_;
    bool looping_;
    bool readOnlyMaterials_;
    irr::scene::IAnimationEndCallBack* animationEndCallback_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ANIMATED_MESH_SCENE_NODE_H
