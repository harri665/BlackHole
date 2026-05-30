#version 460

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform sampler2D hdr_input;

layout(push_constant) uniform TonemapParams {
    float exposure;
    float gamma;
} params;

// ACES filmic tone mapping (fitted by Krzysztof Narkowicz)
vec3 aces_tonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(hdr_input, uv).rgb;

    // Exposure
    hdr *= params.exposure;

    // ACES tonemap
    vec3 ldr = aces_tonemap(hdr);

    // Gamma correction (linear sRGB → sRGB)
    ldr = pow(ldr, vec3(1.0 / params.gamma));

    frag_color = vec4(ldr, 1.0);
}
