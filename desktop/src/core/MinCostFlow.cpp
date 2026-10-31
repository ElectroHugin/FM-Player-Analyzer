#include "MinCostFlow.h"

#include <algorithm>
#include <limits>

namespace fm {

MinCostFlow::MinCostFlow(int nodeCount)
    : m_nodeCount(nodeCount)
    , m_adj(static_cast<size_t>(nodeCount))
{
}

int MinCostFlow::addEdge(int from, int to, int capacity, std::int64_t cost)
{
    const int id = static_cast<int>(m_edges.size());
    m_edges.push_back({to, capacity, cost});
    m_edges.push_back({from, 0, -cost});
    m_adj[static_cast<size_t>(from)].push_back(id);
    m_adj[static_cast<size_t>(to)].push_back(id + 1);
    return id;
}

std::pair<int, std::int64_t> MinCostFlow::solve(int source, int sink, int maxFlow)
{
    constexpr std::int64_t kInf = std::numeric_limits<std::int64_t>::max() / 4;
    int flow = 0;
    std::int64_t totalCost = 0;
    const size_t n = static_cast<size_t>(m_nodeCount);
    std::vector<std::int64_t> dist(n);
    std::vector<int> viaEdge(n);

    while (flow < maxFlow) {
        // Bellman-Ford over the residual graph (SPFA-free plain rounds: the
        // graphs are tiny and this stays obviously correct). The residual
        // graph of a min-cost flow has no negative cycles.
        std::fill(dist.begin(), dist.end(), kInf);
        std::fill(viaEdge.begin(), viaEdge.end(), -1);
        dist[static_cast<size_t>(source)] = 0;
        for (size_t round = 0; round + 1 < n; ++round) {
            bool changed = false;
            for (size_t u = 0; u < n; ++u) {
                if (dist[u] == kInf)
                    continue;
                for (const int e : m_adj[u]) {
                    const Edge &edge = m_edges[static_cast<size_t>(e)];
                    if (edge.capacity - edge.flow <= 0)
                        continue;
                    const std::int64_t candidate = dist[u] + edge.cost;
                    if (candidate < dist[static_cast<size_t>(edge.to)]) {
                        dist[static_cast<size_t>(edge.to)] = candidate;
                        viaEdge[static_cast<size_t>(edge.to)] = e;
                        changed = true;
                    }
                }
            }
            if (!changed)
                break;
        }
        const std::int64_t pathCost = dist[static_cast<size_t>(sink)];
        if (pathCost == kInf || pathCost >= 0)
            break; // no path, or every further unit would lower the value

        // Bottleneck, then augment.
        int push = maxFlow - flow;
        for (int v = sink; v != source;) {
            const Edge &edge = m_edges[static_cast<size_t>(viaEdge[static_cast<size_t>(v)])];
            push = std::min(push, edge.capacity - edge.flow);
            v = m_edges[static_cast<size_t>(viaEdge[static_cast<size_t>(v)] ^ 1)].to;
        }
        for (int v = sink; v != source;) {
            const int e = viaEdge[static_cast<size_t>(v)];
            m_edges[static_cast<size_t>(e)].flow += push;
            m_edges[static_cast<size_t>(e ^ 1)].flow -= push;
            v = m_edges[static_cast<size_t>(e ^ 1)].to;
        }
        flow += push;
        totalCost += pathCost * push;
    }
    return {flow, totalCost};
}

int MinCostFlow::flowOn(int edgeId) const
{
    return m_edges[static_cast<size_t>(edgeId)].flow;
}

} // namespace fm
