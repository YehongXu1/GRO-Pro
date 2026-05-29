#include "gro.hpp"

#include <algorithm>
#include <cmath>
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
        "python/results/gro_fixed_random_selection.csv";
    unsigned int random_seed = 0;
    bool random_seed_set = false;
    int max_files = 0;
    std::vector<int> random_fractions = {10, 30};
    std::vector<std::string> methods = {"random", "most_delayed"};
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

std::vector<std::string> parse_method_list(const std::string& text) {
    std::vector<std::string> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        if (!value.empty()) {
            values.push_back(value);
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
        } else if (arg == "--random-fractions") {
            options.random_fractions = parse_percent_list(require_value(arg));
        } else if (arg == "--methods") {
            options.methods = parse_method_list(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_fixed_random_selection_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--random-fractions 10,30] [--random-seed n] "
                << "[--methods random,most_delayed] "
                << "[--max-files n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.random_fractions.empty()) {
        throw std::runtime_error("At least one random fraction is required");
    }
    if (options.methods.empty()) {
        throw std::runtime_error("At least one selection method is required");
    }

    return options;
}

DatasetInfo parse_dataset_info(const std::filesystem::path& path) {
    DatasetInfo info;
    info.dataset = path.stem().string();
    std::regex pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+))");
    std::smatch match;
    if (std::regex_search(info.dataset, match, pattern)) {
        info.hop = std::stoi(match[1].str());
        info.rep = std::stoi(match[2].str());
        info.seed = std::stoi(match[3].str());
    }
    return info;
}

std::vector<DatasetInput> discover_datasets(
    const std::filesystem::path& query_dir) {
    std::vector<DatasetInput> datasets;
    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::filesystem::path path = entry.path();
        if (path.extension() != ".txt") {
            continue;
        }
        DatasetInfo info = parse_dataset_info(path);
        if (info.hop < 0 || info.rep < 0 || info.seed < 0) {
            continue;
        }
        datasets.push_back(DatasetInput{info, path});
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
            if (lhs.info.seed != rhs.info.seed) {
                return lhs.info.seed < rhs.info.seed;
            }
            return lhs.path < rhs.path;
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
        std::filesystem::path path =
            options.query_file.empty()
                ? std::filesystem::path(input.queries_path)
                : options.query_file;
        datasets.push_back(DatasetInput{parse_dataset_info(path), path});
    }

    if (options.max_files > 0 &&
        static_cast<std::size_t>(options.max_files) < datasets.size()) {
        datasets.resize(static_cast<std::size_t>(options.max_files));
    }
    return datasets;
}

