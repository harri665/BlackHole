#pragma once
#include <string>
#include <cmath>

namespace bh2 {

struct RenderConfig {
    // Camera (Boyer-Lindquist coordinates)
    float cam_r = 30.0f;
    float cam_theta = 1.4f;       // ~80 degrees from pole (near equatorial)
    float cam_phi = 0.0f;
    float cam_fov = 3.14159265f;  // 180 degrees for full fisheye

    // Black hole
    float bh_mass = 1.0f;
    float bh_spin = 0.998f;       // near-extremal Kerr, dimensionless a/M

    // Integrator
    float step_size = -0.05f;     // negative = backward ray tracing
    int   max_steps = 5000;
    int   integrator_type = 0;    // 0=RK4, 1=RKF45
    float tolerance = 1e-6f;

    // Disk
    float disk_r_inner = 0.0f;    // 0 = auto (ISCO)
    float disk_r_outer = 30.0f;
    float disk_half_angle = 0.15f; // radians
    float disk_density_scale = 1.0f;
    float disk_temp_scale = 5000.0f; // base temperature in K
    bool  disk_enabled = true;

    // VDB
    float vdb_offset[3] = {0, 0, 0};
    float vdb_scale = 1.0f;
    std::string vdb_path;

    // Rendering
    int   width = 1024;
    int   height = 1024;
    int   samples = 1;            // samples per pixel (1 for preview, more for offline)
    float r_far = 500.0f;

    // Display
    float exposure = 1.0f;
    float gamma = 2.2f;

    // Skybox
    std::string skybox_path;

    // Output
    std::string output_path = "output.exr";
    bool offline_mode = false;
    int  offline_width = 4096;
    int  offline_height = 4096;
    int  offline_samples = 64;
};

// GPU-side UBO layout matching trace.comp.glsl
struct alignas(16) RenderConfigGPU {
    float cam_pos[4];
    float cam_vel[4];
    float cam_fov;

    float bh_mass;
    float bh_spin;

    float step_size;
    int   max_steps;
    int   integrator_type;
    float tolerance;

    float disk_r_inner;
    float disk_r_outer;
    float disk_half_angle;
    float disk_density_scale;
    float disk_temp_scale;
    float disk_vdb_offset[4]; // xyz + scale in w
    int   disk_enabled;

    int   width;
    int   height;
    int   sample_index;
    int   total_samples;
    float r_far;

    float _pad[3];
};

RenderConfigGPU to_gpu_config(const RenderConfig& cfg, int sample_index);

} // namespace bh2
