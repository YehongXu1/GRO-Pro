#include "fahl.hpp"
#include "data_structures.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <queue>
#include <utility>

namespace gro {
namespace {

constexpr Cost kScale = 1000000;

Cost infinity() {
    return std::numeric_limits<Cost>::max() / 8;
}

Cost safe_add(Cost lhs, Cost rhs) {
    if (lhs >= infinity() || rhs >= infinity()) {
        return infinity();
    }
    if (lhs > infinity() - rhs) {
        return infinity();
    }
    return lhs + rhs;
}

int bucket_for(Time time, Time time_step) {
    Time safe_step = time_step > 0 ? time_step : 1;
    return static_cast<int>(time / safe_step);
}

std::vector<int> occupied_buckets(
    Time enter_time,
    Cost travel_time,
    Time time_step) {
    Time safe_step = time_step > 0 ? time_step : 1;
    Time safe_travel_time = static_cast<Time>(std::max<Cost>(1, travel_time));
    Time exit_time = enter_time + safe_travel_time;
    int first_bucket = static_cast<int>(enter_time / safe_step);
    int last_bucket = static_cast<int>((exit_time - 1) / safe_step);

    std::vector<int> buckets;
    buckets.reserve(static_cast<std::size_t>(last_bucket - first_bucket + 1));
    for (int bucket = first_bucket; bucket <= last_bucket; ++bucket) {
        buckets.push_back(bucket);
    }
    return buckets;
}

void append_without_duplicate(
    std::vector<NodeId>& target,
    const std::vector<NodeId>& suffix) {
    if (suffix.empty()) {
        target.clear();
        return;
    }
    std::size_t start = 0;
    if (!target.empty() && target.back() == suffix.front()) {
        start = 1;
    }
    target.insert(target.end(), suffix.begin() + static_cast<long>(start), suffix.end());
}

}  // namespace

FAHLFlowProfile build_fahl_flow_profile(
    const Graph& graph,
    const std::vector<Route>& routes,
    Time time_step) {
    FAHLFlowProfile profile;
    for (const Route& route : routes) {
        Time enter_time = route.departure_time;
        for (EdgeId edge_id : route.edge_ids) {
            if (edge_id < 0 || edge_id >= static_cast<EdgeId>(graph.edges.size())) {
                continue;
            }
            const Edge& edge = graph.edges[edge_id];
            for (int bucket : occupied_buckets(
                     enter_time,
                     edge.free_flow_time,
                     time_step)) {
                ++profile[{edge_id, bucket}];
            }
            enter_time += static_cast<Time>(edge.free_flow_time);
        }
    }
    return profile;
}

FAHLFlowProfile build_fahl_flow_profile(
    const Graph& graph,
    const std::vector<Trajectory>& trajectories,
    Time time_step) {
    FAHLFlowProfile profile;
    for (const Trajectory& trajectory : trajectories) {
        for (std::size_t i = 0; i < trajectory.edge_ids.size(); ++i) {
            EdgeId edge_id = trajectory.edge_ids[i];
            if (edge_id < 0 || edge_id >= static_cast<EdgeId>(graph.edges.size())) {
                continue;
            }
            Time enter_time = i < trajectory.schedule.size()
                ? trajectory.schedule[i]
                : trajectory.arrival_time;
            Cost travel_time = graph.edges[edge_id].free_flow_time;
            if (i + 1 < trajectory.schedule.size() && trajectory.schedule[i + 1] > enter_time) {
                travel_time = trajectory.schedule[i + 1] - enter_time;
            }
            for (int bucket : occupied_buckets(enter_time, travel_time, time_step)) {
                ++profile[{edge_id, bucket}];
            }
        }
    }
    return profile;
}

struct FAHLIndex::Impl {
    struct Arc {
        Cost cost = infinity();
        NodeId mid = kInvalidId;
    };

    struct NeighborLabel {
        NodeId node = kInvalidId;
        Cost forward_cost = infinity();
        Cost backward_cost = infinity();
    };

    struct TreeNode {
        NodeId vertex = kInvalidId;
        int parent = kInvalidId;
        int height = 0;
        std::vector<int> children;
        std::vector<NeighborLabel> neighbors;
        std::vector<NodeId> label_vertices;
        std::unordered_map<NodeId, int> label_pos;
        std::vector<Cost> forward_dist;
        std::vector<Cost> backward_dist;
        std::vector<NodeId> forward_pivot;
        std::vector<NodeId> backward_pivot;
    };

