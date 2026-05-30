// Kerr black hole geodesic integrator in Boyer-Lindquist coordinates.
// Hamiltonian form: H = 1/2 g^{mu nu} p_mu p_nu = 0 for null geodesics.
// State = (r, theta, phi, p_r, p_theta); E, Lz, Q are conserved per-ray.

#ifndef KERR_GLSL
#define KERR_GLSL

// Black hole parameters (from UBO)
// bh.x = M (mass), bh.y = a (spin = a/M * M), bh.z = a^2
// Geometrized units: G = c = 1, lengths in units of M

struct KerrParams {
    float M;
    float a;
    float a2; // a*a
};

struct RayConstants {
    float E;   // energy = -p_t
    float Lz;  // angular momentum = p_phi
    float Q;   // Carter constant
};

struct GeoState {
    float r;
    float theta;
    float phi;
    float p_r;     // covariant p_r (= Sigma/Delta * dr/dlambda)
    float p_theta;  // covariant p_theta (= Sigma * dtheta/dlambda)
};

// Metric helper quantities
struct KerrMetric {
    float Sigma;    // r^2 + a^2 cos^2(theta)
    float Delta;    // r^2 - 2Mr + a^2
    float A;        // (r^2 + a^2)^2 - Delta * a^2 sin^2(theta)
    float sinTheta;
    float cosTheta;
    float sin2Theta;
};

KerrMetric kerr_metric(float r, float theta, KerrParams bh) {
    KerrMetric m;
    m.sinTheta = sin(theta);
    m.cosTheta = cos(theta);
    m.sin2Theta = m.sinTheta * m.sinTheta;
    float cos2 = m.cosTheta * m.cosTheta;
    float r2 = r * r;
    m.Sigma = r2 + bh.a2 * cos2;
    m.Delta = r2 - 2.0 * bh.M * r + bh.a2;
    float r2a2 = r2 + bh.a2;
    m.A = r2a2 * r2a2 - m.Delta * bh.a2 * m.sin2Theta;
    return m;
}

float kerr_horizon(KerrParams bh) {
    return bh.M + sqrt(bh.M * bh.M - bh.a2);
}

// Hamiltonian equations of motion: d/dlambda of state variables
// Using the effective potential formulation for numerical stability.
//
// dr/dlambda     = Delta / Sigma * p_r
// dtheta/dlambda = 1 / Sigma * p_theta
// dphi/dlambda   = -(a E - Lz / sin^2 theta) ... standard BL form
// dp_r/dlambda   = ... (partial derivatives of H w.r.t. r)
// dp_theta/dlambda = ... (partial derivatives of H w.r.t. theta)

struct GeoDerivs {
    float dr;
    float dtheta;
    float dphi;
    float dp_r;
    float dp_theta;
};

