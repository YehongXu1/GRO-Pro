#pragma once

#include <chrono>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unordered_set>

namespace gro {

using NodeId = int;
using EdgeId = int;
using QueryId = int;
using TDGNodeId = int;
using Time = int;
using Cost = long long int;
using Flow = int;
using Coordinate = std::pair<double, double>;
using Clock = std::chrono::steady_clock;

constexpr int kInvalidId = -1;

struct Edge {
    EdgeId id = kInvalidId;
    NodeId from = kInvalidId;
    NodeId to = kInvalidId;
    Cost free_flow_time = 1;
    Flow capacity = 0;
};

struct Graph {
    std::vector<Edge> edges;
    std::vector<std::vector<EdgeId>> outgoing_edges;
    std::vector<std::vector<EdgeId>> incoming_edges;
    std::vector<Coordinate> node_coordinates;
    int vertex_count, edge_count;
};

struct Query {
    QueryId id = kInvalidId;
    NodeId origin = kInvalidId;
    NodeId destination = kInvalidId;
    Time departure_time = 0;
};

struct Route {
    QueryId query_id = kInvalidId;
    std::vector<EdgeId> edge_ids;
    Time departure_time = 0;
    Cost travel_time = 0;
};

struct Trajectory {
    QueryId query_id = kInvalidId;
    std::vector<EdgeId> edge_ids;
    std::vector<Time> schedule;
    std::vector<TDGNodeId> tdg_node_ids;
    Cost travel_time = 0;
    Time arrival_time = 0;
};

struct Event {
    Flow flow = 0;
    Time time = 0;
    bool type = false; // true entry, false exit
    std::unordered_set<QueryId> query_ids; // set of queries that contribute to this event
};


struct TrafficResult {
    std::vector<Trajectory> trajectories;
    std::vector<std::vector<Event>> edge_profiles;
    Cost total_travel_time = 0;
};

struct TrafficOptions {
    int alpha = 15; // in percent
    int beta = 4;
    Time max_travel_time = 0;
};

using ParameterMap = std::unordered_map<std::string, std::string>;

struct InputConfig {
    std::string graph_path;
    std::string coordinates_path;
    std::string queries_path;
};

ParameterMap load_parameter_file(const std::string& path);
InputConfig load_input_config(const std::string& path);
TrafficOptions load_traffic_options(const std::string& path, TrafficOptions defaults = {});

Graph read_graph(const std::string& graph_path, const std::string& coordinates_path = "");
Graph read_graph(const InputConfig& config);
std::vector<Query> read_queries(const std::string& queries_path);
std::vector<Query> read_queries(const InputConfig& config);

Cost bpr_travel_time(const Edge& edge, Flow flow, const TrafficOptions& options);

Flow get_edge_flow(const TrafficResult& result, EdgeId edge_id, Time time);

TrafficResult evaluate_traffic(
    const Graph& graph,
    const std::vector<Query>& queries,
    std::vector<Route>& routes,
    const TrafficOptions& options);

long long elapsed_us(Clock::time_point start);

void log_timing(
    bool enabled,
    int iteration,
    const char* operation,
    long long count,
    long long microseconds);

void log_timing(
    bool enabled,
    int iteration,
    const char* operation,
    long long count,
    Clock::time_point start);

void log_timing(
    bool enabled,
    const char* operation,
    long long count,
    long long microseconds);

void log_timing(
    bool enabled,
    const char* operation,
    long long count,
    Clock::time_point start);

}  // namespace gro
