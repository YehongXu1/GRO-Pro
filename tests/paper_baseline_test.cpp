#include "fahl.hpp"
#include "gor.hpp"
#include "sor.hpp"
#include "svp.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
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

constexpr gro::Time kSorDefaultTimeStep = 1800;
constexpr int kSorDefaultMaxTimeSteps = 48;  // 24h horizon with 30min buckets.
constexpr int kSorDefaultMaxLabelsPerQuery = 3;

struct Options {
    std::string config_path = "config/config.yaml";
    std::filesystem::path query_dir;
    std::filesystem::path query_file;
    std::filesystem::path output_path =
        "python/results/paper_baselines.csv";
    std::vector<std::string> methods = {"svp", "gor", "sor", "fahl"};
    int max_files = 0;
    int max_queries = 0;
    int rep_filter = -1;
    int svp_k = 3;
    int svp_theta = 80;
    int sor_detour_percent = 20;
    gro::Time sor_time_step = kSorDefaultTimeStep;
    int sor_max_time_steps = kSorDefaultMaxTimeSteps;
    int sor_max_labels_per_query = kSorDefaultMaxLabelsPerQuery;
    int fahl_alpha_percent = 50;
    gro::Time fahl_time_step = 60;
    int fahl_order_beta_percent = 70;
};

struct RunStats {
    long long reference_us = 0;
    long long profile_us = 0;
    long long index_us = 0;
    long long route_us = 0;
    long long evaluate_us = 0;
    gro::Cost final_total_travel_time = 0;
    std::size_t unreachable_count = 0;
    gro::Time fahl_effective_time_step = 0;
    std::size_t fahl_query_buckets = 0;
};

double seconds(long long microseconds) {
    return static_cast<double>(microseconds) / 1000000.0;
}

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

std::vector<std::string> parse_csv(const std::string& text) {
    std::vector<std::string> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        value = trim(value);
        if (!value.empty()) {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
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
        if (arg == "--query-dir") {
            options.query_dir = require_value(arg);
        } else if (arg == "--query-file") {
            options.query_file = require_value(arg);
        } else if (arg == "--output") {
            options.output_path = require_value(arg);
        } else if (arg == "--methods") {
            options.methods = parse_csv(require_value(arg));
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--max-queries") {
            options.max_queries = std::stoi(require_value(arg));
        } else if (arg == "--rep") {
            options.rep_filter = std::stoi(require_value(arg));
        } else if (arg == "--svp-k") {
            options.svp_k = std::stoi(require_value(arg));
        } else if (arg == "--svp-theta") {
            options.svp_theta = std::stoi(require_value(arg));
        } else if (arg == "--sor-detour-percent") {
            options.sor_detour_percent = std::stoi(require_value(arg));
        } else if (arg == "--sor-time-step") {
            options.sor_time_step =
                static_cast<gro::Time>(std::stoll(require_value(arg)));
        } else if (arg == "--sor-max-time-steps") {
            options.sor_max_time_steps = std::stoi(require_value(arg));
        } else if (arg == "--sor-max-labels-per-query") {
            options.sor_max_labels_per_query = std::stoi(require_value(arg));
        } else if (arg == "--fahl-alpha-percent") {
            options.fahl_alpha_percent = std::stoi(require_value(arg));
        } else if (arg == "--fahl-time-step") {
            options.fahl_time_step =
                static_cast<gro::Time>(std::stoll(require_value(arg)));
        } else if (arg == "--fahl-order-beta-percent") {
            options.fahl_order_beta_percent = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./paper_baseline_test [config] "
                << "[--query-dir path | --query-file path] "
                << "[--output path] [--methods svp,gor,sor,fahl] "
                << "[--rep n] [--max-files n] [--max-queries n] "
                << "[--fahl-time-step sec] [--fahl-order-beta-percent n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    require(!options.methods.empty(), "At least one method is required");
    require(
        options.fahl_order_beta_percent >= 0 &&
            options.fahl_order_beta_percent <= 100,
        "--fahl-order-beta-percent must be in [0, 100]");
    return options;
}

DatasetInfo dataset_info_from_path(const std::filesystem::path& path) {
    DatasetInfo info;
    info.dataset = path.stem().string();

    std::smatch match;
    std::string filename = path.filename().string();
    std::regex synthetic_pattern(R"(Hop([0-9]+)Rep([0-9]+)-([0-9]+)\.txt)");
    std::regex bj_real_pattern(R"(BJRealRep([0-9]+)-([0-9]+)\.txt)");
    if (std::regex_match(filename, match, synthetic_pattern)) {
        info.hop = std::stoi(match[1].str());
        info.rep = std::stoi(match[2].str());
        info.seed = std::stoi(match[3].str());
    } else if (std::regex_match(filename, match, bj_real_pattern)) {
        info.hop = -1;
        info.rep = std::stoi(match[1].str());
        info.seed = std::stoi(match[2].str());
    }
    return info;
}

std::vector<DatasetInput> discover_datasets(
    const std::filesystem::path& query_dir,
    const Options& options) {
    std::vector<DatasetInput> datasets;
    std::regex query_pattern(
        R"((Hop([0-9]+)Rep([0-9]+)-([0-9]+)|BJRealRep([0-9]+)-([0-9]+))\.txt)");
    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt") {
            continue;
        }
        if (!std::regex_match(entry.path().filename().string(), query_pattern)) {
            continue;
        }
        DatasetInput input{dataset_info_from_path(entry.path()), entry.path()};
        if (options.rep_filter >= 0 && input.info.rep != options.rep_filter) {
            continue;
        }
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
            return lhs.info.seed < rhs.info.seed;
        });
    if (options.max_files > 0 &&
        static_cast<std::size_t>(options.max_files) < datasets.size()) {
        datasets.resize(static_cast<std::size_t>(options.max_files));
    }
    return datasets;
}

