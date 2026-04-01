# LAB5/1

使用 C++ 模拟以太网网卡根据目的 MAC 地址判断帧是否应该被接收。

## 要点

- 定义宏 `MAC_ADDRESS_LENGTH`
- 使用 `typedef` 定义 `MAC_address`
- 定义结构体 `EthernetFrame`
- 定义本机地址 `this_mac_address`
- 实现函数 `int mac_address_match(const struct EthernetFrame *frame);`

函数返回值约定：

- `1`：匹配，应接收
- `0`：不匹配，不接收

匹配条件：

- 目的地址等于本机 MAC 地址
- 目的地址为多播地址（首字节最低位为 `1`）
- 目的地址为广播地址 `FF:FF:FF:FF:FF:FF`

## 编译运行

```bash
make
./mac_address_demo
```
