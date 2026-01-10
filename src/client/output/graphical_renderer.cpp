#include "client/output/graphical_renderer.h"

namespace eqt {
namespace output {

void GraphicalRenderer::cycleCameraMode() {
    switch (m_cameraMode) {
        case CameraMode::Free:
            m_cameraMode = CameraMode::Follow;
            break;
        case CameraMode::Follow:
            m_cameraMode = CameraMode::FirstPerson;
            break;
        case CameraMode::FirstPerson:
            m_cameraMode = CameraMode::Free;
            break;
    }
}

std::string GraphicalRenderer::getCameraModeString() const {
    switch (m_cameraMode) {
        case CameraMode::Free:
            return "Free";
        case CameraMode::Follow:
            return "Follow";
        case CameraMode::FirstPerson:
            return "First Person";
        default:
            return "Unknown";
    }
}

} // namespace output
} // namespace eqt
