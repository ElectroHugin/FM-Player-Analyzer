#pragma once

#include <array>
#include <optional>
#include <vector>

namespace fm {

// Shape is dimensionless, weight is the full contribution from 1 to 20.
// A disabled attribute retains its editable shape with weight zero.
struct RatingCurve {
    double weight = 0.0;
    std::array<double, 20> shape{};
    double baseline = 0.0; // contribution at level 1; cancels in normalization

    static RatingCurve linear(double weight);
    bool isValid() const;
    std::optional<double> contribution(int value) const;
    // Uniform distribution over the inclusive integer range, not C(mean(x)).
    std::optional<double> expectedContribution(int low, int high) const;
};

struct CurveObservation {
    RatingCurve curve;
    int low = 0;
    int high = 0;
    double roleFactor = 1.0;
};

struct CurveScore {
    enum class Status { Available, MissingAttribute, InvalidInput, NoVariation };
    Status status = Status::NoVariation;
    double absolute = 0.0;
    std::optional<double> normalized;
    // Share (0..1) of the role's total weight that was known for this player.
    // Below 1 the rating was formed from the available attributes only.
    double coverage = 1.0;
};

// Pure evaluator: missing active attributes prevent a point score. Unknown is
// never silently zero or a neutral imputation. Disabled attributes may be missing.
CurveScore evaluateCurves(const std::vector<CurveObservation> &observations);

// Equal-weight pool-adjacent-violators fit. No noise cutoff is implied by this
// mathematical operation. Empty or non-finite input is rejected.
std::optional<std::vector<double>> isotonicFit(const std::vector<double> &values);

// The approved Harvestgreen preparation rule uses four knots 1,6,12,18 and
// disables the whole curve if its fitted total is <= the supplied cutoff.
// Inputs and cutoff use percentage points; this is not a confidence interval.
std::optional<RatingCurve> harvestgreenCurve(const std::array<double, 4> &points,
                                            double cutoff);

} // namespace fm
