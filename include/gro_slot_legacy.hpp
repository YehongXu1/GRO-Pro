#pragma once

#include "core.hpp"

#include <cstddef>
#include <map>
#include <unordered_set>
#include <vector>

namespace gro {

using SlotLegacyKey = std::pair<EdgeId, Time>;

struct SlotLegacyOptions {
    int slot_width = 1;
    int tau_percentile = 90;
    int gamma = 50;
    int lambda = 80;
    int capacity_ignore = 0;
    int reroute_impact_weight = 100;
};

struct SlotLegacyNode {
    TDGNodeId id = kInvalidId;
    EdgeId edge_id = kInvalidId;
    Time slot = 0;
    Time rep_time = 0;
    Flow flow = 0;
    Flow original_flow = 0;
    Cost impact = 0;
    bool active = true;
    std::vector<TDGNodeId> outgoing;
    std::vector<TDGNodeId> incoming;
    std::vector<QueryId> query_ids;
};

struct SlotLegacyTDG {
    std::vector<SlotLegacyNode> nodes;
    std::vector<std::map<Time, TDGNodeId>> edge_slots;
    std::unordered_map<SlotLegacyKey, TDGNodeId, PairHash> key_to_node_id;
};

struct SlotLegacySelectionResult {
    std::vector<QueryId> selected_query_ids;
    std::size_t candidate_count = 0;
    std::size_t source_node_count = 0;
    std::size_t active_node_count = 0;
};

class SlotLegacyGRO {
public:
    SlotLegacyGRO(
        Graph graph,
        SlotLegacyOptions options = {},
        TrafficOptions traffic_options = {});

    SlotLegacyTDG build_tdg(const TrafficResult& result) const;

    void compute_impacts(SlotLegacyTDG& tdg) const;

    std::unordered_set<QueryId> select_candidates(
        const SlotLegacyTDG& tdg,
        std::size_t* source_node_count = nullptr) const;

    SlotLegacySelectionResult select_queries(
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const SlotLegacyTDG& tdg) const;

    Route reroute_query(
        const Query& query,
        const TrafficResult& result,
        const SlotLegacyTDG& tdg) const;

    std::vector<Route> reroute_queries(
        const std::vector<QueryId>& query_ids,
        const std::vector<Query>& queries,
        const TrafficResult& result,
        const SlotLegacyTDG& tdg) const;

private:
    Graph graph_;
    SlotLegacyOptions options_;
    TrafficOptions traffic_options_;

    Time slot_of(Time time) const;
    Cost edge_cost_for_reroute(
        EdgeId edge_id,
        Time time,
        const TrafficResult& result,
        const SlotLegacyTDG& tdg) const;
};

}  // namespace gro
