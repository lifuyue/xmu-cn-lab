#include "rs232c.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

// 打印命令行帮助信息，方便手工演示编码与解码。
void printUsage() {
    std::cout
        << "Usage:\n"
        << "  rs232c_demo encode <message>\n"
        << "  rs232c_demo decode <v1> <v2> ...\n"
        << "Examples:\n"
        << "  rs232c_demo encode A\n"
        << "  rs232c_demo decode -12 12 -12 12 12 12 12 12 -12 -12\n";
}

// 把编码结果按空格分隔的方式打印出来，便于人工核对。
void printVoltSequence(const std::vector<double>& volts) {
    for (std::size_t index = 0; index < volts.size(); ++index) {
        if (index > 0) {
            std::cout << ' ';
        }
        std::cout << volts[index];
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 3) {
            printUsage();
            return 1;
        }

        const std::string mode = argv[1];

        if (mode == "encode") {
            const std::string message = argv[2];
            std::vector<double> volts(message.size() * 10);
            const int written = rs232c_encode(
                volts.data(),
                static_cast<int>(volts.size()),
                message.c_str(),
                static_cast<int>(message.size())
            );
            if (written < 0) {
                throw std::runtime_error("encode failed");
            }
            volts.resize(static_cast<std::size_t>(written));
            printVoltSequence(volts);
            return 0;
        }

        if (mode == "decode") {
            std::vector<double> volts;
            volts.reserve(static_cast<std::size_t>(argc - 2));
            for (int index = 2; index < argc; ++index) {
                volts.push_back(std::stod(argv[index]));
            }

            std::vector<char> buffer(volts.size() / 10 + 1, '\0');
            const int decoded = rs232c_decode(
                buffer.data(),
                static_cast<int>(buffer.size() - 1),
                volts.data(),
                static_cast<int>(volts.size())
            );
            if (decoded < 0) {
                throw std::runtime_error("decode failed");
            }

            std::cout.write(buffer.data(), decoded);
            std::cout << '\n';
            return 0;
        }

        printUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "rs232c_demo error: " << ex.what() << '\n';
        return 1;
    }
}
