#include "net.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

struct CommonOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 19097;
};

std::string process_id_text() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(getpid());
#endif
}

std::string one_line(std::string text) {
    const std::size_t pos = text.find('\n');
    if (pos != std::string::npos) {
        text.erase(pos);
    }
    return ex7::trim_copy(text);
}

std::string exchange_or_error(const CommonOptions &options, const std::string &line) {
    try {
        return ex7::exchange_line(options.host, options.port, line);
    } catch (const std::exception &ex) {
        return std::string("ERR NETWORK ") + ex.what() + "\n";
    }
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  software_a admin --user USER --password PASS --type TYPE [--host HOST] [--port PORT]\n"
        << "  software_a run --serial SERIAL [--client-id ID] [--heartbeat SEC] [--hold SEC] [--no-release] [--host HOST] [--port PORT]\n"
        << "  software_a status [--host HOST] [--port PORT]\n"
        << "\n"
        << "TYPE can be single, demo, team10, team50, or a positive number.\n";
}

std::string require_value(int argc, char **argv, int &i, const std::string &name) {
    if (i + 1 >= argc) {
        throw std::invalid_argument(name + " requires a value");
    }
    return argv[++i];
}

void parse_host_port(CommonOptions &options, const std::string &arg, int argc, char **argv, int &i) {
    if (arg == "--host") {
        options.host = require_value(argc, argv, i, arg);
    } else if (arg == "--port") {
        const int value = std::stoi(require_value(argc, argv, i, arg));
        if (value <= 0 || value > 65535) {
            throw std::invalid_argument("--port must be within 1-65535");
        }
        options.port = static_cast<std::uint16_t>(value);
    } else {
        throw std::invalid_argument("unknown argument: " + arg);
    }
}

int run_admin(int argc, char **argv) {
    CommonOptions options;
    std::string user;
    std::string password;
    std::string type;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--user") {
            user = require_value(argc, argv, i, arg);
        } else if (arg == "--password") {
            password = require_value(argc, argv, i, arg);
        } else if (arg == "--type") {
            type = require_value(argc, argv, i, arg);
        } else {
            parse_host_port(options, arg, argc, argv, i);
        }
    }

    if (user.empty() || password.empty() || type.empty()) {
        throw std::invalid_argument("admin mode requires --user, --password, and --type");
    }

    const std::string command = "ISSUE " + user + " " + password + " " + type;
    const std::string response = exchange_or_error(options, command);
    std::cout << "admin request: " << command << "\n";
    std::cout << "server reply: " << one_line(response) << "\n";
    return ex7::starts_with(response, "OK ") ? 0 : 1;
}

int run_status(int argc, char **argv) {
    CommonOptions options;
    for (int i = 2; i < argc; ++i) {
        parse_host_port(options, argv[i], argc, argv, i);
    }

    std::cout << exchange_or_error(options, "STATUS");
    return 0;
}

int run_client(int argc, char **argv) {
    CommonOptions options;
    std::string serial;
    std::string client_id = "client-" + process_id_text();
    int heartbeat_seconds = 2;
    int hold_seconds = 8;
    bool release_on_exit = true;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--serial") {
            serial = require_value(argc, argv, i, arg);
        } else if (arg == "--client-id") {
            client_id = require_value(argc, argv, i, arg);
        } else if (arg == "--heartbeat") {
            heartbeat_seconds = std::stoi(require_value(argc, argv, i, arg));
            if (heartbeat_seconds <= 0) {
                throw std::invalid_argument("--heartbeat must be positive");
            }
        } else if (arg == "--hold") {
            hold_seconds = std::stoi(require_value(argc, argv, i, arg));
            if (hold_seconds < 0) {
                throw std::invalid_argument("--hold must be non-negative");
            }
        } else if (arg == "--no-release") {
            release_on_exit = false;
        } else {
            parse_host_port(options, arg, argc, argv, i);
        }
    }

    if (serial.empty()) {
        throw std::invalid_argument("run mode requires --serial");
    }

    const std::string verify_command = "VERIFY " + serial + " " + client_id;
    std::string response = exchange_or_error(options, verify_command);
    std::cout << "verify: " << one_line(response) << "\n";
    if (!ex7::starts_with(response, "OK GRANTED")) {
        std::cout << "software A exits because the license server rejected this client.\n";
        return 2;
    }

    std::cout << "software A is running, client_id=" << client_id
              << ", heartbeat=" << heartbeat_seconds << "s"
              << ", hold=" << hold_seconds << "s\n";

    int elapsed = 0;
    while (elapsed < hold_seconds) {
        const int sleep_seconds = std::min(heartbeat_seconds, hold_seconds - elapsed);
        std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
        elapsed += sleep_seconds;

        response = exchange_or_error(options, "HEARTBEAT " + serial + " " + client_id);
        std::cout << "heartbeat t=+" << elapsed << "s: " << one_line(response) << "\n";
        if (ex7::starts_with(response, "ERR NETWORK")) {
            std::cout << "server unavailable, keep retrying so a restarted server can recover the session.\n";
        }
    }

    if (release_on_exit) {
        response = exchange_or_error(options, "RELEASE " + serial + " " + client_id);
        std::cout << "release: " << one_line(response) << "\n";
    } else {
        std::cout << "simulated abnormal exit: no RELEASE command was sent.\n";
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    try {
        ex7::SocketRuntime runtime;
        if (argc < 2 || std::string(argv[1]) == "--help") {
            print_usage();
            return argc < 2 ? 1 : 0;
        }

        const std::string mode = argv[1];
        if (mode == "admin") {
            return run_admin(argc, argv);
        }
        if (mode == "run") {
            return run_client(argc, argv);
        }
        if (mode == "status") {
            return run_status(argc, argv);
        }

        throw std::invalid_argument("unknown mode: " + mode);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        print_usage();
        return 1;
    }
}