    Impl(
        const Graph& graph,
        const FAHLFlowProfile& flow_profile,
        int time_bucket,
        FAHLOptions options)
        : graph(graph),
          options(options),
          time_bucket(time_bucket) {
        build_edge_scores(flow_profile);
        build_working_graph();
        contract_graph();
        build_tree();
        build_labels();
    }

    const Graph& graph;
    FAHLOptions options;
    int time_bucket = 0;
    std::vector<Cost> edge_scores;
    std::vector<Flow> edge_flows;
    std::vector<Flow> vertex_flows;
    std::vector<NodeId> order;
    std::vector<std::vector<NeighborLabel>> contracted_neighbors;
    std::vector<std::unordered_map<NodeId, Arc>> working;
    std::unordered_map<std::pair<NodeId, NodeId>, EdgeId, PairHash> original_edge;
    std::unordered_map<std::pair<NodeId, NodeId>, NodeId, PairHash> shortcut_mid;
    std::vector<int> rank;
    std::vector<TreeNode> tree;
    int order_max_degree = 1;
    Flow order_min_flow = 0;
    Flow order_max_flow = 0;

    Flow flow_for_edge(const FAHLFlowProfile& flow_profile, EdgeId edge_id) const {
        auto it = flow_profile.find({edge_id, time_bucket});
        return it == flow_profile.end() ? 0 : it->second;
    }

    void build_edge_scores(const FAHLFlowProfile& flow_profile) {
        edge_scores.assign(graph.edges.size(), 1);
        edge_flows.assign(graph.edges.size(), 0);
        vertex_flows.assign(graph.vertex_count, 0);

        Cost max_time = 1;
        Flow max_flow = 0;
        for (const Edge& edge : graph.edges) {
            max_time = std::max(max_time, edge.free_flow_time);
            Flow flow = flow_for_edge(flow_profile, edge.id);
            edge_flows[edge.id] = flow;
            max_flow = std::max(max_flow, flow);
            if (edge.from >= 0 && edge.from < graph.vertex_count) {
                vertex_flows[edge.from] += flow;
            }
            if (edge.to >= 0 && edge.to < graph.vertex_count) {
                vertex_flows[edge.to] += flow;
            }
        }

        int alpha = std::clamp(options.alpha_percent, 0, 100);
        for (const Edge& edge : graph.edges) {
            Cost distance_scaled =
                std::max<Cost>(1, edge.free_flow_time) * kScale / max_time;
            Cost flow_scaled = max_flow > 0
                ? static_cast<Cost>(edge_flows[edge.id]) * kScale / max_flow
                : 0;
            edge_scores[edge.id] =
                1 + (static_cast<Cost>(alpha) * distance_scaled +
                     static_cast<Cost>(100 - alpha) * flow_scaled) / 100;
        }
    }

    void ensure_arc(NodeId from, NodeId to) {
        if (from < 0 || from >= graph.vertex_count || to < 0 || to >= graph.vertex_count) {
            return;
        }
        if (working[from].find(to) == working[from].end()) {
            working[from][to] = Arc();
        }
    }

    void update_arc(NodeId from, NodeId to, Cost cost, NodeId mid) {
        if (from == to || cost >= infinity()) {
            return;
        }
        ensure_arc(from, to);
        ensure_arc(to, from);
        Arc& arc = working[from][to];
        if (cost < arc.cost || (cost == arc.cost && mid < arc.mid)) {
            arc.cost = cost;
            arc.mid = mid;
            if (mid == kInvalidId) {
                shortcut_mid.erase({from, to});
            } else {
                shortcut_mid[{from, to}] = mid;
            }
        }
    }

    void build_working_graph() {
        working.assign(graph.vertex_count, {});
        for (const Edge& edge : graph.edges) {
            update_arc(edge.from, edge.to, edge_scores[edge.id], kInvalidId);

            auto key = std::make_pair(edge.from, edge.to);
            auto it = original_edge.find(key);
            if (it == original_edge.end() ||
                edge_scores[edge.id] < edge_scores[it->second] ||
                (edge_scores[edge.id] == edge_scores[it->second] && edge.id < it->second)) {
                original_edge[key] = edge.id;
            }
        }
    }

    int active_degree(NodeId node, const std::vector<char>& active) const {
        int degree = 0;
        for (const auto& [neighbor, _] : working[node]) {
            if (neighbor >= 0 && neighbor < graph.vertex_count && active[neighbor]) {
                ++degree;
            }
        }
        return degree;
    }

