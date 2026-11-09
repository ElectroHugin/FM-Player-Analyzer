#include "BridgeAssignment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

namespace fm::BridgeAssignment {

std::vector<Option> refine(const std::vector<std::vector<Option>> &options,
                           const std::vector<Option> &original,
                           int ipSlots, int oopSlots, double tolerance)
{
    constexpr double epsilon = 1e-9;
    const size_t count = original.size();
    std::vector<Option> best = original, current(count);
    double baselineRating = 0.0, baselineScore = 0.0, bestDistance = 0.0;
    int bestSides = 0;
    for (const Option &o : original) {
        baselineRating += o.rating;
        baselineScore += o.score;
        bestSides += o.preferredSides;
        bestDistance += o.distance;
    }
    const double minRating = baselineRating - tolerance - epsilon;
    const double minScore = baselineScore - tolerance - epsilon;
    double bestRating = baselineRating;

    // The most constrained player first. Stable order makes exact ties
    // repeatable without changing the caller's player-to-result mapping.
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return options[a].size() < options[b].size();
    });
    auto ranked = options;
    for (auto &choices : ranked) {
        std::stable_sort(choices.begin(), choices.end(), [](const Option &a, const Option &b) {
            if (a.preferredSides != b.preferredSides)
                return a.preferredSides > b.preferredSides;
            if (a.distance != b.distance)
                return a.distance < b.distance;
            return a.rating > b.rating;
        });
    }
    std::vector<bool> usedIp(static_cast<size_t>(ipSlots)), usedOop(static_cast<size_t>(oopSlots));
    const auto available = [&](const Option &o) { return !usedIp[o.ip] && !usedOop[o.oop]; };

    // Branch-and-bound over only the selected team. The optimistic
    // bounds ignore competition for slots, so they may prune only impossible
    // or already beaten branches, never a better feasible arrangement.
    // A deterministic work limit keeps unusually dense custom tactics from
    // blocking the UI (this also runs for replacement and signing candidates).
    // Every incumbent, including the original, satisfies both quality floors.
    constexpr size_t maxChecks = 2'000'000;
    size_t checks = 0;
    std::function<void(size_t, double, double, int, double)> search;
    search = [&](size_t depth, double rating, double score, int sides, double distance) {
        if (checks >= maxChecks)
            return;
        if (depth == count) {
            if (rating < minRating || score < minScore)
                return;
            if (sides > bestSides || (sides == bestSides && distance < bestDistance - epsilon)
                || (sides == bestSides && std::abs(distance - bestDistance) <= epsilon
                    && rating > bestRating + epsilon)) {
                best = current;
                bestSides = sides;
                bestDistance = distance;
                bestRating = rating;
            }
            return;
        }
        double upperRating = rating, upperScore = score, lowerDistance = distance;
        int upperSides = sides;
        for (size_t remaining = depth; remaining < count; ++remaining) {
            double maxRating = -1.0, maxScore = -1.0;
            double minDistance = std::numeric_limits<double>::infinity();
            int maxSides = 0;
            for (const Option &o : ranked[order[remaining]]) {
                if (++checks >= maxChecks)
                    return;
                if (!available(o))
                    continue;
                maxRating = std::max(maxRating, o.rating);
                maxScore = std::max(maxScore, o.score);
                maxSides = std::max(maxSides, o.preferredSides);
                minDistance = std::min(minDistance, o.distance);
            }
            if (maxRating < 0.0)
                return;
            upperRating += maxRating;
            upperScore += maxScore;
            upperSides += maxSides;
            lowerDistance += minDistance;
        }
        if (upperRating < minRating || upperScore < minScore || upperSides < bestSides)
            return;
        if (upperSides == bestSides
            && (lowerDistance > bestDistance + epsilon
                || (lowerDistance >= bestDistance - epsilon && upperRating <= bestRating + epsilon)))
            return;

        const size_t player = order[depth];
        for (const Option &o : ranked[player]) {
            if (checks >= maxChecks)
                break;
            if (!available(o))
                continue;
            current[player] = o;
            usedIp[o.ip] = usedOop[o.oop] = true;
            search(depth + 1, rating + o.rating, score + o.score,
                   sides + o.preferredSides, distance + o.distance);
            usedIp[o.ip] = usedOop[o.oop] = false;
        }
    };
    search(0, 0.0, 0.0, 0, 0.0);
    return best;
}

} // namespace fm::BridgeAssignment
