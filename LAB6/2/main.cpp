#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr double kCoverageThreshold = -65.0;
constexpr double kStrongCoverageThreshold = -50.0;
constexpr double kMinDistance = 0.5;
constexpr double kFloorPenaltyPerLevel = 6.0;

enum class Band {
    B24,
    B5
};

struct Config {
    double width = 100.0;
    double depth = 80.0;
    int floors = 3;
    double floor_height = 3.5;
    double corridor_width = 2.0;
    double classroom_width = 15.0;
    double classroom_depth = 10.0;
    double bearing_loss = 12.0;
    double partition_loss = 6.0;
    double glass_loss = 7.0;
    double grid_step = 5.0;
    int max_ap_count = 21;
    double candidate_spacing = 12.0;
};

struct Point3D {
    double x = 0.0;
    double y = 0.0;
    int floor = 0;
};

struct Cell {
    Point3D position;
    bool in_corridor = false;
    bool near_glass = false;
};

struct Candidate {
    Point3D position;
    Band band = Band::B24;
};

struct Deployment {
    Point3D position;
    Band band = Band::B24;
    int channel = 0;
};

struct SignalStats {
    std::vector<double> best_signal;
    std::vector<int> overlap_count;
};

std::string trim(const std::string &text) {
    const std::string whitespace = " \t\r\n";
    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

std::string to_lower(std::string text) {
    for (char &ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

Config parse_config(std::istream &input) {
    Config config;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::string key;
        std::string value;
        const std::size_t equal_pos = line.find('=');
        if (equal_pos != std::string::npos) {
            key = trim(line.substr(0, equal_pos));
            value = trim(line.substr(equal_pos + 1));
        } else {
            std::istringstream iss(line);
            if (!(iss >> key >> value)) {
                continue;
            }
        }

        key = to_lower(key);
        if (key == "width") {
            config.width = std::stod(value);
        } else if (key == "depth") {
            config.depth = std::stod(value);
        } else if (key == "floors") {
            config.floors = std::stoi(value);
        } else if (key == "floor_height") {
            config.floor_height = std::stod(value);
        } else if (key == "corridor_width") {
            config.corridor_width = std::stod(value);
        } else if (key == "classroom_width") {
            config.classroom_width = std::stod(value);
        } else if (key == "classroom_depth") {
            config.classroom_depth = std::stod(value);
        } else if (key == "bearing_loss") {
            config.bearing_loss = std::stod(value);
        } else if (key == "partition_loss") {
            config.partition_loss = std::stod(value);
        } else if (key == "glass_loss") {
            config.glass_loss = std::stod(value);
        } else if (key == "grid_step") {
            config.grid_step = std::stod(value);
        } else if (key == "max_ap_count") {
            config.max_ap_count = std::stoi(value);
        } else if (key == "candidate_spacing") {
            config.candidate_spacing = std::stod(value);
        }
    }

    return config;
}

std::string band_name(Band band) {
    return band == Band::B24 ? "2.4GHz" : "5GHz";
}

double reference_radius(Band band) {
    return band == Band::B24 ? 48.0 : 30.0;
}

double signal_strength(const Point3D &ap, const Cell &cell, Band band, const Config &config) {
    const double dx = ap.x - cell.position.x;
    const double dy = ap.y - cell.position.y;
    const double dz = (static_cast<double>(ap.floor) - static_cast<double>(cell.position.floor)) *
                      config.floor_height;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double radius = reference_radius(band);

    double signal = kCoverageThreshold + 20.0 * std::log10(radius / std::max(distance, kMinDistance));
    const int floor_gap = std::abs(ap.floor - cell.position.floor);
    signal -= floor_gap * kFloorPenaltyPerLevel;

    const double corridor_y_min = (config.depth - config.corridor_width) / 2.0;
    const double corridor_y_max = (config.depth + config.corridor_width) / 2.0;
    const bool ap_in_corridor = ap.y >= corridor_y_min && ap.y <= corridor_y_max;
    const bool cell_in_corridor = cell.position.y >= corridor_y_min && cell.position.y <= corridor_y_max;
    if (ap_in_corridor != cell_in_corridor) {
        signal -= config.partition_loss;
    } else if (!ap_in_corridor && !cell_in_corridor &&
               ((ap.y < corridor_y_min && cell.position.y > corridor_y_max) ||
                (cell.position.y < corridor_y_min && ap.y > corridor_y_max))) {
        signal -= config.partition_loss * 2.0;
    }

    const int partitions_crossed =
        static_cast<int>(std::abs(std::floor(ap.x / config.classroom_width) -
                                  std::floor(cell.position.x / config.classroom_width)));
    signal -= partitions_crossed * config.partition_loss;

    const int bearing_walls_crossed =
        static_cast<int>(std::abs(std::floor(ap.x / (config.classroom_width * 3.0)) -
                                  std::floor(cell.position.x / (config.classroom_width * 3.0))));
    signal -= bearing_walls_crossed * config.bearing_loss;

    if (cell.near_glass) {
        signal -= config.glass_loss * 0.25;
    }

    return signal;
}

std::vector<Cell> build_cells(const Config &config) {
    std::vector<Cell> cells;
    const double corridor_y_min = (config.depth - config.corridor_width) / 2.0;
    const double corridor_y_max = (config.depth + config.corridor_width) / 2.0;

    for (int floor = 0; floor < config.floors; ++floor) {
        for (double y = config.grid_step / 2.0; y < config.depth; y += config.grid_step) {
            for (double x = config.grid_step / 2.0; x < config.width; x += config.grid_step) {
                Cell cell;
                cell.position = {x, y, floor};
                cell.in_corridor = (y >= corridor_y_min && y <= corridor_y_max);
                cell.near_glass = (x < config.grid_step || x > config.width - config.grid_step ||
                                   y < config.grid_step || y > config.depth - config.grid_step);
                cells.push_back(cell);
            }
        }
    }
    return cells;
}

std::vector<Candidate> build_candidates(const Config &config) {
    std::set<std::tuple<int, int, int, int>> seen;
    std::vector<Candidate> candidates;
    const double corridor_center = config.depth / 2.0;
    const double upper_class_y = (config.depth + config.corridor_width) / 2.0 + config.classroom_depth / 2.0;
    const double lower_class_y = (config.depth - config.corridor_width) / 2.0 - config.classroom_depth / 2.0;

    auto add_candidate = [&](double x, double y, int floor, Band band) {
        if (x <= 0.0 || x >= config.width || y <= 0.0 || y >= config.depth) {
            return;
        }
        const auto key = std::make_tuple(static_cast<int>(std::round(x * 10.0)),
                                         static_cast<int>(std::round(y * 10.0)),
                                         floor,
                                         band == Band::B24 ? 24 : 50);
        if (!seen.insert(key).second) {
            return;
        }
        candidates.push_back({{x, y, floor}, band});
    };

    for (int floor = 0; floor < config.floors; ++floor) {
        for (double x = config.candidate_spacing / 2.0; x < config.width; x += config.candidate_spacing) {
            add_candidate(x, corridor_center, floor, Band::B24);
            add_candidate(x, corridor_center, floor, Band::B5);
        }
        for (double x = config.classroom_width / 2.0; x < config.width; x += config.classroom_width) {
            add_candidate(x, lower_class_y, floor, Band::B24);
            add_candidate(x, lower_class_y, floor, Band::B5);
            add_candidate(x, upper_class_y, floor, Band::B24);
            add_candidate(x, upper_class_y, floor, Band::B5);
        }
    }

    return candidates;
}

SignalStats evaluate_signals(const std::vector<Cell> &cells,
                             const std::vector<Deployment> &deployments,
                             const Config &config) {
    SignalStats stats;
    stats.best_signal.assign(cells.size(), -120.0);
    stats.overlap_count.assign(cells.size(), 0);

    for (std::size_t cell_index = 0; cell_index < cells.size(); ++cell_index) {
        for (const Deployment &deployment : deployments) {
            const double signal =
                signal_strength(deployment.position, cells[cell_index], deployment.band, config);
            stats.best_signal[cell_index] = std::max(stats.best_signal[cell_index], signal);
            if (signal >= kCoverageThreshold) {
                ++stats.overlap_count[cell_index];
            }
        }
    }

    return stats;
}

double candidate_score(const Candidate &candidate,
                       const std::vector<Cell> &cells,
                       const SignalStats &stats,
                       const Config &config) {
    double score = 0.0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const double signal = signal_strength(candidate.position, cells[i], candidate.band, config);
        if (signal < kCoverageThreshold) {
            continue;
        }

        if (stats.best_signal[i] < kCoverageThreshold) {
            score += 10.0;
        } else if (signal > stats.best_signal[i] + 3.0) {
            score += 2.0;
        }

        if (cells[i].in_corridor && candidate.band == Band::B5) {
            score += 4.0;
        }
        if (stats.overlap_count[i] >= 2) {
            score -= 1.5;
        }
        if (signal >= kStrongCoverageThreshold && stats.overlap_count[i] >= 1) {
            score -= 0.8;
        }
    }

    score -= (candidate.band == Band::B24 ? 7.0 : 5.0);
    return score;
}

std::vector<Deployment> select_deployments(const std::vector<Cell> &cells,
                                           const std::vector<Candidate> &candidates,
                                           const Config &config) {
    std::vector<Deployment> deployments;
    std::vector<bool> used(candidates.size(), false);
    SignalStats stats = evaluate_signals(cells, deployments, config);

    for (int selected = 0; selected < config.max_ap_count; ++selected) {
        double best_score = 0.0;
        int best_index = -1;

        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (used[i]) {
                continue;
            }
            const double score = candidate_score(candidates[i], cells, stats, config);
            if (score > best_score) {
                best_score = score;
                best_index = static_cast<int>(i);
            }
        }

        if (best_index < 0) {
            break;
        }

        used[best_index] = true;
        deployments.push_back({candidates[best_index].position, candidates[best_index].band, 0});
        stats = evaluate_signals(cells, deployments, config);

        int covered = 0;
        for (double signal : stats.best_signal) {
            if (signal >= kCoverageThreshold) {
                ++covered;
            }
        }
        const double coverage_ratio = cells.empty() ? 1.0 : static_cast<double>(covered) / cells.size();
        if (coverage_ratio >= 0.98) {
            break;
        }
    }

    return deployments;
}

