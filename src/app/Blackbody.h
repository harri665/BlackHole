// Blackbody — CPU-side generation of the Planck-spectrum -> linear-sRGB LUT.
#pragma once

#include <vector>

namespace app {

// RGBA32F texels, log-spaced in temperature over [tMin, tMax] kelvin.
// The scale is absolute relative to a 6504 K emitter (luminance 1), so the
// LUT encodes both chromaticity and the steep radiance growth with T —
// required for the g-factor beaming to come out of a single LUT lookup.
std::vector<float> makeBlackbodyLUT(int size, float tMin, float tMax);

} // namespace app