// Hamiltonian equations of motion using direct g^{ab} partial derivatives.
// This gives dp_r/dlambda = -dH/dr and dp_theta/dlambda = -dH/dtheta where
// 2H = g^{ab} p_a p_b = 0 on shell.
GeoDerivs kerr_rhs(GeoState s, RayConstants rc, KerrParams bh) {
    float r = s.r;
    float th = s.theta;
    float a = bh.a;
    float a2 = bh.a2;
    float M = bh.M;

    float r2 = r * r;
    float sth = sin(th);
    float cth = cos(th);
    float s2 = sth * sth;
    float c2 = cth * cth;
    float sc = sth * cth;

    float Sigma = r2 + a2 * c2;
    float Delta = r2 - 2.0 * M * r + a2;
    float invSig = 1.0 / Sigma;

    float E = rc.E;
    float L = rc.Lz;

    // P = E(r^2 + a^2) - aL
    float P = E * (r2 + a2) - a * L;

    // THETA = Q - c2 (a2(E2-1) + L2/s2)  -- but for null geodesics we drop the -1
    // Actually for null: THETA = Q + c2 (a2 E2 - L2/s2)
    // with the sign convention Q = p_theta^2 + c2(a2 E2 - L2/s2) at the turning point
    // So p_theta^2 = THETA = Q + c2 * (a2*E*E - L*L/s2)  ... hmm sign depends on Q definition

    // Using the definition Q_carter such that:
    // p_theta^2 = Q + a2 E2 cos2 - L2 cos2/sin2 = Q + cos2(a2 E2 - L2/sin2)
    // This is the "affine" Q. We'll use this consistently.

    float pr = s.p_r;
    float pth = s.p_theta;

    GeoDerivs d;

    // Position derivatives
    d.dr = Delta * invSig * pr;
    d.dtheta = invSig * pth;

    // phi derivative
    // Guard sin²θ so Lz/sin²θ doesn't diverge near the poles.
    // sin²(0.01 rad) ≈ 1e-4, which limits the dphi spike to ~1e4·L per unit Σ.
    // Geodesics that genuinely cross the pole have Lz ≈ 0, so the cap is harmless.
    d.dphi = (a * P / Delta - a * E + L / max(s2, 1e-4)) * invSig;

    // Momentum derivatives from Hamiltonian partial derivatives
    // -dH/dr and -dH/dtheta where 2*Sigma*H = Delta*pr^2 + pth^2 + R_eff/Delta + Theta_eff

    // dp_r: from differentiating H w.r.t. r
    float dDelta = 2.0 * r - 2.0 * M;
    float dSigma_r = 2.0 * r;

    // R(r) = P^2 - Delta*(Q + (L-aE)^2)  for null geodesics
    float LaE = L - a * E;
    float LaE2 = LaE * LaE;
    float Qeff = rc.Q + LaE2;

    d.dp_r = -(dDelta * pr * pr * 0.5) * invSig
           + (2.0 * r * E * P - 0.5 * dDelta * Qeff) / (Sigma * Delta)
           + (P * P / (Delta * Delta) * dDelta * 0.5) * invSig * (-1.0)
           + dSigma_r * invSig * invSig * (Delta * pr * pr + pth * pth) * 0.5
           - dSigma_r * invSig * invSig * (P * P / Delta + Qeff) * 0.5 * (-1.0);

    // Simplify: use the direct Hamiltonian approach
    // H = 1/(2Sigma) [Delta pr^2 + pth^2 + chi]
    // where chi = -P^2/Delta + Q + LaE^2 + ... no, we need:
    // 2 Sigma H = Delta pr^2 + pth^2 - P^2/Delta + Qeff  (for null, H=0)
    // Wait, that's not right either. Let me think again.

    // For null geodesics in Kerr:
    // Sigma dr/dlambda = +-sqrt(R)   where R = P^2 - Delta*(Q + LaE^2)
    // Sigma dtheta/dlambda = +-sqrt(Theta)  where Theta = Q + c2*(a2 E^2 - L^2/s2)
    //
    // In canonical momentum form:
    // p_r = Sigma/Delta * dr/dlambda  =>  p_r^2 * Delta^2/Sigma^2 = R/Sigma^2
    //                                 =>  p_r^2 = R / (Sigma^2 ... no)
    // Actually p_r = g_{rr} dr/dlambda = Sigma/Delta * dr/dlambda
    // So Sigma dr/dlambda = Delta * p_r
    // Then (Delta p_r)^2 = R = P^2 - Delta*Qeff
    //
    // Similarly p_theta = g_{theta theta} dtheta/dlambda = Sigma * dtheta/dlambda
    // So p_theta^2 = Theta = Q + c2(a2 E^2 - L^2/s2)

    // The Hamiltonian is H = 1/2 g^{ab} p_a p_b = 0
    // Explicitly:
    // 2 Sigma H = -A/(Sigma Delta) E^2 + 4Mar/(Sigma Delta) E Lz
    //           + (Delta - a^2 s2)/(Sigma Delta s2) L^2
    //           + Delta/Sigma p_r^2 + 1/Sigma p_theta^2
    // where A = (r^2+a^2)^2 - Delta a^2 s2
    //
    // Or equivalently (and more useful for derivatives):
    // 2 Sigma H = Delta p_r^2 + p_theta^2 - P^2/Delta + Qeff
    // Because P^2/Delta = [(r2+a2)E - aL]^2 / Delta and the cross terms work out.
    // And 0 = Delta p_r^2 + p_theta^2 - P^2/Delta + Qeff  (on shell)

    // So dp_r/dlambda = -partial H / partial r
    //                 = -1/(2Sigma) partial_r [Delta p_r^2 + pth^2 - P^2/Delta + Qeff]
    //                 + (r/Sigma^2) [Delta p_r^2 + pth^2 - P^2/Delta + Qeff]
    // The second line = 0 on-shell!

    // So dp_r/dlambda = -1/(2Sigma) [dDelta*p_r^2 - d(P^2/Delta)/dr]
    // d(P^2/Delta)/dr = (2P dP/dr Delta - P^2 dDelta) / Delta^2
    //                 = (2P * 2rE * Delta - P^2 * dDelta) / Delta^2

    float P2 = P * P;
    float D2 = Delta * Delta;

    d.dp_r = -0.5 * invSig * (
        dDelta * pr * pr
        - (2.0 * P * 2.0 * r * E * Delta - P2 * dDelta) / D2
    );

    // dp_theta/dlambda = -partial H / partial theta
    // partial_theta [pth^2] = 0 (pth is a variable, not a function of theta in Hamiltonian sense)
    // partial_theta [Delta p_r^2] = 0
    // partial_theta [-P^2/Delta] = 0 (P doesn't depend on theta)
    // partial_theta [Qeff] = partial_theta [Q + LaE^2] = 0 (Q and LaE are constants)
    // BUT we have the Sigma factor: -1/(2Sigma) * stuff + dSigma_dtheta/(2 Sigma^2) * stuff_on_shell
    // and stuff_on_shell = 0, so dp_theta only comes from the theta-dependent part of the Hamiltonian
    // that we haven't separated out.

    // Actually I need to be more careful. The canonical form uses:
    // p_theta^2 contains the theta information implicitly through the constraint.
    // Let me restart with the fully expanded Hamiltonian.

    // The proper way: express H entirely in terms of canonical variables (r,th,phi; p_r,p_th,p_phi=Lz)
    // and the constant p_t = -E.
    //
    // g^{tt} = -A/(Sigma Delta)
    // g^{t phi} = -2Mar/(Sigma Delta)    (= g^{phi t})
    // g^{rr} = Delta/Sigma
    // g^{theta theta} = 1/Sigma
    // g^{phi phi} = (Delta - a^2 s2)/(Sigma Delta s2)
    //
    // 2H = g^{tt}E^2 - 2 g^{t phi} E Lz + g^{rr} p_r^2 + g^{theta theta} p_theta^2 + g^{phi phi} Lz^2
    //
    // dp_theta/dlambda = -dH/dtheta
    // Only theta-dependent pieces: g^{tt}, g^{t phi}, g^{phi phi} through sin/cos, and g^{rr}, g^{theta theta} through Sigma.
    //
    // Let's compute directly:

    float A_val = (r2 + a2) * (r2 + a2) - Delta * a2 * s2;
    // dA/dtheta = -Delta * a2 * 2*sth*cth = -2*Delta*a2*sc

    float invSigDelta = 1.0 / (Sigma * Delta);
    float invSig2 = invSig * invSig;
    float dSigma_th = -2.0 * a2 * sc;

    // Compute each g^{ab} derivative w.r.t. theta:
    // g^{tt} = -A/(Sigma*Delta)
    // dg^{tt}/dth = -(dA_dth * Sigma*Delta - A * dSigma_th * Delta) / (Sigma*Delta)^2
    //            = -1/(Sigma*Delta) * (dA_dth - A * dSigma_th / Sigma)
    float dA_dth = -2.0 * Delta * a2 * sc;
    float dgtt_dth = -(dA_dth - A_val * dSigma_th * invSig) * invSigDelta;

    // g^{t phi} = -2Mar/(Sigma*Delta)   -- only Sigma depends on theta
    // dg^{tphi}/dth = 2Mar * dSigma_th / (Sigma^2 * Delta)
    float omega_cross = 2.0 * M * a * r;
    float dgtphi_dth = omega_cross * dSigma_th * invSig2 / Delta;

    // g^{rr} = Delta/Sigma
    // dg^{rr}/dth = -Delta * dSigma_th / Sigma^2
    float dgrr_dth = -Delta * dSigma_th * invSig2;

    // g^{theta theta} = 1/Sigma
    // dg^{thth}/dth = -dSigma_th / Sigma^2
    float dgthth_dth = -dSigma_th * invSig2;

    // g^{phi phi} = (Delta - a^2 s2) / (Sigma Delta s2)
    // Let N = Delta - a^2 s2, D = Sigma Delta s2
    // dN/dth = -a^2 * 2*sc
    // dD/dth = (dSigma_th * Delta * s2 + Sigma * Delta * 2*sc)
    // dg^{phiphi}/dth = (dN*D - N*dD) / D^2
    float N_pp = Delta - a2 * s2;
    float D_pp = Sigma * Delta * (s2 + 1e-30);
    float dN_dth = -a2 * 2.0 * sc;
    float dD_dth = dSigma_th * Delta * s2 + Sigma * Delta * 2.0 * sc;
    float dgphiphi_dth = (dN_dth * D_pp - N_pp * dD_dth) / (D_pp * D_pp + 1e-30);

    // dp_theta/dlambda = -dH/dtheta = -0.5 * (dgtt * E^2 - 2*dgtphi*E*L + dgrr*pr^2 + dgthth*pth^2 + dgphiphi*L^2)
    d.dp_theta = -0.5 * (
        dgtt_dth * E * E
        - 2.0 * dgtphi_dth * E * L
        + dgrr_dth * pr * pr
        + dgthth_dth * pth * pth
        + dgphiphi_dth * L * L
    );

    return d;
}

