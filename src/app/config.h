// Config — command-line options and derived Kerr quantities.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace app {

// Outer event horizon r+ = M + sqrt(M^2 - a^2)   (M = 1)
inline double kerrHorizonRadius(double a)
{
    return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a));
}

// Innermost stable circular orbit (prograde), Bardeen-Press-Teukolsky.
inline double kerrIscoRadius(double a)
{
    double z1 = 1.0 + std::cbrt(1.0 - a * a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    return 3.0 + z2 - std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
}

struct Config
{
    // output
    uint32_t width = 1280;
    uint32_t height = 720;
    bool offline = false;
    int spp = 256;
    std::string outPath = "render.exr";

    // physics
    float spin = 0.9f;       // dimensionless a in [0, 1)
    float rFar = 1000.0f;    // escape radius
    int maxSteps = 600;
    int integrator = 1;      // 0 = RK4, 1 = RKF45
    float hInit = 0.05f;
    float tol = 1e-6f;

    // disk
    float diskOuter = 18.0f; // inner edge defaults to the ISCO
    float diskTmax = 9000.0f;
    float diskExposure = 1.0f;
    float diskOpacity = 0.92f;
    float diskNoise = 0.25f;

    // camera
    float camDist = 28.0f;
    float camAzimuth = 0.0f;          // degrees
    float camElevation = 8.0f;        // degrees above the disk plane
    float fovY = 55.0f;               // degrees

    // environment / volume
    std::string skyPath;
    float skyIntensity = 1.0f;
    std::string vdbPath;     // .nvdb/.vdb file, or a directory holding a sequence
    float volDensityScale = 1.0f;  // extinction multiplier (auto-reduced for
                                   // very dense grids unless set explicitly)
    float volEmissionScale = 1.0f;
    float volTempScale = 0.0f;     // kelvin per temperature-grid unit;
                                   // 0 = auto (normalized grids -> ~6500 K peak)
    float vdbScale = 1.0f;   // renderer units (M) per file world unit
    int vdbRes = 256;        // dense bake resolution (longest axis)
    bool vdbYup = true;      // Houdini exports are Y-up; renderer is Z-up
    int seqStart = 1;        // 1-based index into the sorted sequence
    int seqEnd = 0;          // offline: last frame to render (0 = seqStart only)
    int seqStep = 1;

    // misc
    float exposure = 1.0f;
    bool validation = false;
    int debugView = 0; // 0 none, 1 Carter constant drift, 2 step count
};

inline void printUsage()
{
    std::printf(
        "blackhole4 — Kerr black hole renderer\n"
        "  --width N --height N      output resolution (default 1280x720)\n"
        "  --offline                 headless progressive render to EXR\n"
        "  --spp N                   samples per pixel for offline mode (256)\n"
        "  --out PATH                EXR output path (render.exr)\n"
        "  --spin A                  dimensionless spin in [0, 1) (0.9)\n"
        "  --integrator rk4|rkf45    geodesic integrator (rkf45)\n"
        "  --tol X                   RKF45 tolerance (1e-6)\n"
        "  --h X                     initial/base step size (0.05)\n"
        "  --maxsteps N              integration step limit (600)\n"
        "  --diskout R               disk outer radius in M (18)\n"
        "  --tmax K                  disk peak temperature in K (9000)\n"
        "  --camdist R --camaz D --camel D --fov D   camera\n"
        "  --sky PATH                equirect HDR/EXR skybox\n"
        "  --vdb PATH                volume: .nvdb/.vdb file, or a directory\n"
        "                            containing a numbered sequence\n"
        "  --vdbscale X              renderer units (M) per VDB world unit (1.0)\n"
        "  --vdbres N                dense bake resolution, longest axis (256)\n"
        "  --voldens X --volemis X   volume extinction / emission scale\n"
        "  --vdbtemp K               kelvin per temperature-grid unit (0 = auto)\n"
        "  --vdbyup 0|1              treat VDB files as Y-up (Houdini) (1)\n"
        "  --seqstart N --seqend N --seqstep N   sequence frame range (1-based);\n"
        "                            offline renders the range, one EXR per frame\n"
        "  --exposure X              tonemap exposure (1.0)\n"
        "  --validation              enable Vulkan validation layer\n");
}

inline Config parseArgs(int argc, char** argv)
{
    Config c;
    auto need = [&](int i) {
        if (i + 1 >= argc)
            throw std::runtime_error(std::string("missing value for ") + argv[i]);
        return argv[i + 1];
    };
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--width") c.width = std::stoul(need(i)), ++i;
        else if (a == "--height") c.height = std::stoul(need(i)), ++i;
        else if (a == "--offline") c.offline = true;
        else if (a == "--spp") c.spp = std::stoi(need(i)), ++i;
        else if (a == "--out") c.outPath = need(i), ++i;
        else if (a == "--spin") c.spin = std::stof(need(i)), ++i;
        else if (a == "--integrator")
        {
            std::string v = need(i); ++i;
            c.integrator = (v == "rk4") ? 0 : 1;
        }
        else if (a == "--tol") c.tol = std::stof(need(i)), ++i;
        else if (a == "--h") c.hInit = std::stof(need(i)), ++i;
        else if (a == "--maxsteps") c.maxSteps = std::stoi(need(i)), ++i;
        else if (a == "--diskout") c.diskOuter = std::stof(need(i)), ++i;
        else if (a == "--tmax") c.diskTmax = std::stof(need(i)), ++i;
        else if (a == "--camdist") c.camDist = std::stof(need(i)), ++i;
        else if (a == "--camaz") c.camAzimuth = std::stof(need(i)), ++i;
        else if (a == "--camel") c.camElevation = std::stof(need(i)), ++i;
        else if (a == "--fov") c.fovY = std::stof(need(i)), ++i;
        else if (a == "--sky") c.skyPath = need(i), ++i;
        else if (a == "--vdb") c.vdbPath = need(i), ++i;
        else if (a == "--vdbscale") c.vdbScale = std::stof(need(i)), ++i;
        else if (a == "--vdbres") c.vdbRes = std::stoi(need(i)), ++i;
        else if (a == "--voldens") c.volDensityScale = std::stof(need(i)), ++i;
        else if (a == "--volemis") c.volEmissionScale = std::stof(need(i)), ++i;
        else if (a == "--vdbtemp") c.volTempScale = std::stof(need(i)), ++i;
        else if (a == "--vdbyup") c.vdbYup = std::stoi(need(i)) != 0, ++i;
        else if (a == "--seqstart") c.seqStart = std::stoi(need(i)), ++i;
        else if (a == "--seqend") c.seqEnd = std::stoi(need(i)), ++i;
        else if (a == "--seqstep") c.seqStep = std::max(1, std::stoi(need(i))), ++i;
        else if (a == "--exposure") c.exposure = std::stof(need(i)), ++i;
        else if (a == "--validation") c.validation = true;
        else if (a == "--debug") c.debugView = std::stoi(need(i)), ++i;
        else if (a == "--help" || a == "-h")
        {
            printUsage();
            std::exit(0);
        }
        else
            throw std::runtime_error("unknown option: " + a);
    }
    c.spin = std::clamp(c.spin, 0.0f, 0.998f);
    return c;
}

} // namespace app
