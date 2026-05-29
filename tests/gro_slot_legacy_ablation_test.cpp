#include "gro.hpp"
#include "gro_slot_legacy.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
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
    std::filesystem::path output_path = "python/results/mh/gro_slot_legacy_ablation.csv";
    std::unordered_set<std::string> dataset_filter;
    std::vector<std::string> methods = {
        "legacy_slot_normal",
        "legacy_slot_tdg",
    };
    int hop_filter = -1;
    int rep_filter = -1;
    int max_files = 0;
    int slot_width = 1;
    int tau = 90;
    int gamma = 50;
    int lambda = 80;
    int capacity_ignore = -1;
    int reroute_impact_weight = 100;
    int random_fraction = 10;
    unsigned int random_seed = 0;
    bool random_seed_set = false;
};

std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() &&
           std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    return text.substr(first, last - first);
}

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

std::vector<std::string> parse_string_list(const std::string& text) {
    std::vector<std::string> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        value = trim(value);
        if (!value.empty()) {
            values.push_back(value);
        }
    }
    return values;
}

void add_dataset_filter_names(
    std::unordered_set<std::string>& filter,
    const std::string& text) {
    for (const std::string& value : parse_string_list(text)) {
        filter.insert(value);
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
        } else if (arg == "--query-dir") {
            options.query_dir = require_value(arg);
        } else if (arg == "--output") {
            options.output_path = require_value(arg);
        } else if (arg == "--datasets") {
            add_dataset_filter_names(options.dataset_filter, require_value(arg));
        } else if (arg == "--methods") {
            options.methods = parse_string_list(require_value(arg));
        } else if (arg == "--hop") {
            options.hop_filter = std::stoi(require_value(arg));
        } else if (arg == "--rep") {
            options.rep_filter = std::stoi(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--slot-width") {
            options.slot_width = std::stoi(require_value(arg));
        } else if (arg == "--tau") {
            options.tau = parse_percent_value(require_value(arg));
        } else if (arg == "--gamma") {
            options.gamma = parse_percent_value(require_value(arg));
        } else if (arg == "--lambda") {
            options.lambda = parse_percent_value(require_value(arg));
        } else if (arg == "--capacity-ignore") {
            options.capacity_ignore = std::stoi(require_value(arg));
        } else if (arg == "--legacy-reroute-weight") {
            options.reroute_impact_weight = parse_percent_value(require_value(arg));
        } else if (arg == "--random-fraction") {
            options.random_fraction = parse_percent_value(require_value(arg));
        } else if (arg == "--random-seed") {
            options.random_seed =
                static_cast<unsigned int>(std::stoul(require_value(arg)));
            options.random_seed_set = true;
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_slot_legacy_ablation_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--methods legacy_slot_normal,legacy_slot_tdg,random_normal] "
                << "[--slot-width 1] [--tau 90] [--gamma 50] [--lambda 80] "
                << "[--capacity-ignore n] [--legacy-reroute-weight 100] "
                << "[--random-fraction 10] [--hop 20] [--rep 4] "
                << "[--datasets Hop20Rep4-4] [--max-files n] [--random-seed n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    require(!options.methods.empty(), "At least one method is required");
    require(options.slot_width > 0, "slot width must be positive");
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
        if (entry.is_regular_file() &&
            std::regex_match(entry.path().filename().string(), pattern)) {
            datasets.push_back({dataset_info_from_path(entry.path()), entry.path()});
        }
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
        datasets.push_back({dataset_info_from_path(query_path), query_path});
    }

    if (!options.dataset_filter.empty()) {
        datasets.erase(
            std::remove_if(
                datasets.begin(),
                datasets.end(),
                [&](const DatasetInput& dataset) {
                    return options.dataset_filter.find(dataset.info.dataset) ==
                           options.dataset_filter.end();
                }),
            datasets.end());
    }

    if (options.hop_filter >= 0 || options.rep_filter >= 0) {
        datasets.erase(
            std::remove_if(
                datasets.begin(),
                datasets.end(),
                [&](const DatasetInput& dataset) {
                    bool hop_mismatch =
                        options.hop_filter >= 0 &&
                        dataset.info.hop != options.hop_filter;
                    bool rep_mismatch =
                        options.rep_filter >= 0 &&
                        dataset.info.rep != options.rep_filter;
                    return hop_mismatch || rep_mismatch;
                }),
            datasets.end());
    }

    if (options.max_files > 0 &&
        static_cast<std::size_t>(options.max_files) < datasets.size()) {
        datasets.resize(static_cast<std::size_t>(options.max_files));
    }

    require(!datasets.empty(), "No query datasets found");
    return datasets;
}

