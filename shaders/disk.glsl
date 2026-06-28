// disk.glsl — thin Keplerian accretion disk in the equatorial plane.
//
// Emission model: locally a blackbody at the Shakura-Sunyaev temperature
// profile, observed through the relativistic g-factor
//     g = nu_obs / nu_emit = 1 / (u^t (E - Omega L))
// where (E, L) are the photon's conserved quantities (normalized so the
// camera-frame energy is 1) and u^mu is the circular-orbit 4-velocity of the
// disk fluid. A Doppler-shifted blackbody is exactly a blackbody at g*T, so
// looking the LUT up at g*T captures shift AND relativistic beaming with no
// extra g^3/g^4 factor (the LUT has an absolute radiometric scale).

#ifndef DISK_GLSL
#define DISK_GLSL

// Shakura-Sunyaev radial temperature profile, normalized so its peak equals
// Tmax. f(x) = x^{-3/4} (1 - x^{-1/2})^{1/4} peaks at x = 49/36 with value
// ~0.4880, x = r/rIn.
float diskTemperature(float r)
{
    float rIn = u.disk.x;
    float x = r / rIn;
    if (x <= 1.0) return 0.0;
    float f = pow(x, -0.75) * pow(1.0 - inversesqrt(x), 0.25);
    return u.disk.z * f * (1.0 / 0.4880);
}

// Relativistic g-factor for prograde Keplerian flow at equatorial radius r.
float diskGFactor(float r, float E, float L)
{
    float a = u.bh.x;
    float Om = 1.0 / (pow(r, 1.5) + a); // Keplerian angular velocity

    // covariant metric at the equator (Sigma = r^2)
    float g_tt = -(1.0 - 2.0 / r);
    float g_tp = -2.0 * a / r;
    float g_pp = r * r + a * a + 2.0 * a * a / r;

    float norm = -(g_tt + 2.0 * Om * g_tp + Om * Om * g_pp);
    float ut = inversesqrt(max(norm, 1e-8)); // u^t for circular orbit
    return 1.0 / max(ut * (E - Om * L), 1e-8);
}

// Cheap seamless turbulence built from incommensurate azimuthal harmonics.
// phi enters only through integer multiples, so there is no 2*pi seam, and
// differential rotation (phase advected by the local Omega) shears the
// pattern into trailing spiral streaks over time.
float diskStreaks(float r, float ph)
{
    float a = u.bh.x;
    float Om = 1.0 / (pow(r, 1.5) + a);
    float p = ph - Om * u.disk2.z; // co-rotating azimuth
    float lr = log(r);
    float n = 0.0;
    n += 0.50 * sin( 3.0 * p + 11.0 * lr + 1.3);
    n += 0.35 * sin( 7.0 * p - 17.0 * lr + 4.1);
    n += 0.25 * sin(13.0 * p + 29.0 * lr + 2.2);
    n += 0.20 * sin(23.0 * p - 41.0 * lr + 5.6);
    return n; // roughly [-1.3, 1.3]
}

// Radiance leaving a disk hit at (r, phi) toward the camera.
// alpha is the disk opacity used for compositing.
vec3 shadeDisk(float r, float ph, float E, float L, out float alpha)
{
    float T = diskTemperature(r);
    float g = max(diskGFactor(r, E, L), 0.0);

    float noise = 1.0 + u.disk2.y * diskStreaks(r, ph);
    vec3 c = blackbodyRGB(g * T) * u.disk.w * max(noise, 0.0);

    // soft fade at the rims so the disk edges don't alias
    float rIn = u.disk.x, rOut = u.disk.y;
    float fade = smoothstep(rIn, rIn * 1.05, r) * (1.0 - smoothstep(rOut * 0.85, rOut, r));
    alpha = u.disk2.x * fade;
    return c * fade;
}

#endif // DISK_GLSL
