#include "gro.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <omp.h>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace gro {

namespace {

int parse_percent(const std::string& value) {
    size_t index = 0;
    bool negative = false;
    if (index < value.size() && value[index] == '-') {
        negative = true;
        ++index;
    }

    long whole = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
        whole = whole * 10 + value[index] - '0';
        ++index;
    }

    long percent = 0;
    if (index < value.size() && value[index] == '.') {
        ++index;
        long fractional = 0;
        int digits = 0;
        while (index < value.size() && value[index] >= '0' && value[index] <= '9' && digits < 2) {
            fractional = fractional * 10 + value[index] - '0';
            ++index;
            ++digits;
        }
        while (digits < 2) {
            fractional *= 10;
            ++digits;
        }
        percent = whole * 100 + fractional;
    } else {
        percent = whole <= 1 ? whole * 100 : whole;
    }

    return negative ? -static_cast<int>(percent) : static_cast<int>(percent);
}

size_t percentile_index(size_t size, int percentile) {
    int clamped_percentile = std::clamp(percentile, 0, 100);
    return static_cast<size_t>(
        static_cast<long long>(clamped_percentile) * static_cast<long long>(size - 1) / 100);
}

Cost congestion_ratio_key(Flow flow, Flow capacity) {
    Flow safe_capacity = capacity > 0 ? capacity : 1;
    return static_cast<Cost>(flow) * 1000000 / safe_capacity;
}

class CandidateComponentDSU {
public:
    explicit CandidateComponentDSU(size_t size)
        : parent_(size),
          rank_(size, 0) {
        for (size_t index = 0; index < size; ++index) {
            parent_[index] = index;
        }
    }

    size_t find(size_t index) {
        if (parent_[index] != index) {
            parent_[index] = find(parent_[index]);
        }
        return parent_[index];
    }

    void unite(size_t lhs, size_t rhs) {
        lhs = find(lhs);
        rhs = find(rhs);
        if (lhs == rhs) {
            return;
        }
        if (rank_[lhs] < rank_[rhs]) {
            std::swap(lhs, rhs);
        }
        parent_[rhs] = lhs;
        if (rank_[lhs] == rank_[rhs]) {
            ++rank_[lhs];
        }
    }

private:
    std::vector<size_t> parent_;
    std::vector<int> rank_;
};

size_t integer_sqrt(size_t value) {
    return static_cast<size_t>(std::sqrt(static_cast<long double>(value)));
}

bool parse_bool(const std::string& value) {
    return value == "1" ||
           value == "true" ||
           value == "True" ||
           value == "yes" ||
           value == "on";
}

std::string seconds_text(long long microseconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << static_cast<double>(microseconds) / 1000000.0;
    return out.str();
}

Cost impact_cap() {
    return std::numeric_limits<Cost>::max() / 4;
}

Cost clamp_impact(__int128 value) {
    if (value <= 0) {
        return 0;
    }
    const __int128 cap = static_cast<__int128>(impact_cap());
    if (value > cap) {
        return impact_cap();
    }
    return static_cast<Cost>(value);
}

Cost add_impact_saturated(Cost lhs, Cost rhs) {
    return clamp_impact(
        static_cast<__int128>(std::max<Cost>(0, lhs)) +
        static_cast<__int128>(std::max<Cost>(0, rhs)));
}

Cost scale_impact_percent_saturated(Cost value, int percent) {
    if (percent <= 0 || value <= 0) {
        return 0;
    }
    return clamp_impact(
        static_cast<__int128>(value) *
        static_cast<__int128>(percent) /
        100);
}

Cost percentile_value(std::vector<Cost> values, int percentile) {
    if (values.empty()) {
        return 0;
    }
    size_t index = percentile_index(values.size(), percentile);
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

void log_select_summary(
    bool enabled,
    int iteration,
    long long candidate_count,
    long long selected_count,
    long long select_impact_us,
    long long select_rank_us,
    long long select_scan_us,
    long long select_remove_us,
    long long select_total_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "SELECT,"
              << "iteration=" << iteration << ','
              << "candidate_count=" << candidate_count << ','
              << "selected_count=" << selected_count << ','
              << "select_impact_sec=" << seconds_text(select_impact_us) << ','
              << "select_rank_sec=" << seconds_text(select_rank_us) << ','
              << "select_scan_sec=" << seconds_text(select_scan_us) << ','
              << "select_remove_sec=" << seconds_text(select_remove_us) << ','
              << "select_total_sec=" << seconds_text(select_total_us) << '\n';
}

void log_candidate_summary(
    bool enabled,
    int iteration,
    long long candidate_count,
    long long candidate_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "CANDIDATE,"
              << "iteration=" << iteration << ','
              << "candidate_count=" << candidate_count << ','
              << "candidate_sec=" << seconds_text(candidate_us) << '\n';
}

std::vector<char> excess_relief_important_nodes(
    const GROAlgorithm& algorithm,
    const Graph& graph,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg) {
    std::vector<std::map<Time, Cost>> anchor_scores =
        algorithm.compute_anchor_scores(result);
    std::vector<char> important =
        algorithm.mark_anchor_tdg_nodes(tdg, anchor_scores);

    bool has_important =
        std::any_of(important.begin(), important.end(), [](char value) {
            return value != 0;
        });
    if (has_important) {
        return important;
    }

    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        const TDGNode& node = tdg.nodes[node_id];
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(graph.edges.size())) {
            continue;
        }
        Flow capacity = std::max<Flow>(1, graph.edges[node.edge_id].capacity);
        if (node.flow > capacity) {
            important[node_id] = 1;
        }
    }
    return important;
}

std::vector<long double> excess_relief_values(
    const Graph& graph,
    const TrafficDependencyGraph& tdg,
    const std::vector<double>& selection_impacts,
    const std::vector<char>& important) {
    std::vector<long double> relief(tdg.nodes.size(), 0.0L);
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (node_id >= static_cast<TDGNodeId>(important.size()) ||
            node_id >= static_cast<TDGNodeId>(selection_impacts.size()) ||
            !important[node_id] ||
            selection_impacts[node_id] <= 0.0) {
            continue;
        }

        const TDGNode& node = tdg.nodes[node_id];
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(graph.edges.size())) {
            continue;
        }
        Flow capacity = std::max<Flow>(1, graph.edges[node.edge_id].capacity);
        if (node.flow <= capacity) {
            continue;
        }

        long double excess =
            static_cast<long double>(node.flow - capacity) /
            static_cast<long double>(capacity);
        relief[node_id] =
            static_cast<long double>(selection_impacts[node_id]) * excess;
    }
    return relief;
}

long double route_relief_score(
    const Trajectory& trajectory,
    const std::vector<long double>& relief,
    std::vector<int>& seen,
    int& seen_epoch) {
    ++seen_epoch;
    if (seen_epoch == std::numeric_limits<int>::max()) {
        std::fill(seen.begin(), seen.end(), 0);
        seen_epoch = 1;
    }

    long double score = 0.0L;
    for (TDGNodeId node_id : trajectory.tdg_node_ids) {
        if (node_id < 0 ||
            node_id >= static_cast<TDGNodeId>(relief.size()) ||
            node_id >= static_cast<TDGNodeId>(seen.size()) ||
            seen[node_id] == seen_epoch ||
            relief[node_id] <= 0.0L) {
            continue;
        }
        seen[node_id] = seen_epoch;
        score += relief[node_id];
    }
    return score;
}

std::vector<long double> build_excess_relief_values(
    const GROAlgorithm& algorithm,
    const Graph& graph,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts) {
    if (tdg.nodes.empty()) {
        return {};
    }
    std::vector<double> selection_impacts =
        algorithm.normalize_tdg_impacts_for_selection(node_impacts);
    std::vector<char> important =
        excess_relief_important_nodes(algorithm, graph, result, tdg);
    return excess_relief_values(graph, tdg, selection_impacts, important);
}

void log_reroute_summary(
    bool enabled,
    int iteration,
    long long reroute_query_count,
    long long reroute_query_us,
    long long insert_trajectory_us,
    long long recompute_impact_us,
    long long reroute_total_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "REROUTE,"
              << "iteration=" << iteration << ','
              << "reroute_query_count=" << reroute_query_count << ','
              << "reroute_query_sec=" << seconds_text(reroute_query_us) << ','
              << "insert_trajectory_sec=" << seconds_text(insert_trajectory_us) << ','
              << "recompute_impact_sec=" << seconds_text(recompute_impact_us) << ','
              << "reroute_total_sec=" << seconds_text(reroute_total_us) << '\n';
}

void log_batch_summary(
    bool enabled,
    int iteration,
    long long selected_count,
    long long batch_count,
    long long batch_queries_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "BATCH,"
              << "iteration=" << iteration << ','
              << "selected_count=" << selected_count << ','
              << "batch_count=" << batch_count << ','
              << "batch_queries_sec=" << seconds_text(batch_queries_us) << '\n';
}

void collect_trajectory_tdg_nodes(
    const TrafficDependencyGraph& tdg,
    Trajectory& trajectory) {
    trajectory.tdg_node_ids.clear();
    if (trajectory.schedule.size() < trajectory.edge_ids.size() + 1) {
        return;
    }

    for (size_t i = 0; i < trajectory.edge_ids.size(); ++i) {
        EdgeId edge_id = trajectory.edge_ids[i];
        if (edge_id < 0 || edge_id >= static_cast<EdgeId>(tdg.edge_timelines.size())) {
            continue;
        }

        Time from_time = trajectory.schedule[i];
        Time to_time = trajectory.schedule[i + 1];
        const auto& timeline = tdg.edge_timelines[edge_id];
        for (auto it = timeline.lower_bound(from_time);
             it != timeline.end() && it->first < to_time;
             ++it) {
            trajectory.tdg_node_ids.push_back(it->second.node_id);
        }
    }
}

}  // namespace

AlgorithmOptions load_algorithm_options(
    const std::string& path,
    AlgorithmOptions defaults) {
    ParameterMap parameters = load_parameter_file(path);


    AlgorithmOptions options = defaults;
    if (auto it = parameters.find("max_iterations"); it != parameters.end()) {
        options.max_iterations = std::stoi(it->second);
    }
    if (auto it = parameters.find("lambda"); it != parameters.end()) {
        options.lambda = parse_percent(it->second);
    }
    if (auto it = parameters.find("gamma"); it != parameters.end()) {
        options.gamma = parse_percent(it->second);
    }
    if (auto it = parameters.find("candidate_theta"); it != parameters.end()) {
        options.candidate_theta = parse_percent(it->second);
    }
    if (auto it = parameters.find("impact_weight"); it != parameters.end()) {
        options.impact_weight = parse_percent(it->second);
    }
    if (auto it = parameters.find("theta_percentile"); it != parameters.end()) {
        options.theta_percentile = parse_percent(it->second);
    }

    if (auto it = parameters.find("min_slot_width"); it != parameters.end()) {
        options.delta_min = std::stoi(it->second);
    }
    if (auto it = parameters.find("delta_initial"); it != parameters.end()) {
        options.delta_initial = std::stoi(it->second);
    }
    if (auto it = parameters.find("eta"); it != parameters.end()) {
        options.eta = std::stoi(it->second);
    }
    if (auto it = parameters.find("conflict_threshold"); it != parameters.end()) {
        options.conflict_threshold = std::stoi(it->second);
    }
    if (auto it = parameters.find("delta_compress"); it != parameters.end()) {
        options.delta_compress = std::stoi(it->second);
    }
    if (auto it = parameters.find("anchor_window"); it != parameters.end()) {
        options.anchor_window = std::stoi(it->second);
    }
    if (auto it = parameters.find("anchor_threshold"); it != parameters.end()) {
        options.anchor_threshold = std::stoi(it->second);
    }
    if (auto it = parameters.find("baseline_fraction_to_reroute"); it != parameters.end()) {
        options.baseline_fraction_to_reroute = parse_percent(it->second);
    }
    if (auto it = parameters.find("baseline_random_seed"); it != parameters.end()) {
        options.baseline_random_seed =
            static_cast<unsigned int>(std::stoul(it->second));
    }
    if (auto it = parameters.find("enable_timing_log"); it != parameters.end()) {
        options.enable_timing_log = parse_bool(it->second);
    }
    if (auto it = parameters.find("timing_log"); it != parameters.end()) {
        options.enable_timing_log = parse_bool(it->second);
    }
    return options;
}

