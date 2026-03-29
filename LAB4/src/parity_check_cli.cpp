#include "parity_check.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " bit0 [bit1 ...]\n";
        return 1;
    }

    unsigned char msg[1024] = {};
    const int length = argc - 1;
    if (length > 1024) {
        std::cerr << "Too many bits.\n";
        return 1;
    }

    for (int index = 0; index < length; ++index) {
        const char* token = argv[index + 1];
        if (token[0] == '0' && token[1] == '\0') {
            msg[index] = 0;
        } else {
            msg[index] = 1;
        }
    }

    std::cout << parity_check(msg, length) << '\n';
    return 0;
}