// RK4 integration step
GeoState kerr_rk4(GeoState s, RayConstants rc, KerrParams bh, float dlambda) {
    GeoDerivs k1 = kerr_rhs(s, rc, bh);

    GeoState s2;
    s2.r       = s.r       + 0.5 * dlambda * k1.dr;
    s2.theta   = s.theta   + 0.5 * dlambda * k1.dtheta;
    s2.phi     = s.phi     + 0.5 * dlambda * k1.dphi;
    s2.p_r     = s.p_r     + 0.5 * dlambda * k1.dp_r;
    s2.p_theta = s.p_theta + 0.5 * dlambda * k1.dp_theta;
    GeoDerivs k2 = kerr_rhs(s2, rc, bh);

    GeoState s3;
    s3.r       = s.r       + 0.5 * dlambda * k2.dr;
    s3.theta   = s.theta   + 0.5 * dlambda * k2.dtheta;
    s3.phi     = s.phi     + 0.5 * dlambda * k2.dphi;
    s3.p_r     = s.p_r     + 0.5 * dlambda * k2.dp_r;
    s3.p_theta = s.p_theta + 0.5 * dlambda * k2.dp_theta;
    GeoDerivs k3 = kerr_rhs(s3, rc, bh);

    GeoState s4;
    s4.r       = s.r       + dlambda * k3.dr;
    s4.theta   = s.theta   + dlambda * k3.dtheta;
    s4.phi     = s.phi     + dlambda * k3.dphi;
    s4.p_r     = s.p_r     + dlambda * k3.dp_r;
    s4.p_theta = s.p_theta + dlambda * k3.dp_theta;
    GeoDerivs k4 = kerr_rhs(s4, rc, bh);

    GeoState result;
    result.r       = s.r       + dlambda / 6.0 * (k1.dr       + 2.0*k2.dr       + 2.0*k3.dr       + k4.dr);
    result.theta   = s.theta   + dlambda / 6.0 * (k1.dtheta   + 2.0*k2.dtheta   + 2.0*k3.dtheta   + k4.dtheta);
    result.phi     = s.phi     + dlambda / 6.0 * (k1.dphi     + 2.0*k2.dphi     + 2.0*k3.dphi     + k4.dphi);
    result.p_r     = s.p_r     + dlambda / 6.0 * (k1.dp_r     + 2.0*k2.dp_r     + 2.0*k3.dp_r     + k4.dp_r);
    result.p_theta = s.p_theta + dlambda / 6.0 * (k1.dp_theta + 2.0*k2.dp_theta + 2.0*k3.dp_theta + k4.dp_theta);

    return result;
}