GROAlgorithm::GROAlgorithm(
    Graph graph,
    AlgorithmOptions options,
    TrafficOptions traffic_options)
    : graph_(std::move(graph)),
      options_(options),
      traffic_options_(traffic_options) {}

const Graph& GROAlgorithm::graph() const {
    return graph_;
}

Route GROAlgorithm::shortest_path(
    const Query& query,
    const std::vector<Cost>*) const {
    return gro::shortest_path(graph_, query);
}

std::vector<Route> GROAlgorithm::compute_initial_routes(
    const std::vector<Query>& queries) const {
    std::vector<Route> routes(queries.size());

    #pragma omp parallel for
    for (size_t i = 0; i < queries.size(); ++i) {
        routes[i] = shortest_path(queries[i]);
    }

    return routes;
}

TrafficDependencyGraph GROAlgorithm::build_tdg(
    TrafficResult& result) const {
    TrafficDependencyGraph tdg;

    int node_id_counter = 0; // count total number of nodes to reserve space in TDG
    tdg.edge_timelines.assign(graph_.edges.size(), std::map<Time, TimeLineEvent>());
    for (const Trajectory &trajectory : result.trajectories) {
        node_id_counter += trajectory.edge_ids.size();
    }
    tdg.nodes.reserve(node_id_counter);
    tdg.route_outgoing.assign(node_id_counter, std::vector<TDGNodeId>());
    tdg.route_incoming.assign(node_id_counter, std::vector<TDGNodeId>());

    TDGNodeId current_route_node_id = 0;
    for (const Trajectory &trajectory : result.trajectories) {
        TDGNodeId previous_node_id = kInvalidId;
        for (size_t i = 0; i < trajectory.edge_ids.size(); ++i) {

            Key key = std::make_pair(trajectory.edge_ids[i], trajectory.schedule[i]);
            TDGNodeId node_id;
            if (tdg.key_to_node_id.find(key) == tdg.key_to_node_id.end()) {
                TDGNode node;
                node.id = current_route_node_id;
                node.edge_id = trajectory.edge_ids[i];
                node.time = trajectory.schedule[i];
                node.flow = get_edge_flow(result, node.edge_id, node.time);
                node.original_flow = node.flow;

                tdg.nodes.push_back(node);
                tdg.key_to_node_id[key] = node.id;
                node_id = node.id;
                ++current_route_node_id;
            } else
            {
                node_id = tdg.key_to_node_id[key];
            }
                
            if (previous_node_id != kInvalidId) {
                tdg.route_outgoing[previous_node_id].push_back(node_id);
                tdg.route_incoming[node_id].push_back(previous_node_id);
            }

            previous_node_id = node_id;

        }
    }

    // Constructe edge timelines from result.edge_profiles
    for (EdgeId edge_id = 0; edge_id < static_cast<EdgeId>(result.edge_profiles.size()); ++edge_id) {
        const auto& edge_profile = result.edge_profiles[edge_id];
        auto& timeline = tdg.edge_timelines[edge_id];

        if (edge_profile.empty()) {
            continue;
        }
        
        auto previous_timeline_it = timeline.end();
        TDGNodeId previous_node_id = kInvalidId;
        
        for (const Event &event : edge_profile) {
            if (!event.type)
                continue; // We only care about entry events for constructing the timeline

            auto node_it = tdg.key_to_node_id.find({edge_id, event.time});
            
            if (node_it == tdg.key_to_node_id.end() || node_it->second == kInvalidId) {
                continue;
            }

            TDGNodeId node_id = node_it->second;

            TimeLineEvent timeline_event = {event.time, node_it->second, event.flow, kInvalidId, kInvalidId};
            auto timeline_it = timeline.insert_or_assign(event.time, timeline_event).first;
            if (previous_timeline_it != timeline.end() && 
                    previous_timeline_it->second.time + bpr_travel_time(
                        graph_.edges[edge_id], 
                        previous_timeline_it->second.flow,
                        traffic_options_) > timeline_it->second.time ) {
                            
                timeline_it->second.same_edge_parent = previous_node_id;
                previous_timeline_it->second.same_edge_child = node_id;
            }

            previous_timeline_it = timeline_it;
            previous_node_id = node_id;
        
        }
        
    }

    for (Trajectory& trajectory : result.trajectories) {
        collect_trajectory_tdg_nodes(tdg, trajectory);
    }

    return tdg;
}


std::vector<std::map<Time, Cost>> GROAlgorithm::compute_anchor_scores(
    const TrafficResult& result) const {
    int delta_compress = options_.delta_compress > 0
        ? options_.delta_compress
        : options_.delta_initial;
    if (delta_compress <= 0) {
        delta_compress = options_.delta_min > 0 ? options_.delta_min : 1;
    }

    int anchor_window = options_.anchor_window > 0
        ? options_.anchor_window
        : delta_compress;
    int anchor_threshold = std::max(0, options_.anchor_threshold);

    std::vector<std::map<Time, Cost>> anchor_scores(graph_.edges.size());
    for (EdgeId edge_id = 0; edge_id < static_cast<EdgeId>(result.edge_profiles.size()); ++edge_id) {
        const auto& profile = result.edge_profiles[edge_id];
        if (profile.empty()) {
            continue;
        }

        std::vector<__int128> cumulative_flow(profile.size(), 0);
        for (size_t i = 1; i < profile.size(); ++i) {
            Time duration = profile[i].time - profile[i - 1].time;
            if (duration > 0) {
                cumulative_flow[i] = cumulative_flow[i - 1] +
                    static_cast<__int128>(profile[i - 1].flow) * duration;
            } else {
                cumulative_flow[i] = cumulative_flow[i - 1];
            }
        }

        Flow capacity = graph_.edges[edge_id].capacity > 0
            ? graph_.edges[edge_id].capacity
            : 1;

        auto area_at = [&](size_t index, Time time) {
            if (time <= profile.front().time) {
                return static_cast<__int128>(0);
            }
            return cumulative_flow[index] +
                static_cast<__int128>(profile[index].flow) *
                (time - profile[index].time);
        };

        bool marked_first_entry = false;
        size_t window_index = 0;
        for (size_t i = 0; i < profile.size(); ++i) {
            if (!profile[i].type) {
                continue;
            }

            if (!marked_first_entry) {
                anchor_scores[edge_id][profile[i].time] = 0;
                marked_first_entry = true;
                continue;
            }

            Time time = profile[i].time;
            Time window_start = std::max<Time>(
                profile.front().time,
                time - anchor_window);
            Time duration = time - window_start;
            if (duration <= 0) {
                continue;
            }

            while (window_index + 1 < profile.size() &&
                   profile[window_index + 1].time <= window_start) {
                ++window_index;
            }

            __int128 window_area =
                cumulative_flow[i] - area_at(window_index, window_start);
            __int128 current_area =
                static_cast<__int128>(profile[i].flow) * duration;
            __int128 deviation = current_area >= window_area
                ? current_area - window_area
                : window_area - current_area;
            __int128 threshold =
                static_cast<__int128>(anchor_threshold) * capacity * duration;

            if (deviation * 100 >= threshold) {
                __int128 denominator =
                    static_cast<__int128>(capacity) * duration;
                Cost score = denominator > 0
                    ? static_cast<Cost>(deviation * 1000000 / denominator)
                    : 0;
                anchor_scores[edge_id][time] = score;
            }
        }
    }
    return anchor_scores;
}

std::vector<char> GROAlgorithm::mark_anchor_tdg_nodes(
    const TrafficDependencyGraph& tdg,
    const std::vector<std::map<Time, Cost>>& anchor_scores) const {
    std::vector<char> important(tdg.nodes.size(), 0);
    for (const TDGNode& node : tdg.nodes) {
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(anchor_scores.size())) {
            continue;
        }

        const auto& edge_scores = anchor_scores[node.edge_id];
        auto score_it = edge_scores.find(node.time);
        if (score_it != edge_scores.end() && score_it->second > 0) {
            important[node.id] = 1;
        }
    }
    return important;
}

TrafficDependencyGraph GROAlgorithm::compress_tdg(
    TrafficResult& result) const {
    TrafficDependencyGraph tdg;
    tdg.edge_timelines.assign(graph_.edges.size(), std::map<Time, TimeLineEvent>());

    int delta_compress = options_.delta_compress > 0
        ? options_.delta_compress
        : options_.delta_initial;
    if (delta_compress <= 0) {
        delta_compress = options_.delta_min > 0 ? options_.delta_min : 1;
    }

    std::vector<std::vector<Time>> entry_times_by_edge(graph_.edges.size());
    for (EdgeId edge_id = 0; edge_id < static_cast<EdgeId>(result.edge_profiles.size()); ++edge_id) {
        for (const Event& event : result.edge_profiles[edge_id]) {
            if (event.type) {
                entry_times_by_edge[edge_id].push_back(event.time);
            }
        }
    }

    std::vector<std::map<Time, Cost>> anchor_scores =
        compute_anchor_scores(result);

    auto add_node = [&](EdgeId edge_id, Time time) {
        Key key = {edge_id, time};
        auto node_it = tdg.key_to_node_id.find(key);
        if (node_it != tdg.key_to_node_id.end()) {
            return node_it->second;
        }

        TDGNode node;
        node.id = static_cast<TDGNodeId>(tdg.nodes.size());
        node.edge_id = edge_id;
        node.time = time;
        node.flow = get_edge_flow(result, edge_id, time);
        node.original_flow = node.flow;

        tdg.nodes.push_back(node);
        tdg.route_outgoing.emplace_back();
        tdg.route_incoming.emplace_back();
        tdg.key_to_node_id[key] = node.id;
        return node.id;
    };

    auto add_route_arc = [&](TDGNodeId from, TDGNodeId to) {
        if (from == kInvalidId || to == kInvalidId || from == to) {
            return;
        }
        auto& outgoing = tdg.route_outgoing[from];
        if (std::find(outgoing.begin(), outgoing.end(), to) == outgoing.end()) {
            outgoing.push_back(to);
            tdg.route_incoming[to].push_back(from);
        }
    };

    for (const Trajectory& trajectory : result.trajectories) {
        if (trajectory.edge_ids.empty() ||
            trajectory.schedule.size() < trajectory.edge_ids.size() + 1) {
            continue;
        }

        std::vector<TDGNodeId> route_nodes;
        auto add_route_node = [&](EdgeId edge_id, Time time) {
            TDGNodeId node_id = add_node(edge_id, time);
            anchor_scores[edge_id].try_emplace(time, 0);
            if (route_nodes.empty() || route_nodes.back() != node_id) {
                route_nodes.push_back(node_id);
            }
        };

        add_route_node(trajectory.edge_ids.front(), trajectory.schedule.front());

        Time trajectory_start = trajectory.schedule.front();
        Time trajectory_end = trajectory.schedule.back();
        size_t segment_index = 0;
        for (Time window_start = trajectory_start; window_start < trajectory_end; ) {
            Time window_end = std::min<Time>(trajectory_end, window_start + delta_compress);
            if (window_end <= window_start) {
                break;
            }

            while (segment_index < trajectory.edge_ids.size() &&
                   trajectory.schedule[segment_index + 1] <= window_start) {
                ++segment_index;
            }

            bool found_anchor = false;
            Key anchor = {kInvalidId, 0};
            bool found_fallback = false;
            Key fallback = {kInvalidId, 0};

            size_t scan_index = segment_index;
            while (scan_index < trajectory.edge_ids.size() &&
                   trajectory.schedule[scan_index] < window_end) {
                Time segment_start = trajectory.schedule[scan_index];
                Time segment_end = trajectory.schedule[scan_index + 1];
                if (segment_end <= window_start) {
                    ++scan_index;
                    continue;
                }

                Time overlap_start = std::max(window_start, segment_start);
                Time overlap_end = std::min(window_end, segment_end);
                if (overlap_start < overlap_end) {
                    EdgeId edge_id = trajectory.edge_ids[scan_index];
                    const auto& edge_anchor_scores = anchor_scores[edge_id];
                    auto anchor_it = edge_anchor_scores.lower_bound(overlap_start);
                    if (anchor_it != edge_anchor_scores.end() && anchor_it->first < overlap_end &&
                        (!found_anchor || anchor_it->first < anchor.second)) {
                        found_anchor = true;
                        anchor = {edge_id, anchor_it->first};
                    }

                    const auto& entry_times = entry_times_by_edge[edge_id];
                    auto entry_it = std::lower_bound(
                        entry_times.begin(),
                        entry_times.end(),
                        overlap_start);
                    if (entry_it != entry_times.end() && *entry_it < overlap_end &&
                        (!found_fallback || *entry_it < fallback.second)) {
                        found_fallback = true;
                        fallback = {edge_id, *entry_it};
                    }
                }

                ++scan_index;
            }

            if (found_anchor) {
                add_route_node(anchor.first, anchor.second);
            } else if (found_fallback) {
                add_route_node(fallback.first, fallback.second);
            }

            window_start = window_end;
        }

        add_route_node(
            trajectory.edge_ids.back(),
            trajectory.schedule[trajectory.edge_ids.size() - 1]);

        for (size_t i = 1; i < route_nodes.size(); ++i) {
            add_route_arc(route_nodes[i - 1], route_nodes[i]);
        }
    }

    for (const TDGNode& node : tdg.nodes) {
        tdg.edge_timelines[node.edge_id].insert_or_assign(
            node.time,
            TimeLineEvent{node.time, node.id, node.flow, kInvalidId, kInvalidId});
    }

    for (EdgeId edge_id = 0; edge_id < static_cast<EdgeId>(tdg.edge_timelines.size()); ++edge_id) {
        auto& timeline = tdg.edge_timelines[edge_id];
        if (timeline.empty()) {
            continue;
        }

        auto previous_it = timeline.end();
        for (auto timeline_it = timeline.begin(); timeline_it != timeline.end(); ++timeline_it) {
            if (previous_it != timeline.end() &&
                previous_it->second.time + bpr_travel_time(
                    graph_.edges[edge_id],
                    previous_it->second.flow,
                    traffic_options_) > timeline_it->second.time) {
                previous_it->second.same_edge_child = timeline_it->second.node_id;
                timeline_it->second.same_edge_parent = previous_it->second.node_id;
            }
            previous_it = timeline_it;
        }
    }

    for (Trajectory& trajectory : result.trajectories) {
        collect_trajectory_tdg_nodes(tdg, trajectory);
    }

    return tdg;
}

