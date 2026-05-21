#include "core.hpp"
#include "gro.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
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
        "python/results/shortest_path_congestion.csv";
    std::unordered_set<std::string> dataset_filter;
    int max_files = 0;
    int hop_filter = -2;
    int rep_filter = -1;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

void load_dataset_filter_file(
    std::unordered_set<std::string>& filter,
    const std::filesystem::path& path) {
    std::ifstream in(path);
    require(static_cast<bool>(in), "Cannot open dataset list: " + path.string());

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        filter.insert(line);
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
        } else if (arg == "--dataset-list") {
            load_dataset_filter_file(options.dataset_filter, require_value(arg));
        } else if (arg == "--hop") {
            options.hop_filter = std::stoi(require_value(arg));
        } else if (arg == "--rep") {
            options.rep_filter = std::stoi(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./shortest_path_congestion_diagnostic [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--datasets name1,name2] [--dataset-list path] "
                << "[--hop n] [--rep n] [--max-files n]\n";
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

    std::regex synthetic_pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+))");
    std::regex bj_real_pattern(R"(BJRealRep([0-9]+)-([0-9]+))");
    std::smatch match;
    if (std::regex_match(info.dataset, match, synthetic_pattern)) {
        info.hop = std::stoi(match[1].str());
        info.rep = std::stoi(match[2].str());
        info.seed = std::stoi(match[3].str());
    } else if (std::regex_match(info.dataset, match, bj_real_pattern)) {
        info.hop = -1;
        info.rep = std::stoi(match[1].str());
        info.seed = std::stoi(match[2].str());
    }
    return info;
}

bool keep_dataset(const DatasetInput& dataset, const Options& options) {
    if (!options.dataset_filter.empty() &&
        options.dataset_filter.find(dataset.info.dataset) ==
            options.dataset_filter.end()) {
        return false;
    }
    if (options.hop_filter != -2 && dataset.info.hop != options.hop_filter) {
        return false;
    }
    if (options.rep_filter >= 0 && dataset.info.rep != options.rep_filter) {
        return false;
    }
    return true;
}

std::vector<DatasetInput> discover_datasets(
    const std::filesystem::path& query_dir) {
    std::vector<DatasetInput> datasets;
    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt") {
            continue;
        }
        DatasetInput input;
        input.path = entry.path();
        input.info = dataset_info_from_path(entry.path());
        datasets.push_back(input);
    }
    std::sort(
        datasets.begin(),
        datasets.end(),
        [](const DatasetInput& lhs, const DatasetInput& rhs) {
            if (lhs.info.rep != rhs.info.rep) {
                return lhs.info.rep < rhs.info.rep;
            }
            if (lhs.info.hop != rhs.info.hop) {
                return lhs.info.hop < rhs.info.hop;
            }
            if (lhs.info.seed != rhs.info.seed) {
                return lhs.info.seed < rhs.info.seed;
            }
            return lhs.info.dataset < rhs.info.dataset;
        });
    return datasets;
}

