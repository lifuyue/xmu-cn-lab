#include "color_channel.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// 检查 binary::encode 是否会正确拒绝非法输入。
void expectThrowBinary(int msg) {
    bool thrown = false;
    try {
        (void)binary::encode(msg);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    assert(thrown);
}

// 检查 octal::encode 是否会正确拒绝非法输入。
void expectThrowOctal(int msg) {
    bool thrown = false;
    try {
        (void)octal::encode(msg);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    assert(thrown);
}

// 测试二进制协议在文件模式下的完整流程：
// 编码 -> 生成图片 -> 写文件 -> 读文件 -> 采样 -> 解码。
void testBinaryRoundTrip() {
    channel::SenderOptions senderOptions;
    senderOptions.mode = channel::SenderMode::File;
    channel::ReceiverOptions receiverOptions;
    receiverOptions.mode = channel::ReceiverMode::File;

    for (int value = 0; value <= 1; ++value) {
        const std::string path = "output/test_binary_" + std::to_string(value) + ".png";
        senderOptions.outputPath = path;
        receiverOptions.inputPath = path;
        channel::setSenderOptions(senderOptions);
        channel::setReceiverOptions(receiverOptions);
        // 解码结果必须和发送值完全一致。
        binary::send(value);
        assert(binary::receive() == value);
    }
}

// 测试八进制协议在文件模式下的完整流程，覆盖 0 到 7 全部符号。
void testOctalRoundTrip() {
    channel::SenderOptions senderOptions;
    senderOptions.mode = channel::SenderMode::File;
    channel::ReceiverOptions receiverOptions;
    receiverOptions.mode = channel::ReceiverMode::File;

    for (int value = 0; value <= 7; ++value) {
        const std::string path = "output/test_octal_" + std::to_string(value) + ".png";
        senderOptions.outputPath = path;
        receiverOptions.inputPath = path;
        channel::setSenderOptions(senderOptions);
        channel::setReceiverOptions(receiverOptions);
        // 每一个标准颜色都应该能经过文件往返后仍然解码回原值。
        octal::send(value);
        assert(octal::receive() == value);
    }
}

// 测试两个关键行为：
// 1. 合法颜色在有轻微扰动时仍应识别成功
// 2. 明显不属于调色板的颜色应在八进制模式下返回 -1
void testToleranceAndRejects() {
    assert(binary::decode(cv::Scalar(5, 5, 5)) == 1);
    assert(binary::decode(cv::Scalar(250, 250, 250)) == 0);
    assert(octal::decode(cv::Scalar(10, 10, 245)) == 2);
    assert(octal::decode(cv::Scalar(127, 127, 127)) == -1);
}

// 测试编码函数对非法输入的边界检查。
void testInvalidInputs() {
    expectThrowBinary(-1);
    expectThrowBinary(2);
    expectThrowOctal(-1);
    expectThrowOctal(8);
}

}  // namespace

int main() {
    // 按固定顺序执行测试，便于定位失败位置。
    testBinaryRoundTrip();
    testOctalRoundTrip();
    testToleranceAndRejects();
    testInvalidInputs();
    std::cout << "All round-trip tests passed.\n";
    return 0;
}