std::vector<Cost> GROAlgorithm::compute_tdg_impact(
    const TrafficDependencyGraph& tdg) const {
    auto add_unique = [&](std::vector<TDGNodeId>& ids, TDGNodeId node_id) {
        if (node_id == kInvalidId ||
            node_id < 0 ||
            node_id >= static_cast<TDGNodeId>(tdg.nodes.size())) {
            return;
        }
        if (std::find(ids.begin(), ids.end(), node_id) == ids.end()) {
            ids.push_back(node_id);
        }
    };

    auto children = [&](TDGNodeId node_id) {
        std::vector<TDGNodeId> ids=tdg.route_outgoing[node_id];
        const auto &node = tdg.nodes[node_id];
        const auto &timeline = tdg.edge_timelines[node.edge_id];
        if (timeline.find(node.time) != timeline.end()) {
            const auto &timeline_event = timeline.at(node.time);
            if (timeline_event.same_edge_child != kInvalidId) {
                add_unique(ids, timeline_event.same_edge_child);
            }
        }
        return ids;
    };

    auto parents = [&](TDGNodeId node_id) {
        std::vector<TDGNodeId> ids = tdg.route_incoming[node_id];
        const auto &node = tdg.nodes[node_id];
        const auto &timeline = tdg.edge_timelines[node.edge_id];
        if (timeline.find(node.time) != timeline.end()) {
            const auto &timeline_event = timeline.at(node.time);
            if (timeline_event.same_edge_parent != kInvalidId) {
                add_unique(ids, timeline_event.same_edge_parent);
            }
        }
        return ids;
    };

    std::vector<Cost> impacts(tdg.nodes.size(), 0);
    std::vector<int> child_count(tdg.nodes.size(), 0);
    std::vector<int> parent_count(tdg.nodes.size(), 0);
    std::vector<TDGNodeId> ready;
    ready.reserve(tdg.nodes.size());

    for (TDGNodeId node_id = 0; node_id < static_cast<TDGNodeId>(tdg.nodes.size()); ++node_id) {
        const TDGNode& node = tdg.nodes[node_id];
        if (node.edge_id >= 0 && node.edge_id < static_cast<EdgeId>(graph_.edges.size())) {
            impacts[node_id] = bpr_travel_time(graph_.edges[node.edge_id], node.flow, traffic_options_);
        }
        child_count[node_id] = static_cast<int>(children(node_id).size());
        parent_count[node_id] = static_cast<int>(parents(node_id).size());
        if (child_count[node_id] == 0) {
            ready.push_back(node_id);
        }
    }

    while (!ready.empty()) {
        TDGNodeId node_id = ready.back();
        ready.pop_back();

        Cost child_impact = 0;
        for (TDGNodeId child_id : children(node_id)) {
            int divisor = std::max(1, parent_count[child_id]);
            child_impact = add_impact_saturated(
                child_impact,
                impacts[child_id] / static_cast<Cost>(divisor));
        }
        impacts[node_id] = add_impact_saturated(
            impacts[node_id],
            scale_impact_percent_saturated(child_impact, options_.lambda));

        for (TDGNodeId parent_id : parents(node_id)) {
            --child_count[parent_id];
            if (child_count[parent_id] == 0) {
                ready.push_back(parent_id);
            }
        }
    }

    return impacts;
}

std::vector<Cost> GROAlgorithm::normalize_tdg_impacts_for_reroute(
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& raw_impacts) const {
    std::vector<Cost> normalized(raw_impacts.size(), 0);
    if (raw_impacts.empty() || tdg.nodes.empty()) {
        return normalized;
    }

    std::vector<Cost> impact_values;
    impact_values.reserve(raw_impacts.size());
    for (Cost impact : raw_impacts) {
        impact_values.push_back(std::max<Cost>(0, impact));
    }

    Cost impact_clip = percentile_value(std::move(impact_values), 99);
    if (impact_clip <= 0) {
        return normalized;
    }

    std::vector<Cost> edge_time_values;
    edge_time_values.reserve(tdg.nodes.size());
    for (const TDGNode& node : tdg.nodes) {
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(graph_.edges.size())) {
            continue;
        }
        edge_time_values.push_back(
            std::max<Cost>(
                1,
                bpr_travel_time(
                    graph_.edges[node.edge_id],
                    node.flow,
                    traffic_options_)));
    }

    Cost time_scale = percentile_value(std::move(edge_time_values), 90);
    if (time_scale <= 0) {
        time_scale = 1;
    }

    const long double log_clip =
        std::log1p(static_cast<long double>(impact_clip));
    if (log_clip <= 0.0L) {
        return normalized;
    }

    for (size_t index = 0; index < raw_impacts.size(); ++index) {
        Cost clipped =
            std::min(std::max<Cost>(0, raw_impacts[index]), impact_clip);
        long double ratio =
            std::log1p(static_cast<long double>(clipped)) / log_clip;
        long double scaled = ratio * static_cast<long double>(time_scale);
        normalized[index] = clamp_impact(
            static_cast<__int128>(std::llround(scaled)));
    }

    return normalized;
}

std::vector<double> GROAlgorithm::normalize_tdg_impacts_for_selection(
    const std::vector<Cost>& raw_impacts) const {
    std::vector<double> normalized(raw_impacts.size(), 0.0);
    if (raw_impacts.empty()) {
        return normalized;
    }

    std::vector<Cost> impact_values;
    impact_values.reserve(raw_impacts.size());
    for (Cost impact : raw_impacts) {
        impact_values.push_back(std::max<Cost>(0, impact));
    }

    Cost impact_clip = percentile_value(std::move(impact_values), 99);
    if (impact_clip <= 0) {
        return normalized;
    }

    const long double log_clip =
        std::log1p(static_cast<long double>(impact_clip));
    if (log_clip <= 0.0L) {
        return normalized;
    }

    for (size_t index = 0; index < raw_impacts.size(); ++index) {
        Cost clipped =
            std::min(std::max<Cost>(0, raw_impacts[index]), impact_clip);
        long double ratio =
            std::log1p(static_cast<long double>(clipped)) / log_clip;
        normalized[index] = static_cast<double>(
            std::clamp<long double>(ratio, 0.0L, 1.0L));
    }

    return normalized;
}

void GROAlgorithm::remove_trajectory_from_tdg(
    TrafficDependencyGraph& tdg,
    const Trajectory& trajectory,
    const TrafficResult&) const {

    auto remove_route_references = [&](TDGNodeId node_id) {
        for (TDGNodeId parent_id : tdg.route_incoming[node_id]) {
            auto& outgoing = tdg.route_outgoing[parent_id];
            outgoing.erase(std::remove(outgoing.begin(), outgoing.end(), node_id), outgoing.end());
        }

        for (TDGNodeId child_id : tdg.route_outgoing[node_id]) {
            auto& incoming = tdg.route_incoming[child_id];
            incoming.erase(std::remove(incoming.begin(), incoming.end(), node_id), incoming.end());
        }

        tdg.route_incoming[node_id].clear();
        tdg.route_outgoing[node_id].clear();
    };

    auto has_edge_dependency = [&](EdgeId edge_id, const TimeLineEvent& from, const TimeLineEvent& to) {
        return from.time + bpr_travel_time(
            graph_.edges[edge_id],
            from.flow,
            traffic_options_) > to.time;
    };

    auto set_timeline_connection = [&](EdgeId edge_id,
                                       std::map<Time, TimeLineEvent>::iterator from_it,
                                       std::map<Time, TimeLineEvent>::iterator to_it) {
        bool connected = has_edge_dependency(edge_id, from_it->second, to_it->second);
        from_it->second.same_edge_child = connected ? to_it->second.node_id : kInvalidId;
        to_it->second.same_edge_parent = connected ? from_it->second.node_id : kInvalidId;
    };

    auto recompute_around = [&](EdgeId edge_id,
                                std::map<Time, TimeLineEvent>& timeline,
                                std::map<Time, TimeLineEvent>::iterator it) {
        if (it != timeline.begin()) {
            auto prev_it = std::prev(it);
            prev_it->second.same_edge_child = kInvalidId;
            it->second.same_edge_parent = kInvalidId;
            set_timeline_connection(edge_id, prev_it, it);
        } else {
            it->second.same_edge_parent = kInvalidId;
        }

        auto next_it = std::next(it);
        if (next_it != timeline.end()) {
            it->second.same_edge_child = kInvalidId;
            next_it->second.same_edge_parent = kInvalidId;
            set_timeline_connection(edge_id, it, next_it);
        } else {
            it->second.same_edge_child = kInvalidId;
        }
    };

    for (TDGNodeId node_id : trajectory.tdg_node_ids) {
        TDGNode& node = tdg.nodes[node_id];
        if (node.flow <= 0) {
            continue;
        }
        EdgeId edge_id = node.edge_id;
        auto& timeline = tdg.edge_timelines[edge_id];
        auto timeline_it = timeline.find(node.time);
        if (timeline_it == timeline.end()) {
            continue;
        }

        node.flow -= 1;
        timeline_it->second.flow = node.flow;

        if (node.flow == 0) {
            remove_route_references(node_id);
            tdg.key_to_node_id.erase({edge_id, node.time});

            bool has_prev = timeline_it != timeline.begin();
            auto prev_it = has_prev ? std::prev(timeline_it) : timeline.end();
            auto next_it = std::next(timeline_it);
            bool has_next = next_it != timeline.end();

            if (has_prev) {
                prev_it->second.same_edge_child = kInvalidId;
            }
            if (has_next) {
                next_it->second.same_edge_parent = kInvalidId;
            }

            timeline.erase(timeline_it);
            if (has_prev && has_next) {
                set_timeline_connection(edge_id, prev_it, next_it);
            }
        } else {
            recompute_around(edge_id, timeline, timeline_it);
        }
    }
}

