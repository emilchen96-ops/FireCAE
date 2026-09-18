# FireCAE R01 专业建筑建模阶段验收报告

报告日期：2026-08-29  
状态：**部分完成；本报告所列子链路已验收，R01 全量需求尚未验收**

## 1. 本轮完成的生产功能

- 视图连续绘墙，完成一段后以前一端点继续，`Esc` 结束；支持自动正交设置。
- 新墙端点可按容差吸附到已有墙端点，并保存被连接墙的 UUID。
- 新墙端点也可投影吸附到已有墙线段中部形成 T 形连接，`connectedStartWallUuid/connectedEndWallUuid` 随工程保存重开。
- 墙体左/中心/右基准线切换时可保持原物理占地；墙厚、高度、端点和旋转参数继续由建筑对象持有。
- 门、窗、洞口和墙面通风口保存宿主墙 UUID；宿主墙平移或旋转后，开口位置与方向随之更新，并进入同一 Undo/Redo 操作。
- 动态门洞通过 GUI 创建 `DEVC → CTRL(INPUT_ID UUID) → HOLE(CTRL_ID UUID)` 控制链；模型校验会拒绝缺失引用，FDS 会拒绝动态 HOLE 与其他 HOLE 重叠，修正位置后真实求解通过。
- 建筑对象可按对象选择 `Auto`、`OBST` 或 `GEOM` 转换路线。
- 原生 `GEOM` 由 OpenCascade 真实三角剖分生成 `VERTS/FACES`，保留拓扑面到表面 UUID 的分配，不修改源 CAD/BREP。
- 建筑编辑器除六方向表面外，按稳定 `TopoFace:<SHA1>` 面身份提供任意拓扑面表面选择；保存重开后恢复 UUID 分配。
- 新增独立“Assign Surfaces”专业对话框：支持多选批量默认面、六方向或全部拓扑面局部覆盖、清除覆盖后继承默认面、从另一几何对象复制，以及颜色和可用纹理缩略图预览；整次操作可 Undo/Redo。
- 几何质量报告增加顶点数、面数、重复顶点、重复面等检查，并保留已有闭合性、退化边、非流形和 OCCT 有效性检查。
- GUI 创建的通用 `MESH` namelist 现在可被建筑转换预览识别；实体/Hole 向外包络吸附到网格，平面 VENT 保持平面，避免薄墙塌缩成零厚度 OBST。
- 无表面引用的 VENT 使用安全的 `INERT` 后备并给出警告，不再生成位于内部边界的无效 `OPEN`。
- 自动生成的 `MESH/OBST/HOLE/VENT/GEOM` 使用正确记录顺序，FDS 在读取 GEOM 前已获得网格定义。

## 2. GUI 端到端验收

专项程序：

```powershell
$env:FIRECAE_ACCEPTANCE_SCREENSHOT_DIR='D:\FireCAE\docs\acceptance\screenshots\R01-professional-building'
$env:FIRECAE_R01_ARTIFACT_DIR='D:\FireCAE\docs\acceptance\R01-professional-building'
D:\FireCAE\build\debug\FireCAEUiTests.exe --a09-building-smoke
```

结果：`FireCAE A09 building GUI smoke test passed.`

该测试从空工程经生产菜单、对话框和真实视图鼠标事件完成：

- 普通两点绘墙；
- 连续绘制两段墙并以 `Esc` 结束；
- 地面板、两层房间、二层楼板、楼板洞口、门、窗、墙面通风口和楼梯；
- 温度 DEVC、引用该设备 UUID 的 CTRL，以及引用 CTRL UUID 的动态门洞；
- 通过正式编辑对话框修改墙端点，验证宿主开口跟随，并执行 Undo/Redo；
- 独立原生 GEOM 屋面；
- `MESH IJK=70,60,50, XB=-1,13,-1,11,-1,9`；
- FDS 块预览、生成、导出、工程保存和重新打开；
- 创建 `R01_BLUE` 表面，对两个对象批量赋值并覆盖全部拓扑面，将分配复制到第三个对象，再执行清除/继承和 Undo；保存重开后至少三个默认表面 UUID 及拓扑面 UUID 保持；
- 检查导出同时含 `MESH/OBST/HOLE/VENT/GEOM/VERTS/FACES`。

截图：

- [源建筑](screenshots/R01-professional-building/A09-01-building-source.png)
- [FDS 块预览](screenshots/R01-professional-building/A09-02-fds-block-preview.png)
- [保存重开](screenshots/R01-professional-building/A09-03-saved-reopened.png)

产物：

- [FireCAE 工程](R01-professional-building/r01_building.firecae)
- [GUI 导出的 FDS](R01-professional-building/r01_building.fds)
- [FDS 求解日志](R01-professional-building/r01_building.out)

## 3. 真实 FDS 验收

求解器：FDS 6.11.1，单 MPI 进程，`OMP_NUM_THREADS=1`。  
输入来自上述 GUI 端到端测试，不是手写参考输入。

导出记录计数：

| 记录 | 数量 |
|---|---:|
| MESH | 1 |
| SURF | 1 |
| DEVC | 1 |
| CTRL | 1 |
| OBST | 26 |
| HOLE | 4 |
| VENT | 1 |
| GEOM | 1 |

求解结果：

```text
Time Step: 1, Simulation Time: 0.10000 s
Total Elapsed Wall Clock Time (s): 27.996
STOP: FDS completed successfully (CHID: r01_building)
```

另有最小原生 GEOM 探针 [r01_geom_probe.fds](../../tests/data/r01_geom_probe.fds)，同样由 FDS 6.11.1 完成，用于隔离验证 `VERTS/FACES` 写法。

## 4. 自动回归

- Debug 全量构建：通过；仅保留两个既有 C4458 局部变量遮蔽警告。
- Debug 正式 CTest：13/13 通过，总耗时 218.97 秒；包含七教程空工程 GUI、求解、场景、后处理、导入、恢复和对比回归。
- `FireCAECoreTests` 专项：1/1 通过。
- 核心断言覆盖：稳定拓扑面身份、三角剖分、基准线占地保持、端点 UUID 吸附、宿主开口跟随、GEOM 写出、表面 UUID 保存重开、薄实体网格包络和平面 VENT。
- `--a09-building-smoke`：通过。

## 5. 尚未完成，禁止据此标记 R01 全量通过

- 任意墙交叉处的自动修剪、T/L/X 节点清理与交点后端点编辑仍需更完整的 GUI 验收。
- 合并共面面、法向一键修复、稳健自交检测/修复尚未达到专业网格修复器水平。
- 本轮场景只运行至 `T_END=0.1 s`，目的是验证完整输入读取、几何初始化和求解链路，不代表建筑火灾物理结果验收。

因此，R01 当前状态为“专业建筑基础链路部分完成”，下一轮仍需补齐上述项目后再宣告 R01 全量通过。
