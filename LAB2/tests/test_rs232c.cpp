#include "rs232c.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

// 测试单个字符 A 的编码结果是否符合题目要求。
void testSingleCharEncoding() {
    const char msg[] = {'A'};
    double volts[10] = {};
    const int written = rs232c_encode(volts, 10, msg, 1);
    assert(written == 10);

    // 'A' 的 7-bit ASCII 是 1000001。
    // 按位小端序输出时，数据位顺序应为：1 0 0 0 0 0 1。
    const double expected[10] = {
        -12.0,  // 空闲位 1
        12.0,   // 起始位 0
        -12.0,  // bit0 = 1
        12.0,   // bit1 = 0
        12.0,   // bit2 = 0
        12.0,   // bit3 = 0
        12.0,   // bit4 = 0
        12.0,   // bit5 = 0
        -12.0,  // bit6 = 1
        -12.0   // 终止位 1
    };

    for (int index = 0; index < 10; ++index) {
        assert(volts[index] == expected[index]);
    }
}

// 测试多字符消息是否保持原始字节顺序，并且可以正确往返解码。
void testMultiCharRoundTrip() {
    const char msg[] = {'A', 'B'};
    double volts[20] = {};
    const int written = rs232c_encode(volts, 20, msg, 2);
    assert(written == 20);

    char decoded[2] = {};
    const int restored = rs232c_decode(decoded, 2, volts, written);
    assert(restored == 2);
    assert(decoded[0] == 'A');
    assert(decoded[1] == 'B');
}

// 测试编码时的参数错误和边界条件。
void testEncodeErrors() {
    const char msg[] = {'A'};
    double volts[9] = {};
    assert(rs232c_encode(volts, 9, msg, 1) == -1);

    const char invalidMsg[] = {static_cast<char>(0xFF)};
    double validVolts[10] = {};
    assert(rs232c_encode(validVolts, 10, invalidMsg, 1) == -1);

    assert(rs232c_encode(nullptr, 0, nullptr, 0) == 0);
}

// 测试解码时对非法帧和缓冲区不足的处理。
void testDecodeErrors() {
    const double validFrame[10] = {
        -12.0, 12.0, -12.0, 12.0, 12.0, 12.0, 12.0, 12.0, -12.0, -12.0
    };

    char buffer[1] = {};
    assert(rs232c_decode(buffer, 1, validFrame, 9) == -1);
    assert(rs232c_decode(buffer, 0, validFrame, 10) == -1);

    double brokenStart[10];
    std::memcpy(brokenStart, validFrame, sizeof(validFrame));
    brokenStart[1] = -12.0;
    assert(rs232c_decode(buffer, 1, brokenStart, 10) == -1);

    double brokenStop[10];
    std::memcpy(brokenStop, validFrame, sizeof(validFrame));
    brokenStop[9] = 12.0;
    assert(rs232c_decode(buffer, 1, brokenStop, 10) == -1);

    double brokenIdle[10];
    std::memcpy(brokenIdle, validFrame, sizeof(validFrame));
    brokenIdle[0] = 12.0;
    assert(rs232c_decode(buffer, 1, brokenIdle, 10) == -1);
}

// 测试解码阈值逻辑：负电压视为 1，非负电压视为 0。
void testVoltageThreshold() {
    const double frame[10] = {
        -5.0,   // 空闲位 -> 1
        0.0,    // 起始位 -> 0
        -5.0,   // bit0 = 1
        2.0,    // bit1 = 0
        2.0,    // bit2 = 0
        2.0,    // bit3 = 0
        2.0,    // bit4 = 0
        2.0,    // bit5 = 0
        -5.0,   // bit6 = 1
        -5.0    // 终止位 -> 1
    };

    char buffer[1] = {};
    const int restored = rs232c_decode(buffer, 1, frame, 10);
    assert(restored == 1);
    assert(buffer[0] == 'A');
}

}  // namespace

int main() {
    testSingleCharEncoding();
    testMultiCharRoundTrip();
    testEncodeErrors();
    testDecodeErrors();
    testVoltageThreshold();
    std::cout << "All RS232C tests passed.\n";
    return 0;
}
