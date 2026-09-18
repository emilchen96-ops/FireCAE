# FireCAE / PyroSim 2023.3 工作流差距矩阵（P17–P29 持续审计）

审计日期：2026-09-03  
审计对象：`D:\FireCAE` 生产代码、Release/Debug/GUI-E2E 构建、真实 FDS 结果及当前主机稳定性测试  
FireCAE 版本：0.3.0

## 1. 审计口径

- **已完整实现**：生产入口、对象持久化、导出/运行链路和自动化或真实算例证据均存在。
- **部分实现**：主链路存在，但缺少 PyroSim 2023.3 工作流中的专业界面、无损往返、性能或验收证据。
- **未实现**：生产程序没有可用入口或只有占位/外部替代能力。
- 菜单、静态截图、测试数据直接注入和文档声明均不能单独作为完成证据。
- PyroSim 仅作为公开工作流参考；FDS 字段和运行语义必须以 FireCAE 实际绑定的 FDS 版本为准。

## 2. 基线验证

2026-08-30 完成 P27 后运行三套完整 CTest：

- Release：24/24 PASS，185.51 s；
- Debug：24/24 PASS，324.40 s；
- GUI-E2E：24/24 PASS，320.20 s；
- 新增的 P27 First Fire 和复杂 IFC 测试均通过正式窗口、保存/重开、FDS 6.11.1 求解和结果加载链路；没有直接向目标窗口的 `FcDocument` 注入对象。
- P28 已将项目默认 Schema、绑定求解器版本探测和当前注册记录的 6.11.1 新增字段对齐；旧工程缺少版本时继续按 6.10 打开并给出迁移告警。
- P29 五个新入门教程创建 78 个 GUI 对象并完成 7/7 实际 FDS 任务；加入新测试后 Release 全量回归 25/25 PASS（320.46 s）。

已确认的基础约束：

- `FcDocument` 是对象树事实来源；
- `ModelTreeWidget` 从 `document()->groups()` 递归生成树；
- 对象树条目使用 `Qt::UserRole` 保存 UUID；
- 树点击通过 `FcDocument::findObject(UUID)` 解析对象；
- FDS 对象引用使用 UUID，`FdsWriter` 导出时解析为 FDS ID；
- 工程写入使用 `QSaveFile`；
- `D:\FireCAE` 当前不是 Git 仓库，无法用提交历史作为审计证据。

## 3. 工作流矩阵

