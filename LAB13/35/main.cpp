#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Edge {
    std::string to;
    int weight = 0;
};

using Graph = std::map<std::string, std::vector<Edge>>;

std::string read_all(std::istream &is) {
    std::ostringstream oss;
    oss << is.rdbuf();
    return oss.str();
}

void add_edge(Graph &graph, const std::string &from, const std::string &to, int weight) {
    if (weight < 0) {
        throw std::invalid_argument("Dijkstra does not allow negative weights");
    }
    graph[from].push_back({to, weight});
    graph.try_emplace(to);
}

Graph parse_graph(const std::string &text, bool directed) {
    static const std::regex triple_pattern(
        R"(\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*([0-9]+)\s*\))");

    Graph graph;
    auto begin = std::sregex_iterator(text.begin(), text.end(), triple_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::string from = (*it)[1].str();
        const std::string to = (*it)[2].str();
        const int weight = std::stoi((*it)[3].str());
        add_edge(graph, from, to, weight);
        if (!directed) {
            add_edge(graph, to, from, weight);
        }
    }

    if (graph.empty()) {
        throw std::invalid_argument("expected triples such as (R1,R2,3)");
    }
    return graph;
}

struct Result {
    std::map<std::string, int> distance;
    std::map<std::string, std::string> previous;
};

Result dijkstra(const Graph &graph, const std::string &source) {
    if (!graph.count(source)) {
        throw std::invalid_argument("source router does not exist in the graph");
    }

    constexpr int kInfinity = std::numeric_limits<int>::max() / 4;
    Result result;
    for (const auto &item : graph) {
        result.distance[item.first] = kInfinity;
    }
    result.distance[source] = 0;

    using QueueItem = std::pair<int, std::string>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    queue.push({0, source});

    while (!queue.empty()) {
        const auto [distance, node] = queue.top();
        queue.pop();
        if (distance != result.distance[node]) {
            continue;
        }

        for (const Edge &edge : graph.at(node)) {
            const int candidate = distance + edge.weight;
            if (candidate < result.distance[edge.to]) {
                result.distance[edge.to] = candidate;
                result.previous[edge.to] = node;
                queue.push({candidate, edge.to});
            }
        }
    }

    return result;
}

std::vector<std::string> build_path(const std::map<std::string, std::string> &previous,
                                    const std::string &source,
                                    const std::string &target) {
    std::vector<std::string> path;
    for (std::string current = target;;) {
        path.push_back(current);
        if (current == source) {
            break;
        }
        const auto it = previous.find(current);
        if (it == previous.end()) {
            path.clear();
            break;
        }
        current = it->second;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void print_results(const Graph &graph, const Result &result, const std::string &source) {
    std::cout << "Source: " << source << "\n";
    for (const auto &item : graph) {
        const std::string &target = item.first;
        std::cout << target << ": ";
        if (result.distance.at(target) >= std::numeric_limits<int>::max() / 8) {
            std::cout << "unreachable\n";
            continue;
        }

        std::cout << "distance=" << result.distance.at(target) << ", path=";
        const std::vector<std::string> path = build_path(result.previous, source, target);
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0) {
                std::cout << "->";
            }
            std::cout << path[i];
        }
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        bool directed = false;
        std::string source;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--directed") {
                directed = true;
            } else if (source.empty()) {
                source = arg;
            } else {
                throw std::invalid_argument("Usage: dijkstra [--directed] <source>");
            }
        }
        if (source.empty()) {
            throw std::invalid_argument("Usage: dijkstra [--directed] <source>");
        }

        const Graph graph = parse_graph(read_all(std::cin), directed);
        const Result result = dijkstra(graph, source);
        print_results(graph, result, source);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
