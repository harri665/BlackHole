#pragma once
#include <string>
#include <vector>

namespace bh2::io {

void write_exr(const std::string& path, int width, int height,
               const float* rgba_pixels);

} // namespace bh2::io
