#pragma once
#include <string>
#include <vector>

namespace bh2::io {

struct HDRImage {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<float> pixels; // RGBA float32
};

HDRImage load_hdr(const std::string& path);
HDRImage load_exr(const std::string& path);
HDRImage load_hdr_any(const std::string& path);

} // namespace bh2::io
