#include "ex3/pcap_lab.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pcap.h>
#include <pcap/dlt.h>
#include <pcap/sll.h>
#include <sys/socket.h>

namespace ex3 {
namespace {

constexpr std::size_t kEthernetHeaderLength = 14;
constexpr std::uint16_t kEtherTypeIpv4 = 0x0800;

using PcapHandle = std::unique_ptr<pcap_t, decltype(&pcap_close)>;

std::string format_mac(const std::uint8_t* bytes, std::size_t length) {
    if (bytes == nullptr || length == 0) {
        return "00-00-00-00-00-00";
    }

    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < length; ++index) {
        if (index > 0) {
            out << '-';
        }
        out << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return out.str();
}

std::string format_ipv4_address(std::uint32_t address) {
    char buffer[INET_ADDRSTRLEN] = {};
    in_addr addr {};
    addr.s_addr = address;
    if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return buffer;
}

std::optional<std::size_t> find_header_delimiter(const std::string& buffer, std::size_t start) {
    const std::size_t crlf = buffer.find("\r\n\r\n", start);
    const std::size_t lf = buffer.find("\n\n", start);
    if (crlf == std::string::npos) {
        return lf == std::string::npos ? std::nullopt : std::optional<std::size_t>(lf + 2);
    }
    if (lf == std::string::npos) {
        return crlf + 4;
    }
    return std::min(crlf + 4, lf + 2);
}

std::string percent_decode(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const char ch = input[index];
        if (ch == '+') {
            output.push_back(' ');
            continue;
        }
        if (ch == '%' && index + 2 < input.size()) {
            const auto hex_value = input.substr(index + 1, 2);
            int decoded = 0;
            std::istringstream parser{std::string(hex_value)};
            parser >> std::hex >> decoded;
            if (!parser.fail()) {
                output.push_back(static_cast<char>(decoded));
                index += 2;
                continue;
            }
        }
        output.push_back(ch);
    }
    return output;
}

std::uint16_t read_be16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);
}

}  // namespace

std::optional<PacketView> decode_packet(
    const timeval& timestamp,
    std::uint32_t frame_length,
    const std::uint8_t* bytes,
    const std::size_t captured_length,
    const int datalink) {
    if (bytes == nullptr || captured_length == 0) {
        return std::nullopt;
    }

    std::size_t network_offset = 0;
    std::uint16_t network_type = 0;
    PacketView packet;
    packet.timestamp = timestamp;
    packet.frame_length = frame_length;

    switch (datalink) {
        case DLT_EN10MB:
            if (captured_length < kEthernetHeaderLength) {
                return std::nullopt;
            }
            packet.dst_mac = format_mac(bytes, 6);
            packet.src_mac = format_mac(bytes + 6, 6);
            network_type = read_be16(bytes + 12);
            network_offset = kEthernetHeaderLength;
            break;
        case DLT_LINUX_SLL:
            if (captured_length < SLL_HDR_LEN) {
                return std::nullopt;
            }
            packet.src_mac = format_mac(bytes + 6, std::min<std::size_t>(bytes[5], SLL_ADDRLEN));
            network_type = read_be16(bytes + 14);
            network_offset = SLL_HDR_LEN;
            break;
        case DLT_LINUX_SLL2:
            if (captured_length < SLL2_HDR_LEN) {
                return std::nullopt;
            }
            packet.src_mac = format_mac(bytes + 12, std::min<std::size_t>(bytes[11], SLL_ADDRLEN));
            network_type = read_be16(bytes);
            network_offset = SLL2_HDR_LEN;
            break;
        case DLT_NULL: {
            if (captured_length < 4) {
                return std::nullopt;
            }
            std::uint32_t family = 0;
            std::memcpy(&family, bytes, sizeof(family));
            if (family != AF_INET && ntohl(family) != AF_INET) {
                return std::nullopt;
            }
            network_type = kEtherTypeIpv4;
            network_offset = 4;
            break;
        }
        case DLT_RAW:
            if ((bytes[0] >> 4) != 4) {
                return std::nullopt;
            }
            network_type = kEtherTypeIpv4;
            network_offset = 0;
            break;
        default:
            return std::nullopt;
    }

    if (network_type != kEtherTypeIpv4 || captured_length < network_offset + sizeof(ip)) {
        return std::nullopt;
    }

    const auto* ipv4 = reinterpret_cast<const ip*>(bytes + network_offset);
    const std::size_t ipv4_header_length = static_cast<std::size_t>(ipv4->ip_hl) * 4;
    if (ipv4->ip_v != 4 || ipv4_header_length < sizeof(ip) ||
        captured_length < network_offset + ipv4_header_length) {
        return std::nullopt;
    }

    packet.src_ip = format_ipv4_address(ipv4->ip_src.s_addr);
    packet.dst_ip = format_ipv4_address(ipv4->ip_dst.s_addr);

    if (ipv4->ip_p != IPPROTO_TCP) {
        return packet;
    }

    const std::size_t tcp_offset = network_offset + ipv4_header_length;
    if (captured_length < tcp_offset + sizeof(tcphdr)) {
        return packet;
    }

    const auto* tcp = reinterpret_cast<const tcphdr*>(bytes + tcp_offset);
    const std::size_t tcp_header_length = static_cast<std::size_t>(tcp->th_off) * 4;
    if (tcp_header_length < sizeof(tcphdr) || captured_length < tcp_offset + tcp_header_length) {
        return packet;
    }

    packet.has_tcp = true;
    packet.src_port = ntohs(tcp->th_sport);
    packet.dst_port = ntohs(tcp->th_dport);

    const std::size_t payload_offset = tcp_offset + tcp_header_length;
    if (captured_length > payload_offset) {
        packet.payload.assign(bytes + payload_offset, bytes + captured_length);
    }
    return packet;
}

