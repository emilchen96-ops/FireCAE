# FireCAE P22 — FDS/CAD/IFC 导入成熟化验收报告

验收日期：2026-08-30  
构建：`D:\FireCAE\build\debug\FireCAE.exe`  
专项测试：`FireCAEP22ImportMaturityUiTests`、`FireCAEA14GeometryImportUiTests`、`FireCAEFdsModelTests`、`FireCAEP21RecordSourceMapUiTests`

## 1. 已交付能力

### 1.1 FDS 输入导入

- 已支持 namelist 解析为 UUID 业务对象，保留原始 FDS `ID` 和记录顺序。
- 所有对象读入完成后再解析 `*_ID`、`FUEL` 和喷雾表引用，因此支持前向引用；对象内部保存目标 UUID，导出时恢复目标 FDS ID。
- 导入报告分别统计未知 namelist、已知 namelist 的未知字段、已解析引用和未解析引用。
- 未知 namelist 进入 `Additional Records`；未知字段保留在原业务对象高级参数中。
- 未解析引用保留原始文本并形成可见警告，不静默删除。
- 导入后的工程可编辑、校验、保存、重开和再次导出；未知记录与未知字段保持语义往返。

### 1.2 CAD/网格导入向导

生产文件选择器只声明已落地格式：

- IFC；
- STL；
- OBJ；
- glTF/GLB；
- STEP/STP；
- IGES/IGS；
- ASCII DXF。

FBX、DAE、DWG 不出现在生产导入过滤器中。DWG、FBX 和 DAE 的内部格式枚举只用于给外部路径提供明确的“不支持”诊断，不形成虚假入口。

通用几何导入在后台线程运行，显示阶段进度并支持取消。服务层在读取、变换、拓扑检查和三角化检查之间设置取消检查点；向导关闭时会请求取消并等待工作线程安全回收。

### 1.3 IFC 导入

- 导入前扫描 STEP 文本，报告 IFC Schema、源长度单位、文件大小、实体数、构件数、楼层数、空间数、材料关系数、Property Set 数和各 IFC 构件类型数量。
- 使用独立部署的 IfcOpenShell `IfcConvert.exe` 生成 GLB 几何和 XML 语义层级，FireCAE 不加载不兼容的 Python DLL。
- `IfcBuildingStorey` 和 `IfcSpace` 层级进入 `FcIfcObject` 对象树；构件继续使用 FireCAE UUID 与 IFC GlobalId 双重身份。
- 导入向导允许按 IFC 类型勾选构件，显示数量，并提供保持层级、按楼层合并、全部合并三种策略。
- 支持保持原几何或包围盒简化。
- 支持 Reference、OBST、HOLE、GEOM 四种 FDS 转换路由；路由进入持久化的 `FcIfcObject::fdsConversionRoute`，供后续建筑几何转换使用。
- 支持附加比例、Y-up/Z-up、X/Y/Z 原点平移、初始可见性、材料元数据和 Property Set 摘要保留选项。
- 导入报告列出预检统计、过滤数量、最终几何数量、合并/简化/FDS 路由、未匹配 GlobalId 和警告。
- `QProcess` 使用短轮询检查取消，不再由 GUI 线程执行长时间阻塞等待；取消或超时会终止并回收 IfcConvert 子进程。
- MainWindow 直接提交向导后台生成的 IFC 结果，不在点击 Finish 后重复同步转换。

## 2. 数据与持久化规则

- `FcDocument` 仍是工程事实来源，模型树不保存独立业务状态。
- IFC 语义节点、几何构件、可见性、标签、Schema、GlobalId、Source File 和 FDS 转换路由都随 `.firecae` 工程保存。
- IFC 模型根删除时删除整棵语义树及其显示对象；Undo/Redo 按模型根恢复/移除。
- FDS 引用只持久化 UUID；目标显示名称或 FDS ID 不是唯一关联键。
- 合并只改变用于显示/转换的 Shape 布局，不把导入构件名称当作关联键。

## 3. 自动化验收

`FireCAEP22ImportMaturityUiTests` 覆盖：

1. 前向 `DEVC CTRL_ID` 和 `CTRL INPUT_ID` 重建为 UUID 引用；
2. 未知 `&ZZZZ` 和 `CTRL.FUTURE_FIELD` 分项报告并再次导出；
3. 生产文件过滤器完整声明 IFC/STL/OBJ/glTF/GLB/STEP/IGES/DXF，且不声明 FBX/DAE/DWG；
4. IFC4 毫米单位样例的 Schema、单位、构件类型和实体预检；
5. 13 MB `Clinic_Architectural_IFC2x3.ifc` 的楼层、构件、材料关系、Property Set 统计；
6. 实际 IfcConvert 几何转换开始后触发取消，并验证无根对象残留；
7. IFC 向导中的类型树、数量预览、合并、简化、FDS 路由、进度和取消控件；
8. 后台导入、全部合并、包围盒简化和 GEOM 路由得到单一 Shape、100% 进度和完整报告。

`FireCAEA14GeometryImportUiTests` 回归 STL、GLB 材质、后台完成状态和工程保存/重开。  
`FireCAEFdsModelTests` 与 P21 测试回归 FDS 导入、未知记录和 UUID Source Map。

结果：PASS。

## 4. 全量回归证据

- Debug 全量 CTest（加入 P22 后）：18/18 PASS，0 failed。
- 总耗时：169.24 s。
- 新增复杂 IFC 运行中取消专项：PASS，6.65 s。
- 七教程从空工程逐对象 GUI 重建：PASS，110.62 s。
- FDS 求解器运行测试：PASS，4.57 s。

## 5. 阶段边界

- STEP、IGES 和 DXF 已有工程导入路径，但 DXF 当前覆盖 ASCII LINE、LWPOLYLINE、3DFACE 等基础实体；不宣称等同完整 CAD SDK。
- GLB/glTF 可读取颜色/材质/纹理统计，但复杂多材质编辑和纹理打包仍可增强。
- IFC 当前保存材料关系和 Property Set 的预检数量/摘要及 IfcConvert 提供的语义属性；完整 IFC 属性图谱、每个 Pset 属性值的专用表格编辑器仍是后续增强项。
- P22 不负责仿真参数、OpenMP/MPI 命令预览、Restart 和任务中心进程生命周期；这些进入 P23。

