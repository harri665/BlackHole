#version 460
#extension GL_GOOGLE_include_directive : enable

layout(local_size_x = 16, local_size_y = 16) in;

// Output HDR accumulation image
layout(set = 0, binding = 0, rgba32f) uniform image2D hdr_image;

// Render configuration UBO
layout(set = 0, binding = 1) uniform RenderConfigUBO {
    // Camera
    vec4  cam_pos;          // (r, theta, phi, 0) in BL coords
    vec4  cam_vel;          // camera 4-velocity components (for boosted FIDO)
    float cam_fov;          // full FOV in radians for fisheye

    // Black hole
    float bh_mass;
    float bh_spin;          // a/M dimensionless spin parameter

    // Integration
    float step_size;        // affine parameter step (negative = backward tracing)
    int   max_steps;
    int   integrator_type;  // 0 = RK4 fixed, 1 = RKF45 adaptive
    float tolerance;        // for adaptive integrator

    // Disk
    float disk_r_inner;
    float disk_r_outer;
    float disk_half_angle;
    float disk_density_scale;
    float disk_temp_scale;
    vec4  disk_vdb_offset;  // .xyz = offset, .w = scale
    int   disk_enabled;

    // Rendering
    int   width;
    int   height;
    int   sample_index;     // for progressive accumulation
    int   total_samples;
    float r_far;            // escape radius

    // Padding to 256-byte alignment
    float _pad[3];
} config;

// HDR skybox texture
layout(set = 0, binding = 2) uniform sampler2D skybox;

// NanoVDB density grid (SSBO)
layout(set = 0, binding = 3) buffer DensityGridBuf {
    uint density_grid[];
};

// NanoVDB temperature grid (SSBO)
layout(set = 0, binding = 4) buffer TempGridBuf {
    uint temp_grid[];
};

#include "kerr.glsl"
#include "blackbody.glsl"
#include "sky.glsl"
#include "disk.glsl"

// FIDO tetrad at position (r, theta) in Kerr spacetime.
// Returns the 4 basis vectors e_hat^mu (contravariant) as columns of a 4x4 matrix.
// e_t = FIDO time direction, e_r, e_theta, e_phi = spatial triad.
//
// The FIDO has zero angular momentum (ZAMO), so omega_FIDO = -g_{t phi}/g_{phi phi}.
// The tetrad is:
//   e_t^mu = (1/alpha, 0, 0, omega/alpha)
//   e_r^mu = (0, sqrt(Delta/Sigma), 0, 0)
//   e_theta^mu = (0, 0, 1/sqrt(Sigma), 0)
//   e_phi^mu = (0, 0, 0, 1/sqrt(g_phiphi))
struct FIDOTetrad {
    vec4 e_t;
    vec4 e_r;
    vec4 e_theta;
    vec4 e_phi;
};

FIDOTetrad build_fido_tetrad(float r, float theta, KerrParams bh) {
    float r2 = r * r;
    float sth = sin(theta);
    float cth = cos(theta);
    float s2 = sth * sth;
    float c2 = cth * cth;

    float Sigma = r2 + bh.a2 * c2;
    float Delta = r2 - 2.0 * bh.M * r + bh.a2;
    float A = (r2 + bh.a2) * (r2 + bh.a2) - Delta * bh.a2 * s2;

    // Lapse
    float alpha = sqrt(max(Delta * Sigma / A, 1e-20));

    // Frame-dragging angular velocity
    float omega_fido = 2.0 * bh.M * bh.a * r / A;

    // g_phiphi = A sin^2(theta) / Sigma
    float g_phiphi = A * s2 / Sigma;
    float sqrt_gpp = sqrt(max(g_phiphi, 1e-20));

    FIDOTetrad tet;
    // Indices: (t, r, theta, phi)
    tet.e_t     = vec4(1.0 / alpha, 0.0, 0.0, omega_fido / alpha);
    tet.e_r     = vec4(0.0, sqrt(max(Delta / Sigma, 1e-20)), 0.0, 0.0);
    tet.e_theta = vec4(0.0, 0.0, 1.0 / sqrt(max(Sigma, 1e-20)), 0.0);
    tet.e_phi   = vec4(0.0, 0.0, 0.0, 1.0 / sqrt_gpp);

    return tet;
}