void GROAlgorithm::insert_trajectory_into_tdg(
    TrafficDependencyGraph& tdg,
    const Trajectory& trajectory,
    const TrafficResult&) const {

    auto has_edge_dependency = [&](EdgeId edge_id, const TimeLineEvent& from, const TimeLineEvent& to) {
        return from.time + bpr_travel_time(
            graph_.edges[edge_id],
            from.flow,
            traffic_options_) > to.time;
    };

    auto set_timeline_connection = [&](EdgeId edge_id,
                                       std::map<Time, TimeLineEvent>::iterator from_it,
                                       std::map<Time, TimeLineEvent>::iterator to_it) {
        bool connected = has_edge_dependency(edge_id, from_it->second, to_it->second);
        from_it->second.same_edge_child = connected ? to_it->second.node_id : kInvalidId;
        to_it->second.same_edge_parent = connected ? from_it->second.node_id : kInvalidId;
    };

    auto recompute_around = [&](EdgeId edge_id,
                                std::map<Time, TimeLineEvent>& timeline,
                                std::map<Time, TimeLineEvent>::iterator it) {
        if (it != timeline.begin()) {
            auto prev_it = std::prev(it);
            prev_it->second.same_edge_child = kInvalidId;
            it->second.same_edge_parent = kInvalidId;
            set_timeline_connection(edge_id, prev_it, it);
        } else {
            it->second.same_edge_parent = kInvalidId;
        }

        auto next_it = std::next(it);
        if (next_it != timeline.end()) {
            it->second.same_edge_child = kInvalidId;
            next_it->second.same_edge_parent = kInvalidId;
            set_timeline_connection(edge_id, it, next_it);
        } else {
            it->second.same_edge_child = kInvalidId;
        }
    };

    auto add_route_arc = [&](TDGNodeId from, TDGNodeId to) {
        if (from == kInvalidId || to == kInvalidId || from == to ||
            from < 0 || to < 0 ||
            from >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
            to >= static_cast<TDGNodeId>(tdg.nodes.size())) {
            return;
        }
        auto& outgoing = tdg.route_outgoing[from];
        if (std::find(outgoing.begin(), outgoing.end(), to) == outgoing.end()) {
            outgoing.push_back(to);
        }
        auto& incoming = tdg.route_incoming[to];
        if (std::find(incoming.begin(), incoming.end(), from) == incoming.end()) {
            incoming.push_back(from);
        }
    };

#if 0
    // Disabled by design: reroute insertion should not create new TDG nodes.
    auto ensure_node = [&](EdgeId edge_id, Time time) -> TDGNodeId {
        if (edge_id < 0 ||
            edge_id >= static_cast<EdgeId>(tdg.edge_timelines.size())) {
            return kInvalidId;
        }

        Key key = {edge_id, time};
        auto node_it = tdg.key_to_node_id.find(key);
        if (node_it != tdg.key_to_node_id.end()) {
            return node_it->second;
        }

        TDGNode node;
        node.id = static_cast<TDGNodeId>(tdg.nodes.size());
        node.edge_id = edge_id;
        node.time = time;
        node.flow = 0;
        node.original_flow = 0;
        tdg.nodes.push_back(node);
        while (tdg.route_outgoing.size() <= static_cast<size_t>(node.id)) {
            tdg.route_outgoing.emplace_back();
        }
        while (tdg.route_incoming.size() <= static_cast<size_t>(node.id)) {
            tdg.route_incoming.emplace_back();
        }
        tdg.key_to_node_id[key] = node.id;

        auto& timeline = tdg.edge_timelines[edge_id];
        auto timeline_it = timeline.insert_or_assign(
            time,
            TimeLineEvent{time, node.id, 0, kInvalidId, kInvalidId}).first;
        recompute_around(edge_id, timeline, timeline_it);
        return node.id;
    };
#endif

    std::vector<TDGNodeId> route_nodes;
    std::vector<TDGNodeId> touched_nodes;
    std::vector<char> touched_seen(tdg.nodes.size(), 0);

    auto mark_touched = [&](TDGNodeId node_id) {
        if (node_id == kInvalidId ||
            node_id < 0 ||
            node_id >= static_cast<TDGNodeId>(tdg.nodes.size())) {
            return;
        }
        if (static_cast<size_t>(node_id) >= touched_seen.size()) {
            touched_seen.resize(static_cast<size_t>(node_id) + 1, 0);
        }
        if (!touched_seen[node_id]) {
            touched_seen[node_id] = 1;
            touched_nodes.push_back(node_id);
        }
    };

    route_nodes = trajectory.tdg_node_ids;
    for (TDGNodeId node_id : route_nodes) {
        mark_touched(node_id);
    }

    for (TDGNodeId node_id : touched_nodes) {
        TDGNode& node = tdg.nodes[node_id];
        auto& timeline = tdg.edge_timelines[node.edge_id];
        auto timeline_it = timeline.find(node.time);
        if (timeline_it != timeline.end()) {
            node.flow += 1;
            timeline_it->second.flow = node.flow;
        }
    }

    for (TDGNodeId node_id : touched_nodes) {
        const TDGNode& node = tdg.nodes[node_id];
        auto& timeline = tdg.edge_timelines[node.edge_id];
        auto timeline_it = timeline.find(node.time);
        if (timeline_it != timeline.end()) {
            recompute_around(node.edge_id, timeline, timeline_it);
        }
    }

    for (size_t index = 1; index < route_nodes.size(); ++index) {
        add_route_arc(route_nodes[index - 1], route_nodes[index]);
    }
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates(
    const std::vector<Query>&,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>&) const {
    if (tdg.nodes.empty()) {
        return {};
    }
    TDGNodeId node_count = tdg.nodes.size();

    auto children = [&](TDGNodeId node_id) {
        std::vector<TDGNodeId> ids = tdg.route_outgoing[node_id];

        const TDGNode& node = tdg.nodes[node_id];
        const auto& timeline = tdg.edge_timelines[node.edge_id];
        auto event_it = timeline.find(node.time);
        if (event_it != timeline.end() and event_it->second.same_edge_child != kInvalidId) {
            ids.push_back(event_it->second.same_edge_child);
        }
        return ids;
    };

    std::vector<Cost> congestion_ratios;
    congestion_ratios.reserve(tdg.nodes.size());
    for (const TDGNode& node : tdg.nodes) {
        congestion_ratios.push_back(
            congestion_ratio_key(node.flow, graph_.edges[node.edge_id].capacity));
    }

    if (congestion_ratios.empty()) {
        return {};
    }

    std::sort(congestion_ratios.begin(), congestion_ratios.end());
    size_t theta_index = percentile_index(congestion_ratios.size(), options_.theta_percentile);
    Cost theta = congestion_ratios[theta_index];

    std::vector<char> targeted(tdg.nodes.size(), 0);
    for (TDGNodeId node_id = 0; node_id < node_count; ++node_id) {
        const TDGNode& node = tdg.nodes[node_id];
        targeted[node_id] =
            congestion_ratio_key(node.flow, graph_.edges[node.edge_id].capacity) > theta;
    }

    std::vector<int> indegree(tdg.nodes.size(), 0);
    for (TDGNodeId node_id = 0; node_id < node_count; ++node_id) {
        for (TDGNodeId child_id : children(node_id)) {
            ++indegree[child_id];
        }
    }

    std::vector<char> has_targeted_ancestor(tdg.nodes.size(), 0);
    std::vector<char> source_node(tdg.nodes.size(), 0);
    std::vector<TDGNodeId> ready;
    ready.reserve(tdg.nodes.size());

    // Start from nodes with zero indegree (no dependencies)
    for (TDGNodeId node_id = 0; node_id < node_count; ++node_id) {
        if (indegree[node_id] == 0) {
            ready.push_back(node_id);
        }
    }

    while (!ready.empty()) {
        TDGNodeId node_id = ready.back();
        ready.pop_back();

        if (targeted[node_id] && !has_targeted_ancestor[node_id]) {
            source_node[node_id] = 1;
        }

        bool child_has_targeted_ancestor =
            has_targeted_ancestor[node_id] || targeted[node_id];
        for (TDGNodeId child_id : children(node_id)) {
            has_targeted_ancestor[child_id] =
                has_targeted_ancestor[child_id] || child_has_targeted_ancestor;
            if (--indegree[child_id] == 0) {
                ready.push_back(child_id);
            }
        }
    }

    std::unordered_set<QueryId> candidate_ids;
    for (const Trajectory& trajectory : result.trajectories) {
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (source_node[node_id]) {
                candidate_ids.insert(trajectory.query_id);
                break;
            }
        }
    }

    return candidate_ids;
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates_by_score(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts) const {
    std::unordered_set<QueryId> candidate_ids;
    if (queries.empty() || result.trajectories.empty() || tdg.nodes.empty()) {
        return candidate_ids;
    }

    std::vector<long double> relief =
        build_excess_relief_values(*this, graph_, result, tdg, node_impacts);
    if (relief.empty()) {
        return candidate_ids;
    }

    std::vector<int> seen(tdg.nodes.size(), 0);
    int seen_epoch = 0;
    std::vector<std::pair<long double, QueryId>> ranking;
    ranking.reserve(result.trajectories.size());
    long double total_score = 0.0L;

    for (const Trajectory& trajectory : result.trajectories) {
        if (trajectory.query_id < 0 ||
            trajectory.query_id >= static_cast<QueryId>(queries.size())) {
            continue;
        }
        long double score =
            route_relief_score(trajectory, relief, seen, seen_epoch);
        if (score <= 0.0L) {
            continue;
        }
        ranking.push_back({score, trajectory.query_id});
        total_score += score;
    }

    if (ranking.empty() || total_score <= 0.0L) {
        return candidate_ids;
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

    int clamped_theta = std::clamp(options_.candidate_theta, 0, 100);
    if (clamped_theta <= 0) {
        return candidate_ids;
    }

    long double target_score =
        total_score *
        static_cast<long double>(clamped_theta) /
        100.0L;
    long double covered_score = 0.0L;
    for (const auto& item : ranking) {
        candidate_ids.insert(item.second);
        covered_score += item.first;
        if (covered_score >= target_score) {
            break;
        }
    }

    return candidate_ids;
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates_by_component_balance(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts) const {
    std::unordered_set<QueryId> candidate_ids;
    if (queries.empty() || result.trajectories.empty() || tdg.nodes.empty()) {
        return candidate_ids;
    }

    std::vector<long double> relief =
        build_excess_relief_values(*this, graph_, result, tdg, node_impacts);
    if (relief.empty()) {
        return candidate_ids;
    }

    std::vector<char> active(tdg.nodes.size(), 0);
    long long active_count = 0;
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(relief.size());
         ++node_id) {
        if (relief[node_id] > 0.0L) {
            active[node_id] = 1;
            ++active_count;
        }
    }
    if (active_count == 0) {
        return candidate_ids;
    }

    CandidateComponentDSU dsu(tdg.nodes.size());
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (!active[node_id]) {
            continue;
        }

        for (TDGNodeId child_id : tdg.route_outgoing[node_id]) {
            if (child_id >= 0 &&
                child_id < static_cast<TDGNodeId>(active.size()) &&
                active[child_id]) {
                dsu.unite(
                    static_cast<size_t>(node_id),
                    static_cast<size_t>(child_id));
            }
        }

        const TDGNode& node = tdg.nodes[node_id];
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(tdg.edge_timelines.size())) {
            continue;
        }
        const auto& timeline = tdg.edge_timelines[node.edge_id];
        auto event_it = timeline.find(node.time);
        if (event_it == timeline.end()) {
            continue;
        }
        TDGNodeId child_id = event_it->second.same_edge_child;
        if (child_id >= 0 &&
            child_id < static_cast<TDGNodeId>(active.size()) &&
            active[child_id]) {
            dsu.unite(
                static_cast<size_t>(node_id),
                static_cast<size_t>(child_id));
        }
    }

    std::unordered_map<size_t, int> root_to_component;
    std::vector<int> node_component(tdg.nodes.size(), -1);
    std::vector<long double> component_mass;
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (!active[node_id]) {
            continue;
        }

        size_t root = dsu.find(static_cast<size_t>(node_id));
        auto [it, inserted] =
            root_to_component.emplace(root, static_cast<int>(component_mass.size()));
        if (inserted) {
            component_mass.push_back(0.0L);
        }
        int component_id = it->second;
        node_component[node_id] = component_id;
        component_mass[component_id] += relief[node_id];
    }

    if (component_mass.empty()) {
        return candidate_ids;
    }

    std::vector<std::vector<std::pair<long double, QueryId>>> component_queries(
        component_mass.size());
    std::vector<long double> query_component_score(component_mass.size(), 0.0L);
    std::vector<int> seen_component(component_mass.size(), 0);
    int component_epoch = 0;
    std::vector<int> seen_node(tdg.nodes.size(), 0);
    int node_epoch = 0;

    for (const Trajectory& trajectory : result.trajectories) {
        QueryId query_id = trajectory.query_id;
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(queries.size())) {
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

        std::vector<int> touched_components;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(relief.size()) ||
                node_id >= static_cast<TDGNodeId>(seen_node.size()) ||
                seen_node[node_id] == node_epoch ||
                relief[node_id] <= 0.0L) {
                continue;
            }
            seen_node[node_id] = node_epoch;

            int component_id = node_component[node_id];
            if (component_id < 0 ||
                component_id >= static_cast<int>(component_mass.size())) {
                continue;
            }
            if (seen_component[component_id] != component_epoch) {
                seen_component[component_id] = component_epoch;
                query_component_score[component_id] = 0.0L;
                touched_components.push_back(component_id);
            }
            query_component_score[component_id] += relief[node_id];
        }

        for (int component_id : touched_components) {
            long double score = query_component_score[component_id];
            if (score <= 0.0L) {
                continue;
            }
            component_queries[component_id].push_back({score, query_id});
        }
    }

    int clamped_gamma = std::clamp(options_.gamma, 0, 100);
    if (clamped_gamma <= 0) {
        return candidate_ids;
    }

    for (size_t component_id = 0;
         component_id < component_queries.size();
         ++component_id) {
        auto& ranking = component_queries[component_id];
        if (ranking.empty() || component_mass[component_id] <= 0.0L) {
            continue;
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

        long double target_score =
            component_mass[component_id] *
            static_cast<long double>(clamped_gamma) /
            100.0L;
        long double covered_score = 0.0L;
        for (const auto& item : ranking) {
            candidate_ids.insert(item.second);
            covered_score += item.first;
            if (covered_score >= target_score) {
                break;
            }
        }
    }

    return candidate_ids;
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates_by_component_marginal(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts) const {
    return select_candidates_by_component_marginal(
        queries,
        result,
        tdg,
        node_impacts,
        std::numeric_limits<size_t>::max());
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates_by_component_marginal(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    size_t max_candidate_count) const {
    return select_candidates_by_component_marginal(
        queries,
        result,
        tdg,
        node_impacts,
        max_candidate_count,
        100);
}

std::unordered_set<QueryId> GROAlgorithm::select_candidates_by_component_marginal(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    size_t max_candidate_count,
    int component_mass_percent) const {
    std::unordered_set<QueryId> candidate_ids;
    if (max_candidate_count == 0) {
        return candidate_ids;
    }
    if (queries.empty() || result.trajectories.empty() || tdg.nodes.empty()) {
        return candidate_ids;
    }

    std::vector<long double> relief =
        build_excess_relief_values(*this, graph_, result, tdg, node_impacts);
    if (relief.empty()) {
        return candidate_ids;
    }

    std::vector<char> active(tdg.nodes.size(), 0);
    long long active_count = 0;
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(relief.size());
         ++node_id) {
        if (relief[node_id] > 0.0L) {
            active[node_id] = 1;
            ++active_count;
        }
    }
    if (active_count == 0) {
        return candidate_ids;
    }

    CandidateComponentDSU dsu(tdg.nodes.size());
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (!active[node_id]) {
            continue;
        }

        for (TDGNodeId child_id : tdg.route_outgoing[node_id]) {
            if (child_id >= 0 &&
                child_id < static_cast<TDGNodeId>(active.size()) &&
                active[child_id]) {
                dsu.unite(
                    static_cast<size_t>(node_id),
                    static_cast<size_t>(child_id));
            }
        }

        const TDGNode& node = tdg.nodes[node_id];
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(tdg.edge_timelines.size())) {
            continue;
        }
        const auto& timeline = tdg.edge_timelines[node.edge_id];
        auto event_it = timeline.find(node.time);
        if (event_it == timeline.end()) {
            continue;
        }
        TDGNodeId child_id = event_it->second.same_edge_child;
        if (child_id >= 0 &&
            child_id < static_cast<TDGNodeId>(active.size()) &&
            active[child_id]) {
            dsu.unite(
                static_cast<size_t>(node_id),
                static_cast<size_t>(child_id));
        }
    }

    std::unordered_map<size_t, int> root_to_component;
    std::vector<int> node_component(tdg.nodes.size(), -1);
    std::vector<long double> component_mass;
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (!active[node_id]) {
            continue;
        }

        size_t root = dsu.find(static_cast<size_t>(node_id));
        auto [it, inserted] =
            root_to_component.emplace(root, static_cast<int>(component_mass.size()));
        if (inserted) {
            component_mass.push_back(0.0L);
        }
        int component_id = it->second;
        node_component[node_id] = component_id;
        component_mass[component_id] += relief[node_id];
    }

    if (component_mass.empty()) {
        return candidate_ids;
    }

    int clamped_component_mass_percent =
        std::clamp(component_mass_percent, 0, 100);
    if (clamped_component_mass_percent <= 0) {
        return candidate_ids;
    }

    std::vector<char> kept_component(component_mass.size(), 1);
    if (clamped_component_mass_percent < 100) {
        std::fill(kept_component.begin(), kept_component.end(), 0);

        long double total_component_mass = 0.0L;
        std::vector<size_t> component_order(component_mass.size());
        for (size_t component_id = 0;
             component_id < component_mass.size();
             ++component_id) {
            total_component_mass += component_mass[component_id];
            component_order[component_id] = component_id;
        }
        if (total_component_mass <= 0.0L) {
            return candidate_ids;
        }

        std::sort(
            component_order.begin(),
            component_order.end(),
            [&](size_t lhs, size_t rhs) {
                if (component_mass[lhs] != component_mass[rhs]) {
                    return component_mass[lhs] > component_mass[rhs];
                }
                return lhs < rhs;
            });

        long double target_component_mass =
            total_component_mass *
            static_cast<long double>(clamped_component_mass_percent) /
            100.0L;
        long double covered_component_mass = 0.0L;
        for (size_t component_id : component_order) {
            if (component_mass[component_id] <= 0.0L) {
                continue;
            }
            kept_component[component_id] = 1;
            covered_component_mass += component_mass[component_id];
            if (covered_component_mass >= target_component_mass) {
                break;
            }
        }
    }

    std::vector<std::vector<std::pair<int, long double>>> query_components(
        queries.size());
    std::vector<long double> query_component_score(component_mass.size(), 0.0L);
    std::vector<int> seen_component(component_mass.size(), 0);
    int component_epoch = 0;
    std::vector<int> seen_node(tdg.nodes.size(), 0);
    int node_epoch = 0;

    for (const Trajectory& trajectory : result.trajectories) {
        QueryId query_id = trajectory.query_id;
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(queries.size())) {
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

        std::vector<int> touched_components;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(relief.size()) ||
                node_id >= static_cast<TDGNodeId>(seen_node.size()) ||
                seen_node[node_id] == node_epoch ||
                relief[node_id] <= 0.0L) {
                continue;
            }
            seen_node[node_id] = node_epoch;

            int component_id = node_component[node_id];
            if (component_id < 0 ||
                component_id >= static_cast<int>(component_mass.size()) ||
                !kept_component[component_id]) {
                continue;
            }
            if (seen_component[component_id] != component_epoch) {
                seen_component[component_id] = component_epoch;
                query_component_score[component_id] = 0.0L;
                touched_components.push_back(component_id);
            }
            query_component_score[component_id] += relief[node_id];
        }

        auto& components_for_query = query_components[query_id];
        components_for_query.reserve(touched_components.size());
        for (int component_id : touched_components) {
            long double score = query_component_score[component_id];
            if (score <= 0.0L) {
                continue;
            }
            components_for_query.push_back({component_id, score});
        }
    }

    int clamped_gamma = std::clamp(options_.gamma, 0, 100);
    if (clamped_gamma <= 0) {
        return candidate_ids;
    }

    std::vector<long double> remaining(component_mass.size(), 0.0L);
    long double total_remaining = 0.0L;
    for (size_t component_id = 0;
         component_id < component_mass.size();
         ++component_id) {
        if (!kept_component[component_id]) {
            continue;
        }
        remaining[component_id] =
            component_mass[component_id] *
            static_cast<long double>(clamped_gamma) /
            100.0L;
        total_remaining += remaining[component_id];
    }
    if (total_remaining <= 0.0L) {
        return candidate_ids;
    }

    auto exact_gain = [&](QueryId query_id) {
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(query_components.size())) {
            return 0.0L;
        }
        long double gain = 0.0L;
        for (const auto& item : query_components[query_id]) {
            int component_id = item.first;
            if (component_id < 0 ||
                component_id >= static_cast<int>(remaining.size()) ||
                remaining[component_id] <= 0.0L) {
                continue;
            }
            gain += std::min(item.second, remaining[component_id]);
        }
        return gain;
    };

    struct HeapEntry {
        long double gain = 0.0L;
        QueryId query_id = kInvalidId;
    };
    struct HeapEntryLess {
        bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const {
            if (lhs.gain != rhs.gain) {
                return lhs.gain < rhs.gain;
            }
            return lhs.query_id > rhs.query_id;
        }
    };

    std::priority_queue<
        HeapEntry,
        std::vector<HeapEntry>,
        HeapEntryLess> heap;
    for (QueryId query_id = 0;
         query_id < static_cast<QueryId>(query_components.size());
         ++query_id) {
        if (query_components[query_id].empty()) {
            continue;
        }
        long double gain = exact_gain(query_id);
        if (gain > 0.0L) {
            heap.push({gain, query_id});
        }
    }

    std::vector<char> selected(queries.size(), 0);
    const long double stale_epsilon = 1e-12L;
    const long double stop_epsilon = 1e-12L;
    while (!heap.empty() &&
           total_remaining > stop_epsilon &&
           candidate_ids.size() < max_candidate_count) {
        HeapEntry entry = heap.top();
        heap.pop();

        QueryId query_id = entry.query_id;
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(selected.size()) ||
            selected[query_id]) {
            continue;
        }

        long double gain = exact_gain(query_id);
        if (gain <= stop_epsilon) {
            continue;
        }
        if (gain + stale_epsilon < entry.gain) {
            heap.push({gain, query_id});
            continue;
        }

        selected[query_id] = 1;
        candidate_ids.insert(query_id);
        for (const auto& item : query_components[query_id]) {
            int component_id = item.first;
            if (component_id < 0 ||
                component_id >= static_cast<int>(remaining.size()) ||
                remaining[component_id] <= 0.0L) {
                continue;
            }
            long double decrement = std::min(item.second, remaining[component_id]);
            remaining[component_id] -= decrement;
            total_remaining -= decrement;
        }
    }

    return candidate_ids;
}

