// volume.glsl — emissive/absorbing volumetric gas sampled from 3D
// density/temperature textures (baked from NanoVDB grids on the CPU).
//
// Each geodesic integration step is subdivided in pseudo-Cartesian space and
// the radiative transfer equation is integrated with simple exponential
// extinction. Emission color comes from the blackbody LUT at the (Doppler
// shifted) local temperature, using the Keplerian g-factor at the sample's
// cylindrical radius.

#ifndef VOLUME_GLSL
#define VOLUME_GLSL

bool volumeEnabled()
{
    return u.vol.x > 0.5;
}

// Segment [p0, p1] crosses the volume AABB?
bool segmentHitsBox(vec3 p0, vec3 p1)
{
    vec3 lo = u.volBoxMin.xyz, hi = u.volBoxMax.xyz;
    vec3 d = p1 - p0;
    vec3 invD = 1.0 / (d + vec3(1e-20));
    vec3 t0 = (lo - p0) * invD;
    vec3 t1 = (hi - p0) * invD;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float a = max(max(tmin.x, tmin.y), tmin.z);
    float b = min(min(tmax.x, tmax.y), tmax.z);
    return a <= b && b >= 0.0 && a <= 1.0;
}

// March the segment, accumulating emission into `radiance` (premultiplied by
// the running transmittance) and attenuating `transmittance`.
void volumeMarch(vec3 p0, vec3 p1, float E, float L,
                 inout vec3 radiance, inout float transmittance)
{
    if (!segmentHitsBox(p0, p1))
        return;

    int substeps = int(u.vol.w);
    vec3 lo = u.volBoxMin.xyz;
    vec3 invExtent = 1.0 / max(u.volBoxMax.xyz - lo, vec3(1e-6));
    float ds = length(p1 - p0) / float(substeps);
    float a = u.bh.x;

    for (int i = 0; i < substeps; ++i)
    {
        vec3 p = mix(p0, p1, (float(i) + 0.5) / float(substeps));
        vec3 uvw = (p - lo) * invExtent;
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
            continue;

        float dens = texture(volDensityTex, uvw).r * u.vol.y;
        if (dens <= 1e-6)
            continue;

        // Local gas temperature: from the temperature grid when the file has
        // one, otherwise from the thin-disk Shakura-Sunyaev profile at the
        // sample's cylindrical radius (density-only Houdini exports).
        float rc = max(length(p.xy), u.bh.z);
        float T = u.volBoxMin.w > 0.5
                ? diskTemperature(max(rc, u.disk.x * 1.06))
                : texture(volTempTex, uvw).r * u.volBoxMax.w; // kelvin/unit
        vec3 emit = vec3(0.0);
        if (T > 1.0)
        {
            // Doppler/gravitational shift using the Keplerian flow at this
            // cylindrical radius (clamped at the ISCO, where circular orbits
            // become unstable and the approximation breaks down).
            float g = max(diskGFactor(rc, E, L), 0.0);
            emit = blackbodyRGB(g * T) * u.vol.z * dens;
        }
        radiance += transmittance * emit * ds;
        transmittance *= exp(-dens * ds);
        if (transmittance < 1e-3)
            return;
    }
}

#endif // VOLUME_GLSL
