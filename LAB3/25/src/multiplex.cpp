#include "multiplex.hpp"

namespace {

int maxInt(int lhs, int rhs) {
    return lhs > rhs ? lhs : rhs;
}

unsigned char normalizeBit(unsigned char value) {
    return value == 0 ? 0U : 1U;
}

bool checkInputPointer(const unsigned char* ptr, int len) {
    return len == 0 || ptr != nullptr;
}

bool checkOutputPointer(unsigned char* ptr, int len) {
    return len == 0 || ptr != nullptr;
}

bool validateCommonLengths(int lhs, int rhs, int out) {
    return lhs >= 0 && rhs >= 0 && out >= 0;
}

int bitToChipValue(unsigned char bit) {
    return bit == 0 ? -1 : 1;
}

}  // namespace

namespace tdm_async {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len) {
    if (!validateCommonLengths(a_len, b_len, c_size)) {
        return -1;
    }

    const int required = a_len + b_len;
    if (c_size < required) {
        return -1;
    }
    if (!checkOutputPointer(c, required) || !checkInputPointer(a, a_len) ||
        !checkInputPointer(b, b_len)) {
        return -1;
    }
    if (required == 0) {
        return 0;
    }

    int out_index = 0;
    const int rounds = maxInt(a_len, b_len);
    for (int index = 0; index < rounds; ++index) {
        if (index < a_len) {
            c[out_index++] = normalizeBit(a[index]);
        }
        if (index < b_len) {
            c[out_index++] = static_cast<unsigned char>(2U + normalizeBit(b[index]));
        }
    }

    return out_index;
}

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len) {
    if (!validateCommonLengths(a_size, b_size, c_len)) {
        return -1;
    }

    const int expected = a_size + b_size;
    if (c_len != expected) {
        return -1;
    }
    if (!checkOutputPointer(a, a_size) || !checkOutputPointer(b, b_size) ||
        !checkInputPointer(c, c_len)) {
        return -1;
    }
    if (c_len == 0) {
        return 0;
    }

    int a_index = 0;
    int b_index = 0;
    for (int index = 0; index < c_len; ++index) {
        const unsigned char symbol = c[index];
        if (symbol > 3U) {
            return -1;
        }
        if (symbol < 2U) {
            if (a_index >= a_size) {
                return -1;
            }
            a[a_index++] = symbol;
        } else {
            if (b_index >= b_size) {
                return -1;
            }
            b[b_index++] = static_cast<unsigned char>(symbol - 2U);
        }
    }

    if (a_index != a_size || b_index != b_size) {
        return -1;
    }
    return c_len;
}

}  // namespace tdm_async

namespace tdm_sync {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len) {
    if (!validateCommonLengths(a_len, b_len, c_size)) {
        return -1;
    }

    const int rounds = maxInt(a_len, b_len);
    const int required = 2 * rounds;
    if (c_size < required) {
        return -1;
    }
    if (!checkOutputPointer(c, required) || !checkInputPointer(a, a_len) ||
        !checkInputPointer(b, b_len)) {
        return -1;
    }
    if (required == 0) {
        return 0;
    }

    int out_index = 0;
    for (int index = 0; index < rounds; ++index) {
        c[out_index++] = index < a_len ? normalizeBit(a[index]) : 2U;
        c[out_index++] = index < b_len ? normalizeBit(b[index]) : 2U;
    }

    return out_index;
}

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len) {
    if (!validateCommonLengths(a_size, b_size, c_len)) {
        return -1;
    }

    const int rounds = maxInt(a_size, b_size);
    const int expected = 2 * rounds;
    if (c_len != expected) {
        return -1;
    }
    if (!checkOutputPointer(a, a_size) || !checkOutputPointer(b, b_size) ||
        !checkInputPointer(c, c_len)) {
        return -1;
    }
    if (c_len == 0) {
        return 0;
    }

    for (int index = 0; index < rounds; ++index) {
        const unsigned char a_symbol = c[2 * index];
        const unsigned char b_symbol = c[2 * index + 1];
        if (a_symbol > 2U || b_symbol > 2U) {
            return -1;
        }

        if (index < a_size) {
            if (a_symbol == 2U) {
                return -1;
            }
            a[index] = a_symbol;
        } else if (a_symbol != 2U) {
            return -1;
        }

        if (index < b_size) {
            if (b_symbol == 2U) {
                return -1;
            }
            b[index] = b_symbol;
        } else if (b_symbol != 2U) {
            return -1;
        }
    }

    return c_len;
}

}  // namespace tdm_sync

