#include "fahl.hpp"

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

bool follows_graph(const gro::Graph& graph, gro::NodeId origin, const gro::Route& route) {
    gro::NodeId current = origin;
    for (gro::EdgeId edge_id : route.edge_ids) {
        if (edge_id < 0 || edge_id >= static_cast<gro::EdgeId>(graph.edges.size())) {
            return false;
        }
        const gro::Edge& edge = graph.edges[edge_id];
        if (edge.from != current) {
            return false;
        }
        current = edge.to;
    }
    return true;
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

    std::vector<gro::Route> reference_routes;
    reference_routes.reserve(queries.size());
    for (const gro::Query& query : queries) {
        reference_routes.push_back(gro::shortest_path(graph, query));
    }

    gro::FAHLOptions options;
    options.alpha_percent = 50;
    options.time_step = 1;

    gro::FAHLFlowProfile profile =
        gro::build_fahl_flow_profile(graph, reference_routes, options.time_step);
    require(!profile.empty(), "FAHL flow profile should contain occupied edge buckets");

    gro::FAHLIndex index(graph, profile, 0, options);
    require(index.contraction_order().size() == static_cast<std::size_t>(graph.vertex_count),
            "FAHL index should assign one contraction order position per vertex");

    gro::Route route = index.query(queries.front());
    require(route.query_id == queries.front().id, "FAHL route query id should match");
    require(route.departure_time == queries.front().departure_time,
            "FAHL route departure time should match");
    require(!route.edge_ids.empty(), "FAHL should find a route for connected test queries");
    require(follows_graph(graph, queries.front().origin, route),
            "FAHL route should follow directed graph edges");

    gro::Query reverse_query;
    reverse_query.id = 0;
    reverse_query.origin = queries.front().destination;
    reverse_query.destination = queries.front().origin;
    reverse_query.departure_time = queries.front().departure_time;
    gro::Route reverse_route = index.query(reverse_query);
    require(reverse_route.edge_ids.empty(),
            "FAHL should not create a virtual route against directed connectivity");

    std::vector<gro::Route> routes =
        gro::compute_fahl_routes(graph, queries, profile, options);
    require(routes.size() == queries.size(), "FAHL should return one route per query");
    for (std::size_t i = 0; i < routes.size(); ++i) {
        require(routes[i].query_id == queries[i].id, "FAHL route query id should match");
        require(routes[i].departure_time == queries[i].departure_time,
                "FAHL route departure time should match");
        require(!routes[i].edge_ids.empty(),
                "FAHL should find non-empty routes for connected test queries");
        require(follows_graph(graph, queries[i].origin, routes[i]),
                "FAHL route should follow directed graph edges");
    }

    gro::TrafficResult traffic = gro::evaluate_traffic(graph, queries, routes, traffic_options);
    require(traffic.trajectories.size() == queries.size(),
            "FAHL routes should be traffic-evaluable");

    std::cout << "FAHL Done!" << std::endl;
    return 0;
}
