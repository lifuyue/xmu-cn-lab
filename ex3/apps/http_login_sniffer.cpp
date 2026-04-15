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
    std::uint16_t server_port = 0;

    bool operator<(const SessionKey& other) const {
        return std::tie(client_ip, client_port, server_ip, server_port) <
               std::tie(other.client_ip, other.client_port, other.server_ip, other.server_port);
    }
};

struct PendingRequest {
    std::string time;
    std::string src_mac;
    std::string src_ip;
    std::string dst_mac;
    std::string dst_ip;
    std::string username;
    std::string password;
};

struct Session {
    std::string request_buffer;
    std::string response_buffer;
    std::deque<PendingRequest> pending;
};

struct Options {
    ex3::CaptureOptions capture;
    std::string output_csv = "http_logins.csv";
};

bool is_http_server_port(const std::uint16_t port) {
    return port == 80 || port == 8080;
}

std::optional<std::pair<SessionKey, bool>> resolve_session(const ex3::PacketView& packet) {
    if (!packet.has_tcp) {
        return std::nullopt;
    }
    if (is_http_server_port(packet.dst_port)) {
        return std::pair<SessionKey, bool>{{packet.src_ip, packet.src_port, packet.dst_ip, packet.dst_port}, true};
    }
    if (is_http_server_port(packet.src_port)) {
        return std::pair<SessionKey, bool>{{packet.dst_ip, packet.dst_port, packet.src_ip, packet.src_port}, false};
    }
    return std::nullopt;
}

std::string extract_field(const std::map<std::string, std::string>& fields,
                          const std::initializer_list<std::string>& candidates) {
    for (const auto& key : candidates) {
        const auto iter = fields.find(key);
        if (iter != fields.end() && !iter->second.empty()) {
            return iter->second;
        }
    }
    return {};
}

std::string classify_response(const ex3::HttpMessage& message) {
    if (ex3::contains_case_insensitive(message.body, "LOGIN_RESULT=SUCCEED") ||
        ex3::contains_case_insensitive(message.body, "login_result=succeed") ||
        ex3::contains_case_insensitive(message.body, "\"success\":true") ||
        ex3::contains_case_insensitive(message.body, "login succeed")) {
        return "SUCCEED";
    }
    if (ex3::contains_case_insensitive(message.body, "LOGIN_RESULT=FAILED") ||
        ex3::contains_case_insensitive(message.body, "\"success\":false") ||
        ex3::contains_case_insensitive(message.body, "login failed") ||
        message.start_line.find(" 401 ") != std::string::npos ||
        message.start_line.find(" 403 ") != std::string::npos) {
        return "FAILED";
    }
    return {};
}

void print_usage() {
    std::cerr << "usage: http_login_sniffer (--read FILE | --iface NAME) [--filter EXPR] [--output FILE]\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    options.capture.filter_expr = "tcp and (port 80 or port 8080)";

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
        std::string& buffer = from_client ? session.request_buffer : session.response_buffer;
        buffer.append(reinterpret_cast<const char*>(packet.payload.data()), packet.payload.size());

        for (const ex3::HttpMessage& message : ex3::drain_http_messages(buffer)) {
            if (from_client) {
                const std::string start_line = ex3::to_lower_copy(message.start_line);
                if (start_line.rfind("post ", 0) != 0) {
                    continue;
                }
                const auto fields = ex3::parse_form_urlencoded(message.body);
                const std::string username = extract_field(fields, {"username", "user", "user_id"});
                const std::string password = extract_field(fields, {"password", "pass", "pwd"});
                if (username.empty() && password.empty()) {
                    continue;
                }
                session.pending.push_back({ex3::format_timestamp(packet.timestamp),
                                           packet.src_mac,
                                           packet.src_ip,
                                           packet.dst_mac,
                                           packet.dst_ip,
                                           username,
                                           password});
                continue;
            }

            const std::string result = classify_response(message);
            if (result.empty() || session.pending.empty()) {
                continue;
            }
            const PendingRequest request = session.pending.front();
            session.pending.pop_front();
            ex3::write_csv_row(out,
                               {request.time,
                                request.src_mac,
                                request.src_ip,
                                request.dst_mac,
                                request.dst_ip,
                                request.username,
                                request.password,
                                result});
            ++emitted;
        }
        return true;
    }, &error);

    if (!ok) {
        std::cerr << "capture failed: " << error << '\n';
        return 1;
    }

    std::cout << "wrote " << emitted << " HTTP login rows to " << options.output_csv << '\n';
    return 0;
}
