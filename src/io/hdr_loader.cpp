#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <tinyexr.h>

#include "io/hdr_loader.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace bh2::io {

HDRImage load_hdr(const std::string& path) {
    HDRImage img;
    int n;
    float* data = stbi_loadf(path.c_str(), &img.width, &img.height, &n, 4);
    if (!data) throw std::runtime_error("Failed to load HDR: " + path);

    size_t count = img.width * img.height * 4;
    img.pixels.resize(count);
    memcpy(img.pixels.data(), data, count * sizeof(float));
    stbi_image_free(data);
    return img;
}

HDRImage load_exr(const std::string& path) {
    HDRImage img;
    float* rgba = nullptr;
    const char* err = nullptr;

    int ret = LoadEXR(&rgba, &img.width, &img.height, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string msg = err ? err : "unknown error";
        FreeEXRErrorMessage(err);
        throw std::runtime_error("Failed to load EXR: " + path + " (" + msg + ")");
    }

    size_t count = img.width * img.height * 4;
    img.pixels.resize(count);
    memcpy(img.pixels.data(), rgba, count * sizeof(float));
    free(rgba);
    return img;
}

HDRImage load_hdr_any(const std::string& path) {
    auto ext = path.substr(path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "exr") return load_exr(path);
    return load_hdr(path);
}

} // namespace bh2::io
