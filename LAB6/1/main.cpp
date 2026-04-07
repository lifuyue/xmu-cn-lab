#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Frame {
    int source_address = 0;
    int source_port = 0;
    int destination_address = 0;
};

class Bridge {
public:
    std::string process_frame(const Frame &frame) {
        learn(frame.source_address, frame.source_port);
        const int other_port = frame.source_port == 1 ? 2 : 1;

        if (frame.destination_address == kBroadcastAddress) {
            return "broadcast -> port " + std::to_string(other_port);
        }

        const auto it = mac_table_.find(frame.destination_address);
        if (it == mac_table_.end()) {
            return "flood -> port " + std::to_string(other_port);
        }

        if (it->second == frame.source_port) {
            return "filter";
        }

        return "port " + std::to_string(it->second);
    }

    void print_mac_table(std::ostream &os) const {
        os << "MAC Table:\n";
        if (mac_table_.empty()) {
            os << "  (empty)\n";
            return;
        }

        for (const auto &[address, port] : mac_table_) {
            os << "  " << format_address(address) << " -> port " << port << "\n";
        }
    }

    static std::string format_address(int address) {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << address;
        return oss.str();
    }

private:
    static constexpr int kBroadcastAddress = 0xF;

    void learn(int source_address, int source_port) {
        if (source_address == kBroadcastAddress) {
            return;
        }
        mac_table_[source_address] = source_port;
    }

    std::map<int, int> mac_table_;
};

int parse_address(const std::string &token) {
    std::string normalized = token;
    for (char &ch : normalized) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    if (normalized.size() == 1 && std::isxdigit(static_cast<unsigned char>(normalized[0]))) {
        return std::stoi(normalized, nullptr, 16);
    }

    if (normalized.size() == 3 && normalized[0] == '0' && normalized[1] == 'X' &&
        std::isxdigit(static_cast<unsigned char>(normalized[2]))) {
        return std::stoi(normalized, nullptr, 16);
    }

    throw std::invalid_argument("invalid address token: " + token);
}

Frame parse_frame_line(const std::string &line) {
    std::istringstream iss(line);
    std::string src_addr;
    int src_port = 0;
    std::string dst_addr;

    if (!(iss >> src_addr >> src_port >> dst_addr)) {
        throw std::invalid_argument("expected: <src_addr> <src_port> <dst_addr>");
    }

    Frame frame;
    frame.source_address = parse_address(src_addr);
    frame.source_port = src_port;
    frame.destination_address = parse_address(dst_addr);

    if (frame.source_address < 0x0 || frame.source_address > 0xF ||
        frame.destination_address < 0x0 || frame.destination_address > 0xF) {
        throw std::invalid_argument("address must be within 0x0 to 0xF");
    }

    if (frame.source_port != 1 && frame.source_port != 2) {
        throw std::invalid_argument("source port must be 1 or 2");
    }

    return frame;
}

int main() {
    try {
        int frame_count = 0;
        if (!(std::cin >> frame_count)) {
            std::cerr << "Failed to read frame count.\n";
            return 1;
        }

        if (frame_count < 0) {
            std::cerr << "Frame count must be non-negative.\n";
            return 1;
        }

        std::string line;
        std::getline(std::cin, line);

        Bridge bridge;
        for (int index = 0; index < frame_count; ++index) {
            if (!std::getline(std::cin, line)) {
                std::cerr << "Missing frame line " << (index + 1) << ".\n";
                return 1;
            }

            const Frame frame = parse_frame_line(line);
            const std::string decision = bridge.process_frame(frame);

            std::cout << "Frame " << (index + 1) << ":\n";
            std::cout << "  Source: " << Bridge::format_address(frame.source_address)
                      << " on port " << frame.source_port << "\n";
            std::cout << "  Destination: " << Bridge::format_address(frame.destination_address)
                      << "\n";
            std::cout << "  Decision: " << decision << "\n";
            bridge.print_mac_table(std::cout);
            if (index + 1 != frame_count) {
                std::cout << "\n";
            }
        }
    } catch (const std::exception &ex) {
        std::cerr << "Input error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
