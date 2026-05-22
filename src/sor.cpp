#include "sor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gro {
namespace {

using EdgeStep = std::pair<EdgeId, int>;

struct SearchLabel {
    NodeId node = kInvalidId;
    Time arrival_time = 0;
    Cost travel_time = 0;
    double metric = 0.0;
    std::vector<EdgeId> edge_ids;
    std::vector<char> visited;
};

struct LabelCompare {
    bool operator()(const SearchLabel& lhs, const SearchLabel& rhs) const {
        if (lhs.metric != rhs.metric) {
            return lhs.metric > rhs.metric;
        }
        return lhs.travel_time > rhs.travel_time;
    }
};

Flow edge_capacity(const Edge& edge) {
    return edge.capacity > 0 ? edge.capacity : 1;
}

std::vector<int> occupied_steps(
    Time enter_time,
    Cost travel_time,
    Time time_step) {
    Time safe_step = time_step > 0 ? time_step : 1;
    Time exit_time = enter_time + static_cast<Time>(std::max<Cost>(1, travel_time));
    int first_step = static_cast<int>(enter_time / safe_step);
    int last_step = static_cast<int>((exit_time - 1) / safe_step);

    std::vector<int> steps;
    steps.reserve(static_cast<size_t>(last_step - first_step + 1));
    for (int step = first_step; step <= last_step; ++step) {
        steps.push_back(step);
    }
    return steps;
}

double variable_value(
    const Graph& graph,
    const std::unordered_map<EdgeStep, int, PairHash>& volumes,
    EdgeId edge_id,
    int step,
    double lambda,
    int max_time_steps) {
    const Edge& edge = graph.edges[edge_id];
    double capacity = static_cast<double>(edge_capacity(edge));
    int horizon = std::max(1, max_time_steps);
    double denominator =
        2.0 * static_cast<double>(horizon) *
        static_cast<double>(std::max(1, graph.edge_count)) *
        capacity;
    double base = 1.0 + 1.0 / (2.0 * lambda * capacity);

    int volume = 0;
    auto volume_it = volumes.find({edge_id, step});
    if (volume_it != volumes.end()) {
        volume = volume_it->second;
    }

    return std::pow(base, volume) / denominator;
}

double edge_metric(
    const Graph& graph,
    const std::unordered_map<EdgeStep, int, PairHash>& volumes,
    EdgeId edge_id,
    Time enter_time,
    double lambda,
    const SOROptions& options) {
    double metric = 0.0;
    for (int step : occupied_steps(
             enter_time,
             graph.edges[edge_id].free_flow_time,
             options.time_step)) {
        metric += variable_value(
            graph,
            volumes,
            edge_id,
            step,
            lambda,
            options.max_time_steps);
    }
    return metric;
}

bool variable_bound_exceeded(
    const Graph& graph,
    const std::unordered_map<EdgeStep, int, PairHash>& volumes,
    double lambda,
    const SOROptions& options) {
    for (const auto& [edge_step, _] : volumes) {
        EdgeId edge_id = edge_step.first;
        int step = edge_step.second;
        double capacity = static_cast<double>(edge_capacity(graph.edges[edge_id]));
        double bound = std::exp(0.5) / capacity;
        if (variable_value(
                graph,
                volumes,
                edge_id,
                step,
                lambda,
                options.max_time_steps) > bound) {
            return true;
        }
    }
    return false;
}

Route find_min_metric_route(
    const Graph& graph,
    const Query& query,
    const std::vector<Cost>& remaining,
    const std::unordered_map<EdgeStep, int, PairHash>& volumes,
    double lambda,
    const SOROptions& options) {
    const Cost infinity = std::numeric_limits<Cost>::max() / 4;
    Route empty_route;
    empty_route.query_id = query.id;
    empty_route.departure_time = query.departure_time;

    if (remaining[query.origin] == infinity) {
        return empty_route;
    }

    Cost max_travel_time =
        remaining[query.origin] *
        static_cast<Cost>(100 + std::max(0, options.detour_percent)) / 100;

    std::priority_queue<SearchLabel, std::vector<SearchLabel>, LabelCompare> queue;
    SearchLabel start;
    start.node = query.origin;
    start.arrival_time = query.departure_time;
    start.visited.assign(graph.vertex_count, 0);
    start.visited[query.origin] = 1;
    queue.push(std::move(start));

    int labels_expanded = 0;
    while (!queue.empty()) {
        SearchLabel label = queue.top();
        queue.pop();
        ++labels_expanded;
        if (options.max_labels_per_query > 0 &&
            labels_expanded > options.max_labels_per_query) {
            break;
        }

        if (label.node == query.destination) {
            Route route;
            route.query_id = query.id;
            route.edge_ids = std::move(label.edge_ids);
            route.departure_time = query.departure_time;
            route.travel_time = label.travel_time;
            return route;
        }

        for (EdgeId edge_id : graph.outgoing_edges[label.node]) {
            const Edge& edge = graph.edges[edge_id];
            if (edge.to < 0 ||
                edge.to >= graph.vertex_count ||
                label.visited[edge.to] ||
                remaining[edge.to] == infinity) {
                continue;
            }

            Cost next_travel_time = label.travel_time + edge.free_flow_time;
            if (next_travel_time + remaining[edge.to] > max_travel_time) {
                continue;
            }

            SearchLabel next = label;
            next.node = edge.to;
            next.arrival_time += static_cast<Time>(edge.free_flow_time);
            next.travel_time = next_travel_time;
            next.metric += edge_metric(
                graph,
                volumes,
                edge_id,
                label.arrival_time,
                lambda,
                options);
            next.edge_ids.push_back(edge_id);
            next.visited[edge.to] = 1;
            queue.push(std::move(next));
        }
    }

    Route fallback = shortest_path(graph, query);
    if (fallback.travel_time <= max_travel_time) {
        return fallback;
    }
    return empty_route;
}

double route_metric(
    const Graph& graph,
    const Route& route,
    const std::unordered_map<EdgeStep, int, PairHash>& volumes,
    double lambda,
    const SOROptions& options) {
    double metric = 0.0;
    Time time = route.departure_time;
    for (EdgeId edge_id : route.edge_ids) {
        metric += edge_metric(graph, volumes, edge_id, time, lambda, options);
        time += static_cast<Time>(graph.edges[edge_id].free_flow_time);
    }
    return metric;
}

void insert_route(
    const Graph& graph,
    const Route& route,
    std::unordered_map<EdgeStep, int, PairHash>& volumes,
    const SOROptions& options) {
    Time time = route.departure_time;
    for (EdgeId edge_id : route.edge_ids) {
        for (int step : occupied_steps(
                 time,
                 graph.edges[edge_id].free_flow_time,
                 options.time_step)) {
            ++volumes[{edge_id, step}];
        }
        time += static_cast<Time>(graph.edges[edge_id].free_flow_time);
    }
}

}  // namespace

