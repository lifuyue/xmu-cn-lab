#include "color_channel.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// 题目要求发送和接收接口必须保持为 `send(int)` 和 `receive()`。
// 为了在不改接口的前提下支持文件模式、屏幕模式和摄像头模式，
// 这里把运行时配置保存在模块级全局变量里。
channel::SenderOptions g_senderOptions;
channel::ReceiverOptions g_receiverOptions;

// 二进制解码时使用的亮度阈值。
// 小于这个阈值认为是“黑”，否则认为是“白”。
constexpr double kBinaryThreshold = 128.0;
// 八进制解码时使用的颜色距离阈值。
// 如果采样颜色到所有标准颜色的距离都大于这个值，就认为识别失败。
constexpr double kOctalDistanceThreshold = 120.0;

// 计算一个 BGR 颜色的亮度值。
// 注意 OpenCV 中颜色顺序是 B、G、R，不是常见的 R、G、B。
double luminance(const cv::Scalar& color) {
    return 0.114 * color[0] + 0.587 * color[1] + 0.299 * color[2];
}

// 计算两个 BGR 颜色之间的欧氏距离。
// 八进制解码时就用这个距离来寻找“最接近”的标准颜色。
double colorDistance(const cv::Scalar& lhs, const cv::Scalar& rhs) {
    const double db = lhs[0] - rhs[0];
    const double dg = lhs[1] - rhs[1];
    const double dr = lhs[2] - rhs[2];
    return std::sqrt(db * db + dg * dg + dr * dr);
}