// Build initial photon 4-momentum from a camera-frame direction.
// dir_cam is (dx, dy, dz) in the camera's local orthonormal frame where
// z points along the camera's line of sight.
// Returns covariant 4-momentum components (p_t, p_r, p_theta, p_phi).
vec4 photon_momentum_from_camera_dir(vec3 dir_cam, FIDOTetrad tet, float r, float theta, KerrParams bh) {
    // Photon has E = 1 in the FIDO frame (we can normalize later).
    // In FIDO frame: p_hat = (1, dir_cam.x, dir_cam.y, dir_cam.z) with |dir|=1

    // Camera frame: (right=e_phi, up=-e_theta, forward=e_r)
    // A photon arriving at the camera from the BH direction (small r) was traveling
    // outward (p^r > 0). "Looking toward BH" = forward = +e_r.
    // With backward tracing (dlambda < 0): dr = dlambda * (Delta/Sigma * p_r) < 0 → toward BH. ✓
    //
    // Convention: dir_cam.z = forward (+e_r), dir_cam.x = right (+e_phi), dir_cam.y = up (-e_theta)
    vec4 p_contra = tet.e_t
                  + dir_cam.z * tet.e_r        // forward = toward BH
                  - dir_cam.y * tet.e_theta     // up = toward pole (smaller theta)
                  + dir_cam.x * tet.e_phi;      // right = azimuthal

    // Now lower indices using the metric to get covariant components.
    float r2 = r * r;
    float sth = sin(theta);
    float cth = cos(theta);
    float s2 = sth * sth;
    float Sigma = r2 + bh.a2 * cth * cth;
    float Delta = r2 - 2.0 * bh.M * r + bh.a2;

    float g_tt = -(1.0 - 2.0 * bh.M * r / Sigma);
    float g_rr = Sigma / Delta;
    float g_thth = Sigma;
    float g_phiphi = (r2 + bh.a2 + 2.0 * bh.M * bh.a2 * r * s2 / Sigma) * s2;
    float g_tphi = -2.0 * bh.M * bh.a * r * s2 / Sigma;

    vec4 p_cov;
    p_cov.x = g_tt * p_contra.x + g_tphi * p_contra.w;   // p_t
    p_cov.y = g_rr * p_contra.y;                           // p_r
    p_cov.z = g_thth * p_contra.z;                         // p_theta
    p_cov.w = g_tphi * p_contra.x + g_phiphi * p_contra.w; // p_phi

    return p_cov;
}

