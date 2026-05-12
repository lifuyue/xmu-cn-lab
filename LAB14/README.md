# LAB14

第14课“客户和服务模式”编程题。

- `16/multicast_sender.cpp` / `16/multicast_receiver.cpp`：用 UDP 多播在局域网内分发文件。
- `17/file_transfer.cpp`：同一套程序支持 TCP/UDP 文件传输，并输出传输耗时和有效速率。

## 编译

```bash
make
```

## 题16：多播文件分发

先启动接收端，再启动发送端：

```bash
./16/multicast_receiver 239.10.10.10 5000 recv.bin
./16/multicast_sender 239.10.10.10 5000 big_file.bin 3
```

最后一个参数表示重复发送轮数。UDP 多播不保证可靠送达，局域网内传较大文件时可增大重复轮数，接收端会按分块序号写入文件并统计缺失块。

## 题17：TCP/UDP 文件传输测速

TCP：

```bash
./17/file_transfer server tcp 39017 recv.bin
./17/file_transfer client tcp 127.0.0.1 39017 big_file.bin
```

UDP：

```bash
./17/file_transfer server udp 39018 recv.bin
./17/file_transfer client udp 127.0.0.1 39018 big_file.bin
```

程序会输出字节数、耗时和 MiB/s。实际对比时可分别在本机、本地局域网和远程网络中运行同样命令，记录输出速率。
