# LAB3 / 25

两路二值消息序列的多路复用与解复用实验。

本题把输入数组视为离散比特流：

- 元素 `0` 表示比特 `0`
- 元素非 `0` 表示比特 `1`

实验分别用四种规则完成复用与解复用：

- 异步时分复用 `tdm_async`
- 同步时分复用 `tdm_sync`
- 频分复用 `fdm`
- 码分复用 `cdm`

## 目录结构

```text
include/multiplex.hpp     公共接口定义
src/multiplex.cpp         四种复用方式实现
src/multiplex_cli.cpp     命令行演示程序
tests/test_multiplex.cpp  自动化测试
Makefile                  构建脚本
```

## 编译

```bash
make
```

## 测试

```bash
make test
```

## 接口说明

```cpp
namespace tdm_async {
int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len);
int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len);
}

namespace tdm_sync { ...同签名... }
namespace fdm { ...同签名... }
namespace cdm { ...同签名... }
```

返回值约定：

- 成功时返回实际写入元素数
- 失败时返回 `-1`
- 零长度输入合法，返回 `0`

## 四种规则

### `tdm_async`

- 输出长度：`a_len + b_len`
- A 路符号：
  - `0 = A:0`
  - `1 = A:1`
- B 路符号：
  - `2 = B:0`
  - `3 = B:1`
- 调度方式：每轮先发 A，再发 B；某一路结束后另一条继续发送

示例：

```text
a = 1 0 1
b = 0 1
c = 1 2 0 3 1
```

### `tdm_sync`

- 输出长度：`2 * max(a_len, b_len)`
- 偶数位固定为 A 槽，奇数位固定为 B 槽
- 槽值：
  - `0 = bit 0`
  - `1 = bit 1`
  - `2 = idle/pad`

示例：

```text
a = 1 0 1
b = 0 1
c = 1 0 0 1 1 2
```

### `fdm`

- 输出长度：`max(a_len, b_len)`
- 复合符号定义：`symbol = a_bit + 2 * b_bit`
- 合法值只有 `0..3`

示例：

```text
a = 1 0 1
b = 0 1
c = 1 2 1
```

### `cdm`

- 输出长度：`2 * max(a_len, b_len)`
- A 码：`[+1, +1]`
- B 码：`[+1, -1]`
- 比特映射：
  - `1 -> +1`
  - `0 -> -1`
  - 缺失路 `-> 0`
- 每轮两片叠加后整体平移 `+2` 存储，合法值为 `0..4`

示例：

```text
a = 1 0 1
b = 0 1
c = 2 4 2 0 3 3
```

## 命令行演示

编码：

```bash
./bin/multiplex_demo encode fdm 101 01
```

预期输出：

```text
1 2 1
```

解码：

```bash
./bin/multiplex_demo decode fdm 3 2 1 2 1
```

预期输出：

```text
A=101
B=01
```

## 测试覆盖

- 四种方式的等长 round-trip
- 四种方式的不等长 round-trip
- 非零输入归一化
- 缓冲区不足与长度不匹配
- 非法符号检测
- `tdm_sync` 补空槽校验
- `cdm` 芯片对长度校验
- CLI 编解码烟雾测试
