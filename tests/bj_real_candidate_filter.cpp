#include "core.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

struct Options {
    std::string graph_path = "data/BJ.txt";
    std::string coords_path = "data/BJ_NodeIDLonLat.txt";
    std::filesystem::path candidates_path;
    std::filesystem::path output_dir = "data/BJ_Real_query_sets_long100k";
    int sets = 5;
    int queries_per_set = 10000;
    std::vector<int> rep_values = {10};
    gro::Cost min_free_flow_seconds = 8 * 60;
    gro::Cost max_free_flow_seconds = 60 * 60;
    int threads = 0;
    int progress_interval = 50000;
    int random_seed = 0;
    int jitter_seconds = 0;
};

struct Candidate {
    gro::NodeId origin = gro::kInvalidId;
    gro::NodeId destination = gro::kInvalidId;
    gro::Time departure_abs_seconds = 0;
    gro::Cost duration_seconds = 0;
    double haversine_km = 0.0;
    int unique_od = -1;
};

struct SelectedCandidate {
    Candidate candidate;
    gro::Cost free_flow_seconds = 0;
};

struct Workspace {
    std::vector<gro::Cost> distances;
    std::vector<int> stamps;
    int stamp = 0;

    explicit Workspace(std::size_t vertex_count)
        : distances(vertex_count, 0), stamps(vertex_count, 0) {}
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string trim(const std::string& text) {
    std::size_t first = 0;
    while (first < text.size() &&
           std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }
    return text.substr(first, last - first);
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream stream(line);
    std::string part;
    while (std::getline(stream, part, ',')) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<int> parse_int_list(const std::string& text) {
    std::vector<int> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        value = trim(value);
        if (!value.empty()) {
            values.push_back(std::stoi(value));
        }
    }
    if (values.empty()) {
        fail("Expected a non-empty integer list");
    }
    return values;
}

Options parse_args(int argc, char** argv) {
    Options options;
    int index = 1;
    auto require_value = [&](const std::string& flag) -> std::string {
        if (index >= argc) {
            fail("Missing value after " + flag);
        }
        return argv[index++];
    };

    while (index < argc) {
        std::string arg = argv[index++];
        if (arg == "--graph") {
            options.graph_path = require_value(arg);
        } else if (arg == "--coords") {
            options.coords_path = require_value(arg);
        } else if (arg == "--candidates") {
            options.candidates_path = require_value(arg);
        } else if (arg == "--output-dir") {
            options.output_dir = require_value(arg);
        } else if (arg == "--sets") {
            options.sets = std::stoi(require_value(arg));
        } else if (arg == "--queries-per-set") {
            options.queries_per_set = std::stoi(require_value(arg));
        } else if (arg == "--rep-values") {
            options.rep_values = parse_int_list(require_value(arg));
        } else if (arg == "--min-free-flow-min") {
            options.min_free_flow_seconds =
                static_cast<gro::Cost>(std::llround(std::stod(require_value(arg)) * 60.0));
        } else if (arg == "--max-free-flow-min") {
            options.max_free_flow_seconds =
                static_cast<gro::Cost>(std::llround(std::stod(require_value(arg)) * 60.0));
        } else if (arg == "--threads") {
            options.threads = std::stoi(require_value(arg));
        } else if (arg == "--progress-interval") {
            options.progress_interval = std::stoi(require_value(arg));
        } else if (arg == "--random-seed") {
            options.random_seed = std::stoi(require_value(arg));
        } else if (arg == "--amplify-time-jitter-sec") {
            options.jitter_seconds = std::stoi(require_value(arg));
        } else if (arg == "--help") {
            std::cout
                << "Usage: ./bj_real_candidate_filter --candidates path "
                << "[--graph data/BJ.txt] [--coords data/BJ_NodeIDLonLat.txt] "
                << "[--output-dir dir] [--threads n]\n";
            std::exit(0);
        } else {
            fail("Unknown argument: " + arg);
        }
    }

    if (options.candidates_path.empty()) {
        fail("--candidates is required");
    }
    if (options.sets <= 0 || options.queries_per_set <= 0) {
        fail("--sets and --queries-per-set must be positive");
    }
    if (options.max_free_flow_seconds < options.min_free_flow_seconds) {
        fail("--max-free-flow-min must be >= --min-free-flow-min");
    }
    return options;
}

gro::Cost bounded_shortest_path(
    const gro::Graph& graph,
    gro::NodeId source,
    gro::NodeId target,
    gro::Cost cutoff,
    Workspace& workspace) {
    if (source == target) {
        return 0;
    }
    if (source < 0 || target < 0 ||
        source >= static_cast<gro::NodeId>(graph.outgoing_edges.size()) ||
        target >= static_cast<gro::NodeId>(graph.outgoing_edges.size())) {
        return std::numeric_limits<gro::Cost>::max();
    }

    ++workspace.stamp;
    if (workspace.stamp == std::numeric_limits<int>::max()) {
        std::fill(workspace.stamps.begin(), workspace.stamps.end(), 0);
        workspace.stamp = 1;
    }
    const int stamp = workspace.stamp;

    using QueueItem = std::pair<gro::Cost, gro::NodeId>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    workspace.stamps[source] = stamp;
    workspace.distances[source] = 0;
    queue.push({0, source});

    while (!queue.empty()) {
        auto [distance, node] = queue.top();
        queue.pop();
        if (workspace.stamps[node] != stamp || distance != workspace.distances[node]) {
            continue;
        }
        if (distance > cutoff) {
            break;
        }
        if (node == target) {
            return distance;
        }

        for (gro::EdgeId edge_id : graph.outgoing_edges[node]) {
            const gro::Edge& edge = graph.edges[edge_id];
            gro::Cost next_distance = distance + edge.free_flow_time;
            if (next_distance > cutoff) {
                continue;
            }
            if (workspace.stamps[edge.to] != stamp ||
                next_distance < workspace.distances[edge.to]) {
                workspace.stamps[edge.to] = stamp;
                workspace.distances[edge.to] = next_distance;
                queue.push({next_distance, edge.to});
            }
        }
    }
    return std::numeric_limits<gro::Cost>::max();
}

void read_candidates(
    const std::filesystem::path& path,
    std::vector<Candidate>& candidates,
    std::vector<std::pair<gro::NodeId, gro::NodeId>>& unique_od) {
    std::ifstream file(path);
    if (!file) {
        fail("Cannot open candidates CSV: " + path.string());
    }

    std::unordered_map<std::pair<gro::NodeId, gro::NodeId>, int, gro::PairHash> ids;
    std::string line;
    bool first_line = true;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (first_line) {
            first_line = false;
            if (line.find("origin") != std::string::npos) {
                continue;
            }
        }
        std::vector<std::string> parts = split_csv_line(line);
        if (parts.size() < 6) {
            fail("Bad candidates CSV line: " + line);
        }

        Candidate candidate;
        candidate.origin = std::stoi(parts[0]);
        candidate.destination = std::stoi(parts[1]);
        candidate.departure_abs_seconds = std::stoll(parts[2]);
        candidate.duration_seconds = std::stoll(parts[4]);
        candidate.haversine_km = std::stod(parts[5]);

        std::pair<gro::NodeId, gro::NodeId> key{
            candidate.origin,
            candidate.destination,
        };
        auto [it, inserted] = ids.emplace(key, static_cast<int>(unique_od.size()));
        if (inserted) {
            unique_od.push_back(key);
        }
        candidate.unique_od = it->second;
        candidates.push_back(candidate);
    }
}