// Adaptive RK45 (Fehlberg) step with error control
GeoState kerr_rkf45(GeoState s, RayConstants rc, KerrParams bh,
                     inout float dlambda, float tol) {
    // Cash-Karp coefficients for 4th/5th order embedded pair
    GeoDerivs k1 = kerr_rhs(s, rc, bh);

    GeoState st;

    // Stage 2
    st.r = s.r + dlambda * 0.2 * k1.dr;
    st.theta = s.theta + dlambda * 0.2 * k1.dtheta;
    st.phi = s.phi + dlambda * 0.2 * k1.dphi;
    st.p_r = s.p_r + dlambda * 0.2 * k1.dp_r;
    st.p_theta = s.p_theta + dlambda * 0.2 * k1.dp_theta;
    GeoDerivs k2 = kerr_rhs(st, rc, bh);

    // Stage 3
    st.r = s.r + dlambda * (3.0/40.0*k1.dr + 9.0/40.0*k2.dr);
    st.theta = s.theta + dlambda * (3.0/40.0*k1.dtheta + 9.0/40.0*k2.dtheta);
    st.phi = s.phi + dlambda * (3.0/40.0*k1.dphi + 9.0/40.0*k2.dphi);
    st.p_r = s.p_r + dlambda * (3.0/40.0*k1.dp_r + 9.0/40.0*k2.dp_r);
    st.p_theta = s.p_theta + dlambda * (3.0/40.0*k1.dp_theta + 9.0/40.0*k2.dp_theta);
    GeoDerivs k3 = kerr_rhs(st, rc, bh);

    // Stage 4
    st.r = s.r + dlambda * (0.3*k1.dr - 0.9*k2.dr + 1.2*k3.dr);
    st.theta = s.theta + dlambda * (0.3*k1.dtheta - 0.9*k2.dtheta + 1.2*k3.dtheta);
    st.phi = s.phi + dlambda * (0.3*k1.dphi - 0.9*k2.dphi + 1.2*k3.dphi);
    st.p_r = s.p_r + dlambda * (0.3*k1.dp_r - 0.9*k2.dp_r + 1.2*k3.dp_r);
    st.p_theta = s.p_theta + dlambda * (0.3*k1.dp_theta - 0.9*k2.dp_theta + 1.2*k3.dp_theta);
    GeoDerivs k4 = kerr_rhs(st, rc, bh);

    // Stage 5
    st.r = s.r + dlambda * (-11.0/54.0*k1.dr + 2.5*k2.dr - 70.0/27.0*k3.dr + 35.0/27.0*k4.dr);
    st.theta = s.theta + dlambda * (-11.0/54.0*k1.dtheta + 2.5*k2.dtheta - 70.0/27.0*k3.dtheta + 35.0/27.0*k4.dtheta);
    st.phi = s.phi + dlambda * (-11.0/54.0*k1.dphi + 2.5*k2.dphi - 70.0/27.0*k3.dphi + 35.0/27.0*k4.dphi);
    st.p_r = s.p_r + dlambda * (-11.0/54.0*k1.dp_r + 2.5*k2.dp_r - 70.0/27.0*k3.dp_r + 35.0/27.0*k4.dp_r);
    st.p_theta = s.p_theta + dlambda * (-11.0/54.0*k1.dp_theta + 2.5*k2.dp_theta - 70.0/27.0*k3.dp_theta + 35.0/27.0*k4.dp_theta);
    GeoDerivs k5 = kerr_rhs(st, rc, bh);

    // Stage 6
    st.r = s.r + dlambda * (1631.0/55296.0*k1.dr + 175.0/512.0*k2.dr + 575.0/13824.0*k3.dr + 44275.0/110592.0*k4.dr + 253.0/4096.0*k5.dr);
    st.theta = s.theta + dlambda * (1631.0/55296.0*k1.dtheta + 175.0/512.0*k2.dtheta + 575.0/13824.0*k3.dtheta + 44275.0/110592.0*k4.dtheta + 253.0/4096.0*k5.dtheta);
    st.phi = s.phi + dlambda * (1631.0/55296.0*k1.dphi + 175.0/512.0*k2.dphi + 575.0/13824.0*k3.dphi + 44275.0/110592.0*k4.dphi + 253.0/4096.0*k5.dphi);
    st.p_r = s.p_r + dlambda * (1631.0/55296.0*k1.dp_r + 175.0/512.0*k2.dp_r + 575.0/13824.0*k3.dp_r + 44275.0/110592.0*k4.dp_r + 253.0/4096.0*k5.dp_r);
    st.p_theta = s.p_theta + dlambda * (1631.0/55296.0*k1.dp_theta + 175.0/512.0*k2.dp_theta + 575.0/13824.0*k3.dp_theta + 44275.0/110592.0*k4.dp_theta + 253.0/4096.0*k5.dp_theta);
    GeoDerivs k6 = kerr_rhs(st, rc, bh);

    // 5th order solution
    const float b5_r = 37.0/378.0*k1.dr + 250.0/621.0*k3.dr + 125.0/594.0*k4.dr + 512.0/1771.0*k6.dr;
    const float b5_th = 37.0/378.0*k1.dtheta + 250.0/621.0*k3.dtheta + 125.0/594.0*k4.dtheta + 512.0/1771.0*k6.dtheta;
    const float b5_pr = 37.0/378.0*k1.dp_r + 250.0/621.0*k3.dp_r + 125.0/594.0*k4.dp_r + 512.0/1771.0*k6.dp_r;

    // 4th order solution for error estimate
    const float b4_r = 2825.0/27648.0*k1.dr + 18575.0/48384.0*k3.dr + 13525.0/55296.0*k4.dr + 277.0/14336.0*k5.dr + 0.25*k6.dr;
    const float b4_th = 2825.0/27648.0*k1.dtheta + 18575.0/48384.0*k3.dtheta + 13525.0/55296.0*k4.dtheta + 277.0/14336.0*k5.dtheta + 0.25*k6.dtheta;
    const float b4_pr = 2825.0/27648.0*k1.dp_r + 18575.0/48384.0*k3.dp_r + 13525.0/55296.0*k4.dp_r + 277.0/14336.0*k5.dp_r + 0.25*k6.dp_r;

    // Error estimate
    float err_r  = abs(b5_r  - b4_r)  * dlambda;
    float err_th = abs(b5_th - b4_th) * dlambda;
    float err_pr = abs(b5_pr - b4_pr) * dlambda;
    float err = max(err_r, max(err_th, err_pr));

    // Adjust step size
    float safety = 0.9;
    if (err > 1e-30) {
        float scale = safety * pow(tol / err, 0.2);
        scale = clamp(scale, 0.1, 5.0);
        dlambda *= scale;
    }

    GeoState result;
    result.r       = s.r       + dlambda * b5_r;
    result.theta   = s.theta   + dlambda * (37.0/378.0*k1.dtheta + 250.0/621.0*k3.dtheta + 125.0/594.0*k4.dtheta + 512.0/1771.0*k6.dtheta);
    result.phi     = s.phi     + dlambda * (37.0/378.0*k1.dphi + 250.0/621.0*k3.dphi + 125.0/594.0*k4.dphi + 512.0/1771.0*k6.dphi);
    result.p_r     = s.p_r     + dlambda * b5_pr;
    result.p_theta = s.p_theta + dlambda * (37.0/378.0*k1.dp_theta + 250.0/621.0*k3.dp_theta + 125.0/594.0*k4.dp_theta + 512.0/1771.0*k6.dp_theta);

    return result;
}

