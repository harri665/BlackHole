#include "AssetLoad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include <stb_image.h>

#include <tinyexr.h>

#ifdef BH_WITH_NANOVDB
#include <nanovdb/NanoVDB.h>
#include <nanovdb/GridHandle.h>
#include <nanovdb/io/IO.h>
#endif

#ifdef BH_WITH_OPENVDB
#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <openvdb/tools/Interpolation.h>
#endif

namespace app {

namespace {

bool endsWithI(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && _stricmp(s.c_str() + s.size() - n, suffix) == 0;
}

// Renderer (Z-up, units of M) -> file world space. The yUp swap is its own
// inverse, so the same mapping converts both ways up to the scale factor.
inline void rendererToFile(const float p[3], const VolumeLoadOptions& opt, double out[3])
{
    double x = p[0] / opt.scale, y = p[1] / opt.scale, z = p[2] / opt.scale;
    if (opt.yUp) std::swap(y, z);
    out[0] = x; out[1] = y; out[2] = z;
}

// File world AABB -> renderer AABB + bake dimensions (longest axis = maxDim).
void setupBakeDomain(const double fileMin[3], const double fileMax[3],
                     const VolumeLoadOptions& opt, VolumeData& vol)
{
    double rMin[3] = {fileMin[0], fileMin[1], fileMin[2]};
    double rMax[3] = {fileMax[0], fileMax[1], fileMax[2]};
    if (opt.yUp)
    {
        std::swap(rMin[1], rMin[2]);
        std::swap(rMax[1], rMax[2]);
    }
    double ext[3], maxExt = 1e-6;
    for (int i = 0; i < 3; ++i)
    {
        rMin[i] *= opt.scale;
        rMax[i] *= opt.scale;
        ext[i] = rMax[i] - rMin[i];
        maxExt = std::max(maxExt, ext[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        // floor of 64: disk-shaped grids are far thinner than they are wide,
        // and a proportional bake starves the thin axis, causing slab banding
        vol.dim[i] = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(opt.maxDim * ext[i] / maxExt)),
            std::min(64u, opt.maxDim), opt.maxDim);
        vol.boxMin[i] = static_cast<float>(rMin[i]);
        vol.boxMax[i] = static_cast<float>(rMax[i]);
    }
}

// Parallel bake over z-slices. sampleFn(fileWorldPos3) -> float must be
// callable concurrently from multiple threads.
template <typename SampleFn>
void bakeDense(const VolumeData& vol, const VolumeLoadOptions& opt,
               std::vector<float>& out, const SampleFn& makeSlabSampler)
{
    out.assign(static_cast<size_t>(vol.dim[0]) * vol.dim[1] * vol.dim[2], 0.0f);
    uint32_t threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::future<void>> jobs;
    for (uint32_t t = 0; t < threads; ++t)
    {
        jobs.push_back(std::async(std::launch::async, [&, t] {
            auto sample = makeSlabSampler();
            for (uint32_t k = t; k < vol.dim[2]; k += threads)
                for (uint32_t j = 0; j < vol.dim[1]; ++j)
                    for (uint32_t i = 0; i < vol.dim[0]; ++i)
                    {
                        float p[3] = {
                            vol.boxMin[0] + (vol.boxMax[0] - vol.boxMin[0]) * ((i + 0.5f) / vol.dim[0]),
                            vol.boxMin[1] + (vol.boxMax[1] - vol.boxMin[1]) * ((j + 0.5f) / vol.dim[1]),
                            vol.boxMin[2] + (vol.boxMax[2] - vol.boxMin[2]) * ((k + 0.5f) / vol.dim[2])};
                        double w[3];
                        rendererToFile(p, opt, w);
                        out[(static_cast<size_t>(k) * vol.dim[1] + j) * vol.dim[0] + i] =
                            sample(w);
                    }
        }));
    }
    for (auto& j : jobs)
        j.get();
}

} // namespace

// ------------------------------------------------------------------- skybox

ImageData2D loadEquirectHDR(const std::string& path)
{
    ImageData2D img;
    if (path.empty())
        return img;

    if (endsWithI(path, ".exr"))
    {
        float* rgba = nullptr;
        int w = 0, h = 0;
        const char* err = nullptr;
        if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS)
        {
            std::fprintf(stderr, "[assets] failed to load EXR '%s': %s\n",
                         path.c_str(), err ? err : "unknown");
            FreeEXRErrorMessage(err);
            return img;
        }
        img.width = static_cast<uint32_t>(w);
        img.height = static_cast<uint32_t>(h);
        img.rgba.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
        std::free(rgba);
    }
    else
    {
        int w = 0, h = 0, comp = 0;
        float* data = stbi_loadf(path.c_str(), &w, &h, &comp, 4);
        if (!data)
        {
            std::fprintf(stderr, "[assets] failed to load HDR '%s': %s\n",
                         path.c_str(), stbi_failure_reason());
            return img;
        }
        img.width = static_cast<uint32_t>(w);
        img.height = static_cast<uint32_t>(h);
        img.rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
    }
    std::printf("[assets] skybox %s (%ux%u)\n", path.c_str(), img.width, img.height);
    return img;
}

