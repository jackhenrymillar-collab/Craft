#include "Bessel.h"

#include <cmath>
#include <numbers>

namespace foundry::bessel
{

double J (int m, double x) noexcept
{
    // Number of nodes must exceed x + m comfortably for full double accuracy.
    const int n = 64 + 2 * static_cast<int> (x + m);
    const double h = 2.0 * std::numbers::pi / n;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i * h;
        sum += std::cos (m * t - x * std::sin (t));
    }
    return sum / n;
}

void JandDerivative (int m, double x, double& j, double& dj) noexcept
{
    // cos((m +- 1) t - x sin t) = cos(a) cos t -+ sin(a) sin t, with a = m t - x sin t,
    // so J_{m-1}, J_m and J_{m+1} come out of the same pass.
    const int n = 66 + 2 * static_cast<int> (x + m);
    const double h = 2.0 * std::numbers::pi / n;
    double s0 = 0.0, sMinus = 0.0, sPlus = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i * h;
        const double st = std::sin (t), ct = std::cos (t);
        const double a = m * t - x * st;
        const double ca = std::cos (a), sa = std::sin (a);
        s0 += ca;
        sMinus += ca * ct + sa * st;
        sPlus += ca * ct - sa * st;
    }
    j = s0 / n;
    // J_{-1} = -J_1, which the identity gives automatically for m = 0.
    dj = 0.5 * (sMinus - sPlus) / n;
}

double dJ (int m, double x) noexcept
{
    if (m == 0)
        return -J (1, x);
    return 0.5 * (J (m - 1, x) - J (m + 1, x));
}

double zero (int m, int n) noexcept
{
    // McMahon-style first guess, then scan for the bracketing sign change and refine.
    // Zeros are spaced roughly pi apart; scan from just above the previous one.
    int found = 0;
    double x = (m == 0 ? 1.0 : static_cast<double> (m));
    const double step = 0.05;
    double fPrev = J (m, x);
    for (int guard = 0; guard < 200000; ++guard)
    {
        const double xn = x + step;
        const double f = J (m, xn);
        if ((fPrev < 0.0) != (f < 0.0))
        {
            if (++found == n)
            {
                double lo = x, hi = xn, flo = fPrev;
                for (int it = 0; it < 80; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    const double fm = J (m, mid);
                    if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
                    else hi = mid;
                }
                return 0.5 * (lo + hi);
            }
        }
        x = xn;
        fPrev = f;
    }
    return 0.0;
}

} // namespace foundry::bessel