bool run_capture(
    const CaptureOptions& options,
    const std::function<bool(const PacketView&)>& callback,
    std::string* error) {
    char error_buffer[PCAP_ERRBUF_SIZE] = {};
    PcapHandle handle(nullptr, &pcap_close);

    if (!options.read_file.empty()) {
        handle.reset(pcap_open_offline(options.read_file.c_str(), error_buffer));
    } else if (!options.interface_name.empty()) {
        pcap_t* live = pcap_create(options.interface_name.c_str(), error_buffer);
        if (live == nullptr) {
            if (error != nullptr) {
                *error = error_buffer;
            }
            return false;
        }
        handle.reset(live);
        if (pcap_set_snaplen(handle.get(), options.snaplen) != 0 ||
            pcap_set_promisc(handle.get(), options.promiscuous ? 1 : 0) != 0 ||
            pcap_set_timeout(handle.get(), options.timeout_ms) != 0) {
            if (error != nullptr) {
                *error = pcap_geterr(handle.get());
            }
            return false;
        }
        const int activate_result = pcap_activate(handle.get());
        if (activate_result < 0) {
            if (error != nullptr) {
                *error = pcap_geterr(handle.get());
            }
            return false;
        }
    } else {
        if (error != nullptr) {
            *error = "missing capture source: use --read or --iface";
        }
        return false;
    }

    if (!handle) {
        if (error != nullptr) {
            *error = error_buffer;
        }
        return false;
    }

    if (!options.filter_expr.empty()) {
        bpf_program program {};
        if (pcap_compile(handle.get(), &program, options.filter_expr.c_str(), 1, PCAP_NETMASK_UNKNOWN) != 0) {
            if (error != nullptr) {
                *error = pcap_geterr(handle.get());
            }
            return false;
        }
        const int set_result = pcap_setfilter(handle.get(), &program);
        pcap_freecode(&program);
        if (set_result != 0) {
            if (error != nullptr) {
                *error = pcap_geterr(handle.get());
            }
            return false;
        }
    }

    const int datalink = pcap_datalink(handle.get());
    while (true) {
        pcap_pkthdr* header = nullptr;
        const u_char* bytes = nullptr;
        const int result = pcap_next_ex(handle.get(), &header, &bytes);
        if (result == 1) {
            const auto packet = decode_packet(
                header->ts,
                header->len,
                reinterpret_cast<const std::uint8_t*>(bytes),
                header->caplen,
                datalink);
            if (packet.has_value() && !callback(*packet)) {
                return true;
            }
            continue;
        }
        if (result == 0) {
            continue;
        }
        if (result == -2) {
            return true;
        }
        if (error != nullptr) {
            *error = pcap_geterr(handle.get());
        }
        return false;
    }
}

