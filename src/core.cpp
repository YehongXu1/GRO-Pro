#include "core.hpp"
#include "data_structures.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>


namespace gro {
namespace {

std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }

    return text.substr(first, last - first);
}

int parse_integer(const std::string& value) {
    size_t index = 0;
    bool negative = false;
    if (index < value.size() && value[index] == '-') {
        negative = true;
        ++index;
    }

    int result = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
        result = result * 10 + value[index] - '0';
        ++index;
    }
    return negative ? -result : result;
}

int parse_percent(const std::string& value) {
    size_t index = 0;
    bool negative = false;
    if (index < value.size() && value[index] == '-') {
        negative = true;
        ++index;
    }

    long whole = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
        whole = whole * 10 + value[index] - '0';
        ++index;
    }

    long percent = 0;
    if (index < value.size() && value[index] == '.') {
        ++index;
        long fractional = 0;
        int digits = 0;
        while (index < value.size() && value[index] >= '0' && value[index] <= '9' && digits < 2) {
            fractional = fractional * 10 + value[index] - '0';
            ++index;
            ++digits;
        }
        while (digits < 2) {
            fractional *= 10;
            ++digits;
        }
        percent = whole * 100 + fractional;
    } else {
        percent = whole <= 1 ? whole * 100 : whole;
    }

    return negative ? -static_cast<int>(percent) : static_cast<int>(percent);
}

}  // namespace

ParameterMap load_parameter_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open parameter file: " + path);
    }

    ParameterMap parameters;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;

        std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::string key;
        std::string value;
        std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            throw std::runtime_error(
                "Expected key: value parameter at line " + std::to_string(line_number));
        }

        key = trim(line.substr(0, separator));
        value = trim(line.substr(separator + 1));
        if (key.empty()) {
            throw std::runtime_error("Invalid parameter at line " + std::to_string(line_number));
        }

        parameters[key] = value;
    }

    return parameters;
}

long long elapsed_us(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start).count();
}

void log_timing(
    bool enabled,
    int iteration,
    const char* operation,
    long long count,
    long long microseconds) {
    if (!enabled) {
        return;
    }

    std::cerr << "TIMING,"
              << iteration << ','
              << operation << ','
              << count << ','
              << microseconds << '\n';
}

void log_timing(
    bool enabled,
    int iteration,
    const char* operation,
    long long count,
    Clock::time_point start) {
    log_timing(enabled, iteration, operation, count, elapsed_us(start));
}

void log_timing(
    bool enabled,
    const char* operation,
    long long count,
    long long microseconds) {
    log_timing(enabled, -1, operation, count, microseconds);
}

void log_timing(
    bool enabled,
    const char* operation,
    long long count,
    Clock::time_point start) {
    log_timing(enabled, -1, operation, count, elapsed_us(start));
}

InputConfig load_input_config(const std::string& path) {
    ParameterMap parameters = load_parameter_file(path);
    const std::filesystem::path base_dir = std::filesystem::path(path).parent_path();
    auto resolve_path = [&](const std::string& value) {
        if (value.empty()) {
            return value;
        }
        std::filesystem::path candidate(value);
        if (candidate.is_relative() && !base_dir.empty()) {
            candidate = base_dir / candidate;
        }
        return candidate.string();
    };

    InputConfig config;
    if (auto it = parameters.find("graph_path"); it != parameters.end()) {
        config.graph_path = resolve_path(it->second);
    }
    if (auto it = parameters.find("coordinates_path"); it != parameters.end()) {
        config.coordinates_path = resolve_path(it->second);
    }
    if (auto it = parameters.find("queries_path"); it != parameters.end()) {
        config.queries_path = resolve_path(it->second);
    }

    if (config.graph_path.empty()) {
        throw std::runtime_error("Missing graph_path in input config: " + path);
    }

    return config;
}

TrafficOptions load_traffic_options(const std::string& path, TrafficOptions defaults) {
    ParameterMap parameters = load_parameter_file(path);

    TrafficOptions options = defaults;
    if (auto it = parameters.find("alpha"); it != parameters.end()) {
        options.alpha = parse_percent(it->second);
    }
    if (auto it = parameters.find("beta"); it != parameters.end()) {
        options.beta = parse_integer(it->second);
    }
    if (auto it = parameters.find("max_travel_time"); it != parameters.end()) {
        options.max_travel_time = static_cast<Time>(std::stoll(it->second));
    }
    if (auto it = parameters.find("max_time"); it != parameters.end()) {
        options.max_travel_time = static_cast<Time>(std::stoll(it->second));
    }
    return options;
}