std::vector<DatasetInput> resolve_datasets(
    const Options& options,
    const gro::InputConfig& input_config) {
    std::vector<DatasetInput> datasets;
    if (!options.query_file.empty()) {
        DatasetInput input;
        input.path = options.query_file;
        input.info = dataset_info_from_path(options.query_file);
        datasets.push_back(input);
    } else {
        std::filesystem::path query_dir = options.query_dir;
        if (query_dir.empty()) {
            require(
                !input_config.queries_path.empty(),
                "Provide --query-dir or --query-file, or set queries_path in config");
            query_dir = std::filesystem::path(input_config.queries_path).parent_path();
        }
        datasets = discover_datasets(query_dir);
    }

    std::vector<DatasetInput> filtered;
    for (const DatasetInput& dataset : datasets) {
        if (keep_dataset(dataset, options)) {
            filtered.push_back(dataset);
        }
    }
    if (options.max_files > 0 &&
        static_cast<std::size_t>(options.max_files) < filtered.size()) {
        filtered.resize(static_cast<std::size_t>(options.max_files));
    }
    return filtered;
}

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_header(std::ofstream& out) {
    out << "dataset,hop,rep,seed,query_count,min_departure,max_departure,"
        << "departure_span_sec,reachable_count,unreachable_count,"
        << "free_flow_ttt,evaluated_ttt,congestion_extra_ttt,"
        << "inflation_ratio,avg_free_flow_tt,avg_evaluated_tt,"
        << "shortest_path_sec,evaluate_sec\n";
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

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);
        require(!datasets.empty(), "No query files matched the requested filters");

        ensure_parent_dir(options.output_path);
        std::ofstream out(options.output_path);
        require(
            static_cast<bool>(out),
            "Cannot open output CSV: " + options.output_path.string());
        write_header(out);

        std::cerr << "Graph loaded: " << graph.vertex_count << " vertices, "
                  << graph.edge_count << " edges.\n";
        std::cerr << "Datasets: " << datasets.size() << "\n";

        gro::GROAlgorithm algorithm(graph, algorithm_options, traffic_options);
        for (const DatasetInput& dataset : datasets) {
            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(
                !queries.empty(),
                "queries should not be empty: " + dataset.path.string());

            gro::Time min_departure = queries.front().departure_time;
            gro::Time max_departure = queries.front().departure_time;
            for (const gro::Query& query : queries) {
                min_departure = std::min(min_departure, query.departure_time);
                max_departure = std::max(max_departure, query.departure_time);
            }

            auto shortest_start = gro::Clock::now();
            std::vector<gro::Route> routes =
                algorithm.compute_initial_routes(queries);
            long long shortest_us = gro::elapsed_us(shortest_start);

            gro::Cost free_flow_ttt = 0;
            std::size_t reachable_count = 0;
            std::size_t unreachable_count = 0;
            for (std::size_t i = 0; i < routes.size(); ++i) {
                const gro::Query& query = queries[i];
                const gro::Route& route = routes[i];
                if (query.origin != query.destination && route.edge_ids.empty()) {
                    ++unreachable_count;
                    continue;
                }
                ++reachable_count;
                free_flow_ttt += route.travel_time;
            }

            std::vector<gro::Route> evaluated_routes = routes;
            auto evaluate_start = gro::Clock::now();
            gro::TrafficResult traffic =
                gro::evaluate_traffic(
                    graph,
                    queries,
                    evaluated_routes,
                    traffic_options);
            long long evaluate_us = gro::elapsed_us(evaluate_start);

            double inflation_ratio =
                free_flow_ttt > 0
                    ? static_cast<double>(traffic.total_travel_time) /
                          static_cast<double>(free_flow_ttt)
                    : 0.0;
            double avg_free_flow =
                reachable_count > 0
                    ? static_cast<double>(free_flow_ttt) /
                          static_cast<double>(reachable_count)
                    : 0.0;
            double avg_evaluated =
                reachable_count > 0
                    ? static_cast<double>(traffic.total_travel_time) /
                          static_cast<double>(reachable_count)
                    : 0.0;

            out << std::setprecision(12)
                << dataset.info.dataset << ','
                << dataset.info.hop << ','
                << dataset.info.rep << ','
                << dataset.info.seed << ','
                << queries.size() << ','
                << min_departure << ','
                << max_departure << ','
                << (max_departure - min_departure) << ','
                << reachable_count << ','
                << unreachable_count << ','
                << free_flow_ttt << ','
                << traffic.total_travel_time << ','
                << (traffic.total_travel_time - free_flow_ttt) << ','
                << inflation_ratio << ','
                << avg_free_flow << ','
                << avg_evaluated << ','
                << static_cast<double>(shortest_us) / 1000000.0 << ','
                << static_cast<double>(evaluate_us) / 1000000.0 << '\n';
            out.flush();

            std::cerr << "[done] " << dataset.info.dataset
                      << " queries=" << queries.size()
                      << " free_flow=" << free_flow_ttt
                      << " evaluated=" << traffic.total_travel_time
                      << " ratio=" << inflation_ratio << "\n";
        }

        std::cerr << "Wrote " << options.output_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
