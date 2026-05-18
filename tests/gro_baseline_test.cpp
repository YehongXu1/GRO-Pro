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
    gro::AlgorithmResult random_result = algorithm.run_baseline_gro(queries);
    require(random_result.final_routes.size() == queries.size(),
            "GRO baseline should return one route per query");
    require(random_result.final_total_travel_time >= 0,
            "GRO baseline final total travel time should be non-negative");

    gro::AlgorithmResult selection_td_result =
        algorithm.run_selection_td_baseline(queries);
    require(selection_td_result.final_routes.size() == queries.size(),
            "GRO selection TD baseline should return one route per query");
    require(selection_td_result.final_total_travel_time >= 0,
            "GRO selection TD baseline final total travel time should be non-negative");

    gro::AlgorithmResult normal_selection_gro_result =
        algorithm.run_normal_selection_gro_reroute_baseline(queries);
    require(normal_selection_gro_result.final_routes.size() == queries.size(),
            "GRO normal selection + GRO reroute baseline should return one route per query");
    require(normal_selection_gro_result.final_total_travel_time >= 0,
            "GRO normal selection + GRO reroute baseline final total travel time should be non-negative");

    std::cout << "GRO Baselines Done!" << std::endl;
    return 0;
}