Graph read_graph(const std::string& graph_path, const std::string& coordinates_path) {
    Graph graph;

    std::ifstream graph_file(graph_path);
    if (!graph_file) {
        throw std::runtime_error("Cannot open graph file: " + graph_path);
    }

    std::string line;
    int vertex_count = 0, edge_count = 0;
    while (std::getline(graph_file, line)) {
        line = trim(line);
        if (line[0] == '%') {
            std::istringstream input(line.substr(1));
            if (input >> vertex_count >> edge_count) {
                vertex_count += 1; // Assuming nodes are 0-indexed, we need to add 1 to get the correct count
                graph.vertex_count = vertex_count;
                graph.edge_count = edge_count;
                graph.outgoing_edges.resize(vertex_count);
                graph.incoming_edges.resize(vertex_count);
                graph.node_coordinates.resize(vertex_count);
            }
            continue;
        }

        std::istringstream input(line);
        std::vector<int> values;
        int value = 0;
        while (input >> value) {
            values.push_back(value);
        }

        if (values.size() < 3) {
            continue;
        }

        Edge edge;
        edge.id = static_cast<EdgeId>(graph.edges.size());
        if (values.size() >= 5) {
            edge.from = values[1];
            edge.to = values[2];
            edge.free_flow_time = values[3];
            edge.capacity = values[4];
        } else {
            edge.from = values[0];
            edge.to = values[1];
            edge.free_flow_time = values[2];
            edge.capacity = edge.free_flow_time / 40;  // Assume a default capacity based on free flow time if not provided
        }

        graph.outgoing_edges[edge.from].push_back(edge.id);
        graph.incoming_edges[edge.to].push_back(edge.id);
        graph.edges.push_back(edge);
    }

    if (!coordinates_path.empty()) {
        std::ifstream coord_file(coordinates_path);
        if (!coord_file) {
            throw std::runtime_error("Cannot open coordinates file: " + coordinates_path);
        }

        std::string line;
        while (std::getline(coord_file, line)) {
            if (trim(line).empty()) {
                continue;
            }

            std::istringstream input(line);
            NodeId node = kInvalidId;
            double lon = 0.0;
            double lat = 0.0;
            if (input >> node >> lon >> lat) {
                if (node >= static_cast<NodeId>(graph.node_coordinates.size())) {
                    graph.node_coordinates.resize(node + 1);
                }
                graph.node_coordinates[node] = {lon, lat};
            }
        }
    }

    return graph;
}

Graph read_graph(const InputConfig& config) {
    return read_graph(config.graph_path, config.coordinates_path);
}

std::vector<Query> read_queries(const std::string& queries_path) {
    std::ifstream file(queries_path);
    if (!file) {
        throw std::runtime_error("Cannot open queries file: " + queries_path);
    }

    std::vector<Query> queries;
    std::string line;
    int origin = 0, destination = 0, departure_time = 0;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream input(line);
        Query query;
        query.id = static_cast<QueryId>(queries.size());
        if (input >> origin >> destination >> departure_time) {
            query.origin = origin;
            query.destination = destination;
            query.departure_time = departure_time;  
            queries.push_back(query);
        }
    }

    return queries;
}

std::vector<Query> read_queries(const InputConfig& config) {
    if (config.queries_path.empty()) {
        throw std::runtime_error("Missing queries_path in input config");
    }
    return read_queries(config.queries_path);
}

Cost bpr_travel_time(const Edge& edge, Flow flow, const TrafficOptions& options) {
    Flow capacity = edge.capacity > 0 ? edge.capacity : 1;
    Flow safe_flow = flow > 0 ? flow : 0;

    __int128 numerator = static_cast<__int128>(edge.free_flow_time) * options.alpha;
    __int128 denominator = 100;
    for (int i = 0; i < options.beta; ++i) {
        numerator *= safe_flow;
        denominator *= capacity;
    }

    __int128 penalty = denominator > 0 ? (numerator + denominator - 1) / denominator : 0;
    __int128 value = static_cast<__int128>(edge.free_flow_time) + penalty;
    if (value > std::numeric_limits<Cost>::max()) {
        value = std::numeric_limits<Cost>::max();
    }

    Cost travel_time = static_cast<Cost>(value);
    if (options.max_travel_time > 0 && travel_time > options.max_travel_time) {
        travel_time = options.max_travel_time;
    }
    return travel_time;
}