std::vector<QueryId> GROAlgorithm::select_queries(
    const std::unordered_set<QueryId>& candidate_set,
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    (void)queries;
    (void)node_impacts;
    auto total_start = Clock::now();
    if (candidate_set.empty()) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            0,
            0,
            0,
            0,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    TrafficDependencyGraph working_tdg = tdg;
    std::unordered_set<QueryId> selected;
    // std::unordered_set<QueryId> candidate_set(candidate_queries.begin(), candidate_queries.end());

    auto route_score = [&](const Trajectory& trajectory, const std::vector<Cost>& impacts) {
        Cost score = 0;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            score = add_impact_saturated(score, impacts[node_id]);
        }
        return score;
    };

    auto is_removable = [&](const Trajectory& trajectory) {
        (void)trajectory;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            const TDGNode& node = working_tdg.nodes[node_id];
            int min_flow =
                (100 - options_.gamma) * node.original_flow / 100;
            if (node.flow - 1 < min_flow) {
                return false;
            }
        }
        return true;
    };

    long long refresh_impact_us = 0;
    long long rank_us = 0;
    long long scan_us = 0;
    long long remove_us = 0;

    while (selected.size() < candidate_set.size()) {
        auto impact_start = Clock::now();
        std::vector<Cost> impacts = compute_tdg_impact(working_tdg);
        refresh_impact_us += elapsed_us(impact_start);

        auto ranking_start = Clock::now();
        std::vector<std::pair<Cost, QueryId>> ranking;
        ranking.reserve(candidate_set.size() - selected.size());

        for (QueryId query_id : candidate_set) {
            if (selected.find(query_id) != selected.end()) {
                continue;
            }

            ranking.push_back({
                route_score(result.trajectories[query_id], impacts),
                query_id
            });
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
        rank_us += elapsed_us(ranking_start);

        size_t reject = 0;
        size_t selected_this_round = 0;
        size_t stale_threshold = integer_sqrt(ranking.size());
        auto scan_start = Clock::now();

        for (const auto& [_, query_id] : ranking) {
            const Trajectory& trajectory = result.trajectories[query_id];

            if (is_removable(trajectory)) {
                selected.insert(query_id);
                ++selected_this_round;
                reject = 0;
                auto remove_start = Clock::now();
                remove_trajectory_from_tdg(working_tdg, trajectory, result);
                remove_us += elapsed_us(remove_start);
            } else {
                ++reject;
                if (reject > stale_threshold) {
                    break;
                }
            }
        }
        scan_us += elapsed_us(scan_start);

        if (selected_this_round == 0) {
            break;
        }
    }

    std::vector<QueryId> selected_queries = {selected.begin(), selected.end()};
    log_select_summary(
        options_.enable_timing_log,
        iteration,
        static_cast<long long>(candidate_set.size()),
        static_cast<long long>(selected_queries.size()),
        refresh_impact_us,
        rank_us,
        scan_us,
        remove_us,
        elapsed_us(total_start));
    return selected_queries;
}

