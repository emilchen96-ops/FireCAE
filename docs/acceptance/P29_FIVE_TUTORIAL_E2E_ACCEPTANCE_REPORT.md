# FireCAE P29 — 五个入门教程 GUI→FDS→结果端到端验收报告

验收日期：2026-09-03  
工程：`D:\FireCAE`  
Schema / 求解器：FDS 6.11.1

## 1. 验收规则

五个教程均从 `MainWindow` 新建空工程开始。对象通过生产 `ProjectSettingsAction`、`AddFdsObjectAction`、`Edit Selected Object`、`ImportGeometryAction` 和 `ScenarioManagerAction` 创建；测试配方只提供期望参数，不直接向目标 `FcDocument` 注入对象。

统一流程为：空工程 → 工程设置 → 创建/导入 → UUID 引用绑定 → 3D 检查 → 模型校验 → 保存/重开 → 导出 FDS → 实际求解 → 加载结果。整个过程没有控制用户的物理鼠标或键盘。

## 2. 实际结果

| 教程 | GUI 创建对象 | 额外路径 | 实际任务 | 结果 |
|---|---:|---|---:|---|
| Basic Data Output | 13 | DEVC、标量/矢量 SLCF、BNDF、ISOF、PL3D | 1 | Completed |
| Importing Geometry | 4 | STL 导入向导预检 4 三角形、参考几何 + OBST | 1 | Completed |
| Materials and Layered Surfaces | 8 | 2 MATL、多层 SURF、UUID 材料引用 | 1 | Completed |
| Fire Protection Systems and Controls | 44 | DEVC、CTRL、RAMP、受控 VENT UUID 联动 | 1 | Completed |
| Fire Design Scenarios | 9 | GUI 参数研究生成 Baseline + 2 变体 | 3 | 3/3 Completed |

合计：78 个正式 GUI 创建的 FDS 对象、7 个实际 FDS 任务、7 个 `.out/.smv` 结果集，全部正常结束。每个 `.out` 均包含 `STOP: FDS completed successfully`。

加入 P29 后的 Release 全量回归：25/25 PASS，0 failed，320.46 s。

## 3. 证据

- 工程、FDS 和结果：[p29-five-tutorials](pyrosim-workflow/p29-five-tutorials/)
- 逐步截图：[screenshots/p29-five-tutorials](pyrosim-workflow/screenshots/p29-five-tutorials/)
- 截图共 21 张：每例均有空工程、完成建模、保存导出和结果；导入几何另有导入向导预检图。
- 每个教程目录含独立 `P29-ACCEPTANCE.md`，记录工程路径、对象数和实际任务数。

## 4. 产品补充

- 场景管理器的 New、Duplicate、Rename、Delete、Set Default、Parameter Study 和 Override 控件增加稳定 object name，改善自动验收和辅助功能定位；
- 批处理保留用户目录/并发数对话框，同时提供仅由测试环境变量启用的自动化输入，生产交互不变；
- 新增 `FireCAEP29FiveTutorialE2ETests`，超时上限 900 s。

## 5. 结论

P29 补齐了 P27 后“其余五个教程只有引导、没有独立实算”的缺口。十三个教程现在都有从空工程到结果的正式 GUI 证据。下一项高优先级工作转为独立 PyroSim 三方来源比较；在取得真正独立的 PyroSim 工程与结果之前，仍不得宣称两者数值一致。
