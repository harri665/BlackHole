#include "Blackbody.h"

#include <algorithm>
#include <cmath>

namespace app {
namespace {

// Piecewise-Gaussian fits of the CIE 1931 2-degree color matching functions
// (Wyman, Sloan, Shirley, JCGT 2013). lambda in nanometers.
double gauss(double x, double mu, double s1, double s2)
{
    double t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}

double cieX(double l)
{
    return 1.056 * gauss(l, 599.8, 37.9, 31.0)
         + 0.362 * gauss(l, 442.0, 16.0, 26.7)
         - 0.065 * gauss(l, 501.1, 20.4, 26.2);
}

double cieY(double l)
{
    return 0.821 * gauss(l, 568.8, 46.9, 40.5)
         + 0.286 * gauss(l, 530.9, 16.3, 31.1);
}

double cieZ(double l)
{
    return 1.217 * gauss(l, 437.0, 11.8, 36.0)
         + 0.681 * gauss(l, 459.0, 26.0, 13.8);
}

// Spectral radiance of a Planck emitter; lambda in meters, T in kelvin.
double planck(double lambda, double T)
{
    constexpr double h = 6.62607015e-34;
    constexpr double c = 2.99792458e8;
    constexpr double kB = 1.380649e-23;
    double x = h * c / (lambda * kB * T);
    if (x > 700.0)
        return 0.0;
    return (2.0 * h * c * c) / std::pow(lambda, 5.0) / (std::exp(x) - 1.0);
}

void planckXYZ(double T, double& X, double& Y, double& Z)
{
    X = Y = Z = 0.0;
    constexpr double l0 = 380.0, l1 = 780.0, dl = 2.0;
    for (double l = l0; l <= l1; l += dl)
    {
        double B = planck(l * 1e-9, T);
        X += B * cieX(l);
        Y += B * cieY(l);
        Z += B * cieZ(l);
    }
}

} // namespace

std::vector<float> makeBlackbodyLUT(int size, float tMin, float tMax)
{
    // normalize so a 6504 K (D65-ish) emitter has luminance 1
    double Xn, Yn, Zn;
    planckXYZ(6504.0, Xn, Yn, Zn);
    double norm = 1.0 / Yn;

    std::vector<float> texels(static_cast<size_t>(size) * 4);
    double logMin = std::log(tMin), logMax = std::log(tMax);

    for (int i = 0; i < size; ++i)
    {
        double t = size > 1 ? double(i) / (size - 1) : 0.0;
        double T = std::exp(logMin + t * (logMax - logMin));

        double X, Y, Z;
        planckXYZ(T, X, Y, Z);
        X *= norm; Y *= norm; Z *= norm;

        // XYZ -> linear sRGB (D65)
        double r =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
        double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
        double b =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;

        texels[i * 4 + 0] = static_cast<float>(std::max(r, 0.0));
        texels[i * 4 + 1] = static_cast<float>(std::max(g, 0.0));
        texels[i * 4 + 2] = static_cast<float>(std::max(b, 0.0));
        texels[i * 4 + 3] = 1.0f;
    }
    return texels;
}

} // namespace app