| 能力域 | 状态 | 当前生产入口/实现 | 当前缺口 | 优先级 | 自动测试 | 人工验收方法 |
|---|---|---|---|---:|---|---|
| 主界面与导航 | 已实现（P18） | `MainWindow` 四页工作区、左右/底部 Dock、布局持久化、`ModelTreeWidget` 楼层过滤与场景状态 | P19 后继续增强楼层业务和绘图工具属性；当前 Drawing Tool 页显示活动工具和关键参数 | P0 | `FireCAEP18WorkspaceUiTests`、`FireCAEUiTests` | 四页切换、双视图选择、校验/选择集/FDS 输出/结果状态、布局区域及高 DPI 边界均通过 |
| 模型树对象来源和 UUID | 已完整实现（P18 性能补齐） | `ModelTreeWidget::refresh/appendObjectItem/loadNextChildBatch/ensureItemForObject`、`FcDocument::findObject` | 搜索时会主动物化待筛选分支；后续可改成后台索引进一步优化超大 IFC 搜索 | P1 | `FireCAEP18WorkspaceUiTests`、Core/UI tests | 250 节点测试验证初始延迟创建、100 个/批、Tooltip、可调列宽和未加载 UUID 定位 |
| 3D View | 已实现（P18/P19 基础工作流） | `OccViewWidget`、`OccViewer`、`GeometryDisplayManager`、`FdsSceneSynchronizer` | 双视图 UUID 选择、投影、操纵器、六面楼层裁剪、背景隔离、墙绘制及删除/Undo 显示生命周期已验收；命名视图和尺寸标注继续增强 | P1 | A07/A08、P18、P19 UI | 树/3D/2D 双向选择、变换、隐藏、锁定、裁剪、保存重开 |
| 2D View | 已实现（P19 建模基础） | 独立 `Plan2DView`、楼层背景/裁剪、共享墙草图状态机 | 已支持楼层选择、墙绘制、精确长度/角度、捕捉和 2D/3D 同步；复杂尺寸标注与所有对象的直接画布绘制继续增强 | P1 | `FireCAEP19FloorDrawingUiTests` | 切换楼层，在 2D 绘墙并在 3D/对象树同步，保存重开保持 UUID |
| Record View | 已实现（P21） | 实时/增量 FDS 预览、行号、语法高亮、搜索复制、风险草稿模式、行/字段到 UUID Source Map、校验错误定位 | HEAD/TIME 项目级字段尚不伪造对象映射；高级草稿不会反向覆盖业务对象 | P2 | `FireCAEP21RecordSourceMapUiTests`、FDS model | 点击 IJK 定位 MESH UUID；点击错误定位字段；退出高级草稿恢复正式文本 |
| Floors | 已实现（P19） | `FcFloorObject`、`FloorEditorDialog`、楼层过滤、背景 UUID 子对象、六面裁剪、复制到楼层 | 完整楼层属性和持久化已验收；后续补充更强的楼层剖切导航和尺寸标注 | P2 | Core、`FireCAEP19FloorDrawingUiTests` | 建立楼层，绘墙、背景隔离、裁剪、保存重开并保持属性/UUID |
| Scenarios | 已完整实现 | `FcScenario`、`ScenarioManagerDialog`、UUID 覆盖、参数矩阵、批量运行、实际 CSV 结果比较 | 结果比较要求双方已经求解；覆盖编辑仍保留高级 FDS 参数名 | P2 | A12 + P25 | 基准复制、禁用对象、参数覆盖、批量运行及两套 `.smv/CSV` 容差比较 |
| Meshes | 已实现（P20） | MESH 三级编辑器、透视实时预览、目标尺寸/数量双模式、D*/dx、多网格、自动完整单元、内存/计算量、对齐/重叠和 MPI 建议 | 后续可把多网格负载从文本建议增强为交互图表 | P2 | P20/FDS model/A10 UI | 专项测试验证 `IJK/XB` 隔离、D*/dx、切分、估算及生产 FDS 导出 |
| Materials | 已实现（P20） | 基础/专业/高级页、热物性、热解、温度曲线、含湿率、液体燃料、单位范围和 `FdsPropertyLibrary` | 后续可扩充更多厂商材料数据库内容 | P2 | P20/FDS model | 固体、热解和液体字段可编辑，UUID 曲线引用及范围校验通过 |
| Surfaces | 已实现（P20） | 按材料层、传热、火源、气流、外观、辐射、点火、粒子和物种分组；火源向导和六面赋值 | 后续可增加更多面向行业的命名预设，不影响当前字段完整性 | P2 | P20/A10/A09 | 分层/燃烧/通风/注入字段及 UUID 引用均由专业页编辑 |
| Reactions | 已实现（P20） | REAC 业务字段及火源向导，支持燃料、烟/CO 产率、燃烧热和辐射份额 | 后续扩充燃料预置库 | P2 | P20/A10 | 火源向导创建并导出独立 REAC，CO/辐射语义通过 |
| Species | 部分实现 | SPEC Schema 和 UUID 引用 | 缺少专业物种向导、初始/边界注入流程和常用数据库 | P1 | A10 | 创建背景/示踪物种并在 INIT/SURF/输出引用 |
| Particles | 部分实现 | PART/PROP/TABL Schema、Smokeview 粒子结果入口 | 缺少粒子、喷嘴和喷雾联合专业窗口及喷雾分布预览 | P1 | A10 | 喷淋/喷嘴粒子创建、PROP 引用、实际粒子输出 |
| Geometry | 部分实现（P19 主流程完成） | 墙、板、柱、梁、房间、门窗、楼梯、屋顶、扫掠、布尔、修复、测量、镜像、阵列、对齐、复制到楼层，支持 OBST/HOLE/VENT/GEOM/IGNORE/REFERENCE | 绘图与变换生产入口已完成；复杂多墙自动修剪和高级尺寸标注继续增强 | P1 | A08/A09、P19 | 从空工程建立楼层并绘墙，变换/复制，保存重开及 FDS 策略检查 |
| Snapping/精确输入 | 已实现（P19） | `SnapManager`、墙草图捕捉点、精确长度/角度、实时状态栏 | 端点/中点/网格/正交/交点/边/面/中心和角度捕捉已实现；后续可增加捕捉图标样式 | P2 | Core、`FireCAEP19FloorDrawingUiTests` | 连续绘墙、精确长度/角度、Esc/Enter、Undo/Redo |
| Devices | 已实现（P20） | 设备与控制向导覆盖温度、热电偶、烟/热感、喷淋/喷嘴、热流、气体、HRR、流量、Beam、Aspiration；PROP/位置/阈值/方向使用专业控件 | 更复杂的多点批量布置可后续增强 | P2 | P20/A10 | 从生产向导创建 DEVC 并导出，扩展 QUANTITY 兼容旧教程 |
| Controls | 已实现（P20） | 设备与控制向导、CTRL 基础/专业页、UUID 触发/受控对象、延时、激活/停用、环路校验和逻辑图 | 图形画布目前用于查看，复杂数学表达式继续由专业/高级页编辑 | P2 | P20/A10 | 生产向导创建 DEVC+CTRL，受控对象导出解析为 CTRL FDS ID |
| HVAC | 已实现（P20） | HVAC 业务字段、类型下拉、UUID 拓扑引用、节点/风管/风机/过滤器/盘管参数、连通性/环路检查和网络图 | 后续可增加画布拖线建网，不影响当前表单建网闭环 | P2 | P20/A10/A11 | 类型、引用选择、拓扑诊断及 HVAC 输出目标通过 |
| Output | 已实现（P20） | 统一输出向导覆盖 DEVC/SLCF/矢量/BNDF/ISOF/PL3D/SM3D/PART/PROF/HVAC，按数量、平面、点和对象选择 | 结果显示能力单独由 P24 扩充 | P2 | P20/A10/A13 | 伪类型映射为合法 SLCF/DEVC/DUMP，生产导出通过 |
| Simulation Parameters | 已实现（P23） | `SimulationParametersDialog` 六页专业界面；`FcSimulationParameters` 工程持久化；TIME/MISC/RADI/COMB/DUMP/WIND/INIT/PRES 合法导出；GEOM 正式入口说明 | 跨目录检查点文件选择/复制和长时 Restart 恢复向导仍可增强 | P1 | `FireCAEP23SimulationTaskUiTests`、FDS model | GUI 设置全部专业参数，Undo/Redo、保存重开并逐字段校验 FDS 文本 |
| FDS Schema Version | 已实现（P28 当前注册范围） | `FdsSchemaRegistry` 支持 6.7–6.11.1，新工程默认 6.11.1；运行对话框探测实际 `fds -v` 并显示求解器/工程 Schema；旧工程有显式迁移告警 | 完成的是 FireCAE 当前注册 namelist 的 6.11.1 增量对齐；尚未把 FDS 全部冷门/实验字段包装成专业 GUI | P1 | FDS model/Runner/P23/P27 | 6.10 隔离、6.11.1 新字段、版本探测、旧工程迁移、First Fire/复杂 IFC 全链路均通过 |
| FDS Import/Export | 已实现（P22） | `FdsImporter`、`FdsWriter`、原始 ID、UUID 引用、前向引用、未支持字段/引用报告、Additional Records、未知 namelist/字段工程往返和 Source Map | 注释、空白和原始排版会规范化，不宣称逐字节文本无损 | P2 | P21/P22 UI、Round-trip、FDS model | 前向 DEVC/CTRL 引用重建；未知记录/字段报告、导出、保存重开不丢失 |
| CAD 通用导入 | 已实现（P22 基础格式） | `GeometryImportWizard` + QtConcurrent + OCCT：STL/OBJ/glTF/GLB/STEP/IGES/ASCII DXF，单位/轴/原点/质量报告/进度/取消 | GLB 多材质编辑、复杂纹理打包和 DXF 全实体覆盖有限；FBX/DAE/DWG 正确不显示 | P2 | A14/P22 | 后台 STL/GLB 导入、质量报告、取消控件、保存重开；白名单格式检查 |
| IFC Import | 已完整实现（P22/P27 主链路） | `IfcImportService` + 隔离 IfcConvert；单位/Schema/楼层/空间/类型数量、过滤、合并、包围盒简化、Reference/OBST/HOLE/GEOM、报告/失败清单、后台进度与取消 | 完整 IFC 属性图谱和每个 Pset 属性值的专用编辑器仍可增强；这属于深度 BIM 数据管理，不阻断当前导入转换链路 | P2 | A03/A14/P22、`FireCAEP27ComplexIfcE2ETests` | 13 MB Clinic IFC2X3：按墙/板/门/窗过滤，1395 个几何对象，生成 1083 OBST + 312 HOLE，保存/重开/导出并用 FDS 6.11.1 正常完成 0.1 s 求解 |
| Solver | 已实现（P23 CPU 路径） | Serial=`fds.exe`/1 线程，OpenMP=`fds_openmp.exe`/N 线程，MPI=`mpiexec -n N fds.exe`；运行对话框、命令/环境预览、队列、取消、重试、进度、stdout/stderr、结果入口 | 没有 GPU 求解器且界面不显示 GPU；BFDS/Remote 未配置时不进入生产运行对话框 | P1 | `FireCAEP23SimulationTaskUiTests`、`FireCAEFdsRunnerTests` | 三模式命令预览；真实 Serial、2 线程 OpenMP、8 进程 MPI；关闭时验证进程树回收 |
| Results 基础播放 | 已完整实现（P24） | `NativeResultViewerWidget`：播放/暂停/停止/前后帧/时间滑块/跳转/速度/循环/自动刷新/十一类结果树/色标/范围/截图/PNG 帧/MJPEG AVI | 对 CSV 和 Smokeview 帧缓存工作流完整；AVI 为真实 RIFF/MJPG 文件 | P2 | P24/A13 | 打开真实 `.smv`，播放、刷新、跳转、导出曲线/截图/AVI |
| Results 几何与场数据显示 | 部分实现（P24 来源边界已成熟） | FireCAE 原始几何、FDS Requested、FDS Actual 网格吸附预览、CAD/IFC Reference；Smokeview 帧缓存和外部入口明确区分 | Smoke3D/SLCF/BNDF/PART 仍不是完整原生二进制/GPU 渲染；ISOF/PL3D 原生显示未实现；权威 FDS Actual 仍在 Smokeview | P1 | `FireCAEP24ResultsMaturityUiTests`、A13 render cache | 检查四种几何来源、显示/隐藏、来源状态和 Smokeview 外部路径 |
| Smokeview 集成 | 部分实现 | 外部启动、Windows HWND 嵌入、播放控制、恢复父窗口 | 外部窗口嵌入依赖 Win32，仍需多 DPI/关闭顺序和异常进程稳定性验收 | P1 | A13/稳定性 smoke | 125%/150% 缩放下嵌入、播放、返回模型、关闭主程序无崩溃 |
| Results Comparison | 部分实现（P24 可叠加） | FDS 输入比较、结果 CSV/HTML/PDF 报告；Results 页支持任意多算例同名/同单位曲线虚线叠加、来源标签和合并 CSV 导出 | 尚无合法、独立 PyroSim 结果完成三方数值证据；不能宣称 PyroSim 数值一致 | P2 | P24/A16 | 导入 Native FDS、FireCAE、PyroSim 三套独立结果并记录来源 |
| Libraries | 已完整实现 | `FdsPropertyLibrary` 八类内置/用户条目、导入导出、复制、冲突策略 | 基础专业库已完整；不包含 PyroSim 商业数据库的全部条目 | P1 | Core/UI + P25 | 八类分类、用户条目、冲突处理、引用到工程、保存重开 |
| Preferences | 已完整实现 | 中英文、单位、主题、FDS/MPI/Smokeview、自动保存/结果、运行前保存、打开前备份、建模默认值、高 DPI、大文件阈值、日志 | 高 DPI 切换需要重启；操作系统级缩放仍需 P27 人工矩阵 | P1 | A15 + P25 | v2 设置完整往返、路径生效、重启、备份和大文件提示 |
| Tutorials | 已实现（P26–P29） | 13 个分步教程均从真正空工程启动并检查真实对象树；First Fire、七个 FDS 教程和其余五个入门教程均有生产 GUI 建模、保存重开、导出、真实求解和结果证据 | 自动 GUI 驱动已完整，仍需用户按报告进行最终人工复核 | P1 | P26、P27、P29、A10 blank tutorial | P29 五教程创建 78 对象、21 张截图、7/7 FDS 任务 Completed；原七教程证据继续保留 |
| Stability and Packaging | 已实现（当前主机验收） | 自动保存/恢复、诊断、工程包、便携 ZIP；外部 FDS/Smokeview 有界关闭；Release/Debug/GUI-E2E 24/24 | 未完成第二台物理 Windows 主机验证、数字签名安装器；`D:\FireCAE` 无 Git 元数据 | P1 | A15、P27 35/35 循环、三配置全回归 | 启停 10 轮、保存重开 10 轮、IFC 取消/求解停止/结果播放各 5 轮；无新增残留 FDS/MPI/Smokeview 进程 |

