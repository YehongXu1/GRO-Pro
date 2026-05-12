#pragma once

#include "core.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace gro {

using FAHLFlowProfile = std::unordered_map<std::pair<EdgeId, int>, Flow, PairHash>;

struct FAHLOptions {
    int alpha_percent = 50;
    Time time_step = 60;
    int order_flow_weight = 1;
};

FAHLFlowProfile build_fahl_flow_profile(
    const Graph& graph,
    const std::vector<Route>& routes,
    Time time_step);

FAHLFlowProfile build_fahl_flow_profile(
    const Graph& graph,
    const std::vector<Trajectory>& trajectories,
    Time time_step);

class FAHLIndex {
public:
    FAHLIndex(
        const Graph& graph,
        const FAHLFlowProfile& flow_profile,
        int time_bucket,
        FAHLOptions options = {});

    Route query(const Query& query) const;

    std::vector<Route> query(const std::vector<Query>& queries) const;

    const std::vector<NodeId>& contraction_order() const {
        return contraction_order_;
    }

private:
    struct Impl;

    std::vector<NodeId> contraction_order_;
    std::shared_ptr<Impl> impl_;
};

std::vector<Route> compute_fahl_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const FAHLFlowProfile& flow_profile,
    FAHLOptions options = {});

std::vector<Route> compute_fahl_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const std::vector<Route>& reference_routes,
    FAHLOptions options = {});

}  // namespace gro