std::vector<DatasetInput> build_dataset_list(
    const gro::InputConfig& input,
    const Options& options) {
    if (!options.query_file.empty()) {
        return {DatasetInput{
            dataset_info_from_path(options.query_file),
            options.query_file}};
    }

    std::filesystem::path query_dir = options.query_dir;
    if (query_dir.empty()) {
        require(
            !input.queries_path.empty(),
            "Provide --query-dir or --query-file, or set queries_path in config");
        query_dir = std::filesystem::path(input.queries_path).parent_path();
    }
    return discover_datasets(query_dir, options);
}

std::size_t count_unreachable(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const std::vector<gro::Route>& routes) {
    auto reaches_destination = [&](const gro::Query& query, const gro::Route& route) {
        gro::NodeId current = query.origin;
        for (gro::EdgeId edge_id : route.edge_ids) {
            if (edge_id < 0 ||
                edge_id >= static_cast<gro::EdgeId>(graph.edges.size())) {
                return false;
            }
            const gro::Edge& edge = graph.edges[edge_id];
            if (edge.from != current) {
                return false;
            }
            current = edge.to;
        }
        return current == query.destination;
    };

    std::size_t count = 0;
    for (std::size_t i = 0; i < queries.size() && i < routes.size(); ++i) {
        if (!reaches_destination(queries[i], routes[i])) {
            ++count;
        }
    }
    return count;
}

std::vector<gro::Route> compute_shortest_path_routes(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries) {
    std::vector<gro::Route> routes;
    routes.reserve(queries.size());
    for (const gro::Query& query : queries) {
        routes.push_back(gro::shortest_path(graph, query));
    }
    return routes;
}

std::size_t unique_od_count(const std::vector<gro::Query>& queries) {
    std::unordered_map<std::pair<gro::NodeId, gro::NodeId>, char, gro::PairHash> seen;
    for (const gro::Query& query : queries) {
        seen[{query.origin, query.destination}] = 1;
    }
    return seen.size();
}

gro::Cost evaluate_total(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    std::vector<gro::Route> routes,
    const gro::TrafficOptions& traffic_options,
    long long& evaluate_us) {
    auto start = gro::Clock::now();
    gro::TrafficResult result =
        gro::evaluate_traffic(graph, queries, routes, traffic_options);
    evaluate_us = gro::elapsed_us(start);
    return result.total_travel_time;
}

