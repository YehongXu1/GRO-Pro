#include "gro.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct DatasetInfo {
    std::string dataset = "queries";
    int hop = -1;
    int rep = -1;
    int seed = -1;
};

struct DatasetInput {
    DatasetInfo info;
    std::filesystem::path path;
};

enum class RemovalMode {
    AllNodes,
    CongestionImportant,
    AnchorImportant
};

struct Options {
    std::string config_path = "config/config.yaml";
    std::filesystem::path query_file;
    std::filesystem::path query_dir;
    std::filesystem::path output_path =
        "python/results/gro_selection_debug_iterations.csv";
    unsigned int random_seed = 0;
    bool random_seed_set = false;
    int max_files = 0;
    std::vector<int> gamma_values;
    std::vector<RemovalMode> removal_modes;
};

struct TdgStats {
    long long node_count = 0;
    long long route_arc_count = 0;
    long long same_edge_arc_count = 0;
    long long edge_timeline_count = 0;
    long long max_out_degree = 0;
    gro::Flow max_flow = 0;
    long double mean_flow = 0.0;
    long double max_congestion_ratio = 0.0;
    long double mean_congestion_ratio = 0.0;
};

struct ScoreStats {
    long double sum = 0.0;
    long double mean = 0.0;
    long double max = 0.0;
};

struct SelectionResult {
    std::vector<gro::QueryId> query_ids;
    long long select_us = 0;
    long long important_node_count = 0;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int parse_percent_value(const std::string& text) {
    double value = std::stod(text);
    // CLI percentage arguments are percentage points: 1 means 1%, 10 means 10%.
    return static_cast<int>(std::llround(value));
}

std::vector<int> parse_percent_list(const std::string& text) {
    std::vector<int> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        if (!value.empty()) {
            values.push_back(parse_percent_value(value));
        }
    }
    return values;
}

std::string removal_mode_name(RemovalMode mode) {
    switch (mode) {
    case RemovalMode::AllNodes:
        return "all_nodes";
    case RemovalMode::CongestionImportant:
        return "congestion_important";
    case RemovalMode::AnchorImportant:
        return "anchor_important";
    }
    return "unknown";
}

RemovalMode parse_removal_mode(const std::string& text) {
    if (text == "all_nodes" || text == "all") {
        return RemovalMode::AllNodes;
    }
    if (text == "congestion_important" || text == "congestion") {
        return RemovalMode::CongestionImportant;
    }
    if (text == "anchor_important" || text == "anchor") {
        return RemovalMode::AnchorImportant;
    }
    throw std::runtime_error("Unknown removal mode: " + text);
}

std::vector<RemovalMode> parse_removal_mode_list(const std::string& text) {
    std::vector<RemovalMode> modes;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        if (!value.empty()) {
            modes.push_back(parse_removal_mode(value));
        }
    }
    return modes;
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
        } else if (arg == "--query-dir") {
            options.query_dir = require_value(arg);
        } else if (arg == "--output") {
            options.output_path = require_value(arg);
        } else if (arg == "--runs-output") {
            (void)require_value(arg);
        } else if (arg == "--random-seed") {
            options.random_seed =
                static_cast<unsigned int>(std::stoul(require_value(arg)));
            options.random_seed_set = true;
        } else if (arg == "--gamma-values") {
            options.gamma_values = parse_percent_list(require_value(arg));
        } else if (arg == "--removal-modes") {
            options.removal_modes = parse_removal_mode_list(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_selection_debug_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--random-seed n] "
                << "[--gamma-values 0,25,50,75,100] "
                << "[--removal-modes all_nodes,congestion_important,anchor_important] "
                << "[--max-files n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.removal_modes.empty()) {
        options.removal_modes = {
            RemovalMode::AllNodes,
            RemovalMode::CongestionImportant,
            RemovalMode::AnchorImportant};
    }

    return options;
}

DatasetInfo dataset_info_from_path(const std::filesystem::path& path) {
    DatasetInfo info;
    if (!path.empty()) {
        info.dataset = path.stem().string();
    }

    std::regex pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+))");
    std::smatch match;
    if (std::regex_match(info.dataset, match, pattern)) {
        info.hop = std::stoi(match[1].str());
        info.rep = std::stoi(match[2].str());
        info.seed = std::stoi(match[3].str());
    }
    return info;
}

