#ifndef EQT_GRAPHICS_PLACEABLE_H
#define EQT_GRAPHICS_PLACEABLE_H

#include <string>
#include <vector>
#include <memory>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace EQT {
namespace Graphics {

// Represents a placeable object instance with position, rotation, and scale
class Placeable {
public:
    Placeable() = default;
    ~Placeable() = default;

    // Position
    void setLocation(float nx, float ny, float nz) {
        x_ = nx; y_ = ny; z_ = nz;
    }
    float getX() const { return x_; }
    float getY() const { return y_; }
    float getZ() const { return z_; }

    // Rotation (stored in degrees)
    void setRotation(float rx, float ry, float rz) {
        rotateX_ = rx; rotateY_ = ry; rotateZ_ = rz;
    }
    float getRotateX() const { return rotateX_; }
    float getRotateY() const { return rotateY_; }
    float getRotateZ() const { return rotateZ_; }

    // Get rotation in radians
    float getRotateXRad() const { return rotateX_ * M_PI / 180.0f; }
    float getRotateYRad() const { return rotateY_ * M_PI / 180.0f; }
    float getRotateZRad() const { return rotateZ_ * M_PI / 180.0f; }

    // Scale
    void setScale(float sx, float sy, float sz) {
        scaleX_ = sx; scaleY_ = sy; scaleZ_ = sz;
    }
    float getScaleX() const { return scaleX_; }
    float getScaleY() const { return scaleY_; }
    float getScaleZ() const { return scaleZ_; }

    // Object model name (reference name in WLD)
    void setName(const std::string& name) { modelName_ = name; }
    const std::string& getName() const { return modelName_; }

    // Actual model file name (for external loading)
    void setFileName(const std::string& name) { modelFile_ = name; }
    const std::string& getFileName() const { return modelFile_; }

private:
    float x_ = 0.0f, y_ = 0.0f, z_ = 0.0f;                    // Position
    float rotateX_ = 0.0f, rotateY_ = 0.0f, rotateZ_ = 0.0f;  // Rotation in degrees
    float scaleX_ = 1.0f, scaleY_ = 1.0f, scaleZ_ = 1.0f;     // Scale factors
    std::string modelName_;     // Object definition name (e.g., "LAMP_GUKC")
    std::string modelFile_;     // Geometry model file reference
};

// Holds a placeable instance paired with its geometry
struct PlaceableInstance {
    std::shared_ptr<Placeable> placeable;
    uint32_t geometryIndex;  // Index into object geometry list
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PLACEABLE_H
