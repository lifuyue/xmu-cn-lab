#include "rs232c.hpp"

#include <cstddef>

namespace {

// 标准 RS232C 中，逻辑 1 通常用负电压表示，逻辑 0 用正电压表示。
constexpr double kVoltageOne = -12.0;
constexpr double kVoltageZero = 12.0;

// 每个字符对应的固定帧长度：
// 1 个空闲位 + 1 个起始位 + 7 个数据位 + 1 个终止位 = 10
constexpr int kFrameWidth = 10;

// 空闲位和终止位都是逻辑 1，起始位是逻辑 0。
constexpr int kIdleBit = 1;
constexpr int kStartBit = 0;
constexpr int kStopBit = 1;

// 根据逻辑位值返回对应的 RS232C 电压。
double bitToVoltage(int bit) {
    return bit == 1 ? kVoltageOne : kVoltageZero;
}

// 根据电压值恢复逻辑位。
// 这里以 0V 为分界线：
// - 小于 0 视为逻辑 1
// - 大于等于 0 视为逻辑 0
int voltageToBit(double voltage) {
    return voltage < 0.0 ? 1 : 0;
}

// 判断一个字符是否属于 7-bit ASCII。
bool isSevenBitAscii(unsigned char ch) {
    return (ch & 0x80U) == 0;
}

}  // namespace

int rs232c_encode(double* volts, int volts_size, const char* msg, int size) {
    // 负长度一定非法。
    if (size < 0 || volts_size < 0) {
        return -1;
    }

    // 空消息直接返回 0，不要求调用者必须传非空指针。
    if (size == 0) {
        return 0;
    }

    // 只要需要真正读写数据，对应指针就必须有效。
    if (volts == nullptr || msg == nullptr) {
        return -1;
    }

    // 每个字符固定需要 10 个电压值。
    const int required = size * kFrameWidth;
    if (volts_size < required) {
        return -1;
    }

    int outIndex = 0;
    for (int charIndex = 0; charIndex < size; ++charIndex) {
        const unsigned char ch = static_cast<unsigned char>(msg[charIndex]);
        if (!isSevenBitAscii(ch)) {
            return -1;
        }

        // 先写空闲位，表示线路处于空闲状态。
        volts[outIndex++] = bitToVoltage(kIdleBit);
        // 再写起始位，通知接收端一个新字符开始。
        volts[outIndex++] = bitToVoltage(kStartBit);

        // 数据位按 LSB -> MSB 的顺序输出，即位小端序。
        for (int bitIndex = 0; bitIndex < 7; ++bitIndex) {
            const int bit = (ch >> bitIndex) & 0x01;
            volts[outIndex++] = bitToVoltage(bit);
        }

        // 最后写终止位，表示一个字符结束。
        volts[outIndex++] = bitToVoltage(kStopBit);
    }

    return outIndex;
}

int rs232c_decode(char* msg, int size, const double* volts, int volts_size) {
    // 基本参数校验。
    if (size < 0 || volts_size < 0) {
        return -1;
    }

    // 空输入电压序列直接解码为空消息。
    if (volts_size == 0) {
        return 0;
    }

    if (volts == nullptr || msg == nullptr) {
        return -1;
    }

    // 因为每帧恰好 10 个电压值，所以总长度必须是 10 的整数倍。
    if (volts_size % kFrameWidth != 0) {
        return -1;
    }

    const int charCount = volts_size / kFrameWidth;
    if (size < charCount) {
        return -1;
    }

    int inIndex = 0;
    for (int charIndex = 0; charIndex < charCount; ++charIndex) {
        // 逐段读取一帧：空闲位、起始位、7 位数据、终止位。
        const int idleBit = voltageToBit(volts[inIndex++]);
        const int startBit = voltageToBit(volts[inIndex++]);

        // 严格验证帧结构是否符合实验约定。
        if (idleBit != kIdleBit || startBit != kStartBit) {
            return -1;
        }

        unsigned char ch = 0;
        for (int bitIndex = 0; bitIndex < 7; ++bitIndex) {
            const int bit = voltageToBit(volts[inIndex++]);
            ch |= static_cast<unsigned char>(bit << bitIndex);
        }

        const int stopBit = voltageToBit(volts[inIndex++]);
        if (stopBit != kStopBit) {
            return -1;
        }

        // 这里得到的字符必然仍然处于 7-bit 范围内。
        msg[charIndex] = static_cast<char>(ch);
    }

    return charCount;
}
