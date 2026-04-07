# LAB6/2

使用 C++ 模拟教学楼 AP 部署、信道分配与信号热力图生成。

## 默认建筑模型

- 建筑尺寸：`100m x 80m`
- 楼层数：`3`
- 层高：`3.5m`
- 中央走廊宽度：`2m`
- 教室尺寸：`15m x 10m`
- 墙体衰减：
  - 承重墙：`12dB`
  - 普通隔断墙：`6dB`
  - 玻璃幕墙：`7dB`

程序采用规则化矩形教学楼模型：

- 中央为横向走廊
- 教室分布在走廊上下两侧
- 候选 AP 优先放置在走廊中心线和教室中心
- 使用确定性的贪心选点策略，保证结果可复现

## 输入格式

标准输入可为空；为空时直接使用默认参数。

若需要覆盖参数，可输入 `key=value` 或 `key value` 形式，例如：

```text
width=120
depth=90
floors=4
grid_step=4
max_ap_count=14
bearing_loss=14
candidate_spacing=10
```

支持的键：

- `width`
- `depth`
- `floors`
- `floor_height`
- `corridor_width`
- `classroom_width`
- `classroom_depth`
- `bearing_loss`
- `partition_loss`
- `glass_loss`
- `grid_step`
- `max_ap_count`
- `candidate_spacing`

## 计算规则

- 覆盖阈值：`-65 dBm`
- 强覆盖阈值：`-50 dBm`
- 2.4GHz 参考无墙覆盖半径：`48m`
- 5GHz 参考无墙覆盖半径：`30m`
- 跨楼层额外衰减：每层 `6dB`
- 2.4GHz 信道池：`1/6/11`
- 5GHz 信道池：`36/44/149/157`
- 默认会保证每层至少部署一个 5GHz 走廊 AP，用于体现双频规划

评分目标综合考虑：

- 未覆盖区域尽量少
- 高强度重叠区域尽量少
- 同层同频近邻 AP 尽量避免同信道
- AP 总数尽量少

## 编译运行

```bash
make
./ap_deployment_demo
```

使用默认参数：

```bash
./ap_deployment_demo
```

使用自定义参数：

```bash
printf "grid_step=4\nmax_ap_count=10\n" | ./ap_deployment_demo
```

## 输出内容

程序会输出：

- AP 数量
- 每个 AP 的楼层、坐标、频段、信道
- 覆盖率、强覆盖率、重叠率

同时在当前目录生成：

- `deployment.txt`
- `heatmap_floor1.ppm`
- `heatmap_floor2.ppm`
- `heatmap_floor3.ppm`

PPM 热力图颜色说明：

- 绿色：强覆盖（`>= -50 dBm`）
- 黄色：满足阈值（`-50` 到 `-65 dBm`）
- 红色：弱覆盖或未覆盖（`< -65 dBm`）
- 蓝色/紫色点：AP 位置标记