// ------------------------------------------------------------------- NanoVDB

#ifdef BH_WITH_NANOVDB

static VolumeData loadNanoVDB(const std::string& path, const VolumeLoadOptions& opt)
{
    VolumeData vol;
    try
    {
        auto handles = nanovdb::io::readGrids(path);
        const nanovdb::FloatGrid* density = nullptr;
        const nanovdb::FloatGrid* temperature = nullptr;
        const nanovdb::FloatGrid* firstFloat = nullptr;

        for (auto& h : handles)
            for (uint32_t g = 0; g < h.gridCount(); ++g)
            {
                const auto* grid = h.grid<float>(g);
                if (!grid)
                    continue;
                if (!firstFloat)
                    firstFloat = grid;
                std::string name = grid->gridName();
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name.find("dens") != std::string::npos)
                    density = grid;
                else if (name.find("temp") != std::string::npos)
                    temperature = grid;
            }
        if (!density)
            density = firstFloat;
        if (!density)
        {
            std::fprintf(stderr, "[assets] no float grid found in '%s'\n", path.c_str());
            return vol;
        }

        auto bbox = density->worldBBox();
        double fMin[3] = {bbox.min()[0], bbox.min()[1], bbox.min()[2]};
        double fMax[3] = {bbox.max()[0], bbox.max()[1], bbox.max()[2]};
        setupBakeDomain(fMin, fMax, opt, vol);

        auto makeSampler = [&](const nanovdb::FloatGrid* grid) {
            return [grid]() {
                auto acc = grid->getAccessor();
                return [grid, acc](const double w[3]) mutable -> float {
                    auto idx = grid->worldToIndexF(nanovdb::Vec3f(
                        float(w[0]), float(w[1]), float(w[2])));
                    nanovdb::Coord c(int(std::floor(idx[0] + 0.5f)),
                                     int(std::floor(idx[1] + 0.5f)),
                                     int(std::floor(idx[2] + 0.5f)));
                    return acc.getValue(c);
                };
            };
        };
        bakeDense(vol, opt, vol.density, makeSampler(density));
        if (temperature)
            bakeDense(vol, opt, vol.temperature, makeSampler(temperature));

        for (float d : vol.density)
            vol.maxDensity = std::max(vol.maxDensity, d);
        for (float t : vol.temperature)
            vol.maxTemperature = std::max(vol.maxTemperature, t);
        std::printf("[assets] volume %s baked to %ux%ux%u (density%s)\n",
                    path.c_str(), vol.dim[0], vol.dim[1], vol.dim[2],
                    temperature ? " + temperature" : " only");
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[assets] failed to load NanoVDB '%s': %s\n",
                     path.c_str(), e.what());
        vol = VolumeData{};
    }
    return vol;
}

#endif // BH_WITH_NANOVDB

// ------------------------------------------------------------------- OpenVDB

#ifdef BH_WITH_OPENVDB