// Convert BL coordinates to Cartesian for VDB intersection
vec3 bl_to_cartesian(float r, float theta, float phi) {
    float sth = sin(theta);
    return vec3(
        r * sth * cos(phi),
        r * sth * sin(phi),
        r * cos(theta)
    );
}

// Check if a point (in BL coords) is inside the disk AABB
bool in_disk_aabb(float r, float theta, float r_inner, float r_outer, float half_angle) {
    float th_eq = abs(theta - 3.14159265 * 0.5);
    return r >= r_inner && r <= r_outer && th_eq <= half_angle;
}

// ISCO radius for prograde orbits in Kerr
float kerr_isco(KerrParams bh) {
    float a_star = bh.a / bh.M;
    float Z1 = 1.0 + pow(1.0 - a_star * a_star, 1.0/3.0)
              * (pow(1.0 + a_star, 1.0/3.0) + pow(1.0 - a_star, 1.0/3.0));
    float Z2 = sqrt(3.0 * a_star * a_star + Z1 * Z1);
    return bh.M * (3.0 + Z2 - sqrt((3.0 - Z1) * (3.0 + Z1 + 2.0 * Z2)));
}

// Keplerian angular velocity for circular orbit in Kerr equatorial plane
float kerr_omega(float r, KerrParams bh) {
    return 1.0 / (bh.a + pow(r, 1.5) / sqrt(bh.M));
}

