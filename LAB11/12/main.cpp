#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace {

constexpr std::uint32_t kNtpUnixEpochOffset = 2208988800UL;
constexpr std::size_t kNtpPacketSize = 48;

struct Options {
    std::string time_text;
    std::uint16_t port = 123;
    bool once = false;
};

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void close_socket(SocketHandle socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

std::string socket_error() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

Options parse_options(int argc, char **argv) {
    if (argc < 2) {
        throw std::invalid_argument("Usage: ntp_server \"YYYY-MM-DD HH:MM:SS\" [--port N] [--once]");
    }

    Options options;
    options.time_text = argv[1];

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--once") {
            options.once = true;
        } else if (arg == "--port") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--port requires a value");
            }
            const int value = std::stoi(argv[++i]);
            if (value <= 0 || value > 65535) {
                throw std::invalid_argument("port must be within 1-65535");
            }
            options.port = static_cast<std::uint16_t>(value);
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    return options;
}

std::time_t parse_local_time(const std::string &text) {
    std::tm tm_value{};
    std::istringstream iss(text);
    iss >> std::get_time(&tm_value, "%Y-%m-%d %H:%M:%S");
    if (!iss || !iss.eof()) {
        throw std::invalid_argument("time must match YYYY-MM-DD HH:MM:SS");
    }

    tm_value.tm_isdst = -1;
    const std::time_t unix_time = std::mktime(&tm_value);
    if (unix_time == static_cast<std::time_t>(-1)) {
        throw std::invalid_argument("failed to convert time");
    }
    return unix_time;
}

void write_be32(std::array<std::uint8_t, kNtpPacketSize> &packet,
                std::size_t offset,
                std::uint32_t value) {
    packet[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    packet[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    packet[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    packet[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_timestamp(std::array<std::uint8_t, kNtpPacketSize> &packet,
                     std::size_t offset,
                     std::time_t unix_time) {
    write_be32(packet, offset, static_cast<std::uint32_t>(unix_time) + kNtpUnixEpochOffset);
    write_be32(packet, offset + 4, 0);
}

std::array<std::uint8_t, kNtpPacketSize> make_response(
    const std::array<std::uint8_t, kNtpPacketSize> &request,
    std::time_t fixed_time) {
    std::array<std::uint8_t, kNtpPacketSize> response{};

    const std::uint8_t version = static_cast<std::uint8_t>((request[0] >> 3) & 0x07);
    response[0] = static_cast<std::uint8_t>((version == 0 ? 4 : version) << 3U | 0x04U);
    response[1] = 1;
    response[2] = request[2];
    response[3] = static_cast<std::uint8_t>(-20);
    write_be32(response, 4, 0);
    write_be32(response, 8, 0);
    response[12] = 'L';
    response[13] = 'O';
    response[14] = 'C';
    response[15] = 'L';

    write_timestamp(response, 16, fixed_time);
    std::memcpy(response.data() + 24, request.data() + 40, 8);
    write_timestamp(response, 32, fixed_time);
    write_timestamp(response, 40, fixed_time);

    return response;
}

void run_server(const Options &options) {
    SocketRuntime runtime;
    const std::time_t fixed_time = parse_local_time(options.time_text);

    SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == kInvalidSocket) {
        throw std::runtime_error("failed to create socket: " + socket_error());
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(options.port);

    if (bind(socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close_socket(socket_fd);
        throw std::runtime_error("failed to bind UDP port " + std::to_string(options.port) +
                                 ": " + socket_error());
    }

    std::cout << "NTP server listening on UDP port " << options.port << "\n";
    std::cout << "fixed time: " << options.time_text << "\n";

    int handled = 0;
    while (true) {
        std::array<std::uint8_t, kNtpPacketSize> request{};
        sockaddr_in client{};
#ifdef _WIN32
        int client_len = sizeof(client);
        const int received = recvfrom(socket_fd,
                                      reinterpret_cast<char *>(request.data()),
                                      static_cast<int>(request.size()),
                                      0,
                                      reinterpret_cast<sockaddr *>(&client),
                                      &client_len);
#else
        socklen_t client_len = sizeof(client);
        const ssize_t received = recvfrom(socket_fd,
                                          request.data(),
                                          request.size(),
                                          0,
                                          reinterpret_cast<sockaddr *>(&client),
                                          &client_len);
#endif
        if (received < 0) {
            close_socket(socket_fd);
            throw std::runtime_error("recvfrom failed: " + socket_error());
        }
        if (received < static_cast<int>(kNtpPacketSize)) {
            std::cerr << "ignored short packet: " << received << " bytes\n";
            continue;
        }

        const auto response = make_response(request, fixed_time);
#ifdef _WIN32
        const int sent = sendto(socket_fd,
                                reinterpret_cast<const char *>(response.data()),
                                static_cast<int>(response.size()),
                                0,
                                reinterpret_cast<sockaddr *>(&client),
                                client_len);
#else
        const ssize_t sent = sendto(socket_fd,
                                    response.data(),
                                    response.size(),
                                    0,
                                    reinterpret_cast<sockaddr *>(&client),
                                    client_len);
#endif
        if (sent != static_cast<int>(response.size())) {
            close_socket(socket_fd);
            throw std::runtime_error("sendto failed: " + socket_error());
        }

        char client_ip[64] = {};
        inet_ntop(AF_INET, &client.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "replied to " << client_ip << ":" << ntohs(client.sin_port) << "\n";

        ++handled;
        if (options.once && handled >= 1) {
            break;
        }
    }

    close_socket(socket_fd);
}

}  // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        run_server(options);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
