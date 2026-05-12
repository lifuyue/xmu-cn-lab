#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr char kMagic[] = {'M', 'F', 'T', '1'};
constexpr std::size_t kHeaderSize = 28;
constexpr std::size_t kMaxPacketSize = 65536;

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

[[noreturn]] void throw_errno(const std::string &message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

std::uint32_t get_u32(const unsigned char *buffer, std::size_t offset) {
    return (static_cast<std::uint32_t>(buffer[offset + 0]) << 24) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buffer[offset + 3]);
}

std::uint64_t get_u64(const unsigned char *buffer, std::size_t offset) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | buffer[offset + static_cast<std::size_t>(i)];
    }
    return value;
}

int parse_int(const char *text, const std::string &name) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (text[used] != '\0') {
            throw std::invalid_argument("bad suffix");
        }
        return value;
    } catch (const std::exception &) {
        throw std::invalid_argument("invalid " + name + ": " + text);
    }
}

void usage() {
    std::cerr << "Usage: multicast_receiver <multicast_ip> <port> <output_file> [timeout_seconds]\n";
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 4 || argc > 5) {
            usage();
            return 1;
        }

        const std::string group = argv[1];
        const int port = parse_int(argv[2], "port");
        const std::string output_path = argv[3];
        const int timeout_seconds = argc >= 5 ? parse_int(argv[4], "timeout_seconds") : 10;
        if (port <= 0 || port > 65535 || timeout_seconds <= 0) {
            throw std::invalid_argument("port or timeout out of range");
        }

        Fd sock(socket(AF_INET, SOCK_DGRAM, 0));
        if (sock.get() < 0) {
            throw_errno("socket");
        }

        int reuse = 1;
        if (setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            throw_errno("setsockopt(SO_REUSEADDR)");
        }

        timeval timeout{};
        timeout.tv_sec = timeout_seconds;
        if (setsockopt(sock.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            throw_errno("setsockopt(SO_RCVTIMEO)");
        }

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(static_cast<std::uint16_t>(port));
        if (bind(sock.get(), reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0) {
            throw_errno("bind");
        }

        ip_mreq membership{};
        if (inet_pton(AF_INET, group.c_str(), &membership.imr_multiaddr) != 1) {
            throw std::invalid_argument("invalid multicast address");
        }
        membership.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sock.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
            throw_errno("setsockopt(IP_ADD_MEMBERSHIP)");
        }

        Fd output(open(output_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644));
        if (output.get() < 0) {
            throw_errno("open output file");
        }

        std::vector<unsigned char> packet(kMaxPacketSize);
        std::vector<unsigned char> received;
        std::uint64_t file_size = 0;
        std::uint32_t chunk_size = 0;
        std::uint32_t total_chunks = 0;
        std::uint32_t received_chunks = 0;
        auto start = std::chrono::steady_clock::now();

        while (true) {
            const ssize_t n = recvfrom(sock.get(), packet.data(), packet.size(), 0, nullptr, nullptr);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                throw_errno("recvfrom");
            }
            if (static_cast<std::size_t>(n) < kHeaderSize || !std::equal(std::begin(kMagic), std::end(kMagic), packet.begin())) {
                continue;
            }

            const std::uint32_t seq = get_u32(packet.data(), 4);
            const std::uint32_t total = get_u32(packet.data(), 8);
            const std::uint32_t packet_chunk_size = get_u32(packet.data(), 12);
            const std::uint32_t payload_size = get_u32(packet.data(), 16);
            const std::uint64_t packet_file_size = get_u64(packet.data(), 20);
            if (static_cast<std::size_t>(n) != kHeaderSize + payload_size || seq >= total) {
                continue;
            }

            if (received.empty()) {
                total_chunks = total;
                chunk_size = packet_chunk_size;
                file_size = packet_file_size;
                received.assign(total_chunks, 0);
                if (ftruncate(output.get(), static_cast<off_t>(file_size)) < 0) {
                    throw_errno("ftruncate");
                }
                start = std::chrono::steady_clock::now();
            }

            if (total != total_chunks || packet_chunk_size != chunk_size || packet_file_size != file_size) {
                continue;
            }
            if (!received[seq]) {
                const off_t offset = static_cast<off_t>(static_cast<std::uint64_t>(seq) * chunk_size);
                const ssize_t written = pwrite(output.get(), packet.data() + kHeaderSize, payload_size, offset);
                if (written < 0 || static_cast<std::uint32_t>(written) != payload_size) {
                    throw_errno("pwrite");
                }
                received[seq] = 1;
                ++received_chunks;
                if (received_chunks == total_chunks) {
                    break;
                }
            }
        }

        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();
        const std::uint32_t missing = total_chunks - received_chunks;
        std::cout << "received_chunks=" << received_chunks << "/" << total_chunks
                  << ", missing=" << missing << ", file_size=" << file_size
                  << " bytes, elapsed=" << seconds << "s\n";
        return missing == 0 ? 0 : 2;
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
