// Volumetric accretion disk sampling using NanoVDB.
// Integrates emission + absorption along the geodesic inside the VDB bounding box.

#ifndef DISK_GLSL
#define DISK_GLSL

#include "blackbody.glsl"
#include "kerr.glsl"

// NanoVDB access — PNanoVDB GLSL interface
// The NanoVDB grid is stored as a raw buffer (SSBO).
// We use pnanovdb_readaccessor for fast tree traversal.
// Grid 0 = density, Grid 1 = temperature (both float grids)

struct DiskParams {
    float r_inner;      // inner edge (>= ISCO)
    float r_outer;      // outer edge
    float half_angle;   // angular half-thickness from equatorial plane
    float density_scale; // multiplier for absorption coefficient
    float temp_scale;    // multiplier for emission temperature
    vec3  vdb_offset;    // BL-space offset for VDB origin
    float vdb_scale;     // BL-space scale for VDB coordinates
};

// Convert BL coordinates to VDB index space
vec3 bl_to_vdb_index(float r, float theta, float phi, DiskParams dp) {
    // VDB is in Cartesian coordinates centered on the BH
    float sth = sin(theta);
    float x = r * sth * cos(phi);
    float y = r * sth * sin(phi);
    float z = r * cos(theta);

    // Transform to VDB index space
    vec3 world = vec3(x, y, z) - dp.vdb_offset;
    return world / dp.vdb_scale;
}

// Volumetric ray march through the disk along the geodesic.
// Called when the ray enters the disk AABB; steps along the geodesic
// using the same integrator, sampling the VDB at each step.
//
// Returns accumulated colour (linear sRGB, HDR) and transmittance.
struct DiskResult {
    vec3  color;         // accumulated emission (already shifted by g-factor)
    float transmittance; // remaining transmittance (1 = fully transparent)
};

// Simple stub that works without NanoVDB for initial testing:
// uses an analytic thin-disk density/temperature falloff
DiskResult integrate_disk_analytic(
    GeoState s_entry, RayConstants rc, KerrParams bh, DiskParams dp,
    float dlambda, int max_steps, float u_obs_energy
) {
    DiskResult result;
    result.color = vec3(0.0);
    result.transmittance = 1.0;

    GeoState s = s_entry;
    float r_isco = kerr_isco(bh);

    for (int i = 0; i < max_steps && result.transmittance > 0.01; i++) {
        s = kerr_rk4(s, rc, bh, dlambda);

        float r_horiz = kerr_horizon(bh);
        if (s.r <= r_horiz + 0.01 || s.r > dp.r_outer * 2.0) break;

        if (!in_disk_aabb(s.r, s.theta, dp.r_inner, dp.r_outer, dp.half_angle))
            continue;

        // Analytic density: Gaussian profile in theta, power-law in r
        float th_eq = abs(s.theta - 3.14159265 * 0.5);
        float sigma_th = dp.half_angle * 0.3;
        float density = dp.density_scale * exp(-th_eq * th_eq / (2.0 * sigma_th * sigma_th))
                       * pow(dp.r_inner / s.r, 2.0);

        // Analytic temperature: Novikov-Thorne-like profile
        float x = s.r / r_isco;
        float T = dp.temp_scale * pow(x, -0.75) * pow(max(1.0 - 1.0/sqrt(x), 0.0), 0.25);
        T = max(T, 100.0);

        // Frequency shift
        float g = compute_g_factor(s, rc, bh, u_obs_energy);

        // Emission: shifted blackbody
        vec3 emission = shifted_blackbody_color(T, g);

        // Beer-Lambert absorption
        float ds = abs(dlambda) * s.r; // approximate proper distance
        float tau = density * ds;
        float opacity = 1.0 - exp(-tau);

        result.color += result.transmittance * opacity * emission;
        result.transmittance *= (1.0 - opacity);
    }

    return result;
}

// NanoVDB-based disk integration will be added when PNanoVDB GLSL headers
// are integrated. The density/temperature grids are bound as SSBOs (bindings 3,4)
// in trace.comp.glsl and accessed via pnanovdb_readaccessor_t lookups.
// For now, integrate_disk_analytic() provides the full relativistic pipeline.

#endif // DISK_GLSL