    double ordering_score(NodeId node, int degree) const {
        double beta =
            static_cast<double>(std::clamp(options.order_beta_percent, 0, 100)) /
            100.0;
        double degree_norm =
            static_cast<double>(degree) / static_cast<double>(order_max_degree);
        Flow flow = node < static_cast<NodeId>(vertex_flows.size())
            ? vertex_flows[node]
            : 0;
        double flow_norm = order_max_flow > order_min_flow
            ? static_cast<double>(flow - order_min_flow) /
                  static_cast<double>(order_max_flow - order_min_flow)
            : 0.0;

        // FAHL places lower-flow vertices closer to the tree root, so high-flow
        // vertices receive smaller elimination scores and are contracted earlier.
        double high_flow_first = 1.0 - flow_norm;
        return (1.0 - beta) * degree_norm + beta * high_flow_first;
    }

    struct QueueItem {
        double score = 0.0;
        int degree = 0;
        int version = 0;
        NodeId node = kInvalidId;
    };

    struct QueueCompare {
        bool operator()(const QueueItem& lhs, const QueueItem& rhs) const {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            if (lhs.degree != rhs.degree) {
                return lhs.degree > rhs.degree;
            }
            return lhs.node > rhs.node;
        }
    };

    void initialise_ordering_stats() {
        order_max_degree = 1;
        order_min_flow = std::numeric_limits<Flow>::max();
        order_max_flow = 0;
        for (NodeId node = 0; node < graph.vertex_count; ++node) {
            order_max_degree =
                std::max(order_max_degree, static_cast<int>(working[node].size()));
            Flow flow = node < static_cast<NodeId>(vertex_flows.size())
                ? vertex_flows[node]
                : 0;
            order_min_flow = std::min(order_min_flow, flow);
            order_max_flow = std::max(order_max_flow, flow);
        }
        if (order_min_flow == std::numeric_limits<Flow>::max()) {
            order_min_flow = 0;
        }
    }

    void push_queue_item(
        std::priority_queue<QueueItem, std::vector<QueueItem>, QueueCompare>& queue,
        const std::vector<char>& active,
        std::vector<int>& versions,
        NodeId node) {
        if (node < 0 || node >= graph.vertex_count || !active[node]) {
            return;
        }
        int degree = active_degree(node, active);
        ++versions[node];
        queue.push(QueueItem{
            ordering_score(node, degree),
            degree,
            versions[node],
            node});
    }

    void contract_graph() {
        contracted_neighbors.assign(graph.vertex_count, {});
        initialise_ordering_stats();

        std::vector<char> active(graph.vertex_count, 1);
        std::vector<int> versions(graph.vertex_count, 0);
        std::priority_queue<QueueItem, std::vector<QueueItem>, QueueCompare> queue;
        order.clear();
        order.reserve(graph.vertex_count);
        for (NodeId node = 0; node < graph.vertex_count; ++node) {
            push_queue_item(queue, active, versions, node);
        }

        while (!queue.empty()) {
            QueueItem item = queue.top();
            queue.pop();
            NodeId x = item.node;
            if (x < 0 ||
                x >= graph.vertex_count ||
                !active[x] ||
                item.version != versions[x] ||
                item.degree != active_degree(x, active)) {
                continue;
            }

            std::vector<NodeId> neighbors;
            for (const auto& [neighbor, _] : working[x]) {
                if (neighbor >= 0 && neighbor < graph.vertex_count && active[neighbor]) {
                    neighbors.push_back(neighbor);
                }
            }

            contracted_neighbors[x].reserve(neighbors.size());
            for (NodeId neighbor : neighbors) {
                Cost forward = working[x][neighbor].cost;
                Cost backward = working[neighbor][x].cost;
                contracted_neighbors[x].push_back(NeighborLabel{neighbor, forward, backward});
            }

            active[x] = 0;
            order.push_back(x);

            for (NodeId from : neighbors) {
                Cost from_to_x = working[from][x].cost;
                if (from_to_x >= infinity()) {
                    continue;
                }
                for (NodeId to : neighbors) {
                    if (from == to) {
                        continue;
                    }
                    Cost x_to_to = working[x][to].cost;
                    Cost shortcut_cost = safe_add(from_to_x, x_to_to);
                    if (shortcut_cost < infinity()) {
                        update_arc(from, to, shortcut_cost, x);
                    }
                }
            }

            for (NodeId neighbor : neighbors) {
                working[neighbor].erase(x);
            }
            working[x].clear();

            for (NodeId neighbor : neighbors) {
                push_queue_item(queue, active, versions, neighbor);
            }
        }
    }