std::vector<DatasetInput> discover_datasets(
    const std::filesystem::path& query_dir) {
    std::vector<DatasetInput> datasets;
    std::regex pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+)\.txt)");

    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, pattern)) {
            continue;
        }

        datasets.push_back(DatasetInput{
            dataset_info_from_path(entry.path()),
            entry.path()});
    }

    std::sort(
        datasets.begin(),
        datasets.end(),
        [](const DatasetInput& lhs, const DatasetInput& rhs) {
            if (lhs.info.hop != rhs.info.hop) {
                return lhs.info.hop < rhs.info.hop;
            }
            if (lhs.info.rep != rhs.info.rep) {
                return lhs.info.rep < rhs.info.rep;
            }
            return lhs.info.seed < rhs.info.seed;
        });
    return datasets;
}

std::vector<DatasetInput> resolve_datasets(
    const Options& options,
    const gro::InputConfig& input) {
    if (!options.query_file.empty() && !options.query_dir.empty()) {
        throw std::runtime_error("Use either --query-file or --query-dir, not both");
    }

    std::vector<DatasetInput> datasets;
    if (!options.query_dir.empty()) {
        datasets = discover_datasets(options.query_dir);
    } else {
        std::filesystem::path query_path =
            !options.query_file.empty()
                ? options.query_file
                : std::filesystem::path(input.queries_path);
        datasets.push_back(DatasetInput{
            dataset_info_from_path(query_path),
            query_path});
    }

    if (options.max_files > 0 &&
        static_cast<std::size_t>(options.max_files) < datasets.size()) {
        datasets.resize(static_cast<std::size_t>(options.max_files));
    }

    if (datasets.empty()) {
        throw std::runtime_error("No query datasets found");
    }
    return datasets;
}

std::unordered_set<gro::QueryId> make_query_set(
    const std::vector<gro::QueryId>& ids) {
    return std::unordered_set<gro::QueryId>(ids.begin(), ids.end());
}

std::size_t percentile_index(std::size_t size, int percentile) {
    if (size == 0) {
        return 0;
    }
    int clamped_percentile = std::clamp(percentile, 0, 100);
    return static_cast<std::size_t>(
        static_cast<long long>(clamped_percentile) *
        static_cast<long long>(size - 1) / 100);
}

std::size_t integer_sqrt(std::size_t value) {
    return static_cast<std::size_t>(
        std::sqrt(static_cast<long double>(value)));
}

gro::Cost congestion_ratio_key(gro::Flow flow, gro::Flow capacity) {
    gro::Flow safe_capacity = capacity > 0 ? capacity : 1;
    return static_cast<gro::Cost>(flow) * 1000000 / safe_capacity;
}

std::vector<gro::QueryId> random_query_ids(
    std::size_t query_count,
    std::size_t selected_count,
    unsigned int random_seed) {
    std::vector<gro::QueryId> ids(query_count);
    std::iota(ids.begin(), ids.end(), 0);
    std::shuffle(ids.begin(), ids.end(), std::mt19937{random_seed});
    if (selected_count < ids.size()) {
        ids.resize(selected_count);
    }
    return ids;
}

long double route_tdg_score(
    const gro::Trajectory& trajectory,
    const std::vector<gro::Cost>& impacts) {
    long double score = 0.0;
    for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
        if (node_id >= 0 &&
            node_id < static_cast<gro::TDGNodeId>(impacts.size())) {
            score += static_cast<long double>(std::max<gro::Cost>(0, impacts[node_id]));
        }
    }
    return score;
}

ScoreStats score_stats(
    const std::vector<gro::QueryId>& query_ids,
    const gro::TrafficResult& traffic_result,
    const std::vector<gro::Cost>& impacts) {
    ScoreStats stats;
    if (query_ids.empty()) {
        return stats;
    }

    for (gro::QueryId query_id : query_ids) {
        long double score =
            route_tdg_score(traffic_result.trajectories[query_id], impacts);
        stats.sum += score;
        stats.max = std::max(stats.max, score);
    }
    stats.mean = stats.sum / static_cast<long double>(query_ids.size());
    return stats;
}

std::vector<char> all_tdg_nodes_important(
    const gro::TrafficDependencyGraph& tdg) {
    return std::vector<char>(tdg.nodes.size(), 1);
}

