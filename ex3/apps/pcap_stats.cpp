#include "ex3/pcap_lab.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

namespace {

struct WindowKey {
    std::time_t minute_start = 0;
    std::string direction;
    std::string address_type;
    std::string address;

    bool operator<(const WindowKey& other) const {
        return std::tie(minute_start, direction, address_type, address) <
               std::tie(other.minute_start, other.direction, other.address_type, other.address);
    }
};

struct Options {
    ex3::CaptureOptions capture;
    std::string packets_csv = "ipv4_packets.csv";
    std::string windows_csv = "minute_stats.csv";
};

void print_usage() {
    std::cerr
        << "usage: pcap_stats (--read FILE | --iface NAME) [--filter EXPR] "
        << "[--packets FILE] [--windows FILE]\n";
}

std::string format_minute(std::time_t value) {
    timeval ts {};
    ts.tv_sec = value;
    return ex3::format_timestamp(ts);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    options.capture.filter_expr = "ip";

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        const auto next_value = [&](const std::string& flag) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for " + flag);
            }
            return argv[++index];
        };

        if (arg == "--read") {
            options.capture.read_file = next_value(arg);
        } else if (arg == "--iface") {
            options.capture.interface_name = next_value(arg);
        } else if (arg == "--filter") {
            options.capture.filter_expr = next_value(arg);
        } else if (arg == "--packets") {
            options.packets_csv = next_value(arg);
        } else if (arg == "--windows") {
            options.windows_csv = next_value(arg);
        } else {
            print_usage();
            return 1;
        }
    }

    if (options.capture.read_file.empty() == options.capture.interface_name.empty()) {
        print_usage();
        return 1;
    }

    std::ofstream packet_out(options.packets_csv);
    std::ofstream window_out(options.windows_csv);
    if (!packet_out || !window_out) {
        std::cerr << "failed to open output files\n";
        return 1;
    }

    ex3::write_csv_row(packet_out, {"time", "src_mac", "src_ip", "dst_mac", "dst_ip", "frame_len"});

    std::map<WindowKey, std::uint64_t> byte_totals;
    std::size_t ipv4_records = 0;
    std::string error;
    const bool ok = ex3::run_capture(options.capture, [&](const ex3::PacketView& packet) {
        if (packet.src_ip.empty() || packet.dst_ip.empty()) {
            return true;
        }

        ++ipv4_records;
        ex3::write_csv_row(packet_out,
                           {ex3::format_timestamp(packet.timestamp),
                            packet.src_mac,
                            packet.src_ip,
                            packet.dst_mac,
                            packet.dst_ip,
                            std::to_string(packet.frame_length)});

        const std::time_t minute_start = packet.timestamp.tv_sec - (packet.timestamp.tv_sec % 60);
        const auto accumulate = [&](const std::string& direction,
                                    const std::string& address_type,
                                    const std::string& address) {
            if (address.empty()) {
                return;
            }
            byte_totals[{minute_start, direction, address_type, address}] += packet.frame_length;
        };

        accumulate("SRC", "MAC", packet.src_mac);
        accumulate("DST", "MAC", packet.dst_mac);
        accumulate("SRC", "IP", packet.src_ip);
        accumulate("DST", "IP", packet.dst_ip);
        return true;
    }, &error);

    if (!ok) {
        std::cerr << "capture failed: " << error << '\n';
        return 1;
    }

    ex3::write_csv_row(window_out,
                       {"window_start", "window_end", "direction", "address_type", "address", "total_bytes"});
    for (const auto& [key, total_bytes] : byte_totals) {
        ex3::write_csv_row(window_out,
                           {format_minute(key.minute_start),
                            format_minute(key.minute_start + 60),
                            key.direction,
                            key.address_type,
                            key.address,
                            std::to_string(total_bytes)});
    }

    std::cout << "wrote " << ipv4_records << " IPv4 packet rows to " << options.packets_csv
              << " and " << byte_totals.size() << " window rows to " << options.windows_csv << '\n';
    return 0;
}
