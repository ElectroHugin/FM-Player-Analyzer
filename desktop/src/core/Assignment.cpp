#include "Assignment.h"

#include <limits>

namespace fm {

namespace Assignment {

std::vector<int> solveMinCost(const std::vector<std::vector<std::int64_t>> &cost)
{
    const int rows = static_cast<int>(cost.size());
    if (rows == 0)
        return {};
    const int columns = static_cast<int>(cost.front().size());

    // Classic potentials formulation, 1-based with a virtual column 0.
    // u/v are the row/column potentials, match[j] the row holding column j,
    // way[j] the previous column on the augmenting path.
    constexpr std::int64_t kInf = std::numeric_limits<std::int64_t>::max() / 4;
    std::vector<std::int64_t> u(rows + 1, 0), v(columns + 1, 0);
    std::vector<int> match(columns + 1, 0), way(columns + 1, 0);
    for (int row = 1; row <= rows; ++row) {
        match[0] = row;
        int column0 = 0;
        std::vector<std::int64_t> minSlack(columns + 1, kInf);
        std::vector<bool> used(columns + 1, false);
        do {
            used[column0] = true;
            const int row0 = match[column0];
            std::int64_t delta = kInf;
            int column1 = 0;
            for (int j = 1; j <= columns; ++j) {
                if (used[j])
                    continue;
                const std::int64_t slack = cost[row0 - 1][j - 1] - u[row0] - v[j];
                if (slack < minSlack[j]) {
                    minSlack[j] = slack;
                    way[j] = column0;
                }
                if (minSlack[j] < delta) {
                    delta = minSlack[j];
                    column1 = j;
                }
            }
            for (int j = 0; j <= columns; ++j) {
                if (used[j]) {
                    u[match[j]] += delta;
                    v[j] -= delta;
                } else {
                    minSlack[j] -= delta;
                }
            }
            column0 = column1;
        } while (match[column0] != 0);
        // Flip the augmenting path.
        do {
            const int column1 = way[column0];
            match[column0] = match[column1];
            column0 = column1;
        } while (column0 != 0);
    }

    std::vector<int> result(rows, -1);
    for (int j = 1; j <= columns; ++j) {
        if (match[j] != 0)
            result[match[j] - 1] = j - 1;
    }
    return result;
}

} // namespace Assignment

} // namespace fm
