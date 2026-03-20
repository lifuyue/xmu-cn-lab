#include "modulation.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using DigitalModulationFn = int (*)(double*, int, const unsigned char*, int);
using AnalogModulationFn = int (*)(double*, int, const double*, int);

bool parseInt(const char* text, int* value) {
    if (text == nullptr || value == nullptr) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (*text == '\0' || (end != nullptr && *end != '\0')) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

void printDoubleSequence(const std::vector<double>& values) {
    std::cout << std::fixed << std::setprecision(6);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            std::cout << ' ';
        }
        std::cout << values[index];
    }
    std::cout << '\n';
}

void printByteSequence(const std::vector<unsigned char>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            std::cout << ' ';
        }
        std::cout << static_cast<int>(values[index]);
    }
    std::cout << '\n';
}

int handleCover(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: modulation_demo cover <size>\n";
        return 1;
    }

    int size = 0;
    if (!parseInt(argv[2], &size) || size < 0) {
        std::cerr << "size must be a non-negative integer\n";
        return 1;
    }

    std::vector<double> cover(static_cast<std::size_t>(size), 0.0);
    if (generate_cover_signal(cover.data(), size) < 0) {
        std::cerr << "generate_cover_signal failed\n";
        return 1;
    }
    printDoubleSequence(cover);
    return 0;
}

int handleDigitalMessage(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: modulation_demo digital_msg <size>\n";
        return 1;
    }

    int size = 0;
    if (!parseInt(argv[2], &size) || size < 0) {
        std::cerr << "size must be a non-negative integer\n";
        return 1;
    }

    std::vector<unsigned char> message(static_cast<std::size_t>(size), 0U);
    if (simulate_digital_modulation_signal(message.data(), size) < 0) {
        std::cerr << "simulate_digital_modulation_signal failed\n";
        return 1;
    }
    printByteSequence(message);
    return 0;
}

int handleAnalogMessage(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: modulation_demo analog_msg <size>\n";
        return 1;
    }

    int size = 0;
    if (!parseInt(argv[2], &size) || size < 0) {
        std::cerr << "size must be a non-negative integer\n";
        return 1;
    }

    std::vector<double> message(static_cast<std::size_t>(size), 0.0);
    if (simulate_analog_modulation_signal(message.data(), size) < 0) {
        std::cerr << "simulate_analog_modulation_signal failed\n";
        return 1;
    }
    printDoubleSequence(message);
    return 0;
}

int handleModulate(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: modulation_demo modulate <df|da|dp|af|aa|ap> <cover_len> <msg_len>\n";
        return 1;
    }

    int cover_len = 0;
    int msg_len = 0;
    if (!parseInt(argv[3], &cover_len) || !parseInt(argv[4], &msg_len) ||
        cover_len < 0 || msg_len < 0) {
        std::cerr << "cover_len and msg_len must be non-negative integers\n";
        return 1;
    }

    std::vector<double> cover(static_cast<std::size_t>(cover_len), 0.0);
    const std::string scheme = argv[2];

    if (scheme == "df" || scheme == "da" || scheme == "dp") {
        std::vector<unsigned char> message(static_cast<std::size_t>(msg_len), 0U);
        if (simulate_digital_modulation_signal(message.data(), msg_len) < 0) {
            std::cerr << "simulate_digital_modulation_signal failed\n";
            return 1;
        }

        DigitalModulationFn fn = nullptr;
        if (scheme == "df") {
            fn = modulate_digital_frequency;
        } else if (scheme == "da") {
            fn = modulate_digital_amplitude;
        } else {
            fn = modulate_digital_phase;
        }

        if (fn(cover.data(), cover_len, message.data(), msg_len) < 0) {
            std::cerr << "digital modulation failed\n";
            return 1;
        }
    } else if (scheme == "af" || scheme == "aa" || scheme == "ap") {
        std::vector<double> message(static_cast<std::size_t>(msg_len), 0.0);
        if (simulate_analog_modulation_signal(message.data(), msg_len) < 0) {
            std::cerr << "simulate_analog_modulation_signal failed\n";
            return 1;
        }

        AnalogModulationFn fn = nullptr;
        if (scheme == "af") {
            fn = modulate_analog_frequency;
        } else if (scheme == "aa") {
            fn = modulate_analog_amplitude;
        } else {
            fn = modulate_analog_phase;
        }

        if (fn(cover.data(), cover_len, message.data(), msg_len) < 0) {
            std::cerr << "analog modulation failed\n";
            return 1;
        }
    } else {
        std::cerr << "unknown modulation scheme: " << scheme << '\n';
        return 1;
    }

    printDoubleSequence(cover);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: modulation_demo <cover|digital_msg|analog_msg|modulate> ...\n";
        return 1;
    }

    const std::string command = argv[1];
    if (command == "cover") {
        return handleCover(argc, argv);
    }
    if (command == "digital_msg") {
        return handleDigitalMessage(argc, argv);
    }
    if (command == "analog_msg") {
        return handleAnalogMessage(argc, argv);
    }
    if (command == "modulate") {
        return handleModulate(argc, argv);
    }

    std::cerr << "unknown command: " << command << '\n';
    return 1;
}
