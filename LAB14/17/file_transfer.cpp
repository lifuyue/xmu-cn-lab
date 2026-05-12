#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
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

constexpr char kUdpMagic[] = {'F', 'T', 'U', '1'};
constexpr std::size_t kBufferSize = 64 * 1024;
constexpr std::size_t kUdpHeaderSize = 24;
constexpr std::size_t kUdpPayloadSize = 1200;

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
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

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

std::uint32_t get_u32(const unsigned char *buffer, std::size_t offset) {
    return (static_cast<std::uint32_t>(buffer[offset + 0]) << 24) |
           (static_cast<std::uint32_t>(buffer[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buffer[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buffer[offset + 3]);
}

void put_u64(std::vector<unsigned char> &buffer, std::size_t offset, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        buffer[offset + static_cast<std::size_t>(7 - i)] = static_cast<unsigned char>((value >> (i * 8)) & 0xff);
    }
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

void write_all(int fd, const unsigned char *data, std::size_t size) {
    while (size > 0) {
        const ssize_t n = write(fd, data, size);
        if (n < 0) {
            throw_errno("write");
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}

void read_all(int fd, unsigned char *data, std::size_t size) {
    while (size > 0) {
        const ssize_t n = read(fd, data, size);
        if (n < 0) {
            throw_errno("read");
        }
        if (n == 0) {
            throw std::runtime_error("unexpected EOF");
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}

int make_tcp_listener(int port) {
    Fd sock(socket(AF_INET, SOCK_STREAM, 0));
    if (sock.get() < 0) {
        throw_errno("socket");
    }
    int reuse = 1;
    if (setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        throw_errno("setsockopt");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(sock.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        throw_errno("bind");
    }
    if (listen(sock.get(), 1) < 0) {
        throw_errno("listen");
    }
    return sock.release();
}

int connect_tcp(const std::string &host, int port) {
    Fd sock(socket(AF_INET, SOCK_STREAM, 0));
    if (sock.get() < 0) {
        throw_errno("socket");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        throw std::invalid_argument("only IPv4 numeric addresses are supported");
    }
    if (connect(sock.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        throw_errno("connect");
    }
    return sock.release();
}

void print_rate(const std::string &label, std::uint64_t bytes, double seconds) {
    const double mib = static_cast<double>(bytes) / 1024.0 / 1024.0;
    const double rate = seconds > 0 ? mib / seconds : 0;
    std::cout << label << ": bytes=" << bytes << ", seconds=" << seconds
              << ", rate=" << rate << " MiB/s\n";
}

void tcp_server(int port, const std::string &output_path) {
    Fd listener(make_tcp_listener(port));
    Fd conn(accept(listener.get(), nullptr, nullptr));
    if (conn.get() < 0) {
        throw_errno("accept");
    }

    std::vector<unsigned char> size_buffer(8);
    read_all(conn.get(), size_buffer.data(), size_buffer.size());
    const std::uint64_t expected = get_u64(size_buffer.data(), 0);

    Fd output(open(output_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644));
    if (output.get() < 0) {
        throw_errno("open output");
    }

    std::vector<unsigned char> buffer(kBufferSize);
    std::uint64_t received = 0;
    const auto start = std::chrono::steady_clock::now();
    while (received < expected) {
        const std::size_t need = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), expected - received));
        const ssize_t n = read(conn.get(), buffer.data(), need);
        if (n < 0) {
            throw_errno("read tcp");
        }
        if (n == 0) {
            break;
        }
        write_all(output.get(), buffer.data(), static_cast<std::size_t>(n));
        received += static_cast<std::uint64_t>(n);
    }
    const auto end = std::chrono::steady_clock::now();
    print_rate("tcp server received", received, std::chrono::duration<double>(end - start).count());
}

void tcp_client(const std::string &host, int port, const std::string &input_path) {
    Fd input(open(input_path.c_str(), O_RDONLY));
    if (input.get() < 0) {
        throw_errno("open input");
    }
    const std::uint64_t size = file_size(input.get());
    Fd conn(connect_tcp(host, port));

    std::vector<unsigned char> size_buffer(8);
    put_u64(size_buffer, 0, size);
    write_all(conn.get(), size_buffer.data(), size_buffer.size());

    std::vector<unsigned char> buffer(kBufferSize);
    std::uint64_t sent = 0;
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const ssize_t n = read(input.get(), buffer.data(), buffer.size());
        if (n < 0) {
            throw_errno("read input");
        }
        if (n == 0) {
            break;
        }
        write_all(conn.get(), buffer.data(), static_cast<std::size_t>(n));
        sent += static_cast<std::uint64_t>(n);
    }
    const auto end = std::chrono::steady_clock::now();
    print_rate("tcp client sent", sent, std::chrono::duration<double>(end - start).count());
}

void udp_server(int port, const std::string &output_path) {
    Fd sock(socket(AF_INET, SOCK_DGRAM, 0));
    if (sock.get() < 0) {
        throw_errno("socket");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(sock.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        throw_errno("bind");
    }

    timeval timeout{};
    timeout.tv_sec = 5;
    if (setsockopt(sock.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        throw_errno("setsockopt(SO_RCVTIMEO)");
    }

    Fd output(open(output_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644));
    if (output.get() < 0) {
        throw_errno("open output");
    }

    std::vector<unsigned char> packet(kUdpHeaderSize + kUdpPayloadSize);
    std::vector<unsigned char> received;
    std::uint64_t file_size_value = 0;
    std::uint64_t received_bytes = 0;
    std::uint32_t total = 0;
    std::uint32_t count = 0;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        const ssize_t n = recvfrom(sock.get(), packet.data(), packet.size(), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            throw_errno("recvfrom");
        }
        if (static_cast<std::size_t>(n) < kUdpHeaderSize ||
            !std::equal(std::begin(kUdpMagic), std::end(kUdpMagic), packet.begin())) {
            continue;
        }
        const std::uint32_t seq = get_u32(packet.data(), 4);
        const std::uint32_t packet_total = get_u32(packet.data(), 8);
        const std::uint32_t payload = get_u32(packet.data(), 12);
        const std::uint64_t packet_file_size = get_u64(packet.data(), 16);
        if (static_cast<std::size_t>(n) != kUdpHeaderSize + payload || seq >= packet_total) {
            continue;
        }
        if (received.empty()) {
            total = packet_total;
            file_size_value = packet_file_size;
            received.assign(total, 0);
            if (ftruncate(output.get(), static_cast<off_t>(file_size_value)) < 0) {
                throw_errno("ftruncate");
            }
            start = std::chrono::steady_clock::now();
        }
        if (!received[seq]) {
            const off_t offset = static_cast<off_t>(static_cast<std::uint64_t>(seq) * kUdpPayloadSize);
            const ssize_t written = pwrite(output.get(), packet.data() + kUdpHeaderSize, payload, offset);
            if (written < 0 || static_cast<std::uint32_t>(written) != payload) {
                throw_errno("pwrite");
            }
            received[seq] = 1;
            received_bytes += payload;
            ++count;
            if (count == total) {
                break;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    print_rate("udp server received", received_bytes, std::chrono::duration<double>(end - start).count());
    std::cout << "udp chunks=" << count << "/" << total << "\n";
}

void udp_client(const std::string &host, int port, const std::string &input_path) {
    Fd input(open(input_path.c_str(), O_RDONLY));
    if (input.get() < 0) {
        throw_errno("open input");
    }
    const std::uint64_t size = file_size(input.get());
    const std::uint32_t total = static_cast<std::uint32_t>((size + kUdpPayloadSize - 1) / kUdpPayloadSize);

    Fd sock(socket(AF_INET, SOCK_DGRAM, 0));
    if (sock.get() < 0) {
        throw_errno("socket");
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
        throw std::invalid_argument("only IPv4 numeric addresses are supported");
    }

    std::vector<unsigned char> packet(kUdpHeaderSize + kUdpPayloadSize);
    std::copy(std::begin(kUdpMagic), std::end(kUdpMagic), packet.begin());
    put_u32(packet, 8, total);
    put_u64(packet, 16, size);

    std::uint64_t sent = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t seq = 0; seq < total; ++seq) {
        const ssize_t n = read(input.get(), packet.data() + kUdpHeaderSize, kUdpPayloadSize);
        if (n < 0) {
            throw_errno("read input");
        }
        put_u32(packet, 4, seq);
        put_u32(packet, 12, static_cast<std::uint32_t>(n));
        const ssize_t written = sendto(sock.get(), packet.data(), kUdpHeaderSize + static_cast<std::size_t>(n), 0,
                                       reinterpret_cast<sockaddr *>(&target), sizeof(target));
        if (written < 0) {
            throw_errno("sendto");
        }
        sent += static_cast<std::uint64_t>(n);
    }
    const auto end = std::chrono::steady_clock::now();
    print_rate("udp client sent", sent, std::chrono::duration<double>(end - start).count());
}

void usage() {
    std::cerr << "Usage:\n"
              << "  file_transfer server tcp <port> <output_file>\n"
              << "  file_transfer client tcp <host> <port> <input_file>\n"
              << "  file_transfer server udp <port> <output_file>\n"
              << "  file_transfer client udp <host> <port> <input_file>\n";
}

}  // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 5) {
            usage();
            return 1;
        }
        const std::string role = argv[1];
        const std::string proto = argv[2];
        if (role == "server" && argc == 5) {
            const int port = parse_int(argv[3], "port");
            if (proto == "tcp") {
                tcp_server(port, argv[4]);
            } else if (proto == "udp") {
                udp_server(port, argv[4]);
            } else {
                throw std::invalid_argument("protocol must be tcp or udp");
            }
        } else if (role == "client" && argc == 6) {
            const int port = parse_int(argv[4], "port");
            if (proto == "tcp") {
                tcp_client(argv[3], port, argv[5]);
            } else if (proto == "udp") {
                udp_client(argv[3], port, argv[5]);
            } else {
                throw std::invalid_argument("protocol must be tcp or udp");
            }
        } else {
            usage();
            return 1;
        }
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
