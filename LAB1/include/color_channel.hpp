#ifndef COLOR_CHANNEL_HPP
#define COLOR_CHANNEL_HPP

#include <opencv2/core.hpp>

#include <string>

// 这个头文件定义了整个实验的公共接口。
// 其中：
// 1. `channel` 命名空间负责放置发送/接收共用的底层工具
// 2. `binary` 命名空间负责黑白二值通信
// 3. `octal` 命名空间负责八种颜色的八进制通信
//
// 这样设计的目的是让 CLI 程序和测试程序都能直接复用同一套接口。
namespace channel {

// SenderMode 表示发送端如何“发光”。
// File   : 生成一张纯色图片并保存到磁盘，作为文件模拟方式
// Screen : 在屏幕上全屏显示纯色，作为双机演示方式
enum class SenderMode {
    File,
    Screen
};

// ReceiverMode 表示接收端如何“收光”。
// File   : 从磁盘读取之前生成的图片进行识别
// Camera : 从摄像头持续读取视频帧，并识别中心区域
enum class ReceiverMode {
    File,
    Camera
};

// SenderOptions 保存 send() 运行时需要的发送配置。
// 因为题目要求发送接口固定为 `void send(int msg)`，
// 所以这里把模式、输出路径等信息放到一个全局配置结构里。
struct SenderOptions {
    // 发送模式：文件模式或者屏幕模式。
    SenderMode mode = SenderMode::File;
    // 当 mode == File 时，图片输出路径写到这里。
    std::string outputPath = "output/frame.png";
    // 发送端渲染图像时使用的尺寸。
    cv::Size frameSize = cv::Size(640, 640);
};

// ReceiverOptions 保存 receive() 运行时需要的接收配置。
// 同样因为题目要求接口固定为 `int receive()`，
// 所以接收模式、输入路径、摄像头编号等信息都集中放在这里。
struct ReceiverOptions {
    // 接收模式：文件模式或者摄像头模式。
    ReceiverMode mode = ReceiverMode::File;
    // 当 mode == File 时，接收端从这个图片路径读入。
    std::string inputPath = "output/frame.png";
    // 当 mode == Camera 时，OpenCV 打开哪个摄像头编号。
    int cameraIndex = 0;
    // 摄像头模式下，连续多少帧识别结果相同才认为识别稳定。
    int stableFrames = 5;
    // 摄像头模式下，两帧之间等待多少毫秒。
    int cameraDelayMs = 30;
    // 摄像头模式下，是否显示预览窗口。
    bool showPreview = true;
};

// 设置当前全局发送配置。
void setSenderOptions(const SenderOptions& options);
// 设置当前全局接收配置。
void setReceiverOptions(const ReceiverOptions& options);

// 读取当前生效的发送配置。
SenderOptions getSenderOptions();
// 读取当前生效的接收配置。
ReceiverOptions getReceiverOptions();

// 生成一张指定尺寸的纯色图像。
// 二进制和八进制两种协议都复用这个公共函数。
cv::Mat renderColorFrame(const cv::Scalar& color, const cv::Size& size = cv::Size(640, 640));
// 取图像中心 50% x 50% 的区域，计算其平均 BGR 颜色。
cv::Scalar sampleCenterColor(const cv::Mat& frame);
// 把一个 BGR 颜色格式化成字符串，方便调试输出。
std::string scalarToText(const cv::Scalar& color);

}  // namespace channel

namespace binary {

// 把二进制符号编码成颜色。
// 题目要求：
// 1 -> 黑色
// 0 -> 白色
cv::Scalar encode(int msg);
// 把采样到的 BGR 颜色解码回二进制符号。
int decode(cv::Scalar color);
// 按当前发送配置发送一个二进制符号。
void send(int msg);
// 按当前接收配置接收一个二进制符号。
int receive();

}  // namespace binary

namespace octal {

// 把一个 [0, 7] 的八进制符号编码成对应颜色。
cv::Scalar encode(int msg);
// 把采样到的 BGR 颜色解码回八进制符号。
// 如果这个颜色离已知调色板都太远，则返回 -1。
int decode(cv::Scalar color);
// 按当前发送配置发送一个八进制符号。
void send(int msg);
// 按当前接收配置接收一个八进制符号。
int receive();

}  // namespace octal

#endif  // COLOR_CHANNEL_HPP
