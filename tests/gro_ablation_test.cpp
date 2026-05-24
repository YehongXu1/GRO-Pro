#include "gro.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

enum class RemovalMode {
    AllNodes,
    CongestionImportant,
    AnchorImportant,
    BprRelief,
};

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
    std::unordered_set<std::string> dataset_filter;
    std::filesystem::path output_path = "python/results/gro_ablation.csv";
    unsigned int random_seed = 0;
    bool random_seed_set = false;
    int max_files = 0;
    int hop_filter = -1;
    int rep_filter = -1;
    std::vector<int> fixed_fractions = {10, 30};
    std::vector<std::string> selection_methods = {
        "random",
        "most_delayed",
        "tdg_anchor",
    };
    std::vector<std::string> reroute_methods = {"normal", "tdg"};
    std::vector<int> tdg_gammas = {50};
    std::vector<int> impact_weights = {30};
    std::string candidate_filter = "all";
    bool progress_log = true;
};

struct SelectionRun {
    std::string method;
    RemovalMode removal_mode = RemovalMode::AllNodes;
    int selection_fraction = -1;
    int gamma = -1;
    std::vector<gro::QueryId> selected_ids;
    long long important_node_count = 0;
    long long important_nodes_us = 0;
    std::string candidate_filter = "none";
    long long candidate_count = -1;
    long long candidate_us = 0;
    long long select_us = 0;
};