std::vector<char> congestion_important_tdg_nodes(
    const gro::Graph& graph,
    const gro::TrafficDependencyGraph& tdg,
    int theta_percentile) {
    std::vector<char> important(tdg.nodes.size(), 0);
    if (tdg.nodes.empty()) {
        return important;
    }

    std::vector<gro::Cost> ratios;
    ratios.reserve(tdg.nodes.size());
    for (const gro::TDGNode& node : tdg.nodes) {
        ratios.push_back(
            congestion_ratio_key(
                node.flow,
                graph.edges[node.edge_id].capacity));
    }

    std::sort(ratios.begin(), ratios.end());
    gro::Cost theta = ratios[percentile_index(ratios.size(), theta_percentile)];

    for (const gro::TDGNode& node : tdg.nodes) {
        gro::Cost ratio =
            congestion_ratio_key(
                node.flow,
                graph.edges[node.edge_id].capacity);
        if (ratio >= theta) {
            important[node.id] = 1;
        }
    }
    return important;
}

std::vector<char> important_tdg_nodes(
    RemovalMode mode,
    const gro::GROAlgorithm& algorithm,
    const gro::Graph& graph,
    const gro::TrafficResult& traffic_result,
    const gro::TrafficDependencyGraph& tdg,
    int theta_percentile) {
    switch (mode) {
    case RemovalMode::AllNodes:
        return all_tdg_nodes_important(tdg);
    case RemovalMode::CongestionImportant:
        return congestion_important_tdg_nodes(graph, tdg, theta_percentile);
    case RemovalMode::AnchorImportant: {
        std::vector<std::map<gro::Time, gro::Cost>> anchor_scores =
            algorithm.compute_anchor_scores(traffic_result);
        return algorithm.mark_anchor_tdg_nodes(tdg, anchor_scores);
    }
    }
    return all_tdg_nodes_important(tdg);
}

SelectionResult select_queries_with_removal_mode(
    const gro::GROAlgorithm& algorithm,
    const std::unordered_set<gro::QueryId>& candidate_set,
    const gro::TrafficResult& result,
    const gro::TrafficDependencyGraph& tdg,
    const std::vector<char>& important_nodes,
    int gamma) {
    SelectionResult selection;
    selection.important_node_count =
        static_cast<long long>(
            std::count(important_nodes.begin(), important_nodes.end(), 1));

    auto total_start = gro::Clock::now();
    if (candidate_set.empty()) {
        selection.select_us = gro::elapsed_us(total_start);
        return selection;
    }

    gro::TrafficDependencyGraph working_tdg = tdg;
    std::unordered_set<gro::QueryId> selected;
    std::vector<gro::QueryId> candidate_ids(
        candidate_set.begin(),
        candidate_set.end());
    std::sort(candidate_ids.begin(), candidate_ids.end());

    auto route_score = [&](const gro::Trajectory& trajectory,
                           const std::vector<gro::Cost>& impacts) {
        long double score = 0.0;
        for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id >= 0 &&
                node_id < static_cast<gro::TDGNodeId>(impacts.size())) {
                score += static_cast<long double>(std::max<gro::Cost>(0, impacts[node_id]));
            }
        }
        return score;
    };

    auto is_removable = [&](const gro::Trajectory& trajectory) {
        for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<gro::TDGNodeId>(working_tdg.nodes.size())) {
                continue;
            }
            if (node_id >= static_cast<gro::TDGNodeId>(important_nodes.size()) ||
                !important_nodes[node_id]) {
                continue;
            }

            const gro::TDGNode& node = working_tdg.nodes[node_id];
            int min_flow = (100 - gamma) * node.original_flow / 100;
            if (node.flow - 1 < min_flow) {
                return false;
            }
        }
        return true;
    };

    while (selected.size() < candidate_ids.size()) {
        std::vector<gro::Cost> impacts =
            algorithm.compute_tdg_impact(working_tdg);

        std::vector<std::pair<long double, gro::QueryId>> ranking;
        ranking.reserve(candidate_ids.size() - selected.size());
        for (gro::QueryId query_id : candidate_ids) {
            if (selected.find(query_id) != selected.end()) {
                continue;
            }
            ranking.push_back({
                route_score(result.trajectories[query_id], impacts),
                query_id});
        }

        if (ranking.empty()) {
            break;
        }

        std::sort(
            ranking.begin(),
            ranking.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.first != rhs.first) {
                    return lhs.first > rhs.first;
                }
                return lhs.second < rhs.second;
            });

        std::size_t reject = 0;
        std::size_t selected_this_round = 0;
        std::size_t stale_threshold = integer_sqrt(ranking.size());
        for (const auto& [_, query_id] : ranking) {
            const gro::Trajectory& trajectory = result.trajectories[query_id];
            if (is_removable(trajectory)) {
                selected.insert(query_id);
                ++selected_this_round;
                reject = 0;
                algorithm.remove_trajectory_from_tdg(
                    working_tdg,
                    trajectory,
                    result);
            } else {
                ++reject;
                if (reject > stale_threshold) {
                    break;
                }
            }
        }

        if (selected_this_round == 0) {
            break;
        }
    }

    selection.query_ids.assign(selected.begin(), selected.end());
    std::sort(selection.query_ids.begin(), selection.query_ids.end());
    selection.select_us = gro::elapsed_us(total_start);
    return selection;
}