void ensure_dual_band_presence(std::vector<Deployment> &deployments, const Config &config) {
    std::set<int> floors_with_5g;
    for (const Deployment &deployment : deployments) {
        if (deployment.band == Band::B5) {
            floors_with_5g.insert(deployment.position.floor);
        }
    }

    const double corridor_center = config.depth / 2.0;
    std::vector<Deployment> fallback_5g;
    for (int floor = 0; floor < config.floors; ++floor) {
        if (floors_with_5g.count(floor) != 0) {
            continue;
        }
        fallback_5g.push_back({{config.width / 2.0, corridor_center, floor}, Band::B5, 0});
    }

    for (const Deployment &fallback : fallback_5g) {
        if (static_cast<int>(deployments.size()) < config.max_ap_count) {
            deployments.push_back(fallback);
        }
    }

    if (!fallback_5g.empty() && static_cast<int>(deployments.size()) == config.max_ap_count) {
        std::size_t replacement_index = deployments.size();
        for (const Deployment &fallback : fallback_5g) {
            if (replacement_index == 0) {
                break;
            }
            --replacement_index;
            deployments[replacement_index] = fallback;
        }
    }
}

double planar_distance(const Point3D &lhs, const Point3D &rhs) {
    const double dx = lhs.x - rhs.x;
    const double dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

void assign_channels(std::vector<Deployment> &deployments) {
    const std::array<int, 3> channels_24 = {1, 6, 11};
    const std::array<int, 4> channels_5 = {36, 44, 149, 157};

    for (std::size_t i = 0; i < deployments.size(); ++i) {
        const std::vector<int> pool = deployments[i].band == Band::B24
                                          ? std::vector<int>(channels_24.begin(), channels_24.end())
                                          : std::vector<int>(channels_5.begin(), channels_5.end());
        const double conflict_distance = deployments[i].band == Band::B24 ? 28.0 : 20.0;

        int best_channel = pool.front();
        int best_conflict = std::numeric_limits<int>::max();
        for (int channel : pool) {
            int conflicts = 0;
            for (std::size_t j = 0; j < i; ++j) {
                if (deployments[j].band != deployments[i].band) {
                    continue;
                }
                if (deployments[j].position.floor != deployments[i].position.floor) {
                    continue;
                }
                if (deployments[j].channel != channel) {
                    continue;
                }
                if (planar_distance(deployments[j].position, deployments[i].position) < conflict_distance) {
                    ++conflicts;
                }
            }
            if (conflicts < best_conflict) {
                best_conflict = conflicts;
                best_channel = channel;
            }
        }
        deployments[i].channel = best_channel;
    }
}

void write_deployment_report(const std::vector<Deployment> &deployments,
                             const SignalStats &stats,
                             const std::vector<Cell> &cells) {
    std::ofstream output("deployment.txt");
    output << std::fixed << std::setprecision(1);
    output << "AP count: " << deployments.size() << "\n";
    for (std::size_t i = 0; i < deployments.size(); ++i) {
        output << "AP" << (i + 1) << ": floor=" << (deployments[i].position.floor + 1)
               << " x=" << deployments[i].position.x
               << " y=" << deployments[i].position.y
               << " band=" << band_name(deployments[i].band)
               << " channel=" << deployments[i].channel << "\n";
    }

    int covered = 0;
    int strong = 0;
    int overlap = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (stats.best_signal[i] >= kCoverageThreshold) {
            ++covered;
        }
        if (stats.best_signal[i] >= kStrongCoverageThreshold) {
            ++strong;
        }
        if (stats.overlap_count[i] >= 2) {
            ++overlap;
        }
    }

    const double total = cells.empty() ? 1.0 : static_cast<double>(cells.size());
    output << "Coverage ratio: " << covered / total << "\n";
    output << "Strong coverage ratio: " << strong / total << "\n";
    output << "Overlap ratio: " << overlap / total << "\n";
}