struct ScoreStats {
    long double sum = 0.0;
    long double mean = 0.0;
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
        value = trim(value);
        if (!value.empty()) {
            values.push_back(parse_percent_value(value));
        }
    }
    return values;
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
        } else if (arg == "--datasets") {
            add_dataset_filter_names(options.dataset_filter, require_value(arg));
        } else if (arg == "--dataset-list") {
            load_dataset_filter_file(options.dataset_filter, require_value(arg));
        } else if (arg == "--hop") {
            options.hop_filter = std::stoi(require_value(arg));
        } else if (arg == "--rep") {
            options.rep_filter = std::stoi(require_value(arg));
        } else if (arg == "--output") {
            options.output_path = require_value(arg);
        } else if (arg == "--random-seed") {
            options.random_seed =
                static_cast<unsigned int>(std::stoul(require_value(arg)));
            options.random_seed_set = true;
        } else if (arg == "--fixed-fraction") {
            options.fixed_fractions = {parse_percent_value(require_value(arg))};
        } else if (arg == "--fixed-fractions") {
            options.fixed_fractions = parse_percent_list(require_value(arg));
        } else if (arg == "--selection-methods") {
            options.selection_methods = parse_string_list(require_value(arg));
        } else if (arg == "--reroute-methods") {
            options.reroute_methods = parse_string_list(require_value(arg));
        } else if (arg == "--tdg-gammas") {
            options.tdg_gammas = parse_percent_list(require_value(arg));
        } else if (arg == "--impact-weights") {
            options.impact_weights = parse_percent_list(require_value(arg));
        } else if (arg == "--candidate-filter") {
            options.candidate_filter = require_value(arg);
        } else if (arg == "--max-files") {
            options.max_files = std::stoi(require_value(arg));
        } else if (arg == "--no-progress-log") {
            options.progress_log = false;
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./gro_ablation_test [config] "
                << "[--query-file path | --query-dir path] [--output path] "
                << "[--selection-methods random,most_delayed,tdg_anchor,tdg_excess,tdg_bpr_relief] "
                << "[--reroute-methods normal,tdg] "
                << "[--fixed-fractions 10,30] [--tdg-gammas 50] "
                << "[--impact-weights 30] "
                << "[--candidate-filter all|source_congestion|score_top|global_score|component_balanced|component_marginal|component_marginal_budget5|component_marginal_budget3|component_marginal_samek|component_marginal_major80_budget5|component_marginal_major90_budget5|component_marginal_major90_samek] "
                << "[--hop 10] [--rep 1] "
                << "[--datasets Hop10Rep1-0,BJRealRep10-0] "
                << "[--dataset-list path] [--random-seed n] [--max-files n] "
                << "[--no-progress-log]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    require(!options.selection_methods.empty(), "At least one selection method is required");
    require(!options.reroute_methods.empty(), "At least one reroute method is required");
    require(!options.fixed_fractions.empty(), "At least one fixed selection fraction is required");
    require(!options.tdg_gammas.empty(), "At least one TDG gamma is required");
    require(!options.impact_weights.empty(), "At least one impact weight is required");
    require(
        options.candidate_filter == "all" ||
            options.candidate_filter == "source_congestion" ||
            options.candidate_filter == "score_top" ||
            options.candidate_filter == "global_score" ||
            options.candidate_filter == "component_balanced" ||
            options.candidate_filter == "component_marginal" ||
            options.candidate_filter == "component_marginal_budget5" ||
            options.candidate_filter == "component_marginal_budget3" ||
            options.candidate_filter == "component_marginal_samek" ||
            options.candidate_filter == "component_marginal_major80_budget5" ||
            options.candidate_filter == "component_marginal_major90_budget5" ||
            options.candidate_filter == "component_marginal_major90_samek",
        "Unknown candidate filter: " + options.candidate_filter +
            " (expected all, source_congestion, score_top, global_score, component_balanced, component_marginal, component_marginal_budget5, component_marginal_budget3, component_marginal_samek, component_marginal_major80_budget5, component_marginal_major90_budget5, or component_marginal_major90_samek)");
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

std::vector<DatasetInput> discover_datasets(
    const std::filesystem::path& query_dir) {
    std::vector<DatasetInput> datasets;
    std::regex pattern(
        R"((Hop([0-9]+)Rep([0-9]+)-([0-9]+)|BJRealRep([0-9]+)-([0-9]+))\.txt)");

    for (const auto& entry : std::filesystem::directory_iterator(query_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!std::regex_match(entry.path().filename().string(), pattern)) {
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
    for (const auto& item : ranking) {
        ids.push_back(item.second);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t percentile_index(std::size_t size, int percentile) {
    if (size == 0) {
        return 0;
    }
    int clamped = std::clamp(percentile, 0, 100);
    return std::min(
        size - 1,
        static_cast<std::size_t>(
            static_cast<long double>(clamped) *
            static_cast<long double>(size - 1) / 100.0L));
}

gro::Cost congestion_ratio_key(gro::Flow flow, gro::Flow capacity) {
    if (capacity <= 0) {
        return std::numeric_limits<gro::Cost>::max();
    }
    return static_cast<gro::Cost>(
        static_cast<long double>(flow) * 1000000.0L /
        static_cast<long double>(capacity));
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
    case RemovalMode::BprRelief:
        return all_tdg_nodes_important(tdg);
    }
    return all_tdg_nodes_important(tdg);
}

std::string removal_mode_name(RemovalMode mode) {
    switch (mode) {
    case RemovalMode::AllNodes:
        return "all_nodes";
    case RemovalMode::CongestionImportant:
        return "congestion_important";
    case RemovalMode::AnchorImportant:
        return "anchor_important";
    case RemovalMode::BprRelief:
        return "bpr_relief";
    }
    return "all_nodes";
}

RemovalMode tdg_selection_mode_from_name(const std::string& method) {
    if (method == "tdg_all" || method == "tdg_all_nodes") {
        return RemovalMode::AllNodes;
    }
    if (method == "tdg_congestion" || method == "tdg_congestion_important") {
        return RemovalMode::CongestionImportant;
    }
    if (method == "tdg_anchor" || method == "tdg_anchor_important" ||
        method == "tdg") {
        return RemovalMode::AnchorImportant;
    }
    throw std::runtime_error("Unknown TDG selection method: " + method);
}

bool is_tdg_selection_method(const std::string& method) {
    return method.rfind("tdg", 0) == 0;
}

bool is_tdg_excess_selection_method(const std::string& method) {
    return method == "tdg_excess" || method == "tdg_excess_relief";
}

bool is_tdg_bpr_relief_selection_method(const std::string& method) {
    return method == "tdg_bpr_relief" || method == "tdg_marginal";
}

long double route_tdg_score(
    const gro::Trajectory& trajectory,
    const std::vector<gro::Cost>& impacts) {
    long double score = 0.0;
    for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
        if (node_id >= 0 &&
            node_id < static_cast<gro::TDGNodeId>(impacts.size())) {
            score += static_cast<long double>(
                std::max<gro::Cost>(0, impacts[node_id]));
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
            route_tdg_score(traffic_result.trajectories[query_id], impacts);
    }
    stats.mean = stats.sum / static_cast<long double>(query_ids.size());
    return stats;
}

std::vector<gro::QueryId> tdg_select_query_ids(
    const gro::GROAlgorithm& algorithm,
    const std::unordered_set<gro::QueryId>& candidate_set,
    const gro::TrafficResult& result,
    const gro::TrafficDependencyGraph& tdg,
    const std::vector<char>& important_nodes,
    int gamma) {
    if (candidate_set.empty()) {
        return {};
    }

    gro::TrafficDependencyGraph working_tdg = tdg;
    std::unordered_set<gro::QueryId> selected;
    std::vector<gro::QueryId> candidate_ids(
        candidate_set.begin(),
        candidate_set.end());
    std::sort(candidate_ids.begin(), candidate_ids.end());

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
                route_tdg_score(result.trajectories[query_id], impacts),
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
        std::size_t stale_threshold =
            static_cast<std::size_t>(
                std::sqrt(static_cast<long double>(ranking.size())));
        for (const auto& item : ranking) {
            gro::QueryId query_id = item.second;
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

    std::vector<gro::QueryId> selected_ids(selected.begin(), selected.end());
    std::sort(selected_ids.begin(), selected_ids.end());
    return selected_ids;
}

std::vector<SelectionRun> build_selection_runs(
    const Options& options,
    const gro::Graph& graph,
    const gro::AlgorithmOptions& algorithm_options,
    const gro::TrafficOptions& traffic_options,
    const std::vector<gro::Query>& queries,
    const std::vector<gro::Route>& initial_routes,
    const gro::TrafficResult& initial_traffic,
    const gro::TrafficDependencyGraph& tdg,
    const std::vector<gro::Cost>& raw_impacts) {
    std::vector<SelectionRun> runs;

    std::unordered_set<gro::QueryId> all_query_set;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        all_query_set.insert(static_cast<gro::QueryId>(i));
    }

    auto candidate_set_for_run = [&](
                                     const gro::GROAlgorithm& selector,
                                     SelectionRun& run) {
        run.candidate_filter = options.candidate_filter;
        if (options.candidate_filter == "all") {
            run.candidate_count =
                static_cast<long long>(all_query_set.size());
            run.candidate_us = 0;
            return all_query_set;
        }

        auto candidate_start = gro::Clock::now();
        std::unordered_set<gro::QueryId> candidate_set;
        auto candidate_budget = [&](int percent) {
            if (queries.empty()) {
                return std::size_t{0};
            }
            return static_cast<std::size_t>(
                std::ceil(
                    static_cast<double>(queries.size()) *
                    static_cast<double>(percent) /
                    100.0));
        };
        if (options.candidate_filter == "source_congestion") {
            candidate_set = selector.select_candidates(
                queries,
                initial_traffic,
                tdg,
                raw_impacts);
        } else if (options.candidate_filter == "score_top" ||
                   options.candidate_filter == "global_score") {
            candidate_set = selector.select_candidates_by_score(
                queries,
                initial_traffic,
                tdg,
                raw_impacts);
        } else if (options.candidate_filter == "component_balanced") {
            candidate_set = selector.select_candidates_by_component_balance(
                queries,
                initial_traffic,
                tdg,
                raw_impacts);
        } else if (options.candidate_filter == "component_marginal") {
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts);
        } else if (options.candidate_filter == "component_marginal_budget5") {
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                candidate_budget(5));
        } else if (options.candidate_filter == "component_marginal_budget3") {
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                candidate_budget(3));
        } else if (options.candidate_filter == "component_marginal_samek") {
            std::unordered_set<gro::QueryId> score_top_candidates =
                selector.select_candidates_by_score(
                    queries,
                    initial_traffic,
                    tdg,
                    raw_impacts);
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                score_top_candidates.size());
        } else if (options.candidate_filter == "component_marginal_major80_budget5") {
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                candidate_budget(5),
                80);
        } else if (options.candidate_filter == "component_marginal_major90_budget5") {
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                candidate_budget(5),
                90);
        } else if (options.candidate_filter == "component_marginal_major90_samek") {
            std::unordered_set<gro::QueryId> score_top_candidates =
                selector.select_candidates_by_score(
                    queries,
                    initial_traffic,
                    tdg,
                    raw_impacts);
            candidate_set = selector.select_candidates_by_component_marginal(
                queries,
                initial_traffic,
                tdg,
                raw_impacts,
                score_top_candidates.size(),
                90);
        }
        run.candidate_us = gro::elapsed_us(candidate_start);
        run.candidate_count =
            static_cast<long long>(candidate_set.size());
        return candidate_set;
    };

    for (const std::string& method : options.selection_methods) {
        if (method == "random") {
            for (int fixed_fraction : options.fixed_fractions) {
                SelectionRun run;
                run.method = "random";
                run.selection_fraction = fixed_fraction;
                auto start = gro::Clock::now();
                run.selected_ids =
                    random_query_ids(
                        queries.size(),
                        fixed_fraction,
                        options.random_seed);
                run.select_us = gro::elapsed_us(start);
                runs.push_back(std::move(run));
            }
        } else if (method == "most_delayed") {
            for (int fixed_fraction : options.fixed_fractions) {
                SelectionRun run;
                run.method = "most_delayed";
                run.selection_fraction = fixed_fraction;
                auto start = gro::Clock::now();
                run.selected_ids =
                    most_delayed_query_ids(
                        queries.size(),
                        fixed_fraction,
                        initial_routes,
                        initial_traffic);
                run.select_us = gro::elapsed_us(start);
                runs.push_back(std::move(run));
            }
        } else if (is_tdg_bpr_relief_selection_method(method)) {
            for (int gamma : options.tdg_gammas) {
                gro::AlgorithmOptions run_options = algorithm_options;
                run_options.gamma = gamma;
                gro::GROAlgorithm selector(graph, run_options, traffic_options);

                SelectionRun run;
                run.method = "tdg_bpr_relief";
                run.removal_mode = RemovalMode::BprRelief;
                run.gamma = gamma;

                std::unordered_set<gro::QueryId> candidate_set =
                    candidate_set_for_run(selector, run);
                auto select_start = gro::Clock::now();
                run.selected_ids =
                    selector.select_queries_by_bpr_relief(
                        candidate_set,
                        queries,
                        initial_traffic,
                        tdg,
                        raw_impacts);
                run.select_us = gro::elapsed_us(select_start);
                runs.push_back(std::move(run));
            }
        } else if (is_tdg_excess_selection_method(method)) {
            for (int gamma : options.tdg_gammas) {
                gro::AlgorithmOptions run_options = algorithm_options;
                run_options.gamma = gamma;
                gro::GROAlgorithm selector(graph, run_options, traffic_options);

                SelectionRun run;
                run.method = "tdg_excess";
                run.removal_mode = RemovalMode::AnchorImportant;
                run.gamma = gamma;

                std::unordered_set<gro::QueryId> candidate_set =
                    candidate_set_for_run(selector, run);
                auto select_start = gro::Clock::now();
                run.selected_ids =
                    selector.select_queries_by_excess_relief(
                        candidate_set,
                        queries,
                        initial_traffic,
                        tdg,
                        raw_impacts);
                run.select_us = gro::elapsed_us(select_start);
                runs.push_back(std::move(run));
            }
        } else if (is_tdg_selection_method(method)) {
            RemovalMode mode = tdg_selection_mode_from_name(method);
            for (int gamma : options.tdg_gammas) {
                gro::AlgorithmOptions run_options = algorithm_options;
                run_options.gamma = gamma;
                gro::GROAlgorithm selector(graph, run_options, traffic_options);

                SelectionRun run;
                run.method = "tdg";
                run.removal_mode = mode;
                run.gamma = gamma;
                std::unordered_set<gro::QueryId> candidate_set =
                    candidate_set_for_run(selector, run);

                auto important_start = gro::Clock::now();
                std::vector<char> important_nodes =
                    important_tdg_nodes(
                        mode,
                        selector,
                        graph,
                        initial_traffic,
                        tdg,
                        run_options.theta_percentile);
                run.important_nodes_us = gro::elapsed_us(important_start);
                run.important_node_count =
                    static_cast<long long>(
                        std::count(
                            important_nodes.begin(),
                            important_nodes.end(),
                            1));

                auto select_start = gro::Clock::now();
                run.selected_ids =
                    tdg_select_query_ids(
                        selector,
                        candidate_set,
                        initial_traffic,
                        tdg,
                        important_nodes,
                        gamma);
                run.select_us = gro::elapsed_us(select_start);
                runs.push_back(std::move(run));
            }
        } else {
            throw std::runtime_error("Unknown selection method: " + method);
        }
    }
    return runs;
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