## 4. 最终冻结的高优先级差距

1. 完成 Smoke3D/SLCF/BNDF/PART/ISOF/Plot3D 的原生二进制解码和 GPU 渲染；在此之前 Smokeview 是权威场数据显示器。
2. 取得独立 PyroSim 导出及求解结果，完成 Native FDS / FireCAE / PyroSim 三方来源可追溯比较。
3. 为 Species、Particles/喷雾和复杂数学控制补齐更深的业务编辑器。
4. 在第二台物理 Windows 机器验证便携包/安装包，并增加数字签名。

## 5. P18 原始建议交付边界（已完成记录）

以下是 P18 当时确定的第一批边界，相关工作已纳入当前 0.3.0 实现和回归测试，不代表仍待开发：

- 新增 `WorkspaceWidget`，提供 Model 3D、Plan 2D、FDS Record、Results 四个明确页面；
- 保留现有 `OccViewWidget`、`NativeResultViewerWidget` 和 `SmokeviewHostWidget`，通过适配器接入，避免重写已验收代码；
- 新增右侧 Inspector/Validation Dock，左侧只保留 Navigation；
- 保存/恢复主窗口 Geometry、State 和中央页面；恢复失败时回退默认布局并限制在当前屏幕；
- 模型树增加楼层过滤、场景状态、延迟加载策略和长名称完整 Tooltip；
- 删除全部生产占位入口；
- 新增 P18 专项 UI 测试后重新运行当前 13 项基线。

## 6. 当前结论

FireCAE 0.3.0 已具备工程对象、FDS 导入导出、建筑几何、复杂 IFC、CPU 串行/OpenMP/MPI 求解、Smokeview 和 Results 工作区的可运行闭环。P27 已用 First Fire、设备/控制、HVAC、复杂 IFC 和七教程完成当前主机上的生产 GUI 自动验收与真实 FDS 证据归档；P28 完成当前注册范围的 FDS 6.11.1 对齐；P29 又补齐五个入门教程，当前 Release 回归 25/25 通过。

这不是“完全复刻 PyroSim”。完整原生烟火体渲染、ISOF/Plot3D 解码、第二台机器包装验证和独立 PyroSim 数值证据仍未完成。P28 已消除当前注册 Schema 与绑定 FDS 6.11.1 的版本事实冲突，但不宣称覆盖 FDS 全部实验字段。任何 PyroSim 数值一致性结论必须等待真正独立的 PyroSim 结果。
