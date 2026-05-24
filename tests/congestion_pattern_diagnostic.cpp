#include "gro.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Options {
    std::string config_path = "config/config.yaml";
    std::filesystem::path query_file;
};

struct DSU {
    explicit DSU(std::size_t n) : parent(n), rank(n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    std::size_t find(std::size_t x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }
        return parent[x];
    }

    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (rank[a] < rank[b]) {
            std::swap(a, b);
        }
        parent[b] = a;
        if (rank[a] == rank[b]) {
            ++rank[a];
        }
    }

    std::vector<std::size_t> parent;
    std::vector<int> rank;
};

struct ComponentStats {
    long double mass = 0.0L;
    long long node_count = 0;
    long long query_count = 0;
};

struct QueryStats {
    gro::QueryId query_id = gro::kInvalidId;
    long double score = 0.0L;
    int component_count = 0;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Options parse_args(int argc, char** argv) {
    Options options;
    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.config_path = argv[index++];
    }

    auto require_value = [&](const std::string& flag) {
        if (index >= argc) {
            throw std::runtime_error("Missing value after " + flag);
        }
        return std::string(argv[index++]);
    };

    while (index < argc) {
        std::string arg = argv[index++];
        if (arg == "--query-file") {
            options.query_file = require_value(arg);
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./congestion_pattern_diagnostic [config] "
                << "[--query-file path]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return options;
}

long double percentile(std::vector<long double> values, int percentile_value) {
    if (values.empty()) {
        return 0.0L;
    }
    std::sort(values.begin(), values.end());
    int p = std::clamp(percentile_value, 0, 100);
    std::size_t index =
        static_cast<std::size_t>(
            static_cast<long long>(p) *
            static_cast<long long>(values.size() - 1) /
            100);
    return values[index];
}

long double top_share(std::vector<long double> values, std::size_t top_k) {
    if (values.empty() || top_k == 0) {
        return 0.0L;
    }
    std::sort(values.begin(), values.end(), std::greater<long double>());
    long double total =
        std::accumulate(values.begin(), values.end(), 0.0L);
    if (total <= 0.0L) {
        return 0.0L;
    }
    top_k = std::min(top_k, values.size());
    long double top =
        std::accumulate(values.begin(), values.begin() + top_k, 0.0L);
    return top / total;
}

std::size_t count_to_cover(
    const std::vector<QueryStats>& sorted_queries,
    long double target_fraction,
    long double total_score) {
    if (total_score <= 0.0L) {
        return 0;
    }
    long double target = total_score * target_fraction;
    long double covered = 0.0L;
    for (std::size_t i = 0; i < sorted_queries.size(); ++i) {
        covered += sorted_queries[i].score;
        if (covered >= target) {
            return i + 1;
        }
    }
    return sorted_queries.size();
}

long double prefix_query_score_share(
    const std::vector<QueryStats>& sorted_queries,
    std::size_t prefix_count,
    long double total_score) {
    if (total_score <= 0.0L || sorted_queries.empty()) {
        return 0.0L;
    }
    prefix_count = std::min(prefix_count, sorted_queries.size());
    long double covered = 0.0L;
    for (std::size_t i = 0; i < prefix_count; ++i) {
        covered += sorted_queries[i].score;
    }
    return covered / total_score;
}

long double prefix_component_mass_share(
    const std::vector<QueryStats>& sorted_queries,
    const std::vector<std::vector<int>>& query_components,
    const std::vector<ComponentStats>& components,
    std::size_t prefix_count,
    long double total_component_mass) {
    if (total_component_mass <= 0.0L || sorted_queries.empty()) {
        return 0.0L;
    }
    prefix_count = std::min(prefix_count, sorted_queries.size());
    std::vector<char> seen(components.size(), 0);
    long double covered = 0.0L;
    for (std::size_t i = 0; i < prefix_count; ++i) {
        gro::QueryId query_id = sorted_queries[i].query_id;
        if (query_id < 0 ||
            query_id >= static_cast<gro::QueryId>(query_components.size())) {
            continue;
        }
        for (int component_id : query_components[query_id]) {
            if (component_id < 0 ||
                component_id >= static_cast<int>(components.size()) ||
                seen[component_id]) {
                continue;
            }
            seen[component_id] = 1;
            covered += components[component_id].mass;
        }
    }
    return covered / total_component_mass;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_args(argc, argv);

        gro::InputConfig input = gro::load_input_config(options.config_path);
        gro::Graph graph = gro::read_graph(input);
        gro::AlgorithmOptions algorithm_options =
            gro::load_algorithm_options(options.config_path);
        gro::TrafficOptions traffic_options =
            gro::load_traffic_options(options.config_path);
        gro::GROAlgorithm algorithm(graph, algorithm_options, traffic_options);

        std::filesystem::path query_path = options.query_file.empty()
            ? std::filesystem::path(input.queries_path)
            : options.query_file;
        std::vector<gro::Query> queries =
            gro::read_queries(query_path.string());
        require(!queries.empty(), "query file is empty: " + query_path.string());

        auto start = gro::Clock::now();
        std::vector<gro::Route> routes =
            algorithm.compute_initial_routes(queries);
        long long route_us = gro::elapsed_us(start);

        start = gro::Clock::now();
        gro::TrafficResult traffic_result =
            gro::evaluate_traffic(graph, queries, routes, traffic_options);
        long long evaluate_us = gro::elapsed_us(start);

        start = gro::Clock::now();
        gro::TrafficDependencyGraph tdg =
            algorithm.build_tdg(traffic_result);
        long long build_tdg_us = gro::elapsed_us(start);

        start = gro::Clock::now();
        std::vector<gro::Cost> raw_impacts =
            algorithm.compute_tdg_impact(tdg);
        long long impact_us = gro::elapsed_us(start);

        std::vector<double> selection_impacts =
            algorithm.normalize_tdg_impacts_for_selection(raw_impacts);
        std::vector<std::map<gro::Time, gro::Cost>> anchor_scores =
            algorithm.compute_anchor_scores(traffic_result);
        std::vector<char> important =
            algorithm.mark_anchor_tdg_nodes(tdg, anchor_scores);

        bool has_important =
            std::any_of(important.begin(), important.end(), [](char value) {
                return value != 0;
            });
        if (!has_important) {
            for (gro::TDGNodeId node_id = 0;
                 node_id < static_cast<gro::TDGNodeId>(tdg.nodes.size());
                 ++node_id) {
                const gro::TDGNode& node = tdg.nodes[node_id];
                gro::Flow capacity =
                    std::max<gro::Flow>(
                        1,
                        graph.edges[node.edge_id].capacity);
                if (node.flow > capacity) {
                    important[node_id] = 1;
                }
            }
        }

        std::vector<long double> relief(tdg.nodes.size(), 0.0L);
        std::vector<char> active(tdg.nodes.size(), 0);
        std::vector<long double> edge_mass(graph.edges.size(), 0.0L);
        long double total_relief_mass = 0.0L;
        long long overloaded_nodes = 0;
        long long important_nodes = 0;
        long long active_nodes = 0;

        for (gro::TDGNodeId node_id = 0;
             node_id < static_cast<gro::TDGNodeId>(tdg.nodes.size());
             ++node_id) {
            const gro::TDGNode& node = tdg.nodes[node_id];
            gro::Flow capacity =
                std::max<gro::Flow>(1, graph.edges[node.edge_id].capacity);
            if (node.flow > capacity) {
                ++overloaded_nodes;
            }
            if (important[node_id]) {
                ++important_nodes;
            }
            if (!important[node_id] ||
                node_id >= static_cast<gro::TDGNodeId>(selection_impacts.size()) ||
                selection_impacts[node_id] <= 0.0 ||
                node.flow <= capacity) {
                continue;
            }

            long double excess =
                static_cast<long double>(node.flow - capacity) /
                static_cast<long double>(capacity);
            relief[node_id] =
                static_cast<long double>(selection_impacts[node_id]) *
                excess;
            if (relief[node_id] <= 0.0L) {
                continue;
            }
            active[node_id] = 1;
            ++active_nodes;
            total_relief_mass += relief[node_id];
            edge_mass[node.edge_id] += relief[node_id];
        }

        std::unordered_set<gro::QueryId> source_candidates =
            algorithm.select_candidates(
                queries,
                traffic_result,
                tdg,
                raw_impacts);
        std::unordered_set<gro::QueryId> score_top_candidates =
            algorithm.select_candidates_by_score(
                queries,
                traffic_result,
                tdg,
                raw_impacts);
        std::unordered_set<gro::QueryId> component_balanced_candidates =
            algorithm.select_candidates_by_component_balance(
                queries,
                traffic_result,
                tdg,
                raw_impacts);
        std::unordered_set<gro::QueryId> component_marginal_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts);
        std::size_t budget5 = static_cast<std::size_t>(
            std::ceil(static_cast<double>(queries.size()) * 0.05));
        std::size_t budget3 = static_cast<std::size_t>(
            std::ceil(static_cast<double>(queries.size()) * 0.03));
        std::unordered_set<gro::QueryId> component_marginal_budget5_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                budget5);
        std::unordered_set<gro::QueryId> component_marginal_budget3_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                budget3);
        std::unordered_set<gro::QueryId> component_marginal_samek_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                score_top_candidates.size());
        std::unordered_set<gro::QueryId> component_marginal_major80_budget5_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                budget5,
                80);
        std::unordered_set<gro::QueryId> component_marginal_major90_budget5_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                budget5,
                90);
        std::unordered_set<gro::QueryId> component_marginal_major90_samek_candidates =
            algorithm.select_candidates_by_component_marginal(
                queries,
                traffic_result,
                tdg,
                raw_impacts,
                score_top_candidates.size(),
                90);

        DSU dsu(tdg.nodes.size());
        for (gro::TDGNodeId node_id = 0;
             node_id < static_cast<gro::TDGNodeId>(tdg.nodes.size());
             ++node_id) {
            if (!active[node_id]) {
                continue;
            }
            for (gro::TDGNodeId child_id : tdg.route_outgoing[node_id]) {
                if (child_id >= 0 &&
                    child_id < static_cast<gro::TDGNodeId>(active.size()) &&
                    active[child_id]) {
                    dsu.unite(
                        static_cast<std::size_t>(node_id),
                        static_cast<std::size_t>(child_id));
                }
            }

            const gro::TDGNode& node = tdg.nodes[node_id];
            const auto& timeline = tdg.edge_timelines[node.edge_id];
            auto event_it = timeline.find(node.time);
            if (event_it != timeline.end()) {
                gro::TDGNodeId child_id = event_it->second.same_edge_child;
                if (child_id >= 0 &&
                    child_id < static_cast<gro::TDGNodeId>(active.size()) &&
                    active[child_id]) {
                    dsu.unite(
                        static_cast<std::size_t>(node_id),
                        static_cast<std::size_t>(child_id));
                }
            }
        }

        std::unordered_map<std::size_t, int> root_to_component;
        std::vector<int> node_component(tdg.nodes.size(), -1);
        std::vector<ComponentStats> components;
        for (gro::TDGNodeId node_id = 0;
             node_id < static_cast<gro::TDGNodeId>(tdg.nodes.size());
             ++node_id) {
            if (!active[node_id]) {
                continue;
            }
            std::size_t root = dsu.find(static_cast<std::size_t>(node_id));
            auto [it, inserted] =
                root_to_component.emplace(root, static_cast<int>(components.size()));
            if (inserted) {
                components.push_back(ComponentStats{});
            }
            int component_id = it->second;
            node_component[node_id] = component_id;
            components[component_id].mass += relief[node_id];
            ++components[component_id].node_count;
        }

        std::vector<int> seen_component(components.size(), 0);
        int component_epoch = 0;
        std::vector<int> seen_node(tdg.nodes.size(), 0);
        int node_epoch = 0;
        std::vector<QueryStats> query_stats(queries.size());
        std::vector<std::vector<int>> query_components(queries.size());
        std::vector<long double> active_query_scores;
        std::vector<long double> active_query_component_counts;

        for (const gro::Trajectory& trajectory : traffic_result.trajectories) {
            gro::QueryId query_id = trajectory.query_id;
            if (query_id < 0 ||
                query_id >= static_cast<gro::QueryId>(queries.size())) {
                continue;
            }
            ++component_epoch;
            ++node_epoch;
            if (component_epoch == std::numeric_limits<int>::max()) {
                std::fill(seen_component.begin(), seen_component.end(), 0);
                component_epoch = 1;
            }
            if (node_epoch == std::numeric_limits<int>::max()) {
                std::fill(seen_node.begin(), seen_node.end(), 0);
                node_epoch = 1;
            }

            long double score = 0.0L;
            for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
                if (node_id < 0 ||
                    node_id >= static_cast<gro::TDGNodeId>(relief.size()) ||
                    relief[node_id] <= 0.0L) {
                    continue;
                }
                if (seen_node[node_id] != node_epoch) {
                    seen_node[node_id] = node_epoch;
                    score += relief[node_id];
                }

                int component_id = node_component[node_id];
                if (component_id < 0 ||
                    component_id >= static_cast<int>(components.size()) ||
                    seen_component[component_id] == component_epoch) {
                    continue;
                }
                seen_component[component_id] = component_epoch;
                query_components[query_id].push_back(component_id);
                ++components[component_id].query_count;
            }

            query_stats[query_id] = QueryStats{
                query_id,
                score,
                static_cast<int>(query_components[query_id].size())};
            if (score > 0.0L) {
                active_query_scores.push_back(score);
                active_query_component_counts.push_back(
                    static_cast<long double>(query_components[query_id].size()));
            }
        }

        std::vector<QueryStats> sorted_queries;
        sorted_queries.reserve(query_stats.size());
        for (const QueryStats& stats : query_stats) {
            if (stats.score > 0.0L) {
                sorted_queries.push_back(stats);
            }
        }
        std::sort(
            sorted_queries.begin(),
            sorted_queries.end(),
            [](const QueryStats& lhs, const QueryStats& rhs) {
                if (lhs.score != rhs.score) {
                    return lhs.score > rhs.score;
                }
                return lhs.query_id < rhs.query_id;
            });

        std::vector<long double> component_masses;
        component_masses.reserve(components.size());
        long double component_mass_total = 0.0L;
        long long multi_component_queries = 0;
        for (const ComponentStats& component : components) {
            component_masses.push_back(component.mass);
            component_mass_total += component.mass;
        }
        for (const QueryStats& stats : query_stats) {
            if (stats.component_count > 1) {
                ++multi_component_queries;
            }
        }

        long double total_query_score = 0.0L;
        for (const QueryStats& stats : sorted_queries) {
            total_query_score += stats.score;
        }

        auto component_count_to_cover = [&](int percent) {
            if (component_masses.empty() || component_mass_total <= 0.0L) {
                return std::size_t{0};
            }
            std::vector<long double> sorted_masses = component_masses;
            std::sort(
                sorted_masses.begin(),
                sorted_masses.end(),
                std::greater<long double>());
            long double target =
                component_mass_total *
                static_cast<long double>(std::clamp(percent, 0, 100)) /
                100.0L;
            long double covered = 0.0L;
            for (std::size_t i = 0; i < sorted_masses.size(); ++i) {
                covered += sorted_masses[i];
                if (covered >= target) {
                    return i + 1;
                }
            }
            return sorted_masses.size();
        };

        std::vector<long double> nonzero_edge_masses;
        for (long double mass : edge_mass) {
            if (mass > 0.0L) {
                nonzero_edge_masses.push_back(mass);
            }
        }

        std::size_t top10pct_count =
            static_cast<std::size_t>(
                std::ceil(static_cast<long double>(queries.size()) * 0.10L));
        std::size_t top20pct_count =
            static_cast<std::size_t>(
                std::ceil(static_cast<long double>(queries.size()) * 0.20L));
        std::size_t top30pct_count =
            static_cast<std::size_t>(
                std::ceil(static_cast<long double>(queries.size()) * 0.30L));

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "dataset=" << query_path.stem().string() << '\n';
        std::cout << "query_file=" << query_path.string() << '\n';
        std::cout << "query_count=" << queries.size() << '\n';
        std::cout << "total_travel_time=" << traffic_result.total_travel_time << '\n';
        std::cout << "initial_routes_sec=" << route_us / 1000000.0 << '\n';
        std::cout << "evaluate_sec=" << evaluate_us / 1000000.0 << '\n';
        std::cout << "build_tdg_sec=" << build_tdg_us / 1000000.0 << '\n';
        std::cout << "compute_impact_sec=" << impact_us / 1000000.0 << '\n';
        std::cout << "theta_percentile=" << algorithm_options.theta_percentile << '\n';
        std::cout << "tdg_nodes=" << tdg.nodes.size() << '\n';
        std::cout << "overloaded_nodes=" << overloaded_nodes << '\n';
        std::cout << "overloaded_node_fraction="
                  << (tdg.nodes.empty()
                          ? 0.0
                          : static_cast<double>(overloaded_nodes) /
                                static_cast<double>(tdg.nodes.size()))
                  << '\n';
        std::cout << "important_nodes=" << important_nodes << '\n';
        std::cout << "active_relief_nodes=" << active_nodes << '\n';
        std::cout << "active_relief_node_fraction="
                  << (tdg.nodes.empty()
                          ? 0.0
                          : static_cast<double>(active_nodes) /
                                static_cast<double>(tdg.nodes.size()))
                  << '\n';
        std::cout << "active_relief_edges=" << nonzero_edge_masses.size() << '\n';
        std::cout << "active_relief_components=" << components.size() << '\n';
        std::cout << "active_relief_mass=" << static_cast<double>(total_relief_mass) << '\n';
        std::cout << "source_candidate_count=" << source_candidates.size() << '\n';
        std::cout << "source_candidate_fraction="
                  << static_cast<double>(source_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "score_top_candidate_count="
                  << score_top_candidates.size() << '\n';
        std::cout << "score_top_candidate_fraction="
                  << static_cast<double>(score_top_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_balanced_candidate_count="
                  << component_balanced_candidates.size() << '\n';
        std::cout << "component_balanced_candidate_fraction="
                  << static_cast<double>(component_balanced_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_candidate_count="
                  << component_marginal_candidates.size() << '\n';
        std::cout << "component_marginal_candidate_fraction="
                  << static_cast<double>(component_marginal_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_budget5_candidate_count="
                  << component_marginal_budget5_candidates.size() << '\n';
        std::cout << "component_marginal_budget5_candidate_fraction="
                  << static_cast<double>(component_marginal_budget5_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_budget3_candidate_count="
                  << component_marginal_budget3_candidates.size() << '\n';
        std::cout << "component_marginal_budget3_candidate_fraction="
                  << static_cast<double>(component_marginal_budget3_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_samek_candidate_count="
                  << component_marginal_samek_candidates.size() << '\n';
        std::cout << "component_marginal_samek_candidate_fraction="
                  << static_cast<double>(component_marginal_samek_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_major80_budget5_candidate_count="
                  << component_marginal_major80_budget5_candidates.size() << '\n';
        std::cout << "component_marginal_major80_budget5_candidate_fraction="
                  << static_cast<double>(component_marginal_major80_budget5_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_major90_budget5_candidate_count="
                  << component_marginal_major90_budget5_candidates.size() << '\n';
        std::cout << "component_marginal_major90_budget5_candidate_fraction="
                  << static_cast<double>(component_marginal_major90_budget5_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "component_marginal_major90_samek_candidate_count="
                  << component_marginal_major90_samek_candidates.size() << '\n';
        std::cout << "component_marginal_major90_samek_candidate_fraction="
                  << static_cast<double>(component_marginal_major90_samek_candidates.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "positive_score_query_count=" << sorted_queries.size() << '\n';
        std::cout << "positive_score_query_fraction="
                  << static_cast<double>(sorted_queries.size()) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "edge_top1_mass_share=" << static_cast<double>(top_share(nonzero_edge_masses, 1)) << '\n';
        std::cout << "edge_top5_mass_share=" << static_cast<double>(top_share(nonzero_edge_masses, 5)) << '\n';
        std::cout << "edge_top10_mass_share=" << static_cast<double>(top_share(nonzero_edge_masses, 10)) << '\n';
        std::cout << "component_top1_mass_share=" << static_cast<double>(top_share(component_masses, 1)) << '\n';
        std::cout << "component_top5_mass_share=" << static_cast<double>(top_share(component_masses, 5)) << '\n';
        std::cout << "component_top10_mass_share=" << static_cast<double>(top_share(component_masses, 10)) << '\n';
        std::cout << "components_to_cover_80pct_mass="
                  << component_count_to_cover(80) << '\n';
        std::cout << "components_to_cover_90pct_mass="
                  << component_count_to_cover(90) << '\n';
        std::cout << "active_query_component_mean="
                  << (active_query_component_counts.empty()
                          ? 0.0
                          : static_cast<double>(
                                std::accumulate(
                                    active_query_component_counts.begin(),
                                    active_query_component_counts.end(),
                                    0.0L) /
                                active_query_component_counts.size()))
                  << '\n';
        std::cout << "active_query_component_p50="
                  << static_cast<double>(
                         percentile(active_query_component_counts, 50))
                  << '\n';
        std::cout << "active_query_component_p90="
                  << static_cast<double>(
                         percentile(active_query_component_counts, 90))
                  << '\n';
        std::cout << "multi_component_query_fraction="
                  << static_cast<double>(multi_component_queries) /
                        static_cast<double>(queries.size())
                  << '\n';
        std::cout << "top10pct_query_score_share="
                  << static_cast<double>(
                         prefix_query_score_share(
                             sorted_queries,
                             top10pct_count,
                             total_query_score))
                  << '\n';
        std::cout << "top20pct_query_score_share="
                  << static_cast<double>(
                         prefix_query_score_share(
                             sorted_queries,
                             top20pct_count,
                             total_query_score))
                  << '\n';
        std::cout << "top30pct_query_score_share="
                  << static_cast<double>(
                         prefix_query_score_share(
                             sorted_queries,
                             top30pct_count,
                             total_query_score))
                  << '\n';
        std::cout << "top10pct_component_mass_share="
                  << static_cast<double>(
                         prefix_component_mass_share(
                             sorted_queries,
                             query_components,
                             components,
                             top10pct_count,
                             component_mass_total))
                  << '\n';
        std::cout << "top20pct_component_mass_share="
                  << static_cast<double>(
                         prefix_component_mass_share(
                             sorted_queries,
                             query_components,
                             components,
                             top20pct_count,
                             component_mass_total))
                  << '\n';
        std::cout << "top30pct_component_mass_share="
                  << static_cast<double>(
                         prefix_component_mass_share(
                             sorted_queries,
                             query_components,
                             components,
                             top30pct_count,
                             component_mass_total))
                  << '\n';
        std::cout << "queries_to_cover_90pct_score="
                  << count_to_cover(sorted_queries, 0.90L, total_query_score)
                  << '\n';
        std::cout << "queries_to_cover_95pct_score="
                  << count_to_cover(sorted_queries, 0.95L, total_query_score)
                  << '\n';
        std::cout << "queries_to_cover_99pct_score="
                  << count_to_cover(sorted_queries, 0.99L, total_query_score)
                  << '\n';
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
