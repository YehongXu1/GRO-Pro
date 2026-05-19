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
        "python/results/gro_reroute_debug.csv";
    unsigned int random_seed = 0;
    bool random_seed_set = false;
    int max_files = 0;
    std::vector<int> impact_weights;
};

struct ScoreStats {
    gro::Cost sum = 0;
    long double mean = 0.0;
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
        } else if (arg == "--random-seed") {
            options.random_seed =
                static_cast<unsigned int>(std::stoul(require_value(arg)));
            options.random_seed_set = true;
        } else if (arg == "--impact-weights") {
            options.impact_weights = parse_percent_list(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_reroute_debug_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--impact-weights 0,5,15,30,50,100] "
                << "[--random-seed n] [--max-files n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
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

std::vector<gro::QueryId> random_query_ids(
    std::size_t query_count,
    int selection_fraction,
    unsigned int random_seed) {
    int fraction = std::clamp(selection_fraction, 0, 100);
    std::vector<gro::QueryId> ids(query_count);
    std::iota(ids.begin(), ids.end(), 0);
    std::shuffle(ids.begin(), ids.end(), std::mt19937{random_seed});

    std::size_t selected_count =
        static_cast<std::size_t>(fraction) * query_count / 100;
    if (selected_count < ids.size()) {
        ids.resize(selected_count);
    }
    return ids;
}

std::size_t tdg_edge_timeline_count(
    const gro::TrafficDependencyGraph& tdg) {
    std::size_t count = 0;
    for (const auto& timeline : tdg.edge_timelines) {
        count += timeline.size();
    }
    return count;
}

gro::Cost route_impact_score(
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
        stats.sum +=
            route_impact_score(traffic_result.trajectories[query_id], impacts);
    }
    stats.mean =
        static_cast<long double>(stats.sum) /
        static_cast<long double>(query_ids.size());
    return stats;
}

void replace_routes(
    std::vector<gro::Route>& routes,
    const std::vector<gro::Route>& new_routes) {
    for (const gro::Route& route : new_routes) {
        if (route.query_id >= 0 &&
            route.query_id < static_cast<gro::QueryId>(routes.size())) {
            routes[route.query_id] = route;
        }
    }
}

gro::Cost evaluate_total_after(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    std::vector<gro::Route> routes,
    const std::vector<gro::Route>& new_routes,
    const gro::TrafficOptions& traffic_options,
    long long& evaluate_us) {
    replace_routes(routes, new_routes);
    auto start = gro::Clock::now();
    gro::TrafficResult result =
        gro::evaluate_traffic(graph, queries, routes, traffic_options);
    evaluate_us = gro::elapsed_us(start);
    return result.total_travel_time;
}

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,query_count,"
        << "selection_method,selection_fraction,random_seed,selected_count,"
        << "lambda,reroute_method,impact_weight,total_before,total_after,"
        << "tdg_prepare_sec,reroute_sec,evaluate_after_sec,"
        << "mean_selected_impact_score,mean_all_query_impact_score,"
        << "tdg_node_count,tdg_edge_timeline_count\n";
}