    void build_tree() {
        rank.assign(graph.vertex_count, kInvalidId);
        tree.clear();
        tree.reserve(order.size());

        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            NodeId vertex = *it;
            TreeNode node;
            node.vertex = vertex;
            node.neighbors = contracted_neighbors[vertex];

            int parent = kInvalidId;
            for (const NeighborLabel& neighbor : node.neighbors) {
                if (neighbor.node >= 0 &&
                    neighbor.node < graph.vertex_count &&
                    rank[neighbor.node] != kInvalidId) {
                    if (parent == kInvalidId || rank[neighbor.node] > parent) {
                        parent = rank[neighbor.node];
                    }
                }
            }

            node.parent = parent;
            node.height = parent == kInvalidId ? 0 : tree[parent].height + 1;
            int tree_index = static_cast<int>(tree.size());
            rank[vertex] = tree_index;
            tree.push_back(std::move(node));
            if (parent != kInvalidId) {
                tree[parent].children.push_back(tree_index);
            }
        }
    }

    void initialise_label_positions(TreeNode& node) {
        node.label_pos.clear();
        for (int i = 0; i < static_cast<int>(node.label_vertices.size()); ++i) {
            node.label_pos[node.label_vertices[i]] = i;
        }
    }

    void build_labels() {
        for (TreeNode& node : tree) {
            if (node.parent != kInvalidId) {
                node.label_vertices = tree[node.parent].label_vertices;
            }
            node.label_vertices.push_back(node.vertex);
            initialise_label_positions(node);

            std::size_t label_count = node.label_vertices.size();
            node.forward_dist.assign(label_count, infinity());
            node.backward_dist.assign(label_count, infinity());
            node.forward_pivot.assign(label_count, kInvalidId);
            node.backward_pivot.assign(label_count, kInvalidId);

            int self_pos = node.label_pos[node.vertex];
            node.forward_dist[self_pos] = 0;
            node.backward_dist[self_pos] = 0;

            for (const NeighborLabel& neighbor : node.neighbors) {
                auto label_it = node.label_pos.find(neighbor.node);
                if (label_it == node.label_pos.end()) {
                    continue;
                }
                int pos = label_it->second;
                if (neighbor.forward_cost < node.forward_dist[pos]) {
                    node.forward_dist[pos] = neighbor.forward_cost;
                }
                if (neighbor.backward_cost < node.backward_dist[pos]) {
                    node.backward_dist[pos] = neighbor.backward_cost;
                }
            }

            for (const NeighborLabel& neighbor : node.neighbors) {
                if (neighbor.node < 0 ||
                    neighbor.node >= graph.vertex_count ||
                    rank[neighbor.node] == kInvalidId) {
                    continue;
                }

                const TreeNode& via = tree[rank[neighbor.node]];
                for (int pos = 0; pos < static_cast<int>(node.label_vertices.size()); ++pos) {
                    NodeId hub = node.label_vertices[pos];
                    auto via_pos_it = via.label_pos.find(hub);
                    if (via_pos_it == via.label_pos.end()) {
                        continue;
                    }
                    int via_pos = via_pos_it->second;

                    Cost forward_candidate =
                        safe_add(neighbor.forward_cost, via.forward_dist[via_pos]);
                    if (forward_candidate < node.forward_dist[pos]) {
                        node.forward_dist[pos] = forward_candidate;
                        node.forward_pivot[pos] = neighbor.node;
                    }

                    Cost backward_candidate =
                        safe_add(via.backward_dist[via_pos], neighbor.backward_cost);
                    if (backward_candidate < node.backward_dist[pos]) {
                        node.backward_dist[pos] = backward_candidate;
                        node.backward_pivot[pos] = neighbor.node;
                    }
                }
            }
        }
    }

