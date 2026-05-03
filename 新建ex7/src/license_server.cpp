#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 19097;
    std::string db_path = "data/licenses.db";
    int timeout_seconds = 8;
};

struct Session {
    std::string client_id;
    std::int64_t last_seen = 0;
};

struct License {
    std::string serial;
    std::string username;
    std::uint64_t password_hash = 0;
    std::string type;
    int limit = 1;
    std::int64_t issued_at = 0;
    std::map<std::string, Session> sessions;
};

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string format_time(std::int64_t timestamp) {
    std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &value);
#else
    localtime_r(&value, &tm_value);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::vector<std::string> split(const std::string &text, char delimiter) {
    std::vector<std::string> parts;
    std::string item;
    std::istringstream iss(text);
    while (std::getline(iss, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

std::uint64_t fnv1a64(const std::string &text) {
    std::uint64_t value = 1469598103934665603ULL;
    for (unsigned char ch : text) {
        value ^= ch;
        value *= 1099511628211ULL;
    }
    return value;
}

int parse_license_limit(const std::string &type) {
    if (type == "single") {
        return 1;
    }
    if (type == "demo") {
        return 2;
    }
    if (type == "team10") {
        return 10;
    }
    if (type == "team50") {
        return 50;
    }

    try {
        const int value = std::stoi(type);
        if (value <= 0 || value > 1000) {
            throw std::out_of_range("license type out of range");
        }
        return value;
    } catch (const std::exception &) {
        throw std::invalid_argument("license type must be single, demo, team10, team50, or a positive number");
    }
}

bool token_safe(const std::string &value) {
    return !value.empty() &&
           value.find_first_of(" \t\r\n|") == std::string::npos;
}

class LicenseStore {
public:
    explicit LicenseStore(std::string db_path) : db_path_(std::move(db_path)) {
        load();
    }

    std::string issue(const std::string &username, const std::string &password, const std::string &type) {
        if (!token_safe(username) || !token_safe(type)) {
            return "ERR BAD_REQUEST username and type must be non-empty single tokens\n";
        }
        const int limit = parse_license_limit(type);
        const std::string serial = next_serial(username + password + type);

        License license;
        license.serial = serial;
        license.username = username;
        license.password_hash = fnv1a64(password);
        license.type = type;
        license.limit = limit;
        license.issued_at = unix_now();
        licenses_[serial] = license;
        save();

        std::ostringstream oss;
        oss << "OK SERIAL " << serial << " LIMIT " << limit << "\n";
        return oss.str();
    }

    std::string verify(const std::string &serial, const std::string &client_id, int timeout_seconds) {
        if (!token_safe(serial) || !token_safe(client_id)) {
            return "ERR BAD_REQUEST serial and client_id must be non-empty single tokens\n";
        }
        cleanup(timeout_seconds);
        auto it = licenses_.find(serial);
        if (it == licenses_.end()) {
            return "ERR UNKNOWN_SERIAL\n";
        }

        License &license = it->second;
        const std::int64_t now = unix_now();
        auto session_it = license.sessions.find(client_id);
        if (session_it != license.sessions.end()) {
            session_it->second.last_seen = now;
            save();
            return granted_response(license, client_id, "GRANTED");
        }

        if (static_cast<int>(license.sessions.size()) >= license.limit) {
            std::ostringstream oss;
            oss << "ERR REJECTED SERIAL " << serial << " ACTIVE " << license.sessions.size()
                << " LIMIT " << license.limit << "\n";
            return oss.str();
        }

        license.sessions[client_id] = Session{client_id, now};
        save();
        return granted_response(license, client_id, "GRANTED");
    }

    std::string heartbeat(const std::string &serial, const std::string &client_id, int timeout_seconds) {
        cleanup(timeout_seconds);
        auto license_it = licenses_.find(serial);
        if (license_it == licenses_.end()) {
            return "ERR UNKNOWN_SERIAL\n";
        }

        License &license = license_it->second;
        auto session_it = license.sessions.find(client_id);
        if (session_it == license.sessions.end()) {
            return "ERR UNKNOWN_SESSION\n";
        }

        session_it->second.last_seen = unix_now();
        save();
        return granted_response(license, client_id, "ALIVE");
    }

    std::string release(const std::string &serial, const std::string &client_id) {
        auto license_it = licenses_.find(serial);
        if (license_it == licenses_.end()) {
            return "ERR UNKNOWN_SERIAL\n";
        }

        License &license = license_it->second;
        const std::size_t removed = license.sessions.erase(client_id);
        save();

        std::ostringstream oss;
        oss << "OK RELEASED SERIAL " << serial << " CLIENT " << client_id
            << " REMOVED " << removed << " ACTIVE " << license.sessions.size()
            << " LIMIT " << license.limit << "\n";
        return oss.str();
    }

    std::string status(int timeout_seconds) {
        cleanup(timeout_seconds);
        std::ostringstream oss;
        oss << "OK STATUS LICENSES " << licenses_.size() << "\n";
        for (const auto &[serial, license] : licenses_) {
            oss << "LICENSE serial=" << serial
                << " user=" << license.username
                << " type=" << license.type
                << " active=" << license.sessions.size() << "/" << license.limit
                << " issued=\"" << format_time(license.issued_at) << "\"\n";
            for (const auto &[client_id, session] : license.sessions) {
                oss << "SESSION serial=" << serial
                    << " client=" << client_id
                    << " last_seen=\"" << format_time(session.last_seen) << "\"\n";
            }
        }
        oss << "END\n";
        return oss.str();
    }

    bool cleanup(int timeout_seconds) {
        const std::int64_t now = unix_now();
        bool changed = false;
        for (auto &[serial, license] : licenses_) {
            (void)serial;
            for (auto it = license.sessions.begin(); it != license.sessions.end();) {
                if (now - it->second.last_seen > timeout_seconds) {
                    it = license.sessions.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
        if (changed) {
            save();
        }
        return changed;
    }

private:
    void load() {
        std::ifstream input(db_path_);
        if (!input) {
            return;
        }

        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const auto parts = split(line, '|');
            if (parts.empty()) {
                continue;
            }
            if (parts[0] == "LICENSE" && parts.size() == 7) {
                License license;
                license.serial = parts[1];
                license.username = parts[2];
                license.password_hash = static_cast<std::uint64_t>(std::stoull(parts[3]));
                license.type = parts[4];
                license.limit = std::stoi(parts[5]);
                license.issued_at = std::stoll(parts[6]);
                licenses_[license.serial] = license;
            } else if (parts[0] == "SESSION" && parts.size() == 4) {
                auto it = licenses_.find(parts[1]);
                if (it != licenses_.end()) {
                    it->second.sessions[parts[2]] = Session{parts[2], std::stoll(parts[3])};
                }
            }
        }
    }

    void save() const {
        std::ofstream output(db_path_, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("failed to write database: " + db_path_);
        }

        output << "# ex7 license server database\n";
        for (const auto &[serial, license] : licenses_) {
            output << "LICENSE|" << serial << "|" << license.username << "|"
                   << license.password_hash << "|" << license.type << "|"
                   << license.limit << "|" << license.issued_at << "\n";
            for (const auto &[client_id, session] : license.sessions) {
                output << "SESSION|" << serial << "|" << client_id << "|"
                       << session.last_seen << "\n";
            }
        }
    }

    std::string next_serial(const std::string &salt) const {
        const std::uint64_t seed =
            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
            fnv1a64(salt);
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<std::uint64_t> dist(1000000000ULL, 9999999999ULL);

        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::string serial = std::to_string(dist(rng));
            if (licenses_.find(serial) == licenses_.end()) {
                return serial;
            }
        }
        throw std::runtime_error("failed to generate unique serial");
    }

    static std::string granted_response(const License &license,
                                        const std::string &client_id,
                                        const std::string &verb) {
        std::ostringstream oss;
        oss << "OK " << verb << " SERIAL " << license.serial
            << " CLIENT " << client_id
            << " ACTIVE " << license.sessions.size()
            << " LIMIT " << license.limit << "\n";
        return oss.str();
    }

    std::string db_path_;
    std::map<std::string, License> licenses_;
};

Options parse_options(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const std::string &name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(name + " requires a value");
            }
            return argv[++i];
        };

        if (arg == "--host") {
            options.host = need_value(arg);
        } else if (arg == "--port") {
            const int value = std::stoi(need_value(arg));
            if (value <= 0 || value > 65535) {
                throw std::invalid_argument("--port must be within 1-65535");
            }
            options.port = static_cast<std::uint16_t>(value);
        } else if (arg == "--db") {
            options.db_path = need_value(arg);
        } else if (arg == "--timeout") {
            options.timeout_seconds = std::stoi(need_value(arg));
            if (options.timeout_seconds <= 0) {
                throw std::invalid_argument("--timeout must be positive");
            }
        } else if (arg == "--help") {
            throw std::invalid_argument("Usage: license_server [--host 127.0.0.1] [--port 19097] [--db data/licenses.db] [--timeout seconds]");
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }
    return options;
}

std::string read_request(SocketHandle client_fd) {
    std::string request;
    char ch = '\0';
    while (request.size() < 4096) {
#ifdef _WIN32
        const int nread = recv(client_fd, &ch, 1, 0);
#else
        const ssize_t nread = recv(client_fd, &ch, 1, 0);
#endif
        if (nread <= 0) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        request.push_back(ch);
    }
    return ex7::trim_copy(request);
}

std::string handle_command(LicenseStore &store, const std::string &line, int timeout_seconds) {
    std::istringstream iss(line);
    std::string command;
    iss >> command;
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (command == "ISSUE") {
        std::string username;
        std::string password;
        std::string type;
        if (!(iss >> username >> password >> type)) {
            return "ERR BAD_REQUEST usage: ISSUE <username> <password> <type>\n";
        }
        return store.issue(username, password, type);
    }
    if (command == "VERIFY") {
        std::string serial;
        std::string client_id;
        if (!(iss >> serial >> client_id)) {
            return "ERR BAD_REQUEST usage: VERIFY <serial> <client_id>\n";
        }
        return store.verify(serial, client_id, timeout_seconds);
    }
    if (command == "HEARTBEAT") {
        std::string serial;
        std::string client_id;
        if (!(iss >> serial >> client_id)) {
            return "ERR BAD_REQUEST usage: HEARTBEAT <serial> <client_id>\n";
        }
        return store.heartbeat(serial, client_id, timeout_seconds);
    }
    if (command == "RELEASE") {
        std::string serial;
        std::string client_id;
        if (!(iss >> serial >> client_id)) {
            return "ERR BAD_REQUEST usage: RELEASE <serial> <client_id>\n";
        }
        return store.release(serial, client_id);
    }
    if (command == "STATUS") {
        return store.status(timeout_seconds);
    }
    if (command == "HELP") {
        return "OK COMMANDS ISSUE VERIFY HEARTBEAT RELEASE STATUS\n";
    }
    if (command.empty()) {
        return "ERR EMPTY_REQUEST\n";
    }
    return "ERR UNKNOWN_COMMAND\n";
}

void run_server(const Options &options) {
    ex7::SocketRuntime runtime;
    LicenseStore store(options.db_path);

    SocketHandle server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == kInvalidSocket) {
        throw std::runtime_error("failed to create socket: " + ex7::socket_error());
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        ex7::close_socket(server_fd);
        throw std::runtime_error("invalid IPv4 listen address: " + options.host);
    }

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        ex7::close_socket(server_fd);
        throw std::runtime_error("bind failed: " + ex7::socket_error());
    }
    if (listen(server_fd, 16) != 0) {
        ex7::close_socket(server_fd);
        throw std::runtime_error("listen failed: " + ex7::socket_error());
    }

    std::cout << std::unitbuf;
    std::cout << "license_server listening on " << options.host << ":" << options.port << "\n";
    std::cout << "database=" << options.db_path << " timeout=" << options.timeout_seconds << "s\n";

    while (!g_stop) {
        sockaddr_in client{};
#ifdef _WIN32
        int client_len = sizeof(client);
#else
        socklen_t client_len = sizeof(client);
#endif
        SocketHandle client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client), &client_len);
        if (client_fd == kInvalidSocket) {
            if (g_stop) {
                break;
            }
            std::cerr << "accept failed: " << ex7::socket_error() << "\n";
            continue;
        }

        char ip_text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client.sin_addr, ip_text, sizeof(ip_text));
        const std::string request = read_request(client_fd);
        std::string response;
        try {
            response = handle_command(store, request, options.timeout_seconds);
        } catch (const std::exception &ex) {
            response = std::string("ERR SERVER_ERROR ") + ex.what() + "\n";
        }
        ex7::send_all(client_fd, response);
        ex7::close_socket(client_fd);

        std::cout << "[" << format_time(unix_now()) << "] "
                  << ip_text << ":" << ntohs(client.sin_port)
                  << " request=\"" << request << "\" response=\""
                  << ex7::trim_copy(response.substr(0, response.find('\n'))) << "\"\n";
    }

    ex7::close_socket(server_fd);
}

}  // namespace

int main(int argc, char **argv) {
    try {
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        const Options options = parse_options(argc, argv);
        run_server(options);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
