#include "io/vdb_loader.h"
#include <cstring>
#include <cstdio>

#ifdef BH2_HAS_NANOVDB
#include <nanovdb/NanoVDB.h>
#include <nanovdb/io/IO.h>
#include <nanovdb/tools/GridBuilder.h>
#endif

namespace bh2::io {

#ifdef BH2_HAS_NANOVDB

static NanoVDBGridData convert_grid(const std::string& path, const std::string& grid_name) {
    NanoVDBGridData result{};

    try {
        auto handle = nanovdb::io::readGrid(path, grid_name);
        if (!handle) {
            fprintf(stderr, "Warning: grid '%s' not found in %s\n",
                    grid_name.c_str(), path.c_str());
            return result;
        }

        auto* grid = handle.grid<float>();
        if (!grid) {
            fprintf(stderr, "Warning: grid '%s' is not float type\n", grid_name.c_str());
            return result;
        }

        auto bbox = grid->worldBBox();
        result.bbox_min[0] = static_cast<float>(bbox.min()[0]);
        result.bbox_min[1] = static_cast<float>(bbox.min()[1]);
        result.bbox_min[2] = static_cast<float>(bbox.min()[2]);
        result.bbox_max[0] = static_cast<float>(bbox.max()[0]);
        result.bbox_max[1] = static_cast<float>(bbox.max()[1]);
        result.bbox_max[2] = static_cast<float>(bbox.max()[2]);
        result.voxel_size = static_cast<float>(grid->voxelSize()[0]);

        result.buffer.resize(handle.size());
        memcpy(result.buffer.data(), handle.data(), handle.size());
        result.valid = true;

        printf("Loaded NanoVDB grid '%s': %zu bytes, bbox [%.1f,%.1f,%.1f]-[%.1f,%.1f,%.1f]\n",
               grid_name.c_str(), result.buffer.size(),
               result.bbox_min[0], result.bbox_min[1], result.bbox_min[2],
               result.bbox_max[0], result.bbox_max[1], result.bbox_max[2]);
    } catch (const std::exception& e) {
        fprintf(stderr, "Error loading VDB grid '%s': %s\n", grid_name.c_str(), e.what());
    }

    return result;
}

VDBDiskData load_vdb(const std::string& path,
                     const std::string& density_grid_name,
                     const std::string& temp_grid_name) {
    VDBDiskData data;
    data.density = convert_grid(path, density_grid_name);
    data.temperature = convert_grid(path, temp_grid_name);
    return data;
}

#endif // BH2_HAS_NANOVDB

VDBDiskData create_dummy_vdb() {
    VDBDiskData data;
    data.density.buffer.resize(64, 0);
    data.density.valid = false;
    data.temperature.buffer.resize(64, 0);
    data.temperature.valid = false;
    return data;
}

} // namespace bh2::io
