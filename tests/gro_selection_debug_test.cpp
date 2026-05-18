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

struct Options {
    std::string config_path = "config/config.yaml";
    std::filesystem::path query_file;
    std::filesystem::path query_dir;
    std::filesystem::path output_path =
        "python/results/gro_selection_debug_iterations.csv";
    std::filesystem::path runs_output_path;
    unsigned int random_seed = 0;
    bool random_seed_set = false;
    int max_files = 0;
    std::vector<int> gamma_values;
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
    gro::Cost sum = 0;
    long double mean = 0.0;
    gro::Cost max = 0;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int parse_percent_value(const std::string& text) {
    double value = std::stod(text);
    if (value >= -1.0 && value <= 1.0) {
        value *= 100.0;
    }
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
            options.runs_output_path = require_value(arg);
        } else if (arg == "--random-seed") {
            options.random_seed =
                static_cast<unsigned int>(std::stoul(require_value(arg)));
            options.random_seed_set = true;
        } else if (arg == "--gamma-values") {
            options.gamma_values = parse_percent_list(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_selection_debug_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--runs-output path] [--random-seed n] "
                << "[--gamma-values 0,25,50,75,100] "
                << "[--max-files n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.runs_output_path.empty()) {
        std::filesystem::path runs_path = options.output_path;
        runs_path.replace_filename(
            options.output_path.stem().string() + "_runs.csv");
        options.runs_output_path = runs_path;
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

gro::Cost route_tdg_score(
    const gro::Trajectory& trajectory,
    const std::vector<gro::Cost>& impacts) {
    gro::Cost score = 0;
    for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
        if (node_id >= 0 &&
            node_id < static_cast<gro::TDGNodeId>(impacts.size())) {
            score += impacts[node_id];
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
        gro::Cost score =
            route_tdg_score(traffic_result.trajectories[query_id], impacts);
        stats.sum += score;
        stats.max = std::max(stats.max, score);
    }
    stats.mean =
        static_cast<long double>(stats.sum) /
        static_cast<long double>(query_ids.size());
    return stats;
}

gro::Cost remaining_before_total(
    const gro::TrafficResult& traffic_result,
    const std::unordered_set<gro::QueryId>& selected) {
    gro::Cost total = 0;
    for (std::size_t i = 0; i < traffic_result.trajectories.size(); ++i) {
        gro::QueryId query_id = static_cast<gro::QueryId>(i);
        if (selected.find(query_id) == selected.end()) {
            total += traffic_result.trajectories[i].travel_time;
        }
    }
    return total;
}

gro::Cost remaining_after_remove_total(
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

void write_runs_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,algorithm,query_count,"
        << "lambda,gamma,theta_percentile,impact_weight,"
        << "conflict_threshold,random_seed,max_iterations\n";
}

void write_run_row(
    std::ofstream& out,
    const std::string& run_id,
    unsigned int random_seed,
    const DatasetInfo& dataset,
    const gro::AlgorithmOptions& algorithm_options,
    std::size_t query_count) {
    out << run_id << ','
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << "tdg_selection_no_candidate_vs_random" << ','
        << query_count << ','
        << algorithm_options.lambda << ','
        << algorithm_options.gamma << ','
        << algorithm_options.theta_percentile << ','
        << algorithm_options.impact_weight << ','
        << algorithm_options.conflict_threshold << ','
        << random_seed << ','
        << algorithm_options.max_iterations << '\n';
}

void write_iterations_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,algorithm,query_count,iteration,"
        << "selected_count,total_before,"
        << "tdg_remaining_before,tdg_remaining_after_remove,"
        << "random_remaining_before,random_remaining_after_remove,"
        << "tdg_selected_score_sum,random_selected_score_sum,"
        << "mean_selected_tdg_score,mean_random_tdg_score,"
        << "mean_all_query_tdg_score,max_selected_tdg_score,"
        << "max_random_tdg_score,max_all_query_tdg_score,"
        << "tdg_node_count,tdg_route_arc_count,tdg_same_edge_arc_count,"
        << "tdg_edge_timeline_count,tdg_max_out_degree,tdg_max_flow,"
        << "tdg_mean_flow,tdg_max_congestion_ratio,"
        << "tdg_mean_congestion_ratio\n";
}

void write_iteration_row(
    std::ofstream& out,
    const std::string& run_id,
    const DatasetInfo& dataset,
    std::size_t query_count,
    std::size_t selected_count,
    gro::Cost total_before,
    gro::Cost tdg_remaining_before,
    gro::Cost tdg_remaining_after_remove,
    gro::Cost random_remaining_before,
    gro::Cost random_remaining_after_remove,
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
        << query_count << ','
        << 0 << ','
        << selected_count << ','
        << total_before << ','
        << tdg_remaining_before << ','
        << tdg_remaining_after_remove << ','
        << random_remaining_before << ','
        << random_remaining_after_remove << ','
        << selected_scores.sum << ','
        << random_scores.sum << ','
        << static_cast<double>(selected_scores.mean) << ','
        << static_cast<double>(random_scores.mean) << ','
        << static_cast<double>(all_scores.mean) << ','
        << selected_scores.max << ','
        << random_scores.max << ','
        << all_scores.max << ','
        << tdg_stats.node_count << ','
        << tdg_stats.route_arc_count << ','
        << tdg_stats.same_edge_arc_count << ','
        << tdg_stats.edge_timeline_count << ','
        << tdg_stats.max_out_degree << ','
        << tdg_stats.max_flow << ','
        << static_cast<double>(tdg_stats.mean_flow) << ','
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

        ensure_parent_dir(options.runs_output_path);
        std::ofstream runs_out(options.runs_output_path);
        require(
            static_cast<bool>(runs_out),
            "Cannot open runs output: " +
                options.runs_output_path.string());
        write_runs_header(runs_out);

        std::cout << "Selection diagnostic written:\n"
                  << "  " << options.output_path << "\n"
                  << "  " << options.runs_output_path << "\n"
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

            gro::TrafficDependencyGraph tdg =
                base_algorithm.build_tdg(traffic_result);
            std::vector<gro::Cost> impacts =
                base_algorithm.compute_tdg_impact(tdg);

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
                std::string run_id =
                    dataset.info.dataset +
                    "_gamma" +
                    std::to_string(run_options.gamma);

                std::vector<gro::QueryId> selected_ids =
                    selector.select_queries(
                        all_query_set,
                        queries,
                        traffic_result,
                        tdg,
                        impacts,
                        0);
                std::unordered_set<gro::QueryId> selected_set =
                    make_query_set(selected_ids);

                std::vector<gro::QueryId> random_ids =
                    random_query_ids(
                        queries.size(),
                        selected_ids.size(),
                        options.random_seed);
                std::unordered_set<gro::QueryId> random_set =
                    make_query_set(random_ids);

                gro::Cost tdg_remaining_before =
                    remaining_before_total(traffic_result, selected_set);
                gro::Cost tdg_remaining_after_remove =
                    remaining_after_remove_total(
                        graph,
                        queries,
                        routes,
                        selected_set,
                        traffic_options);
                gro::Cost random_remaining_before =
                    remaining_before_total(traffic_result, random_set);
                gro::Cost random_remaining_after_remove =
                    remaining_after_remove_total(
                        graph,
                        queries,
                        routes,
                        random_set,
                        traffic_options);

                ScoreStats selected_scores =
                    score_stats(selected_ids, traffic_result, impacts);
                ScoreStats random_scores =
                    score_stats(random_ids, traffic_result, impacts);

                write_run_row(
                    runs_out,
                    run_id,
                    options.random_seed,
                    dataset.info,
                    run_options,
                    queries.size());
                write_iteration_row(
                    iterations_out,
                    run_id,
                    dataset.info,
                    queries.size(),
                    selected_ids.size(),
                    total_before,
                    tdg_remaining_before,
                    tdg_remaining_after_remove,
                    random_remaining_before,
                    random_remaining_after_remove,
                    selected_scores,
                    random_scores,
                    all_scores,
                    tdg_stats);

                std::cout << "run_id=" << run_id
                          << " selected_count=" << selected_ids.size()
                          << " total_before=" << total_before
                          << " tdg_remaining_before="
                          << tdg_remaining_before
                          << " tdg_remaining_after_remove="
                          << tdg_remaining_after_remove
                          << " random_remaining_before="
                          << random_remaining_before
                          << " random_remaining_after_remove="
                          << random_remaining_after_remove
                          << "\n";
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