std::vector<QueryId> GROAlgorithm::select_queries(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    auto candidate_start = Clock::now();
    std::unordered_set<QueryId> candidate_set =
        select_candidates(queries, result, tdg, node_impacts);
    long long candidate_us = elapsed_us(candidate_start);
    log_candidate_summary(
        options_.enable_timing_log,
        iteration,
        static_cast<long long>(candidate_set.size()),
        candidate_us);
    return select_queries(candidate_set, queries, result, tdg, node_impacts, iteration);
}

std::vector<QueryId> GROAlgorithm::select_queries_by_excess_relief(
    const std::unordered_set<QueryId>& candidate_set,
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    (void)queries;
    auto total_start = Clock::now();
    if (candidate_set.empty() || tdg.nodes.empty()) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            0,
            0,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    auto prepare_start = Clock::now();
    std::vector<double> selection_impacts =
        normalize_tdg_impacts_for_selection(node_impacts);

    std::vector<std::map<Time, Cost>> anchor_scores =
        compute_anchor_scores(result);
    std::vector<char> important =
        mark_anchor_tdg_nodes(tdg, anchor_scores);

    bool has_important =
        std::any_of(important.begin(), important.end(), [](char value) {
            return value != 0;
        });
    if (!has_important) {
        for (TDGNodeId node_id = 0;
             node_id < static_cast<TDGNodeId>(tdg.nodes.size());
             ++node_id) {
            const TDGNode& node = tdg.nodes[node_id];
            if (node.edge_id < 0 ||
                node.edge_id >= static_cast<EdgeId>(graph_.edges.size())) {
                continue;
            }
            Flow capacity = std::max<Flow>(1, graph_.edges[node.edge_id].capacity);
            if (node.flow > capacity) {
                important[node_id] = 1;
            }
        }
    }
    long long prepare_us = elapsed_us(prepare_start);

    TrafficDependencyGraph working_tdg = tdg;
    std::unordered_set<QueryId> selected;
    std::vector<QueryId> candidate_ids(
        candidate_set.begin(),
        candidate_set.end());
    std::sort(candidate_ids.begin(), candidate_ids.end());

    auto node_relief = [&](TDGNodeId node_id) -> long double {
        if (node_id < 0 ||
            node_id >= static_cast<TDGNodeId>(working_tdg.nodes.size()) ||
            node_id >= static_cast<TDGNodeId>(important.size()) ||
            node_id >= static_cast<TDGNodeId>(selection_impacts.size()) ||
            !important[node_id]) {
            return 0.0L;
        }

        const TDGNode& node = working_tdg.nodes[node_id];
        if (node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(graph_.edges.size())) {
            return 0.0L;
        }

        Flow capacity = std::max<Flow>(1, graph_.edges[node.edge_id].capacity);
        if (node.flow <= capacity) {
            return 0.0L;
        }

        long double excess =
            static_cast<long double>(node.flow - capacity) /
            static_cast<long double>(capacity);
        return static_cast<long double>(selection_impacts[node_id]) * excess;
    };

    auto congestion_mass = [&]() -> long double {
        long double mass = 0.0L;
        for (TDGNodeId node_id = 0;
             node_id < static_cast<TDGNodeId>(working_tdg.nodes.size());
             ++node_id) {
            mass += node_relief(node_id);
        }
        return mass;
    };

    std::vector<int> seen(tdg.nodes.size(), 0);
    int seen_epoch = 0;
    auto route_score = [&](const Trajectory& trajectory) -> long double {
        ++seen_epoch;
        if (seen_epoch == std::numeric_limits<int>::max()) {
            std::fill(seen.begin(), seen.end(), 0);
            seen_epoch = 1;
        }

        long double score = 0.0L;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(seen.size()) ||
                seen[node_id] == seen_epoch) {
                continue;
            }
            seen[node_id] = seen_epoch;
            score += node_relief(node_id);
        }
        return score;
    };

    std::vector<int> updated(tdg.nodes.size(), 0);
    int update_epoch = 0;
    auto decrease_trajectory_flow_only =
        [&](const Trajectory& trajectory) -> long double {
        ++update_epoch;
        if (update_epoch == std::numeric_limits<int>::max()) {
            std::fill(updated.begin(), updated.end(), 0);
            update_epoch = 1;
        }

        long double removed_mass = 0.0L;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(working_tdg.nodes.size()) ||
                node_id >= static_cast<TDGNodeId>(updated.size()) ||
                updated[node_id] == update_epoch) {
                continue;
            }
            updated[node_id] = update_epoch;
            TDGNode& node = working_tdg.nodes[node_id];
            long double before = node_relief(node_id);
            if (node.flow > 0) {
                node.flow -= 1;
            }
            long double after = node_relief(node_id);
            if (before > after) {
                removed_mass += before - after;
            }
        }
        return removed_mass;
    };

    long double initial_mass = congestion_mass();
    if (initial_mass <= 0.0L) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            prepare_us,
            0,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    int clamped_gamma = std::clamp(options_.gamma, 0, 100);
    long double target_mass =
        initial_mass *
        static_cast<long double>(100 - clamped_gamma) /
        100.0L;

    long long rank_us = 0;
    long long scan_us = 0;
    long long remove_us = 0;

    // Route relief scores only decrease as flows are removed, so stale heap
    // entries are safe upper bounds for an exact lazy-greedy scan.
    struct CandidateScoreEntry {
        long double score = 0.0L;
        QueryId query_id = kInvalidId;
    };

    struct CandidateScoreLess {
        bool operator()(
            const CandidateScoreEntry& lhs,
            const CandidateScoreEntry& rhs) const {
            if (lhs.score != rhs.score) {
                return lhs.score < rhs.score;
            }
            return lhs.query_id > rhs.query_id;
        }
    };

    auto outranks = [](
                        const CandidateScoreEntry& lhs,
                        const CandidateScoreEntry& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.query_id < rhs.query_id;
    };

    std::priority_queue<
        CandidateScoreEntry,
        std::vector<CandidateScoreEntry>,
        CandidateScoreLess>
        queue;
    std::vector<long double> cached_scores(result.trajectories.size(), 0.0L);
    std::vector<char> selected_flags(result.trajectories.size(), 0);

    auto rank_start = Clock::now();
    for (QueryId query_id : candidate_ids) {
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(result.trajectories.size())) {
            continue;
        }

        long double score = route_score(result.trajectories[query_id]);
        if (score <= 0.0L) {
            continue;
        }
        cached_scores[query_id] = score;
        queue.push({score, query_id});
    }
    rank_us += elapsed_us(rank_start);

    auto discard_invalid_top = [&]() {
        while (!queue.empty()) {
            const CandidateScoreEntry& entry = queue.top();
            if (entry.query_id < 0 ||
                entry.query_id >= static_cast<QueryId>(cached_scores.size()) ||
                selected_flags[entry.query_id] ||
                entry.score != cached_scores[entry.query_id]) {
                queue.pop();
                continue;
            }
            break;
        }
    };

    long double current_mass = initial_mass;
    while (selected.size() < candidate_ids.size()) {
        if (current_mass <= target_mass) {
            auto scan_start = Clock::now();
            current_mass = congestion_mass();
            scan_us += elapsed_us(scan_start);
            if (current_mass <= target_mass) {
                break;
            }
        }

        auto scan_start = Clock::now();
        discard_invalid_top();
        bool has_candidate = !queue.empty();
        scan_us += elapsed_us(scan_start);
        if (!has_candidate) {
            break;
        }

        CandidateScoreEntry entry = queue.top();
        queue.pop();
        QueryId query_id = entry.query_id;
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(result.trajectories.size()) ||
            selected_flags[query_id]) {
            continue;
        }

        rank_start = Clock::now();
        long double refreshed_score =
            route_score(result.trajectories[query_id]);
        rank_us += elapsed_us(rank_start);
        if (refreshed_score <= 0.0L) {
            cached_scores[query_id] = 0.0L;
            continue;
        }

        CandidateScoreEntry refreshed{refreshed_score, query_id};
        cached_scores[query_id] = refreshed_score;

        scan_start = Clock::now();
        discard_invalid_top();
        bool next_outranks =
            !queue.empty() && outranks(queue.top(), refreshed);
        scan_us += elapsed_us(scan_start);
        if (next_outranks) {
            queue.push(refreshed);
            continue;
        }

        selected.insert(query_id);
        selected_flags[query_id] = 1;
        cached_scores[query_id] = 0.0L;

        auto remove_start = Clock::now();
        long double removed_mass =
            decrease_trajectory_flow_only(result.trajectories[query_id]);
        if (removed_mass > 0.0L) {
            current_mass =
                std::max<long double>(0.0L, current_mass - removed_mass);
        }
        remove_us += elapsed_us(remove_start);
    }

    std::vector<QueryId> selected_queries(selected.begin(), selected.end());
    std::sort(selected_queries.begin(), selected_queries.end());
    log_select_summary(
        options_.enable_timing_log,
        iteration,
        static_cast<long long>(candidate_set.size()),
        static_cast<long long>(selected_queries.size()),
        prepare_us,
        rank_us,
        scan_us,
        remove_us,
        elapsed_us(total_start));
    return selected_queries;
}

std::vector<QueryId> GROAlgorithm::select_queries_by_excess_relief(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    std::unordered_set<QueryId> candidate_set;
    for (QueryId query_id = 0;
         query_id < static_cast<QueryId>(queries.size());
         ++query_id) {
        candidate_set.insert(query_id);
    }
    return select_queries_by_excess_relief(
        candidate_set,
        queries,
        result,
        tdg,
        node_impacts,
        iteration);
}

