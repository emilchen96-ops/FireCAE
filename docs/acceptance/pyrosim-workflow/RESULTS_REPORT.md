# FireCAE P27 结果与 Smokeview 报告

## 1. FireCAE Results 已验收能力

- 播放、暂停、停止、前后帧、时间滑块、跳转、速度、循环；
- 手动刷新与自动刷新；
- Geometry、3D Smoke/Fire、Slice、Vector Slice、Boundary、Isosurface、Particles、Plot3D、Devices、CSV、HVAC 结果树；
- CSV 多曲线、单位/图例、范围、最小/最大/平均值、合并 CSV、PNG；
- 截图、PNG 帧和真实 RIFF/MJPEG AVI；
- 多算例曲线叠加和来源标签；
- FireCAE 原始几何、FDS Requested、FDS Actual、CAD/IFC Reference 来源区分。

## 2. First Fire

实际加载 `first_fire.smv`，选择 `first_fire_hrr.csv`，时间轴能够前进，并成功导出：

- `results/first_fire/first_fire-visible.csv`
- `results/first_fire/first_fire-results.png`

HRR 共 31 行，最终 497.63843 kW，峰值 501.31335 kW，平均 473.181403 kW。对应界面截图为 `screenshots/first-fire/P27-first-fire-06-results.png`。

## 3. Smokeview

First Fire 已启动实际 Smokeview，并保存 `P27-first-fire-07-smokeview.png`。七教程分别保存 Smokeview 帧截图。`SmokeviewHostWidget` 关闭时先请求窗口关闭，进行有界等待，再在必要时终止本次启动的进程；验收前后没有新增残留 Smokeview PID。

## 4. 原生与外部能力边界

FireCAE 原生结果页对 CSV、元数据、几何来源、时间轴和导出已成熟。Smoke3D/SLCF/BNDF/PART 的完整二进制/GPU 渲染，以及 ISOF/Plot3D 原生解码尚未完成；当前权威场数据显示路径仍是 Smokeview。报告和界面必须保留“FireCAE 原生”“Smokeview 帧缓存”“外部 Smokeview”三种来源标识。

## 5. 比较报告

七教程已生成 CSV/HTML/PDF 报告。五例 PASS；`couch`、`tunnel_demo` 因官方参考运行不完整而 REVIEW，FireCAE 自身正常完成。当前没有真正的独立 PyroSim 第三套结果，不能宣称三方数值一致。

