# LAB8/61

用 C++ 实现函数
`vector<Fragment> fragmentPacket(int packetLength, const vector<int>& pathMTUs);`，
模拟 IPv4 分片过程。题设固定 IP 首部长度为 `20B`。

## 实现要点

- 初始只有一个分片，偏移量为 `0`
- 遍历路径上的每个 MTU
- 若当前分片总长度不超过 MTU，则保持不变
- 若超过 MTU，则按 `8B` 对齐切分数据部分
- 偏移量按 IPv4 的 `8B` 单位累加

## 编译运行

```bash
make
printf "24576 5 4325 2346 1500 4464 2346\n" | ./fragment_packet_demo
```