RunStats run_svp(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const gro::TrafficOptions& traffic_options,
    const Options& options) {
    RunStats stats;
    std::cerr << "  [phase] svp route_start queries=" << queries.size()
              << " unique_od=" << unique_od_count(queries)
              << " k=" << options.svp_k
              << " theta=" << options.svp_theta << "\n";
    auto start = gro::Clock::now();
    std::vector<gro::Route> routes = gro::compute_svp_baseline_routes(
        graph,
        queries,
        gro::SVPOptions{options.svp_k, options.svp_theta});
    stats.route_us = gro::elapsed_us(start);
    std::cerr << "  [phase] svp route_done sec=" << seconds(stats.route_us)
              << "\n";
    stats.unreachable_count = count_unreachable(graph, queries, routes);
    std::cerr << "  [phase] svp evaluate_start\n";
    stats.final_total_travel_time =
        evaluate_total(graph, queries, routes, traffic_options, stats.evaluate_us);
    std::cerr << "  [phase] svp evaluate_done sec="
              << seconds(stats.evaluate_us) << "\n";
    return stats;
}

RunStats run_gor(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const gro::TrafficOptions& traffic_options) {
    RunStats stats;
    std::cerr << "  [phase] gor route_start queries=" << queries.size()
              << "\n";
    auto start = gro::Clock::now();
    std::vector<gro::Route> routes =
        gro::compute_gor_greedy_routes(graph, queries, traffic_options);
    stats.route_us = gro::elapsed_us(start);
    std::cerr << "  [phase] gor route_done sec=" << seconds(stats.route_us)
              << "\n";
    stats.unreachable_count = count_unreachable(graph, queries, routes);
    std::cerr << "  [phase] gor evaluate_start\n";
    stats.final_total_travel_time =
        evaluate_total(graph, queries, routes, traffic_options, stats.evaluate_us);
    std::cerr << "  [phase] gor evaluate_done sec="
              << seconds(stats.evaluate_us) << "\n";
    return stats;
}

RunStats run_sor(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const gro::TrafficOptions& traffic_options,
    const Options& options) {
    RunStats stats;
    gro::SOROptions sor_options;
    sor_options.detour_percent = options.sor_detour_percent;
    sor_options.time_step = options.sor_time_step;
    sor_options.max_time_steps = options.sor_max_time_steps;
    sor_options.max_labels_per_query = options.sor_max_labels_per_query;

    std::cerr << "  [phase] sor route_start queries=" << queries.size()
              << " detour_percent=" << sor_options.detour_percent
              << " time_step=" << sor_options.time_step
              << " max_time_steps=" << sor_options.max_time_steps
              << " max_time_sec="
              << (sor_options.time_step * sor_options.max_time_steps)
              << " max_labels_per_query=" << sor_options.max_labels_per_query
              << "\n";
    auto start = gro::Clock::now();
    std::vector<gro::Route> routes =
        gro::compute_sor_routes(graph, queries, sor_options);
    stats.route_us = gro::elapsed_us(start);
    std::cerr << "  [phase] sor route_done sec=" << seconds(stats.route_us)
              << "\n";
    stats.unreachable_count = count_unreachable(graph, queries, routes);
    std::cerr << "  [phase] sor evaluate_start\n";
    stats.final_total_travel_time =
        evaluate_total(graph, queries, routes, traffic_options, stats.evaluate_us);
    std::cerr << "  [phase] sor evaluate_done sec="
              << seconds(stats.evaluate_us) << "\n";
    return stats;
}

