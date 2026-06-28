// integrators.glsl — fixed-step RK4 and adaptive RKF45 (Cash-Karp) steppers
// for the 5-component geodesic state. Both call geodesicRHS() from kerr.glsl.

#ifndef INTEGRATORS_GLSL
#define INTEGRATORS_GLSL

GeoState gsAdd(GeoState a, GeoState b, float h)
{
    GeoState o;
    o.r   = a.r   + h * b.r;
    o.th  = a.th  + h * b.th;
    o.ph  = a.ph  + h * b.ph;
    o.pr  = a.pr  + h * b.pr;
    o.pth = a.pth + h * b.pth;
    return o;
}

// ------------------------------------------------------------- classical RK4
void rk4Step(inout GeoState y, float h, float E, float L, float a)
{
    GeoState k1, k2, k3, k4;
    geodesicRHS(y, E, L, a, k1);
    geodesicRHS(gsAdd(y, k1, 0.5 * h), E, L, a, k2);
    geodesicRHS(gsAdd(y, k2, 0.5 * h), E, L, a, k3);
    geodesicRHS(gsAdd(y, k3, h), E, L, a, k4);

    float h6 = h / 6.0;
    y.r   += h6 * (k1.r   + 2.0 * k2.r   + 2.0 * k3.r   + k4.r);
    y.th  += h6 * (k1.th  + 2.0 * k2.th  + 2.0 * k3.th  + k4.th);
    y.ph  += h6 * (k1.ph  + 2.0 * k2.ph  + 2.0 * k3.ph  + k4.ph);
    y.pr  += h6 * (k1.pr  + 2.0 * k2.pr  + 2.0 * k3.pr  + k4.pr);
    y.pth += h6 * (k1.pth + 2.0 * k2.pth + 2.0 * k3.pth + k4.pth);
}

// --------------------------------------------- RKF45 with Cash-Karp tableau
// Embedded 5(4) pair: advances with the 5th-order solution, uses the
// difference to the 4th-order one for step-size control.
// Returns true if the step was accepted; h is updated in place either way.
bool rkf45Step(inout GeoState y, inout float h, float E, float L, float a,
               float tol, float hMin, float hMax)
{
    const float a21 = 1.0 / 5.0;
    const float a31 = 3.0 / 40.0,        a32 = 9.0 / 40.0;
    const float a41 = 3.0 / 10.0,        a42 = -9.0 / 10.0,    a43 = 6.0 / 5.0;
    const float a51 = -11.0 / 54.0,      a52 = 5.0 / 2.0,      a53 = -70.0 / 27.0,
                a54 = 35.0 / 27.0;
    const float a61 = 1631.0 / 55296.0,  a62 = 175.0 / 512.0,  a63 = 575.0 / 13824.0,
                a64 = 44275.0 / 110592.0, a65 = 253.0 / 4096.0;
    // 5th-order weights
    const float b1 = 37.0 / 378.0,   b3 = 250.0 / 621.0,
                b4 = 125.0 / 594.0,  b6 = 512.0 / 1771.0;
    // (5th - 4th) weights for the error estimate
    const float e1 = b1 - 2825.0 / 27648.0;
    const float e3 = b3 - 18575.0 / 48384.0;
    const float e4 = b4 - 13525.0 / 55296.0;
    const float e5 =    - 277.0 / 14336.0;
    const float e6 = b6 - 1.0 / 4.0;

    GeoState k1, k2, k3, k4, k5, k6, t;
    geodesicRHS(y, E, L, a, k1);

    t = gsAdd(y, k1, a21 * h);
    geodesicRHS(t, E, L, a, k2);

    t = gsAdd(gsAdd(y, k1, a31 * h), k2, a32 * h);
    geodesicRHS(t, E, L, a, k3);

    t = gsAdd(gsAdd(gsAdd(y, k1, a41 * h), k2, a42 * h), k3, a43 * h);
    geodesicRHS(t, E, L, a, k4);

    t = gsAdd(gsAdd(gsAdd(gsAdd(y, k1, a51 * h), k2, a52 * h), k3, a53 * h), k4, a54 * h);
    geodesicRHS(t, E, L, a, k5);

    t = gsAdd(gsAdd(gsAdd(gsAdd(gsAdd(y, k1, a61 * h), k2, a62 * h), k3, a63 * h),
              k4, a64 * h), k5, a65 * h);
    geodesicRHS(t, E, L, a, k6);

    // 5th-order candidate
    GeoState y5;
    y5.r   = y.r   + h * (b1 * k1.r   + b3 * k3.r   + b4 * k4.r   + b6 * k6.r);
    y5.th  = y.th  + h * (b1 * k1.th  + b3 * k3.th  + b4 * k4.th  + b6 * k6.th);
    y5.ph  = y.ph  + h * (b1 * k1.ph  + b3 * k3.ph  + b4 * k4.ph  + b6 * k6.ph);
    y5.pr  = y.pr  + h * (b1 * k1.pr  + b3 * k3.pr  + b4 * k4.pr  + b6 * k6.pr);
    y5.pth = y.pth + h * (b1 * k1.pth + b3 * k3.pth + b4 * k4.pth + b6 * k6.pth);

    // error estimate, normalized per-component by atol + rtol*|y|
    float err = 0.0;
    {
        float e, sc;
        e = abs(h * (e1 * k1.r   + e3 * k3.r   + e4 * k4.r   + e5 * k5.r   + e6 * k6.r));
        sc = tol + tol * abs(y5.r);            err = max(err, e / sc);
        e = abs(h * (e1 * k1.th  + e3 * k3.th  + e4 * k4.th  + e5 * k5.th  + e6 * k6.th));
        sc = tol + tol * abs(y5.th);           err = max(err, e / sc);
        e = abs(h * (e1 * k1.pr  + e3 * k3.pr  + e4 * k4.pr  + e5 * k5.pr  + e6 * k6.pr));
        sc = tol + tol * abs(y5.pr);           err = max(err, e / sc);
        e = abs(h * (e1 * k1.pth + e3 * k3.pth + e4 * k4.pth + e5 * k5.pth + e6 * k6.pth));
        sc = tol + tol * abs(y5.pth);          err = max(err, e / sc);
    }

    bool accept = (err <= 1.0) || (abs(h) <= hMin * 1.01);
    if (accept)
        y = y5;

    // PI-free step controller with safety factor and growth clamps
    float fac = 0.9 * pow(max(err, 1e-10), -0.2);
    fac = clamp(fac, 0.2, 5.0);
    h = clamp(abs(h) * fac, hMin, hMax) * sign(h);
    return accept;
}

#endif // INTEGRATORS_GLSL
