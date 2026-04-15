# ex3

实验三代码放在这个目录，按报告中的四个模块拆成一个小型 C++ 项目：

- `packet_parser` 由 `src/pcap_lab.cpp` 中的 `decode_packet()` 负责，统一解析 `DLT_EN10MB`、`DLT_LINUX_SLL`、`DLT_LINUX_SLL2`、`DLT_NULL` 和 `DLT_RAW`。
- `pcap_stats` 输出逐包 IPv4 记录和按分钟聚合的通信字节统计。
- `ftp_login_sniffer` 重组 FTP 控制流并提取 `USER`、`PASS`、`230`、`530`。
- `http_login_sniffer` 重组后端明文 HTTP 报文，解析表单字段并根据响应内容判定登录结果。

构建方式：

```bash
cd /Users/lifuyue/Projects/xmu-cn-lab/ex3
cmake -S . -B build
cmake --build build
```

常用运行示例：

```bash
./build/pcap_stats --read /path/to/lab-all.pcap --packets ipv4_packets.csv --windows minute_stats.csv
./build/ftp_login_sniffer --read /path/to/lab-all.pcap --output ftp_logins.csv
./build/http_login_sniffer --read /path/to/http-backend.pcap --output http_logins.csv
```

仓库目录地址：

`https://github.com/lifuyue/xmu-cn-lab/tree/main/ex3`
