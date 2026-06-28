// kerr.glsl — Kerr spacetime in Boyer-Lindquist coordinates (G = c = M = 1).
//
// Null geodesics are integrated from the super-Hamiltonian
//     H = 1/2 g^{mu nu} p_mu p_nu = 0,
// which for the axisymmetric, stationary Kerr metric reduces to five coupled
// ODEs for the state y = (r, theta, phi, p_r, p_theta). The two cyclic
// momenta are conserved: E = -p_t and Lz = p_phi. Hamilton's equations:
//     dr/dl      =  g^rr p_r
//     dtheta/dl  =  g^thth p_th
//     dphi/dl    =  g^tph p_t + g^phph p_ph
//     dp_r/dl    = -dH/dr      (analytic)
//     dp_th/dl   = -dH/dtheta  (analytic)

#ifndef KERR_GLSL
#define KERR_GLSL

struct GeoState
{
    float r, th, ph; // position (t is ignorable)
    float pr, pth;   // covariant momenta
};

// Inverse metric components (the five that matter; metric is block-diagonal
// apart from the t-phi coupling).
void kerrMetricInv(float r, float th, float a,
                   out float gtt, out float gtp, out float gpp,
                   out float grr, out float gthth)
{
    float r2 = r * r, a2 = a * a;
    float s = sin(th), c = cos(th);
    float s2 = max(s * s, 1e-12), c2 = c * c;
    float Sig = r2 + a2 * c2;
    float Del = r2 - 2.0 * r + a2;
    float A   = (r2 + a2) * (r2 + a2) - a2 * Del * s2;
    float invSD = 1.0 / (Sig * Del);

    gtt   = -A * invSD;
    gtp   = -2.0 * a * r * invSD;
    gpp   = (Del - a2 * s2) * invSD / s2;
    grr   = Del / Sig;
    gthth = 1.0 / Sig;
}

// Right-hand side of Hamilton's equations for fixed (E, L).
// All partial derivatives of the inverse metric are analytic — no finite
// differences anywhere in the integrator.
void geodesicRHS(in GeoState y, float E, float L, float a, out GeoState d)
{
    float r = y.r, th = y.th;
    float r2 = r * r, a2 = a * a;
    float s = sin(th), c = cos(th);
    float s2 = max(s * s, 1e-12), c2 = c * c;
    float twosc = 2.0 * s * c;

    float Sig    = r2 + a2 * c2;
    float Sig_r  = 2.0 * r;
    float Sig_th = -a2 * twosc;
    float Del    = r2 - 2.0 * r + a2;
    float Del_r  = 2.0 * r - 2.0;

    float A    = (r2 + a2) * (r2 + a2) - a2 * Del * s2;
    float A_r  = 4.0 * r * (r2 + a2) - a2 * Del_r * s2;
    float A_th = -a2 * Del * twosc;

    float SD     = Sig * Del;
    float SD_r   = Sig_r * Del + Sig * Del_r;
    float SD_th  = Sig_th * Del;
    float invSD  = 1.0 / SD;
    float invSD2 = invSD * invSD;
    float invSig2 = 1.0 / (Sig * Sig);

    // components
    float gtt   = -A * invSD;
    float gtp   = -2.0 * a * r * invSD;
    float gpp   = (Del - a2 * s2) * invSD / s2;
    float grr   = Del / Sig;
    float gthth = 1.0 / Sig;

    // d/dr
    float gtt_r   = -(A_r * SD - A * SD_r) * invSD2;
    float gtp_r   = -2.0 * a * (SD - r * SD_r) * invSD2;
    float gpp_r   = (Del_r * SD - (Del - a2 * s2) * SD_r) * invSD2 / s2;
    float grr_r   = (Del_r * Sig - Del * Sig_r) * invSig2;
    float gthth_r = -Sig_r * invSig2;

    // d/dtheta   (gpp = (Del - a2 s2) / (s2 * SD), quotient rule)
    float gtt_th   = -(A_th * SD - A * SD_th) * invSD2;
    float gtp_th   = 2.0 * a * r * SD_th * invSD2;
    float num      = (-a2 * twosc) * (s2 * SD)
                   - (Del - a2 * s2) * (twosc * SD + s2 * SD_th);
    float gpp_th   = num / (s2 * s2 * SD * SD);
    float grr_th   = -Del * Sig_th * invSig2;
    float gthth_th = -Sig_th * invSig2;

    // Hamilton's equations with p_t = -E, p_phi = L
    d.r   = grr * y.pr;
    d.th  = gthth * y.pth;
    d.ph  = -gtp * E + gpp * L;
    d.pr  = -0.5 * (gtt_r  * E * E - 2.0 * gtp_r  * E * L + gpp_r  * L * L
                  + grr_r  * y.pr * y.pr + gthth_r  * y.pth * y.pth);
    d.pth = -0.5 * (gtt_th * E * E - 2.0 * gtp_th * E * L + gpp_th * L * L
                  + grr_th * y.pr * y.pr + gthth_th * y.pth * y.pth);
}

