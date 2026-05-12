#include "gor.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <vector>

namespace gro {
namespace {

struct Label {
    QueryId query_id = kInvalidId;
    NodeId current_node = kInvalidId;
    Time arrival_time = 0;
    Cost travel_time = 0;
    std::vector<EdgeId> edge_ids;
    std::vector<char> visited;
};

struct QueueItem {
    Time arrival_time = 0;
    QueryId query_id = kInvalidId;
};

struct QueueCompare {
    bool operator()(const QueueItem& lhs, const QueueItem& rhs) const {
        if (lhs.arrival_time != rhs.arrival_time) {
            return lhs.arrival_time > rhs.arrival_time;
        }
        return lhs.query_id > rhs.query_id;
    }
};

Flow flow_at(const std::map<Time, int>& timeline, Time time) {
    Flow flow = 0;
    for (const auto& [event_time, delta] : timeline) {
        if (event_time > time) {
            break;
        }
        flow += delta;
    }
    return flow;
}

void add_flow_interval(
    std::map<Time, int>& timeline,
    Time enter_time,
    Time exit_time) {
    timeline[enter_time] += 1;
    timeline[exit_time] -= 1;
}

Route make_route(const Query& query, const Label& label) {
    Route route;
    route.query_id = query.id;
    route.edge_ids = label.edge_ids;
    route.departure_time = query.departure_time;
    route.travel_time = label.travel_time;
    return route;
}

}  // namespace

std::vector<Route> compute_gor_greedy_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const TrafficOptions& traffic_options,
    GORGreedyOptions options) {
    const Cost infinity = std::numeric_limits<Cost>::max() / 4;
    std::vector<Route> routes(queries.size());
    std::vector<Label> labels(queries.size());
    std::vector<char> finished(queries.size(), 0);
    std::vector<std::map<Time, int>> edge_flow_events(graph.edges.size());
    std::unordered_map<NodeId, std::vector<Cost>> remaining_cache;

    auto remaining_distances = [&](NodeId destination) -> const std::vector<Cost>& {
        auto it = remaining_cache.find(destination);
        if (it == remaining_cache.end()) {
            it = remaining_cache.emplace(
                destination,
                reverse_shortest_distances(graph, destination)).first;
        }
        return it->second;
    };

    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueCompare> queue;
    for (const Query& query : queries) {
        Label& label = labels[query.id];
        label.query_id = query.id;
        label.current_node = query.origin;
        label.arrival_time = query.departure_time;
        label.visited.assign(graph.vertex_count, 0);
        if (query.origin >= 0 && query.origin < graph.vertex_count) {
            label.visited[query.origin] = 1;
        }
        queue.push(QueueItem{label.arrival_time, query.id});
    }

    while (!queue.empty()) {
        QueueItem item = queue.top();
        queue.pop();
        if (item.query_id < 0 || item.query_id >= static_cast<QueryId>(queries.size())) {
            continue;
        }
        if (finished[item.query_id]) {
            continue;
        }

        const Query& query = queries[item.query_id];
        Label& label = labels[item.query_id];
        if (item.arrival_time != label.arrival_time) {
            continue;
        }
        if (label.current_node == query.destination) {
            routes[query.id] = make_route(query, label);
            finished[query.id] = 1;
            continue;
        }

        const std::vector<Cost>& remaining = remaining_distances(query.destination);
        Cost current_remaining = remaining[label.current_node];

        EdgeId best_edge = kInvalidId;
        Cost best_score = infinity;
        Time best_travel_time = 0;

        auto consider_edges = [&](bool require_progress) {
            for (EdgeId edge_id : graph.outgoing_edges[label.current_node]) {
                const Edge& edge = graph.edges[edge_id];
                if (edge.to < 0 || edge.to >= graph.vertex_count || label.visited[edge.to]) {
                    continue;
                }
                if (remaining[edge.to] == infinity) {
                    continue;
                }
                if (require_progress && remaining[edge.to] >= current_remaining) {
                    continue;
                }

                Flow current_flow = flow_at(edge_flow_events[edge_id], label.arrival_time);
                Cost edge_travel_time =
                    bpr_travel_time(edge, current_flow + 1, traffic_options);
                Cost score = label.travel_time + edge_travel_time + remaining[edge.to];
                if (score < best_score ||
                    (score == best_score && edge_id < best_edge)) {
                    best_score = score;
                    best_edge = edge_id;
                    best_travel_time = static_cast<Time>(edge_travel_time);
                }
            }
        };

        consider_edges(options.require_remaining_progress);
        if (best_edge == kInvalidId && options.require_remaining_progress) {
            consider_edges(false);
        }

        if (best_edge == kInvalidId) {
            routes[query.id] = make_route(query, label);
            finished[query.id] = 1;
            continue;
        }

        const Edge& best = graph.edges[best_edge];
        Time depart_time = label.arrival_time;
        Time next_arrival_time = depart_time + best_travel_time;
        add_flow_interval(edge_flow_events[best_edge], depart_time, next_arrival_time);

        label.edge_ids.push_back(best_edge);
        label.current_node = best.to;
        label.arrival_time = next_arrival_time;
        label.travel_time += best_travel_time;
        label.visited[best.to] = 1;
        queue.push(QueueItem{label.arrival_time, label.query_id});
    }

    for (const Query& query : queries) {
        if (!finished[query.id]) {
            routes[query.id] = make_route(query, labels[query.id]);
        }
    }
    return routes;
}

}  // namespace gro
