# CISCO IOS 路由器基本配置实验

本目录使用 C++ 程序驱动 Docker + FRRouting 完成实验手册中“实验6 CISCO IOS 路由器基本配置”的核心操作：

- IOS 风格的路由器命名、接口 IP、接口启用和运行配置查看
- 三路由器拓扑的静态路由配置与端到端连通性验证
- RIP v2 动态路由配置、路由学习与端到端连通性验证
- 802.1Q VLAN 端口划分、Trunk 与三层子接口互通验证

FRRouting 的 `vtysh` 命令行风格与 Cisco IOS 接近，适合在本机没有 Packet Tracer/Router eSIM 时复现实验命令与路由行为。程序会真实创建容器、网络和 VLAN，并把命令输出保存到 `artifacts/`。

## 编译运行

```bash
make
./ios_router_lab
```

清理实验容器和网络：

```bash
./ios_router_lab --cleanup
```

## 输出文件

```text
artifacts/
├── configs/              # 各路由器 show running-config 输出
├── screenshot_text/      # 用于截图取证的真实命令输出
├── 00_environment.log
├── 01_static_routes.log
├── 02_rip_dynamic_routes.log
└── 03_vlan_bridge.log
```
