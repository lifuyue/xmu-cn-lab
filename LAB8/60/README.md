# LAB8/60

用 C 语言实现函数 `int classwise(unsigned char *ip);`，
对 IPv4 地址做传统 A-E 类分类，并按题意返回 `0-4`。

## 映射规则

- `0` 对应 A 类
- `1` 对应 B 类
- `2` 对应 C 类
- `3` 对应 D 类
- `4` 对应 E 类

## 编译运行

```bash
make
./classwise_demo 224.0.0.2
```
