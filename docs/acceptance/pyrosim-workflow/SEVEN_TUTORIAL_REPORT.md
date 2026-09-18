# FireCAE 七教程端到端报告

版本：FireCAE 0.3.0  
求解器：FDS-6.11.1-0-gff928db-release

## 1. GUI 建模口径

七个目标工程都从空的生产 `MainWindow` 开始。自动驱动逐次打开正式项目设置和 FDS 对象编辑对话框，创建 151 个对象，再用目标工程中新生成的 UUID 绑定引用。完成后执行校验、保存、关闭重开、UUID 稳定检查和 FDS 导出。参考配方只提供应输入的参数，没有直接写入目标 `FcDocument`。

## 2. 求解与对比结果

| 教程 | GUI 对象 | FireCAE 结束时间 | 关键结果 | 对比结论 |
|---|---:|---:|---|---|
| activate_vents | 44 | 20 s | 控制/设备动作：3.00、5.05、6.00、6.05、7.05、8.10、11.00、12.00 s | PASS |
| bucket_test_2 | 13 | 15 s | 最终累计水质量 4.958014 kg | PASS |
| couch | 26 | 600 s | 峰值 HRR 1955.7398 kW @ 255.01009 s；最终 0.26043643 kW；FDS 4533.215 s | REVIEW：官方基线只到 10.007602 s；FireCAE 正常完成 |
| couch_smoke_12s | 26 | 12 s | 最终 HRR 538.27895 kW；225 个物理文件哈希一致 | PASS |
| HVAC_aircoil | 20 | 1 s | 盘管换热 45.243122 kW；出口 58.868922 °C | PASS |
| tunnel_demo | 11 | 30 s | FireCAE 最终 HRR 9832.9590 kW；共同时间 28.621181 s 时 8232.2610 kW | REVIEW：官方基线未正常完成；共同区间一致 |
| tunnel_smoke_10s | 11 | 10 s | 最终 HRR 8313.3098 kW；130 个物理文件哈希一致 | PASS |

七个 FireCAE 求解均正常产生 `.out`、`.smv` 和相关结果；`couch` 与 `tunnel_demo` 的 REVIEW 来源是官方参考计算不完整，不是 FireCAE 计算失败。

## 3. 证据入口

- 工程：`projects/<case>.firecae`
- 导出输入：`exported-fds/<case>.fds`
- 完整结果：`results/seven-tutorials/<case>/`
- GUI 六阶段截图：`screenshots/seven-tutorials-gui/`
- Smokeview 结果截图：`screenshots/seven-tutorials-results/`
- CSV/HTML/PDF 对比：`comparisons/<case>/`
- 参数级操作说明：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\<case>\GUI-RECONSTRUCTION.md`

## 4. 结果播放

每个 `.smv` 已通过结果扫描器加载，Smokeview 帧截图存档。FireCAE Results 支持可用 CSV 的曲线、统计、时间轴和导出；Smoke3D、切片和粒子场的权威显示仍由 Smokeview 提供，不将外部帧冒充 FireCAE 原生二进制解码。

## 5. PyroSim 对比边界

当前对比来源是官方/原生 FDS 参考与 FireCAE GUI 导出后求解结果。目录中没有一套由 PyroSim 2023.3 独立建模、独立导出、独立求解并记录来源哈希的第三方结果，因此本报告不宣称已经完成 Native FDS / FireCAE / PyroSim 三方数值一致性验收。

