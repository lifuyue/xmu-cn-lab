# LAB4

奇偶校验码检验实验。

## 实验目标

实现题目要求的接口：

```cpp
int parity_check(const unsigned char *msg, const int msg_length);
```

其中 `msg` 的每个元素只区分两种取值：

- `0` 表示二进制 `0`
- 非 `0` 表示二进制 `1`

本实验采用偶校验约定：整个消息（包含校验位）中，`1` 的总个数为偶数时返回 `1`，否则返回 `0`。

## 目录结构

```text
include/parity_check.hpp      公共接口定义
src/parity_check.cpp          核心实现
src/parity_check_cli.cpp      命令行演示程序
tests/test_parity_check.cpp   自动化测试
Makefile                      构建脚本
```

## 编译

```bash
make
```

## 运行测试

```bash
make test
```

## 使用示例

```bash
./bin/parity_check_demo 1 0 1 0 0
```

输出：

```text
1
```

说明：该序列中共有 2 个 `1`，满足偶校验。

## 返回值约定

- `1`：校验通过
- `0`：校验失败

## 测试覆盖

- 偶数个 `1` 的通过场景
- 奇数个 `1` 的失败场景
- 非 `0` 输入按 `1` 处理
- 包含校验位的消息检验
- 空消息、空指针、非法长度等边界条件