// Simple hash for sub-pixel jitter
uint pcg_hash(uint seed_val) {
    uint state = seed_val * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand_float(uint seed) {
    return float(pcg_hash(seed)) / 4294967295.0;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= config.width || pixel.y >= config.height) return;

    // Sub-pixel jitter for progressive accumulation
    uint seed = uint(pixel.x * 1973 + pixel.y * 9277 + config.sample_index * 26699);
    float jx = rand_float(seed) - 0.5;
    float jy = rand_float(seed + 1u) - 0.5;

    // Normalized pixel coordinates [-1, 1]
    float px = (float(pixel.x) + 0.5 + jx) / float(config.width)  * 2.0 - 1.0;
    float py = (float(pixel.y) + 0.5 + jy) / float(config.height) * 2.0 - 1.0;

    // ── Equidistant fisheye projection ──
    float r_pix = sqrt(px * px + py * py);

    vec3 final_color = vec3(0.0);

    if (r_pix <= 1.0) {
        float theta_cam = r_pix * config.cam_fov * 0.5;
        float phi_cam = atan(py, px);

        // Camera-frame direction (z = forward/line-of-sight)
        float st = sin(theta_cam);
        vec3 dir_cam = vec3(st * cos(phi_cam), st * sin(phi_cam), cos(theta_cam));

        // Build FIDO tetrad and initial photon 4-momentum
        KerrParams bh;
        bh.M = config.bh_mass;
        bh.a = config.bh_spin * config.bh_mass;
        bh.a2 = bh.a * bh.a;

        float r_cam = config.cam_pos.x;
        float th_cam = config.cam_pos.y;
        float phi_cam_bl = config.cam_pos.z;

        FIDOTetrad tet = build_fido_tetrad(r_cam, th_cam, bh);
        vec4 p_cov = photon_momentum_from_camera_dir(dir_cam, tet, r_cam, th_cam, bh);

        // Extract conserved quantities
        RayConstants rc;
        rc.E  = -p_cov.x;       // E = -p_t
        rc.Lz =  p_cov.w;       // Lz = p_phi

        // Carter constant Q (Carroll/MTW convention):
        //   Q = p_θ² + cos²θ · (Lz²/sin²θ − a²E²)
        // The θ-potential Θ(θ) = Q − cos²θ·(Lz²/sin²θ − a²E²) must be ≥ 0 in
        // the allowed region; at the camera Θ = p_θ² ≥ 0 by construction.
        float cth = cos(th_cam);
        float sth = sin(th_cam);
        float c2 = cth * cth;
        float s2 = sth * sth;
        rc.Q = p_cov.z * p_cov.z + c2 * (rc.Lz * rc.Lz / (s2 + 1e-30) - bh.a2 * rc.E * rc.E);

        // Initial state
        GeoState s;
        s.r = r_cam;
        s.theta = th_cam;
        s.phi = phi_cam_bl;
        s.p_r = p_cov.y;        // p_r (covariant)
        s.p_theta = p_cov.z;    // p_theta (covariant)

        // Observer energy for g-factor reference
        float u_obs_energy = rc.E; // FIDO normalization: p·u_FIDO = -E (we set E=1 in FIDO frame)
        // Actually u_obs_energy should be (p_mu u^mu) at the camera = -1 by construction
        // since we set the photon to have unit energy in FIDO frame.
        u_obs_energy = 1.0;

        // Disk parameters
        DiskParams dp;
        dp.r_inner = max(config.disk_r_inner, kerr_isco(bh));
        dp.r_outer = config.disk_r_outer;
        dp.half_angle = config.disk_half_angle;
        dp.density_scale = config.disk_density_scale;
        dp.temp_scale = config.disk_temp_scale;
        dp.vdb_offset = config.disk_vdb_offset.xyz;
        dp.vdb_scale = config.disk_vdb_offset.w;

        float r_horizon = kerr_horizon(bh);
        float dlambda = config.step_size; // negative for backward tracing

        DiskResult disk_accum;
        disk_accum.color = vec3(0.0);
        disk_accum.transmittance = 1.0;

        bool escaped = false;
        bool captured = false;

        for (int step = 0; step < config.max_steps; step++) {
            float r_ratio = s.r / r_horizon;
            float adaptive_dl = dlambda;
            if (r_ratio < 5.0) {
                // Near BH: shrink step to resolve strong-field region
                adaptive_dl *= max(0.05, (r_ratio - 1.0) * 0.25);
            } else {
                // Far from BH: spacetime nearly flat, scale step with r
                // so outward rays reach r_far in O(100) steps instead of O(10000)
                adaptive_dl *= clamp(s.r * 0.4, 1.0, 80.0);
            }

            GeoState s_prev = s;

            if (config.integrator_type == 0) {
                s = kerr_rk4(s, rc, bh, adaptive_dl);
            } else {
                s = kerr_rkf45(s, rc, bh, adaptive_dl, config.tolerance);
            }

            const float PI = 3.14159265358979;

            // --- Carter-constant θ turning-point enforcement ---
            // Θ(θ) = Q − cos²θ·(Lz²/sin²θ − a²E²)  (Carroll convention, Q stored above).
            // Must be ≥ 0 in the physically allowed region.  If RK4 drifts past the
            // turning point (Θ < 0), revert to the last good state and flip p_theta.
            // Do NOT add φ+=π in this case — the ray never actually crossed the pole.
            // Only add φ+=π when θ genuinely overshoots 0 or π (Lz≈0 rays that can
            // reach the polar axis), which requires Θ ≥ 0 throughout.
            {
                float cth2 = cos(s.theta) * cos(s.theta);
                float sth2 = max(sin(s.theta) * sin(s.theta), 1e-8);
                float Theta_pot = rc.Q - rc.Lz * rc.Lz * cth2 / sth2
                                + bh.a2 * rc.E * rc.E * cth2;
                if (Theta_pot < 0.0) {
                    s         = s_prev;
                    s.p_theta = -s_prev.p_theta;
                } else {
                    // Genuine coordinate reflection at the poles
                    if (s.theta < 0.0) {
                        s.theta   = -s.theta;
                        s.p_theta = -s.p_theta;
                        s.phi    += PI;
                    }
                    if (s.theta > PI) {
                        s.theta   = 2.0 * PI - s.theta;
                        s.p_theta = -s.p_theta;
                        s.phi    += PI;
                    }
                }
            }
            s.theta = clamp(s.theta, 1e-4, PI - 1e-4);

            // Check termination
            if (s.r <= r_horizon + 0.01) {
                captured = true;
                break;
            }

            if (s.r >= config.r_far) {
                escaped = true;
                break;
            }

            // Check disk intersection
            if (config.disk_enabled != 0 && disk_accum.transmittance > 0.01) {
                if (in_disk_aabb(s.r, s.theta, dp.r_inner, dp.r_outer, dp.half_angle)) {
                    // Single-step disk sample (inline, not sub-marching for performance)
                    float th_eq = abs(s.theta - 3.14159265 * 0.5);
                    float sigma_th = dp.half_angle * 0.3;
                    float density = dp.density_scale
                                  * exp(-th_eq * th_eq / (2.0 * sigma_th * sigma_th))
                                  * pow(dp.r_inner / s.r, 2.0);

                    float r_isco = kerr_isco(bh);
                    float x = s.r / r_isco;
                    float T = dp.temp_scale * pow(x, -0.75) * pow(max(1.0 - 1.0/sqrt(x), 0.0), 0.25);
                    T = max(T, 100.0);

                    float g = compute_g_factor(s, rc, bh, u_obs_energy);
                    vec3 emission = shifted_blackbody_color(T, g);

                    float ds = abs(adaptive_dl) * s.r;
                    float tau = density * ds;
                    float opacity = 1.0 - exp(-tau);

                    disk_accum.color += disk_accum.transmittance * opacity * emission;
                    disk_accum.transmittance *= (1.0 - opacity);
                }
            }
        }

        // Compose final color
        final_color = disk_accum.color;

        // escaped = reached r_far cleanly; also treat step-limit as escaped if r is large
        bool sky_hit = escaped || (!captured && s.r > r_horizon * 3.0);
        if (sky_hit) {
            float g_sky = sky_g_factor(r_cam, th_cam, bh);
            // Normalize accumulated phi to [0, 2π] before skybox lookup.
            float phi_norm = s.phi - 6.28318530 * floor(s.phi / 6.28318530);
            vec3 sky_col = sample_skybox(skybox, s.theta, phi_norm, g_sky);
            final_color += disk_accum.transmittance * sky_col;
        }
        // If captured (r <= horizon): stays black
    }

    // Progressive accumulation
    if (config.sample_index > 0) {
        vec4 prev = imageLoad(hdr_image, pixel);
        float w = float(config.sample_index);
        final_color = (prev.rgb * w + final_color) / (w + 1.0);
    }

    imageStore(hdr_image, pixel, vec4(final_color, 1.0));
}