    std::vector<NodeId> expand_arc(NodeId from, NodeId to, int depth) const {
        if (from == to) {
            return {from};
        }
        if (depth > graph.vertex_count * 4 + 4) {
            return {};
        }

        auto shortcut_it = shortcut_mid.find({from, to});
        if (shortcut_it != shortcut_mid.end() &&
            shortcut_it->second != kInvalidId &&
            shortcut_it->second != from &&
            shortcut_it->second != to) {
            std::vector<NodeId> prefix = expand_arc(from, shortcut_it->second, depth + 1);
            std::vector<NodeId> suffix = expand_arc(shortcut_it->second, to, depth + 1);
            if (prefix.empty() || suffix.empty()) {
                return {};
            }
            append_without_duplicate(prefix, suffix);
            return prefix;
        }

        if (original_edge.find({from, to}) != original_edge.end()) {
            return {from, to};
        }
        return {};
    }

    std::vector<NodeId> build_forward_path(NodeId from, NodeId hub, int depth) const {
        if (from == hub) {
            return {from};
        }
        if (depth > graph.vertex_count * 4 + 4 ||
            from < 0 ||
            from >= graph.vertex_count ||
            rank[from] == kInvalidId) {
            return {};
        }

        const TreeNode& node = tree[rank[from]];
        auto pos_it = node.label_pos.find(hub);
        if (pos_it == node.label_pos.end()) {
            return {};
        }
        int pos = pos_it->second;
        if (node.forward_dist[pos] >= infinity()) {
            return {};
        }

        NodeId pivot = node.forward_pivot[pos];
        if (pivot != kInvalidId && pivot != from && pivot != hub) {
            std::vector<NodeId> prefix = build_forward_path(from, pivot, depth + 1);
            std::vector<NodeId> suffix = build_forward_path(pivot, hub, depth + 1);
            if (prefix.empty() || suffix.empty()) {
                return {};
            }
            append_without_duplicate(prefix, suffix);
            return prefix;
        }
        return expand_arc(from, hub, depth + 1);
    }

    std::vector<NodeId> build_backward_path(NodeId hub, NodeId to, int depth) const {
        if (hub == to) {
            return {hub};
        }
        if (depth > graph.vertex_count * 4 + 4 ||
            to < 0 ||
            to >= graph.vertex_count ||
            rank[to] == kInvalidId) {
            return {};
        }

        const TreeNode& node = tree[rank[to]];
        auto pos_it = node.label_pos.find(hub);
        if (pos_it == node.label_pos.end()) {
            return {};
        }
        int pos = pos_it->second;
        if (node.backward_dist[pos] >= infinity()) {
            return {};
        }

        NodeId pivot = node.backward_pivot[pos];
        if (pivot != kInvalidId && pivot != hub && pivot != to) {
            std::vector<NodeId> prefix = build_backward_path(hub, pivot, depth + 1);
            std::vector<NodeId> suffix = build_backward_path(pivot, to, depth + 1);
            if (prefix.empty() || suffix.empty()) {
                return {};
            }
            append_without_duplicate(prefix, suffix);
            return prefix;
        }
        return expand_arc(hub, to, depth + 1);
    }

