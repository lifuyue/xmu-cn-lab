# 实验 7：利用 Socket API 实现许可认证软件

本目录使用 C++17 和 TCP Socket 实现一个许可证服务器 `license_server` 与模拟远程桌面软件 `software_a`。

## 功能

- 管理员通过 `software_a admin` 提交用户名、口令和许可证类型，服务器返回 10 位数字序列号。
- 客户端通过 `software_a run` 携带序列号和客户端编号向服务器认证。
- 服务器按许可证并发上限决定授权或拒绝。
- 客户端运行期间定期发送心跳，正常退出时发送释放指令。
- 客户端异常退出时不发送释放指令，服务器按超时时间自动剔除过期会话。
- 服务器将许可证和活动会话持久化到数据库文件，重启后可恢复已认证会话。

## 编译

```bash
make
```

## 运行示例

启动服务器，演示时将超时时间设为 5 秒：

```bash
./license_server --port 19097 --db data/licenses.db --timeout 5
```

管理员购买一个 2 人许可证：

```bash
./software_a admin --user xmu --password network2026 --type 2 --port 19097
```

假设返回序列号为 `1234567890`，启动两个客户端并保持运行：

```bash
./software_a run --serial 1234567890 --client-id pc01 --heartbeat 1 --hold 8 --port 19097
./software_a run --serial 1234567890 --client-id pc02 --heartbeat 1 --hold 8 --port 19097
```

第三个客户端会因并发人数达到上限被拒绝：

```bash
./software_a run --serial 1234567890 --client-id pc03 --heartbeat 1 --hold 1 --port 19097
```

模拟异常退出：

```bash
./software_a run --serial 1234567890 --client-id crash01 --heartbeat 1 --hold 2 --no-release --port 19097
```

查询服务器状态：

```bash
./software_a status --port 19097
```
