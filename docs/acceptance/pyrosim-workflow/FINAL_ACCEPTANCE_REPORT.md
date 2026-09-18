# FireCAE P17–P32 持续验收总入口

日期：2026-09-03  
版本：FireCAE 0.3.0  
程序入口：`D:\FireCAE\build\release\FireCAE.exe`

## 1. 当前结论

FireCAE 已在当前 Windows 主机形成可运行的 FDS 工作流：生产 GUI 从空工程创建对象、UUID 引用、校验、保存/重开、生成 FDS、Serial/OpenMP/MPI CPU 求解、结果时间轴与 CSV/图片/视频导出，并可启动 Smokeview 查看权威场数据。

本次验收不等于“完整复刻 PyroSim”。P28 已完成当前注册范围的 FDS 6.11.1 Schema 对齐，P29 已完成其余五个新入门教程的独立实算，P30 已完成三方证据入口与防误报校验，P31 已完成 classic/uncompressed SLCF 原生解码及真实输出验证，P32 已完成粒子/喷淋专用向导及真实求解；其余场格式、真实 PyroSim 第三套数值证据和第二台物理机器验证仍有缺口，详见 [REMAINING_GAPS.md](REMAINING_GAPS.md)。

## 2. 核心实测结果

| 验收对象 | 实际证据 | 结论 |
|---|---|---|
| First Fire | 正式 GUI 创建 9 个对象；FDS 6.11.1 至 30 s；最终 HRR 497.63843 kW | PASS |
| 复杂 IFC | 13 MB Clinic IFC2X3；1395 几何；1083 OBST + 312 HOLE；实际边界粗网格；FDS 至 0.1 s | PASS（链路验收，不作物理精度结论） |
| 七个教程 | 空工程 GUI 创建 151 个对象；保存/重开/导出；七例均有真实 FDS 输出 | 5 PASS，2 REVIEW（官方基线不完整） |
| 五个新入门教程 | 空工程 GUI 创建 78 个 FDS 对象；21 张截图；4 个单算例 + 3 个场景批处理 | 7/7 FDS 任务 Completed |
| 求解器 | Serial、2 线程 OpenMP、8 进程 MPI 生命周期专项 | PASS；无 GPU 求解器 |
| 稳定性 | 启停、保存重开、IFC 取消、求解停止、结果播放共 35 轮 | 35/35 PASS |
| 全回归 | P32 Release；P27 Debug/GUI-E2E 基线 | Release 26/26 PASS（277.83 s）；Debug/GUI-E2E 历史基线各 24/24 PASS |
| 三方对比入口 | 独立路径、工程文件、SHA-256、导出时间、三组配对比较 | 基础设施 PASS；真实 PyroSim 来源 PENDING_EVIDENCE |
| 原生 SLCF 底座 | 五个 FDS 6.11.1 实际 `.sf`；大小端/损坏文件单测；原生 PNG | 阶段 PASS；其余场格式未完成，GUI 仍默认 Smokeview |
| 粒子/喷淋向导 | GUI 创建 SPEC/PART/PROP/2×TABL/DEVC/DUMP；保存重开；FDS 2 s 实算 | PASS；25/25 结果文件加载 |

## 3. 人工检查顺序

1. 启动 `D:\FireCAE\build\release\FireCAE.exe`；
2. 按 [FIRST_FIRE_TUTORIAL.md](FIRST_FIRE_TUTORIAL.md) 从空工程完成第一个算例；
3. 按 [GUI_MANUAL_TEST_REPORT.md](GUI_MANUAL_TEST_REPORT.md) 复核设备/控制、HVAC 和复杂 IFC；
4. 按 [TUTORIAL_INDEX.md](TUTORIAL_INDEX.md) 区分“已完整实测”和“仅引导已实现”的教程；
5. 用 [SEVEN_TUTORIAL_REPORT.md](SEVEN_TUTORIAL_REPORT.md) 对照七教程结果；
6. 对照 [SOLVER_REPORT.md](SOLVER_REPORT.md)、[RESULTS_REPORT.md](RESULTS_REPORT.md) 和 [STABILITY_REPORT.md](STABILITY_REPORT.md) 检查求解、后处理和稳定性边界。

## 4. 证据目录

- `projects/`：保存并重开的 FireCAE 工程；
- `exported-fds/`：生产 GUI 导出的 FDS 输入；
- `results/first_fire/`、`results/complex-ifc/`、`results/seven-tutorials/`、`p29-five-tutorials/`、`p32-particle-spray/`：真实求解输出；
- `screenshots/`：建模、求解、Results 和 Smokeview 截图；
- `comparisons/`：七教程 CSV/HTML/PDF 比较；
- `logs/P27-evidence-manifest.csv`：文件大小、时间和 SHA-256 清单。

自动化已证明正式 GUI 路径可执行，但不能替代用户本人鼠标操作的最终签字。人工复核状态仍为“待用户确认”。
