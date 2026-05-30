#include "io/exr_writer.h"
#include <tinyexr.h>
// tinyexr is linked as a pre-built library — no TINYEXR_IMPLEMENTATION needed here
#include <stdexcept>
#include <vector>

namespace bh2::io {

void write_exr(const std::string& path, int width, int height,
               const float* rgba_pixels) {
    EXRHeader header;
    InitEXRHeader(&header);

    EXRImage image;
    InitEXRImage(&image);

    image.num_channels = 4;
    image.width = width;
    image.height = height;

    std::vector<float> channels[4];
    for (int c = 0; c < 4; c++) channels[c].resize(width * height);

    for (int i = 0; i < width * height; i++) {
        channels[0][i] = rgba_pixels[4 * i + 0]; // R
        channels[1][i] = rgba_pixels[4 * i + 1]; // G
        channels[2][i] = rgba_pixels[4 * i + 2]; // B
        channels[3][i] = rgba_pixels[4 * i + 3]; // A
    }

    float* channel_ptrs[4] = {
        channels[3].data(), // A (EXR sorts channels alphabetically)
        channels[2].data(), // B
        channels[1].data(), // G
        channels[0].data(), // R
    };
    image.images = reinterpret_cast<unsigned char**>(channel_ptrs);

    header.num_channels = 4;
    std::vector<EXRChannelInfo> channel_infos(4);
    strncpy(channel_infos[0].name, "A", 255);
    strncpy(channel_infos[1].name, "B", 255);
    strncpy(channel_infos[2].name, "G", 255);
    strncpy(channel_infos[3].name, "R", 255);
    header.channels = channel_infos.data();

    std::vector<int> pixel_types(4, TINYEXR_PIXELTYPE_FLOAT);
    std::vector<int> requested_types(4, TINYEXR_PIXELTYPE_FLOAT);
    header.pixel_types = pixel_types.data();
    header.requested_pixel_types = requested_types.data();

    const char* err = nullptr;
    int ret = SaveEXRImageToFile(&image, &header, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string msg = err ? err : "unknown error";
        FreeEXRErrorMessage(err);
        throw std::runtime_error("Failed to write EXR: " + path + " (" + msg + ")");
    }
}

} // namespace bh2::io
