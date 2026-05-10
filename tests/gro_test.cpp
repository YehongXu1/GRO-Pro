#include "gro.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace


int main(int argc, char** argv) {

    const std::string config_path = argc > 1 ? argv[1] : "config/config.yaml";

    gro::InputConfig input = gro::load_input_config(config_path);
    gro::Graph graph = gro::read_graph(input);
    std::vector<gro::Query> queries = gro::read_queries(input);
    gro::AlgorithmOptions algorithm_options = gro::load_algorithm_options(config_path);
    gro::TrafficOptions traffic_options = gro::load_traffic_options(config_path);

    require(!graph.edges.empty(), "graph should contain edges");
    require(!queries.empty(), "queries should not be empty");

    gro::GROAlgorithm algorithm(graph, algorithm_options, traffic_options);
    std::vector<gro::Route> routes = algorithm.compute_initial_routes(queries);
    gro::TrafficResult traffic = gro::evaluate_traffic(
        graph, queries, routes, traffic_options);
    gro::TrafficDependencyGraph compressed_tdg = algorithm.compress_tdg(traffic);
    require(!compressed_tdg.nodes.empty(), "compressed TDG should contain route anchors");
    for (const gro::Trajectory& trajectory : traffic.trajectories) {
        if (!trajectory.edge_ids.empty()) {
            require(!trajectory.tdg_node_ids.empty(), "trajectory should store covered compressed TDG nodes");
        }
        for (gro::TDGNodeId node_id : trajectory.tdg_node_ids) {
            require(node_id >= 0 && node_id < static_cast<gro::TDGNodeId>(compressed_tdg.nodes.size()),
                    "trajectory TDG node id should be valid");
        }
    }
    for (const gro::TDGNode& node : compressed_tdg.nodes) {
        bool is_entry_event = false;
        for (const gro::Event& event : traffic.edge_profiles[node.edge_id]) {
            if (event.type && event.time == node.time) {
                is_entry_event = true;
                break;
            }
        }
        require(is_entry_event, "compressed TDG nodes should be entry edge-times");
    }

    gro::AlgorithmResult baseline_result = algorithm.run_baseline_gro(queries);
    require(baseline_result.final_routes.size() == queries.size(),
            "baseline should return one route per query");

    std::cout << "Done!" << std::endl;
    return 0;
}
