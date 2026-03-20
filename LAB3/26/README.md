# LAB3 / 26

调制实验：生成正弦载波、模拟/数字调制信号，并输出调幅、调频、调相三类频带信号。

## 实现内容

- 载波 `generate_cover_signal`
  - 生成离散正弦波：`sin(2πfc n / fs)`
- 数字调制信号 `simulate_digital_modulation_signal`
  - 固定重复模式：`1 1 0 0 1 0 1 0`
- 模拟调制信号 `simulate_analog_modulation_signal`
  - 生成低频正弦：`sin(2πfm n / fs)`
- 六种调制输出
  - 数字调频 `modulate_digital_frequency`：`BFSK`
  - 模拟调频 `modulate_analog_frequency`：`FM`
  - 数字调幅 `modulate_digital_amplitude`：`ASK`
  - 模拟调幅 `modulate_analog_amplitude`：`AM`
  - 数字调相 `modulate_digital_phase`：`BPSK`
  - 模拟调相 `modulate_analog_phase`：`PM`

## 目录结构

```text
include/modulation.hpp     公共接口定义
src/modulation.cpp         载波、调制信号与六种调制实现
src/modulation_cli.cpp     命令行演示程序
tests/test_modulation.cpp  自动化测试
Makefile                   构建脚本
```

## 编译

```bash
make
```

## 测试

```bash
make test
```

## 接口

题面中的 `unsigned double` 不是合法的 C/C++ 类型，这里按 `double` 修正：

```cpp
int generate_cover_signal(double* cover, int size);
int simulate_digital_modulation_signal(unsigned char* message, int size);
int simulate_analog_modulation_signal(double* message, int size);

int modulate_digital_frequency(double* cover, int cover_len,
                               const unsigned char* message, int msg_len);
int modulate_analog_frequency(double* cover, int cover_len,
                              const double* message, int msg_len);

int modulate_digital_amplitude(double* cover, int cover_len,
                               const unsigned char* message, int msg_len);
int modulate_analog_amplitude(double* cover, int cover_len,
                              const double* message, int msg_len);

int modulate_digital_phase(double* cover, int cover_len,
                           const unsigned char* message, int msg_len);
int modulate_analog_phase(double* cover, int cover_len,
                          const double* message, int msg_len);
```

返回值约定：

- 成功时返回实际写入的样本数
- 失败时返回 `-1`
- 零长度输入返回 `0`

## 固定参数

- 采样率：`fs = 1024.0`
- 载波频率：`fc = 64.0`
- 模拟消息频率：`fm = 4.0`
- 数字 ASK 幅度：
  - `0 -> 0.35`
  - `1 -> 1.0`
- 数字 BFSK 频率：
  - `0 -> fc - 16`
  - `1 -> fc + 16`
- 数字 BPSK 相位：
  - `0 -> 0`
  - `1 -> π`
- 模拟 AM：
  - `(1 + 0.5 * m[n]) * sin(...)`
- 模拟 FM：
  - 初始 `phase = 0`
  - 每个采样点先输出 `sin(phase)`，再按 `phase += 2π(fc + 24 * m[n]) / fs` 递推
- 模拟 PM：
  - `sin(2πfc n / fs + (π/2) * m[n])`

消息到样本的映射使用：

```text
msg_index = n * msg_len / cover_len
```

因此消息长度与载波长度不必相等。

## 命令行演示

生成 8 个载波样本：

```bash
./bin/modulation_demo cover 8
```

生成 8 个数字调制消息样本：

```bash
./bin/modulation_demo digital_msg 8
```

生成 8 个模拟调制消息样本：

```bash
./bin/modulation_demo analog_msg 8
```

生成调制后的频带信号：

```bash
./bin/modulation_demo modulate df 16 4
./bin/modulation_demo modulate da 16 4
./bin/modulation_demo modulate dp 16 4
./bin/modulation_demo modulate af 16 4
./bin/modulation_demo modulate aa 16 4
./bin/modulation_demo modulate ap 16 4
```

其中：

- `df`：数字调频
- `da`：数字调幅
- `dp`：数字调相
- `af`：模拟调频
- `aa`：模拟调幅
- `ap`：模拟调相

输出均为空格分隔的离散样本序列，便于复制到绘图工具观察波形。

## 测试覆盖

- 载波关键样本检查
- 数字消息固定模式检查
- 模拟消息幅度范围检查
- 六种调制的公式与关键样本检查
- 调制结果有限值与非平凡变化检查
- 参数错误与零长度边界
- CLI 烟雾测试
