#include "multiplex.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using EncodeFn = int (*)(unsigned char*, int, const unsigned char*, int, const unsigned char*, int);
using DecodeFn = int (*)(unsigned char*, int, unsigned char*, int, const unsigned char*, int);

struct SchemeOps {
    const char* name;
    EncodeFn encode;
    DecodeFn decode;
};

const SchemeOps kSchemes[] = {
    {"tdm_async", tdm_async::multiplex, tdm_async::demultiplex},
    {"tdm_sync", tdm_sync::multiplex, tdm_sync::demultiplex},
    {"fdm", fdm::multiplex, fdm::demultiplex},
    {"cdm", cdm::multiplex, cdm::demultiplex},
};

bool sequencesEqual(const std::vector<unsigned char>& lhs, const std::vector<unsigned char>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

void runRoundTrip(const SchemeOps& scheme,
                  const std::vector<unsigned char>& a,
                  const std::vector<unsigned char>& b) {
    std::vector<unsigned char> c(64, 0);
    const int written = scheme.encode(c.data(), static_cast<int>(c.size()),
                                      a.data(), static_cast<int>(a.size()),
                                      b.data(), static_cast<int>(b.size()));
    assert(written >= 0);
    c.resize(static_cast<std::size_t>(written));

    std::vector<unsigned char> restored_a(a.size(), 0);
    std::vector<unsigned char> restored_b(b.size(), 0);
    const int restored = scheme.decode(restored_a.data(), static_cast<int>(restored_a.size()),
                                       restored_b.data(), static_cast<int>(restored_b.size()),
                                       c.data(), static_cast<int>(c.size()));
    assert(restored == written);
    assert(sequencesEqual(restored_a, a));
    assert(sequencesEqual(restored_b, b));
}

void testEqualLengthRoundTrips() {
    const std::vector<unsigned char> a = {1, 0, 1, 1};
    const std::vector<unsigned char> b = {0, 1, 0, 1};
    for (const SchemeOps& scheme : kSchemes) {
        runRoundTrip(scheme, a, b);
    }
}

void testUnequalLengthRoundTrips() {
    const std::vector<unsigned char> a = {1, 0, 1, 1, 0};
    const std::vector<unsigned char> b = {0, 1};
    for (const SchemeOps& scheme : kSchemes) {
        runRoundTrip(scheme, a, b);
    }
}

void testNormalizationOfNonZeroValues() {
    const std::vector<unsigned char> a = {0, 7, 255, 0};
    const std::vector<unsigned char> b = {3, 0, 2};
    const std::vector<unsigned char> expected_a = {0, 1, 1, 0};
    const std::vector<unsigned char> expected_b = {1, 0, 1};

    for (const SchemeOps& scheme : kSchemes) {
        std::vector<unsigned char> c(64, 0);
        const int written = scheme.encode(c.data(), static_cast<int>(c.size()),
                                          a.data(), static_cast<int>(a.size()),
                                          b.data(), static_cast<int>(b.size()));
        assert(written >= 0);
        c.resize(static_cast<std::size_t>(written));

        std::vector<unsigned char> restored_a(expected_a.size(), 0);
        std::vector<unsigned char> restored_b(expected_b.size(), 0);
        const int restored = scheme.decode(restored_a.data(), static_cast<int>(restored_a.size()),
                                           restored_b.data(), static_cast<int>(restored_b.size()),
                                           c.data(), static_cast<int>(c.size()));
        assert(restored == written);
        assert(sequencesEqual(restored_a, expected_a));
        assert(sequencesEqual(restored_b, expected_b));
    }
}

void testEncodeBufferTooSmall() {
    const std::vector<unsigned char> a = {1, 0, 1};
    const std::vector<unsigned char> b = {0, 1};
    for (const SchemeOps& scheme : kSchemes) {
        unsigned char c[1] = {};
        assert(scheme.encode(c, 1, a.data(), static_cast<int>(a.size()),
                             b.data(), static_cast<int>(b.size())) == -1);
    }
}

void testDecodeSizeMismatch() {
    const std::vector<unsigned char> a = {1, 0, 1};
    const std::vector<unsigned char> b = {0, 1};
    for (const SchemeOps& scheme : kSchemes) {
        std::vector<unsigned char> c(64, 0);
        const int written = scheme.encode(c.data(), static_cast<int>(c.size()),
                                          a.data(), static_cast<int>(a.size()),
                                          b.data(), static_cast<int>(b.size()));
        assert(written >= 0);

        std::vector<unsigned char> wrong_a(a.size() - 1U, 0);
        std::vector<unsigned char> restored_b(b.size(), 0);
        assert(scheme.decode(wrong_a.data(), static_cast<int>(wrong_a.size()),
                             restored_b.data(), static_cast<int>(restored_b.size()),
                             c.data(), written) == -1);
    }
}

void testInvalidSymbols() {
    unsigned char out_a[2] = {};
    unsigned char out_b[2] = {};

    const unsigned char async_symbols[] = {4, 0};
    assert(tdm_async::demultiplex(out_a, 1, out_b, 1, async_symbols, 2) == -1);

    const unsigned char sync_symbols[] = {0, 3};
    assert(tdm_sync::demultiplex(out_a, 1, out_b, 1, sync_symbols, 2) == -1);

    const unsigned char fdm_symbols[] = {4};
    assert(fdm::demultiplex(out_a, 1, out_b, 0, fdm_symbols, 1) == -1);

    const unsigned char cdm_symbols[] = {5, 2};
    assert(cdm::demultiplex(out_a, 1, out_b, 0, cdm_symbols, 2) == -1);
}

void testTdmSyncPaddingValidation() {
    unsigned char out_a[3] = {};
    unsigned char out_b[1] = {};
    const unsigned char invalid[] = {1, 1, 0, 2, 1, 1};
    assert(tdm_sync::demultiplex(out_a, 3, out_b, 1, invalid, 6) == -1);
}

void testCdmLengthValidation() {
    unsigned char out_a[1] = {};
    unsigned char out_b[1] = {};
    const unsigned char odd_length[] = {2, 2, 2};
    assert(cdm::demultiplex(out_a, 1, out_b, 1, odd_length, 3) == -1);
}

void testZeroLength() {
    assert(tdm_async::multiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(tdm_sync::multiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(fdm::multiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(cdm::multiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);

    assert(tdm_async::demultiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(tdm_sync::demultiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(fdm::demultiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
    assert(cdm::demultiplex(nullptr, 0, nullptr, 0, nullptr, 0) == 0);
}

void testCliSmoke() {
    const int encode_status = std::system("./bin/multiplex_demo encode fdm 101 01 >/dev/null");
    const int decode_status = std::system("./bin/multiplex_demo decode fdm 3 2 1 2 1 >/dev/null");
    assert(encode_status == 0);
    assert(decode_status == 0);
}

}  // namespace

int main() {
    testEqualLengthRoundTrips();
    testUnequalLengthRoundTrips();
    testNormalizationOfNonZeroValues();
    testEncodeBufferTooSmall();
    testDecodeSizeMismatch();
    testInvalidSymbols();
    testTdmSyncPaddingValidation();
    testCdmLengthValidation();
    testZeroLength();
    testCliSmoke();
    std::cout << "All multiplex tests passed.\n";
    return 0;
}