std::vector<QueryId> GROAlgorithm::select_queries_by_bpr_relief(
    const std::unordered_set<QueryId>& candidate_set,
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    (void)queries;
    auto total_start = Clock::now();
    if (candidate_set.empty() || tdg.nodes.empty()) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            0,
            0,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    auto prepare_start = Clock::now();
    std::vector<double> selection_impacts =
        normalize_tdg_impacts_for_selection(node_impacts);

    auto soft_bpr_time = [&](const Edge& edge, Flow flow) -> long double {
        if (edge.capacity <= 0 ||
            (traffic_options_.min_bpr_capacity > 0 &&
             edge.capacity <= traffic_options_.min_bpr_capacity)) {
            return static_cast<long double>(edge.free_flow_time);
        }

        Flow safe_flow = std::max<Flow>(0, flow);
        long double ratio =
            static_cast<long double>(safe_flow) /
            static_cast<long double>(edge.capacity);
        long double penalty =
            static_cast<long double>(edge.free_flow_time) *
            static_cast<long double>(traffic_options_.alpha) /
            100.0L *
            std::pow(ratio, traffic_options_.beta);
        return static_cast<long double>(edge.free_flow_time) + penalty;
    };

    std::vector<long double> node_margins(tdg.nodes.size(), 0.0L);
    std::vector<long double> positive_margins;
    positive_margins.reserve(tdg.nodes.size());
    for (const TDGNode& node : tdg.nodes) {
        if (node.id < 0 ||
            node.id >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
            node.id >= static_cast<TDGNodeId>(selection_impacts.size()) ||
            node.edge_id < 0 ||
            node.edge_id >= static_cast<EdgeId>(graph_.edges.size()) ||
            node.flow <= 0) {
            continue;
        }

        const Edge& edge = graph_.edges[node.edge_id];
        long double current_time = soft_bpr_time(edge, node.flow);
        long double without_one_time = soft_bpr_time(edge, node.flow - 1);
        if (current_time <= without_one_time) {
            continue;
        }

        long double margin = current_time - without_one_time;
        node_margins[node.id] = margin;
        positive_margins.push_back(margin);
    }

    if (positive_margins.empty()) {
        long long prepare_us = elapsed_us(prepare_start);
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            prepare_us,
            0,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    size_t margin_clip_index =
        percentile_index(positive_margins.size(), 99);
    std::nth_element(
        positive_margins.begin(),
        positive_margins.begin() + margin_clip_index,
        positive_margins.end());
    long double margin_clip =
        std::max<long double>(1.0L, positive_margins[margin_clip_index]);
    const long double log_margin_clip = std::log1p(margin_clip);
    std::vector<long double> node_scores(tdg.nodes.size(), 0.0L);
    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        if (node_id >= static_cast<TDGNodeId>(selection_impacts.size()) ||
            node_margins[node_id] <= 0.0L ||
            selection_impacts[node_id] <= 0.0) {
            continue;
        }

        long double clipped_margin = std::min(
            node_margins[node_id],
            margin_clip);
        long double normalized_margin =
            std::log1p(clipped_margin) / log_margin_clip;
        node_scores[node_id] =
            normalized_margin *
            static_cast<long double>(selection_impacts[node_id]);
    }
    long long prepare_us = elapsed_us(prepare_start);

    std::vector<QueryId> candidate_ids(
        candidate_set.begin(),
        candidate_set.end());
    std::sort(candidate_ids.begin(), candidate_ids.end());

    std::vector<int> seen(tdg.nodes.size(), 0);
    int seen_epoch = 0;
    auto route_score = [&](const Trajectory& trajectory) -> long double {
        ++seen_epoch;
        if (seen_epoch == std::numeric_limits<int>::max()) {
            std::fill(seen.begin(), seen.end(), 0);
            seen_epoch = 1;
        }

        long double score = 0.0L;
        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(node_scores.size()) ||
                seen[node_id] == seen_epoch) {
                continue;
            }
            seen[node_id] = seen_epoch;
            score += node_scores[node_id];
        }
        return score;
    };

    auto rank_start = Clock::now();
    std::vector<std::pair<long double, QueryId>> ranking;
    ranking.reserve(candidate_ids.size());
    long double total_positive_score = 0.0L;
    for (QueryId query_id : candidate_ids) {
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(result.trajectories.size())) {
            continue;
        }

        long double score = route_score(result.trajectories[query_id]);
        if (score <= 0.0L) {
            continue;
        }
        ranking.push_back({score, query_id});
        total_positive_score += score;
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
    long long rank_us = elapsed_us(rank_start);

    if (ranking.empty() || total_positive_score <= 0.0L) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            prepare_us,
            rank_us,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    int clamped_gamma = std::clamp(options_.gamma, 0, 100);
    if (clamped_gamma <= 0) {
        log_select_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(candidate_set.size()),
            0,
            prepare_us,
            rank_us,
            0,
            0,
            elapsed_us(total_start));
        return {};
    }

    long double target_score =
        total_positive_score *
        static_cast<long double>(clamped_gamma) /
        100.0L;

    auto scan_start = Clock::now();
    std::vector<QueryId> selected_queries;
    selected_queries.reserve(ranking.size());
    long double covered_score = 0.0L;
    for (const auto& item : ranking) {
        if (covered_score >= target_score && !selected_queries.empty()) {
            break;
        }
        selected_queries.push_back(item.second);
        covered_score += item.first;
    }
    std::sort(selected_queries.begin(), selected_queries.end());
    long long scan_us = elapsed_us(scan_start);

    log_select_summary(
        options_.enable_timing_log,
        iteration,
        static_cast<long long>(candidate_set.size()),
        static_cast<long long>(selected_queries.size()),
        prepare_us,
        rank_us,
        scan_us,
        0,
        elapsed_us(total_start));
    return selected_queries;
}

std::vector<QueryId> GROAlgorithm::select_queries_by_bpr_relief(
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    std::unordered_set<QueryId> candidate_set;
    for (QueryId query_id = 0;
         query_id < static_cast<QueryId>(queries.size());
         ++query_id) {
        candidate_set.insert(query_id);
    }
    return select_queries_by_bpr_relief(
        candidate_set,
        queries,
        result,
        tdg,
        node_impacts,
        iteration);
}

std::vector<std::vector<QueryId>> GROAlgorithm::batch_queries(
    const std::vector<QueryId>& selected_query_ids,
    const TrafficDependencyGraph &tdg,
    const TrafficResult &result,
    int iteration) const {
    (void)iteration;
    if (selected_query_ids.empty()) {
        return {};
    }

    std::vector<Cost> node_ratios(tdg.nodes.size(), 0);
    std::vector<Cost> sorted_ratios;
    sorted_ratios.reserve(tdg.nodes.size());
    for (size_t node_id = 0; node_id < tdg.nodes.size(); ++node_id) {
        const TDGNode& node = tdg.nodes[node_id]; 
        node_ratios[node_id] =
            congestion_ratio_key(node.flow, graph_.edges[node.edge_id].capacity);
        sorted_ratios.push_back(node_ratios[node_id]);
    }

    std::sort(sorted_ratios.begin(), sorted_ratios.end());
    size_t theta_index = percentile_index(sorted_ratios.size(), options_.theta_percentile);
    Cost theta = sorted_ratios[theta_index];

    std::vector<char> important(tdg.nodes.size(), 0);
    for (size_t node_id = 0; node_id < tdg.nodes.size(); ++node_id) {
        important[node_id] = node_ratios[node_id] >= theta;
    }

    std::vector<std::vector<TDGNodeId>> sketches(result.trajectories.size());
    std::vector<int> seen(tdg.nodes.size(), 0);
    int seen_epoch = 0; // We use seen_epoch to avoid clearing the seen array for each query

    for (const QueryId& query_id : selected_query_ids) {
        ++seen_epoch;
        const Trajectory& trajectory = result.trajectories[query_id];
        auto& sketch = sketches[query_id];

        for (TDGNodeId node_id : trajectory.tdg_node_ids) {
            if (important[node_id] && seen[node_id] != seen_epoch) {
                seen[node_id] = seen_epoch;
                sketch.push_back(node_id);
            }
        }
    }

    std::vector<QueryId> query_order;
    query_order.reserve(selected_query_ids.size());
    for (const QueryId& query_id : selected_query_ids) {
        query_order.push_back(query_id);
    }
    std::sort(
        query_order.begin(),
        query_order.end(),
        [&](QueryId lhs, QueryId rhs) {
            if (sketches[lhs].size() != sketches[rhs].size()) {
                return sketches[lhs].size() > sketches[rhs].size();
            }
            return lhs < rhs;
        });

    std::vector<std::vector<QueryId>> batches;
    std::vector<std::unordered_map<TDGNodeId, int>> batch_loads; // stores for each batch, how many queries in that batch already cover each important TDG node 
    std::vector<std::vector<int>> node_to_batches(tdg.nodes.size()); // gives the list of batch ids that contain this node in their sketch
    std::vector<int> batch_seen; // batch_seen[batch_id] is the last candidate_epoch in which batch_id was considered as a candidate for a query
    std::vector<int> candidate_batches;
    int candidate_epoch = 0;

    auto smallest_batch_not_seen = [&]() {
        int best_batch = kInvalidId;
        for (size_t batch_index = 0; batch_index < batches.size(); ++batch_index) {
            int batch_id = static_cast<int>(batch_index);
            if (batch_seen[batch_id] == candidate_epoch) {
                continue; // This batch has already been considered as a candidate for the current query
            }
            if (best_batch == kInvalidId ||
                batches[batch_id].size() < batches[best_batch].size()) {
                best_batch = batch_id;
            }
        }
        return best_batch;
    };

    // conflict_delta calculates how many nodes in the sketch of the current query are already present in the candidate batch
    auto conflict_delta = [&](int batch_id, const std::vector<TDGNodeId>& sketch) {
        int delta = 0; 
        for (TDGNodeId node_id : sketch) {
            auto load_it = batch_loads[batch_id].find(node_id);
            if (load_it != batch_loads[batch_id].end()) {
                delta += load_it->second;
            }
        }
        return delta;
    };

    for (QueryId query_id : query_order) {
        const auto& sketch = sketches[query_id];
        ++candidate_epoch;
        candidate_batches.clear();

        for (TDGNodeId node_id : sketch) {
            for (int batch_id : node_to_batches[node_id]) {
                if (batch_seen[batch_id] != candidate_epoch) {
                    batch_seen[batch_id] = candidate_epoch; // Mark this batch as seen for the current query
                    candidate_batches.push_back(batch_id);
                }
            }
        }

        int best_batch = kInvalidId;
        int best_delta = 0;
        if (!batches.empty()) {
            best_batch = smallest_batch_not_seen();
            if (best_batch == kInvalidId) {
                for (int batch_id : candidate_batches) {
                    int delta = conflict_delta(batch_id, sketch);
                    if (best_batch == kInvalidId || delta < best_delta ||
                        (delta == best_delta &&
                         batches[batch_id].size() < batches[best_batch].size())) {
                        best_batch = batch_id;
                        best_delta = delta;
                    }
                }
            }
        }

        int conflict_limit =
            options_.conflict_threshold * static_cast<int>(sketch.size()) / 100;
        if (best_batch == kInvalidId || best_delta > conflict_limit) {
            best_batch = batches.size();
            batches.emplace_back();
            batch_loads.emplace_back();
            batch_seen.push_back(0); // Initialize batch_seen for the new batch
        }

        batches[best_batch].push_back(query_id);
        for (TDGNodeId node_id : sketch) {
            int& load = batch_loads[best_batch][node_id]; // number of queries in best_batch that cover node_id
            if (load == 0) {
                // This is the first time this node is being added to the batch, so we add the batch to node_to_batches for this node   
                node_to_batches[node_id].push_back(best_batch);
            }
            ++load;
        }
    }

    return batches;
}

