// post.frag — resolve the accumulation buffer: exposure, ACES tonemap
// (Stephen Hill's RRT+ODT fit), sRGB encode.
#version 460

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accumTex;

layout(push_constant) uniform Push
{
    float exposure;
    float invSampleCount;
    int tonemap; // 0 = clamp only, 1 = ACES
    int pad;
} pc;

// sRGB-linear -> ACEScg-ish RRT input space
const mat3 ACESInput = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);

const mat3 ACESOutput = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v)
{
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 acesFitted(vec3 c)
{
    c = ACESInput * c;
    c = RRTAndODTFit(c);
    return clamp(ACESOutput * c, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c)
{
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, c * 12.92, lo);
}

void main()
{
    vec3 c = texture(accumTex, vUV).rgb * pc.invSampleCount * pc.exposure;
    c = pc.tonemap == 1 ? acesFitted(c) : clamp(c, 0.0, 1.0);
    outColor = vec4(linearToSrgb(c), 1.0);
}