void write_row(
    std::ofstream& out,
    const std::string& run_id,
    const DatasetInfo& dataset,
    std::size_t query_count,
    int selection_fraction,
    unsigned int random_seed,
    std::size_t selected_count,
    int lambda,
    const std::string& reroute_method,
    int impact_weight,
    gro::Cost total_before,
    gro::Cost total_after,
    long long tdg_prepare_us,
    long long reroute_us,
    long long evaluate_after_us,
    long double mean_selected_impact_score,
    long double mean_all_query_impact_score,
    std::size_t tdg_node_count,
    std::size_t tdg_edge_timeline_count) {
    out << std::setprecision(12)
        << run_id << ','
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << query_count << ','
        << "random" << ','
        << selection_fraction << ','
        << random_seed << ','
        << selected_count << ','
        << lambda << ','
        << reroute_method << ','
        << impact_weight << ','
        << total_before << ','
        << total_after << ','
        << static_cast<double>(tdg_prepare_us) / 1000000.0 << ','
        << static_cast<double>(reroute_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_after_us) / 1000000.0 << ','
        << static_cast<double>(mean_selected_impact_score) << ','
        << static_cast<double>(mean_all_query_impact_score) << ','
        << tdg_node_count << ','
        << tdg_edge_timeline_count << '\n';
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
        gro::TrafficOptions traffic_options =
            gro::load_traffic_options(options.config_path);

        if (!options.random_seed_set) {
            options.random_seed = algorithm_options.baseline_random_seed;
        }
        if (options.impact_weights.empty()) {
            options.impact_weights = {0, 5, 15, 30, 50, 100};
        }

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);

        ensure_parent_dir(options.output_path);
        std::ofstream out(options.output_path);
        require(
            static_cast<bool>(out),
            "Cannot open output: " + options.output_path.string());
        write_header(out);

        std::cout << "Reroute diagnostic written:\n"
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

            std::vector<gro::Route> initial_routes =
                base_algorithm.compute_initial_routes(queries);
            gro::TrafficResult initial_traffic =
                gro::evaluate_traffic(
                    graph,
                    queries,
                    initial_routes,
                    traffic_options);
            gro::Cost total_before = initial_traffic.total_travel_time;

            auto tdg_start = gro::Clock::now();
            gro::TrafficDependencyGraph tdg =
                base_algorithm.build_tdg(initial_traffic);
            long long build_tdg_us = gro::elapsed_us(tdg_start);

            auto impact_start = gro::Clock::now();
            std::vector<gro::Cost> impacts =
                base_algorithm.compute_tdg_impact(tdg);
            long long compute_impact_us = gro::elapsed_us(impact_start);
            long long tdg_prepare_us = build_tdg_us + compute_impact_us;

            std::vector<gro::QueryId> all_query_ids(queries.size());
            std::iota(all_query_ids.begin(), all_query_ids.end(), 0);
            ScoreStats all_scores =
                score_stats(all_query_ids, initial_traffic, impacts);

            int selection_fraction =
                algorithm_options.baseline_fraction_to_reroute;
            {
                std::vector<gro::QueryId> selected_ids =
                    random_query_ids(
                        queries.size(),
                        selection_fraction,
                        options.random_seed);
                ScoreStats selected_scores =
                    score_stats(selected_ids, initial_traffic, impacts);

                std::string prefix =
                    dataset.info.dataset +
                    "_random" +
                    std::to_string(selection_fraction);

                auto normal_start = gro::Clock::now();
                std::vector<gro::Route> normal_routes =
                    base_algorithm.baseline_reroute_queries(
                        selected_ids,
                        queries,
                        initial_traffic);
                long long normal_reroute_us = gro::elapsed_us(normal_start);

                long long normal_evaluate_us = 0;
                gro::Cost normal_total_after =
                    evaluate_total_after(
                        graph,
                        queries,
                        initial_routes,
                        normal_routes,
                        traffic_options,
                        normal_evaluate_us);

                write_row(
                    out,
                    prefix + "_normal_td_dijkstra",
                    dataset.info,
                    queries.size(),
                    selection_fraction,
                    options.random_seed,
                    selected_ids.size(),
            algorithm_options.lambda,
            "normal_td_dijkstra",
                    -1,
                    total_before,
                    normal_total_after,
                    0,
                    normal_reroute_us,
                    normal_evaluate_us,
            selected_scores.mean,
            all_scores.mean,
            tdg.nodes.size(),
            tdg_edge_timeline_count(tdg));

                std::cout << "run_id=" << prefix << "_normal_td_dijkstra"
                          << " selected_count=" << selected_ids.size()
                          << " total_before=" << total_before
                          << " total_after=" << normal_total_after
                          << " reroute_sec="
                          << static_cast<double>(normal_reroute_us) / 1000000.0
                          << "\n";

                std::vector<std::vector<gro::QueryId>> one_batch{
                    selected_ids};
                for (int impact_weight : options.impact_weights) {
                    gro::AlgorithmOptions run_options = algorithm_options;
                    run_options.impact_weight = impact_weight;
                    run_options.enable_timing_log = false;
                    gro::GROAlgorithm rerouter(
                        graph,
                        run_options,
                        traffic_options);

                    auto reroute_start = gro::Clock::now();
                    std::vector<gro::Route> tdg_routes =
                        rerouter.reroute_queries(
                            one_batch,
                            queries,
                            initial_traffic,
                            tdg,
                            impacts);
                    long long reroute_us = gro::elapsed_us(reroute_start);

                    long long evaluate_after_us = 0;
                    gro::Cost total_after =
                        evaluate_total_after(
                            graph,
                            queries,
                            initial_routes,
                            tdg_routes,
                            traffic_options,
                            evaluate_after_us);

                    std::string run_id =
                        prefix +
                        "_tdg_impact_w" +
                        std::to_string(impact_weight);
                    write_row(
                        out,
                        run_id,
                        dataset.info,
                        queries.size(),
                        selection_fraction,
                        options.random_seed,
                        selected_ids.size(),
                        run_options.lambda,
                        "tdg_impact_reroute",
                        impact_weight,
                        total_before,
                        total_after,
                        tdg_prepare_us,
                        reroute_us,
                        evaluate_after_us,
                        selected_scores.mean,
                        all_scores.mean,
                        tdg.nodes.size(),
                        tdg_edge_timeline_count(tdg));

                    std::cout << "run_id=" << run_id
                              << " selected_count=" << selected_ids.size()
                              << " total_before=" << total_before
                              << " total_after=" << total_after
                              << " tdg_prepare_sec="
                              << static_cast<double>(tdg_prepare_us) / 1000000.0
                              << " reroute_sec="
                              << static_cast<double>(reroute_us) / 1000000.0
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
