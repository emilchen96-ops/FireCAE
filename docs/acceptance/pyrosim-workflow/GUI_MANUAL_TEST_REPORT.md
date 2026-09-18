# FireCAE P27 GUI 人工复核报告

版本：FireCAE 0.3.0  
入口：`D:\FireCAE\build\release\FireCAE.exe`

## 1. 验收口径

现有证据由自动化驱动**正式生产窗口、正式菜单和正式对话框**生成；它不是测试程序直接向目标 `FcDocument` 注入对象。该过程可以验证 GUI 路径和持久化，但不能冒充用户本人用鼠标完成的人工验收。本报告给出可逐项人工复核的操作顺序；最终“人工签字”仍需用户亲自执行。

## 2. First Fire 人工复核

按 [FIRST_FIRE_TUTORIAL.md](FIRST_FIRE_TUTORIAL.md) 从空工程操作。关键检查点：

1. 新建后模型树没有 FDS 业务对象；对应 `screenshots/first-fire/P27-first-fire-01-blank.png`。
2. 项目设置为 `CHID=first_fire`、`T_END=30 s`；对应 `P27-first-fire-02-settings.png`。
3. 用正式对象对话框建立 1 MESH、1 REAC、1 SURF、1 OBST、2 VENT、1 DEVC、1 SLCF、1 DUMP，共 9 个对象；对应 `P27-first-fire-03-modeled.png`。
4. 校验通过，保存到 `projects/first_fire.firecae`，关闭重开后 UUID 不变，导出 `exported-fds/first_fire.fds`。
5. 仿真选择 Serial CPU，任务状态必须为 Completed；对应 `P27-first-fire-05-solved.png`。
6. 打开 `.smv`，选择 `first_fire_hrr.csv`，播放时间轴并导出曲线/截图；对应 `P27-first-fire-06-results.png`。
7. 启动 Smokeview；对应 `P27-first-fire-07-smokeview.png`。

实际结果：FDS 6.11.1 正常完成 30.0 s；HRR 共 31 行，最终 497.63843 kW，峰值 501.31335 kW，平均 473.181403 kW。

## 3. 设备和控制人工复核

打开 `screenshots/seven-tutorials-gui/activate_vents-01-blank-project.png` 到 `-06-reopened-exported.png` 对照操作：

1. 从空工程创建 MESH、7 个 SURF、7 个 PART、12 个 VENT、6 个 DEVC、3 个 CTRL、7 个 RAMP 等共 44 个可编辑对象。
2. 在普通界面通过引用选择器绑定受控对象；保存值应为 UUID。
3. 校验、保存、重开和导出。
4. 运行到 20 s；结果日志中应依次看到 3.00、5.05、6.00、6.05、7.05、8.10、11.00、12.00 s 的设备/控制动作。

## 4. HVAC 人工复核

对照 `HVAC_aircoil-01-blank-project.png` 到 `-06-reopened-exported.png`：

1. 从空工程创建 20 个对象，包含 HVAC 节点、风管和盘管等；
2. 用 UUID 选择器建立 9 个拓扑引用；
3. 打开网络图并确认无断开的必需连接；
4. 校验、保存/重开、导出并运行到 1 s；
5. 最终盘管换热量应为约 45.243122 kW，出口温度约 58.868922 °C。

## 5. 复杂 IFC 人工复核

源文件：`D:\FireCAE\tests\data\Clinic_Architectural_IFC2x3.ifc`。

1. 几何 → Import Geometry，选择 IFC；
2. 类型仅保留 IfcWall/IfcWallStandardCase、IfcSlab、IfcDoor、IfcWindow；
3. 合并策略选 Preserve hierarchy，简化选 Bounding boxes，FDS 路径选 OBST；对照 `screenshots/complex-ifc/P27-ifc-01-filter-options.png`；
4. 预览确认 IFC2X3、4 个楼层、过滤报告和 OBST 路径；对照 `P27-ifc-02-preview-ready.png`；
5. 导入后按楼层/类型过滤模型树，确认 1395 个显示几何；对照 `P27-ifc-03-imported.png`；
6. 选中 IFC 根节点并执行 Geometry → Generate FDS Blocks；导出应有 1083 个 OBST 与 312 个 HOLE；
7. 创建覆盖模型实际包围盒的粗验收网格：`XB=-55,2,-2,15,-58,12`、`IJK=20,20,10`。保存、重开、校验、导出并运行 0.1 s；FDS 必须正常 STOP。该网格只验收几何转换、导出和求解链路，不用于有物理意义的火灾分析；
8. 打开 `p27_complex_ifc.smv`；对应 `P27-ifc-05-solved-results.png`。

## 6. 七教程人工复核

每个教程重复：空工程 → 项目设置 → 逐对象创建 → UUID 引用 → 校验 → 保存/重开 → 导出 → 求解 → Results/Smokeview → 比较报告。具体对象参数在各教程的 `GUI-RECONSTRUCTION.md`，入口汇总见 [SEVEN_TUTORIAL_REPORT.md](SEVEN_TUTORIAL_REPORT.md)。

人工复核时不能点击 “Completed Examples (Reference)” 作为建模步骤；该入口只用于对照最终效果。

## 7. 截图注释

| 截图组 | 工程/对象 | 求解器与结果时间 | 验收项 |
|---|---|---|---|
| `screenshots/first-fire/` | First Fire，9 个 FDS 对象 | FDS 6.11.1，30 s | 验收一 |
| `screenshots/complex-ifc/` | Clinic IFC，1395 几何；1083 OBST + 312 HOLE | FDS 6.11.1，0.1 s | 验收四 |
| `screenshots/seven-tutorials-gui/` | 七工程，共 151 个对象 | 建模/保存/导出阶段 | 验收二、三、五 |
| `screenshots/seven-tutorials-results/` | 七工程真实结果 | FDS 6.11.1，1–600 s | 验收二、三、五 |

当前自动验收结论为 PASS；用户本人鼠标复核和签字状态为“待用户确认”。
