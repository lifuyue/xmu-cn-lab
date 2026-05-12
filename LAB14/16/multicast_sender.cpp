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
constexpr std::size_t kDefaultChunkSize = 1200;

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

void put_u32(std::vector<unsigned char> &buffer, std::size_t offset, std::uint32_t value) {
    buffer[offset + 0] = static_cast<unsigned char>((value >> 24) & 0xff);
    buffer[offset + 1] = static_cast<unsigned char>((value >> 16) & 0xff);
    buffer[offset + 2] = static_cast<unsigned char>((value >> 8) & 0xff);
    buffer[offset + 3] = static_cast<unsigned char>(value & 0xff);
}

void put_u64(std::vector<unsigned char> &buffer, std::size_t offset, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        buffer[offset + static_cast<std::size_t>(7 - i)] = static_cast<unsigned char>((value >> (i * 8)) & 0xff);
    }
}

std::uint64_t file_size(int fd) {
    const off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0) {
        throw_errno("lseek");
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        throw_errno("lseek");
    }
    return static_cast<std::uint64_t>(end);
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
    std::cerr << "Usage: multicast_sender <multicast_ip> <port> <file> [rounds] [chunk_size]\n";
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 4 || argc > 6) {
            usage();
            return 1;
        }

        const std::string group = argv[1];
        const int port = parse_int(argv[2], "port");
        const std::string input_path = argv[3];
        const int rounds = argc >= 5 ? parse_int(argv[4], "rounds") : 2;
        const int chunk_size_arg = argc >= 6 ? parse_int(argv[5], "chunk_size") : static_cast<int>(kDefaultChunkSize);
        if (port <= 0 || port > 65535 || rounds <= 0 || chunk_size_arg <= 0 || chunk_size_arg > 60000) {
            throw std::invalid_argument("port, rounds, or chunk_size out of range");
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(static_cast<std::uint16_t>(port));
        if (inet_pton(AF_INET, group.c_str(), &target.sin_addr) != 1) {
            throw std::invalid_argument("invalid multicast address");
        }

        Fd sock(socket(AF_INET, SOCK_DGRAM, 0));
        if (sock.get() < 0) {
            throw_errno("socket");
        }

        unsigned char ttl = 1;
        if (setsockopt(sock.get(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
            throw_errno("setsockopt(IP_MULTICAST_TTL)");
        }

        Fd input(open(input_path.c_str(), O_RDONLY));
        if (input.get() < 0) {
            throw_errno("open input file");
        }

        const std::uint64_t size = file_size(input.get());
        const std::uint32_t chunk_size = static_cast<std::uint32_t>(chunk_size_arg);
        const std::uint32_t total_chunks = static_cast<std::uint32_t>((size + chunk_size - 1) / chunk_size);
        std::vector<unsigned char> packet(kHeaderSize + chunk_size);
        std::copy(std::begin(kMagic), std::end(kMagic), packet.begin());

        const auto start = std::chrono::steady_clock::now();
        for (int round = 0; round < rounds; ++round) {
            if (lseek(input.get(), 0, SEEK_SET) < 0) {
                throw_errno("lseek input");
            }

            for (std::uint32_t seq = 0; seq < total_chunks; ++seq) {
                const ssize_t n = read(input.get(), packet.data() + kHeaderSize, chunk_size);
                if (n < 0) {
                    throw_errno("read input");
                }

                put_u32(packet, 4, seq);
                put_u32(packet, 8, total_chunks);
                put_u32(packet, 12, chunk_size);
                put_u32(packet, 16, static_cast<std::uint32_t>(n));
                put_u64(packet, 20, size);

                const ssize_t sent = sendto(sock.get(), packet.data(), kHeaderSize + static_cast<std::size_t>(n), 0,
                                            reinterpret_cast<sockaddr *>(&target), sizeof(target));
                if (sent < 0) {
                    throw_errno("sendto");
                }
            }
            std::cout << "round " << (round + 1) << "/" << rounds << " sent\n";
        }

        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();
        std::cout << "sent file_size=" << size << " bytes, chunks=" << total_chunks
                  << ", repeated_rounds=" << rounds << ", elapsed=" << seconds << "s\n";
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
