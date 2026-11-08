#pragma once

#include <vector>

namespace fm::BridgeAssignment {

// Refinement of an already selected team. Each candidate joins two legal
// slots for one player; the roster and manual constraints are fixed upstream.
struct Option {
    int ip = -1;
    int oop = -1;
    double rating = 0.0;
    double score = 0.0;
    int preferredSides = 0;
    double distance = 0.0;
};

// Prefer explicit central sides, then the smallest sum of bridge lengths.
// Both raw DWRS and the weighted selection score stay within tolerance of
// the original team, so successive moves cannot spend the allowance twice.
// A deterministic search budget returns the best feasible incumbent for very
// dense domains; shortest distance is not guaranteed when that budget is hit.
std::vector<Option> refine(const std::vector<std::vector<Option>> &options,
                           const std::vector<Option> &original,
                           int ipSlots, int oopSlots, double tolerance);

} // namespace fm::BridgeAssignment
