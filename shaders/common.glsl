// common.glsl — shared declarations for the geodesic tracer.
// Geometric units: G = c = M = 1. Lengths are in units of the BH mass M.

#ifndef COMMON_GLSL
#define COMMON_GLSL

const float PI      = 3.14159265358979323846;
const float HALF_PI = 1.57079632679489661923;

// ---------------------------------------------------------------- parameters
// Everything is packed into vec4s so the std140 layout is trivially
// identical to the C++ mirror struct (see app/Renderer.h).
layout(std140, set = 0, binding = 1) uniform Params
{
    vec4 camPos;    // r, theta, phi (Boyer-Lindquist), tan(fovY/2)
    vec4 camRight;  // local-frame basis, components along (rhat, thhat, phhat); w = aspect
    vec4 camUp;     // w unused
    vec4 camFwd;    // w unused
    vec4 bh;        // a (spin), rHorizon, rIsco, rFar
    vec4 disk;      // rIn, rOut, Tmax [K], diskExposure
    vec4 disk2;     // diskOpacity, noiseAmp, time, lutTmin [K]
    vec4 integ;     // hInit, tol, maxSteps, integrator (0 = RK4, 1 = RKF45)
    vec4 frame;     // unused, width, height, debugView (0 none, 1 |dQ| Carter drift, 2 step count)
    vec4 env;       // useSkyTex, skyIntensity, skyRotation, lutTmax [K]
    vec4 vol;       // useVolume, densityScale, emissionScale, substepsPerStep
    vec4 volBoxMin; // world-space AABB of the volume grid (xyz); w: 1 = no
                    // temperature grid, use the disk temperature profile
    vec4 volBoxMax; // xyz: AABB max; w: kelvin per temperature-grid unit
} u;

layout(set = 0, binding = 2) uniform sampler2D skyTex;
layout(set = 0, binding = 3) uniform sampler1D blackbodyLUT;
layout(set = 0, binding = 4) uniform sampler3D volDensityTex;
layout(set = 0, binding = 5) uniform sampler3D volTempTex;

// ----------------------------------------------------------------------- rng
// PCG hash; used only for sub-pixel AA jitter.
uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand01(inout uint seed)
{
    seed = pcg(seed);
    return float(seed) * (1.0 / 4294967296.0);
}

// ----------------------------------------------------------------- blackbody
// The LUT stores linear-sRGB radiance of a Planck emitter, log-spaced in T,
// with an absolute (relative to 6504 K) radiometric scale. Because a
// blackbody spectrum shifted by a factor g is exactly a blackbody at g*T,
// looking up at g*T accounts for Doppler/gravitational shift AND beaming.
vec3 blackbodyRGB(float T)
{
    float tMin = u.disk2.w;
    float tMax = u.env.w;
    if (T <= tMin) return vec3(0.0);
    float t = (log(T) - log(tMin)) / (log(tMax) - log(tMin));
    return texture(blackbodyLUT, clamp(t, 0.0, 1.0)).rgb;
}

// Boyer-Lindquist -> pseudo-Cartesian (Kerr-Schild-like radii).
vec3 blToCartesian(float r, float th, float ph)
{
    float a  = u.bh.x;
    float rho = sqrt(r * r + a * a) * sin(th);
    return vec3(rho * cos(ph), rho * sin(ph), r * cos(th));
}

#endif // COMMON_GLSL