gro::Cost unselected_after_remove_total(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const std::vector<gro::Route>& routes,
    const std::unordered_set<gro::QueryId>& selected,
    const gro::TrafficOptions& traffic_options) {
    std::vector<gro::Query> remaining_queries;
    std::vector<gro::Route> remaining_routes;
    remaining_queries.reserve(queries.size() - selected.size());
    remaining_routes.reserve(routes.size() - selected.size());

    for (std::size_t i = 0; i < queries.size(); ++i) {
        gro::QueryId original_query_id = static_cast<gro::QueryId>(i);
        if (selected.find(original_query_id) != selected.end()) {
            continue;
        }

        gro::Query query = queries[i];
        gro::Route route = routes[i];
        gro::QueryId new_query_id =
            static_cast<gro::QueryId>(remaining_queries.size());
        query.id = new_query_id;
        route.query_id = new_query_id;
        remaining_queries.push_back(query);
        remaining_routes.push_back(route);
    }

    gro::TrafficResult remaining_result =
        gro::evaluate_traffic(
            graph,
            remaining_queries,
            remaining_routes,
            traffic_options);
    return remaining_result.total_travel_time;
}

TdgStats compute_tdg_stats(
    const gro::Graph& graph,
    const gro::TrafficDependencyGraph& tdg) {
    TdgStats stats;
    stats.node_count = static_cast<long long>(tdg.nodes.size());

    long double flow_sum = 0.0;
    long double congestion_ratio_sum = 0.0;
    for (const gro::TDGNode& node : tdg.nodes) {
        stats.max_flow = std::max(stats.max_flow, node.flow);
        flow_sum += static_cast<long double>(node.flow);

        gro::Flow capacity = graph.edges[node.edge_id].capacity > 0
            ? graph.edges[node.edge_id].capacity
            : 1;
        long double ratio =
            static_cast<long double>(node.flow) /
            static_cast<long double>(capacity);
        stats.max_congestion_ratio =
            std::max(stats.max_congestion_ratio, ratio);
        congestion_ratio_sum += ratio;
    }

    if (!tdg.nodes.empty()) {
        stats.mean_flow = flow_sum / static_cast<long double>(tdg.nodes.size());
        stats.mean_congestion_ratio =
            congestion_ratio_sum /
            static_cast<long double>(tdg.nodes.size());
    }

    std::vector<long long> out_degrees(tdg.nodes.size(), 0);
    for (std::size_t node_id = 0;
         node_id < tdg.route_outgoing.size() && node_id < tdg.nodes.size();
         ++node_id) {
        long long degree =
            static_cast<long long>(tdg.route_outgoing[node_id].size());
        stats.route_arc_count += degree;
        out_degrees[node_id] += degree;
    }

    for (const auto& timeline : tdg.edge_timelines) {
        stats.edge_timeline_count += static_cast<long long>(timeline.size());
        for (const auto& [_, event] : timeline) {
            if (event.same_edge_child != gro::kInvalidId) {
                ++stats.same_edge_arc_count;
                if (event.node_id >= 0 &&
                    event.node_id < static_cast<gro::TDGNodeId>(out_degrees.size())) {
                    ++out_degrees[event.node_id];
                }
            }
        }
    }
    for (long long degree : out_degrees) {
        stats.max_out_degree = std::max(stats.max_out_degree, degree);
    }

    return stats;
}

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_iterations_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,algorithm,removal_mode,"
        << "query_count,lambda,gamma,theta_percentile,anchor_window,"
        << "anchor_threshold,random_seed,"
        << "selected_count,important_node_count,"
        << "total_before,"
        << "tdg_unselected_after_remove,random_unselected_after_remove,"
        << "tdg_prepare_sec,"
        << "tdg_select_sec,random_select_sec,"
        << "mean_tdg_selected_impact_score,"
        << "mean_random_selected_impact_score,"
        << "mean_all_query_impact_score,"
        << "tdg_node_count,tdg_edge_timeline_count,tdg_max_flow,"
        << "tdg_max_congestion_ratio,"
        << "tdg_mean_congestion_ratio\n";
}