std::vector<Route> compute_sor_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    SOROptions options) {
    std::vector<Route> routes(queries.size());
    std::unordered_map<NodeId, std::vector<Cost>> remaining_cache;
    std::unordered_map<EdgeStep, int, PairHash> volumes;

    double lambda = std::numeric_limits<double>::max();
    for (const Edge& edge : graph.edges) {
        lambda = std::min(lambda, 1.0 / static_cast<double>(edge_capacity(edge)));
    }
    if (!std::isfinite(lambda) || lambda <= 0.0) {
        lambda = 1.0;
    }

    auto remaining_distances = [&](NodeId destination) -> const std::vector<Cost>& {
        auto it = remaining_cache.find(destination);
        if (it == remaining_cache.end()) {
            it = remaining_cache.emplace(
                destination,
                reverse_shortest_distances(graph, destination)).first;
        }
        return it->second;
    };

    std::vector<QueryId> query_order(queries.size());
    std::iota(query_order.begin(), query_order.end(), 0);
    std::sort(
        query_order.begin(),
        query_order.end(),
        [&](QueryId lhs, QueryId rhs) {
            if (queries[lhs].departure_time != queries[rhs].departure_time) {
                return queries[lhs].departure_time < queries[rhs].departure_time;
            }
            return lhs < rhs;
        });

    for (QueryId query_id : query_order) {
        const Query& query = queries[query_id];
        const std::vector<Cost>& remaining = remaining_distances(query.destination);
        Route route = find_min_metric_route(
            graph,
            query,
            remaining,
            volumes,
            lambda,
            options);
        double metric = route_metric(graph, route, volumes, lambda, options);

        while ((metric > lambda ||
                variable_bound_exceeded(graph, volumes, lambda, options)) &&
               lambda < std::numeric_limits<double>::max() / 4.0) {
            lambda *= 2.0;
            route = find_min_metric_route(
                graph,
                query,
                remaining,
                volumes,
                lambda,
                options);
            metric = route_metric(graph, route, volumes, lambda, options);
        }

        insert_route(graph, route, volumes, options);
        routes[query.id] = std::move(route);
    }

    return routes;
}

}  // namespace gro
