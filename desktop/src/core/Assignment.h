#pragma once

#include <cstdint>
#include <vector>

namespace fm {

// Exact solver for the assignment problem: give each row its own column so
// that the summed cost is minimal. Hungarian method with potentials
// (Kuhn-Munkres, O(rows^2 * columns)); the result is a proven optimum, not a
// heuristic. Integer costs keep it exact — no floating-point tie noise.
namespace Assignment {

// cost[row][column] for rows <= columns (every row gets a column; spare
// columns stay unused). Returns the column chosen for each row.
std::vector<int> solveMinCost(const std::vector<std::vector<std::int64_t>> &cost);

} // namespace Assignment

} // namespace fm
