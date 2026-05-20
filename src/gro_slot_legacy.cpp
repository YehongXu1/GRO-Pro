#include "gro_slot_legacy.hpp"

#include "data_structures.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <stack>
#include <unordered_set>

namespace gro {
namespace {

Cost add_saturated(Cost lhs, Cost rhs) {
    if (rhs <= 0) {
        return lhs;
    }
    if (lhs > std::numeric_limits<Cost>::max() - rhs) {
        return std::numeric_limits<Cost>::max();
    }
    return lhs + rhs;
}

Cost scale_percent(Cost value, int percent) {
    if (value <= 0 || percent <= 0) {
        return 0;
    }
    long double scaled =
        static_cast<long double>(value) *
        static_cast<long double>(percent) /
        100.0L;
    if (scaled >= static_cast<long double>(std::numeric_limits<Cost>::max())) {
        return std::numeric_limits<Cost>::max();
    }
    return static_cast<Cost>(std::llround(scaled));
}

std::size_t percentile_index(std::size_t size, int percentile) {
    if (size == 0) {
        return 0;
    }
    int clamped = std::clamp(percentile, 0, 100);
    return static_cast<std::size_t>(
        static_cast<long double>(clamped) *
        static_cast<long double>(size - 1) /
        100.0L);
}

void add_arc(SlotLegacyTDG& tdg, TDGNodeId from, TDGNodeId to) {
    if (from == kInvalidId || to == kInvalidId || from == to ||
        from < 0 || to < 0 ||
        from >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
        to >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
        !tdg.nodes[from].active || !tdg.nodes[to].active) {
        return;
    }

    auto& outgoing = tdg.nodes[from].outgoing;
    if (std::find(outgoing.begin(), outgoing.end(), to) == outgoing.end()) {
        outgoing.push_back(to);
    }
    auto& incoming = tdg.nodes[to].incoming;
    if (std::find(incoming.begin(), incoming.end(), from) == incoming.end()) {
        incoming.push_back(from);
    }
}

void remove_arc(SlotLegacyTDG& tdg, TDGNodeId from, TDGNodeId to) {
    if (from == kInvalidId || to == kInvalidId ||
        from < 0 || to < 0 ||
        from >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
        to >= static_cast<TDGNodeId>(tdg.nodes.size())) {
        return;
    }

    auto& outgoing = tdg.nodes[from].outgoing;
    outgoing.erase(std::remove(outgoing.begin(), outgoing.end(), to), outgoing.end());
    auto& incoming = tdg.nodes[to].incoming;
    incoming.erase(std::remove(incoming.begin(), incoming.end(), from), incoming.end());
}

std::vector<QueryId> sorted_query_ids(const std::unordered_set<QueryId>& query_ids) {
    std::vector<QueryId> ids(query_ids.begin(), query_ids.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t active_incoming_count(const SlotLegacyTDG& tdg, TDGNodeId node_id) {
    std::size_t count = 0;
    for (TDGNodeId parent_id : tdg.nodes[node_id].incoming) {
        if (parent_id >= 0 &&
            parent_id < static_cast<TDGNodeId>(tdg.nodes.size()) &&
            tdg.nodes[parent_id].active) {
            ++count;
        }
    }
    return count;
}

std::size_t active_node_count(const SlotLegacyTDG& tdg) {
    return static_cast<std::size_t>(
        std::count_if(
            tdg.nodes.begin(),
            tdg.nodes.end(),
            [](const SlotLegacyNode& node) { return node.active; }));
}

}  // namespace

SlotLegacyGRO::SlotLegacyGRO(
    Graph graph,
    SlotLegacyOptions options,
    TrafficOptions traffic_options)
    : graph_(std::move(graph)),
      options_(options),
      traffic_options_(traffic_options) {
    if (options_.slot_width <= 0) {
        options_.slot_width = 1;
    }
    if (options_.capacity_ignore < 0) {
        options_.capacity_ignore = 0;
    }
}

Time SlotLegacyGRO::slot_of(Time time) const {
    if (time <= 0) {
        return 0;
    }
    return time / options_.slot_width;
}

SlotLegacyTDG SlotLegacyGRO::build_tdg(const TrafficResult& result) const {
    SlotLegacyTDG tdg;
    tdg.edge_slots.assign(graph_.edges.size(), {});

    auto ensure_node = [&](EdgeId edge_id, Time slot) -> TDGNodeId {
        SlotLegacyKey key{edge_id, slot};
        auto found = tdg.key_to_node_id.find(key);
        if (found != tdg.key_to_node_id.end()) {
            return found->second;
        }

        SlotLegacyNode node;
        node.id = static_cast<TDGNodeId>(tdg.nodes.size());
        node.edge_id = edge_id;
        node.slot = slot;
        node.rep_time = slot * options_.slot_width;

        tdg.nodes.push_back(std::move(node));
        tdg.key_to_node_id[key] = tdg.nodes.back().id;
        tdg.edge_slots[edge_id][slot] = tdg.nodes.back().id;
        return tdg.nodes.back().id;
    };

    for (EdgeId edge_id = 0;
         edge_id < static_cast<EdgeId>(result.edge_profiles.size());
         ++edge_id) {
        for (const Event& event : result.edge_profiles[edge_id]) {
            Time slot = slot_of(event.time);
            TDGNodeId node_id = ensure_node(edge_id, slot);
            SlotLegacyNode& node = tdg.nodes[node_id];
            if (event.flow >= node.flow) {
                node.flow = event.flow;
                node.original_flow = event.flow;
                node.rep_time = event.time;
                node.query_ids = sorted_query_ids(event.query_ids);
            }
        }
    }

    for (EdgeId edge_id = 0;
         edge_id < static_cast<EdgeId>(tdg.edge_slots.size());
         ++edge_id) {
        TDGNodeId previous_id = kInvalidId;
        for (const auto& [slot, node_id] : tdg.edge_slots[edge_id]) {
            (void)slot;
            if (previous_id != kInvalidId && tdg.nodes[previous_id].flow > 0) {
                const SlotLegacyNode& previous = tdg.nodes[previous_id];
                const SlotLegacyNode& current = tdg.nodes[node_id];
                Cost previous_time =
                    bpr_travel_time(
                        graph_.edges[edge_id],
                        previous.flow,
                        traffic_options_);
                if (previous.rep_time + previous_time >= current.rep_time) {
                    add_arc(tdg, previous_id, node_id);
                }
            }
            previous_id = node_id;
        }
    }

    for (const Trajectory& trajectory : result.trajectories) {
        if (trajectory.edge_ids.size() < 2 ||
            trajectory.schedule.size() < trajectory.edge_ids.size()) {
            continue;
        }
        for (std::size_t index = 0; index + 1 < trajectory.edge_ids.size(); ++index) {
            SlotLegacyKey from_key{
                trajectory.edge_ids[index],
                slot_of(trajectory.schedule[index])};
            SlotLegacyKey to_key{
                trajectory.edge_ids[index + 1],
                slot_of(trajectory.schedule[index + 1])};

            auto from_it = tdg.key_to_node_id.find(from_key);
            auto to_it = tdg.key_to_node_id.find(to_key);
            if (from_it != tdg.key_to_node_id.end() &&
                to_it != tdg.key_to_node_id.end()) {
                add_arc(tdg, from_it->second, to_it->second);
            }
        }
    }

    return tdg;
}

void SlotLegacyGRO::compute_impacts(SlotLegacyTDG& tdg) const {
    std::vector<int> remaining_children(tdg.nodes.size(), 0);
    std::deque<TDGNodeId> ready;

    for (TDGNodeId node_id = 0;
         node_id < static_cast<TDGNodeId>(tdg.nodes.size());
         ++node_id) {
        SlotLegacyNode& node = tdg.nodes[node_id];
        if (!node.active) {
            node.impact = 0;
            continue;
        }

        node.impact =
            bpr_travel_time(
                graph_.edges[node.edge_id],
                node.flow,
                traffic_options_);

        int child_count = 0;
        for (TDGNodeId child_id : node.outgoing) {
            if (child_id >= 0 &&
                child_id < static_cast<TDGNodeId>(tdg.nodes.size()) &&
                tdg.nodes[child_id].active) {
                ++child_count;
            }
        }
        remaining_children[node_id] = child_count;
        if (child_count == 0) {
            ready.push_back(node_id);
        }
    }

    while (!ready.empty()) {
        TDGNodeId node_id = ready.front();
        ready.pop_front();
        const SlotLegacyNode& node = tdg.nodes[node_id];
        if (!node.active) {
            continue;
        }

        std::size_t parent_count = active_incoming_count(tdg, node_id);
        for (TDGNodeId parent_id : node.incoming) {
            if (parent_id < 0 ||
                parent_id >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
                !tdg.nodes[parent_id].active) {
                continue;
            }

            Cost propagated = parent_count == 0
                ? 0
                : scale_percent(node.impact / static_cast<Cost>(parent_count),
                                options_.lambda);
            tdg.nodes[parent_id].impact =
                add_saturated(tdg.nodes[parent_id].impact, propagated);
            --remaining_children[parent_id];
            if (remaining_children[parent_id] == 0) {
                ready.push_back(parent_id);
            }
        }
    }
}

std::unordered_set<QueryId> SlotLegacyGRO::select_candidates(
    const SlotLegacyTDG& tdg,
    std::size_t* source_node_count) const {
    if (source_node_count) {
        *source_node_count = 0;
    }
    std::vector<long double> ratios;
    ratios.reserve(tdg.nodes.size());
    for (const SlotLegacyNode& node : tdg.nodes) {
        if (!node.active) {
            continue;
        }
        Flow capacity = graph_.edges[node.edge_id].capacity;
        if (capacity > options_.capacity_ignore) {
            ratios.push_back(
                static_cast<long double>(node.flow) /
                static_cast<long double>(capacity));
        }
    }

    if (ratios.empty()) {
        return {};
    }

    std::sort(ratios.begin(), ratios.end());
    long double theta =
        std::max<long double>(
            1.0L,
            ratios[percentile_index(ratios.size(), options_.tau_percentile)]);

    std::vector<char> targeted(tdg.nodes.size(), 0);
    for (const SlotLegacyNode& node : tdg.nodes) {
        if (!node.active) {
            continue;
        }
        Flow capacity = graph_.edges[node.edge_id].capacity;
        targeted[node.id] =
            capacity > options_.capacity_ignore &&
            static_cast<long double>(node.flow) >
                theta * static_cast<long double>(capacity);
    }

    std::vector<int> indegree(tdg.nodes.size(), 0);
    std::deque<TDGNodeId> ready;
    for (const SlotLegacyNode& node : tdg.nodes) {
        if (!node.active) {
            continue;
        }
        for (TDGNodeId child_id : node.outgoing) {
            if (child_id >= 0 &&
                child_id < static_cast<TDGNodeId>(tdg.nodes.size()) &&
                tdg.nodes[child_id].active) {
                ++indegree[child_id];
            }
        }
    }
    for (const SlotLegacyNode& node : tdg.nodes) {
        if (node.active && indegree[node.id] == 0) {
            ready.push_back(node.id);
        }
    }

    std::vector<char> has_targeted_ancestor(tdg.nodes.size(), 0);
    std::vector<TDGNodeId> root_targets;
    std::size_t processed = 0;
    while (!ready.empty()) {
        TDGNodeId node_id = ready.front();
        ready.pop_front();
        ++processed;

        if (targeted[node_id] && !has_targeted_ancestor[node_id]) {
            root_targets.push_back(node_id);
        }

        bool child_has_targeted =
            has_targeted_ancestor[node_id] || targeted[node_id];
        for (TDGNodeId child_id : tdg.nodes[node_id].outgoing) {
            if (child_id < 0 ||
                child_id >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
                !tdg.nodes[child_id].active) {
                continue;
            }
            has_targeted_ancestor[child_id] =
                has_targeted_ancestor[child_id] || child_has_targeted;
            if (--indegree[child_id] == 0) {
                ready.push_back(child_id);
            }
        }
    }

    if (processed < active_node_count(tdg)) {
        for (const SlotLegacyNode& node : tdg.nodes) {
            if (node.active && targeted[node.id]) {
                root_targets.push_back(node.id);
            }
        }
    }

    std::unordered_set<TDGNodeId> source_nodes;
    std::vector<TDGNodeId> stack;
    std::unordered_set<TDGNodeId> visited_source_search;
    for (TDGNodeId root_id : root_targets) {
        stack.clear();
        stack.push_back(root_id);
        while (!stack.empty()) {
            TDGNodeId node_id = stack.back();
            stack.pop_back();
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(tdg.nodes.size()) ||
                !tdg.nodes[node_id].active) {
                continue;
            }
            if (!visited_source_search.insert(node_id).second) {
                continue;
            }

            const SlotLegacyNode& node = tdg.nodes[node_id];
            Flow capacity = graph_.edges[node.edge_id].capacity;
            if (capacity > options_.capacity_ignore &&
                node.flow > capacity) {
                source_nodes.insert(node_id);
            }
            for (TDGNodeId parent_id : node.incoming) {
                stack.push_back(parent_id);
            }
        }
    }

    std::unordered_set<QueryId> candidates;
    for (TDGNodeId node_id : source_nodes) {
        for (QueryId query_id : tdg.nodes[node_id].query_ids) {
            candidates.insert(query_id);
        }
    }

    if (source_node_count) {
        *source_node_count = source_nodes.size();
    }
    return candidates;
}

SlotLegacySelectionResult SlotLegacyGRO::select_queries(
    const std::vector<Query>&,
    const TrafficResult& result,
    const SlotLegacyTDG& tdg) const {
    SlotLegacyTDG working_tdg = tdg;

    SlotLegacySelectionResult output;
    output.active_node_count = active_node_count(working_tdg);
    std::unordered_set<QueryId> candidate_set =
        select_candidates(working_tdg, &output.source_node_count);
    output.candidate_count = candidate_set.size();
    if (candidate_set.empty()) {
        return output;
    }

    std::vector<QueryId> candidates(candidate_set.begin(), candidate_set.end());
    std::sort(candidates.begin(), candidates.end());
    std::unordered_set<QueryId> selected;

    auto entry_score = [&](const Trajectory& trajectory) -> long double {
        long double score = 0.0L;
        std::size_t count = 0;
        for (std::size_t index = 0; index < trajectory.edge_ids.size(); ++index) {
            SlotLegacyKey key{trajectory.edge_ids[index], slot_of(trajectory.schedule[index])};
            auto found = working_tdg.key_to_node_id.find(key);
            if (found == working_tdg.key_to_node_id.end()) {
                continue;
            }
            TDGNodeId node_id = found->second;
            if (node_id < 0 ||
                node_id >= static_cast<TDGNodeId>(working_tdg.nodes.size()) ||
                !working_tdg.nodes[node_id].active) {
                continue;
            }
            score += static_cast<long double>(working_tdg.nodes[node_id].impact);
            ++count;
        }
        return count == 0 ? 0.0L : score / static_cast<long double>(count);
    };

    auto affected_nodes = [&](const Trajectory& trajectory) {
        std::vector<TDGNodeId> ids;
        std::unordered_set<TDGNodeId> seen;
        if (trajectory.schedule.size() < trajectory.edge_ids.size() + 1) {
            return ids;
        }

        for (std::size_t index = 0; index < trajectory.edge_ids.size(); ++index) {
            Time first_slot = slot_of(trajectory.schedule[index]);
            Time last_slot = slot_of(trajectory.schedule[index + 1]);
            EdgeId edge_id = trajectory.edge_ids[index];
            if (edge_id < 0 ||
                edge_id >= static_cast<EdgeId>(working_tdg.edge_slots.size())) {
                continue;
            }
            const auto& slots = working_tdg.edge_slots[edge_id];
            for (auto slot_it = slots.lower_bound(first_slot);
                 slot_it != slots.end() && slot_it->first <= last_slot;
                 ++slot_it) {
                TDGNodeId node_id = slot_it->second;
                if (node_id >= 0 &&
                    node_id < static_cast<TDGNodeId>(working_tdg.nodes.size()) &&
                    working_tdg.nodes[node_id].active &&
                    seen.insert(node_id).second) {
                    ids.push_back(node_id);
                }
            }
        }
        return ids;
    };

    auto update_topology = [&](const std::vector<TDGNodeId>& changed_nodes) {
        std::vector<TDGNodeId> zero_nodes;
        for (TDGNodeId node_id : changed_nodes) {
            if (node_id >= 0 &&
                node_id < static_cast<TDGNodeId>(working_tdg.nodes.size()) &&
                working_tdg.nodes[node_id].active &&
                working_tdg.nodes[node_id].flow == 0) {
                zero_nodes.push_back(node_id);
            }
        }

        for (TDGNodeId node_id : zero_nodes) {
            SlotLegacyNode& node = working_tdg.nodes[node_id];
            if (!node.active) {
                continue;
            }

            TDGNodeId previous_id = kInvalidId;
            TDGNodeId next_id = kInvalidId;
            auto& slots = working_tdg.edge_slots[node.edge_id];
            auto current_it = slots.find(node.slot);
            if (current_it != slots.end()) {
                if (current_it != slots.begin()) {
                    auto previous_it = std::prev(current_it);
                    previous_id = previous_it->second;
                }
                auto next_it = std::next(current_it);
                if (next_it != slots.end()) {
                    next_id = next_it->second;
                }
            }

            std::vector<TDGNodeId> parents = node.incoming;
            std::vector<TDGNodeId> children = node.outgoing;
            for (TDGNodeId parent_id : parents) {
                remove_arc(working_tdg, parent_id, node_id);
            }
            for (TDGNodeId child_id : children) {
                remove_arc(working_tdg, node_id, child_id);
            }

            if (previous_id != kInvalidId &&
                next_id != kInvalidId &&
                previous_id != node_id &&
                next_id != node_id &&
                working_tdg.nodes[previous_id].active &&
                working_tdg.nodes[next_id].active) {
                const SlotLegacyNode& previous = working_tdg.nodes[previous_id];
                const SlotLegacyNode& next = working_tdg.nodes[next_id];
                Cost previous_time =
                    bpr_travel_time(
                        graph_.edges[previous.edge_id],
                        previous.flow,
                        traffic_options_);
                if (previous.rep_time + previous_time >= next.rep_time) {
                    add_arc(working_tdg, previous_id, next_id);
                }
            }

            slots.erase(node.slot);
            working_tdg.key_to_node_id.erase(SlotLegacyKey{node.edge_id, node.slot});
            node.active = false;
            node.incoming.clear();
            node.outgoing.clear();
        }
    };

    auto try_select = [&](const Trajectory& trajectory) {
        std::vector<TDGNodeId> affected = affected_nodes(trajectory);
        if (affected.empty()) {
            return false;
        }

        for (TDGNodeId node_id : affected) {
            const SlotLegacyNode& node = working_tdg.nodes[node_id];
            int min_flow =
                (100 - std::clamp(options_.gamma, 0, 100)) *
                node.original_flow /
                100;
            if (node.flow <= 0 || node.flow - 1 < min_flow) {
                return false;
            }
        }

        for (TDGNodeId node_id : affected) {
            --working_tdg.nodes[node_id].flow;
        }
        update_topology(affected);
        return true;
    };

    while (selected.size() < candidates.size()) {
        compute_impacts(working_tdg);

        std::vector<std::pair<long double, QueryId>> ranking;
        ranking.reserve(candidates.size() - selected.size());
        for (QueryId query_id : candidates) {
            if (selected.find(query_id) != selected.end() ||
                query_id < 0 ||
                query_id >= static_cast<QueryId>(result.trajectories.size())) {
                continue;
            }
            long double score = entry_score(result.trajectories[query_id]);
            if (score > 0.0L) {
                ranking.push_back({score, query_id});
            }
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

        std::size_t miss = 0;
        std::size_t selected_this_round = 0;
        std::size_t stale_threshold =
            static_cast<std::size_t>(
                std::sqrt(static_cast<long double>(ranking.size())));
        for (const auto& [score, query_id] : ranking) {
            (void)score;
            if (try_select(result.trajectories[query_id])) {
                selected.insert(query_id);
                ++selected_this_round;
                miss = 0;
            } else {
                ++miss;
                if (miss > stale_threshold) {
                    break;
                }
            }
        }

        if (selected_this_round == 0) {
            break;
        }
    }

    output.selected_query_ids.assign(selected.begin(), selected.end());
    std::sort(output.selected_query_ids.begin(), output.selected_query_ids.end());
    output.active_node_count = active_node_count(working_tdg);
    return output;
}

Cost SlotLegacyGRO::edge_cost_for_reroute(
    EdgeId edge_id,
    Time time,
    const TrafficResult& result,
    const SlotLegacyTDG& tdg) const {
    if (edge_id < 0 || edge_id >= static_cast<EdgeId>(graph_.edges.size())) {
        return std::numeric_limits<Cost>::max() / 4;
    }

    Flow flow = get_edge_flow(result, edge_id, time);
    Cost base =
        bpr_travel_time(graph_.edges[edge_id], flow, traffic_options_);
    if (base <= 0) {
        base = graph_.edges[edge_id].free_flow_time;
    }

    Cost impact = 0;
    Time first_slot = slot_of(time);
    Time last_slot = slot_of(time + std::max<Cost>(base, 1) - 1);
    const auto& slots = tdg.edge_slots[edge_id];
    for (auto slot_it = slots.lower_bound(first_slot);
         slot_it != slots.end() && slot_it->first <= last_slot;
         ++slot_it) {
        TDGNodeId node_id = slot_it->second;
        if (node_id >= 0 &&
            node_id < static_cast<TDGNodeId>(tdg.nodes.size()) &&
            tdg.nodes[node_id].active) {
            impact = add_saturated(impact, tdg.nodes[node_id].impact);
        }
    }

    Cost weighted_impact = scale_percent(impact, options_.reroute_impact_weight);
    return add_saturated(base, weighted_impact);
}

Route SlotLegacyGRO::reroute_query(
    const Query& query,
    const TrafficResult& result,
    const SlotLegacyTDG& tdg) const {
    Route route;
    route.query_id = query.id;
    route.departure_time = query.departure_time;

    if (query.origin == query.destination ||
        query.origin < 0 ||
        query.origin >= static_cast<NodeId>(graph_.outgoing_edges.size()) ||
        query.destination < 0 ||
        query.destination >= static_cast<NodeId>(graph_.outgoing_edges.size())) {
        return route;
    }

    std::vector<Cost> distance(graph_.outgoing_edges.size(), std::numeric_limits<Cost>::max());
    std::vector<EdgeId> parent_edge(graph_.outgoing_edges.size(), kInvalidId);
    data_structures::IndexedHeap<4, Cost, NodeId> queue(graph_.outgoing_edges.size());

    distance[query.origin] = query.departure_time;
    queue.push_or_update(query.origin, query.departure_time);

    while (!queue.empty()) {
        auto current = queue.extract_min();
        NodeId node_id = current.id;
        Cost current_time = current.key;
        if (current_time != distance[node_id]) {
            continue;
        }
        if (node_id == query.destination) {
            break;
        }

        for (EdgeId edge_id : graph_.outgoing_edges[node_id]) {
            const Edge& edge = graph_.edges[edge_id];
            Cost travel_time =
                edge_cost_for_reroute(edge_id, current_time, result, tdg);
            if (travel_time >= std::numeric_limits<Cost>::max() / 8) {
                continue;
            }
            Cost candidate = add_saturated(current_time, travel_time);
            if (candidate < distance[edge.to]) {
                distance[edge.to] = candidate;
                parent_edge[edge.to] = edge_id;
                queue.push_or_update(edge.to, candidate);
            }
        }
    }

    if (distance[query.destination] == std::numeric_limits<Cost>::max()) {
        return route;
    }

    std::vector<EdgeId> reversed_edges;
    NodeId node_id = query.destination;
    while (node_id != query.origin) {
        EdgeId edge_id = parent_edge[node_id];
        if (edge_id == kInvalidId) {
            route.edge_ids.clear();
            return route;
        }
        reversed_edges.push_back(edge_id);
        node_id = graph_.edges[edge_id].from;
    }

    route.edge_ids.assign(reversed_edges.rbegin(), reversed_edges.rend());
    route.travel_time = distance[query.destination] - query.departure_time;
    return route;
}

std::vector<Route> SlotLegacyGRO::reroute_queries(
    const std::vector<QueryId>& query_ids,
    const std::vector<Query>& queries,
    const TrafficResult& result,
    const SlotLegacyTDG& tdg) const {
    SlotLegacyTDG working_tdg = tdg;
    compute_impacts(working_tdg);

    std::vector<Route> routes;
    routes.reserve(query_ids.size());
    for (QueryId query_id : query_ids) {
        if (query_id < 0 ||
            query_id >= static_cast<QueryId>(queries.size())) {
            continue;
        }
        Route route = reroute_query(queries[query_id], result, working_tdg);
        if (!route.edge_ids.empty()) {
            routes.push_back(std::move(route));
        }
    }
    return routes;
}

}  // namespace gro
