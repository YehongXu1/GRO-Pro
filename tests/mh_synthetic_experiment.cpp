#include "gro.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Dataset {
    int hop = 0;
    int rep = 0;
    int seed = 0;
    std::filesystem::path path;

    std::string name() const {
        return "Hop" + std::to_string(hop) +
               "Rep" + std::to_string(rep) +
               "-" + std::to_string(seed);
    }
};

struct Options {
    std::string config_path = "config/config.yaml";
    std::filesystem::path query_dir = "data/MH_Synthetic_query_sets";
    std::filesystem::path output_path = "python/results/mh_synthetic_all_cpp.csv";
    std::vector<std::string> algorithms = {
        "tdg",
        "baseline",
        "tdg_selection_baseline",
        "tdg_reroute_baseline"};
    bool timing_log = false;
    int max_files = 0;
    int hop_filter = -1;
    int rep_filter = -1;
    int seed_filter = -1;
    int max_iterations = -1;
    int beta_override = -1;
    gro::Time max_time_override = -1;
    gro::Flow min_bpr_capacity_override = -1;
};

std::vector<std::string> split_csv(const std::string& text) {
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

    while (index < argc) {
        std::string arg = argv[index++];
        auto require_value = [&](const std::string& flag) {
            if (index >= argc) {
                throw std::runtime_error("Missing value after " + flag);
            }
            return std::string(argv[index++]);
        };

        if (arg == "--query-dir") {
            options.query_dir = require_value(arg);
        } else if (arg == "--output") {
            options.output_path = require_value(arg);
        } else if (arg == "--algorithms") {
            options.algorithms = split_csv(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--max-iterations") {
            options.max_iterations = std::stoi(require_value(arg));
        } else if (arg == "--beta") {
            options.beta_override = std::stoi(require_value(arg));
        } else if (arg == "--max-time") {
            options.max_time_override =
                static_cast<gro::Time>(std::stoll(require_value(arg)));
        } else if (arg == "--min-bpr-capacity") {
            options.min_bpr_capacity_override =
                static_cast<gro::Flow>(std::stoi(require_value(arg)));
        } else if (arg == "--hop") {
            options.hop_filter = std::stoi(require_value(arg));
        } else if (arg == "--rep") {
            options.rep_filter = std::stoi(require_value(arg));
        } else if (arg == "--seed") {
            options.seed_filter = std::stoi(require_value(arg));
        } else if (arg == "--timing-log") {
            options.timing_log = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return options;
}

std::vector<Dataset> discover_datasets(const std::filesystem::path& query_dir) {
    std::vector<Dataset> datasets;
    std::regex pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+)\.txt)");

    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::smatch match;
        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, match, pattern)) {
            continue;
        }
        datasets.push_back(Dataset{
            std::stoi(match[1].str()),
            std::stoi(match[2].str()),
            std::stoi(match[3].str()),
            entry.path()});
    }

    std::sort(
        datasets.begin(),
        datasets.end(),
        [](const Dataset& lhs, const Dataset& rhs) {
            if (lhs.hop != rhs.hop) {
                return lhs.hop < rhs.hop;
            }
            if (lhs.rep != rhs.rep) {
                return lhs.rep < rhs.rep;
            }
            return lhs.seed < rhs.seed;
        });
    return datasets;
}

std::vector<Dataset> filter_datasets(
    const std::vector<Dataset>& datasets,
    const Options& options) {
    std::vector<Dataset> filtered;
    for (const Dataset& dataset : datasets) {
        if (options.hop_filter >= 0 && dataset.hop != options.hop_filter) {
            continue;
        }
        if (options.rep_filter >= 0 && dataset.rep != options.rep_filter) {
            continue;
        }
        if (options.seed_filter >= 0 && dataset.seed != options.seed_filter) {
            continue;
        }
        filtered.push_back(dataset);
    }
    return filtered;
}

std::string canonical_algorithm_name(const std::string& algorithm) {
    if (algorithm == "gro") {
        return "tdg";
    }
    if (algorithm == "random_baseline") {
        return "baseline";
    }
    if (algorithm == "selection_td_baseline") {
        return "tdg_selection_baseline";
    }
    if (algorithm == "normal_selection_gro_reroute") {
        return "tdg_reroute_baseline";
    }
    return algorithm;
}

