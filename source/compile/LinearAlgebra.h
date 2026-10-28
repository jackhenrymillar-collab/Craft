#pragma once

#include <vector>

namespace foundry::linalg
{
// Dense helpers for the compile step (never used on the audio thread).
// Matrices are row-major n x n in std::vector<double>.

// In-place Cholesky A = L L^T (lower triangle of A receives L). Returns false if not SPD.
bool cholesky (std::vector<double>& A, int n);

// Symmetric eigen-decomposition by cyclic Jacobi. A is destroyed. Eigenvalues ascending;
// eigenvectors are the columns of V (V[i*n + k] = component i of vector k).
void symmetricEigen (std::vector<double>& A, int n, std::vector<double>& eigenvalues, std::vector<double>& V);

// Solve K x = lambda M x for symmetric K and SPD M. Columns of X are M-orthonormal.
bool generalisedEigen (const std::vector<double>& K, const std::vector<double>& M, int n,
                       std::vector<double>& eigenvalues, std::vector<double>& X);
}
