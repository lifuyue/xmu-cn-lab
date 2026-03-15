# LAB1

屏幕发光 + 摄像头收光的颜色通信实验。

基于 OpenCV 的“屏幕发光 + 摄像头收光”颜色通信作业实现。

## 功能概览

- 二进制编码：
  - 黑色表示比特 `1`
  - 白色表示比特 `0`
- 八进制编码：
  - 黑、白、红、蓝、绿、紫、黄、青分别表示 `0..7`
- 共用接口：
  - `binary::encode / decode / send / receive`
  - `octal::encode / decode / send / receive`
- 两种运行方式：
  - 文件模拟：发送端生成纯色图像，接收端读取图像并解码
  - 实时演示：发送端全屏显示纯色，接收端通过摄像头识别

## 目录结构

```text
include/color_channel.hpp   共享接口
src/color_channel.cpp       公共协议层与发送/接收实现
src/sender.cpp              发送端 CLI
src/receiver.cpp            接收端 CLI
tests/test_roundtrip.cpp    文件模拟测试
Makefile                    构建脚本
```

## 依赖前提

默认 OpenCV 4 和 `pkg-config` 已经安装完成。

可用下面的命令确认环境：

```bash
pkg-config --modversion opencv4
```

## 编译

```bash
make
```

## 接口说明

```cpp
namespace binary {
cv::Scalar encode(int msg);
int decode(cv::Scalar color);
void send(int msg);
int receive();
}

namespace octal {
cv::Scalar encode(int msg);
int decode(cv::Scalar color);
void send(int msg);
int receive();
}
```

- `binary::encode(int msg)` 只接受 `0` 或 `1`
- `octal::encode(int msg)` 只接受 `0..7`
- 超出范围时抛出 `std::invalid_argument`
- `decode` 无法可靠识别时返回 `-1`

## 文件模拟示例

### 题目一：黑白二进制

生成图像：

```bash
./bin/sender --scheme binary --mode file --msg 1 --out output/binary_1.png
```

读取并解码：

```bash
./bin/receiver --scheme binary --mode file --in output/binary_1.png
```

预期输出：

```text
1
```

### 题目二：八进制颜色

生成图像：

```bash
./bin/sender --scheme octal --mode file --msg 6 --out output/octal_6.png
```

读取并解码：

```bash
./bin/receiver --scheme octal --mode file --in output/octal_6.png
```

预期输出：

```text
6
```

## 双机实时演示

发送端电脑：

```bash
./bin/sender --scheme octal --mode screen --msg 3
```

接收端电脑：

```bash
./bin/receiver --scheme octal --mode camera --camera 0 --stable-frames 5
```

说明：

- 发送端会全屏显示纯色，按任意键后退出
- 接收端默认打开摄像头预览，画出中心采样区域
- 接收端连续 `5` 帧得到同样结果后输出最终数字
- 按 `Esc` 可提前退出，返回 `-1`

## 解码策略

- 二进制：
  - 计算亮度 `Y = 0.114B + 0.587G + 0.299R`
  - `Y < 128` 判为黑，即比特 `1`
  - 否则判为白，即比特 `0`
- 八进制：
  - 对中心 ROI 计算平均 BGR
  - 与 8 个标准颜色求欧氏距离
  - 取最近颜色对应的数字
  - 最小距离大于 `120` 时返回 `-1`

## 测试

运行文件模拟测试：

```bash
make test
```

测试覆盖：

- 二进制 `0/1` round-trip
- 八进制 `0..7` round-trip
- 非法输入抛异常
- 颜色轻微扰动下仍可识别
- 明显非调色板颜色返回 `-1`
