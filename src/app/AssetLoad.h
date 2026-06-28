// AssetLoad — HDR/EXR equirectangular skybox loading and volume grid baking
// (NanoVDB .nvdb via the bundled headers, OpenVDB .vdb via the optional
// extern/install build — see scripts/build_openvdb.ps1).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct ImageData2D
{
    uint32_t width = 0, height = 0;
    std::vector<float> rgba; // RGBA32F
    bool valid() const { return width > 0; }
};

// Loads .hdr (stb_image) or .exr (tinyexr) into linear RGBA32F.
ImageData2D loadEquirectHDR(const std::string& path);

struct VolumeData
{
    uint32_t dim[3] = {0, 0, 0};
    float boxMin[3] = {0, 0, 0}; // renderer-space AABB (units of M, Z-up)
    float boxMax[3] = {0, 0, 0};
    std::vector<float> density;     // R32F, dim[0]*dim[1]*dim[2]
    std::vector<float> temperature; // R32F, file units; empty if no temp grid
    float maxDensity = 0.0f;        // value diagnostics for auto-scaling
    float maxTemperature = 0.0f;    // (Houdini temps are often normalized 0..1)
    bool valid() const { return dim[0] > 0; }
};

struct VolumeLoadOptions
{
    uint32_t maxDim = 192; // dense bake resolution cap (longest axis)
    float scale = 1.0f;    // renderer units (M) per file world unit
    bool yUp = false;      // file is Y-up (Houdini); renderer is Z-up
};

// Dispatches on extension: .nvdb -> NanoVDB, .vdb -> OpenVDB. Looks for float
// grids named *dens* / *temp* (falls back to the first float grid as density)
// and bakes them into dense arrays. Returns an invalid VolumeData on failure
// (logged) or when built without the corresponding library.
VolumeData loadVolume(const std::string& path, const VolumeLoadOptions& opt = {});

} // namespace app
