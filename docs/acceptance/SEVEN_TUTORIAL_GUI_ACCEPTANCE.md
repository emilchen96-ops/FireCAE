# FireCAE 七教程 GUI 端到端验收总表

日期：2026-08-29

## 用户可复现的主流程

每个教程均可按以下顺序操作：

1. 文件 → 新建；
2. 模型 → 项目设置；
3. 模型 → 添加 FDS 对象，创建网格、材料、表面、火源、设备、控制、HVAC 和输出对象；
4. 双击对象编辑参数，通过引用选择器绑定 UUID；
5. 点击“校验模型”；
6. 保存 `.firecae`，关闭并重新打开；
7. 导出 `.fds`；
8. 仿真 → 运行 FDS 算例，或按网格条件使用 CPU/MPI；
9. 求解结束后从结果树打开 FireCAE 原生结果工作区，或启动独立 Smokeview；
10. 结果 → 对比 FDS 结果，导出 HTML/PDF/CSV。

详细到每个对象和参数的手工复现说明位于：

`docs/acceptance/tutorials/blank-gui/<case>/GUI-RECONSTRUCTION.md`

## 教程对象重点

| 教程 | 主要建模内容 | 结果重点 |
|---|---|---|
| activate_vents | VENT、PART、DEVC、CTRL、RAMP 动态引用 | 控制器开关、粒子/通风口状态 |
| bucket_test_2 | MATL、SURF、PART、PROP、TABL、DEVC | 喷淋粒子与累计水量 |
| couch | 多层材料、反应、粒子、初始条件、MULT、RAMP | 600 s HRR、边界、切片、烟气与粒子 |
| couch_smoke_12s | couch 的短时烟气输出链 | Smoke3D、温度切片、壁面边界、粒子 |
| HVAC_aircoil | HVAC NODE、DUCT、AIRCOIL、VENT、DEVC | 换热量、节点温度 |
| tunnel_demo | MULT 多网格、火源、通风、DUMP/MISC/PRES | 30 s HRR 和多网格结果 |
| tunnel_smoke_10s | 多网格短时烟气/切片/边界 | Smokeview 动态结果与 10 s HRR |

## 自动化演示证据

`FireCAEA10BlankTutorialUiTests` 对每个教程驱动正式生产对话框，逐个创建对象、补齐 UUID 引用、校验、保存、重开和导出；教程目标工程从空项目开始。共创建 151 个业务对象，七个输入语义比较 7/7 PASS。

每个教程有六张阶段截图，位于：

`docs/acceptance/tutorials/blank-gui/screenshots/`

命名规则为 `<case>-01-blank-project.png` 至 `<case>-06-reopened-exported.png`。最终模型截图为 `<case>-blank-gui-model.png`。

## 求解与后处理证据

- 七个 FireCAE 候选均由 FDS 6.11.1 正常结束；详见 `tests/data/gui-generated/<case>/ACCEPTANCE.md` 和 `.out`。
- Smokeview/原生结果工作区的代表性截图位于 `docs/acceptance/screenshots/A13-*.png`。
- 七教程逐例的真实 Smokeview 结果帧位于 `docs/acceptance/screenshots/Smokeview-seven-tutorials/<case>-smokeview_000000.png`。粒子、累计水量边界、烟气和温度切片均由实际 `.smv` 及关联二进制结果加载，自动渲染 7/7 PASS，并已逐张检查不是空白占位图。
- 每个教程的输入和数值对比位于 `docs/acceptance/comparisons/<case>/`。
- `couch` 与 `tunnel_demo` 的官方长时基线不是正常结束的完整运行，严格结果状态保留为 REVIEW；FireCAE 候选自身正常结束。

## 自动化与人工操作的关系

自动化测试使用与人工操作相同的 `QAction`、模态对话框、校验器、序列化器和导出器，适合稳定回归和截图。它证明 GUI 功能链存在并可重复，不等同于录像式的人工鼠标演示。

用户可按照七份 `GUI-RECONSTRUCTION.md` 手工逐步复现。后台验收没有使用系统鼠标或键盘；也没有把自动化截图伪装成人工点击录像。