std::string format_timestamp(const timeval& timestamp) {
    std::time_t seconds = timestamp.tv_sec;
    std::tm local_time {};
    localtime_r(&seconds, &local_time);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
    return buffer;
}

std::string trim(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(start, end - start));
}

std::string to_lower_copy(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool contains_case_insensitive(const std::string_view text, const std::string_view needle) {
    return to_lower_copy(text).find(to_lower_copy(needle)) != std::string::npos;
}

bool starts_with_code(const std::string_view line, const std::string_view code) {
    if (line.size() < code.size()) {
        return false;
    }
    if (line.substr(0, code.size()) != code) {
        return false;
    }
    return line.size() == code.size() || std::isspace(static_cast<unsigned char>(line[code.size()])) != 0;
}

std::map<std::string, std::string> parse_form_urlencoded(const std::string_view body) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start <= body.size()) {
        const std::size_t end = body.find('&', start);
        const auto part = body.substr(start, end == std::string::npos ? body.size() - start : end - start);
        const std::size_t equal = part.find('=');
        if (equal != std::string::npos) {
            fields[to_lower_copy(percent_decode(part.substr(0, equal)))] =
                percent_decode(part.substr(equal + 1));
        } else if (!part.empty()) {
            fields[to_lower_copy(percent_decode(part))] = "";
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return fields;
}

std::vector<std::string> drain_lines(std::string& buffer) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = buffer.find('\n', start);
        if (newline == std::string::npos) {
            buffer.erase(0, start);
            return lines;
        }
        std::string line = buffer.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = newline + 1;
    }
}

std::vector<HttpMessage> drain_http_messages(std::string& buffer) {
    std::vector<HttpMessage> messages;
    std::size_t offset = 0;
    while (true) {
        const auto header_end = find_header_delimiter(buffer, offset);
        if (!header_end.has_value()) {
            buffer.erase(0, offset);
            return messages;
        }

        const std::size_t header_start = offset;
        const std::size_t header_length = *header_end - header_start;
        const std::string header_block = buffer.substr(header_start, header_length);

        std::size_t body_length = 0;
        std::istringstream header_stream(header_block);
        std::string start_line;
        if (!std::getline(header_stream, start_line)) {
            buffer.erase(0, *header_end);
            offset = 0;
            continue;
        }
        if (!start_line.empty() && start_line.back() == '\r') {
            start_line.pop_back();
        }

        HttpMessage message;
        message.start_line = start_line;
        std::string line;
        while (std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            const std::string key = to_lower_copy(trim(line.substr(0, colon)));
            const std::string value = trim(line.substr(colon + 1));
            message.headers[key] = value;
            if (key == "content-length") {
                body_length = static_cast<std::size_t>(std::stoul(value));
            }
        }

        const std::size_t total_length = *header_end + body_length;
        if (buffer.size() < total_length) {
            buffer.erase(0, offset);
            return messages;
        }

        message.body = buffer.substr(*header_end, body_length);
        messages.push_back(std::move(message));
        offset = total_length;
    }
}

void write_csv_row(std::ostream& out, const std::vector<std::string>& fields) {
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << '"';
        for (const char ch : fields[index]) {
            if (ch == '"') {
                out << "\"\"";
            } else {
                out << ch;
            }
        }
        out << '"';
    }
    out << '\n';
}

}  // namespace ex3