RunStats run_fahl(
    const gro::Graph& graph,
    const std::vector<gro::Query>& queries,
    const gro::TrafficOptions& traffic_options,
    const Options& options,
    const std::vector<gro::Route>& reference_routes) {
    RunStats stats;
    gro::FAHLOptions fahl_options;
    fahl_options.alpha_percent = options.fahl_alpha_percent;
    fahl_options.order_beta_percent = options.fahl_order_beta_percent;
    fahl_options.time_step = options.fahl_time_step > 0 ? options.fahl_time_step : 1;
    stats.fahl_effective_time_step = fahl_options.time_step;
    std::cerr << "  [phase] fahl single_bucket enabled"
              << " profile_time_step=" << fahl_options.time_step
              << "\n";

    std::cerr << "  [phase] fahl profile_start reference_routes="
              << reference_routes.size()
              << " alpha_percent=" << fahl_options.alpha_percent
              << " order_beta_percent=" << fahl_options.order_beta_percent
              << " time_step=" << fahl_options.time_step << "\n";
    auto profile_start = gro::Clock::now();
    gro::FAHLFlowProfile profile =
        gro::build_fahl_flow_profile(graph, reference_routes, fahl_options.time_step);
    stats.profile_us = gro::elapsed_us(profile_start);
    std::cerr << "  [phase] fahl profile_done sec="
              << seconds(stats.profile_us)
              << " profile_buckets=" << profile.size() << "\n";

    std::map<int, std::vector<const gro::Query*>> queries_by_bucket;
    for (const gro::Query& query : queries) {
        queries_by_bucket[0].push_back(&query);
    }
    stats.fahl_query_buckets = queries_by_bucket.size();
    std::cerr << "  [phase] fahl buckets=" << queries_by_bucket.size()
              << " query_start queries=" << queries.size() << "\n";

    std::vector<gro::Route> routes(queries.size());
    std::size_t bucket_index = 0;
    for (const auto& bucket_entry : queries_by_bucket) {
        int bucket = bucket_entry.first;
        const std::vector<const gro::Query*>& bucket_queries = bucket_entry.second;
        ++bucket_index;
        std::cerr << "  [phase] fahl bucket_start " << bucket_index
                  << "/" << queries_by_bucket.size()
                  << " bucket=" << bucket
                  << " queries=" << bucket_queries.size() << "\n";
        auto index_start = gro::Clock::now();
        gro::FAHLIndex index(graph, profile, bucket, fahl_options);
        long long index_us = gro::elapsed_us(index_start);
        stats.index_us += index_us;
        std::cerr << "  [phase] fahl bucket_index_done " << bucket_index
                  << "/" << queries_by_bucket.size()
                  << " sec=" << seconds(index_us) << "\n";

        auto query_start = gro::Clock::now();
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < static_cast<long long>(bucket_queries.size()); ++i) {
            const gro::Query* query = bucket_queries[static_cast<std::size_t>(i)];
            routes[query->id] = index.query(*query);
        }
        long long route_us = gro::elapsed_us(query_start);
        stats.route_us += route_us;
        std::cerr << "  [phase] fahl bucket_query_done " << bucket_index
                  << "/" << queries_by_bucket.size()
                  << " sec=" << seconds(route_us) << "\n";
    }

    stats.unreachable_count = count_unreachable(graph, queries, routes);
    std::cerr << "  [phase] fahl evaluate_start\n";
    stats.final_total_travel_time =
        evaluate_total(graph, queries, routes, traffic_options, stats.evaluate_us);
    std::cerr << "  [phase] fahl evaluate_done sec="
              << seconds(stats.evaluate_us) << "\n";
    return stats;
}

std::string params_for_method(
    const std::string& method,
    const Options& options,
    const RunStats& stats) {
    std::ostringstream out;
    if (method == "svp") {
        out << "k=" << options.svp_k << ";theta=" << options.svp_theta;
    } else if (method == "gor") {
        out << "variant=greedy;require_remaining_progress=true";
    } else if (method == "sor") {
        out << "detour_percent=" << options.sor_detour_percent
            << ";time_step=" << options.sor_time_step
            << ";max_time_steps=" << options.sor_max_time_steps
            << ";max_time_sec="
            << (options.sor_time_step * options.sor_max_time_steps)
            << ";max_labels_per_query=" << options.sor_max_labels_per_query;
    } else if (method == "fahl") {
        out << "alpha_percent=" << options.fahl_alpha_percent
            << ";order_beta_percent=" << options.fahl_order_beta_percent
            << ";time_step=" << options.fahl_time_step
            << ";effective_time_step=" << stats.fahl_effective_time_step
            << ";query_buckets=" << stats.fahl_query_buckets
            << ";single_bucket=true"
            << ";flow_reference=shortest_path";
    }
    return out.str();
}

void write_header(std::ofstream& out) {
    out << "dataset,hop,rep,seed,method,query_count,unreachable_count,"
        << "initial_total_travel_time,final_total_travel_time,"
        << "avg_initial_travel_time,avg_final_travel_time,"
        << "reference_sec,profile_sec,index_sec,route_sec,evaluate_sec,total_sec,"
        << "parameters\n";
}

