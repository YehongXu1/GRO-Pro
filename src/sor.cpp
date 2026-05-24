#include "sor.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gro {
namespace {

using EdgeStep = std::pair<EdgeId, int>;

struct SearchStats {
    int states_expanded = 0;
    bool budget_exhausted = false;
    bool fallback_used = false;
    bool found_route = false;
};

double seconds(long long microseconds) {
    return static_cast<double>(microseconds) / 1000000.0;
}

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
    const SOROptions& options,
    SearchStats* stats = nullptr) {
    if (stats != nullptr) {
        *stats = SearchStats{};
    }

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

    struct Candidate {
        EdgeId edge_id = kInvalidId;
        NodeId to = kInvalidId;
        Time next_time = 0;
        Cost next_travel_time = 0;
        double next_metric = 0.0;
    };

    std::vector<char> visited(graph.vertex_count, 0);
    std::vector<EdgeId> path;
    std::vector<EdgeId> best_path;
    double best_metric = std::numeric_limits<double>::infinity();
    Cost best_travel_time = infinity;
    int states_expanded = 0;
    bool budget_exhausted = false;

    visited[query.origin] = 1;

    std::function<void(NodeId, Time, Cost, double)> dfs =
        [&](NodeId node, Time arrival_time, Cost travel_time, double metric) {
            if (budget_exhausted) {
                return;
            }
            if (options.max_labels_per_query > 0 &&
                states_expanded >= options.max_labels_per_query) {
                budget_exhausted = true;
                return;
            }
            ++states_expanded;

            if (node == query.destination) {
                if (metric < best_metric ||
                    (metric == best_metric && travel_time < best_travel_time)) {
                    best_metric = metric;
                    best_travel_time = travel_time;
                    best_path = path;
                }
                return;
            }

            std::vector<Candidate> candidates;
            candidates.reserve(graph.outgoing_edges[node].size());
            for (EdgeId edge_id : graph.outgoing_edges[node]) {
                const Edge& edge = graph.edges[edge_id];
                if (edge.to < 0 ||
                    edge.to >= graph.vertex_count ||
                    visited[edge.to] ||
                    remaining[edge.to] == infinity) {
                    continue;
                }

                Cost next_travel_time = travel_time + edge.free_flow_time;
                if (next_travel_time + remaining[edge.to] > max_travel_time) {
                    continue;
                }

                double next_metric =
                    metric +
                    edge_metric(graph, volumes, edge_id, arrival_time, lambda, options);
                if (next_metric > best_metric) {
                    continue;
                }
                candidates.push_back(Candidate{
                    edge_id,
                    edge.to,
                    arrival_time + static_cast<Time>(edge.free_flow_time),
                    next_travel_time,
                    next_metric});
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                [&](const Candidate& lhs, const Candidate& rhs) {
                    if (lhs.next_metric != rhs.next_metric) {
                        return lhs.next_metric < rhs.next_metric;
                    }
                    Cost lhs_bound = lhs.next_travel_time + remaining[lhs.to];
                    Cost rhs_bound = rhs.next_travel_time + remaining[rhs.to];
                    if (lhs_bound != rhs_bound) {
                        return lhs_bound < rhs_bound;
                    }
                    return lhs.edge_id < rhs.edge_id;
                });

            for (const Candidate& candidate : candidates) {
                path.push_back(candidate.edge_id);
                visited[candidate.to] = 1;
                dfs(
                    candidate.to,
                    candidate.next_time,
                    candidate.next_travel_time,
                    candidate.next_metric);
                visited[candidate.to] = 0;
                path.pop_back();
                if (budget_exhausted) {
                    return;
                }
            }
        };

    dfs(query.origin, query.departure_time, 0, 0.0);

    if (stats != nullptr) {
        stats->states_expanded = states_expanded;
        stats->budget_exhausted = budget_exhausted;
    }

    if (!best_path.empty() || query.origin == query.destination) {
        Route route;
        route.query_id = query.id;
        route.edge_ids = std::move(best_path);
        route.departure_time = query.departure_time;
        route.travel_time = best_travel_time == infinity ? 0 : best_travel_time;
        if (stats != nullptr) {
            stats->found_route = true;
        }
        return route;
    }

    Route fallback = shortest_path(graph, query);
    if (fallback.travel_time <= max_travel_time) {
        if (stats != nullptr) {
            stats->fallback_used = true;
            stats->found_route = !fallback.edge_ids.empty();
        }
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
    std::unordered_map<EdgeStep, int, PairHash> volumes;
    auto total_start = Clock::now();
    std::cerr << "[sor] route_start"
              << " queries=" << queries.size()
              << " detour_percent=" << options.detour_percent
              << " time_step=" << options.time_step
              << " max_time_steps=" << options.max_time_steps
              << " max_time_sec="
              << (options.time_step * options.max_time_steps)
              << " search_budget=" << options.max_labels_per_query
              << " lower_bound_cache_size=" << options.lower_bound_cache_size
              << " lower_bound_cache_min_frequency="
              << options.lower_bound_cache_min_frequency
              << "\n";

    double lambda = std::numeric_limits<double>::max();
    for (const Edge& edge : graph.edges) {
        lambda = std::min(lambda, 1.0 / static_cast<double>(edge_capacity(edge)));
    }
    if (!std::isfinite(lambda) || lambda <= 0.0) {
        lambda = 1.0;
    }

    std::unordered_map<NodeId, int> destination_frequency;
    destination_frequency.reserve(queries.size());
    for (const Query& query : queries) {
        if (query.destination >= 0 && query.destination < graph.vertex_count) {
            ++destination_frequency[query.destination];
        }
    }

    std::vector<std::pair<NodeId, int>> popular_destinations;
    popular_destinations.reserve(destination_frequency.size());
    int min_cache_frequency = std::max(1, options.lower_bound_cache_min_frequency);
    for (const auto& [destination, frequency] : destination_frequency) {
        if (frequency >= min_cache_frequency) {
            popular_destinations.push_back({destination, frequency});
        }
    }
    std::sort(
        popular_destinations.begin(),
        popular_destinations.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

    std::size_t cache_size = options.lower_bound_cache_size < 0
        ? popular_destinations.size()
        : static_cast<std::size_t>(options.lower_bound_cache_size);
    if (popular_destinations.size() > cache_size) {
        popular_destinations.resize(cache_size);
    }

    std::vector<NodeId> cached_destinations;
    cached_destinations.reserve(popular_destinations.size());
    for (const auto& entry : popular_destinations) {
        cached_destinations.push_back(entry.first);
    }

    std::vector<std::vector<Cost>> cached_distances(cached_destinations.size());
    auto cache_start = Clock::now();
    double estimated_cache_gib =
        static_cast<double>(cached_destinations.size()) *
        static_cast<double>(std::max(0, graph.vertex_count)) *
        static_cast<double>(sizeof(Cost)) /
        (1024.0 * 1024.0 * 1024.0);
    std::cerr << "[sor] lower_bound_cache_start"
              << " unique_destinations=" << destination_frequency.size()
              << " cached_destinations=" << cached_destinations.size()
              << " min_frequency=" << min_cache_frequency
              << " estimated_gib=" << estimated_cache_gib
              << "\n";
    #pragma omp parallel for schedule(dynamic)
    for (long long i = 0; i < static_cast<long long>(cached_destinations.size()); ++i) {
        cached_distances[static_cast<std::size_t>(i)] =
            reverse_shortest_distances(
                graph,
                cached_destinations[static_cast<std::size_t>(i)]);
    }
    std::cerr << "[sor] lower_bound_cache_done"
              << " sec=" << seconds(elapsed_us(cache_start))
              << "\n";

    std::unordered_map<NodeId, std::size_t> cached_index;
    cached_index.reserve(cached_destinations.size());
    for (std::size_t i = 0; i < cached_destinations.size(); ++i) {
        cached_index[cached_destinations[i]] = i;
    }

    std::unordered_map<NodeId, std::vector<Cost>> on_demand_distances;
    on_demand_distances.reserve(
        destination_frequency.size() > cached_destinations.size()
            ? destination_frequency.size() - cached_destinations.size()
            : 0);
    long long uncached_lower_bound_calls = 0;
    auto remaining_distances = [&](NodeId destination) -> const std::vector<Cost>& {
        auto it = cached_index.find(destination);
        if (it != cached_index.end()) {
            return cached_distances[it->second];
        }
        auto on_demand_it = on_demand_distances.find(destination);
        if (on_demand_it != on_demand_distances.end()) {
            return on_demand_it->second;
        }
        ++uncached_lower_bound_calls;
        auto inserted = on_demand_distances.emplace(
            destination,
            reverse_shortest_distances(graph, destination));
        return inserted.first->second;
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

    std::size_t progress_interval = 1;
    if (queries.size() <= 1000) {
        progress_interval = std::max<std::size_t>(1, queries.size() / 10);
    } else {
        progress_interval = std::max<std::size_t>(1000, queries.size() / 100);
    }

    long long total_states_expanded = 0;
    long long budget_exhausted_count = 0;
    long long fallback_count = 0;
    long long empty_route_count = 0;
    std::size_t processed = 0;

    for (QueryId query_id : query_order) {
        const Query& query = queries[query_id];
        const std::vector<Cost>& remaining = remaining_distances(query.destination);
        SearchStats search_stats;
        Route route = find_min_metric_route(
            graph,
            query,
            remaining,
            volumes,
            lambda,
            options,
            &search_stats);
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
                options,
                &search_stats);
            metric = route_metric(graph, route, volumes, lambda, options);
        }

        total_states_expanded += search_stats.states_expanded;
        if (search_stats.budget_exhausted) {
            ++budget_exhausted_count;
        }
        if (search_stats.fallback_used) {
            ++fallback_count;
        }
        if (!search_stats.found_route && route.edge_ids.empty()) {
            ++empty_route_count;
        }

        insert_route(graph, route, volumes, options);
        routes[query.id] = std::move(route);

        ++processed;
        if (processed == queries.size() ||
            processed % progress_interval == 0) {
            double avg_states = processed == 0
                ? 0.0
                : static_cast<double>(total_states_expanded) /
                      static_cast<double>(processed);
            std::cerr << "[sor] route_progress"
                      << " processed=" << processed
                      << "/" << queries.size()
                      << " elapsed_sec=" << seconds(elapsed_us(total_start))
                      << " avg_states=" << avg_states
                      << " budget_exhausted=" << budget_exhausted_count
                      << " fallback=" << fallback_count
                      << " empty=" << empty_route_count
                      << " uncached_lower_bound_calls="
                      << uncached_lower_bound_calls
                      << " lambda=" << lambda
                      << " volume_cells=" << volumes.size()
                      << "\n";
        }
    }

    std::cerr << "[sor] route_done"
              << " queries=" << queries.size()
              << " sec=" << seconds(elapsed_us(total_start))
              << " avg_states="
              << (queries.empty()
                      ? 0.0
                      : static_cast<double>(total_states_expanded) /
                            static_cast<double>(queries.size()))
              << " budget_exhausted=" << budget_exhausted_count
              << " fallback=" << fallback_count
              << " empty=" << empty_route_count
              << " uncached_lower_bound_calls="
              << uncached_lower_bound_calls
              << " volume_cells=" << volumes.size()
              << "\n";
    return routes;
}

}  // namespace gro
