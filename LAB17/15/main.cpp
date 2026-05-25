#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Endpoint {
    std::string ip;
    int port = 0;
};

struct FtpReply {
    int code = 0;
    std::string text;
};

std::vector<int> parse_numbers(const std::string &text) {
    std::vector<int> values;
    for (std::size_t i = 0; i < text.size();) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        int value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = value * 10 + (text[i] - '0');
            ++i;
        }
        values.push_back(value);
    }
    return values;
}

FtpReply parse_reply(const std::string &line) {
    if (line.size() < 3 || !std::isdigit(line[0]) || !std::isdigit(line[1]) || !std::isdigit(line[2])) {
        throw std::invalid_argument("FTP reply must begin with a three-digit code");
    }
    return {std::stoi(line.substr(0, 3)), line.size() > 4 ? line.substr(4) : ""};
}

Endpoint endpoint_from_six_numbers(const std::vector<int> &values) {
    if (values.size() < 6) {
        throw std::invalid_argument("FTP endpoint needs six numbers");
    }
    for (int value : values) {
        if (value < 0 || value > 255) {
            throw std::invalid_argument("FTP endpoint number must be in [0, 255]");
        }
    }
    std::ostringstream ip;
    ip << values[0] << "." << values[1] << "." << values[2] << "." << values[3];
    return {ip.str(), values[4] * 256 + values[5]};
}

Endpoint parse_port_command(const std::string &command) {
    return endpoint_from_six_numbers(parse_numbers(command));
}

Endpoint parse_pasv_reply(const std::string &reply) {
    std::size_t begin = reply.find('(');
    std::size_t end = reply.find(')', begin == std::string::npos ? 0 : begin);
    if (begin == std::string::npos || end == std::string::npos) {
        throw std::invalid_argument("PASV reply must contain a parenthesized endpoint");
    }
    return endpoint_from_six_numbers(parse_numbers(reply.substr(begin, end - begin + 1)));
}

std::string reply_class(int code) {
    switch (code / 100) {
        case 1:
            return "positive preliminary";
        case 2:
            return "positive completion";
        case 3:
            return "positive intermediate";
        case 4:
            return "transient negative";
        case 5:
            return "permanent negative";
        default:
            return "unknown";
    }
}

void print_endpoint(const std::string &label, const Endpoint &endpoint) {
    std::cout << label << endpoint.ip << ":" << endpoint.port << "\n";
}

}  // namespace

int main() {
    const std::string welcome = "220 ftp.example.net FTP service ready";
    const std::string user_ok = "331 Please specify the password";
    const std::string login_ok = "230 Login successful";
    const std::string active = "PORT 192,168,1,20,195,80";
    const std::string passive = "227 Entering Passive Mode (203,0,113,10,210,14)";

    for (const std::string &line : {welcome, user_ok, login_ok, passive}) {
        FtpReply reply = parse_reply(line);
        std::cout << reply.code << " -> " << reply_class(reply.code) << " | " << reply.text << "\n";
    }

    print_endpoint("Active mode client listens at: ", parse_port_command(active));
    print_endpoint("Passive mode server listens at: ", parse_pasv_reply(passive));

    std::cout << "\nIn active mode, the FTP client is the socket server for the data connection.\n"
              << "In passive mode, the FTP server is the socket server for the data connection.\n";
}