void write_row(
    std::ofstream& out,
    const DatasetInfo& info,
    const std::string& method,
    std::size_t query_count,
    gro::Cost initial_total_travel_time,
    const RunStats& stats,
    const Options& options) {
    long long total_us = stats.reference_us + stats.profile_us + stats.index_us +
                         stats.route_us + stats.evaluate_us;
    out << info.dataset << ','
        << info.hop << ','
        << info.rep << ','
        << info.seed << ','
        << method << ','
        << query_count << ','
        << stats.unreachable_count << ','
        << initial_total_travel_time << ','
        << stats.final_total_travel_time << ','
        << std::fixed << std::setprecision(6)
        << (query_count == 0
                ? 0.0
                : static_cast<double>(initial_total_travel_time) /
                      static_cast<double>(query_count))
        << ','
        << (query_count == 0
                ? 0.0
                : static_cast<double>(stats.final_total_travel_time) /
                      static_cast<double>(query_count))
        << ','
        << seconds(stats.reference_us) << ','
        << seconds(stats.profile_us) << ','
        << seconds(stats.index_us) << ','
        << seconds(stats.route_us) << ','
        << seconds(stats.evaluate_us) << ','
        << seconds(total_us) << ','
        << params_for_method(method, options, stats) << '\n';
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
        gro::TrafficOptions traffic_options =
            gro::load_traffic_options(options.config_path);
        std::vector<DatasetInput> datasets = build_dataset_list(input, options);
        require(!datasets.empty(), "No query files found");

        std::ofstream out(options.output_path);
        require(
            static_cast<bool>(out),
            "Cannot open output: " + options.output_path.string());
        write_header(out);

        std::cerr << "Graph loaded: " << graph.vertex_count << " vertices, "
                  << graph.edge_count << " edges.\n";
        std::cerr << "Datasets: " << datasets.size() << "\n";

        for (const DatasetInput& dataset : datasets) {
            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(
                !queries.empty(),
                "queries should not be empty: " + dataset.path.string());
            if (options.max_queries > 0 &&
                static_cast<std::size_t>(options.max_queries) < queries.size()) {
                queries.resize(static_cast<std::size_t>(options.max_queries));
            }

            auto reference_start = gro::Clock::now();
            std::cerr << "[phase] reference_shortest_path_start "
                      << dataset.info.dataset
                      << " queries=" << queries.size() << "\n";
            std::vector<gro::Route> reference_routes =
                compute_shortest_path_routes(graph, queries);
            long long reference_us = gro::elapsed_us(reference_start);
            std::cerr << "[phase] reference_shortest_path_done "
                      << dataset.info.dataset
                      << " sec=" << seconds(reference_us) << "\n";
            std::cerr << "[phase] reference_evaluate_start "
                      << dataset.info.dataset << "\n";
            long long initial_evaluate_us = 0;
            gro::Cost initial_total_travel_time = evaluate_total(
                graph,
                queries,
                reference_routes,
                traffic_options,
                initial_evaluate_us);
            std::cerr << "[phase] reference_evaluate_done "
                      << dataset.info.dataset
                      << " sec=" << seconds(initial_evaluate_us) << "\n";

            std::cerr << "[dataset] " << dataset.info.dataset
                      << " queries=" << queries.size()
                      << " initial=" << initial_total_travel_time << "\n";

            for (const std::string& method : options.methods) {
                std::cerr << "[run] " << method << ' '
                          << dataset.info.dataset << "\n";
                auto total_start = gro::Clock::now();
                RunStats stats;
                if (method == "svp") {
                    stats = run_svp(graph, queries, traffic_options, options);
                } else if (method == "gor") {
                    stats = run_gor(graph, queries, traffic_options);
                } else if (method == "sor") {
                    stats = run_sor(graph, queries, traffic_options, options);
                } else if (method == "fahl") {
                    stats = run_fahl(
                        graph,
                        queries,
                        traffic_options,
                        options,
                        reference_routes);
                    stats.reference_us = reference_us;
                } else {
                    throw std::runtime_error("Unknown method: " + method);
                }
                long long wall_us = gro::elapsed_us(total_start);
                write_row(
                    out,
                    dataset.info,
                    method,
                    queries.size(),
                    initial_total_travel_time,
                    stats,
                    options);
                out.flush();
                std::cerr << "[done] " << method << ' '
                          << dataset.info.dataset
                          << " final=" << stats.final_total_travel_time
                          << " wall_sec=" << seconds(wall_us) << "\n";
            }
        }

        std::cerr << "Wrote " << options.output_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
