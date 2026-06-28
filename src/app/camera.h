// Camera — orbit camera around the hole. Produces the Boyer-Lindquist
// position and the orientation basis used to build the photon's initial
// direction in the local FIDO (ZAMO) frame.
//
// The basis vectors are expressed in components along the local orthonormal
// spatial triad (rhat, thetahat, phihat) of the ZAMO tetrad; the shader
// (kerr.glsl: cameraRay) turns a direction in that frame into the photon's
// conserved quantities. For computing the *orientation* (look-at) we treat
// the BL coordinates as flat spherical coordinates — this only fixes where
// the camera points, not any physics.
#pragma once

#include <glm/glm.hpp>

namespace app {

class Camera
{
public:
    float distance = 28.0f;       // BL radius r
    float azimuthDeg = 0.0f;
    float elevationDeg = 8.0f;    // above equatorial plane
    float fovYDeg = 55.0f;

    struct Frame
    {
        glm::vec3 posBL;    // r, theta, phi
        glm::vec3 right;    // components along (rhat, thhat, phhat)
        glm::vec3 up;
        glm::vec3 fwd;
        float tanHalfFov;
    };

    Frame computeFrame() const;

    void orbit(float dxPixels, float dyPixels);
    void zoom(float scrollSteps, float rMin);
};

} // namespace app
