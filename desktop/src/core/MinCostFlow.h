#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace fm {

// Exact minimum-cost flow (successive shortest paths with Bellman-Ford, so
// negative edge costs are fine). Built for small graphs such as the FM27 best
// XI (two line-ups of 11 slots plus a squad): a few hundred nodes, a handful
// of augmentations. Integer costs keep the optimum exact.
class MinCostFlow
{
public:
    explicit MinCostFlow(int nodeCount);

    // Adds a directed edge; returns its id for flowOn().
    int addEdge(int from, int to, int capacity, std::int64_t cost);

    // Sends flow from source to sink along cheapest paths while a path with
    // negative total cost exists and maxFlow is not reached. With costs that
    // are negative per unit this yields the cheapest flow of any size, i.e.
    // the most valuable selection. Returns (flow, total cost).
    std::pair<int, std::int64_t> solve(int source, int sink, int maxFlow);

    int flowOn(int edgeId) const;

private:
    struct Edge {
        int to;
        int capacity;
        std::int64_t cost;
        int flow = 0;
    };
    int m_nodeCount;
    std::vector<Edge> m_edges;           // edge 2i forward, 2i+1 residual
    std::vector<std::vector<int>> m_adj; // node -> edge indexes
};

} // namespace fm
