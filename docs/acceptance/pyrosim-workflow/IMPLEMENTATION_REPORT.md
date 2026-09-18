# FireCAE P17–P27 实施报告

完成日期：2026-08-30  
版本：FireCAE 0.3.0  
生产程序：`D:\FireCAE\build\release\FireCAE.exe`

## 1. 结论

P17–P27 已形成一个可运行的 FireCAE 主链路：空工程建模、专业参数编辑、UUID 引用、校验、工程保存/重开、FDS 导入导出、Serial/OpenMP/MPI CPU 求解、结果加载与播放、Smokeview 启动、曲线/图片/视频/比较报告导出。当前不宣称完全实现 PyroSim，也不宣称二者数值已经三方验证。

最终专项验收包括：

- First Fire：正式 GUI 创建 9 个 FDS 业务对象，真实运行 FDS 6.11.1 到 30 s；
- 设备与控制：`activate_vents` 从空工程创建，验证 UUID 引用及 3.00–12.00 s 的实际动作序列；
- HVAC：`HVAC_aircoil` 从空工程创建并正常求解，最终换热量 45.243122 kW、出口温度 58.868922 °C；
- 复杂 IFC：13 MB Clinic IFC2X3，筛选墙/楼板/门/窗，导入 1395 个几何对象，转换 1083 个 OBST 和 312 个 HOLE，保存/重开并正常求解；
- 七教程：共 151 个对象经正式对象对话框从空工程创建，全部保存/重开/校验/导出并有真实 FDS 结果；
- 稳定性：35/35 轮循环通过；三套构建各 24/24 CTest 通过。

## 2. 分阶段交付

| 阶段 | 主要交付 | 状态 |
|---|---|---|
| P17 | PyroSim 2023.3 工作流差距矩阵与事实口径 | 完成并在 P27 更新 |
| P18 | 3D/2D/Record/Results 四工作区、Dock、延迟模型树、高 DPI 布局 | 已验收 |
| P19 | 持久化楼层、墙绘制、捕捉、精确输入、背景和裁剪 | 已验收 |
| P20 | 网格、材料、表面、火源、设备/控制、HVAC、输出专业编辑器 | 已验收 |
| P21 | 实时 FDS Record、Source Map、UUID/字段定位、未知记录往返 | 已验收 |
| P22 | FDS/CAD/IFC 导入、后台进度/取消、IFC 过滤/策略 | 已验收 |
| P23 | 仿真参数、Serial/OpenMP/MPI、任务中心、进程树回收 | 已验收 |
| P24 | Results 时间轴、结果树、CSV、截图/AVI、Smokeview 分层入口 | 已验收；原生场数据仍有限 |
| P25 | 场景、八类资源库、首选项 v2、备份/恢复/诊断 | 已验收 |
| P26 | 13 个从空工程启动、可检查和可恢复的分步教程 | 引导系统已验收 |
| P27 | First Fire、设备控制、HVAC、复杂 IFC、七教程和稳定性 | 当前主机验收完成 |

## 3. 主要业务对象和编辑器

新增或成熟化的业务对象包括 `FcFloorObject`、`FcSimulationParameters`、`FcScenario`、IFC 构件对象、FDS namelist 业务对象、结果算例/结果文件对象。它们都挂在 `FcDocument` 对象树中；模型树条目保存 UUID，引用保存目标 UUID，导出时才解析为 FDS ID。

主要专业界面包括：

- `FdsSchemaEditorWidget` 的网格、材料、表面、反应、设备、控制、HVAC 和输出专业页；
- `BuildingElementDialog`、`FloorEditorDialog`、`SimulationParametersDialog`；
- 火源、设备控制、输出、网格助手和场景管理对话框；
- `GeometryImportWizard` 与 IFC 类型/合并/简化/FDS 路径设置；
- `FdsRecordEditor`、`NativeResultViewerWidget`、`SmokeviewHostWidget`；
- `TutorialGuideWidget`。

## 4. 本轮主要文件

`D:\FireCAE` 没有 Git 元数据，无法提供可靠的提交级差分。主要新增或修改文件为：

- `CMakeLists.txt`；
- `src/app/MainWindow.cpp/.h`；
- `src/core/FcFloorObject.*`、`FcProject.*`、`FcScenario.*`；
- `src/ui/ModelTreeWidget.*`、`FdsSchemaEditorWidget.*`、`FdsRecordEditor.*`、`FdsWorkflowDialogs.*`、`GeometryImportWizard.*`、`SimulationParametersDialog.*`、`SimulationRunDialog.*`、`NativeResultViewerWidget.*`、`SmokeviewHostWidget.*`、`TutorialGuideWidget.*`；
- `src/import/IfcImportService.*`、FDS/CAD 导入实现；
- `src/fds/FdsImporter.*`、`FdsWriter.*`、`FdsBlockConversionService.*`、`FcProjectSerializer.*`；
- `src/simulation/*`、`src/results/*`、`src/reliability/*`、`src/settings/*`；
- `tests/ui_smoke_tests.cpp`、FDS/core/scene/runner 测试；
- `tools/run_p27_stability.ps1`、`tools/collect_p27_evidence.ps1`；
- `docs/PYROSIM_2023_WORKFLOW_MATRIX.md` 和 P18–P27 验收文档。

## 5. 由“部分完成”提升为当前主机已验收

- 主工作区、楼层与 2D/3D 基础绘图；
- 网格、材料、表面、反应、设备/控制、HVAC、输出专业主流程；
- FDS Record 和双向定位；
- FDS 导入导出、复杂 IFC 主链路；
- CPU Serial/OpenMP/MPI 求解和任务中心；
- 场景、资源库、首选项与恢复；
- 教程引导系统；
- 当前主机稳定性和复杂 IFC 端到端运行。

仍为部分实现的项目见 [REMAINING_GAPS.md](REMAINING_GAPS.md)。

## 6. 测试总览

| 构建 | 结果 | 总耗时 |
|---|---:|---:|
| Release | 24/24 PASS | 185.51 s |
| Debug | 24/24 PASS | 324.40 s |
| GUI-E2E | 24/24 PASS | 320.20 s |

循环稳定性为 35/35 PASS。完整日志和清单位于 `logs/`。P27 First Fire 与复杂 IFC 均实际调用 `RunFdsAction` 和生产运行对话框，不以测试夹具直接写入目标工程对象树。

## 7. 证据入口

- 手动复核步骤：[GUI_MANUAL_TEST_REPORT.md](GUI_MANUAL_TEST_REPORT.md)
- First Fire：[FIRST_FIRE_TUTORIAL.md](FIRST_FIRE_TUTORIAL.md)
- 七教程：[SEVEN_TUTORIAL_REPORT.md](SEVEN_TUTORIAL_REPORT.md)
- 求解器：[SOLVER_REPORT.md](SOLVER_REPORT.md)
- 后处理：[RESULTS_REPORT.md](RESULTS_REPORT.md)
- 稳定性：[STABILITY_REPORT.md](STABILITY_REPORT.md)
- SHA-256 清单：`logs/P27-evidence-manifest.csv`

