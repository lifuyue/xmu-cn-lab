# LAB7/29

使用 C++ 模拟二端口网桥的自学习功能，并输出每一帧的目的端口与当前 MAC 地址表。

## 题目说明

- 网桥只有两个网段，对应端口 `1` 和 `2`
- 每帧输入格式为 `<源地址> <源端口> <目的地址>`
- 地址长度为 4 bit，支持 `A`、`F`、`0xA`、`0xF` 等写法
- `0xF` 表示广播地址

## 自学习与转发规则

1. 收到帧后，先学习 `源地址 -> 源端口`
2. 若目的地址是广播地址 `0xF`，则转发到另一端口
3. 若目的地址未知，则泛洪到另一端口
4. 若目的地址已知且位于另一端口，则定向转发到该端口
5. 若目的地址已知且与源端口相同，则丢弃，不再转发

## 输入格式

标准输入第一行为帧数量 `n`。

之后输入 `n` 行，每行一个帧：

```text
<src_addr> <src_port> <dst_addr>
```

## 输出格式

对于每一帧，程序输出：

- 帧编号
- 源地址、源端口
- 目的地址
- 目的端口
- 当前 MAC 地址表

其中：

- `port x (broadcast)` 表示广播到另一端口
- `port x (flood)` 表示未知单播泛洪到另一端口
- `discard` 表示目的地址与源端口相同，网桥不过桥

## 编译运行

```bash
make
printf "5\n1 1 2\n2 2 1\n3 1 F\n2 1 3\n2 2 2\n" | ./bridge_learning_demo
```

## 示例输出

```text
Frame 1:
  Source: 0x1 via port 1
  Destination: 0x2
  Output: port 2 (flood)
MAC Table:
  0x1 -> port 1

Frame 2:
  Source: 0x2 via port 2
  Destination: 0x1
  Output: port 1
MAC Table:
  0x1 -> port 1
  0x2 -> port 2
```
