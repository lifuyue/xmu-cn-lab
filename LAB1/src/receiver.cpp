#include "color_channel.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// 打印接收端程序的命令行帮助信息。
void printUsage() {
    std::cout
        << "Usage: receiver --scheme binary|octal --mode file|camera [--in path] [--camera index] [--stable-frames N] [--no-preview]\n"
        << "Examples:\n"
        << "  receiver --scheme binary --mode file --in output/binary_1.png\n"
        << "  receiver --scheme octal --mode camera --camera 0 --stable-frames 5\n";
}

// 读取当前参数后面的值。
std::string requireValue(int argc, char* argv[], int& index) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value after ") + argv[index]);
    }
    ++index;
    return argv[index];
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        // 没有参数时直接输出帮助，让用户先看到正确用法。
        if (argc == 1) {
            printUsage();
            return 1;
        }

        // 先把命令行参数解析到这些变量里，稍后再统一组装成 ReceiverOptions。
        std::string scheme;
        std::string mode;
        std::string inputPath = "output/frame.png";
        int cameraIndex = 0;
        int stableFrames = 5;
        bool showPreview = true;

        // 逐个解析命令行参数。
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printUsage();
                return 0;
            }
            if (arg == "--scheme") {
                scheme = requireValue(argc, argv, i);
            } else if (arg == "--mode") {
                mode = requireValue(argc, argv, i);
            } else if (arg == "--in") {
                inputPath = requireValue(argc, argv, i);
            } else if (arg == "--camera") {
                cameraIndex = std::stoi(requireValue(argc, argv, i));
            } else if (arg == "--stable-frames") {
                stableFrames = std::stoi(requireValue(argc, argv, i));
            } else if (arg == "--no-preview") {
                showPreview = false;
            } else {
                throw std::invalid_argument("unknown argument: " + arg);
            }
        }

        // 接收端至少要知道使用哪种协议，以及从哪里接收数据。
        if (scheme.empty() || mode.empty()) {
            throw std::invalid_argument("scheme and mode are required");
        }
        // 连续稳定帧数必须大于 0，否则摄像头模式无法判断“稳定”。
        if (stableFrames <= 0) {
            throw std::invalid_argument("stable-frames must be positive");
        }

        // 把解析到的参数整理成共享的接收配置结构。
        channel::ReceiverOptions options;
        options.mode = (mode == "file") ? channel::ReceiverMode::File : channel::ReceiverMode::Camera;
        if (mode != "file" && mode != "camera") {
            throw std::invalid_argument("mode must be file or camera");
        }
        options.inputPath = inputPath;
        options.cameraIndex = cameraIndex;
        options.stableFrames = stableFrames;
        options.showPreview = showPreview;
        channel::setReceiverOptions(options);

        // 调用用户指定协议对应的接收函数。
        int decoded = -1;
        if (scheme == "binary") {
            decoded = binary::receive();
        } else if (scheme == "octal") {
            decoded = octal::receive();
        } else {
            throw std::invalid_argument("scheme must be binary or octal");
        }

        // 把最终识别结果打印到标准输出，
        // 方便手动查看，也方便脚本继续处理。
        std::cout << decoded << '\n';
        return decoded == -1 ? 2 : 0;
    } catch (const std::exception& ex) {
        // 所有参数错误和运行时错误都统一从这里输出。
        std::cerr << "receiver error: " << ex.what() << '\n';
        return 1;
    }
}