    std::vector<EdgeId> nodes_to_edges(const std::vector<NodeId>& nodes) const {
        std::vector<EdgeId> edge_ids;
        if (nodes.size() < 2) {
            return edge_ids;
        }
        edge_ids.reserve(nodes.size() - 1);
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            auto edge_it = original_edge.find({nodes[i - 1], nodes[i]});
            if (edge_it == original_edge.end()) {
                return {};
            }
            edge_ids.push_back(edge_it->second);
        }
        return edge_ids;
    }

    Route flow_aware_dijkstra(const Query& query) const {
        Route route;
        route.query_id = query.id;
        route.departure_time = query.departure_time;
        if (query.origin == query.destination) {
            return route;
        }
        if (query.origin < 0 ||
            query.origin >= graph.vertex_count ||
            query.destination < 0 ||
            query.destination >= graph.vertex_count) {
            return route;
        }

        data_structures::IndexedHeap<4, Cost, NodeId> heap(graph.vertex_count);
        std::vector<Cost> dist(graph.vertex_count, infinity());
        std::vector<EdgeId> parent(graph.vertex_count, kInvalidId);
        heap.push_or_update(query.origin, 0);
        dist[query.origin] = 0;

        while (!heap.empty()) {
            auto item = heap.extract_min();
            NodeId node = item.id;
            if (item.key != dist[node]) {
                continue;
            }
            if (node == query.destination) {
                break;
            }
            for (EdgeId edge_id : graph.outgoing_edges[node]) {
                const Edge& edge = graph.edges[edge_id];
                Cost next = safe_add(dist[node], edge_scores[edge_id]);
                if (next < dist[edge.to]) {
                    dist[edge.to] = next;
                    parent[edge.to] = edge_id;
                    heap.push_or_update(edge.to, next);
                }
            }
        }

        if (dist[query.destination] >= infinity()) {
            return route;
        }

        for (NodeId current = query.destination; current != query.origin;) {
            EdgeId edge_id = parent[current];
            if (edge_id == kInvalidId) {
                route.edge_ids.clear();
                return route;
            }
            route.edge_ids.push_back(edge_id);
            current = graph.edges[edge_id].from;
        }
        std::reverse(route.edge_ids.begin(), route.edge_ids.end());
        route.travel_time = route_edge_length(graph, route.edge_ids);
        return route;
    }

    Route query(const Query& query) const {
        Route route;
        route.query_id = query.id;
        route.departure_time = query.departure_time;

        if (query.origin == query.destination) {
            return route;
        }
        if (query.origin < 0 ||
            query.origin >= graph.vertex_count ||
            query.destination < 0 ||
            query.destination >= graph.vertex_count ||
            rank[query.origin] == kInvalidId ||
            rank[query.destination] == kInvalidId) {
            return route;
        }

        const TreeNode& source = tree[rank[query.origin]];
        const TreeNode& target = tree[rank[query.destination]];
        Cost best = infinity();
        NodeId best_hub = kInvalidId;

        for (int source_pos = 0;
             source_pos < static_cast<int>(source.label_vertices.size());
             ++source_pos) {
            NodeId hub = source.label_vertices[source_pos];
            auto target_pos_it = target.label_pos.find(hub);
            if (target_pos_it == target.label_pos.end()) {
                continue;
            }
            int target_pos = target_pos_it->second;
            Cost candidate = safe_add(
                source.forward_dist[source_pos],
                target.backward_dist[target_pos]);
            if (candidate < best ||
                (candidate == best && (best_hub == kInvalidId || hub < best_hub))) {
                best = candidate;
                best_hub = hub;
            }
        }

        if (best_hub == kInvalidId || best >= infinity()) {
            return flow_aware_dijkstra(query);
        }

        std::vector<NodeId> node_path = build_forward_path(query.origin, best_hub, 0);
        std::vector<NodeId> suffix = build_backward_path(best_hub, query.destination, 0);
        if (node_path.empty() || suffix.empty()) {
            return flow_aware_dijkstra(query);
        }
        append_without_duplicate(node_path, suffix);

        route.edge_ids = nodes_to_edges(node_path);
        if (route.edge_ids.empty()) {
            return flow_aware_dijkstra(query);
        }
        route.travel_time = route_edge_length(graph, route.edge_ids);
        return route;
    }
};

FAHLIndex::FAHLIndex(
    const Graph& graph,
    const FAHLFlowProfile& flow_profile,
    int time_bucket,
    FAHLOptions options)
    : impl_(std::make_shared<Impl>(graph, flow_profile, time_bucket, options)) {
    contraction_order_ = impl_->order;
}

Route FAHLIndex::query(const Query& query) const {
    return impl_->query(query);
}

std::vector<Route> FAHLIndex::query(const std::vector<Query>& queries) const {
    std::vector<Route> routes;
    routes.reserve(queries.size());
    for (const Query& query_item : queries) {
        routes.push_back(query(query_item));
    }
    return routes;
}

std::vector<Route> compute_fahl_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const FAHLFlowProfile& flow_profile,
    FAHLOptions options) {
    std::vector<Route> routes(queries.size());
    std::unordered_map<int, std::shared_ptr<FAHLIndex>> index_cache;
    Time safe_step = options.time_step > 0 ? options.time_step : 1;

    for (const Query& query : queries) {
        int bucket = bucket_for(query.departure_time, safe_step);
        auto index_it = index_cache.find(bucket);
        if (index_it == index_cache.end()) {
            index_it = index_cache.emplace(
                bucket,
                std::make_shared<FAHLIndex>(graph, flow_profile, bucket, options)).first;
        }
        routes[query.id] = index_it->second->query(query);
    }
    return routes;
}

std::vector<Route> compute_fahl_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const std::vector<Route>& reference_routes,
    FAHLOptions options) {
    FAHLFlowProfile profile =
        build_fahl_flow_profile(graph, reference_routes, options.time_step);
    return compute_fahl_routes(graph, queries, profile, options);
}

}  // namespace gro
