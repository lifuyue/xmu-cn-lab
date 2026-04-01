# LAB5/2

使用 C 语言定义传统以太网帧头部 `struct ethernet_frame_header`。

## 结构说明

- `destination[6]`：目的 MAC 地址
- `source[6]`：源 MAC 地址
- `type_or_length`：类型或长度字段，2 字节

传统以太网帧头部共 14 字节。

## 编译运行

```bash
make
./ethernet_header_demo
```
