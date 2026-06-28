#include "Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace app {

Camera::Frame Camera::computeFrame() const
{
    const float pi = glm::pi<float>();
    float az = glm::radians(azimuthDeg);
    float el = glm::radians(std::clamp(elevationDeg, -89.0f, 89.0f));

    float r = distance;
    float th = pi * 0.5f - el;
    float ph = az;

    // flat-space embedding for orientation only (z up)
    float s = std::sin(th), c = std::cos(th);
    float cp = std::cos(ph), sp = std::sin(ph);
    glm::vec3 pos = r * glm::vec3(s * cp, s * sp, c);

    glm::vec3 fwd = glm::normalize(-pos); // look at the hole
    glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    glm::vec3 right = glm::normalize(glm::cross(fwd, worldUp));
    glm::vec3 up = glm::cross(right, fwd);

    // local spherical triad at the camera
    glm::vec3 rhat(s * cp, s * sp, c);
    glm::vec3 thhat(c * cp, c * sp, -s);
    glm::vec3 phhat(-sp, cp, 0.0f);

    auto toLocal = [&](const glm::vec3& v) {
        return glm::vec3(glm::dot(v, rhat), glm::dot(v, thhat), glm::dot(v, phhat));
    };

    Frame f;
    f.posBL = glm::vec3(r, th, ph);
    f.right = toLocal(right);
    f.up = toLocal(up);
    f.fwd = toLocal(fwd);
    f.tanHalfFov = std::tan(glm::radians(fovYDeg) * 0.5f);
    return f;
}

void Camera::orbit(float dxPixels, float dyPixels)
{
    azimuthDeg += dxPixels * 0.25f;
    elevationDeg = std::clamp(elevationDeg + dyPixels * 0.25f, -89.0f, 89.0f);
}

void Camera::zoom(float scrollSteps, float rMin)
{
    distance *= std::pow(0.9f, scrollSteps);
    distance = std::clamp(distance, rMin, 500.0f);
}

} // namespace app