namespace fdm {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len) {
    if (!validateCommonLengths(a_len, b_len, c_size)) {
        return -1;
    }

    const int required = maxInt(a_len, b_len);
    if (c_size < required) {
        return -1;
    }
    if (!checkOutputPointer(c, required) || !checkInputPointer(a, a_len) ||
        !checkInputPointer(b, b_len)) {
        return -1;
    }
    if (required == 0) {
        return 0;
    }

    for (int index = 0; index < required; ++index) {
        const unsigned char a_bit = index < a_len ? normalizeBit(a[index]) : 0U;
        const unsigned char b_bit = index < b_len ? normalizeBit(b[index]) : 0U;
        c[index] = static_cast<unsigned char>(a_bit + 2U * b_bit);
    }

    return required;
}

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len) {
    if (!validateCommonLengths(a_size, b_size, c_len)) {
        return -1;
    }

    const int expected = maxInt(a_size, b_size);
    if (c_len != expected) {
        return -1;
    }
    if (!checkOutputPointer(a, a_size) || !checkOutputPointer(b, b_size) ||
        !checkInputPointer(c, c_len)) {
        return -1;
    }
    if (c_len == 0) {
        return 0;
    }

    for (int index = 0; index < c_len; ++index) {
        const unsigned char symbol = c[index];
        if (symbol > 3U) {
            return -1;
        }

        const unsigned char a_bit = static_cast<unsigned char>(symbol & 0x01U);
        const unsigned char b_bit = static_cast<unsigned char>((symbol >> 1U) & 0x01U);

        if (index < a_size) {
            a[index] = a_bit;
        } else if (a_bit != 0U) {
            return -1;
        }

        if (index < b_size) {
            b[index] = b_bit;
        } else if (b_bit != 0U) {
            return -1;
        }
    }

    return c_len;
}

}  // namespace fdm

namespace cdm {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len) {
    if (!validateCommonLengths(a_len, b_len, c_size)) {
        return -1;
    }

    const int rounds = maxInt(a_len, b_len);
    const int required = 2 * rounds;
    if (c_size < required) {
        return -1;
    }
    if (!checkOutputPointer(c, required) || !checkInputPointer(a, a_len) ||
        !checkInputPointer(b, b_len)) {
        return -1;
    }
    if (required == 0) {
        return 0;
    }

    for (int index = 0; index < rounds; ++index) {
        const int a_amp = index < a_len ? bitToChipValue(normalizeBit(a[index])) : 0;
        const int b_amp = index < b_len ? bitToChipValue(normalizeBit(b[index])) : 0;
        const int chip0 = a_amp + b_amp;
        const int chip1 = a_amp - b_amp;
        c[2 * index] = static_cast<unsigned char>(chip0 + 2);
        c[2 * index + 1] = static_cast<unsigned char>(chip1 + 2);
    }

    return required;
}

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len) {
    if (!validateCommonLengths(a_size, b_size, c_len)) {
        return -1;
    }

    const int rounds = maxInt(a_size, b_size);
    const int expected = 2 * rounds;
    if (c_len != expected) {
        return -1;
    }
    if (!checkOutputPointer(a, a_size) || !checkOutputPointer(b, b_size) ||
        !checkInputPointer(c, c_len)) {
        return -1;
    }
    if (c_len == 0) {
        return 0;
    }

    for (int index = 0; index < rounds; ++index) {
        const unsigned char raw0 = c[2 * index];
        const unsigned char raw1 = c[2 * index + 1];
        if (raw0 > 4U || raw1 > 4U) {
            return -1;
        }

        const int chip0 = static_cast<int>(raw0) - 2;
        const int chip1 = static_cast<int>(raw1) - 2;
        const int a_corr = chip0 + chip1;
        const int b_corr = chip0 - chip1;

        if ((a_corr % 2) != 0 || (b_corr % 2) != 0) {
            return -1;
        }

        const int a_amp = a_corr / 2;
        const int b_amp = b_corr / 2;
        if (a_amp < -1 || a_amp > 1 || b_amp < -1 || b_amp > 1) {
            return -1;
        }

        if (index < a_size) {
            if (a_amp == 0) {
                return -1;
            }
            a[index] = static_cast<unsigned char>(a_amp > 0 ? 1U : 0U);
        } else if (a_amp != 0) {
            return -1;
        }

        if (index < b_size) {
            if (b_amp == 0) {
                return -1;
            }
            b[index] = static_cast<unsigned char>(b_amp > 0 ? 1U : 0U);
        } else if (b_amp != 0) {
            return -1;
        }
    }

    return c_len;
}

}  // namespace cdm
