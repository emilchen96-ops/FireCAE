# FireCAE P31 — 原生 SLCF 解码与二维色彩图阶段验收

日期：2026-09-03

## 本阶段完成范围

- 读取 FDS classic/uncompressed `.sf` Fortran 顺序记录；
- 自动识别 little-endian / big-endian；
- 读取 long label、short label、unit、六个索引边界；
- 按 `((i * ny) + j) * nz + k` 读取逐帧 float 场值；
- 读取帧时间、逐帧最小值和最大值；
- 支持 X/Y/Z 三种二维切片方向并输出轴信息；
- 使用全时间范围色标生成原生二维 PNG；
- 对记录长度、首尾标记、维度、内存上限、非有限数值和截断文件做防护；
- 提供 `FireCAESliceInspector.exe` 作为不启动 GUI 的只读检查/导出入口。
- 原生结果页内部已接入真实 SLCF 帧时间轴、帧切换、滚轮缩放、拖动平移和双击复位；该入口继续保持隐藏，不改变当前默认 Smokeview 产品路径。

实现依据与交叉核对：

- [firemodels/smv — IOSlice.c](https://github.com/firemodels/smv/blob/master/Source/smokeview/IOslice.c)
- [firemodels/smv — readslice.c](https://github.com/firemodels/smv/blob/master/Source/shared/readslice.c)
- P29 中 FDS 6.11.1 实际生成的五个 `.sf` 文件。

## 真实结果验证

`basic_data_output` 的五个切片全部解析成功：

| 文件 | 量 | 网格点 | 帧 | 时间 | 数值范围 |
|---|---|---:|---:|---:|---:|
| `_1_1.sf` | TEMPERATURE | 31×21×1 | 3 | 0–2 s | 20–228.6218 °C |
| `_1_2.sf` | VELOCITY | 1×21×16 | 3 | 0–2 s | 0–5.3374 m/s |
| `_1_3.sf` | U-VELOCITY | 1×21×16 | 3 | 0–2 s | -0.5133–0.4008 m/s |
| `_1_4.sf` | V-VELOCITY | 1×21×16 | 3 | 0–2 s | -0.9426–0.8159 m/s |
| `_1_5.sf` | W-VELOCITY | 1×21×16 | 3 | 0–2 s | -0.3018–5.4661 m/s |

原生 PNG：`pyrosim-workflow/screenshots/p31-native-slice-temperature.png`。

## 自动化

- 小端格式：标签、单位、维度、两帧时间和指定 `(i,j,k)` 数值精确验证；
- 大端格式：相同数据精确验证；
- 二维图：尺寸、X/Y 轴、全局色标范围和颜色差异验证；
- 低内存上限：安全拒绝；
- 截断/未知格式：安全拒绝且不暴露部分帧；
- Core Release：PASS。

## 明确未完成

本阶段不代表 FireCAE 已拥有完整 Smokeview 替代品。尚未实现 compressed slice、Smoke3D、BNDF、PART、ISOF、Plot3D 的完整解码，也没有把原生查看器重新设为默认。根据当前产品决定，GUI 仍默认启动 Smokeview；已完成的 SLCF 动画与缩放仅作为内部可回归的数据底座。
