#include "color_channel.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

// 打印发送端程序的命令行帮助信息。
void printUsage() {
    std::cout
        << "Usage: sender --scheme binary|octal --mode file|screen --msg N [--out path]\n"
        << "Examples:\n"
        << "  sender --scheme binary --mode file --msg 1 --out output/binary_1.png\n"
        << "  sender --scheme octal --mode screen --msg 6\n";
}

// 读取当前参数后面的值。
// 例如当 argv[index] 是 "--scheme" 时，
// 这个函数会返回它后面的 "binary"。
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
        // 如果用户没有传任何参数，就直接打印帮助并退出。
        if (argc == 1) {
            printUsage();
            return 1;
        }

        // 这些变量用来接收解析后的命令行配置。
        std::string scheme;
        std::string mode;
        std::string outputPath = "output/frame.png";
        int msg = -1;

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
            } else if (arg == "--msg") {
                msg = std::stoi(requireValue(argc, argv, i));
            } else if (arg == "--out") {
                outputPath = requireValue(argc, argv, i);
            } else {
                throw std::invalid_argument("unknown argument: " + arg);
            }
        }

        // 发送端必须知道协议类型、发送模式和要发送的消息值。
        // 这几个参数少一个都无法正常工作。
        if (scheme.empty() || mode.empty() || msg == -1) {
            throw std::invalid_argument("scheme, mode, and msg are required");
        }

        // 把解析得到的字符串配置转换成共享的 SenderOptions 结构。
        channel::SenderOptions options;
        options.mode = (mode == "file") ? channel::SenderMode::File : channel::SenderMode::Screen;
        if (mode != "file" && mode != "screen") {
            throw std::invalid_argument("mode must be file or screen");
        }
        options.outputPath = outputPath;
        channel::setSenderOptions(options);

        // 根据用户选择的协议，调用对应命名空间下的发送函数。
        if (scheme == "binary") {
            binary::send(msg);
        } else if (scheme == "octal") {
            octal::send(msg);
        } else {
            throw std::invalid_argument("scheme must be binary or octal");
        }

        // 发送完成后给出一个简单明确的提示。
        if (options.mode == channel::SenderMode::File) {
            std::cout << "saved frame to " << options.outputPath << '\n';
        } else {
            std::cout << "display finished\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        // 命令行参数错误和运行时错误都会统一从这里输出。
        std::cerr << "sender error: " << ex.what() << '\n';
        return 1;
    }
}
