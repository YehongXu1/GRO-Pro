#pragma once

#include "core.hpp"
#include "data_structures.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <set>
#include <map>


namespace gro {

using Key = std::pair<EdgeId, Time>;

struct TDGNode {
    TDGNodeId id = kInvalidId;
    EdgeId edge_id = kInvalidId;
    Time time = 0;
    Flow flow = 0; 
    Flow original_flow = 0; // from the traffic result
};


struct TimeLineEvent {
    Time time;
    TDGNodeId node_id;
    Flow flow;
    TDGNodeId same_edge_parent = kInvalidId;
    TDGNodeId same_edge_child = kInvalidId;
};

struct TrafficDependencyGraph {
    std::vector<TDGNode> nodes;

    std::vector<std::vector<TDGNodeId>> route_outgoing;
    std::vector<std::vector<TDGNodeId>> route_incoming;

    std::vector<std::map<Time, TimeLineEvent>> edge_timelines;
    std::unordered_map<Key, TDGNodeId, PairHash> key_to_node_id;
};


struct AlgorithmOptions {
    int max_iterations = 5;
    int lambda = 45; // in percent
    int gamma = 50; // in percent
    int candidate_theta = 80; // in percent
    int impact_weight = 15; // in percent
    // Congestion gate for the TDG-impact reroute penalty: scales the penalty by a
    // factor ramping 0 -> 1 as edge load (flow/capacity) rises from this percent
    // up to 100%. Below the threshold the reroute reduces to plain travel-time
    // Dijkstra (no spurious detours where there is no congestion); at/above
    // capacity the original penalty is preserved. <0 or >=100 disables the gate.
    int reroute_congestion_gate = 50; // in percent of capacity
    int theta_percentile = 90; // in percent
    int delta_min = 600; // in seconds
    int delta_initial = 1200; // in seconds
    int eta = 60;// in percent
    int conflict_threshold = 70; // in percent
    int delta_compress = 1200; // in seconds
    int anchor_window = 1200; // in seconds
    int anchor_threshold = 20; // flow deviation, in percent of capacity
    int baseline_fraction_to_reroute = 30; // in percent
    unsigned int baseline_random_seed = 0;
    bool enable_timing_log = false;
};

AlgorithmOptions load_algorithm_options(
    const std::string& path,
    AlgorithmOptions defaults = {});


struct AlgorithmResult {
    std::vector<Route> initial_routes;
    std::vector<Route> final_routes;
    std::vector<Cost> total_travel_time_by_iteration;
    Cost initial_total_travel_time = 0;
    Cost final_total_travel_time = 0;
};

class GROAlgorithm {
public:
    GROAlgorithm(
        Graph graph,
        AlgorithmOptions options = {},
        TrafficOptions traffic_options = {});

    GROAlgorithm() = default;

    const Graph& graph() const;

    Route shortest_path(
        const Query& query,
        const std::vector<Cost>* edge_penalties = nullptr) const;

    std::vector<Route> compute_initial_routes(
        const std::vector<Query>& queries) const;

    TrafficDependencyGraph build_tdg(
        TrafficResult& result) const;

    TrafficDependencyGraph compress_tdg(
        TrafficResult& result) const;

    std::vector<std::map<Time, Cost>> compute_anchor_scores(
        const TrafficResult& result) const;

    std::vector<char> mark_anchor_tdg_nodes(
        const TrafficDependencyGraph& tdg,
        const std::vector<std::map<Time, Cost>>& anchor_scores) const;

    std::vector<Cost> compute_tdg_impact(
        const TrafficDependencyGraph& tdg) const;

    std::vector<Cost> normalize_tdg_impacts_for_reroute(
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& raw_impacts) const;

    std::vector<double> normalize_tdg_impacts_for_selection(
        const std::vector<Cost>& raw_impacts) const;

    void remove_trajectory_from_tdg(
        TrafficDependencyGraph& tdg,
        const Trajectory& trajectory,
        const TrafficResult& result) const;

    void insert_trajectory_into_tdg(
        TrafficDependencyGraph& tdg,
        const Trajectory& trajectory,
        const TrafficResult& result) const;

    std::unordered_set<QueryId> select_candidates(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts) const;

    std::unordered_set<QueryId> select_candidates_by_score(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts) const;

    std::unordered_set<QueryId> select_candidates_by_component_balance(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts) const;

    std::unordered_set<QueryId> select_candidates_by_component_marginal(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts) const;

    std::unordered_set<QueryId> select_candidates_by_component_marginal(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        std::size_t max_candidate_count) const;

    std::unordered_set<QueryId> select_candidates_by_component_marginal(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        std::size_t max_candidate_count,
        int component_mass_percent) const;

    std::vector<QueryId> select_queries(
        const std::unordered_set<QueryId>& candidate_query_ids,
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<QueryId> select_queries(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<QueryId> select_queries_by_excess_relief(
        const std::unordered_set<QueryId>& candidate_query_ids,
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<QueryId> select_queries_by_excess_relief(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<QueryId> select_queries_by_bpr_relief(
        const std::unordered_set<QueryId>& candidate_query_ids,
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<QueryId> select_queries_by_bpr_relief(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<std::vector<QueryId>> batch_queries(
        const std::vector<QueryId>& selected_query_ids,
        const TrafficDependencyGraph& tdg,
        const TrafficResult& result,
        int iteration = -1) const;

    Trajectory reroute_query(
        const Query& query,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts) const;

    std::vector<Route> reroute_queries(
        const std::vector<std::vector<QueryId>>& query_batches,
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const TrafficDependencyGraph& tdg,
        const std::vector<Cost>& node_impacts,
        int iteration = -1) const;

    std::vector<Route> baseline_reroute_queries(
        const std::vector<QueryId>& select_query_Ids,
        const std::vector<Query>& queries, 
        const TrafficResult& result) const;

    AlgorithmResult run(const std::vector<Query>& queries) const;

    AlgorithmResult run_baseline(const std::vector<Query>& queries) const;

    AlgorithmResult run_tdg_selection_baseline(
        const std::vector<Query>& queries) const;

    AlgorithmResult run_tdg_reroute_baseline(
        const std::vector<Query>& queries) const;

    // Critical-path timings for the parallel hot spots, captured during the
    // most recent call. Used for infinite-thread runtime estimates (each
    // parallel region's wall under infinite threads ≈ its slowest single unit).
    //
    // anchor_score_max_us: slowest per-edge time across compute_anchor_scores.
    // reroute_critical_us: sum over batches of (slowest per-query reroute time
    //   in that batch) — i.e. the reroute wall under infinite threads.
    long long last_anchor_score_max_us() const { return last_anchor_score_max_us_; }
    long long last_reroute_critical_us() const { return last_reroute_critical_us_; }

private:
    Graph graph_;
    AlgorithmOptions options_;
    TrafficOptions traffic_options_;
    mutable long long last_anchor_score_max_us_ = 0;
    mutable long long last_reroute_critical_us_ = 0;
};

}  // namespace gro