std::vector<gro::QueryId> random_query_ids(
    std::size_t query_count,
    int fraction,
    unsigned int seed) {
    std::size_t selected_count =
        static_cast<std::size_t>(std::clamp(fraction, 0, 100)) *
        query_count /
        100;
    std::vector<gro::QueryId> ids(query_count);
    std::iota(ids.begin(), ids.end(), 0);
    std::shuffle(ids.begin(), ids.end(), std::mt19937{seed});
    if (selected_count < ids.size()) {
        ids.resize(selected_count);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void replace_routes(
    std::vector<gro::Route>& routes,
    const std::vector<gro::Route>& new_routes) {
    for (const gro::Route& route : new_routes) {
        if (route.query_id >= 0 &&
            route.query_id < static_cast<gro::QueryId>(routes.size()) &&
            !route.edge_ids.empty()) {
            routes[route.query_id] = route;
        }
    }
}

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,query_count,iteration,method,"
        << "slot_width,tau,gamma,lambda,capacity_ignore,legacy_reroute_weight,"
        << "random_fraction,random_seed,"
        << "selected_count,candidate_count,source_node_count,tdg_node_count,"
        << "total_before,total_after,reduction,"
        << "initial_routes_sec,evaluate_before_sec,slot_tdg_sec,select_sec,"
        << "reroute_sec,evaluate_after_sec,iteration_sec,cumulative_sec\n";
}

void write_row(
    std::ofstream& out,
    const std::string& run_id,
    const DatasetInfo& dataset,
    std::size_t query_count,
    int iteration,
    const std::string& method,
    const Options& options,
    std::size_t selected_count,
    std::size_t candidate_count,
    std::size_t source_node_count,
    std::size_t tdg_node_count,
    gro::Cost total_before,
    gro::Cost total_after,
    long long initial_routes_us,
    long long evaluate_before_us,
    long long tdg_us,
    long long select_us,
    long long reroute_us,
    long long evaluate_after_us,
    long long iteration_us,
    long long cumulative_us) {
    out << std::setprecision(12)
        << run_id << ','
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << query_count << ','
        << iteration << ','
        << method << ','
        << options.slot_width << ','
        << options.tau << ','
        << options.gamma << ','
        << options.lambda << ','
        << options.capacity_ignore << ','
        << options.reroute_impact_weight << ','
        << options.random_fraction << ','
        << options.random_seed << ','
        << selected_count << ','
        << candidate_count << ','
        << source_node_count << ','
        << tdg_node_count << ','
        << total_before << ','
        << total_after << ','
        << (total_before - total_after) << ','
        << static_cast<double>(initial_routes_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_before_us) / 1000000.0 << ','
        << static_cast<double>(tdg_us) / 1000000.0 << ','
        << static_cast<double>(select_us) / 1000000.0 << ','
        << static_cast<double>(reroute_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_after_us) / 1000000.0 << ','
        << static_cast<double>(iteration_us) / 1000000.0 << ','
        << static_cast<double>(cumulative_us) / 1000000.0 << '\n';
    out.flush();
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
        if (options.capacity_ignore < 0) {
            options.capacity_ignore = traffic_options.min_bpr_capacity;
        }

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);

        ensure_parent_dir(options.output_path);
        std::ofstream out(options.output_path);
        require(static_cast<bool>(out), "Cannot open output: " + options.output_path.string());
        write_header(out);

        std::cout << "Slot legacy ablation written:\n"
                  << "  " << options.output_path << "\n"
                  << "datasets=" << datasets.size() << "\n";

        gro::GROAlgorithm base_algorithm(graph, algorithm_options, traffic_options);
        gro::SlotLegacyOptions legacy_options;
        legacy_options.slot_width = options.slot_width;
        legacy_options.tau_percentile = options.tau;
        legacy_options.gamma = options.gamma;
        legacy_options.lambda = options.lambda;
        legacy_options.capacity_ignore = options.capacity_ignore;
        legacy_options.reroute_impact_weight = options.reroute_impact_weight;
        gro::SlotLegacyGRO legacy_algorithm(graph, legacy_options, traffic_options);

        std::size_t rows_written = 0;
        for (const DatasetInput& dataset : datasets) {
            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(!queries.empty(), "queries should not be empty: " + dataset.path.string());

            auto initial_start = gro::Clock::now();
            std::vector<gro::Route> initial_routes =
                base_algorithm.compute_initial_routes(queries);
            long long initial_routes_us = gro::elapsed_us(initial_start);

            for (const std::string& method : options.methods) {
                std::vector<gro::Route> routes = initial_routes;
                long long cumulative_us = 0;

                for (int iteration = 0;
                     iteration < algorithm_options.max_iterations;
                     ++iteration) {
                    auto iteration_start = gro::Clock::now();

                    auto evaluate_start = gro::Clock::now();
                    gro::TrafficResult traffic_result =
                        gro::evaluate_traffic(graph, queries, routes, traffic_options);
                    long long evaluate_before_us =
                        gro::elapsed_us(evaluate_start);
                    gro::Cost total_before = traffic_result.total_travel_time;

                    std::vector<gro::QueryId> selected_query_ids;
                    std::size_t candidate_count = 0;
                    std::size_t source_node_count = 0;
                    std::size_t tdg_node_count = 0;
                    long long tdg_us = 0;
                    long long select_us = 0;

                    gro::SlotLegacyTDG tdg;
                    if (method == "legacy_slot_normal" ||
                        method == "legacy_slot_tdg") {
                        auto tdg_start = gro::Clock::now();
                        tdg = legacy_algorithm.build_tdg(traffic_result);
                        tdg_us = gro::elapsed_us(tdg_start);
                        tdg_node_count = tdg.nodes.size();

                        auto select_start = gro::Clock::now();
                        gro::SlotLegacySelectionResult selection =
                            legacy_algorithm.select_queries(
                                queries,
                                traffic_result,
                                tdg);
                        select_us = gro::elapsed_us(select_start);
                        selected_query_ids =
                            std::move(selection.selected_query_ids);
                        candidate_count = selection.candidate_count;
                        source_node_count = selection.source_node_count;
                    } else if (method == "random_normal") {
                        auto select_start = gro::Clock::now();
                        selected_query_ids =
                            random_query_ids(
                                queries.size(),
                                options.random_fraction,
                                options.random_seed +
                                    static_cast<unsigned int>(iteration));
                        select_us = gro::elapsed_us(select_start);
                    } else {
                        throw std::runtime_error("Unknown method: " + method);
                    }

                    std::vector<gro::Route> new_routes;
                    auto reroute_start = gro::Clock::now();
                    if (method == "legacy_slot_tdg") {
                        new_routes =
                            legacy_algorithm.reroute_queries(
                                selected_query_ids,
                                queries,
                                traffic_result,
                                tdg);
                    } else {
                        new_routes =
                            base_algorithm.baseline_reroute_queries(
                                selected_query_ids,
                                queries,
                                traffic_result);
                    }
                    long long reroute_us = gro::elapsed_us(reroute_start);

                    std::vector<gro::Route> candidate_routes = routes;
                    replace_routes(candidate_routes, new_routes);

                    evaluate_start = gro::Clock::now();
                    gro::TrafficResult after_result =
                        gro::evaluate_traffic(
                            graph,
                            queries,
                            candidate_routes,
                            traffic_options);
                    long long evaluate_after_us =
                        gro::elapsed_us(evaluate_start);
                    gro::Cost total_after = after_result.total_travel_time;

                    replace_routes(routes, new_routes);

                    long long iteration_us = gro::elapsed_us(iteration_start);
                    cumulative_us += iteration_us;
                    std::string run_id =
                        dataset.info.dataset +
                        "_" +
                        method +
                        "_slot" +
                        std::to_string(options.slot_width) +
                        "_tau" +
                        std::to_string(options.tau) +
                        "_gamma" +
                        std::to_string(options.gamma) +
                        "_iter" +
                        std::to_string(iteration);

                    write_row(
                        out,
                        run_id,
                        dataset.info,
                        queries.size(),
                        iteration,
                        method,
                        options,
                        selected_query_ids.size(),
                        candidate_count,
                        source_node_count,
                        tdg_node_count,
                        total_before,
                        total_after,
                        iteration == 0 ? initial_routes_us : 0,
                        evaluate_before_us,
                        tdg_us,
                        select_us,
                        reroute_us,
                        evaluate_after_us,
                        iteration_us,
                        cumulative_us);
                    ++rows_written;
                }
            }

            std::cout << "dataset=" << dataset.info.dataset
                      << " done rows_written=" << rows_written << "\n";
        }

        out.close();
        require(static_cast<bool>(out), "Failed while writing output: " + options.output_path.string());
        std::cout << "DONE datasets=" << datasets.size()
                  << " rows=" << rows_written << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
