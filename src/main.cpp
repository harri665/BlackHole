#include "app/app.h"
#include "app/offline.h"
#include "app/config.h"
#include <cstdio>
#include <cstring>
#include <string>

static void print_usage() {
    printf("BlackHole2 — Physically-accurate Kerr black hole renderer\n\n");
    printf("Usage: blackhole2 [options]\n\n");
    printf("Modes:\n");
    printf("  --preview              Interactive preview (default)\n");
    printf("  --offline              Offline 4K fisheye render to EXR\n\n");
    printf("Black hole:\n");
    printf("  --spin <a/M>           Dimensionless spin parameter [0, 0.998] (default: 0.998)\n");
    printf("  --mass <M>             Black hole mass in geometric units (default: 1.0)\n\n");
    printf("Camera:\n");
    printf("  --cam-r <r>            Camera radial distance (default: 30)\n");
    printf("  --cam-theta <deg>      Camera polar angle in degrees (default: 80)\n");
    printf("  --cam-phi <deg>        Camera azimuthal angle in degrees (default: 0)\n");
    printf("  --fov <deg>            Full fisheye FOV in degrees (default: 180)\n\n");
    printf("Disk:\n");
    printf("  --vdb <path>           Path to Houdini .vdb file (density + temperature grids)\n");
    printf("  --disk-temp <K>        Analytic disk base temperature (default: 5000)\n");
    printf("  --no-disk              Disable accretion disk\n\n");
    printf("Rendering:\n");
    printf("  --skybox <path>        HDR/EXR panorama for background\n");
    printf("  --width <px>           Preview width (default: 1024)\n");
    printf("  --height <px>          Preview height (default: 1024)\n");
    printf("  --steps <n>            Max integration steps (default: 5000)\n");
    printf("  --step-size <s>        Integration step size (default: 0.05)\n");
    printf("  --adaptive             Use adaptive RKF45 integrator\n");
    printf("  --tolerance <tol>      Adaptive integrator tolerance (default: 1e-6)\n\n");
    printf("Offline:\n");
    printf("  --out-width <px>       Output width (default: 4096)\n");
    printf("  --out-height <px>      Output height (default: 4096)\n");
    printf("  --samples <n>          Samples per pixel (default: 64)\n");
    printf("  --output <path>        Output EXR path (default: output.exr)\n\n");
    printf("Display:\n");
    printf("  --exposure <e>         Tonemap exposure (default: 1.0)\n");
    printf("  --gamma <g>            Gamma correction (default: 2.2)\n");
}

static float deg_to_rad(float deg) { return deg * 3.14159265f / 180.0f; }

int main(int argc, char* argv[]) {
    bh2::RenderConfig cfg;

    for (int i = 1; i < argc; i++) {
        auto arg = [&](const char* name) { return strcmp(argv[i], name) == 0; };
        auto next = [&]() -> const char* {
            if (i + 1 < argc) return argv[++i];
            return nullptr;
        };

        if (arg("--help") || arg("-h")) { print_usage(); return 0; }
        else if (arg("--offline"))       cfg.offline_mode = true;
        else if (arg("--preview"))       cfg.offline_mode = false;
        else if (arg("--spin"))          cfg.bh_spin = std::stof(next());
        else if (arg("--mass"))          cfg.bh_mass = std::stof(next());
        else if (arg("--cam-r"))         cfg.cam_r = std::stof(next());
        else if (arg("--cam-theta"))     cfg.cam_theta = deg_to_rad(std::stof(next()));
        else if (arg("--cam-phi"))       cfg.cam_phi = deg_to_rad(std::stof(next()));
        else if (arg("--fov"))           cfg.cam_fov = deg_to_rad(std::stof(next()));
        else if (arg("--vdb"))           cfg.vdb_path = next();
        else if (arg("--disk-temp"))     cfg.disk_temp_scale = std::stof(next());
        else if (arg("--no-disk"))       cfg.disk_enabled = false;
        else if (arg("--skybox"))        cfg.skybox_path = next();
        else if (arg("--width"))         cfg.width = std::stoi(next());
        else if (arg("--height"))        cfg.height = std::stoi(next());
        else if (arg("--steps"))         cfg.max_steps = std::stoi(next());
        else if (arg("--step-size"))     cfg.step_size = -std::stof(next());
        else if (arg("--adaptive"))      cfg.integrator_type = 1;
        else if (arg("--tolerance"))     cfg.tolerance = std::stof(next());
        else if (arg("--out-width"))     cfg.offline_width = std::stoi(next());
        else if (arg("--out-height"))    cfg.offline_height = std::stoi(next());
        else if (arg("--samples"))       cfg.offline_samples = std::stoi(next());
        else if (arg("--output"))        cfg.output_path = next();
        else if (arg("--exposure"))      cfg.exposure = std::stof(next());
        else if (arg("--gamma"))         cfg.gamma = std::stof(next());
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    printf("BlackHole2 — Kerr black hole renderer\n");
    printf("  Spin: a/M = %.4f\n", cfg.bh_spin);
    printf("  Camera: r=%.1f, theta=%.1f°, phi=%.1f°\n",
           cfg.cam_r, cfg.cam_theta * 180.0f / 3.14159265f,
           cfg.cam_phi * 180.0f / 3.14159265f);
    printf("  Mode: %s\n", cfg.offline_mode ? "offline" : "preview");

    if (cfg.offline_mode) {
        bh2::run_offline(cfg);
    } else {
        bh2::App app;
        app.init(cfg);
        app.run();
        app.destroy();
    }

    return 0;
}
