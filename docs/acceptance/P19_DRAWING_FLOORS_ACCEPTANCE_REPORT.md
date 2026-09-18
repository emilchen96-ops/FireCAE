# FireCAE P19 — 专业绘图与楼层系统验收报告

验收日期：2026-08-29  
构建：`D:\FireCAE\build\debug\FireCAE.exe`  
专项测试：`FireCAEP19FloorDrawingUiTests`

## 1. 已交付能力

- 新增持久化业务对象 `FcFloorObject`，包含名称、基准高程、默认层高、楼板厚度、默认墙高、可见/锁定、2D 背景图和六向裁剪范围。
- 楼层及其建筑子对象均使用 FireCAE UUID；保存、重开和 Undo/Redo 后 UUID 保持稳定。
- 3D 与独立 2D 平面页共用墙体绘制状态机；新墙按所选楼层插入对象树，并继承楼层标高、墙高与楼层名称。
- 连续墙、Esc 取消、Enter 完成、Shift/正交约束、精确长度、精确角度和实时坐标/长度/角度状态反馈均进入生产界面。
- 增加端点、中点、网格、正交、角度、交点、边、面和对象中心捕捉；交点捕捉使用 OpenCascade 相交拓扑，墙草图同时使用已有墙段端点/中点/线段交点。
- 楼层背景图作为楼层 UUID 子对象持久化，保存嵌入图像数据，转换策略固定为 `REFERENCE`；仅在当前楼层的 2D 视图显示，3D 不显示。
- 楼层裁剪范围驱动真实六面 OpenCascade 裁剪盒。
- 建筑对象支持 `Auto/OBST/HOLE/VENT/GEOM/IGNORE/REFERENCE` 转换策略；忽略和绘图参考对象不会生成 FDS 块，并给出诊断。
- 已提供测量、镜像、阵列、对齐、复制并移动、复制到楼层和变换操纵器生产入口；复制会保留几何类型、宿主/控制 UUID、表面 UUID、楼层和可见性语义。
- 场景重建统一移除不再属于 `FcDocument` 的 OpenCascade 显示对象，并可恢复 IFC 显示，修复删除/Undo 后残留“幽灵几何”的生命周期问题。

## 2. 专项 GUI 验收路径

测试从生产 `MainWindow` 执行以下路径：

1. 从空工程打开“创建楼层”；
2. 输入 Level 02、4.2 m 标高、3.6 m 层高、0.22 m 楼板厚度、3.3 m 墙高、背景路径和裁剪范围；
3. 通过模型树 UUID 选择楼层；
4. 切换独立 Plan 2D；
5. 打开“绘制墙”，在视图中点击两个端点；
6. 验证墙体成为楼层子对象，背景仅在 2D 可见；
7. 保存 `.firecae`，使用生产序列化器重开；
8. 验证楼层、背景、墙体、父子关系、标高、FDS 参考策略及 UUID。

结果：PASS。

## 3. 核心与回归证据

- `FireCAECoreTests`：楼层属性保存/重开、网格/拓扑捕捉、REFERENCE 转换跳过等通过。
- `FireCAEUiTests`：方块创建、选择、Undo/Redo、IFC 删除恢复、显示对象生命周期通过。
- `FireCAEP18WorkspaceUiTests`：四页工作区、Dock、树延迟加载和 UUID 定位通过。
- `FireCAEP19FloorDrawingUiTests`：楼层、背景、裁剪、Plan 2D 墙绘制及重开通过。
- Debug 全量 CTest：15/15 PASS，0 failed，总耗时 155.09 s。

## 4. 非本阶段结论

- P19 不代表已经完成全部 PyroSim 功能；材料、表面、设备、控制、HVAC、输出、Record Source Map、求解任务中心和完整原生结果仍按 P20–P25 顺序推进。
- 当前墙连接保存邻近端点 UUID，门窗宿主和动态开口已有业务字段与几何重建；更复杂的多墙自动修剪/拓扑清理仍需在后续建筑回归案例中继续扩展。
