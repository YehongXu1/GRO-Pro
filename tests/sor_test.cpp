#include "sor.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool reaches_destination(
    const gro::Graph& graph,
    const gro::Query& query,
    const gro::Route& route) {
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
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = argc > 1 ? argv[1] : "config/config.yaml";

    gro::InputConfig input = gro::load_input_config(config_path);
    gro::Graph graph = gro::read_graph(input);
    std::vector<gro::Query> queries = gro::read_queries(input);
    gro::TrafficOptions traffic_options = gro::load_traffic_options(config_path);

    require(!graph.edges.empty(), "graph should contain edges");
    require(!queries.empty(), "queries should not be empty");

    gro::SOROptions options;
    options.detour_percent = 20;
    options.time_step = 1;
    options.max_time_steps = 3600;
    std::vector<gro::Route> routes = gro::compute_sor_routes(graph, queries, options);

    require(routes.size() == queries.size(), "SOR should return one route per query");
    for (std::size_t i = 0; i < routes.size(); ++i) {
        require(routes[i].query_id == queries[i].id, "SOR route query id should match");
        require(routes[i].departure_time == queries[i].departure_time,
                "SOR route departure time should match");
        require(!routes[i].edge_ids.empty(),
                "SOR should find a non-empty route for connected test queries");
        require(reaches_destination(graph, queries[i], routes[i]),
                "SOR route should reach the query destination");
    }

    gro::TrafficResult traffic = gro::evaluate_traffic(graph, queries, routes, traffic_options);
    require(traffic.trajectories.size() == queries.size(),
            "SOR routes should be traffic-evaluable");

    std::cout << "SOR Done!" << std::endl;
    return 0;
}
