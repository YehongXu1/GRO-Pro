#include "svp.hpp"
#include "data_structures.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gro {
namespace {

Cost shared_edge_length(
    const Graph& graph,
    const std::vector<EdgeId>& lhs,
    const std::vector<EdgeId>& rhs) {
    std::unordered_set<EdgeId> lhs_edges(lhs.begin(), lhs.end());
    Cost shared = 0;
    for (EdgeId edge_id : rhs) {
        if (lhs_edges.find(edge_id) != lhs_edges.end() &&
            edge_id >= 0 &&
            edge_id < static_cast<EdgeId>(graph.edges.size())) {
            shared += graph.edges[edge_id].free_flow_time;
        }
    }
    return shared;
}

bool overlap_within_threshold(
    const Graph& graph,
    const Route& candidate,
    const Route& accepted,
    int theta_percent) {
    Cost candidate_length = candidate.travel_time > 0
        ? candidate.travel_time
        : route_edge_length(graph, candidate.edge_ids);
    Cost accepted_length = accepted.travel_time > 0
        ? accepted.travel_time
        : route_edge_length(graph, accepted.edge_ids);
    Cost denominator = std::min(candidate_length, accepted_length);
    if (denominator <= 0) {
        return true;
    }

    Cost shared = shared_edge_length(graph, candidate.edge_ids, accepted.edge_ids);
    return shared * 100 <= denominator * std::clamp(theta_percent, 0, 100);
}

bool is_simple_route(const Graph& graph, NodeId origin, const std::vector<EdgeId>& edge_ids) {
    std::unordered_set<NodeId> seen;
    NodeId current = origin;
    seen.insert(current);
    for (EdgeId edge_id : edge_ids) {
        if (edge_id < 0 || edge_id >= static_cast<EdgeId>(graph.edges.size())) {
            return false;
        }
        const Edge& edge = graph.edges[edge_id];
        if (edge.from != current) {
            return false;
        }
        current = edge.to;
        if (!seen.insert(current).second) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Route> svp_routes(
    const Graph& graph,
    const Query& query,
    int k,
    int theta_percent) {
    Route empty_route;
    empty_route.query_id = query.id;
    empty_route.departure_time = query.departure_time;

    if (k <= 0) {
        return {};
    }
    if (query.origin == query.destination) {
        return {empty_route};
    }

    struct Candidate {
        Cost length = 0;
        NodeId via = kInvalidId;
        std::vector<EdgeId> edge_ids;
    };

    const Cost infinity = std::numeric_limits<Cost>::max() / 4;
    const int vertex_count = graph.vertex_count;

    std::vector<Cost> forward_dist(vertex_count, infinity);
    std::vector<EdgeId> forward_parent(vertex_count, kInvalidId);
    std::vector<char> forward_settled(vertex_count, 0);
    data_structures::IndexedHeap<4, Cost, NodeId> forward_heap(vertex_count);

    forward_dist[query.origin] = 0;
    forward_heap.push_or_update(query.origin, 0);
    while (!forward_heap.empty()) {
        auto element = forward_heap.extract_min();
        NodeId node_id = element.id;
        if (forward_settled[node_id]) {
            continue;
        }
        forward_settled[node_id] = 1;

        for (EdgeId edge_id : graph.outgoing_edges[node_id]) {
            const Edge& edge = graph.edges[edge_id];
            Cost next_dist = forward_dist[node_id] + edge.free_flow_time;
            if (next_dist < forward_dist[edge.to]) {
                forward_dist[edge.to] = next_dist;
                forward_parent[edge.to] = edge_id;
                forward_heap.push_or_update(edge.to, next_dist);
            }
        }
    }

    std::vector<Cost> backward_dist(vertex_count, infinity);
    std::vector<EdgeId> backward_next(vertex_count, kInvalidId);
    std::vector<char> backward_settled(vertex_count, 0);
    data_structures::IndexedHeap<4, Cost, NodeId> backward_heap(vertex_count);

    backward_dist[query.destination] = 0;
    backward_heap.push_or_update(query.destination, 0);
    while (!backward_heap.empty()) {
        auto element = backward_heap.extract_min();
        NodeId node_id = element.id;
        if (backward_settled[node_id]) {
            continue;
        }
        backward_settled[node_id] = 1;

        for (EdgeId edge_id : graph.incoming_edges[node_id]) {
            const Edge& edge = graph.edges[edge_id];
            Cost next_dist = backward_dist[node_id] + edge.free_flow_time;
            if (next_dist < backward_dist[edge.from]) {
                backward_dist[edge.from] = next_dist;
                backward_next[edge.from] = edge_id;
                backward_heap.push_or_update(edge.from, next_dist);
            }
        }
    }

    if (forward_dist[query.destination] == infinity) {
        return {empty_route};
    }

    auto build_candidate = [&](NodeId via) {
        Candidate candidate;
        candidate.via = via;
        candidate.length = forward_dist[via] + backward_dist[via];

        std::vector<EdgeId> prefix;
        for (NodeId current = via; current != query.origin; ) {
            EdgeId edge_id = forward_parent[current];
            if (edge_id == kInvalidId) {
                candidate.edge_ids.clear();
                return candidate;
            }
            prefix.push_back(edge_id);
            current = graph.edges[edge_id].from;
        }
        candidate.edge_ids.assign(prefix.rbegin(), prefix.rend());

        for (NodeId current = via; current != query.destination; ) {
            EdgeId edge_id = backward_next[current];
            if (edge_id == kInvalidId) {
                candidate.edge_ids.clear();
                return candidate;
            }
            candidate.edge_ids.push_back(edge_id);
            current = graph.edges[edge_id].to;
        }
        return candidate;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(vertex_count);
    for (NodeId node_id = 0; node_id < vertex_count; ++node_id) {
        if (forward_dist[node_id] == infinity || backward_dist[node_id] == infinity) {
            continue;
        }
        Candidate candidate = build_candidate(node_id);
        if (candidate.edge_ids.empty() ||
            !is_simple_route(graph, query.origin, candidate.edge_ids)) {
            continue;
        }
        candidates.push_back(std::move(candidate));
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.length != rhs.length) {
                return lhs.length < rhs.length;
            }
            return lhs.via < rhs.via;
        });

    std::vector<Route> accepted;
    std::set<std::vector<EdgeId>> seen_edge_sets;
    for (const Candidate& candidate : candidates) {
        if (seen_edge_sets.find(candidate.edge_ids) != seen_edge_sets.end()) {
            continue;
        }

        Route route;
        route.query_id = query.id;
        route.edge_ids = candidate.edge_ids;
        route.departure_time = query.departure_time;
        route.travel_time = candidate.length;

        bool diverse = true;
        for (const Route& existing : accepted) {
            if (!overlap_within_threshold(graph, route, existing, theta_percent)) {
                diverse = false;
                break;
            }
        }
        if (!diverse) {
            continue;
        }

        seen_edge_sets.insert(route.edge_ids);
        accepted.push_back(std::move(route));
        if (accepted.size() == static_cast<size_t>(k)) {
            break;
        }
    }

    if (accepted.empty()) {
        accepted.push_back(shortest_path(graph, query));
    }
    return accepted;
}

std::vector<Route> compute_svp_baseline_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    SVPOptions options) {
    std::vector<Route> routes(queries.size());
    std::unordered_map<std::pair<NodeId, NodeId>, std::vector<Route>, PairHash> alternatives_by_od;
    std::unordered_map<std::pair<NodeId, NodeId>, size_t, PairHash> next_alternative_by_od; // for round-robin selection among alternatives

    int k = std::max(1, options.k);
    int theta_percent = std::clamp(options.theta, 0, 100);

    for (const Query& query : queries) {
        std::pair<NodeId, NodeId> od = {query.origin, query.destination};
        auto alternatives_it = alternatives_by_od.find(od);
        if (alternatives_it == alternatives_by_od.end()) {
            alternatives_it = alternatives_by_od.emplace(
                od,
                svp_routes(graph, query, k, theta_percent)).first;
        }

        const std::vector<Route>& alternatives = alternatives_it->second;
        size_t& next_index = next_alternative_by_od[od];
        const Route& selected = alternatives[next_index % alternatives.size()];
        ++next_index;

        Route route = selected;
        route.query_id = query.id;
        route.departure_time = query.departure_time;
        route.travel_time = route_edge_length(graph, route.edge_ids);
        routes[query.id] = std::move(route);
    }

    return routes;
}

}  // namespace gro
