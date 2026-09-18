# FireCAE 0.3.0（P29）分步教程与验收状态索引

所有教程从空工程启动。“检查当前步骤”读取真实 `FcDocument`，按 FDS 记录数量、场景数量、校验结果、正式保存路径、FDS 导出文件和已加载结果判定。已完成七算例仅位于 `Completed Examples (Reference)` 子菜单，不能替代教学操作。

| 教程 | 目的/最终效果 | 完整参数和操作 | 当前验收状态 |
|---|---|---|---|
| Your First Fire | 房间、网格、500 kW 火源、开放边界、温度/HRR、求解与结果 | [FIRST_FIRE_TUTORIAL.md](FIRST_FIRE_TUTORIAL.md) | **完整实测**：正式 GUI、工程/FDS、FDS 6.11.1 至 30 s、Results 与 Smokeview 截图 |
| Basic Data Output | DEVC、Slice、Vector Slice、BNDF、ISOF、Plot3D 和 Results | [P29 证据](p29-five-tutorials/basic_data_output/P29-ACCEPTANCE.md) | **完整实测**：空工程 GUI 创建 13 对象、实际求解及结果加载 |
| Importing Geometry | 单位/坐标/过滤/简化，CAD/IFC 到 OBST/HOLE/GEOM | [P29 证据](p29-five-tutorials/importing_geometry/P29-ACCEPTANCE.md) | **完整实测**：生产导入向导、STL 预检、参考几何与 FDS 转换对象、实际求解 |
| Materials and Layered Surfaces | MATL 物性、多层 SURF、UUID 材料层引用 | [P29 证据](p29-five-tutorials/materials_layered_surfaces/P29-ACCEPTANCE.md) | **完整实测**：两个 MATL、多层 SURF、UUID 引用、实际求解 |
| Fire Protection Systems and Controls | 探测器、阈值、CTRL、激活/停用对象 | [P29 证据](p29-five-tutorials/fire_protection_controls/P29-ACCEPTANCE.md) | **完整实测**：GUI 创建 44 对象、DEVC/CTRL 引用与实际求解 |
| Fire Design Scenarios | 基准/复制/UUID 覆盖/批量运行/结果比较 | [P29 证据](p29-five-tutorials/fire_design_scenarios/P29-ACCEPTANCE.md) | **完整实测**：GUI 参数研究生成 3 场景，3/3 批处理求解及结果加载 |
| activate_vents | VENT 激活/停用和 DEVC/CTRL | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/activate_vents/GUI-RECONSTRUCTION.md) | **完整实测**：空工程 GUI、保存重开、实算和结果比较 |
| bucket_test_2 | 喷淋、PROP、粒子和设备 | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/bucket_test_2/GUI-RECONSTRUCTION.md) | **完整实测**：空工程 GUI、保存重开、实算和结果比较 |
| couch | 材料热解、分层表面和完整 600 s 火灾 | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/couch/GUI-RECONSTRUCTION.md) | **完整实测**；官方基线不完整，因此数值结论为 REVIEW |
| couch_smoke_12s | 短时 Smoke3D/切片 | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/couch_smoke_12s/GUI-RECONSTRUCTION.md) | **完整实测**：空工程 GUI、实算和结果比较 |
| HVAC_aircoil | 节点、风管、风机/盘管及连通性 | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/HVAC_aircoil/GUI-RECONSTRUCTION.md) | **完整实测**：空工程 GUI、实算和结果比较 |
| tunnel_demo | MULT 多网格、隧道火源与 MPI | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/tunnel_demo/GUI-RECONSTRUCTION.md) | **完整实测**；官方基线提前结束，因此数值结论为 REVIEW |
| tunnel_smoke_10s | 短时隧道 Smoke3D/矢量切片 | [GUI-RECONSTRUCTION.md](../tutorials/blank-gui/tunnel_smoke_10s/GUI-RECONSTRUCTION.md) | **完整实测**：空工程 GUI、实算和结果比较 |

## 每个教程统一结构

生产教程面板对每一步显示并保存：教学目的、完成效果、前置条件、菜单路径、输入参数、参数物理含义、设置原因、预期结果、常见错误和最终工程位置。其统一收尾步骤是：模型校验 → 保存和重开 → 生成 FDS → 选择 CPU 求解模式 → 加载 Results/Smokeview → 导出曲线和报告。

P27 已完成 First Fire 和原有七教程的生产 GUI/求解证据；P29 又完成其余五个新入门教程的独立工程、21 张逐步截图和 7 个实际 FDS 任务。十三个教程现在均有正式 GUI 路径证据，但自动化证据仍不能替代用户本人最终人工签字。
