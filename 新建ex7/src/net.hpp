#ifndef EX7_NET_HPP
#define EX7_NET_HPP

#include <cerrno>
#include <cstdint>
#include <cstring>
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
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace ex7 {

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

inline void close_socket(SocketHandle socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

inline std::string socket_error() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

inline SocketHandle connect_tcp(const std::string &host, std::uint16_t port) {
    addrinfo hints{};
    addrinfo *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_text = std::to_string(port);
    const int rc = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
    if (rc != 0) {
        throw std::runtime_error("failed to resolve " + host + ": " + gai_strerror(rc));
    }

    SocketHandle socket_fd = kInvalidSocket;
    for (addrinfo *it = result; it != nullptr; it = it->ai_next) {
        socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_fd == kInvalidSocket) {
            continue;
        }

        if (connect(socket_fd, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen)) == 0) {
            freeaddrinfo(result);
            return socket_fd;
        }

        close_socket(socket_fd);
        socket_fd = kInvalidSocket;
    }

    freeaddrinfo(result);
    throw std::runtime_error("failed to connect " + host + ":" + port_text + ": " + socket_error());
}

inline void send_all(SocketHandle socket_fd, const std::string &data) {
    const char *cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
#ifdef _WIN32
        const int sent = send(socket_fd, cursor, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = send(socket_fd, cursor, remaining, 0);
#endif
        if (sent <= 0) {
            throw std::runtime_error("send failed: " + socket_error());
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

inline std::string recv_until_close(SocketHandle socket_fd) {
    std::string response;
    char buffer[4096];
    for (;;) {
#ifdef _WIN32
        const int nread = recv(socket_fd, buffer, sizeof(buffer), 0);
#else
        const ssize_t nread = recv(socket_fd, buffer, sizeof(buffer), 0);
#endif
        if (nread < 0) {
            throw std::runtime_error("recv failed: " + socket_error());
        }
        if (nread == 0) {
            break;
        }
        response.append(buffer, buffer + nread);
    }
    return response;
}

inline std::string exchange_line(const std::string &host, std::uint16_t port, const std::string &line) {
    SocketHandle socket_fd = connect_tcp(host, port);
    send_all(socket_fd, line + "\n");
#ifdef _WIN32
    shutdown(socket_fd, SD_SEND);
#else
    shutdown(socket_fd, SHUT_WR);
#endif
    std::string response = recv_until_close(socket_fd);
    close_socket(socket_fd);
    return response;
}

inline std::string trim_copy(const std::string &text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

inline bool starts_with(const std::string &text, const std::string &prefix) {
    return text.rfind(prefix, 0) == 0;
}

}  // namespace ex7

#endif
