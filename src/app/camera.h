#pragma once
#include "app/config.h"

namespace bh2 {

class Camera {
public:
    void update_from_config(RenderConfig& cfg);

    void orbit(float d_theta, float d_phi);
    void zoom(float dr);
    void set_position(float r, float theta, float phi);

    float r() const { return r_; }
    float theta() const { return theta_; }
    float phi() const { return phi_; }

private:
    float r_ = 30.0f;
    float theta_ = 1.4f;
    float phi_ = 0.0f;
};

} // namespace bh2
