#include "svp.hpp"
#include "data_structures.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gro {
namespace {

double seconds(long long microseconds) {
    return static_cast<double>(microseconds) / 1000000.0;
}

struct SVPSearchStats {
    long long reachable_vias = 0;
    long long candidates_examined = 0;
    long long duplicate_candidates = 0;
    long long non_simple_candidates = 0;
    long long overlap_rejected = 0;
    long long accepted = 0;
    bool fallback_used = false;
};

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

std::vector<Route> svp_routes_impl(
    const Graph& graph,
    const Query& query,
    int k,
    int theta_percent,
    SVPSearchStats* stats) {
    if (stats != nullptr) {
        *stats = SVPSearchStats{};
    }

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

    struct CandidateRef {
        Cost length = 0;
        NodeId via = kInvalidId;
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

    std::vector<CandidateRef> candidate_order;
    candidate_order.reserve(vertex_count);
    for (NodeId node_id = 0; node_id < vertex_count; ++node_id) {
        if (forward_dist[node_id] == infinity || backward_dist[node_id] == infinity) {
            continue;
        }
        candidate_order.push_back(CandidateRef{
            forward_dist[node_id] + backward_dist[node_id],
            node_id});
    }
    if (stats != nullptr) {
        stats->reachable_vias = static_cast<long long>(candidate_order.size());
    }

    std::sort(
        candidate_order.begin(),
        candidate_order.end(),
        [](const CandidateRef& lhs, const CandidateRef& rhs) {
            if (lhs.length != rhs.length) {
                return lhs.length < rhs.length;
            }
            return lhs.via < rhs.via;
        });

    std::vector<Route> accepted;
    accepted.reserve(static_cast<std::size_t>(k));
    std::set<std::vector<EdgeId>> seen_edge_sets;
    for (const CandidateRef& candidate_ref : candidate_order) {
        Candidate candidate = build_candidate(candidate_ref.via);
        candidate.length = candidate_ref.length;
        if (stats != nullptr) {
            ++stats->candidates_examined;
        }

        if (candidate.edge_ids.empty() ||
            !is_simple_route(graph, query.origin, candidate.edge_ids)) {
            if (stats != nullptr) {
                ++stats->non_simple_candidates;
            }
            continue;
        }

        if (seen_edge_sets.find(candidate.edge_ids) != seen_edge_sets.end()) {
            if (stats != nullptr) {
                ++stats->duplicate_candidates;
            }
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
            if (stats != nullptr) {
                ++stats->overlap_rejected;
            }
            continue;
        }

        seen_edge_sets.insert(route.edge_ids);
        accepted.push_back(std::move(route));
        if (stats != nullptr) {
            stats->accepted = static_cast<long long>(accepted.size());
        }
        if (accepted.size() == static_cast<size_t>(k)) {
            break;
        }
    }

    if (accepted.empty()) {
        if (stats != nullptr) {
            stats->fallback_used = true;
        }
        accepted.push_back(shortest_path(graph, query));
    }
    return accepted;
}

}  // namespace

std::vector<Route> svp_routes(
    const Graph& graph,
    const Query& query,
    int k,
    int theta_percent) {
    return svp_routes_impl(graph, query, k, theta_percent, nullptr);
}

std::vector<Route> compute_svp_baseline_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    SVPOptions options) {
    std::vector<Route> routes(queries.size());
    std::vector<std::pair<NodeId, NodeId>> od_order;
    std::unordered_map<std::pair<NodeId, NodeId>, std::size_t, PairHash> od_index;
    od_order.reserve(queries.size());
    od_index.reserve(queries.size());

    int k = std::max(1, options.k);
    int theta_percent = std::clamp(options.theta, 0, 100);

    std::unordered_set<NodeId> unique_origins;
    std::unordered_set<NodeId> unique_destinations;
    unique_origins.reserve(queries.size());
    unique_destinations.reserve(queries.size());
    for (const Query& query : queries) {
        std::pair<NodeId, NodeId> od = {query.origin, query.destination};
        if (od_index.find(od) == od_index.end()) {
            od_index[od] = od_order.size();
            od_order.push_back(od);
        }
        unique_origins.insert(query.origin);
        unique_destinations.insert(query.destination);
    }

    std::vector<std::vector<Route>> alternatives_by_od(od_order.size());
    std::vector<SVPSearchStats> stats_by_od(od_order.size());
    auto total_start = Clock::now();
    std::cerr << "[svp] route_start"
              << " queries=" << queries.size()
              << " unique_od=" << od_order.size()
              << " unique_origins=" << unique_origins.size()
              << " unique_destinations=" << unique_destinations.size()
              << " k=" << k
              << " theta=" << theta_percent
              << " mode=lazy_single_via\n";

    std::size_t progress_interval = od_order.empty()
        ? 1
        : std::max<std::size_t>(1, od_order.size() / 20);
    std::size_t processed_ods = 0;

    #pragma omp parallel for schedule(dynamic)
    for (long long index = 0; index < static_cast<long long>(od_order.size()); ++index) {
        Query representative;
        representative.id = 0;
        representative.origin = od_order[static_cast<std::size_t>(index)].first;
        representative.destination = od_order[static_cast<std::size_t>(index)].second;
        representative.departure_time = 0;
        alternatives_by_od[static_cast<std::size_t>(index)] =
            svp_routes_impl(
                graph,
                representative,
                k,
                theta_percent,
                &stats_by_od[static_cast<std::size_t>(index)]);

        std::size_t done = 0;
        #pragma omp atomic capture
        done = ++processed_ods;
        if (done == od_order.size() || done % progress_interval == 0) {
            #pragma omp critical(svp_progress_log)
            {
                std::cerr << "[svp] od_progress"
                          << " processed_od=" << done
                          << "/" << od_order.size()
                          << " elapsed_sec=" << seconds(elapsed_us(total_start))
                          << "\n";
            }
        }
    }

    long long reachable_vias = 0;
    long long candidates_examined = 0;
    long long duplicate_candidates = 0;
    long long non_simple_candidates = 0;
    long long overlap_rejected = 0;
    long long accepted = 0;
    long long fallback_od = 0;
    long long max_candidates_examined = 0;
    for (const SVPSearchStats& stats : stats_by_od) {
        reachable_vias += stats.reachable_vias;
        candidates_examined += stats.candidates_examined;
        duplicate_candidates += stats.duplicate_candidates;
        non_simple_candidates += stats.non_simple_candidates;
        overlap_rejected += stats.overlap_rejected;
        accepted += stats.accepted;
        fallback_od += stats.fallback_used ? 1 : 0;
        max_candidates_examined =
            std::max(max_candidates_examined, stats.candidates_examined);
    }
    double divisor = od_order.empty() ? 1.0 : static_cast<double>(od_order.size());
    std::cerr << "[svp] alternatives_done"
              << " sec=" << seconds(elapsed_us(total_start))
              << " avg_reachable_vias=" << (reachable_vias / divisor)
              << " avg_candidates_examined=" << (candidates_examined / divisor)
              << " max_candidates_examined=" << max_candidates_examined
              << " accepted_routes=" << accepted
              << " fallback_od=" << fallback_od
              << " duplicate_candidates=" << duplicate_candidates
              << " non_simple_candidates=" << non_simple_candidates
              << " overlap_rejected=" << overlap_rejected
              << "\n";

    std::vector<std::size_t> next_alternative_by_od(od_order.size(), 0);
    for (const Query& query : queries) {
        std::pair<NodeId, NodeId> od = {query.origin, query.destination};
        std::size_t index = od_index.at(od);
        const std::vector<Route>& alternatives = alternatives_by_od[index];
        size_t& next_index = next_alternative_by_od[index];
        const Route& selected = alternatives[next_index % alternatives.size()];
        ++next_index;

        Route route = selected;
        route.query_id = query.id;
        route.departure_time = query.departure_time;
        route.travel_time = route_edge_length(graph, route.edge_ids);
        routes[query.id] = std::move(route);
    }

    std::cerr << "[svp] route_done"
              << " queries=" << queries.size()
              << " unique_od=" << od_order.size()
              << " sec=" << seconds(elapsed_us(total_start))
              << "\n";
    return routes;
}

}  // namespace gro