static VolumeData loadOpenVDB(const std::string& path, const VolumeLoadOptions& opt)
{
    VolumeData vol;
    try
    {
        static bool initialized = false;
        if (!initialized)
        {
            openvdb::initialize();
            initialized = true;
        }

        openvdb::io::File file(path);
        file.open();
        openvdb::GridPtrVecPtr grids = file.getGrids();
        file.close();

        openvdb::FloatGrid::Ptr density, temperature, firstFloat;
        for (const auto& g : *grids)
        {
            auto f = openvdb::gridPtrCast<openvdb::FloatGrid>(g);
            if (!f)
                continue;
            if (!firstFloat)
                firstFloat = f;
            std::string name = f->getName();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find("dens") != std::string::npos)
                density = f;
            else if (name.find("temp") != std::string::npos)
                temperature = f;
        }
        if (!density)
            density = firstFloat;
        if (!density)
        {
            std::fprintf(stderr, "[assets] no float grid found in '%s'\n", path.c_str());
            return vol;
        }

        // world AABB from the 8 corners of the active index bbox (robust to
        // rotated/scaled transforms)
        openvdb::CoordBBox ib = density->evalActiveVoxelBoundingBox();
        double fMin[3] = {1e30, 1e30, 1e30}, fMax[3] = {-1e30, -1e30, -1e30};
        for (int corner = 0; corner < 8; ++corner)
        {
            openvdb::Vec3d idx(
                (corner & 1) ? ib.max().x() + 1.0 : double(ib.min().x()),
                (corner & 2) ? ib.max().y() + 1.0 : double(ib.min().y()),
                (corner & 4) ? ib.max().z() + 1.0 : double(ib.min().z()));
            openvdb::Vec3d w = density->transform().indexToWorld(idx);
            for (int i = 0; i < 3; ++i)
            {
                fMin[i] = std::min(fMin[i], w[i]);
                fMax[i] = std::max(fMax[i], w[i]);
            }
        }
        setupBakeDomain(fMin, fMax, opt, vol);

        auto makeSampler = [](const openvdb::FloatGrid::Ptr& grid) {
            return [grid]() {
                using Sampler = openvdb::tools::GridSampler<
                    openvdb::FloatGrid::ConstAccessor, openvdb::tools::BoxSampler>;
                auto acc = grid->getConstAccessor();
                return [grid, acc](const double w[3]) mutable -> float {
                    Sampler s(acc, grid->transform());
                    return s.wsSample(openvdb::Vec3R(w[0], w[1], w[2]));
                };
            };
        };
        bakeDense(vol, opt, vol.density, makeSampler(density));
        if (temperature)
            bakeDense(vol, opt, vol.temperature, makeSampler(temperature));

        auto maxOf = [](const std::vector<float>& v) {
            float m = 0;
            for (float x : v) m = std::max(m, x);
            return m;
        };
        vol.maxDensity = maxOf(vol.density);
        vol.maxTemperature = vol.temperature.empty() ? 0.0f : maxOf(vol.temperature);
        std::printf("[assets] volume %s baked to %ux%ux%u (density%s), "
                    "box [%.1f %.1f %.1f]..[%.1f %.1f %.1f] M, "
                    "max dens %.3g, max temp %.3g\n",
                    path.c_str(), vol.dim[0], vol.dim[1], vol.dim[2],
                    temperature ? " + temperature" : " only",
                    vol.boxMin[0], vol.boxMin[1], vol.boxMin[2],
                    vol.boxMax[0], vol.boxMax[1], vol.boxMax[2],
                    vol.maxDensity, vol.maxTemperature);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[assets] failed to load OpenVDB '%s': %s\n",
                     path.c_str(), e.what());
        vol = VolumeData{};
    }
    return vol;
}

#endif // BH_WITH_OPENVDB

// ---------------------------------------------------------------- dispatcher

VolumeData loadVolume(const std::string& path, const VolumeLoadOptions& opt)
{
    if (path.empty())
        return {};

    if (endsWithI(path, ".nvdb"))
    {
#ifdef BH_WITH_NANOVDB
        return loadNanoVDB(path, opt);
#else
        std::fprintf(stderr, "[assets] built without NanoVDB; ignoring '%s'\n", path.c_str());
        return {};
#endif
    }
    if (endsWithI(path, ".vdb"))
    {
#ifdef BH_WITH_OPENVDB
        return loadOpenVDB(path, opt);
#else
        std::fprintf(stderr,
                     "[assets] built without OpenVDB; cannot read '%s'.\n"
                     "         Run scripts\\build_openvdb.ps1 and re-run CMake.\n",
                     path.c_str());
        return {};
#endif
    }
    std::fprintf(stderr, "[assets] unknown volume format: '%s'\n", path.c_str());
    return {};
}

} // namespace app
