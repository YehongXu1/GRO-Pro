#include "gro.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>

namespace gro {
namespace {

std::string seconds_text(long long microseconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << static_cast<double>(microseconds) / 1000000.0;
    return out.str();
}

std::vector<QueryId> select_normal_baseline_queries(
    std::size_t query_count,
    int fraction_to_reroute) {
    int clamped_fraction = std::clamp(fraction_to_reroute, 0, 100);
    std::vector<QueryId> query_ids(query_count);
    std::iota(query_ids.begin(), query_ids.end(), 0);
    std::shuffle(
        query_ids.begin(),
        query_ids.end(),
        std::mt19937{std::random_device{}()});

    std::size_t reroute_count =
        static_cast<std::size_t>(clamped_fraction) *
        query_count / 100;
    if (reroute_count < query_ids.size()) {
        query_ids.resize(reroute_count);
    }
    return query_ids;
}

void log_baseline_initial_summary(
    bool enabled,
    long long initial_routes_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "BASELINE_INITIAL,"
              << "initial_routes_sec=" << seconds_text(initial_routes_us) << '\n';
}

void log_baseline_iteration_summary(
    bool enabled,
    int iteration,
    long long selected_count,
    long long reroute_count,
    Cost total_travel_time,
    long long evaluate_us,
    long long reroute_us,
    long long iteration_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "BASELINE,"
              << "iteration=" << iteration << ','
              << "selected_count=" << selected_count << ','
              << "reroute_count=" << reroute_count << ','
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "reroute_sec=" << seconds_text(reroute_us) << ','
              << "iteration_sec=" << seconds_text(iteration_us) << '\n';
}

void log_baseline_final_summary(
    bool enabled,
    Cost total_travel_time,
    long long evaluate_us,
    long long run_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "BASELINE_FINAL,"
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "run_sec=" << seconds_text(run_us) << '\n';
}

void log_selection_td_initial(
    bool enabled,
    long long initial_routes_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "SELECTION_TD_INITIAL,"
              << "initial_routes_sec=" << seconds_text(initial_routes_us) << '\n';
}

void log_selection_td_iteration(
    bool enabled,
    int iteration,
    long long tdg_size,
    long long candidate_count,
    long long selected_count,
    long long reroute_count,
    Cost total_travel_time,
    long long evaluate_us,
    long long tdg_us,
    long long impact_us,
    long long candidate_us,
    long long select_us,
    long long reroute_us,
    long long iteration_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "SELECTION_TD,"
              << "iteration=" << iteration << ','
              << "tdg_size=" << tdg_size << ','
              << "candidate_count=" << candidate_count << ','
              << "selected_count=" << selected_count << ','
              << "reroute_count=" << reroute_count << ','
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "tdg_sec=" << seconds_text(tdg_us) << ','
              << "impact_sec=" << seconds_text(impact_us) << ','
              << "candidate_sec=" << seconds_text(candidate_us) << ','
              << "select_sec=" << seconds_text(select_us) << ','
              << "reroute_sec=" << seconds_text(reroute_us) << ','
              << "iteration_sec=" << seconds_text(iteration_us) << '\n';
}

void log_selection_td_final(
    bool enabled,
    Cost total_travel_time,
    long long evaluate_us,
    long long run_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "SELECTION_TD_FINAL,"
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "run_sec=" << seconds_text(run_us) << '\n';
}

void log_normal_selection_gro_initial(
    bool enabled,
    long long initial_routes_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "NORMAL_SELECTION_GRO_REROUTE_INITIAL,"
              << "initial_routes_sec=" << seconds_text(initial_routes_us) << '\n';
}

void log_normal_selection_gro_iteration(
    bool enabled,
    int iteration,
    long long tdg_size,
    long long selected_count,
    long long batch_count,
    long long reroute_count,
    Cost total_travel_time,
    long long evaluate_us,
    long long tdg_us,
    long long impact_us,
    long long select_us,
    long long batch_us,
    long long reroute_us,
    long long iteration_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "NORMAL_SELECTION_GRO_REROUTE,"
              << "iteration=" << iteration << ','
              << "tdg_size=" << tdg_size << ','
              << "selected_count=" << selected_count << ','
              << "batch_count=" << batch_count << ','
              << "reroute_count=" << reroute_count << ','
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "tdg_sec=" << seconds_text(tdg_us) << ','
              << "impact_sec=" << seconds_text(impact_us) << ','
              << "select_sec=" << seconds_text(select_us) << ','
              << "batch_sec=" << seconds_text(batch_us) << ','
              << "reroute_sec=" << seconds_text(reroute_us) << ','
              << "iteration_sec=" << seconds_text(iteration_us) << '\n';
}

void log_normal_selection_gro_final(
    bool enabled,
    Cost total_travel_time,
    long long evaluate_us,
    long long run_us) {
    if (!enabled) {
        return;
    }

    std::cerr << "NORMAL_SELECTION_GRO_REROUTE_FINAL,"
              << "total_travel_time=" << total_travel_time << ','
              << "evaluate_sec=" << seconds_text(evaluate_us) << ','
              << "run_sec=" << seconds_text(run_us) << '\n';
}

}  // namespace

AlgorithmResult GROAlgorithm::run_baseline_gro(
    const std::vector<Query>& queries) const {
    auto total_start = Clock::now();
    auto phase_start = Clock::now();

    AlgorithmResult result;
    std::vector<Route> routes = compute_initial_routes(queries);
    log_baseline_initial_summary(
        options_.enable_timing_log,
        elapsed_us(phase_start));
    result.initial_routes = routes;

    TrafficResult traffic_result;
    for (int iteration = 0; iteration < options_.max_iterations; ++iteration) {
        auto iteration_start = Clock::now();

        phase_start = Clock::now();
        traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
        long long evaluate_us = elapsed_us(phase_start);
        result.total_travel_time_by_iteration.push_back(
            traffic_result.total_travel_time);
        if (iteration == 0) {
            result.initial_total_travel_time = traffic_result.total_travel_time;
        }

        phase_start = Clock::now();
        std::vector<QueryId> query_ids =
            select_normal_baseline_queries(
                queries.size(),
                options_.baseline_fraction_to_reroute);
        long long select_us = elapsed_us(phase_start);
        (void)select_us;

        phase_start = Clock::now();
        std::vector<Route> new_routes =
            baseline_reroute_queries(query_ids, queries, traffic_result);
        long long reroute_us = elapsed_us(phase_start);

        for (const Route& route : new_routes) {
            routes[route.query_id] = route;
        }
        log_baseline_iteration_summary(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(query_ids.size()),
            static_cast<long long>(new_routes.size()),
            traffic_result.total_travel_time,
            evaluate_us,
            reroute_us,
            elapsed_us(iteration_start));
    }

    phase_start = Clock::now();
    traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
    long long final_evaluate_us = elapsed_us(phase_start);

    result.final_routes = routes;
    result.final_total_travel_time = traffic_result.total_travel_time;
    result.total_travel_time_by_iteration.push_back(
        traffic_result.total_travel_time);
    log_baseline_final_summary(
        options_.enable_timing_log,
        traffic_result.total_travel_time,
        final_evaluate_us,
        elapsed_us(total_start));
    return result;
}

AlgorithmResult GROAlgorithm::run_selection_td_baseline(
    const std::vector<Query>& queries) const {
    auto total_start = Clock::now();
    auto phase_start = Clock::now();

    AlgorithmResult result;
    std::vector<Route> routes = compute_initial_routes(queries);
    log_selection_td_initial(
        options_.enable_timing_log,
        elapsed_us(phase_start));
    result.initial_routes = routes;

    TrafficResult traffic_result;
    for (int iteration = 0; iteration < options_.max_iterations; ++iteration) {
        auto iteration_start = Clock::now();

        phase_start = Clock::now();
        traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
        long long evaluate_us = elapsed_us(phase_start);
        result.total_travel_time_by_iteration.push_back(
            traffic_result.total_travel_time);
        if (iteration == 0) {
            result.initial_total_travel_time = traffic_result.total_travel_time;
        }

        phase_start = Clock::now();
        TrafficDependencyGraph tdg = build_tdg(traffic_result);
        long long tdg_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<Cost> node_impacts = compute_tdg_impact(tdg);
        long long impact_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::unordered_set<QueryId> candidate_query_ids =
            select_candidates(queries, traffic_result, tdg, node_impacts);
        long long candidate_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<QueryId> selected_query_ids =
            select_queries(
                candidate_query_ids,
                queries,
                traffic_result,
                tdg,
                node_impacts,
                iteration);
        long long select_us = elapsed_us(phase_start);

        if (selected_query_ids.empty()) {
            log_selection_td_iteration(
                options_.enable_timing_log,
                iteration,
                static_cast<long long>(tdg.nodes.size()),
                static_cast<long long>(candidate_query_ids.size()),
                0,
                0,
                traffic_result.total_travel_time,
                evaluate_us,
                tdg_us,
                impact_us,
                candidate_us,
                select_us,
                0,
                elapsed_us(iteration_start));
            break;
        }

        phase_start = Clock::now();
        std::vector<Route> new_routes =
            baseline_reroute_queries(selected_query_ids, queries, traffic_result);
        long long reroute_us = elapsed_us(phase_start);

        for (const Route& route : new_routes) {
            routes[route.query_id] = route;
        }

        log_selection_td_iteration(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(tdg.nodes.size()),
            static_cast<long long>(candidate_query_ids.size()),
            static_cast<long long>(selected_query_ids.size()),
            static_cast<long long>(new_routes.size()),
            traffic_result.total_travel_time,
            evaluate_us,
            tdg_us,
            impact_us,
            candidate_us,
            select_us,
            reroute_us,
            elapsed_us(iteration_start));
    }

    phase_start = Clock::now();
    traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
    long long final_evaluate_us = elapsed_us(phase_start);

    result.final_routes = routes;
    result.final_total_travel_time = traffic_result.total_travel_time;
    result.total_travel_time_by_iteration.push_back(
        traffic_result.total_travel_time);

    log_selection_td_final(
        options_.enable_timing_log,
        traffic_result.total_travel_time,
        final_evaluate_us,
        elapsed_us(total_start));
    return result;
}

AlgorithmResult GROAlgorithm::run_normal_selection_gro_reroute_baseline(
    const std::vector<Query>& queries) const {
    auto total_start = Clock::now();
    auto phase_start = Clock::now();

    AlgorithmResult result;
    std::vector<Route> routes = compute_initial_routes(queries);
    log_normal_selection_gro_initial(
        options_.enable_timing_log,
        elapsed_us(phase_start));
    result.initial_routes = routes;

    TrafficResult traffic_result;
    for (int iteration = 0; iteration < options_.max_iterations; ++iteration) {
        auto iteration_start = Clock::now();

        phase_start = Clock::now();
        traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
        long long evaluate_us = elapsed_us(phase_start);
        result.total_travel_time_by_iteration.push_back(
            traffic_result.total_travel_time);
        if (iteration == 0) {
            result.initial_total_travel_time = traffic_result.total_travel_time;
        }

        phase_start = Clock::now();
        TrafficDependencyGraph tdg = build_tdg(traffic_result);
        long long tdg_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<Cost> node_impacts = compute_tdg_impact(tdg);
        long long impact_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<QueryId> selected_query_ids =
            select_normal_baseline_queries(
                queries.size(),
                options_.baseline_fraction_to_reroute);
        long long select_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<std::vector<QueryId>> query_batches =
            batch_queries(selected_query_ids, tdg, traffic_result, iteration);
        long long batch_us = elapsed_us(phase_start);

        phase_start = Clock::now();
        std::vector<Route> new_routes =
            reroute_queries(
                query_batches,
                queries,
                traffic_result,
                tdg,
                node_impacts,
                iteration);
        long long reroute_us = elapsed_us(phase_start);

        for (const Route& route : new_routes) {
            routes[route.query_id] = route;
        }

        log_normal_selection_gro_iteration(
            options_.enable_timing_log,
            iteration,
            static_cast<long long>(tdg.nodes.size()),
            static_cast<long long>(selected_query_ids.size()),
            static_cast<long long>(query_batches.size()),
            static_cast<long long>(new_routes.size()),
            traffic_result.total_travel_time,
            evaluate_us,
            tdg_us,
            impact_us,
            select_us,
            batch_us,
            reroute_us,
            elapsed_us(iteration_start));
    }

    phase_start = Clock::now();
    traffic_result = evaluate_traffic(graph_, queries, routes, traffic_options_);
    long long final_evaluate_us = elapsed_us(phase_start);

    result.final_routes = routes;
    result.final_total_travel_time = traffic_result.total_travel_time;
    result.total_travel_time_by_iteration.push_back(
        traffic_result.total_travel_time);

    log_normal_selection_gro_final(
        options_.enable_timing_log,
        traffic_result.total_travel_time,
        final_evaluate_us,
        elapsed_us(total_start));
    return result;
}

}  // namespace gro