std::string json_escape(const std::string& text) {
    std::string result;
    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

void write_query_file(
    const std::filesystem::path& path,
    const std::vector<SelectedCandidate>& candidates,
    int rep,
    gro::Time time_origin,
    std::mt19937_64& rng,
    int jitter_seconds) {
    std::vector<std::tuple<gro::NodeId, gro::NodeId, gro::Time>> rows;
    rows.reserve(candidates.size() * static_cast<std::size_t>(rep));
    std::uniform_int_distribution<int> jitter_distribution(
        -jitter_seconds,
        jitter_seconds);

    for (const SelectedCandidate& selected : candidates) {
        gro::Time base_departure =
            selected.candidate.departure_abs_seconds - time_origin;
        for (int copy = 0; copy < rep; ++copy) {
            int jitter = jitter_seconds > 0 ? jitter_distribution(rng) : 0;
            gro::Time departure = std::max<gro::Time>(0, base_departure + jitter);
            rows.push_back({
                selected.candidate.origin,
                selected.candidate.destination,
                departure,
            });
        }
    }

    std::sort(
        rows.begin(),
        rows.end(),
        [](const auto& lhs, const auto& rhs) {
            return std::get<2>(lhs) < std::get<2>(rhs);
        });

    std::ofstream out(path);
    if (!out) {
        fail("Cannot write query file: " + path.string());
    }
    for (const auto& [origin, destination, departure] : rows) {
        out << origin << ' ' << destination << ' ' << departure << '\n';
    }
}

void write_metadata(
    const Options& options,
    const std::vector<std::vector<SelectedCandidate>>& selected_sets,
    std::size_t coarse_count,
    std::size_t unique_count,
    std::size_t accepted_count,
    std::size_t below_min_count,
    std::size_t above_max_or_unreachable_count,
    gro::Time time_origin) {
    std::ofstream metadata(options.output_dir / "metadata.json");
    if (!metadata) {
        fail("Cannot write metadata.json");
    }

    metadata
        << "{\n"
        << "  \"source\": \"T-Drive Beijing taxi GPS trajectories\",\n"
        << "  \"graph\": \"" << json_escape(options.graph_path) << "\",\n"
        << "  \"coordinates\": \"" << json_escape(options.coords_path) << "\",\n"
        << "  \"coarse_candidates\": \"" << json_escape(options.candidates_path.string()) << "\",\n"
        << "  \"output_dir\": \"" << json_escape(options.output_dir.string()) << "\",\n"
        << "  \"naming\": \"BJRealRep{amplification_factor}-{set_id}.txt\",\n"
        << "  \"sets\": " << options.sets << ",\n"
        << "  \"queries_per_base_set\": " << options.queries_per_set << ",\n"
        << "  \"rep_values\": [";
    for (std::size_t i = 0; i < options.rep_values.size(); ++i) {
        if (i > 0) {
            metadata << ", ";
        }
        metadata << options.rep_values[i];
    }
    metadata
        << "],\n"
        << "  \"min_free_flow_min\": " << options.min_free_flow_seconds / 60.0 << ",\n"
        << "  \"max_free_flow_min\": " << options.max_free_flow_seconds / 60.0 << ",\n"
        << "  \"coarse_candidate_segments_seen\": " << coarse_count << ",\n"
        << "  \"unique_od_count\": " << unique_count << ",\n"
        << "  \"candidate_segments_seen\": " << accepted_count << ",\n"
        << "  \"below_min_free_flow_count\": " << below_min_count << ",\n"
        << "  \"above_max_or_unreachable_count\": " << above_max_or_unreachable_count << ",\n"
        << "  \"time_origin_seconds\": " << time_origin << "\n"
        << "}\n";

    std::ofstream summary(options.output_dir / "query_set_summary.csv");
    if (!summary) {
        fail("Cannot write query_set_summary.csv");
    }
    summary
        << "dataset,base_query_count,min_departure,max_departure,"
        << "mean_duration_sec,mean_haversine_km,mean_free_flow_sec\n";
    for (std::size_t set_id = 0; set_id < selected_sets.size(); ++set_id) {
        const auto& selected = selected_sets[set_id];
        gro::Time min_departure = std::numeric_limits<gro::Time>::max();
        gro::Time max_departure = 0;
        long double duration_sum = 0.0;
        long double haversine_sum = 0.0;
        long double free_flow_sum = 0.0;
        for (const SelectedCandidate& item : selected) {
            min_departure = std::min(min_departure, item.candidate.departure_abs_seconds);
            max_departure = std::max(max_departure, item.candidate.departure_abs_seconds);
            duration_sum += item.candidate.duration_seconds;
            haversine_sum += item.candidate.haversine_km;
            free_flow_sum += item.free_flow_seconds;
        }
        long double n = static_cast<long double>(selected.size());
        summary
            << "BJReal-" << set_id << ','
            << selected.size() << ','
            << min_departure << ','
            << max_departure << ','
            << static_cast<double>(duration_sum / n) << ','
            << static_cast<double>(haversine_sum / n) << ','
            << static_cast<double>(free_flow_sum / n) << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_args(argc, argv);
        const int required_candidates = options.sets * options.queries_per_set;

        auto total_start = gro::Clock::now();
        std::cerr << "[bj-filter] load_graph_start\n";
        gro::Graph graph = gro::read_graph(options.graph_path, options.coords_path);
        std::cerr
            << "[bj-filter] load_graph_done vertices=" << graph.vertex_count
            << " edges=" << graph.edges.size() << '\n';

        std::vector<Candidate> candidates;
        std::vector<std::pair<gro::NodeId, gro::NodeId>> unique_od;
        read_candidates(options.candidates_path, candidates, unique_od);
        std::cerr
            << "[bj-filter] candidates_loaded coarse=" << candidates.size()
            << " unique_od=" << unique_od.size() << '\n';

        if (static_cast<int>(candidates.size()) < required_candidates) {
            fail("Not enough coarse candidates for requested query sets");
        }

        int thread_count = options.threads;
#ifdef _OPENMP
        if (thread_count <= 0) {
            thread_count = omp_get_max_threads();
        }
        omp_set_num_threads(thread_count);
#else
        thread_count = 1;
#endif
        std::cerr << "[bj-filter] filter_start threads=" << thread_count << '\n';

        const gro::Cost infinity = std::numeric_limits<gro::Cost>::max();
        std::vector<gro::Cost> unique_distances(unique_od.size(), infinity);
        std::atomic<long long> processed{0};
        auto filter_start = gro::Clock::now();

#ifdef _OPENMP
#pragma omp parallel
        {
            Workspace workspace(static_cast<std::size_t>(graph.vertex_count));
#pragma omp for schedule(dynamic, 64)
            for (long long index = 0;
                 index < static_cast<long long>(unique_od.size());
                 ++index) {
                unique_distances[static_cast<std::size_t>(index)] =
                    bounded_shortest_path(
                        graph,
                        unique_od[static_cast<std::size_t>(index)].first,
                        unique_od[static_cast<std::size_t>(index)].second,
                        options.max_free_flow_seconds,
                        workspace);
                long long done = ++processed;
                if (options.progress_interval > 0 &&
                    done % options.progress_interval == 0) {
#pragma omp critical
                    {
                        std::cerr
                            << "[bj-filter] filter_progress processed=" << done
                            << "/" << unique_od.size()
                            << " elapsed_sec="
                            << gro::elapsed_us(filter_start) / 1000000.0
                            << '\n';
                    }
                }
            }
        }
#else
        {
            Workspace workspace(static_cast<std::size_t>(graph.vertex_count));
            for (std::size_t index = 0; index < unique_od.size(); ++index) {
                unique_distances[index] =
                    bounded_shortest_path(
                        graph,
                        unique_od[index].first,
                        unique_od[index].second,
                        options.max_free_flow_seconds,
                        workspace);
                long long done = ++processed;
                if (options.progress_interval > 0 &&
                    done % options.progress_interval == 0) {
                    std::cerr
                        << "[bj-filter] filter_progress processed=" << done
                        << "/" << unique_od.size()
                        << " elapsed_sec="
                        << gro::elapsed_us(filter_start) / 1000000.0
                        << '\n';
                }
            }
        }
#endif

        std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(options.random_seed));
        std::vector<SelectedCandidate> reservoir;
        reservoir.reserve(static_cast<std::size_t>(required_candidates));
        std::size_t accepted_count = 0;
        std::size_t below_min_count = 0;
        std::size_t above_max_or_unreachable_count = 0;
        for (const Candidate& candidate : candidates) {
            gro::Cost distance = unique_distances[static_cast<std::size_t>(candidate.unique_od)];
            if (distance == infinity || distance > options.max_free_flow_seconds) {
                ++above_max_or_unreachable_count;
                continue;
            }
            if (distance < options.min_free_flow_seconds) {
                ++below_min_count;
                continue;
            }
            ++accepted_count;
            SelectedCandidate selected{candidate, distance};
            if (static_cast<int>(reservoir.size()) < required_candidates) {
                reservoir.push_back(selected);
            } else {
                std::uniform_int_distribution<std::size_t> distribution(
                    0,
                    accepted_count - 1);
                std::size_t replacement = distribution(rng);
                if (replacement < reservoir.size()) {
                    reservoir[replacement] = selected;
                }
            }
        }

        std::cerr
            << "[bj-filter] filter_done accepted=" << accepted_count
            << " reservoir=" << reservoir.size()
            << " below_min=" << below_min_count
            << " above_max_or_unreachable=" << above_max_or_unreachable_count
            << " sec=" << gro::elapsed_us(filter_start) / 1000000.0
            << '\n';

        if (static_cast<int>(reservoir.size()) < required_candidates) {
            fail("Only accepted " + std::to_string(reservoir.size()) +
                 " candidates, but " + std::to_string(required_candidates) +
                 " are required");
        }

        std::shuffle(reservoir.begin(), reservoir.end(), rng);
        std::vector<std::vector<SelectedCandidate>> selected_sets;
        selected_sets.reserve(static_cast<std::size_t>(options.sets));
        for (int set_id = 0; set_id < options.sets; ++set_id) {
            auto first = reservoir.begin() + set_id * options.queries_per_set;
            auto last = first + options.queries_per_set;
            selected_sets.emplace_back(first, last);
        }

        gro::Time time_origin = std::numeric_limits<gro::Time>::max();
        for (const auto& selected : selected_sets) {
            for (const SelectedCandidate& item : selected) {
                time_origin = std::min(time_origin, item.candidate.departure_abs_seconds);
            }
        }

        std::filesystem::create_directories(options.output_dir);
        for (std::size_t set_id = 0; set_id < selected_sets.size(); ++set_id) {
            for (int rep : options.rep_values) {
                std::filesystem::path path =
                    options.output_dir /
                    ("BJRealRep" + std::to_string(rep) + "-" +
                     std::to_string(set_id) + ".txt");
                write_query_file(
                    path,
                    selected_sets[set_id],
                    rep,
                    time_origin,
                    rng,
                    options.jitter_seconds);
                std::cerr
                    << "[bj-filter] wrote " << path
                    << " base=" << selected_sets[set_id].size()
                    << " total=" << selected_sets[set_id].size() * static_cast<std::size_t>(rep)
                    << '\n';
            }
        }

        write_metadata(
            options,
            selected_sets,
            candidates.size(),
            unique_od.size(),
            accepted_count,
            below_min_count,
            above_max_or_unreachable_count,
            time_origin);

        std::cerr
            << "[bj-filter] done total_sec="
            << gro::elapsed_us(total_start) / 1000000.0
            << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
