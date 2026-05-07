#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kRipInfinity = 16;

struct RouteEntry {
    std::string network;
    int distance = 0;
    std::string next_hop;
};

std::vector<RouteEntry> read_table(std::istream &is, const std::string &name) {
    int count = 0;
    if (!(is >> count)) {
        throw std::invalid_argument("missing route count for " + name);
    }
    if (count < 0) {
        throw std::invalid_argument("route count must be non-negative");
    }

    std::vector<RouteEntry> table;
    table.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        RouteEntry entry;
        if (!(is >> entry.network >> entry.distance >> entry.next_hop)) {
            throw std::invalid_argument("invalid route entry in " + name);
        }
        if (entry.distance < 0) {
            throw std::invalid_argument("distance must be non-negative");
        }
        entry.distance = std::min(entry.distance, kRipInfinity);
        table.push_back(entry);
    }

    return table;
}

void update_from_neighbor(std::vector<RouteEntry> &local_table,
                          const std::vector<RouteEntry> &neighbor_table,
                          const std::string &neighbor) {
    std::map<std::string, std::size_t> index_by_network;
    for (std::size_t i = 0; i < local_table.size(); ++i) {
        index_by_network[local_table[i].network] = i;
    }

    for (const RouteEntry &advertised : neighbor_table) {
        const int candidate_distance = std::min(advertised.distance + 1, kRipInfinity);
        const auto it = index_by_network.find(advertised.network);

        if (it == index_by_network.end()) {
            if (candidate_distance < kRipInfinity) {
                index_by_network[advertised.network] = local_table.size();
                local_table.push_back({advertised.network, candidate_distance, neighbor});
            }
            continue;
        }

        RouteEntry &current = local_table[it->second];
        if (current.next_hop == neighbor) {
            current.distance = candidate_distance;
        } else if (candidate_distance < current.distance) {
            current.distance = candidate_distance;
            current.next_hop = neighbor;
        }
    }

    std::sort(local_table.begin(), local_table.end(), [](const RouteEntry &lhs, const RouteEntry &rhs) {
        return lhs.network < rhs.network;
    });
}

void print_table(const std::vector<RouteEntry> &table) {
    std::cout << std::left << std::setw(12) << "Network"
              << std::setw(10) << "Distance"
              << "NextHop\n";
    std::cout << std::string(29, '-') << "\n";
    for (const RouteEntry &entry : table) {
        std::cout << std::left << std::setw(12) << entry.network
                  << std::setw(10) << entry.distance
                  << entry.next_hop << "\n";
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        const std::string neighbor = argc >= 2 ? argv[1] : "R2";
        if (argc > 2) {
            throw std::invalid_argument("Usage: rip_update [neighbor_router]");
        }

        std::vector<RouteEntry> local_table = read_table(std::cin, "local table");
        const std::vector<RouteEntry> neighbor_table = read_table(std::cin, "neighbor table");

        update_from_neighbor(local_table, neighbor_table, neighbor);
        print_table(local_table);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
