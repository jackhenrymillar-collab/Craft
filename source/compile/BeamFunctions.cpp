#include "BeamFunctions.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>

namespace foundry::beam
{
namespace
{
constexpr double kPi = std::numbers::pi;

// Elastic beam functions written so they stay accurate for large beta: the hyperbolic part
// cosh(b x) - s sinh(b x) is evaluated as ((1 - s) e^{bx} + (1 + s) e^{-bx}) / 2 with (1 - s)
// computed without cancellation.
struct Hyper
{
    double oneMinus, onePlus, b;

    void at (double xi, double& T, double& T1) const noexcept
    {
        // (1 - s) e^{b xi} is passed in pre-scaled as oneMinus * e^{b (xi - 1)}.
        const double ep = oneMinus * std::exp (b * (xi - 1.0));
        const double em = onePlus * std::exp (-b * xi);
        T = 0.5 * (ep + em);   // cosh - s sinh
        T1 = 0.5 * (ep - em);  // sinh - s cosh
    }
};

void freeFree (double b, double xi, double& X, double& dX, double& ddX)
{
    // X = cosh + cos - s (sinh + sin),  s = (cosh b - cos b) / (sinh b - sin b)
    const double eb = std::exp (-b);
    const double denom = 1.0 - eb * eb - 2.0 * std::sin (b) * eb;      // 2 e^{-b} (sinh b - sin b)
    const double s = (1.0 + eb * eb - 2.0 * std::cos (b) * eb) / denom; // (cosh - cos) / (sinh - sin)
    const double oneMinusScaled = 2.0 * (std::cos (b) - std::sin (b) - eb) / denom; // (1 - s) e^{b}
    const Hyper h { oneMinusScaled, 1.0 + s, b };
    double T, T1;
    h.at (xi, T, T1);
    const double c = std::cos (b * xi), sn = std::sin (b * xi);
    X = T + c - s * sn;
    dX = b * (T1 - sn - s * c);
    ddX = b * b * (T - c + s * sn);
}

void clampedFree (double b, double xi, double& X, double& dX, double& ddX)
{
    // X = cosh - cos - s (sinh - sin),  s = (cosh b + cos b) / (sinh b + sin b)
    const double eb = std::exp (-b);
    const double denom = 1.0 - eb * eb + 2.0 * std::sin (b) * eb;
    const double s = (1.0 + eb * eb + 2.0 * std::cos (b) * eb) / denom;
    const double oneMinusScaled = 2.0 * (std::sin (b) - std::cos (b) - eb) / denom;
    const Hyper h { oneMinusScaled, 1.0 + s, b };
    double T, T1;
    h.at (xi, T, T1);
    const double c = std::cos (b * xi), sn = std::sin (b * xi);
    X = T - c + s * sn;
    dX = b * (T1 + sn + s * c);
    ddX = b * b * (T + c - s * sn);
}
} // namespace

double betaL (Ends ends, int n) noexcept
{
    n = std::max (1, n);
    switch (ends)
    {
        case Ends::FreeFree:
        {
            constexpr double first[] = { 4.730040745, 7.853204624, 10.99560784, 14.13716549, 17.27875966 };
            return n <= 5 ? first[n - 1] : (n + 0.5) * kPi;
        }
        case Ends::ClampedFree:
        {
            constexpr double first[] = { 1.875104069, 4.694091133, 7.854757438, 10.99554073, 14.13716839 };
            return n <= 5 ? first[n - 1] : (n - 0.5) * kPi;
        }
        case Ends::PinnedPinned: return n * kPi;
        case Ends::FixedFree: return (n - 0.5) * kPi;
    }
    return n * kPi;
}

void Table::eval (double xi, double& x, double& dx) const noexcept
{
    const double u0 = std::clamp (xi, 0.0, 1.0) * kSize;
    const int i = std::min ((int) u0, kSize - 1);
    const double u = u0 - i;
    const double h = 1.0 / kSize;
    const double u2 = u * u, u3 = u2 * u;
    const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    x = h00 * X[(size_t) i] + h10 * h * dX[(size_t) i] + h01 * X[(size_t) i + 1] + h11 * h * dX[(size_t) i + 1];
    dx = dX[(size_t) i] + u * (dX[(size_t) i + 1] - dX[(size_t) i]);
}

double Table::value (double xi) const noexcept
{
    double x, dx;
    eval (xi, x, dx);
    return x;
}

std::shared_ptr<const Table> table (Ends ends, int index)
{
    static std::mutex lock;
    static std::map<std::pair<int, int>, std::shared_ptr<const Table>> cache;
    const auto key = std::make_pair ((int) ends, index);
    {
        std::lock_guard g (lock);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }

    Table t;
    if (ends == Ends::FreeFree && index == 0)
        t = makeTable ([] (double, double& X, double& dX, double& ddX) { X = 1.0; dX = 0.0; ddX = 0.0; });
    else if (ends == Ends::FreeFree && index == 1)
        t = makeTable ([] (double xi, double& X, double& dX, double& ddX) { X = 2.0 * xi - 1.0; dX = 2.0; ddX = 0.0; });
    else
    {
        const int n = ends == Ends::FreeFree ? index - 1 : index + 1;
        const double b = betaL (ends, n);
        switch (ends)
        {
            case Ends::FreeFree:
                t = makeTable ([b] (double xi, double& X, double& dX, double& ddX) { freeFree (b, xi, X, dX, ddX); });
                break;
            case Ends::ClampedFree:
                t = makeTable ([b] (double xi, double& X, double& dX, double& ddX) { clampedFree (b, xi, X, dX, ddX); });
                break;
            case Ends::PinnedPinned:
            case Ends::FixedFree:
                t = makeTable ([b] (double xi, double& X, double& dX, double& ddX) {
                    X = std::sin (b * xi);
                    dX = b * std::cos (b * xi);
                    ddX = -b * b * X;
                });
                break;
        }
    }
    auto p = std::make_shared<const Table> (std::move (t));
    std::lock_guard g (lock);
    return cache.emplace (key, std::move (p)).first->second;
}

double integrate (const std::vector<double>& a, const std::vector<double>& b, const std::vector<double>* w) noexcept
{
    double s = 0.0;
    const int n = Table::kSize;
    for (int i = 0; i <= n; ++i)
    {
        const double c = (i == 0 || i == n) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        s += c * a[(size_t) i] * b[(size_t) i] * (w != nullptr ? (*w)[(size_t) i] : 1.0);
    }
    return s / (3.0 * n);
}

} // namespace foundry::beam
