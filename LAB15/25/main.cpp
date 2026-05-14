#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kClassIn = 1;
constexpr std::size_t kDnsHeaderSize = 12;

enum QueryType : std::uint16_t {
    A = 1,
    NS = 2,
    CNAME = 5,
    PTR = 12,
    MX = 15,
    TXT = 16,
    AAAA = 28,
};

struct DnsHeader {
    std::uint16_t id = 0;
    std::uint16_t flags = 0;
    std::uint16_t qdcount = 0;
    std::uint16_t ancount = 0;
    std::uint16_t nscount = 0;
    std::uint16_t arcount = 0;
};

struct ResourceRecord {
    std::string name;
    std::uint16_t type = 0;
    std::uint16_t rr_class = 0;
    std::uint32_t ttl = 0;
    std::string data;
};

void usage() {
    std::cerr << "Usage: nslookup_flow <name> [type] [server]\n"
              << "Examples:\n"
              << "  nslookup_flow www.xmu.edu.cn A\n"
              << "  nslookup_flow www.xmu.edu.cn CNAME 8.8.8.8\n"
              << "  nslookup_flow 8.8.8.8 PTR\n";
}

std::uint16_t read_u16(const std::vector<std::uint8_t> &buffer, std::size_t offset) {
    if (offset + 2 > buffer.size()) {
        throw std::runtime_error("truncated DNS packet");
    }
    return static_cast<std::uint16_t>((buffer[offset] << 8) | buffer[offset + 1]);
}

std::uint32_t read_u32(const std::vector<std::uint8_t> &buffer, std::size_t offset) {
    if (offset + 4 > buffer.size()) {
        throw std::runtime_error("truncated DNS packet");
    }
    return (static_cast<std::uint32_t>(buffer[offset]) << 24) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buffer[offset + 3]);
}

void append_u16(std::vector<std::uint8_t> &buffer, std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value >> 8));
    buffer.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void append_name(std::vector<std::uint8_t> &buffer, const std::string &name) {
    std::stringstream ss(name);
    std::string label;
    while (std::getline(ss, label, '.')) {
        if (label.empty()) {
            continue;
        }
        if (label.size() > 63) {
            throw std::invalid_argument("DNS label is longer than 63 bytes");
        }
        buffer.push_back(static_cast<std::uint8_t>(label.size()));
        buffer.insert(buffer.end(), label.begin(), label.end());
    }
    buffer.push_back(0);
}

std::string decode_name(const std::vector<std::uint8_t> &packet, std::size_t &offset) {
    std::vector<std::string> labels;
    bool jumped = false;
    std::size_t cursor = offset;
    std::size_t next_offset = offset;
    int jump_count = 0;

    while (true) {
        if (cursor >= packet.size()) {
            throw std::runtime_error("DNS name exceeds packet length");
        }
        const std::uint8_t length = packet[cursor];
        if (length == 0) {
            ++cursor;
            if (!jumped) {
                next_offset = cursor;
            }
            break;
        }
        if ((length & 0xc0) == 0xc0) {
            if (cursor + 1 >= packet.size()) {
                throw std::runtime_error("truncated compression pointer");
            }
            const std::size_t pointer = ((length & 0x3f) << 8) | packet[cursor + 1];
            if (!jumped) {
                next_offset = cursor + 2;
            }
            cursor = pointer;
            jumped = true;
            if (++jump_count > 32) {
                throw std::runtime_error("DNS compression pointer loop");
            }
            continue;
        }
        ++cursor;
        if (cursor + length > packet.size()) {
            throw std::runtime_error("truncated DNS label");
        }
        labels.emplace_back(reinterpret_cast<const char *>(packet.data() + cursor), length);
        cursor += length;
        if (!jumped) {
            next_offset = cursor;
        }
    }

    offset = next_offset;
    if (labels.empty()) {
        return ".";
    }

    std::string name;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i) {
            name += ".";
        }
        name += labels[i];
    }
    return name;
}

DnsHeader parse_header(const std::vector<std::uint8_t> &packet) {
    if (packet.size() < kDnsHeaderSize) {
        throw std::runtime_error("DNS packet is shorter than header");
    }
    return {
        read_u16(packet, 0),
        read_u16(packet, 2),
        read_u16(packet, 4),
        read_u16(packet, 6),
        read_u16(packet, 8),
        read_u16(packet, 10),
    };
}

std::string type_name(std::uint16_t type) {
    switch (type) {
        case A:
            return "A";
        case NS:
            return "NS";
        case CNAME:
            return "CNAME";
        case PTR:
            return "PTR";
        case MX:
            return "MX";
        case TXT:
            return "TXT";
        case AAAA:
            return "AAAA";
        default:
            return std::to_string(type);
    }
}

std::uint16_t parse_type(const std::string &text) {
    if (text == "A") {
        return A;
    }
    if (text == "AAAA") {
        return AAAA;
    }
    if (text == "CNAME") {
        return CNAME;
    }
    if (text == "MX") {
        return MX;
    }
    if (text == "NS") {
        return NS;
    }
    if (text == "PTR") {
        return PTR;
    }
    if (text == "TXT") {
        return TXT;
    }
    throw std::invalid_argument("unsupported DNS type: " + text);
}

std::string system_dns_server() {
    std::ifstream file("/etc/resolv.conf");
    std::string word;
    while (file >> word) {
        if (word == "nameserver") {
            std::string server;
            file >> server;
            if (!server.empty()) {
                return server;
            }
        }
        std::string rest;
        std::getline(file, rest);
    }
    return "8.8.8.8";
}