// 确保输出图片所在的父目录存在。
// 这样即使 `output/` 目录还没有创建，文件模式也能正常写出图片。
void ensureParentDirectory(const std::string& path) {
    const std::filesystem::path filePath(path);
    const std::filesystem::path parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

// 把渲染好的纯色图像全屏显示出来。
// 发送端用它来模拟“发光面板”。
void showColorFullscreen(const cv::Mat& frame) {
    const std::string windowName = "sender";
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    cv::setWindowProperty(windowName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    cv::imshow(windowName, frame);
    cv::waitKey(0);
    cv::destroyWindow(windowName);
}

template <typename EncodeFn>
void sendWithCodec(int msg, EncodeFn encodeFn) {
    // 第一步：先把消息符号映射成纯颜色。
    const cv::Scalar color = encodeFn(msg);
    // 第二步：用这个颜色生成一整张纯色图。
    const channel::SenderOptions options = channel::getSenderOptions();
    cv::Mat frame = channel::renderColorFrame(color, options.frameSize);

    if (options.mode == channel::SenderMode::File) {
        // 文件模式下，通过保存图片来模拟一次发送。
        ensureParentDirectory(options.outputPath);
        if (!cv::imwrite(options.outputPath, frame)) {
            throw std::runtime_error("failed to write image: " + options.outputPath);
        }
        return;
    }

    // 屏幕模式下，通过全屏显示来模拟发光二极管。
    showColorFullscreen(frame);
}

template <typename DecodeFn>
int receiveFromFile(DecodeFn decodeFn) {
    // 文件模式下，读取图片并对中心区域做颜色识别。
    const channel::ReceiverOptions options = channel::getReceiverOptions();
    cv::Mat frame = cv::imread(options.inputPath, cv::IMREAD_COLOR);
    if (frame.empty()) {
        throw std::runtime_error("failed to read image: " + options.inputPath);
    }
    return decodeFn(channel::sampleCenterColor(frame));
}

template <typename DecodeFn>
int receiveFromCamera(DecodeFn decodeFn) {
    // 摄像头模式下，持续采集视频帧，
    // 直到连续多帧识别结果一致，才认为识别成功。
    const channel::ReceiverOptions options = channel::getReceiverOptions();
    cv::VideoCapture capture(options.cameraIndex);
    if (!capture.isOpened()) {
        throw std::runtime_error("failed to open camera index " + std::to_string(options.cameraIndex));
    }

    const std::string windowName = "receiver";
    if (options.showPreview) {
        cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    }

    int lastDecoded = std::numeric_limits<int>::min();
    int stableCount = 0;

    while (true) {
        cv::Mat frame;
        capture >> frame;
        // 遇到空帧时直接跳过，继续等待下一帧。
        if (frame.empty()) {
            continue;
        }

        // 题目约定使用图像中心区域作为通信区域。
        // 这里既要用它做采样，也要在预览图上把它画出来。
        const int roiX = frame.cols / 4;
        const int roiY = frame.rows / 4;
        const int roiWidth = frame.cols / 2;
        const int roiHeight = frame.rows / 2;
        const cv::Rect roi(roiX, roiY, roiWidth, roiHeight);
        // sampleCenterColor() 会计算中心区域的平均 BGR 值。
        const cv::Scalar sampledColor = channel::sampleCenterColor(frame);
        const int decoded = decodeFn(sampledColor);

        // 为了避免摄像头抖动造成误判，这里要求结果连续稳定若干帧。
        if (decoded == lastDecoded) {
            ++stableCount;
        } else {
            lastDecoded = decoded;
            stableCount = 1;
        }

        if (options.showPreview) {
            // 预览窗口用于显示当前采样区域和当前识别结果，
            // 方便实验演示和调试。
            cv::Mat preview = frame.clone();
            cv::rectangle(preview, roi, cv::Scalar(0, 255, 255), 2);
            std::ostringstream text;
            text << "decoded=" << decoded << " stable=" << stableCount
                 << " avg=" << channel::scalarToText(sampledColor);
            cv::putText(
                preview,
                text.str(),
                cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX,
                0.8,
                cv::Scalar(0, 255, 255),
                2
            );
            cv::imshow(windowName, preview);
        }

        // 只有在结果有效且连续稳定时，才真正返回识别值。
        // 八进制无法匹配时会返回 -1。
        if (decoded != -1 && stableCount >= options.stableFrames) {
            if (options.showPreview) {
                cv::destroyWindow(windowName);
            }
            return decoded;
        }

        // 用户可以按 Esc 提前退出接收流程。
        const int key = cv::waitKey(options.cameraDelayMs);
        if (key == 27) {
            break;
        }
    }

    if (options.showPreview) {
        cv::destroyWindow(windowName);
    }
    return -1;
}

template <typename DecodeFn>
int receiveWithCodec(DecodeFn decodeFn) {
    // 公共接收入口：根据配置决定走文件模式还是摄像头模式。
    const channel::ReceiverOptions options = channel::getReceiverOptions();
    if (options.mode == channel::ReceiverMode::File) {
        return receiveFromFile(decodeFn);
    }
    return receiveFromCamera(decodeFn);
}

const std::vector<cv::Scalar>& octalPalette() {
    // OpenCV 的颜色顺序是 BGR。
    // 这里严格按照题目要求给出八进制位与颜色的固定映射：
    // 0 黑，1 白，2 红，3 蓝，4 绿，5 紫，6 黄，7 青。
    static const std::vector<cv::Scalar> palette = {
        cv::Scalar(0, 0, 0),
        cv::Scalar(255, 255, 255),
        cv::Scalar(0, 0, 255),
        cv::Scalar(255, 0, 0),
        cv::Scalar(0, 255, 0),
        cv::Scalar(255, 0, 255),
        cv::Scalar(0, 255, 255),
        cv::Scalar(255, 255, 0),
    };
    return palette;
}

}  // namespace

namespace channel {

void setSenderOptions(const SenderOptions& options) {
    // 保存最新的发送配置，供 send(int) 在运行时读取。
    g_senderOptions = options;
}

void setReceiverOptions(const ReceiverOptions& options) {
    // 保存最新的接收配置，供 receive() 在运行时读取。
    g_receiverOptions = options;
}

SenderOptions getSenderOptions() {
    // 返回当前生效的发送配置副本。
    return g_senderOptions;
}

ReceiverOptions getReceiverOptions() {
    // 返回当前生效的接收配置副本。
    return g_receiverOptions;
}

cv::Mat renderColorFrame(const cv::Scalar& color, const cv::Size& size) {
    // 创建一张被同一种 BGR 颜色填满的整图。
    return cv::Mat(size, CV_8UC3, color).clone();
}

cv::Scalar sampleCenterColor(const cv::Mat& frame) {
    if (frame.empty()) {
        throw std::invalid_argument("cannot sample empty frame");
    }

    // 只取中心 50% x 50% 区域做均值，
    // 可以减小屏幕边框、背景杂色和视角偏移带来的干扰。
    const int roiX = frame.cols / 4;
    const int roiY = frame.rows / 4;
    const int roiWidth = frame.cols / 2;
    const int roiHeight = frame.rows / 2;
    const cv::Rect roi(roiX, roiY, roiWidth, roiHeight);
    return cv::mean(frame(roi));
}

std::string scalarToText(const cv::Scalar& color) {
    // 把颜色格式化成字符串，便于叠加到预览图或打印到日志。
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "(" << color[0] << ", " << color[1] << ", " << color[2] << ")";
    return out.str();
}

}  // namespace channel

namespace binary {

cv::Scalar encode(int msg) {
    // 题目规定：
    // 黑色表示比特 1
    // 白色表示比特 0
    if (msg == 1) {
        return cv::Scalar(0, 0, 0);
    }
    if (msg == 0) {
        return cv::Scalar(255, 255, 255);
    }
    throw std::invalid_argument("binary::encode expects 0 or 1");
}

int decode(cv::Scalar color) {
    // 亮度低则判为黑色，解码为 1；
    // 否则判为白色，解码为 0。
    return luminance(color) < kBinaryThreshold ? 1 : 0;
}

void send(int msg) {
    // 二进制发送接口，本质上复用公共发送流程。
    sendWithCodec(msg, encode);
}

int receive() {
    // 二进制接收接口，本质上复用公共接收流程。
    return receiveWithCodec(decode);
}

}  // namespace binary

namespace octal {

cv::Scalar encode(int msg) {
    // 八进制协议只有 0 到 7 这 8 个合法符号。
    if (msg < 0 || msg > 7) {
        throw std::invalid_argument("octal::encode expects a value in [0, 7]");
    }
    return octalPalette()[static_cast<std::size_t>(msg)];
}

int decode(cv::Scalar color) {
    // 在 8 个标准颜色中寻找“距离最近”的那个颜色。
    // 这样可以容忍摄像头采样时产生的小幅颜色偏差。
    const auto& palette = octalPalette();
    int bestIndex = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < palette.size(); ++index) {
        const double distance = colorDistance(color, palette[index]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = static_cast<int>(index);
        }
    }
    // 即使找到了最近颜色，如果距离仍然太大，也说明它不够像任何一个合法颜色。
    return bestDistance <= kOctalDistanceThreshold ? bestIndex : -1;
}

void send(int msg) {
    // 八进制发送接口，本质上复用公共发送流程。
    sendWithCodec(msg, encode);
}

int receive() {
    // 八进制接收接口，本质上复用公共接收流程。
    return receiveWithCodec(decode);
}

}  // namespace octal
