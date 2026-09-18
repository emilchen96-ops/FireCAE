# FireCAE P20 — 面向用户的专业参数编辑器验收报告

验收日期：2026-08-29  
构建：`D:\FireCAE\build\debug\FireCAE.exe`  
专项测试：`FireCAEP20ProfessionalEditorsUiTests`

## 1. 已交付能力

- 所有已建模 FDS 对象采用“基础参数 → 专业参数 → 高级 FDS 参数”三级编辑结构；普通流程使用业务名称、单位、数值范围、枚举和对象选择，高级页继续无损保留原始参数。
- MESH 的 `IJK`、`XB` 只在高级页出现。基础网格页使用 X/Y/Z 起点、长度、终点、单元数或目标尺寸，并提供透视实时预览、总单元数、实际单元尺寸、内存估算和长宽比提示。
- 网格工程助手支持按几何加余量适配、多网格切分、4 的倍数调整、自动对齐完整单元、D*/dx 推荐、计算量指标、重叠/对齐诊断和 CPU MPI 负载建议。
- 材料页提供密度、比热、导热率、发射率、热解、温度曲线引用、含湿率、液体燃料沸点/汽化热/汽化率、单位及范围校验；项目属性库继续提供内置与用户材料/表面对象。
- 表面页按材料层、传热、火源、气流、外观、辐射、点火、粒子注入和物种注入分组；引用使用 UUID，导出时才解析为 FDS ID。
- 火源向导可以附着现有几何/OBST/VENT，也可以从位置和尺寸直接创建燃烧 VENT；可设置总 HRR 或 HRRPUA、面积、增长曲线、时间、燃料、烟产率、CO 产率和辐射份额，并自动创建 SURF、REAC、RAMP 与必要的 VENT。
- 新增“设备与控制向导”，以温度、热电偶、烟感、热感、喷淋、喷嘴、热流、气体浓度、HRR、流量、Beam 和 Aspiration 业务类型创建设备；可设置测点、阈值、方向、PROP，并通过 UUID 把 DEVC/CTRL 绑定到受控 VENT/OBST，支持延时及激活/停用初始状态。
- HVAC 基础页使用节点、风管、风机、过滤器、盘管、流量、面积、长度和损失系数等业务字段；网络引用、拓扑诊断和控制/HVAC 网络图保持可用。
- 输出向导覆盖 Devices、Slices、Vector Slices、Boundary、Isosurfaces、Plot3D、Smoke3D、Particles、Profiles 和 HVAC Output。界面伪类型会映射为合法的 `SLCF`、`DEVC`、`DUMP` 等记录，不会把 `SLCF_VECTOR` 或 `DUMP_PART` 写入 FDS。
- 新增字段和主要向导标签接入中英文界面；普通用户不需要输入 UUID 或逗号形式的 `XYZ/XB`。

## 2. 关键兼容性处理

- DEVC 的物理量控件是可编辑下拉框：内置常用量用于发现，但不会拒绝导入文件或新版 FDS 中的合法扩展量。
- 因此 `TIME`、`AMPUA`、`AIRCOIL HEAT EXCHANGE`、`NODE TEMPERATURE` 等既有教程量仍可保存、校验和导出。
- 未识别参数继续由高级参数页透传；专项改动未删除任何旧教程、测试或高级字段。

## 3. 专项 GUI 验收路径

1. 创建 MESH、MATL、DEVC、HVAC 对象编辑器，验证三级页、业务字段和 UUID 引用按钮；
2. 验证 MESH 的 `IJK/XB` 不出现在基础/专业页，只保留在高级表；
3. 运行网格工程助手，验证 D*/dx、内存、计算量、对齐、重叠和 MPI 建议并生成网格块；
4. 从无宿主火源向导创建 REAC、SURF、RAMP 和燃烧 VENT；
5. 通过设备与控制向导创建 DEVC、延时 CTRL，并将受控对象 UUID 引用写回目标；
6. 通过输出向导创建矢量切片和粒子输出周期；
7. 使用生产 `MainWindow::exportCurrentProjectToFds` 导出并检查合法记录、CO/辐射、CTRL 引用和 `DT_PART`。

结果：PASS。

## 4. 回归证据

- `FireCAEP20ProfessionalEditorsUiTests`：PASS。
- 七教程从空工程逐对象 GUI 重建：PASS。
- 原有主 UI、FDS 模型、场景、求解器、导入、可靠性、结果和对比测试：PASS。
- Debug 全量 CTest：16/16 PASS，0 failed，总耗时 222.36 s。

## 5. 阶段边界

- P20 完成专业输入流程，不代表 Record View 已具备行级 Source Map；该能力进入 P21。
- P20 保留通用高级页，以覆盖尚未建模或后续 FDS 版本新增字段；普通流程不会默认要求用户编辑原始 namelist。