gro::AlgorithmResult run_algorithm(
    const std::string& algorithm,
    const gro::GROAlgorithm& runner,
    const std::vector<gro::Query>& queries) {
    std::string canonical_algorithm = canonical_algorithm_name(algorithm);
    if (canonical_algorithm == "tdg") {
        return runner.run(queries);
    }
    if (canonical_algorithm == "baseline") {
        return runner.run_baseline(queries);
    }
    if (canonical_algorithm == "tdg_selection_baseline") {
        return runner.run_tdg_selection_baseline(queries);
    }
    if (canonical_algorithm == "tdg_reroute_baseline") {
        return runner.run_tdg_reroute_baseline(queries);
    }
    throw std::runtime_error("Unknown algorithm: " + algorithm);
}

void write_result_rows(
    std::ofstream& out,
    const Dataset& dataset,
    const std::string& algorithm,
    std::size_t query_count,
    const gro::AlgorithmResult& result) {
    for (std::size_t i = 0; i < result.total_travel_time_by_iteration.size(); ++i) {
        out << dataset.name() << ','
            << dataset.hop << ','
            << dataset.rep << ','
            << dataset.seed << ','
            << algorithm << ','
            << query_count << ','
            << i << ','
            << result.total_travel_time_by_iteration[i] << ','
            << result.initial_total_travel_time << ','
            << result.final_total_travel_time << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_args(argc, argv);
        const auto output_dir = options.output_path.parent_path();
        if (!output_dir.empty()) {
            std::filesystem::create_directories(output_dir);
        }

        gro::InputConfig input = gro::load_input_config(options.config_path);
        gro::Graph graph = gro::read_graph(input);
        gro::AlgorithmOptions algorithm_options =
            gro::load_algorithm_options(options.config_path);
        if (options.max_iterations >= 0) {
            algorithm_options.max_iterations = options.max_iterations;
        }
        algorithm_options.enable_timing_log = options.timing_log;
        gro::TrafficOptions traffic_options =
            gro::load_traffic_options(options.config_path);
        if (options.beta_override >= 0) {
            traffic_options.beta = options.beta_override;
        }
        if (options.max_time_override >= 0) {
            traffic_options.max_travel_time = options.max_time_override;
        }
        if (options.min_bpr_capacity_override >= 0) {
            traffic_options.min_bpr_capacity =
                options.min_bpr_capacity_override;
        }

        std::vector<Dataset> datasets =
            filter_datasets(discover_datasets(options.query_dir), options);
        if (options.max_files > 0 &&
            static_cast<std::size_t>(options.max_files) < datasets.size()) {
            datasets.resize(static_cast<std::size_t>(options.max_files));
        }
        if (datasets.empty()) {
            throw std::runtime_error("No synthetic query files found in " + options.query_dir.string());
        }

        std::ofstream out(options.output_path);
        if (!out) {
            throw std::runtime_error("Cannot open output CSV: " + options.output_path.string());
        }
        out << "dataset,hop,rep,seed,algorithm,query_count,iteration,total_travel_time,"
            << "initial_total_travel_time,final_total_travel_time\n";

        std::cerr << "Graph loaded: " << graph.vertex_count << " vertices, "
                  << graph.edge_count << " edges.\n";
        std::cerr << "Datasets: " << datasets.size() << "\n";

        for (const Dataset& dataset : datasets) {
            gro::InputConfig query_input = input;
            query_input.queries_path = dataset.path.string();
            std::vector<gro::Query> queries = gro::read_queries(query_input);

            for (const std::string& requested_algorithm : options.algorithms) {
                std::string algorithm =
                    canonical_algorithm_name(requested_algorithm);
                std::cerr << "[run] " << algorithm << ' ' << dataset.name()
                          << " queries=" << queries.size() << "\n";
                gro::GROAlgorithm runner(graph, algorithm_options, traffic_options);
                gro::AlgorithmResult result =
                    run_algorithm(algorithm, runner, queries);
                write_result_rows(out, dataset, algorithm, queries.size(), result);
                out.flush();
                std::cerr << "[done] " << algorithm << ' ' << dataset.name()
                          << " initial=" << result.initial_total_travel_time
                          << " final=" << result.final_total_travel_time << "\n";
            }
        }

        std::cerr << "Wrote " << options.output_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