std::string reverse_ipv4_name(const std::string &ip) {
    std::array<unsigned char, 4> bytes{};
    if (inet_pton(AF_INET, ip.c_str(), bytes.data()) != 1) {
        throw std::invalid_argument("PTR query expects an IPv4 address");
    }
    std::ostringstream out;
    out << static_cast<int>(bytes[3]) << "."
        << static_cast<int>(bytes[2]) << "."
        << static_cast<int>(bytes[1]) << "."
        << static_cast<int>(bytes[0]) << ".in-addr.arpa";
    return out.str();
}

std::vector<std::uint8_t> build_query(std::uint16_t id, const std::string &name, std::uint16_t type) {
    std::vector<std::uint8_t> packet;
    append_u16(packet, id);
    append_u16(packet, 0x0100);
    append_u16(packet, 1);
    append_u16(packet, 0);
    append_u16(packet, 0);
    append_u16(packet, 0);
    append_name(packet, name);
    append_u16(packet, type);
    append_u16(packet, kClassIn);
    return packet;
}

std::string parse_rdata(const std::vector<std::uint8_t> &packet,
                        std::size_t offset,
                        std::uint16_t length,
                        std::uint16_t type) {
    if (offset + length > packet.size()) {
        throw std::runtime_error("truncated resource record data");
    }

    char address[INET6_ADDRSTRLEN] = {};
    if (type == A && length == 4) {
        inet_ntop(AF_INET, packet.data() + offset, address, sizeof(address));
        return address;
    }
    if (type == AAAA && length == 16) {
        inet_ntop(AF_INET6, packet.data() + offset, address, sizeof(address));
        return address;
    }
    if (type == NS || type == CNAME || type == PTR) {
        return decode_name(packet, offset);
    }
    if (type == MX && length >= 3) {
        const std::uint16_t preference = read_u16(packet, offset);
        offset += 2;
        return std::to_string(preference) + " " + decode_name(packet, offset);
    }
    if (type == TXT) {
        std::string result;
        std::size_t cursor = offset;
        const std::size_t end = offset + length;
        while (cursor < end) {
            const std::uint8_t part_length = packet[cursor++];
            if (cursor + part_length > end) {
                break;
            }
            if (!result.empty()) {
                result += " ";
            }
            result.append(reinterpret_cast<const char *>(packet.data() + cursor), part_length);
            cursor += part_length;
        }
        return result;
    }

    std::ostringstream hex;
    for (std::size_t i = 0; i < length; ++i) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(packet[offset + i]);
    }
    return hex.str();
}

ResourceRecord parse_record(const std::vector<std::uint8_t> &packet, std::size_t &offset) {
    ResourceRecord rr;
    rr.name = decode_name(packet, offset);
    rr.type = read_u16(packet, offset);
    rr.rr_class = read_u16(packet, offset + 2);
    rr.ttl = read_u32(packet, offset + 4);
    const std::uint16_t data_length = read_u16(packet, offset + 8);
    offset += 10;
    rr.data = parse_rdata(packet, offset, data_length, rr.type);
    offset += data_length;
    return rr;
}

std::vector<std::uint8_t> send_query(const std::string &server, const std::vector<std::uint8_t> &query) {
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        throw std::runtime_error("socket failed");
    }

    timeval timeout{};
    timeout.tv_sec = 3;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(53);
    if (inet_pton(AF_INET, server.c_str(), &address.sin_addr) != 1) {
        close(sock);
        throw std::invalid_argument("invalid DNS server IPv4 address");
    }

    const ssize_t sent = sendto(sock, query.data(), query.size(), 0,
                               reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (sent < 0 || static_cast<std::size_t>(sent) != query.size()) {
        close(sock);
        throw std::runtime_error("sendto failed");
    }

    std::vector<std::uint8_t> response(4096);
    const ssize_t received = recvfrom(sock, response.data(), response.size(), 0, nullptr, nullptr);
    close(sock);
    if (received < 0) {
        throw std::runtime_error("recvfrom timed out or failed");
    }
    response.resize(static_cast<std::size_t>(received));
    return response;
}

void print_records(const std::vector<ResourceRecord> &records) {
    for (const ResourceRecord &rr : records) {
        std::cout << std::left << std::setw(32) << rr.name
                  << std::right << std::setw(8) << rr.ttl
                  << " IN " << std::left << std::setw(6) << type_name(rr.type)
                  << rr.data << "\n";
    }
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 2 || argc > 4) {
            usage();
            return 1;
        }

        const std::uint16_t query_type = argc >= 3 ? parse_type(argv[2]) : A;
        const std::string server = argc >= 4 ? argv[3] : system_dns_server();
        const std::string query_name = query_type == PTR ? reverse_ipv4_name(argv[1]) : argv[1];

        std::random_device rd;
        const std::uint16_t id = static_cast<std::uint16_t>(rd());
        const std::vector<std::uint8_t> request = build_query(id, query_name, query_type);
        const std::vector<std::uint8_t> response = send_query(server, request);
        const DnsHeader header = parse_header(response);

        if (header.id != id) {
            throw std::runtime_error("response transaction id mismatch");
        }
        const std::uint16_t rcode = header.flags & 0x000f;
        if (rcode != 0) {
            throw std::runtime_error("DNS server returned RCODE=" + std::to_string(rcode));
        }

        std::size_t offset = kDnsHeaderSize;
        for (std::uint16_t i = 0; i < header.qdcount; ++i) {
            decode_name(response, offset);
            offset += 4;
        }

        std::vector<ResourceRecord> answers;
        for (std::uint16_t i = 0; i < header.ancount; ++i) {
            answers.push_back(parse_record(response, offset));
        }

        std::cout << "Server: " << server << "\n";
        std::cout << "Query:  " << argv[1] << " " << type_name(query_type) << "\n";
        if (answers.empty()) {
            std::cout << "No answer records.\n";
            return 2;
        }
        print_records(answers);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