Trajectory GROAlgorithm::reroute_query(
    const Query& query,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& impacts) const {
    (void)result;

    struct Predecessor {
        NodeId previous_node = kInvalidId;
        EdgeId edge_id = kInvalidId;
        Time departure_time = 0;
    };

    Trajectory trajectory;
    trajectory.query_id = query.id;

    if (query.origin == query.destination) {
        trajectory.schedule.push_back(query.departure_time);
        trajectory.arrival_time = query.departure_time;
        return trajectory;
    }

    const int vertex_count = graph_.vertex_count;
    const Time infinity_time = std::numeric_limits<Time>::max();
    const Cost infinity_score = std::numeric_limits<Cost>::max() / 4;

    std::vector<Time> arrival(vertex_count, infinity_time);
    std::vector<Cost> scores(vertex_count, infinity_score);
    std::vector<Predecessor> predecessors(vertex_count);
    std::vector<char> settled(vertex_count, 0);
    data_structures::IndexedHeap<4, Cost, NodeId> heap(vertex_count);

    arrival[query.origin] = query.departure_time;
    scores[query.origin] = 0;
    heap.push_or_update(query.origin, 0);

    auto working_edge_flow = [&](EdgeId edge_id, Time time) -> Flow {
        if (edge_id < 0 ||
            edge_id >= static_cast<EdgeId>(tdg.edge_timelines.size())) {
            assert(false);
            return 0;
        }

        const auto& timeline = tdg.edge_timelines[edge_id];
        if (timeline.empty()) {
            return 0;
        }

        auto timeline_it = timeline.upper_bound(time);
        if (timeline_it == timeline.begin()) {
            return 0;
        }
        --timeline_it;
        return timeline_it->second.flow;
    };

    while (!heap.empty()) {
        auto element = heap.extract_min();
        NodeId node_id = element.id;
        if (settled[node_id]) {
            continue;
        }
        settled[node_id] = 1;

        if (node_id == query.destination) {
            break;
        }

        Time time = arrival[node_id];
        for (EdgeId edge_id : graph_.outgoing_edges[node_id]) {
            const Edge& edge = graph_.edges[edge_id];
            if (settled[edge.to]) {
                continue;
            }

            Flow flow = working_edge_flow(edge_id, time);
            Cost travel_time = bpr_travel_time(edge, flow, traffic_options_);
            Time next_time = time + travel_time;

            Cost impact = 0;
            const auto& timeline = tdg.edge_timelines[edge_id];
            for (auto timeline_it = timeline.lower_bound(time);
                 timeline_it != timeline.end() && timeline_it->first <= next_time;
                 ++timeline_it) {
                TDGNodeId tdg_node_id = timeline_it->second.node_id;
                if (tdg_node_id < static_cast<TDGNodeId>(impacts.size())) {
                    impact = std::max(impact, impacts[tdg_node_id]);
                }
            }

            Cost next_score = add_impact_saturated(
                add_impact_saturated(scores[node_id], travel_time),
                scale_impact_percent_saturated(impact, options_.impact_weight));
            if (next_score < scores[edge.to]) {
                arrival[edge.to] = next_time;
                scores[edge.to] = next_score;
                predecessors[edge.to] = Predecessor{node_id, edge_id, time};
                heap.push_or_update(edge.to, next_score);
            }
        }
    }

    if (predecessors[query.destination].edge_id == kInvalidId) {
        return trajectory;
    }

    std::vector<EdgeId> reversed_edges;
    std::vector<Time> reversed_times;
    for (NodeId node_id = query.destination;
         node_id != query.origin;
         node_id = predecessors[node_id].previous_node) {
        const Predecessor& predecessor = predecessors[node_id];
        reversed_edges.push_back(predecessor.edge_id);
        reversed_times.push_back(predecessor.departure_time);
    }

    trajectory.edge_ids.assign(reversed_edges.rbegin(), reversed_edges.rend());
    trajectory.schedule.assign(reversed_times.rbegin(), reversed_times.rend());
    trajectory.schedule.push_back(arrival[query.destination]);
    trajectory.arrival_time = arrival[query.destination];
    trajectory.travel_time = arrival[query.destination] - query.departure_time;
    collect_trajectory_tdg_nodes(tdg, trajectory);
    return trajectory;
}


std::vector<Route> GROAlgorithm::reroute_queries(
    const std::vector<std::vector<QueryId>>& query_batches,
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const TrafficDependencyGraph& tdg,
    const std::vector<Cost>& node_impacts,
    int iteration) const {
    auto total_start = Clock::now();
    std::vector<Route> routes;
    TrafficDependencyGraph working_tdg = tdg;
    std::vector<Cost> working_impacts = node_impacts;
    long long reroute_query_us = 0;
    long long reroute_query_count = 0;
    long long insert_trajectory_us = 0;
    long long recompute_impact_us = 0;

    for (size_t batch_index = 0; batch_index < query_batches.size(); ++batch_index) {
        const auto& batch = query_batches[batch_index];
        std::vector<Trajectory> batch_trajectories(batch.size());

        auto reroute_start = Clock::now();
        #pragma omp parallel for
        for (size_t i = 0; i < batch.size(); ++i) {
            batch_trajectories[i] =
                reroute_query(queries[batch[i]], result, working_tdg, working_impacts);
        }
        reroute_query_us += elapsed_us(reroute_start);
        reroute_query_count += static_cast<long long>(batch.size());

        auto insert_start = Clock::now();
        for (const Trajectory& trajectory : batch_trajectories) {

            Route route;
            route.query_id = trajectory.query_id;
            route.edge_ids = trajectory.edge_ids;
            route.departure_time = trajectory.schedule.front();
            route.travel_time = trajectory.arrival_time - route.departure_time;
            routes.push_back(route);

            insert_trajectory_into_tdg(working_tdg, trajectory, result);
        }
        insert_trajectory_us += elapsed_us(insert_start);

        if (batch_index + 1 < query_batches.size()) {
            auto impact_start = Clock::now();
            std::vector<Cost> raw_impacts = compute_tdg_impact(working_tdg);
            working_impacts =
                normalize_tdg_impacts_for_reroute(working_tdg, raw_impacts);
            recompute_impact_us += elapsed_us(impact_start);
        }
    }
    log_reroute_summary(
        options_.enable_timing_log,
        iteration,
        reroute_query_count,
        reroute_query_us,
        insert_trajectory_us,
        recompute_impact_us,
        elapsed_us(total_start));
    return routes;
}

std::vector<Route> GROAlgorithm::baseline_reroute_queries(
    const std::vector<QueryId>& query_ids,
    const std::vector<Query>& queries,
    const TrafficResult& result) const {
    struct Predecessor {
        NodeId previous_node = kInvalidId;
        EdgeId edge_id = kInvalidId;
        Time departure_time = 0;
    };

    auto reroute_one = [&](const Query& query) {
        Route route;
        route.query_id = query.id;
        route.departure_time = query.departure_time;

        if (query.origin == query.destination) {
            return route;
        }

        const Time infinity_time = std::numeric_limits<Time>::max();
        const Cost infinity_score = std::numeric_limits<Cost>::max() / 4;

        std::vector<Time> arrival(graph_.vertex_count, infinity_time);
        std::vector<Cost> scores(graph_.vertex_count, infinity_score);
        std::vector<Predecessor> predecessors(graph_.vertex_count);
        std::vector<char> settled(graph_.vertex_count, 0);
        data_structures::IndexedHeap<4, Cost, NodeId> heap(graph_.vertex_count);

        arrival[query.origin] = query.departure_time;
        scores[query.origin] = 0;
        heap.push_or_update(query.origin, 0);

        while (!heap.empty()) {
            auto element = heap.extract_min();
            NodeId node_id = element.id;
            if (settled[node_id]) {
                continue;
            }
            settled[node_id] = 1;

            if (node_id == query.destination) {
                break;
            }

            Time time = arrival[node_id];
            for (EdgeId edge_id : graph_.outgoing_edges[node_id]) {
                const Edge& edge = graph_.edges[edge_id];
                if (settled[edge.to]) {
                    continue;
                }

                Flow flow = get_edge_flow(result, edge_id, time);
                Cost travel_time = bpr_travel_time(edge, flow, traffic_options_);
                Cost next_score = scores[node_id] + travel_time;
                Time next_time = time + travel_time;

                if (next_score < scores[edge.to]) {
                    arrival[edge.to] = next_time;
                    scores[edge.to] = next_score;
                    predecessors[edge.to] = Predecessor{node_id, edge_id, time};
                    heap.push_or_update(edge.to, next_score);
                }
            }
        }

        if (predecessors[query.destination].edge_id == kInvalidId) {
            return route;
        }

        for (NodeId node_id = query.destination;
             node_id != query.origin;
             node_id = predecessors[node_id].previous_node) {
            route.edge_ids.push_back(predecessors[node_id].edge_id);
        }
        std::reverse(route.edge_ids.begin(), route.edge_ids.end());
        route.travel_time = arrival[query.destination] - query.departure_time;
        return route;
    };

    std::vector<Route> routes(query_ids.size());

    #pragma omp parallel for 
    for (size_t i = 0; i < query_ids.size(); ++i) {
        routes[i] = reroute_one(queries[query_ids[i]]);
    }

    return routes;
}

AlgorithmResult GROAlgorithm::run(const std::vector<Query>& queries) const {
    auto total_start = Clock::now();
    auto phase_start = Clock::now();
    std::vector<Route> routes = compute_initial_routes(queries);
    log_timing(options_.enable_timing_log, "initial_routes",
               static_cast<long long>(queries.size()), phase_start);

    AlgorithmResult algResult;
    algResult.initial_routes = routes;

    TrafficResult traffic_result;
    for (int i = 0; i < options_.max_iterations; ++i) {
        auto iteration_start = Clock::now();
        phase_start = Clock::now();
        traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
        log_timing(options_.enable_timing_log, i, "evaluate_traffic",
                   static_cast<long long>(routes.size()), phase_start);
        log_metric(options_.enable_timing_log, i, "total_travel_time",
                   static_cast<long long>(routes.size()),
                   traffic_result.total_travel_time);
        algResult.total_travel_time_by_iteration.push_back(
            traffic_result.total_travel_time);
        if (i == 0) {
            algResult.initial_total_travel_time = traffic_result.total_travel_time;
        }

        phase_start = Clock::now();
        TrafficDependencyGraph tdg = build_tdg(traffic_result);
        // TrafficDependencyGraph tdg = compress_tdg(traffic_result);
        log_timing(options_.enable_timing_log, i, "build_tdg",
                   static_cast<long long>(tdg.nodes.size()), phase_start);

        phase_start = Clock::now();
        std::vector<Cost> node_impacts = compute_tdg_impact(tdg);
        log_timing(options_.enable_timing_log, i, "compute_impact",
                   static_cast<long long>(tdg.nodes.size()), phase_start);

        phase_start = Clock::now();
        std::unordered_set<QueryId> candidate_query_ids =
            select_candidates(queries, traffic_result, tdg, node_impacts);
        long long candidate_us = elapsed_us(phase_start);
        log_candidate_summary(
            options_.enable_timing_log,
            i,
            static_cast<long long>(candidate_query_ids.size()),
            candidate_us);

        std::vector<QueryId> selected_query_ids =
            select_queries(candidate_query_ids, queries, traffic_result, tdg, node_impacts, i);
        if (selected_query_ids.empty()) {
            log_timing(options_.enable_timing_log, i, "iteration_total", 0, iteration_start);
            break;
        }

        phase_start = Clock::now();
        std::vector<std::vector<QueryId>> query_batches =
            batch_queries(selected_query_ids, tdg, traffic_result, i);
        long long batch_queries_us = elapsed_us(phase_start);
        log_batch_summary(
            options_.enable_timing_log,
            i,
            static_cast<long long>(selected_query_ids.size()),
            static_cast<long long>(query_batches.size()),
            batch_queries_us);

        phase_start = Clock::now();
        std::vector<Cost> reroute_impacts =
            normalize_tdg_impacts_for_reroute(tdg, node_impacts);
        log_timing(options_.enable_timing_log, i, "normalize_impact",
                   static_cast<long long>(reroute_impacts.size()), phase_start);

        std::vector<Route> new_routes =
            reroute_queries(
                query_batches,
                queries,
                traffic_result,
                tdg,
                reroute_impacts,
                i);
        for (const Route &route : new_routes) {
            routes[route.query_id] = route;
        }
        log_timing(options_.enable_timing_log, i, "iteration_total",
                   static_cast<long long>(selected_query_ids.size()), iteration_start);
    }

    phase_start = Clock::now();
    traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
    log_timing(options_.enable_timing_log, "final_evaluate_traffic",
               static_cast<long long>(routes.size()), phase_start);
    log_metric(options_.enable_timing_log, "final_total_travel_time",
               static_cast<long long>(routes.size()),
               traffic_result.total_travel_time);

    algResult.final_routes = routes;
    algResult.final_total_travel_time = traffic_result.total_travel_time;
    algResult.total_travel_time_by_iteration.push_back(
        traffic_result.total_travel_time);
    log_timing(options_.enable_timing_log, "run_total",
               static_cast<long long>(queries.size()), total_start);
    return algResult;
}


}  // namespace gro
