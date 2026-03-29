#include "parity_check.hpp"

#include <cassert>
#include <iostream>

namespace {

void testPassWithEvenOnes() {
    const unsigned char msg[] = {1, 0, 1, 0};
    assert(parity_check(msg, 4) == 1);
}

void testFailWithOddOnes() {
    const unsigned char msg[] = {1, 0, 1, 1};
    assert(parity_check(msg, 4) == 0);
}

void testTreatNonZeroAsOne() {
    const unsigned char msg[] = {2, 0, 7, 8};
    assert(parity_check(msg, 4) == 0);
}

void testCheckDataWithParityBit() {
    const unsigned char passMsg[] = {1, 0, 1, 0, 0};
    assert(parity_check(passMsg, 5) == 1);

    const unsigned char failMsg[] = {1, 0, 1, 0, 1};
    assert(parity_check(failMsg, 5) == 0);
}

void testEdgeCases() {
    assert(parity_check(nullptr, 0) == 1);
    assert(parity_check(nullptr, 3) == 0);
    assert(parity_check(nullptr, -1) == 0);
}

}  // namespace

int main() {
    testPassWithEvenOnes();
    testFailWithOddOnes();
    testTreatNonZeroAsOne();
    testCheckDataWithParityBit();
    testEdgeCases();
    std::cout << "All parity_check tests passed.\n";
    return 0;
}
