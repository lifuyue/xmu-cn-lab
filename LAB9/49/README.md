# LAB8/49

使用 Python 实现一个遵守 ICMP 协议的 PING 客户端和服务器端程序。

- `ping_client.py`：主动发送 ICMP Echo Request，并统计应答 RTT
- `ping_server.py`：监听 ICMP Echo Request，返回 Echo Reply

这两个程序都基于原始套接字实现，因此需要管理员权限：

```bash
sudo python3 ping_server.py --listen-ip 192.168.1.20
sudo python3 ping_client.py 192.168.1.20 -c 4
```

它们可以与 Windows 或 Linux 自带的 `ping` 程序配合：

- 本目录的 `ping_client.py` 可以直接探测普通 Linux/Windows 主机
- 本目录的 `ping_server.py` 可以回应系统自带 `ping` 发送的 Echo Request

注意事项：

- 如果监听地址本机操作系统本来就会自动回复 Ping，可能出现“双份应答”。做实验时建议使用专用测试地址、额外网卡，或临时关闭系统自带 ICMP 应答。
- macOS/Linux 上创建原始套接字都需要 `sudo`。
