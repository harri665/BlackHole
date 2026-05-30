#include "app/config.h"

namespace bh2 {

RenderConfigGPU to_gpu_config(const RenderConfig& cfg, int sample_index) {
    RenderConfigGPU gpu{};
    gpu.cam_pos[0] = cfg.cam_r;
    gpu.cam_pos[1] = cfg.cam_theta;
    gpu.cam_pos[2] = cfg.cam_phi;
    gpu.cam_pos[3] = 0.0f;
    gpu.cam_vel[0] = 0.0f;
    gpu.cam_vel[1] = 0.0f;
    gpu.cam_vel[2] = 0.0f;
    gpu.cam_vel[3] = 0.0f;
    gpu.cam_fov = cfg.cam_fov;

    gpu.bh_mass = cfg.bh_mass;
    gpu.bh_spin = cfg.bh_spin;

    gpu.step_size = cfg.step_size;
    gpu.max_steps = cfg.max_steps;
    gpu.integrator_type = cfg.integrator_type;
    gpu.tolerance = cfg.tolerance;

    gpu.disk_r_inner = cfg.disk_r_inner;
    gpu.disk_r_outer = cfg.disk_r_outer;
    gpu.disk_half_angle = cfg.disk_half_angle;
    gpu.disk_density_scale = cfg.disk_density_scale;
    gpu.disk_temp_scale = cfg.disk_temp_scale;
    gpu.disk_vdb_offset[0] = cfg.vdb_offset[0];
    gpu.disk_vdb_offset[1] = cfg.vdb_offset[1];
    gpu.disk_vdb_offset[2] = cfg.vdb_offset[2];
    gpu.disk_vdb_offset[3] = cfg.vdb_scale;
    gpu.disk_enabled = cfg.disk_enabled ? 1 : 0;

    int w = cfg.offline_mode ? cfg.offline_width : cfg.width;
    int h = cfg.offline_mode ? cfg.offline_height : cfg.height;
    gpu.width = w;
    gpu.height = h;
    gpu.sample_index = sample_index;
    gpu.total_samples = cfg.offline_mode ? cfg.offline_samples : cfg.samples;
    gpu.r_far = cfg.r_far;

    return gpu;
}

} // namespace bh2
