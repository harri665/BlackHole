#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace bh2::io {

struct NanoVDBGridData {
    std::vector<uint8_t> buffer;
    float bbox_min[3];
    float bbox_max[3];
    float voxel_size;
    bool valid = false;
};

struct VDBDiskData {
    NanoVDBGridData density;
    NanoVDBGridData temperature;
};

#ifdef BH2_HAS_NANOVDB
VDBDiskData load_vdb(const std::string& path,
                     const std::string& density_grid_name = "density",
                     const std::string& temp_grid_name = "temperature");
#endif

VDBDiskData create_dummy_vdb();

} // namespace bh2::io
