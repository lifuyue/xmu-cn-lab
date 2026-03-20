#include "multiplex.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using EncodeFn = int (*)(unsigned char*, int, const unsigned char*, int, const unsigned char*, int);
using DecodeFn = int (*)(unsigned char*, int, unsigned char*, int, const unsigned char*, int);

struct SchemeOps {
    EncodeFn encode;
    DecodeFn decode;
};

bool parseBitString(const std::string& bits, std::vector<unsigned char>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    out->reserve(bits.size());
    for (char ch : bits) {
        if (ch != '0' && ch != '1') {
            return false;
        }
        out->push_back(static_cast<unsigned char>(ch - '0'));
    }
    return true;
}

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

bool getSchemeOps(const std::string& scheme, SchemeOps* ops) {
    if (ops == nullptr) {
        return false;
    }

    if (scheme == "tdm_async") {
        ops->encode = tdm_async::multiplex;
        ops->decode = tdm_async::demultiplex;
        return true;
    }
    if (scheme == "tdm_sync") {
        ops->encode = tdm_sync::multiplex;
        ops->decode = tdm_sync::demultiplex;
        return true;
    }
    if (scheme == "fdm") {
        ops->encode = fdm::multiplex;
        ops->decode = fdm::demultiplex;
        return true;
    }
    if (scheme == "cdm") {
        ops->encode = cdm::multiplex;
        ops->decode = cdm::demultiplex;
        return true;
    }
    return false;
}

void printSequence(const std::vector<unsigned char>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            std::cout << ' ';
        }
        std::cout << static_cast<int>(values[index]);
    }
    std::cout << '\n';
}

void printBitSequence(const std::vector<unsigned char>& values) {
    for (unsigned char value : values) {
        std::cout << static_cast<int>(value);
    }
    std::cout << '\n';
}

int handleEncode(const SchemeOps& ops, int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: multiplex_demo encode <scheme> <a_bits> <b_bits>\n";
        return 1;
    }

    std::vector<unsigned char> a;
    std::vector<unsigned char> b;
    if (!parseBitString(argv[3], &a) || !parseBitString(argv[4], &b)) {
        std::cerr << "bit strings must contain only 0 or 1\n";
        return 1;
    }

    const int capacity = 2 * static_cast<int>(a.size() + b.size() + 1U);
    std::vector<unsigned char> c(capacity, 0);
    const int written = ops.encode(c.data(), static_cast<int>(c.size()),
                                   a.data(), static_cast<int>(a.size()),
                                   b.data(), static_cast<int>(b.size()));
    if (written < 0) {
        std::cerr << "encode failed\n";
        return 1;
    }

    c.resize(static_cast<std::size_t>(written));
    printSequence(c);
    return 0;
}

int handleDecode(const SchemeOps& ops, int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: multiplex_demo decode <scheme> <a_size> <b_size> <c...>\n";
        return 1;
    }

    int a_size = 0;
    int b_size = 0;
    if (!parseInt(argv[3], &a_size) || !parseInt(argv[4], &b_size) || a_size < 0 || b_size < 0) {
        std::cerr << "sizes must be non-negative integers\n";
        return 1;
    }

    std::vector<unsigned char> c;
    c.reserve(static_cast<std::size_t>(argc - 5));
    for (int index = 5; index < argc; ++index) {
        int symbol = 0;
        if (!parseInt(argv[index], &symbol) || symbol < 0 || symbol > 255) {
            std::cerr << "symbols must be integers in [0, 255]\n";
            return 1;
        }
        c.push_back(static_cast<unsigned char>(symbol));
    }

    std::vector<unsigned char> a(static_cast<std::size_t>(a_size), 0);
    std::vector<unsigned char> b(static_cast<std::size_t>(b_size), 0);
    const int restored = ops.decode(a.data(), a_size, b.data(), b_size,
                                    c.data(), static_cast<int>(c.size()));
    if (restored < 0) {
        std::cerr << "decode failed\n";
        return 1;
    }

    std::cout << "A=";
    printBitSequence(a);
    std::cout << "B=";
    printBitSequence(b);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: multiplex_demo <encode|decode> <scheme> ...\n";
        return 1;
    }

    SchemeOps ops{};
    if (!getSchemeOps(argv[2], &ops)) {
        std::cerr << "unknown scheme: " << argv[2] << '\n';
        return 1;
    }

    const std::string command = argv[1];
    if (command == "encode") {
        return handleEncode(ops, argc, argv);
    }
    if (command == "decode") {
        return handleDecode(ops, argc, argv);
    }

    std::cerr << "unknown command: " << command << '\n';
    return 1;
}
