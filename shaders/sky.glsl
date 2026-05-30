// Equirectangular HDR skybox sampling for escaped geodesics.

#ifndef SKY_GLSL
#define SKY_GLSL

// Sample the HDR skybox from escaped geodesic direction in BL coordinates.
// theta_inf, phi_inf are the asymptotic direction of the ray on the celestial sphere.
// g_sky is the frequency shift factor for a ray escaping to infinity.
vec3 sample_skybox(sampler2D skybox, float theta_inf, float phi_inf, float g_sky) {
    // Equirectangular mapping: u = phi/(2*pi), v = theta/pi
    // Normalize phi to [0, 2π] before mapping to [0, 1].
    // phi can accumulate beyond 2π (or go negative) after pole reflections.
    const float TWO_PI = 6.28318530717959;
    float u = phi_inf / TWO_PI;
    u = u - floor(u);   // equivalent to fract, handles all ranges
    float v = theta_inf / 3.14159265358979;
    v = clamp(v, 0.0, 1.0);

    vec3 hdr = texture(skybox, vec2(u, v)).rgb;

    // Apply gravitational redshift via g-factor
    // For stars (approximately thermal), intensity scales as g^3, color shifts as T -> T*g
    // For a pre-rendered panorama we approximate: scale intensity by g^3
    // and tint by the ratio of shifted/unshifted blackbody at ~5800K (solar)
    float g3 = g_sky * g_sky * g_sky;
    hdr *= g3;

    return max(hdr, vec3(0.0));
}

// Compute g-factor for a photon escaping to infinity from the FIDO at camera position.
// For a static observer at infinity, p·u = -E, so g = E_camera / E = (p·u)_camera / E
// If camera is a FIDO: g = u_FIDO^t * E - u_FIDO^phi * Lz ... but for the skybox
// the relevant factor is the ratio between what the FIDO sees and what infinity sees.
// Since we normalize E=1 at the camera, the skybox g-factor = 1 / (redshift from camera to inf).
float sky_g_factor(float r_cam, float theta_cam, KerrParams bh) {
    float r2 = r_cam * r_cam;
    float ct = cos(theta_cam);
    float Sigma = r2 + bh.a2 * ct * ct;

    // FIDO lapse function: alpha = sqrt(Delta * Sigma / A)
    float Delta = r2 - 2.0 * bh.M * r_cam + bh.a2;
    float st2 = 1.0 - ct * ct;
    float A = (r2 + bh.a2) * (r2 + bh.a2) - Delta * bh.a2 * st2;

    float alpha = sqrt(max(Delta * Sigma / A, 1e-10));

    // Photon blueshift from infinity to FIDO is 1/alpha, so escaping is alpha
    return alpha;
}

#endif // SKY_GLSL
