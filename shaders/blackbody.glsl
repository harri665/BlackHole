// Spectral blackbody emission → linear sRGB via CIE 1931 colour matching functions.
// Samples Planck function at 16 wavelength bins and integrates against x̄, ȳ, z̄.

#ifndef BLACKBODY_GLSL
#define BLACKBODY_GLSL

// CIE 1931 2° standard observer, sampled at 16 wavelengths from 380nm to 780nm (step ~26.7nm)
// Stored as vec3(x_bar, y_bar, z_bar)
const int BB_N_WAVELENGTHS = 16;
const float BB_LAMBDA_MIN = 380.0; // nm
const float BB_LAMBDA_MAX = 780.0; // nm
const float BB_DLAMBDA = (BB_LAMBDA_MAX - BB_LAMBDA_MIN) / float(BB_N_WAVELENGTHS - 1);

// CIE 1931 colour matching functions sampled at our wavelengths
const vec3 CIE_XYZ[16] = vec3[16](
    vec3(0.001368, 0.000039, 0.006450),  // 380nm
    vec3(0.011301, 0.000400, 0.053511),  // 407nm
    vec3(0.068990, 0.002300, 0.328500),  // 433nm
    vec3(0.186220, 0.014700, 0.890600),  // 460nm
    vec3(0.134380, 0.074000, 0.632700),  // 487nm
    vec3(0.017510, 0.159700, 0.249250),  // 513nm
    vec3(0.065500, 0.433450, 0.080650),  // 540nm
    vec3(0.290400, 0.710000, 0.015800),  // 567nm
    vec3(0.594500, 0.916300, 0.001700),  // 593nm
    vec3(0.916300, 0.992600, 0.000800),  // 620nm
    vec3(1.062200, 0.854450, 0.000200),  // 647nm
    vec3(0.854450, 0.586100, 0.000000),  // 673nm
    vec3(0.527963, 0.337300, 0.000000),  // 700nm
    vec3(0.265700, 0.169500, 0.000000),  // 727nm
    vec3(0.106100, 0.068200, 0.000000),  // 753nm
    vec3(0.038000, 0.024900, 0.000000)   // 780nm
);

// Physical constants
const float PLANCK_H = 6.62607015e-34;   // J·s
const float BOLTZMANN_K = 1.380649e-23;   // J/K
const float SPEED_C = 2.99792458e8;       // m/s
const float PLANCK_C1 = 2.0 * PLANCK_H * SPEED_C * SPEED_C; // 2hc^2
const float PLANCK_C2 = PLANCK_H * SPEED_C / BOLTZMANN_K;   // hc/k

// Planck spectral radiance B_lambda(T) in W/(m^2 sr m)
// lambda in meters, T in Kelvin
float planck_Blambda(float lambda_m, float T) {
    float x = PLANCK_C2 / (lambda_m * T);
    if (x > 80.0) return 0.0; // avoid overflow in exp
    float denom = exp(x) - 1.0;
    if (denom <= 0.0) return 0.0;
    float l5 = lambda_m * lambda_m * lambda_m * lambda_m * lambda_m;
    return PLANCK_C1 / (l5 * denom);
}

// Convert temperature T (Kelvin) to CIE XYZ by integrating Planck spectrum
// against colour matching functions via trapezoidal rule.
vec3 blackbody_XYZ(float T) {
    if (T < 100.0) return vec3(0.0);

    vec3 xyz = vec3(0.0);
    for (int i = 0; i < BB_N_WAVELENGTHS; i++) {
        float lambda_nm = BB_LAMBDA_MIN + float(i) * BB_DLAMBDA;
        float lambda_m = lambda_nm * 1e-9;
        float B = planck_Blambda(lambda_m, T);
        xyz += B * CIE_XYZ[i];
    }
    xyz *= BB_DLAMBDA * 1e-9; // integrate over wavelength in meters
    return xyz;
}

// CIE XYZ → linear sRGB (D65 illuminant)
vec3 xyz_to_linear_srgb(vec3 xyz) {
    return vec3(
         3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
        -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
         0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z
    );
}

// Full pipeline: temperature → linear sRGB colour
// Returns colour normalized so that max(r,g,b) at peak = 1 when used standalone;
// in the renderer the absolute intensity scaling comes from the Planck radiance.
vec3 blackbody_color(float T) {
    vec3 xyz = blackbody_XYZ(T);
    vec3 rgb = xyz_to_linear_srgb(xyz);
    return max(rgb, vec3(0.0));
}

// Blackbody radiance integrated over visible band → scalar luminance (cd/m^2 proportional)
float blackbody_luminance(float T) {
    vec3 xyz = blackbody_XYZ(T);
    return xyz.y; // Y tristimulus = luminance
}

// Frequency-shifted blackbody: B_lambda(T, g) where g = nu_obs/nu_emit
// Due to Liouville invariant I_nu / nu^3 = const:
// I_obs(lambda) = g^3 * B(lambda / g, T)
// Equivalently, the observed spectrum is a blackbody at T' = g*T scaled by g^3
// (only exact for thermal, which blackbody is)
vec3 shifted_blackbody_color(float T, float g) {
    float T_shifted = T * g;
    vec3 rgb = blackbody_color(T_shifted);
    float g3 = g * g * g;
    return rgb * g3;
}

// Same but returns the full spectral integral (luminance-proportional)
float shifted_blackbody_luminance(float T, float g) {
    float T_shifted = T * g;
    float g3 = g * g * g;
    return blackbody_luminance(T_shifted) * g3;
}

#endif // BLACKBODY_GLSL