Flow get_edge_flow(const TrafficResult& result, EdgeId edge_id, Time time) {
    if (edge_id >= static_cast<EdgeId>(result.edge_profiles.size())) {
        return 0;
    }
    
    const auto& profile = result.edge_profiles[edge_id];


    
    // Binary search for the first record with time > given time, then look back one step
    auto it = std::upper_bound(
        profile.begin(),
        profile.end(),
        time,
        [](Time t, const auto& record) { return t < record.time; }
    );
    
    if (it == profile.begin()) {
        return 0;
    }
    --it;
    return it->flow;
}

TrafficResult evaluate_traffic(
    const Graph& graph,
    const std::vector<Query>& queries,
    std::vector<Route>& routes,
    const TrafficOptions& options) {
    TrafficResult result;
    result.edge_profiles.resize(graph.edges.size());

    result.trajectories.assign(routes.size(), Trajectory());
    std::vector<int> query_current_position(queries.size(), 0);

    data_structures::IndexedHeap<4, Time, QueryId> event_queue(queries.size());
    for (Route& route : routes) {
        if (!route.edge_ids.empty()) {
            event_queue.push_or_update(route.query_id, route.departure_time);
            route.travel_time = 0; // Initialize travel time for each route
        }
    }

    Time current_time = 0;
    QueryId current_query = 0;
    while(!event_queue.empty()) {
        auto event = event_queue.extract_min();
        current_time = event.key;
        current_query = event.id;
        int position = query_current_position[current_query];

        const Route& route = routes[current_query];
        if (static_cast<std::size_t>(position) >= route.edge_ids.size()) {
            result.trajectories[current_query].schedule.push_back(current_time); // Add arrival time to the schedule
            result.trajectories[current_query].arrival_time = current_time;
            continue;  
        }

        if (position > 0) {
            // Remove flow from the previous edge
            EdgeId previous_edge_id = route.edge_ids[position - 1];
            auto& previous_profile = result.edge_profiles[previous_edge_id];
            if (current_time == previous_profile.back().time) {
                // If there's already a record for the current time, update it instead of adding a new one
                previous_profile.back().flow -= 1; 
                previous_profile.back().query_ids.erase(current_query);
            } else {
                // Otherwise, add a new record for the current time with the updated flow
                Flow previous_flow = previous_profile.back().flow;
                std::unordered_set<QueryId> previous_query_ids = previous_profile.back().query_ids;
                previous_query_ids.erase(current_query);
                previous_profile.push_back({previous_flow - 1, current_time, false, previous_query_ids});
            }
        } else {
            // Starting a new trajectory for the query
            result.trajectories[current_query].query_id = current_query;
        }

        // Add flow to the current edge and create a trajectory segment
        
        EdgeId edge_id = route.edge_ids[position];
        result.trajectories[current_query].edge_ids.push_back(edge_id);
        result.trajectories[current_query].schedule.push_back(current_time);

        const Edge& edge = graph.edges[edge_id];
        auto& profile = result.edge_profiles[edge_id]; // subject to change
        
        Time last_time = profile.empty() ? 0 : profile.back().time;
        assert(current_time >= last_time);
        Flow flow = profile.empty() ? 0 : profile.back().flow;

        if (profile.empty() ) {
            profile.push_back({flow + 1, current_time, true, {current_query}}); // Add a new record for the current time
        } else if (current_time > last_time) {
            std::unordered_set<QueryId> current_query_ids = profile.back().query_ids;
            current_query_ids.insert(current_query);
            profile.push_back({flow + 1, current_time, true, current_query_ids}); // Add a new record for the current time
        } else {
            profile.back().type = true; // Mark the existing record as an entry
            profile.back().flow = flow + 1; // Update the existing record for the current time
            profile.back().query_ids.insert(current_query);
        }

        Cost travel_time = bpr_travel_time(edge, flow, options);
        result.total_travel_time += travel_time;
        result.trajectories[current_query].travel_time += travel_time;
        
        current_time += travel_time;
        event_queue.push_or_update(current_query, current_time);
        query_current_position[current_query]++;
    }

    return result;
}

}  // namespace gro