// Fluid 4-velocity for circular Keplerian orbit (covariant components in BL)
// Returns (u_t, u_phi) for the orbiting fluid element; u_r = u_theta = 0
vec2 keplerian_u_covariant(float r, float theta, KerrParams bh) {
    float omega = kerr_omega(r, bh);
    float sth = sin(theta);
    float s2 = sth * sth;
    float r2 = r * r;
    float Sigma = r2 + bh.a2 * cos(theta) * cos(theta);
    float Delta = r2 - 2.0 * bh.M * r + bh.a2;

    float g_tt = -(1.0 - 2.0 * bh.M * r / Sigma);
    float g_tphi = -2.0 * bh.M * bh.a * r * s2 / Sigma;
    float g_phiphi = (r2 + bh.a2 + 2.0 * bh.M * bh.a2 * r * s2 / Sigma) * s2;

    float u_t_sq = -1.0 / (g_tt + 2.0 * g_tphi * omega + g_phiphi * omega * omega);
    float u_t = (u_t_sq > 0.0) ? sqrt(u_t_sq) : 1.0;

    return vec2(-u_t * (g_tt + g_tphi * omega), u_t * (g_tphi + g_phiphi * omega));
}

// Frequency shift factor g = nu_obs / nu_emit = (p_mu u^mu)_obs / (p_mu u^mu)_emit
float compute_g_factor(GeoState s, RayConstants rc, KerrParams bh, float u_obs_energy) {
    vec2 u_cov = keplerian_u_covariant(s.r, s.theta, bh);
    float p_dot_u_emit = -rc.E * (-u_cov.x) + rc.Lz * u_cov.y;
    // Wait, need to be careful with index placement.
    // p_mu u^mu = p_t u^t + p_phi u^phi
    // p_t = -E, p_phi = Lz
    // We need u^t and u^phi (contravariant)
    float omega = kerr_omega(s.r, bh);
    float sth = sin(s.theta);
    float s2 = sth * sth;
    float r2 = s.r * s.r;
    float Sigma = r2 + bh.a2 * cos(s.theta) * cos(s.theta);
    float g_tt = -(1.0 - 2.0 * bh.M * s.r / Sigma);
    float g_tphi = -2.0 * bh.M * bh.a * s.r * s2 / Sigma;
    float g_phiphi = (r2 + bh.a2 + 2.0 * bh.M * bh.a2 * s.r * s2 / Sigma) * s2;

    float denom = g_tt + 2.0 * g_tphi * omega + g_phiphi * omega * omega;
    float ut = (denom < 0.0) ? 1.0 / sqrt(-denom) : 1.0;
    float uphi = omega * ut;

    // p_mu u^mu = p_t * u^t + p_phi * u^phi = (-E)*u^t + Lz*u^phi
    float p_dot_u = -rc.E * ut + rc.Lz * uphi;

    // For a static observer at the camera location, u_obs_energy = (p·u)_obs is passed in
    return u_obs_energy / p_dot_u;
}

#endif // KERR_GLSL
