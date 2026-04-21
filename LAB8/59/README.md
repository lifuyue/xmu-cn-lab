# LAB8/59

用 C 语言实现函数 `int is_in_net(unsigned char *ip, unsigned char *netip, unsigned char *mask);`，
判断给定 IP 地址是否属于指定网络。

## 思路

- 将 `ip` 和 `netip` 分别与 `mask` 做按位与运算
- 逐字节比较网络部分
- 全部相同返回 `1`，否则返回 `0`

## 编译运行

```bash
make
./is_in_net_demo 192.168.1.10 192.168.1.0 255.255.255.0
```
