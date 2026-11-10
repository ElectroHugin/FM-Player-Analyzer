#include "RatingCurve.h"

#include <algorithm>
#include <cmath>

namespace fm {

RatingCurve RatingCurve::linear(double weight)
{
    RatingCurve result;
    result.weight = weight;
    for (int i = 0; i < 20; ++i)
        result.shape[i] = i / 19.0;
    return result;
}

bool RatingCurve::isValid() const
{
    if (!std::isfinite(weight) || weight < 0.0 || shape.front() != 0.0
        || shape.back() != 1.0)
        return false;
    double previous = 0.0;
    for (double point : shape) {
        if (!std::isfinite(point) || point < previous || point > 1.0)
            return false;
        previous = point;
    }
    return true;
}

std::optional<double> RatingCurve::contribution(int value) const
{
    if (!isValid() || value < 1 || value > 20)
        return std::nullopt;
    return weight * shape[value - 1];
}

std::optional<double> RatingCurve::expectedContribution(int low, int high) const
{
    if (!isValid() || low < 1 || high > 20 || low > high)
        return std::nullopt;
    // Sum normalized shape first: finite weights near DBL_MAX must not cause
    // overflow solely because a range contains several observations.
    double mean = 0.0;
    for (int value = low; value <= high; ++value)
        mean += shape[value - 1] / (high - low + 1);
    return weight * std::clamp(mean, 0.0, 1.0);
}

CurveScore evaluateCurves(const std::vector<CurveObservation> &observations)
{
    CurveScore score;
    double denominator = 0.0;
    for (const auto &observation : observations) {
        if (!observation.curve.isValid() || !std::isfinite(observation.roleFactor)
            || observation.roleFactor < 0.0) {
            score.status = CurveScore::Status::InvalidInput;
            return score;
        }
        const double weight = observation.curve.weight * observation.roleFactor;
        if (!std::isfinite(weight)) {
            score.status = CurveScore::Status::InvalidInput;
            return score;
        }
        if (weight == 0.0)
            continue;
        if (observation.low == 0 && observation.high == 0) {
            score.status = CurveScore::Status::MissingAttribute;
            return score;
        }
        const auto contribution = observation.curve.expectedContribution(observation.low,
                                                                         observation.high);
        if (!contribution) {
            score.status = CurveScore::Status::InvalidInput;
            return score;
        }
        score.absolute += *contribution * observation.roleFactor;
        denominator += weight;
        if (!std::isfinite(score.absolute) || !std::isfinite(denominator)) {
            score.absolute = 0.0;
            score.status = CurveScore::Status::InvalidInput;
            return score;
        }
    }
    if (denominator == 0.0)
        return score;
    score.status = CurveScore::Status::Available;
    score.normalized = std::clamp(score.absolute / denominator * 100.0, 0.0, 100.0);
    return score;
}

std::optional<std::vector<double>> isotonicFit(const std::vector<double> &values)
{
    if (values.empty())
        return std::nullopt;
    struct Block { long double mean; size_t count; };
    std::vector<Block> blocks;
    for (double value : values) {
        if (!std::isfinite(value))
            return std::nullopt;
        blocks.push_back({value, 1});
        while (blocks.size() > 1 && blocks[blocks.size() - 2].mean > blocks.back().mean) {
            const auto right = blocks.back();
            blocks.pop_back();
            auto &left = blocks.back();
            const auto count = left.count + right.count;
            left.mean = left.mean * (static_cast<long double>(left.count) / count)
                        + right.mean * (static_cast<long double>(right.count) / count);
            left.count = count;
        }
    }
    std::vector<double> result;
    result.reserve(values.size());
    for (const auto &block : blocks)
        result.insert(result.end(), block.count, static_cast<double>(block.mean));
    return result;
}

std::optional<RatingCurve> harvestgreenCurve(const std::array<double, 4> &points,
                                            double cutoff)
{
    if (!std::isfinite(cutoff) || cutoff < 0.0)
        return std::nullopt;
    const auto fit = isotonicFit({points.begin(), points.end()});
    if (!fit)
        return std::nullopt;
    const double total = fit->back() - fit->front();
    if (!std::isfinite(total))
        return std::nullopt;
    if (total <= cutoff)
        return RatingCurve::linear(0.0);
    RatingCurve result;
    result.weight = total;
    constexpr std::array<int, 4> knots{1, 6, 12, 18};
    for (int x = 1; x <= 20; ++x) {
        if (x >= 18) {
            result.shape[x - 1] = 1.0;
            continue;
        }
        int segment = 0;
        while (x > knots[segment + 1])
            ++segment;
        const double fraction = double(x - knots[segment]) / (knots[segment + 1] - knots[segment]);
        result.shape[x - 1] = ((*fit)[segment] - fit->front()
                              + ((*fit)[segment + 1] - (*fit)[segment]) * fraction) / total;
    }
    return result.isValid() ? std::optional<RatingCurve>(result) : std::nullopt;
}

} // namespace fm