// Hamiltonian value — should remain ~0 along a null geodesic (diagnostic).
float hamiltonian(in GeoState y, float E, float L, float a)
{
    float gtt, gtp, gpp, grr, gthth;
    kerrMetricInv(y.r, y.th, a, gtt, gtp, gpp, grr, gthth);
    return 0.5 * (gtt * E * E - 2.0 * gtp * E * L + gpp * L * L
                + grr * y.pr * y.pr + gthth * y.pth * y.pth);
}

// Carter constant Q = p_th^2 + cos^2(th) * (L^2/sin^2(th) - a^2 E^2).
// Exactly conserved analytically; its numerical drift is our integration
// error metric (debug view 1).
float carterConstant(in GeoState y, float E, float L, float a)
{
    float s = sin(y.th), c = cos(y.th);
    float s2 = max(s * s, 1e-12);
    return y.pth * y.pth + c * c * (L * L / s2 - a * a * E * E);
}

// -------------------------------------------------------------- camera setup
// Build the photon's initial conserved quantities and momenta from a unit
// direction `n` expressed in the local orthonormal frame of a ZAMO (zero
// angular momentum observer / FIDO) hovering at the camera position.
// n components are along (rhat, thetahat, phihat).
//
// ZAMO tetrad in BL coordinates:
//   e_t  = ( 1/alpha, 0, 0, omega/alpha )      alpha = sqrt(Sig Del / A)
//   e_r  = ( 0, sqrt(Del/Sig), 0, 0 )          omega = 2 a r / A
//   e_th = ( 0, 0, 1/sqrt(Sig), 0 )
//   e_ph = ( 0, 0, 0, sqrt(Sig/A)/sin th )
//
// The photon 4-momentum p^mu = e_t^mu + n^i e_i^mu has unit energy in the
// camera frame, so nu_obs = 1 by construction.
void cameraRay(float r, float th, vec3 n, float a,
               out float E, out float L, out float pr, out float pth)
{
    float r2 = r * r, a2 = a * a;
    float s = sin(th), c = cos(th);
    float s2 = max(s * s, 1e-12), c2 = c * c;
    float Sig = r2 + a2 * c2;
    float Del = r2 - 2.0 * r + a2;
    float A   = (r2 + a2) * (r2 + a2) - a2 * Del * s2;

    float alpha = sqrt(Sig * Del / A);
    float omega = 2.0 * a * r / A;

    // contravariant components of p
    float pt_up  = 1.0 / alpha;
    float pph_up = omega / alpha + n.z * sqrt(Sig / A) / sqrt(s2);
    float pr_up  = n.x * sqrt(Del / Sig);
    float pth_up = n.y / sqrt(Sig);

    // lower indices with the covariant metric
    float g_tt = -(1.0 - 2.0 * r / Sig);
    float g_tp = -2.0 * a * r * s2 / Sig;
    float g_pp = A * s2 / Sig;
    float g_rr = Sig / Del;
    float g_thth = Sig;

    E   = -(g_tt * pt_up + g_tp * pph_up);
    L   =   g_tp * pt_up + g_pp * pph_up;
    pr  =   g_rr * pr_up;
    pth =   g_thth * pth_up;
}

#endif // KERR_GLSL
