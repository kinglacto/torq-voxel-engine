#include "camera.h"

Camera::Camera(glm::vec3 position)
    : zoom(45.0f), cameraPos(position), cameraFront(glm::vec3(0.0f, 0.0f, -1.0f)),
    cameraUp{glm::vec3(0.0f, 1.0f, 0.0f)}, cameraRight{glm::vec3(1.0f, 0.0f, 0.0f)}, 
    worldUp{glm::vec3(0.0f, 1.0f, 0.0f)}, yaw{-90.0f}, pitch{0.0f}, sensitivity{0.12f}, 
    speed{90.0f}, znear{0.1f}, zfar{4000.0f}, deltaTime{0} {
    updateCameraVectors();
}

void Camera::updateCameraDirection(double dx, double dy) {
    yaw += sensitivity * dx;
    pitch += sensitivity * dy;

    if (pitch > 89.0f) {
        pitch = 89.0f;
    }

    else if (pitch < -89.0f) {
        pitch = -89.0f;
    }


    updateCameraVectors();
}
void Camera::updateCameraPos(cameraDirection dir, float dt) {
    float dist = speed * dt;
    switch (dir) {
        case cameraDirection::FORWARD: {
            cameraPos += cameraFront * dist;
            break;
        }

        case cameraDirection::BACKWARD: {
            cameraPos -= cameraFront * dist;
            break;
        }

        case cameraDirection::RIGHT: {
            cameraPos += cameraRight * dist;
            break;
        }

        case cameraDirection::LEFT: {
            cameraPos -= cameraRight * dist;
            break;
        }

        case cameraDirection::UP: {
            cameraPos += cameraUp * dist;
            break;
        }

        case cameraDirection::DOWN: {
            cameraPos -= cameraUp * dist;
            break;
        }

        case cameraDirection::NONE: {
            break;
        }
    }
}

void Camera::updateCameraZoom(const double dy) {
    zoom = glm::clamp(zoom - static_cast<float>(dy), 30.0f, 60.0f);
}


glm::mat4 Camera::getViewMatrix() {
    updateCameraVectors();
    return glm::lookAt(cameraPos, cameraPos + cameraFront, worldUp);
}

void Camera::updateCameraVectors() {
    glm::vec3 direction;
    direction.x = static_cast<float>(cos(glm::radians(yaw)) * cos(glm::radians(pitch)));
    direction.y = static_cast<float>(sin(glm::radians(pitch)));
    direction.z = static_cast<float>(sin(glm::radians(yaw)) * cos(glm::radians(pitch)));
    cameraFront = glm::normalize(direction);

    cameraRight = glm::normalize(glm::cross(cameraFront, worldUp));
    cameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));
}


float Camera::getZoom() const {
    return zoom;
}