std::vector<gro::QueryId> random_query_ids(
    std::size_t query_count,
    int fraction_to_select,
    unsigned int random_seed) {
    int clamped_fraction = std::clamp(fraction_to_select, 0, 100);
    std::size_t selected_count =
        static_cast<std::size_t>(clamped_fraction) * query_count / 100;

    std::vector<gro::QueryId> ids(query_count);
    std::iota(ids.begin(), ids.end(), 0);
    std::shuffle(ids.begin(), ids.end(), std::mt19937{random_seed});
    if (selected_count < ids.size()) {
        ids.resize(selected_count);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<gro::QueryId> most_delayed_query_ids(
    std::size_t query_count,
    int fraction_to_select,
    const std::vector<gro::Route>& free_flow_routes,
    const gro::TrafficResult& traffic_result) {
    int clamped_fraction = std::clamp(fraction_to_select, 0, 100);
    std::size_t selected_count =
        static_cast<std::size_t>(clamped_fraction) * query_count / 100;

    std::vector<std::pair<gro::Cost, gro::QueryId>> ranking;
    ranking.reserve(query_count);
    for (std::size_t i = 0; i < query_count; ++i) {
        gro::QueryId query_id = static_cast<gro::QueryId>(i);
        gro::Cost actual_time = traffic_result.trajectories[query_id].travel_time;
        gro::Cost free_flow_time = free_flow_routes[query_id].travel_time;
        ranking.push_back({actual_time - free_flow_time, query_id});
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

    if (selected_count < ranking.size()) {
        ranking.resize(selected_count);
    }

    std::vector<gro::QueryId> ids;
    ids.reserve(ranking.size());
    for (const auto& [_, query_id] : ranking) {
        ids.push_back(query_id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<gro::QueryId> select_query_ids(
    const std::string& method,
    std::size_t query_count,
    int fraction_to_select,
    unsigned int random_seed,
    const std::vector<gro::Route>& free_flow_routes,
    const gro::TrafficResult& traffic_result) {
    if (method == "random") {
        return random_query_ids(query_count, fraction_to_select, random_seed);
    }
    if (method == "most_delayed") {
        return most_delayed_query_ids(
            query_count,
            fraction_to_select,
            free_flow_routes,
            traffic_result);
    }
    throw std::runtime_error("Unknown selection method: " + method);
}

std::unordered_set<gro::QueryId> make_query_set(
    const std::vector<gro::QueryId>& query_ids) {
    return std::unordered_set<gro::QueryId>(query_ids.begin(), query_ids.end());
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

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_header(std::ofstream& out) {
    out << "dataset,hop,rep,seed,query_count,selection_method,"
        << "selection_fraction,random_seed,selected_count,total_before,"
        << "unselected_after_remove,reduction,select_sec,"
        << "evaluate_remaining_sec\n";
}

void write_row(
    std::ofstream& out,
    const DatasetInfo& dataset,
    std::size_t query_count,
    const std::string& method,
    int selection_fraction,
    unsigned int random_seed,
    std::size_t selected_count,
    gro::Cost total_before,
    gro::Cost unselected_after_remove,
    long long select_us,
    long long evaluate_remaining_us) {
    out << std::setprecision(12)
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << query_count << ','
        << method << ','
        << selection_fraction << ','
        << random_seed << ','
        << selected_count << ','
        << total_before << ','
        << unselected_after_remove << ','
        << total_before - unselected_after_remove << ','
        << static_cast<double>(select_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_remaining_us) / 1000000.0 << '\n';
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

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);

        ensure_parent_dir(options.output_path);
        std::ofstream out(options.output_path);
        require(
            static_cast<bool>(out),
            "Cannot open output: " + options.output_path.string());
        write_header(out);

        std::cout << "Simple selection baseline diagnostic written:\n"
                  << "  " << options.output_path << "\n"
                  << "datasets=" << datasets.size() << "\n";

        for (const DatasetInput& dataset : datasets) {
            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(
                !queries.empty(),
                "queries should not be empty: " + dataset.path.string());

            gro::GROAlgorithm algorithm(
                graph,
                algorithm_options,
                traffic_options);

            std::vector<gro::Route> routes =
                algorithm.compute_initial_routes(queries);
            gro::TrafficResult traffic_result =
                gro::evaluate_traffic(graph, queries, routes, traffic_options);
            gro::Cost total_before = traffic_result.total_travel_time;

            for (const std::string& method : options.methods) {
                for (int fraction : options.random_fractions) {
                    auto select_start = gro::Clock::now();
                    std::vector<gro::QueryId> selected_ids =
                        select_query_ids(
                            method,
                            queries.size(),
                            fraction,
                            options.random_seed,
                            routes,
                            traffic_result);
                    long long select_us = gro::elapsed_us(select_start);
                    std::unordered_set<gro::QueryId> selected_set =
                        make_query_set(selected_ids);

                    auto evaluate_start = gro::Clock::now();
                    gro::Cost unselected_after_remove =
                        unselected_after_remove_total(
                            graph,
                            queries,
                            routes,
                            selected_set,
                            traffic_options);
                    long long evaluate_remaining_us =
                        gro::elapsed_us(evaluate_start);

                    write_row(
                        out,
                        dataset.info,
                        queries.size(),
                        method,
                        fraction,
                        options.random_seed,
                        selected_ids.size(),
                        total_before,
                        unselected_after_remove,
                        select_us,
                        evaluate_remaining_us);
                }
            }
            std::cout << "dataset=" << dataset.info.dataset << " done\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
