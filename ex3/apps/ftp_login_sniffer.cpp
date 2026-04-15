#include "ex3/pcap_lab.hpp"

#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

namespace {

struct SessionKey {
    std::string client_ip;
    std::uint16_t client_port = 0;
    std::string server_ip;
    std::uint16_t server_port = 21;

    bool operator<(const SessionKey& other) const {
        return std::tie(client_ip, client_port, server_ip, server_port) <
               std::tie(other.client_ip, other.client_port, other.server_ip, other.server_port);
    }
};

struct Attempt {
    std::string time;
    std::string src_mac;
    std::string src_ip;
    std::string dst_mac;
    std::string dst_ip;
    std::string username;
    std::string password;
};

struct Session {
    std::string current_username;
    std::string client_buffer;
    std::string server_buffer;
    std::deque<Attempt> pending;
};

struct Options {
    ex3::CaptureOptions capture;
    std::string output_csv = "ftp_logins.csv";
};

std::optional<std::pair<SessionKey, bool>> resolve_session(const ex3::PacketView& packet) {
    if (!packet.has_tcp) {
        return std::nullopt;
    }
    if (packet.dst_port == 21) {
        return std::pair<SessionKey, bool>{{packet.src_ip, packet.src_port, packet.dst_ip, packet.dst_port}, true};
    }
    if (packet.src_port == 21) {
        return std::pair<SessionKey, bool>{{packet.dst_ip, packet.dst_port, packet.src_ip, packet.src_port}, false};
    }
    return std::nullopt;
}

void print_usage() {
    std::cerr << "usage: ftp_login_sniffer (--read FILE | --iface NAME) [--filter EXPR] [--output FILE]\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    options.capture.filter_expr = "tcp port 21";

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
        } else if (arg == "--output") {
            options.output_csv = next_value(arg);
        } else {
            print_usage();
            return 1;
        }
    }

    if (options.capture.read_file.empty() == options.capture.interface_name.empty()) {
        print_usage();
        return 1;
    }

    std::ofstream out(options.output_csv);
    if (!out) {
        std::cerr << "failed to open " << options.output_csv << '\n';
        return 1;
    }
    ex3::write_csv_row(out, {"time", "src_mac", "src_ip", "dst_mac", "dst_ip", "username", "password", "result"});

    std::map<SessionKey, Session> sessions;
    std::size_t emitted = 0;
    std::string error;
    const bool ok = ex3::run_capture(options.capture, [&](const ex3::PacketView& packet) {
        const auto resolved = resolve_session(packet);
        if (!resolved.has_value() || packet.payload.empty()) {
            return true;
        }

        auto& [key, from_client] = *resolved;
        auto& session = sessions[key];
        std::string& buffer = from_client ? session.client_buffer : session.server_buffer;
        buffer.append(reinterpret_cast<const char*>(packet.payload.data()), packet.payload.size());

        for (const std::string& raw_line : ex3::drain_lines(buffer)) {
            const std::string line = ex3::trim(raw_line);
            if (line.empty()) {
                continue;
            }

            if (from_client) {
                if (line.rfind("USER ", 0) == 0) {
                    session.current_username = ex3::trim(line.substr(5));
                } else if (line.rfind("PASS ", 0) == 0) {
                    session.pending.push_back({ex3::format_timestamp(packet.timestamp),
                                               packet.src_mac,
                                               packet.src_ip,
                                               packet.dst_mac,
                                               packet.dst_ip,
                                               session.current_username,
                                               ex3::trim(line.substr(5))});
                }
                continue;
            }

            if ((ex3::starts_with_code(line, "230") || ex3::starts_with_code(line, "530")) && !session.pending.empty()) {
                const Attempt attempt = session.pending.front();
                session.pending.pop_front();
                ex3::write_csv_row(out,
                                   {attempt.time,
                                    attempt.src_mac,
                                    attempt.src_ip,
                                    attempt.dst_mac,
                                    attempt.dst_ip,
                                    attempt.username,
                                    attempt.password,
                                    ex3::starts_with_code(line, "230") ? "SUCCEED" : "FAILED"});
                ++emitted;
            }
        }
        return true;
    }, &error);

    if (!ok) {
        std::cerr << "capture failed: " << error << '\n';
        return 1;
    }

    std::cout << "wrote " << emitted << " FTP login rows to " << options.output_csv << '\n';
    return 0;
}
