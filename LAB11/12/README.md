# LAB11/12

使用 C++ 实现一个简化 NTP 服务器，从命令行读取指定时间，并按 NTP 报文格式回复客户端。

## 功能

- 监听 UDP NTP 请求
- 将命令行时间转换为 NTP 时间戳
- 回复 NTP 48 字节报文
- 支持 `--once` 模式，便于本机测试

## 编译运行

```bash
make
sudo ./ntp_server "2019-05-01 10:41:00"
```

默认监听 UDP `123` 端口，通常需要管理员权限。测试时可以改用普通端口：

```bash
./ntp_server "2019-05-01 10:41:00" --port 9123 --once
```

Windows “Internet 时间”客户端测试时，将服务器地址设为 `localhost`。
