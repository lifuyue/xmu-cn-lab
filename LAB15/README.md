# 第15课 域名系统

本目录用于第15课程序设计题。题目要求阅读 ReactOS 仓库中 `base/applications/network/nslookup` 目录下的文件，理解 Windows 下 `nslookup` 的 DNS 解析流程。

参考源码：

- ReactOS GitHub 目录：<https://github.com/reactos/reactos/tree/master/base/applications/network/nslookup>
- ReactOS Doxygen 源码视图：<https://doxygen.reactos.org/d8/d89/nslookup_8c_source.html>

## ReactOS nslookup 处理流程

1. `main` 初始化全局状态：递归查询、搜索域、默认端口 53、查询类型、超时和重试次数等。
2. 调用系统网络参数接口读取默认 DNS 服务器地址，并初始化 Winsock。
3. `ParseCommandLine` 解析命令行参数；若给出目标主机名则进入非交互查询流程，若仅进入交互模式则当前 ReactOS 源码中仍提示该功能未实现。
4. `PerformLookup` 根据查询类型构造 DNS 查询。普通域名默认按 A/AAAA 查询；若输入为 IP 地址并执行 PTR 查询，则先转换为 `in-addr.arpa` 或 `ip6.arpa` 反向域名。
5. 查询报文按 DNS 协议组织：事务 ID、标志位、问题数、QNAME、QTYPE、QCLASS，其中递归查询通过 RD 位表示。
6. `SendRequest` 将报文发送到默认 DNS 服务器的 53 端口并接收响应。
7. 收到响应后跳过问题区，解析回答区、授权区和附加区，按资源记录类型输出 A、PTR、CNAME、MX 等结果。

## C++ 复现实验

`25/main.cpp` 使用 C++17 和 UDP socket 手工构造 DNS 报文，复现 `nslookup` 的核心流程：构造查询报文、发送到 DNS 服务器、解析响应头、跳过问题区、解析回答区并输出资源记录。

```bash
make -C LAB15
LAB15/25/nslookup_flow www.xmu.edu.cn A
LAB15/25/nslookup_flow www.xmu.edu.cn CNAME 8.8.8.8
LAB15/25/nslookup_flow 8.8.8.8 PTR
```
