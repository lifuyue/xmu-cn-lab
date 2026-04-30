# LAB10

使用 C 语言实现遵守 ICMP 协议的时间服务器客户端和服务器端程序。

- `icmp_time_client.c`：发送 ICMP Timestamp Request（Type 13），接收 Timestamp Reply
- `icmp_time_server.c`：监听 ICMP Timestamp Request，返回 Timestamp Reply（Type 14）
- `icmp_time_common.c` / `icmp_time_common.h`：ICMP 时间戳报文封装、解析和校验和逻辑
- `test_icmp_time.c`：无需管理员权限的报文逻辑测试

## 编译运行

```bash
make
sudo ./icmp_time_server 192.168.1.20
sudo ./icmp_time_client 192.168.1.20 4
```

第一个参数是目标地址，客户端第二个参数是发送次数。服务器第一个参数是监听地址过滤条件，可省略；第二个参数是应答次数，`0` 表示一直运行。

## 测试

```bash
make test
```

注意事项：

- macOS/Linux 上创建 ICMP 原始套接字通常需要 `sudo`。
- 部分操作系统或防火墙会丢弃 ICMP Timestamp Request/Reply，实验时需要放行 ICMP Type 13 和 Type 14。
- ICMP 时间戳字段表示 UTC 当日 0 点以来的毫秒数，客户端同时使用本机时间统计往返时延。
