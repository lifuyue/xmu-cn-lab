#include "parity_check.hpp"

int parity_check(const unsigned char* msg, const int msg_length) {
    if (msg_length < 0) {
        return 0;
    }

    if (msg_length == 0) {
        return 1;
    }

    if (msg == nullptr) {
        return 0;
    }

    unsigned char parity = 0;
    for (int index = 0; index < msg_length; ++index) {
        parity ^= (msg[index] != 0) ? 1U : 0U;
    }

    return parity == 0 ? 1 : 0;
}