void write_heatmaps(const Config &config,
                    const std::vector<Cell> &cells,
                    const std::vector<Deployment> &deployments,
                    const SignalStats &stats) {
    const int width_pixels = static_cast<int>(std::ceil(config.width / config.grid_step));
    const int height_pixels = static_cast<int>(std::ceil(config.depth / config.grid_step));

    for (int floor = 0; floor < config.floors; ++floor) {
        std::vector<std::array<int, 3>> pixels(width_pixels * height_pixels, {0, 0, 0});

        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].position.floor != floor) {
                continue;
            }
            const int px = std::min(width_pixels - 1, static_cast<int>(cells[i].position.x / config.grid_step));
            const int py = std::min(height_pixels - 1, static_cast<int>(cells[i].position.y / config.grid_step));
            std::array<int, 3> color = {120, 120, 120};
            if (stats.best_signal[i] >= kStrongCoverageThreshold) {
                color = {50, 180, 80};
            } else if (stats.best_signal[i] >= kCoverageThreshold) {
                color = {255, 210, 60};
            } else {
                color = {220, 70, 70};
            }
            if (cells[i].in_corridor) {
                color[2] = std::min(255, color[2] + 30);
            }
            pixels[py * width_pixels + px] = color;
        }

        for (const Deployment &deployment : deployments) {
            if (deployment.position.floor != floor) {
                continue;
            }
            const int px =
                std::min(width_pixels - 1, static_cast<int>(deployment.position.x / config.grid_step));
            const int py =
                std::min(height_pixels - 1, static_cast<int>(deployment.position.y / config.grid_step));
            pixels[py * width_pixels + px] = deployment.band == Band::B24
                                                 ? std::array<int, 3>{40, 40, 255}
                                                 : std::array<int, 3>{255, 40, 255};
        }

        const std::string file_name = "heatmap_floor" + std::to_string(floor + 1) + ".ppm";
        std::ofstream image(file_name);
        image << "P3\n" << width_pixels << " " << height_pixels << "\n255\n";
        for (int py = 0; py < height_pixels; ++py) {
            for (int px = 0; px < width_pixels; ++px) {
                const auto &color = pixels[py * width_pixels + px];
                image << color[0] << " " << color[1] << " " << color[2] << "\n";
            }
        }
    }
}

}  // namespace