std::size_t tdg_edge_timeline_count(
    const gro::TrafficDependencyGraph& tdg) {
    std::size_t count = 0;
    for (const auto& timeline : tdg.edge_timelines) {
        count += timeline.size();
    }
    return count;
}

void ensure_parent_dir(const std::filesystem::path& path) {
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void log_progress(const Options& options, const std::string& message) {
    if (options.progress_log) {
        std::cerr << message << std::endl;
    }
}

double seconds_from_us(long long elapsed_us) {
    return static_cast<double>(elapsed_us) / 1000000.0;
}

std::string selection_label(const SelectionRun& selection) {
    if (selection.method == "tdg") {
        return "tdg_" +
            removal_mode_name(selection.removal_mode) +
            "_gamma" +
            std::to_string(selection.gamma);
    }
    if (selection.method == "tdg_excess") {
        return "tdg_excess_gamma" + std::to_string(selection.gamma);
    }
    if (selection.method == "tdg_bpr_relief") {
        return "tdg_bpr_relief_gamma" + std::to_string(selection.gamma);
    }
    return selection.method + std::to_string(selection.selection_fraction);
}

void write_header(std::ofstream& out) {
    out << "run_id,dataset,hop,rep,seed,query_count,"
        << "iteration,"
        << "selection_method,removal_mode,selection_fraction,gamma,"
        << "reroute_method,impact_weight,random_seed,"
        << "selected_count,selected_fraction,"
        << "candidate_filter,candidate_count,candidate_fraction,candidate_sec,"
        << "batch_count,"
        << "total_before,total_after,reduction,"
        << "initial_routes_sec,evaluate_before_sec,"
        << "tdg_prepare_sec,important_nodes_sec,select_sec,"
        << "normalize_sec,batch_sec,reroute_sec,evaluate_after_sec,"
        << "method_total_sec,cumulative_sec,"
        << "mean_selected_impact_score,mean_all_query_impact_score,"
        << "tdg_node_count,tdg_edge_timeline_count\n";
}

void write_row(
    std::ofstream& out,
    const std::string& run_id,
    const DatasetInfo& dataset,
    std::size_t query_count,
    int iteration,
    const SelectionRun& selection,
    const std::string& reroute_method,
    int impact_weight,
    unsigned int random_seed,
    std::size_t batch_count,
    gro::Cost total_before,
    gro::Cost total_after,
    long long initial_routes_us,
    long long evaluate_before_us,
    long long tdg_prepare_us,
    long long normalize_us,
    long long batch_us,
    long long reroute_us,
    long long evaluate_after_us,
    const ScoreStats& selected_scores,
    const ScoreStats& all_scores,
    std::size_t tdg_node_count,
    std::size_t tdg_edge_timeline_count,
    long long cumulative_us) {
    bool uses_tdg_selection = is_tdg_selection_method(selection.method);
    bool uses_tdg_reroute = reroute_method == "tdg_impact_reroute";

    long long included_initial_routes_us = iteration == 0 ? initial_routes_us : 0;
    long long included_tdg_prepare_us =
        (uses_tdg_selection || uses_tdg_reroute) ? tdg_prepare_us : 0;
    long long included_important_us =
        uses_tdg_selection ? selection.important_nodes_us : 0;
    long long included_candidate_us =
        uses_tdg_selection ? selection.candidate_us : 0;
    long long included_normalize_us = uses_tdg_reroute ? normalize_us : 0;
    long long included_batch_us = uses_tdg_reroute ? batch_us : 0;
    long long method_total_us =
        included_initial_routes_us +
        evaluate_before_us +
        included_tdg_prepare_us +
        included_important_us +
        included_candidate_us +
        selection.select_us +
        included_normalize_us +
        included_batch_us +
        reroute_us +
        evaluate_after_us;

    long double selected_fraction = query_count == 0
        ? 0.0L
        : static_cast<long double>(selection.selected_ids.size()) /
            static_cast<long double>(query_count);
    long double candidate_fraction =
        query_count == 0 || selection.candidate_count < 0
            ? -1.0L
            : static_cast<long double>(selection.candidate_count) /
                static_cast<long double>(query_count);

    out << std::setprecision(12)
        << run_id << ','
        << dataset.dataset << ','
        << dataset.hop << ','
        << dataset.rep << ','
        << dataset.seed << ','
        << query_count << ','
        << iteration << ','
        << selection.method << ','
        << (uses_tdg_selection ? removal_mode_name(selection.removal_mode) : "none") << ','
        << selection.selection_fraction << ','
        << selection.gamma << ','
        << reroute_method << ','
        << impact_weight << ','
        << random_seed << ','
        << selection.selected_ids.size() << ','
        << static_cast<double>(selected_fraction) << ','
        << selection.candidate_filter << ','
        << selection.candidate_count << ','
        << static_cast<double>(candidate_fraction) << ','
        << static_cast<double>(included_candidate_us) / 1000000.0 << ','
        << batch_count << ','
        << total_before << ','
        << total_after << ','
        << (total_before - total_after) << ','
        << static_cast<double>(included_initial_routes_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_before_us) / 1000000.0 << ','
        << static_cast<double>(included_tdg_prepare_us) / 1000000.0 << ','
        << static_cast<double>(included_important_us) / 1000000.0 << ','
        << static_cast<double>(selection.select_us) / 1000000.0 << ','
        << static_cast<double>(included_normalize_us) / 1000000.0 << ','
        << static_cast<double>(included_batch_us) / 1000000.0 << ','
        << static_cast<double>(reroute_us) / 1000000.0 << ','
        << static_cast<double>(evaluate_after_us) / 1000000.0 << ','
        << static_cast<double>(method_total_us) / 1000000.0 << ','
        << static_cast<double>(cumulative_us) / 1000000.0 << ','
        << static_cast<double>(selected_scores.mean) << ','
        << static_cast<double>(all_scores.mean) << ','
        << tdg_node_count << ','
        << tdg_edge_timeline_count << '\n';
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

        std::vector<DatasetInput> datasets = resolve_datasets(options, input);

        ensure_parent_dir(options.output_path);
        std::ofstream out(options.output_path);
        require(
            static_cast<bool>(out),
            "Cannot open output: " + options.output_path.string());
        write_header(out);

        std::cout << "Ablation diagnostic written:\n"
                  << "  " << options.output_path << "\n"
                  << "datasets=" << datasets.size() << "\n";
        log_progress(
            options,
            "[start] output=" + options.output_path.string() +
                " datasets=" + std::to_string(datasets.size()) +
                " max_iterations=" +
                std::to_string(algorithm_options.max_iterations));

        std::size_t rows_written = 0;
        std::size_t dataset_index = 0;
        for (const DatasetInput& dataset : datasets) {
            ++dataset_index;
            log_progress(
                options,
                "[dataset start] " + std::to_string(dataset_index) + "/" +
                    std::to_string(datasets.size()) +
                    " dataset=" + dataset.info.dataset +
                    " path=" + dataset.path.string());

            std::vector<gro::Query> queries =
                gro::read_queries(dataset.path.string());
            require(
                !queries.empty(),
                "queries should not be empty: " + dataset.path.string());
            log_progress(
                options,
                "[dataset loaded] dataset=" + dataset.info.dataset +
                    " queries=" + std::to_string(queries.size()));

            gro::GROAlgorithm base_algorithm(
                graph,
                algorithm_options,
                traffic_options);

            log_progress(
                options,
                "[stage start] dataset=" + dataset.info.dataset +
                    " stage=initial_routes");
            auto initial_start = gro::Clock::now();
            std::vector<gro::Route> initial_routes =
                base_algorithm.compute_initial_routes(queries);
            long long initial_routes_us = gro::elapsed_us(initial_start);
            log_progress(
                options,
                "[stage done] dataset=" + dataset.info.dataset +
                    " stage=initial_routes sec=" +
                    std::to_string(seconds_from_us(initial_routes_us)));

            std::vector<gro::QueryId> all_query_ids(queries.size());
            std::iota(all_query_ids.begin(), all_query_ids.end(), 0);

            for (const std::string& selection_method :
                 options.selection_methods) {
                std::vector<int> selection_gammas =
                    is_tdg_selection_method(selection_method)
                        ? options.tdg_gammas
                        : std::vector<int>{-1};
                std::vector<int> selection_fractions =
                    is_tdg_selection_method(selection_method)
                        ? std::vector<int>{-1}
                        : options.fixed_fractions;

                for (int gamma : selection_gammas) {
                    for (int fixed_fraction : selection_fractions) {
                        for (const std::string& reroute_method_option :
                             options.reroute_methods) {
                            std::vector<int> weights =
                                reroute_method_option == "tdg"
                                    ? options.impact_weights
                                    : std::vector<int>{0};

                            for (int impact_weight : weights) {
                                std::string run_context =
                                    "dataset=" + dataset.info.dataset +
                                    " selection=" + selection_method +
                                    " gamma=" + std::to_string(gamma) +
                                    " fraction=" +
                                    std::to_string(fixed_fraction) +
                                    " reroute=" + reroute_method_option +
                                    " impact=" + std::to_string(impact_weight);
                                log_progress(
                                    options,
                                    "[run start] " + run_context);

                                std::vector<gro::Route> routes = initial_routes;
                                long long cumulative_us = 0;

                                for (int iteration = 0;
                                     iteration < algorithm_options.max_iterations;
                                     ++iteration) {
                                    unsigned int iteration_seed =
                                        options.random_seed +
                                        static_cast<unsigned int>(iteration);
                                    std::string iter_context =
                                        run_context +
                                        " iter=" +
                                        std::to_string(iteration + 1) + "/" +
                                        std::to_string(
                                            algorithm_options.max_iterations);

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=evaluate_before");
                                auto evaluate_before_start = gro::Clock::now();
                                gro::TrafficResult traffic_result =
                                    gro::evaluate_traffic(
                                        graph,
                                        queries,
                                        routes,
                                        traffic_options);
                                long long evaluate_before_us =
                                    gro::elapsed_us(evaluate_before_start);
                                gro::Cost total_before =
                                    traffic_result.total_travel_time;
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=evaluate_before sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                evaluate_before_us)) +
                                        " total_before=" +
                                        std::to_string(total_before));

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=build_tdg");
                                auto tdg_start = gro::Clock::now();
                                gro::TrafficDependencyGraph tdg =
                                    base_algorithm.build_tdg(traffic_result);
                                long long build_tdg_us =
                                    gro::elapsed_us(tdg_start);
                                std::size_t tdg_edges =
                                    tdg_edge_timeline_count(tdg);
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=build_tdg sec=" +
                                        std::to_string(
                                            seconds_from_us(build_tdg_us)) +
                                        " tdg_nodes=" +
                                        std::to_string(tdg.nodes.size()) +
                                        " tdg_edge_timelines=" +
                                        std::to_string(tdg_edges));

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=compute_impact");
                                auto impact_start = gro::Clock::now();
                                std::vector<gro::Cost> raw_impacts =
                                    base_algorithm.compute_tdg_impact(tdg);
                                long long compute_impact_us =
                                    gro::elapsed_us(impact_start);
                                long long tdg_prepare_us =
                                    build_tdg_us + compute_impact_us;
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=compute_impact sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                compute_impact_us)));

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=normalize_impacts");
                                auto normalize_start = gro::Clock::now();
                                std::vector<gro::Cost> reroute_impacts =
                                    base_algorithm
                                        .normalize_tdg_impacts_for_reroute(
                                            tdg,
                                            raw_impacts);
                                long long normalize_us =
                                    gro::elapsed_us(normalize_start);
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=normalize_impacts sec=" +
                                        std::to_string(
                                            seconds_from_us(normalize_us)));

                                ScoreStats all_scores =
                                    score_stats(
                                        all_query_ids,
                                        traffic_result,
                                        reroute_impacts);

                                Options selection_options = options;
                                selection_options.selection_methods = {
                                    selection_method};
                                selection_options.random_seed = iteration_seed;
                                if (!is_tdg_selection_method(selection_method)) {
                                    selection_options.fixed_fractions = {
                                        fixed_fraction};
                                }
                                if (is_tdg_selection_method(selection_method)) {
                                    selection_options.tdg_gammas = {gamma};
                                }

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=selection");
                                std::vector<SelectionRun> selection_runs =
                                    build_selection_runs(
                                        selection_options,
                                        graph,
                                        algorithm_options,
                                        traffic_options,
                                        queries,
                                        initial_routes,
                                        traffic_result,
                                        tdg,
                                        raw_impacts);
                                require(
                                    selection_runs.size() == 1,
                                    "Expected exactly one selection run");
                                SelectionRun selection =
                                    std::move(selection_runs.front());
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=selection selected=" +
                                        std::to_string(
                                            selection.selected_ids.size()) +
                                        " candidate_filter=" +
                                        selection.candidate_filter +
                                        " candidate_count=" +
                                        std::to_string(
                                            selection.candidate_count) +
                                        " candidate_sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                selection.candidate_us)) +
                                        " important_nodes=" +
                                        std::to_string(
                                            selection.important_node_count) +
                                        " important_sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                selection
                                                    .important_nodes_us)) +
                                        " select_sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                selection.select_us)));

                                ScoreStats selected_scores =
                                    score_stats(
                                        selection.selected_ids,
                                        traffic_result,
                                        reroute_impacts);

                                std::vector<gro::Route> new_routes;
                                std::size_t batch_count = 0;
                                long long batch_us = 0;
                                long long reroute_us = 0;
                                std::string reroute_method_name;
                                int logged_impact_weight = 0;

                                if (reroute_method_option == "normal") {
                                    reroute_method_name =
                                        "normal_td_dijkstra";
                                    log_progress(
                                        options,
                                        "[stage start] " + iter_context +
                                            " stage=normal_reroute selected=" +
                                            std::to_string(
                                                selection.selected_ids.size()));
                                    auto reroute_start = gro::Clock::now();
                                    new_routes =
                                        base_algorithm.baseline_reroute_queries(
                                            selection.selected_ids,
                                            queries,
                                            traffic_result);
                                    reroute_us =
                                        gro::elapsed_us(reroute_start);
                                    log_progress(
                                        options,
                                        "[stage done] " + iter_context +
                                            " stage=normal_reroute sec=" +
                                            std::to_string(
                                                seconds_from_us(reroute_us)));
                                } else if (reroute_method_option == "tdg") {
                                    reroute_method_name =
                                        "tdg_impact_reroute";
                                    logged_impact_weight = impact_weight;

                                    gro::AlgorithmOptions run_options =
                                        algorithm_options;
                                    run_options.impact_weight = impact_weight;
                                    run_options.enable_timing_log = false;
                                    gro::GROAlgorithm rerouter(
                                        graph,
                                        run_options,
                                        traffic_options);

                                    log_progress(
                                        options,
                                        "[stage start] " + iter_context +
                                            " stage=batch_queries selected=" +
                                            std::to_string(
                                                selection.selected_ids.size()));
                                    auto batch_start = gro::Clock::now();
                                    std::vector<std::vector<gro::QueryId>>
                                        batches =
                                            rerouter.batch_queries(
                                                selection.selected_ids,
                                                tdg,
                                                traffic_result);
                                    batch_us = gro::elapsed_us(batch_start);
                                    batch_count = batches.size();
                                    log_progress(
                                        options,
                                        "[stage done] " + iter_context +
                                            " stage=batch_queries sec=" +
                                            std::to_string(
                                                seconds_from_us(batch_us)) +
                                            " batches=" +
                                            std::to_string(batch_count));

                                    log_progress(
                                        options,
                                        "[stage start] " + iter_context +
                                            " stage=tdg_impact_reroute batches=" +
                                            std::to_string(batch_count));
                                    auto reroute_start = gro::Clock::now();
                                    new_routes =
                                        rerouter.reroute_queries(
                                            batches,
                                            queries,
                                            traffic_result,
                                            tdg,
                                            reroute_impacts);
                                    reroute_us =
                                        gro::elapsed_us(reroute_start);
                                    log_progress(
                                        options,
                                        "[stage done] " + iter_context +
                                            " stage=tdg_impact_reroute sec=" +
                                            std::to_string(
                                                seconds_from_us(reroute_us)));
                                } else {
                                    throw std::runtime_error(
                                        "Unknown reroute method: " +
                                        reroute_method_option);
                                }

                                log_progress(
                                    options,
                                    "[stage start] " + iter_context +
                                        " stage=evaluate_after");
                                long long evaluate_after_us = 0;
                                gro::Cost total_after =
                                    evaluate_total_after(
                                        graph,
                                        queries,
                                        routes,
                                        new_routes,
                                        traffic_options,
                                        evaluate_after_us);
                                log_progress(
                                    options,
                                    "[stage done] " + iter_context +
                                        " stage=evaluate_after sec=" +
                                        std::to_string(
                                            seconds_from_us(
                                                evaluate_after_us)) +
                                        " total_after=" +
                                        std::to_string(total_after) +
                                        " reduction=" +
                                        std::to_string(
                                            total_before - total_after));

                                bool uses_tdg_selection =
                                    is_tdg_selection_method(selection.method);
                                bool uses_tdg_reroute =
                                    reroute_method_name ==
                                    "tdg_impact_reroute";
                                long long method_total_us =
                                    (iteration == 0 ? initial_routes_us : 0) +
                                    evaluate_before_us +
                                    ((uses_tdg_selection || uses_tdg_reroute)
                                         ? tdg_prepare_us
                                         : 0) +
                                    (uses_tdg_selection
                                         ? selection.important_nodes_us
                                         : 0) +
                                    (uses_tdg_selection
                                         ? selection.candidate_us
                                         : 0) +
                                    selection.select_us +
                                    (uses_tdg_reroute ? normalize_us : 0) +
                                    (uses_tdg_reroute ? batch_us : 0) +
                                    reroute_us +
                                    evaluate_after_us;
                                cumulative_us += method_total_us;

                                std::string reroute_label =
                                    reroute_method_name ==
                                            "tdg_impact_reroute"
                                        ? "tdg_impact_w" +
                                              std::to_string(impact_weight)
                                        : "normal_td_dijkstra";
                                std::string run_id =
                                    dataset.info.dataset +
                                    "_" +
                                    selection_label(selection) +
                                    "_" +
                                    reroute_label +
                                    "_iter" +
                                    std::to_string(iteration);

                                write_row(
                                    out,
                                    run_id,
                                    dataset.info,
                                    queries.size(),
                                    iteration,
                                    selection,
                                    reroute_method_name,
                                    logged_impact_weight,
                                    iteration_seed,
                                    batch_count,
                                    total_before,
                                    total_after,
                                    initial_routes_us,
                                    evaluate_before_us,
                                    tdg_prepare_us,
                                    normalize_us,
                                    batch_us,
                                    reroute_us,
                                    evaluate_after_us,
                                    selected_scores,
                                    all_scores,
                                    tdg.nodes.size(),
                                    tdg_edges,
                                    cumulative_us);
                                ++rows_written;
                                log_progress(
                                    options,
                                    "[iteration done] " + iter_context +
                                        " rows_written=" +
                                        std::to_string(rows_written) +
                                        " method_sec=" +
                                        std::to_string(
                                            seconds_from_us(method_total_us)) +
                                        " cumulative_sec=" +
                                        std::to_string(
                                            seconds_from_us(cumulative_us)));

                                replace_routes(routes, new_routes);
                            }
                                log_progress(
                                    options,
                                    "[run done] " + run_context +
                                        " cumulative_sec=" +
                                        std::to_string(
                                            seconds_from_us(cumulative_us)));
                        }
                    }
                }
                }
            }
            std::cout << "dataset=" << dataset.info.dataset
                      << " done rows_written=" << rows_written << "\n";
        }

        out.close();
        require(
            static_cast<bool>(out),
            "Failed while writing output: " + options.output_path.string());
        std::cout << "DONE datasets=" << datasets.size()
                  << " rows=" << rows_written << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
