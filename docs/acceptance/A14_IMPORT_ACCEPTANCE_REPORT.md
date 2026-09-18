# FireCAE A14 IFC/CAD/BIM 导入验收报告

日期：2026-08-29

## 当前结论

A14 已完成第一组可运行闭环，但尚未满足总提示词中的全部格式和材质验收条件，状态为“开发中”。本报告只记录已经由代码和测试证明的能力。

## 已实现

- 统一 `GeometryImportService` 和分步导入向导：文件识别、单位、Y/Z Up、缩放、原点、网格离散精度、预览和质量报告。
- OpenCascade 实际导入：STL、OBJ、glTF/GLB、STEP、IGES。
- glTF/GLB/OBJ 的 XCAF 可视材质数量和纹理槽数量会进入质量报告；可用时保留代表性基色和透明度用于 3D 显示。
- FireCAE 自有 ASCII DXF 导入：`LINE`、`LWPOLYLINE`、`3DFACE`；二进制 DXF 会明确拒绝。
- IFC 继续通过隔离的 IfcOpenShell `IfcConvert` 工作进程导入，构件几何以 GlobalId 映射到独立 FireCAE UUID。
- IFC 构件按楼层继承 `floorName`，类、GlobalId、楼层、空间和 XML 属性进入可搜索标签；模型树搜索可查这些语义字段。
- IFC 语义树、UUID、GlobalId、来源、Schema 和构件 BREP 可保存进 `.firecae` 并重新打开。
- 可选择 IFC 模型根或单个构件进行 FDS 块预览/生成；墙、板、梁、柱、门、窗、开口、楼梯、空间采用明确的转换映射。
- 生成的 FDS 对象以标签保留源 FireCAE UUID、IFC GlobalId 和 IFC Class，可撤销转换。
- 纯三角网格的单位、轴向和原点会真实改写网格节点，不再只保存界面参数。
- DWG 没有合法 SDK 时明确禁用；FBX/DAE 在 Assimp 插件未安装时明确禁用，不伪装支持。

## 自动化证据

- STL：四面体 4 个三角形，源单位 mm，X 原点偏移 2 m；验收边界 `x=[2,3] m`。
- OBJ：OpenCascade 导入棱锥，6 个三角形。
- DXF：ASCII `3DFACE` 与 `LINE`，生成 1 个面和 2 个三角形。
- STEP：OpenCascade 写出并重新导入 2 m × 1 m × 0.5 m 实体，1 个 Solid、6 个 Face。
- GLB：隔离 IFC worker 生成带 1 个可视材质的二进制 GLB，FireCAE 检出材质并保存代表性基色；GUI 导入、显示、保存/重开通过。
- IFC 小模型：语义层级、GlobalId、构件形状、保存/重开和 BIM-to-FDS 源关联通过。
- IFC 复杂模型：`Clinic_Architectural_IFC2x3.ifc`，13 MB，成功映射 2,586 个独立几何构件。
- 完整 CTest：10/10 通过，43.30 秒（加入后续 A14 用例前的基线）。

GUI 截图：

- `docs/acceptance/screenshots/A14-01-stl-import.png`
- `docs/acceptance/screenshots/A14-02-glb-material.png`

## 仍未完成

- GLB 多材质分面和嵌入纹理的完整显示；当前保留代表性基色并统计纹理槽。
- FBX、DAE 的 Assimp 插件。
- DXF 更多实体类型、块引用、文字、圆弧和样条；当前不是完整 AutoCAD 兼容实现。
- DWG 商业 SDK（外部依赖）。
- IFC Property Set 和材料关系的深度提取；当前已索引分解树和 XML 属性。
- 几何简化、重复顶点/面、非流形、自交和法向修复的完整工具链。
- 复杂 IFC 的 GUI 截图、筛选后转换截图，以及 GLB/OBJ/STEP/DXF 的多格式 GUI 验收矩阵。

以上未完成项不得在功能矩阵中标为“已验收”。
