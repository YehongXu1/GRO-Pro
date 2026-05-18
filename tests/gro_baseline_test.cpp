#include "gro.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
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

    std::cout << "Graph loaded: " << graph.vertex_count << " vertices, "
              << graph.edge_count << " edges." << std::endl;

    std::vector<gro::Query> queries = gro::read_queries(input);
    gro::AlgorithmOptions algorithm_options = gro::load_algorithm_options(config_path);
    gro::TrafficOptions traffic_options = gro::load_traffic_options(config_path);

    require(!graph.edges.empty(), "graph should contain edges");
    require(!queries.empty(), "queries should not be empty");

    gro::GROAlgorithm algorithm(graph, algorithm_options, traffic_options);
    gro::AlgorithmResult baseline_result = algorithm.run_baseline(queries);
    require(baseline_result.final_routes.size() == queries.size(),
            "baseline should return one route per query");
    require(baseline_result.final_total_travel_time >= 0,
            "baseline final total travel time should be non-negative");

    gro::AlgorithmResult tdg_selection_result =
        algorithm.run_tdg_selection_baseline(queries);
    require(tdg_selection_result.final_routes.size() == queries.size(),
            "tdg_selection_baseline should return one route per query");
    require(tdg_selection_result.final_total_travel_time >= 0,
            "tdg_selection_baseline final total travel time should be non-negative");

    gro::AlgorithmResult tdg_reroute_result =
        algorithm.run_tdg_reroute_baseline(queries);
    require(tdg_reroute_result.final_routes.size() == queries.size(),
            "tdg_reroute_baseline should return one route per query");
    require(tdg_reroute_result.final_total_travel_time >= 0,
            "tdg_reroute_baseline final total travel time should be non-negative");

    std::cout << "GRO Baselines Done!" << std::endl;
    return 0;
}
