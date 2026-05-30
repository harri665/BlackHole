#include "app/camera.h"
#include <algorithm>
#include <cmath>

namespace bh2 {

void Camera::update_from_config(RenderConfig& cfg) {
    r_ = cfg.cam_r;
    theta_ = cfg.cam_theta;
    phi_ = cfg.cam_phi;
}

void Camera::orbit(float d_theta, float d_phi) {
    theta_ += d_theta;
    phi_ += d_phi;
    theta_ = std::clamp(theta_, 0.05f, 3.09f);
    phi_ = fmod(phi_, 2.0f * 3.14159265f);
    if (phi_ < 0) phi_ += 2.0f * 3.14159265f;
}

void Camera::zoom(float dr) {
    r_ += dr;
    r_ = std::max(r_, 3.0f);
}

void Camera::set_position(float r, float theta, float phi) {
    r_ = r;
    theta_ = theta;
    phi_ = phi;
}

} // namespace bh2