int main() {
    try {
        const Config config = parse_config(std::cin);
        const std::vector<Cell> cells = build_cells(config);
        const std::vector<Candidate> candidates = build_candidates(config);
        std::vector<Deployment> deployments = select_deployments(cells, candidates, config);
        ensure_dual_band_presence(deployments, config);
        assign_channels(deployments);
        const SignalStats stats = evaluate_signals(cells, deployments, config);

        int covered = 0;
        int strong = 0;
        int overlap = 0;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (stats.best_signal[i] >= kCoverageThreshold) {
                ++covered;
            }
            if (stats.best_signal[i] >= kStrongCoverageThreshold) {
                ++strong;
            }
            if (stats.overlap_count[i] >= 2) {
                ++overlap;
            }
        }

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "AP deployment result\n";
        std::cout << "Building: " << config.width << "m x " << config.depth << "m x "
                  << config.floors << " floors\n";
        std::cout << "Grid step: " << config.grid_step << "m\n";
        std::cout << "AP count: " << deployments.size() << "\n";
        for (std::size_t i = 0; i < deployments.size(); ++i) {
            std::cout << "AP" << (i + 1) << ": floor " << (deployments[i].position.floor + 1)
                      << ", (" << deployments[i].position.x << ", " << deployments[i].position.y << ")"
                      << ", band=" << band_name(deployments[i].band)
                      << ", channel=" << deployments[i].channel << "\n";
        }

        const double total = cells.empty() ? 1.0 : static_cast<double>(cells.size());
        std::cout << "Coverage ratio: " << (covered / total) * 100.0 << "%\n";
        std::cout << "Strong coverage ratio: " << (strong / total) * 100.0 << "%\n";
        std::cout << "Overlap ratio: " << (overlap / total) * 100.0 << "%\n";

        write_deployment_report(deployments, stats, cells);
        write_heatmaps(config, cells, deployments, stats);
        std::cout << "Generated files: deployment.txt";
        for (int floor = 1; floor <= config.floors; ++floor) {
            std::cout << ", heatmap_floor" << floor << ".ppm";
        }
        std::cout << "\n";
    } catch (const std::exception &ex) {
        std::cerr << "Execution error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
