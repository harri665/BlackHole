// env.glsl — far-field environment: HDR equirectangular skybox, with a
// procedural starfield fallback when no texture is loaded.

#ifndef ENV_GLSL
#define ENV_GLSL

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// 3D-grid starfield: each cell holds at most one star with hashed
// brightness/temperature.
vec3 proceduralStars(vec3 d)
{
    vec3 col = vec3(0.0);
    for (int layer = 0; layer < 2; ++layer)
    {
        float scale = layer == 0 ? 24.0 : 56.0;
        vec3 p = d * scale;
        vec3 id = floor(p);
        vec3 f = fract(p);
        float h = hash13(id);
        if (h > 0.82)
        {
            vec3 starPos = vec3(hash13(id + 1.7), hash13(id + 9.2), hash13(id + 4.5));
            float dist = length(f - starPos);
            float bright = pow((h - 0.82) / 0.18, 3.0) * 2.5;
            float core = exp(-dist * dist * 220.0);
            // crude stellar color: blue-white to orange
            float tint = hash13(id + 2.3);
            vec3 starCol = mix(vec3(1.0, 0.72, 0.45), vec3(0.62, 0.75, 1.0), tint);
            col += starCol * bright * core;
        }
    }
    return col;
}

// Look up the environment along a (pseudo-Cartesian) escape direction.
vec3 sampleEnvironment(vec3 d)
{
    d = normalize(d);
    if (u.env.x > 0.5)
    {
        float phi = atan(d.y, d.x) + u.env.z;
        float theta = acos(clamp(d.z, -1.0, 1.0));
        vec2 uv = vec2(phi / (2.0 * PI) + 0.5, theta / PI);
        return texture(skyTex, uv).rgb * u.env.y;
    }
    return proceduralStars(d) * u.env.y;
}

#endif // ENV_GLSL
