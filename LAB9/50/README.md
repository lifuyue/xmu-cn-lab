# LAB8/50

使用 Python 实现一个简化 DHCP 服务器，支持向同一网段内的客户端分配固定 IPv4 地址。

默认行为：

- 服务器监听 UDP `67`
- 收到 `DHCPDISCOVER` 时回复 `DHCPOFFER`
- 收到 `DHCPREQUEST` 时回复 `DHCPACK`
- 默认提供固定地址 `192.168.1.2`

运行示例：

```bash
sudo python3 dhcp_server.py \
  --server-ip 192.168.1.1 \
  --subnet-mask 255.255.255.0 \
  --router 192.168.1.1 \
  --dns 192.168.1.1 \
  --default-ip 192.168.1.2
```

也可以为多个 MAC 地址预置静态租约：

```bash
sudo python3 dhcp_server.py \
  --server-ip 192.168.1.1 \
  --static-lease 00:11:22:33:44:55=192.168.1.20 \
  --static-lease aa:bb:cc:dd:ee:ff=192.168.1.21
```

说明：

- 本程序为了配合课程实验，重点实现 DHCP 的发现、请求、应答主流程，以及最常用的地址、网关、掩码、DNS、租约时间等选项。
- 需要管理员权限，因为要绑定 DHCP 服务端口 `67` 并发送广播报文。
- 若实验网络中已存在真实 DHCP 服务器，请在隔离网段中测试，避免地址冲突。
