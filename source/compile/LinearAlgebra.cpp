#include "LinearAlgebra.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace foundry::linalg
{

bool cholesky (std::vector<double>& A, int n)
{
    for (int j = 0; j < n; ++j)
    {
        double d = A[(size_t) (j * n + j)];
        for (int k = 0; k < j; ++k)
            d -= A[(size_t) (j * n + k)] * A[(size_t) (j * n + k)];
        if (! (d > 0.0))
            return false;
        const double ljj = std::sqrt (d);
        A[(size_t) (j * n + j)] = ljj;
        for (int i = j + 1; i < n; ++i)
        {
            double s = A[(size_t) (i * n + j)];
            for (int k = 0; k < j; ++k)
                s -= A[(size_t) (i * n + k)] * A[(size_t) (j * n + k)];
            A[(size_t) (i * n + j)] = s / ljj;
        }
        for (int i = 0; i < j; ++i)
            A[(size_t) (i * n + j)] = 0.0;
    }
    return true;
}

void symmetricEigen (std::vector<double>& A, int n, std::vector<double>& w, std::vector<double>& V)
{
    V.assign ((size_t) (n * n), 0.0);
    for (int i = 0; i < n; ++i)
        V[(size_t) (i * n + i)] = 1.0;

    auto at = [&] (int i, int j) -> double& { return A[(size_t) (i * n + j)]; };

    for (int sweep = 0; sweep < 60; ++sweep)
    {
        double off = 0.0, diag = 0.0;
        for (int i = 0; i < n; ++i)
        {
            diag += at (i, i) * at (i, i);
            for (int j = i + 1; j < n; ++j)
                off += at (i, j) * at (i, j);
        }
        if (off <= 1e-30 * diag || off == 0.0)
            break;

        for (int p = 0; p < n - 1; ++p)
            for (int q = p + 1; q < n; ++q)
            {
                const double apq = at (p, q);
                if (std::abs (apq) < 1e-300)
                    continue;
                const double app = at (p, p), aqq = at (q, q);
                const double theta = (aqq - app) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs (theta) + std::sqrt (theta * theta + 1.0));
                const double c = 1.0 / std::sqrt (t * t + 1.0), s = t * c;

                for (int k = 0; k < n; ++k)
                {
                    const double akp = at (k, p), akq = at (k, q);
                    at (k, p) = c * akp - s * akq;
                    at (k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double apk = at (p, k), aqk = at (q, k);
                    at (p, k) = c * apk - s * aqk;
                    at (q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double vkp = V[(size_t) (k * n + p)], vkq = V[(size_t) (k * n + q)];
                    V[(size_t) (k * n + p)] = c * vkp - s * vkq;
                    V[(size_t) (k * n + q)] = s * vkp + c * vkq;
                }
            }
    }

    // Sort ascending.
    std::vector<int> order ((size_t) n);
    std::iota (order.begin(), order.end(), 0);
    std::sort (order.begin(), order.end(), [&] (int x, int y) { return at (x, x) < at (y, y); });
    w.resize ((size_t) n);
    std::vector<double> Vs ((size_t) (n * n));
    for (int k = 0; k < n; ++k)
    {
        const int src = order[(size_t) k];
        w[(size_t) k] = at (src, src);
        for (int i = 0; i < n; ++i)
            Vs[(size_t) (i * n + k)] = V[(size_t) (i * n + src)];
    }
    V.swap (Vs);
}

bool generalisedEigen (const std::vector<double>& K, const std::vector<double>& M, int n,
                       std::vector<double>& w, std::vector<double>& X)
{
    std::vector<double> L = M;
    if (! cholesky (L, n))
        return false;

    // C = L^-1 K L^-T. First Y = L^-1 K (forward substitution per column), then C = (L^-1 Y^T)^T.
    std::vector<double> Y = K;
    for (int col = 0; col < n; ++col)
        for (int i = 0; i < n; ++i)
        {
            double s = Y[(size_t) (i * n + col)];
            for (int k = 0; k < i; ++k)
                s -= L[(size_t) (i * n + k)] * Y[(size_t) (k * n + col)];
            Y[(size_t) (i * n + col)] = s / L[(size_t) (i * n + i)];
        }
    std::vector<double> C ((size_t) (n * n));
    for (int row = 0; row < n; ++row)          // C row 'row' = L^-1 applied to Y row 'row' (as a column)
        for (int i = 0; i < n; ++i)
        {
            double s = Y[(size_t) (row * n + i)];
            for (int k = 0; k < i; ++k)
                s -= L[(size_t) (i * n + k)] * C[(size_t) (row * n + k)];
            C[(size_t) (row * n + i)] = s / L[(size_t) (i * n + i)];
        }
    // Symmetrise against round-off.
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            const double s = 0.5 * (C[(size_t) (i * n + j)] + C[(size_t) (j * n + i)]);
            C[(size_t) (i * n + j)] = C[(size_t) (j * n + i)] = s;
        }

    std::vector<double> V;
    symmetricEigen (C, n, w, V);

    // X = L^-T V (back substitution per column).
    X.assign ((size_t) (n * n), 0.0);
    for (int col = 0; col < n; ++col)
        for (int i = n - 1; i >= 0; --i)
        {
            double s = V[(size_t) (i * n + col)];
            for (int k = i + 1; k < n; ++k)
                s -= L[(size_t) (k * n + i)] * X[(size_t) (k * n + col)];
            X[(size_t) (i * n + col)] = s / L[(size_t) (i * n + i)];
        }
    return true;
}

} // namespace foundry::linalg