void write_iteration_row(
    std::ofstream& out,
    const std::string& run_id,
    RemovalMode removal_mode,
    unsigned int random_seed,
    const DatasetInfo& dataset,
    const gro::AlgorithmOptions& algorithm_options,
    std::size_t query_count,
    std::size_t selected_count,
    long long important_node_count,
    gro::Cost total_before,
    gro::Cost tdg_unselected_after_remove,
    gro::Cost random_unselected_after_remove,
    long long build_tdg_us,
    long long compute_impact_us,
    long long important_nodes_us,
    long long tdg_select_us,
    long long random_select_us,
    const ScoreStats& selected_scores,
    const ScoreStats& random_scores,
    const ScoreStats& all_scores,
    const TdgStats& tdg_stats) {
    out << std::setprecision(12)
        << run_id << ','
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << "tdg_selection_no_candidate_vs_random" << ','
        << removal_mode_name(removal_mode) << ','
        << query_count << ','
        << algorithm_options.lambda << ','
        << algorithm_options.gamma << ','
        << algorithm_options.theta_percentile << ','
        << algorithm_options.anchor_window << ','
        << algorithm_options.anchor_threshold << ','
        << random_seed << ','
        << selected_count << ','
        << important_node_count << ','
        << total_before << ','
        << tdg_unselected_after_remove << ','
        << random_unselected_after_remove << ','
        << static_cast<double>(
               build_tdg_us + compute_impact_us + important_nodes_us) /
               1000000.0
        << ','
        << static_cast<double>(tdg_select_us) / 1000000.0 << ','
        << static_cast<double>(random_select_us) / 1000000.0 << ','
        << static_cast<double>(selected_scores.mean) << ','
        << static_cast<double>(random_scores.mean) << ','
        << static_cast<double>(all_scores.mean) << ','
        << tdg_stats.node_count << ','
        << tdg_stats.edge_timeline_count << ','
        << tdg_stats.max_flow << ','
        << static_cast<double>(tdg_stats.max_congestion_ratio) << ','
        << static_cast<double>(tdg_stats.mean_congestion_ratio) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_args(argc, argv);

        gro::InputConfig input = gro::load_input_config(options.config_path);
        gro::Graph graph = gro::read_graph(input);
        gro::AlgorithmOptions algorithm_options =
            gro::load_algorithm_options(options.config_path);
        algorithm_options.enable_timing_log = false;
        if (options.gamma_values.empty()) {
            options.gamma_values.push_back(algorithm_options.gamma);
        }
        gro::TrafficOptions traffic_options =
            gro::load_traffic_options(options.config_path);

        if (!options.random_seed_set) {
            options.random_seed = algorithm_options.baseline_random_seed;
        }

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);

        ensure_parent_dir(options.output_path);
        std::ofstream iterations_out(options.output_path);
        require(
            static_cast<bool>(iterations_out),
            "Cannot open output: " + options.output_path.string());
        write_iterations_header(iterations_out);

        std::cout << "Selection diagnostic written:\n"
                  << "  " << options.output_path << "\n"
                  << "datasets=" << datasets.size() << "\n";

        for (const DatasetInput& dataset : datasets) {
            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(
                !queries.empty(),
                "queries should not be empty: " + dataset.path.string());

            gro::GROAlgorithm base_algorithm(
                graph,
                algorithm_options,
                traffic_options);

            std::vector<gro::Route> routes =
                base_algorithm.compute_initial_routes(queries);

            gro::TrafficResult traffic_result =
                gro::evaluate_traffic(graph, queries, routes, traffic_options);
            gro::Cost total_before = traffic_result.total_travel_time;

            auto build_tdg_start = gro::Clock::now();
            gro::TrafficDependencyGraph tdg =
                base_algorithm.build_tdg(traffic_result);
            long long build_tdg_us = gro::elapsed_us(build_tdg_start);

            auto compute_impact_start = gro::Clock::now();
            std::vector<gro::Cost> impacts =
                base_algorithm.compute_tdg_impact(tdg);
            long long compute_impact_us =
                gro::elapsed_us(compute_impact_start);

            std::unordered_set<gro::QueryId> all_query_set;
            std::vector<gro::QueryId> all_query_ids;
            all_query_ids.reserve(queries.size());
            for (std::size_t i = 0; i < queries.size(); ++i) {
                gro::QueryId query_id = static_cast<gro::QueryId>(i);
                all_query_set.insert(query_id);
                all_query_ids.push_back(query_id);
            }

            ScoreStats all_scores =
                score_stats(all_query_ids, traffic_result, impacts);
            TdgStats tdg_stats = compute_tdg_stats(graph, tdg);

            for (std::size_t gamma_index = 0;
                 gamma_index < options.gamma_values.size();
                 ++gamma_index) {
                gro::AlgorithmOptions run_options = algorithm_options;
                run_options.gamma = options.gamma_values[gamma_index];
                gro::GROAlgorithm selector(
                    graph,
                    run_options,
                    traffic_options);

                for (RemovalMode removal_mode : options.removal_modes) {
                    std::string mode_name = removal_mode_name(removal_mode);
                    std::string run_id =
                        dataset.info.dataset +
                        "_gamma" +
                        std::to_string(run_options.gamma) +
                        "_" +
                        mode_name;

                    auto important_nodes_start = gro::Clock::now();
                    std::vector<char> important_nodes =
                        important_tdg_nodes(
                            removal_mode,
                            selector,
                            graph,
                            traffic_result,
                            tdg,
                            run_options.theta_percentile);
                    long long important_nodes_us =
                        gro::elapsed_us(important_nodes_start);

                    SelectionResult selection =
                        select_queries_with_removal_mode(
                            selector,
                            all_query_set,
                            traffic_result,
                            tdg,
                            important_nodes,
                            run_options.gamma);
                    std::vector<gro::QueryId>& selected_ids =
                        selection.query_ids;
                    std::unordered_set<gro::QueryId> selected_set =
                        make_query_set(selected_ids);

                    auto random_select_start = gro::Clock::now();
                    std::vector<gro::QueryId> random_ids =
                        random_query_ids(
                            queries.size(),
                            selected_ids.size(),
                            options.random_seed);
                    long long random_select_us =
                        gro::elapsed_us(random_select_start);
                    std::unordered_set<gro::QueryId> random_set =
                        make_query_set(random_ids);

                    gro::Cost tdg_unselected_after_remove =
                        unselected_after_remove_total(
                            graph,
                            queries,
                            routes,
                            selected_set,
                            traffic_options);
                    gro::Cost random_unselected_after_remove =
                        unselected_after_remove_total(
                            graph,
                            queries,
                            routes,
                            random_set,
                            traffic_options);

                    ScoreStats selected_scores =
                        score_stats(selected_ids, traffic_result, impacts);
                    ScoreStats random_scores =
                        score_stats(random_ids, traffic_result, impacts);

                    write_iteration_row(
                        iterations_out,
                        run_id,
                        removal_mode,
                        options.random_seed,
                        dataset.info,
                        run_options,
                        queries.size(),
                        selected_ids.size(),
                        selection.important_node_count,
                        total_before,
                        tdg_unselected_after_remove,
                        random_unselected_after_remove,
                        build_tdg_us,
                        compute_impact_us,
                        important_nodes_us,
                        selection.select_us,
                        random_select_us,
                        selected_scores,
                        random_scores,
                        all_scores,
                        tdg_stats);

                    std::cout << "run_id=" << run_id
                              << " selected_count=" << selected_ids.size()
                              << " important_node_count="
                              << selection.important_node_count
                              << " total_before=" << total_before
                              << " tdg_unselected_after_remove="
                              << tdg_unselected_after_remove
                              << " random_unselected_after_remove="
                              << random_unselected_after_remove
                              << " tdg_prepare_sec="
                              << static_cast<double>(
                                     build_tdg_us +
                                     compute_impact_us +
                                     important_nodes_us) /
                                     1000000.0
                              << " tdg_select_sec="
                              << static_cast<double>(selection.select_us) / 1000000.0
                              << " random_select_sec="
                              << static_cast<double>(random_select_us) / 1000000.0
                              << "\n";
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
