# FireCAE P24 — Results 工作区成熟化验收报告

验收日期：2026-08-30  
生产构建：`D:\FireCAE\build\release\FireCAE.exe`  
专项测试：`FireCAEP24ResultsMaturityUiTests`

## 1. 本阶段交付

- 将结果文件平铺列表替换为生产可见的语义对象树，固定提供 Geometry、3D Smoke/Fire、Slice、Vector Slice、Boundary、Isosurface、Particles、Plot3D、Devices、CSV、HVAC 十一类节点；旧平铺列表仅作为不可见的自动化兼容索引。
- `FdsResultScanner` 根据输入文件中 `SLCF VECTOR=.TRUE.` 标记矢量切片，避免仅按扩展名猜测。
- 保留并验收播放、暂停、停止、上一帧、下一帧、时间滑块、当前时间、速度、循环；把原时间数值框明确标为“跳转到时间”。
- 增加手动刷新、1–60 秒自动刷新和刷新状态；刷新时尽量保持当前文件、曲线和时间。
- 增加图表起止时间范围、完整范围恢复、多曲线、双 Y 轴、单位、图例、滚轮缩放、最小值/最大值/平均值/绝对峰值。
- 增加多个对比算例叠加；同名同单位曲线以虚线显示，来源标签可明确填写为 FireCAE、Native FDS 或 PyroSim；导出 CSV 同时写入叠加曲线及来源标签。
- 增加四种几何来源：FireCAE Original Geometry、FDS Requested Geometry、FDS Actual Geometry（网格吸附预览）、CAD/IFC Reference Geometry；提供显示/隐藏和七种显示风格。
- FDS Actual 的名称和状态明确说明它是按 MESH 单元吸附得到的 FireCAE 预览，权威体素化仍需在 Smokeview 中确认。
- Smoke3D、Slice、Boundary、Particle 的内嵌画面明确标记为 Smokeview frame-cache display；ISOF、Plot3D 和完整体渲染继续使用外部 Smokeview，不冒充 FireCAE 原生二进制解码。
- 增加真正的 Motion JPEG/AVI 视频导出。导出写入 RIFF/AVI、MJPG stream、movi 帧块和 idx1 索引，不再把 PNG 帧目录称为视频。
- 为 ModelTree 的内部 `QTreeWidget` 增加稳定对象名 `ModelTreeObjectTree`，消除新增 Results 树后自动化测试误取树控件的问题。

## 2. 来源边界

| 显示路径 | 当前含义 |
|---|---|
| FireCAE native display | CSV 曲线、统计、几何来源预览和网格吸附预览由 FireCAE 自己绘制 |
| Smokeview frame-cache display | FireCAE 调用 Smokeview 批量渲染真实场帧，再在 Results 页播放缓存图片 |
| External Smokeview | ISOF、Plot3D、完整烟火体渲染及权威 FDS Actual 体素显示 |

P24 没有实现 Smoke3D/SLCF/BNDF/PRT5/ISOF/Plot3D 的完整原生二进制解码或 GPU 体渲染，因此不得把帧缓存称为原生场解码。

## 3. 自动验收

`FireCAEP24ResultsMaturityUiTests` 验证：

- 十一类结果树存在且顺序固定；
- 真实 tunnel 结果中的 `VECTOR=.TRUE.` 被识别为 Vector Slice；
- 自动刷新、时间范围、几何显示和视频按钮存在；
- 第二个独立 `.smv` 算例可叠加，导出 CSV 含来源标签；
- 三帧结果可导出非空 RIFF/AVI 文件；
- Geometry 节点进入 FireCAE 原生几何画布；
- 四种几何来源完整，FDS Actual 可切换；
- Results 工作区可导出验收截图。

## 4. 回归结果

Release 构建成功。完整 CTest：

- 20/20 PASS；
- 0 failed；
- 总耗时 72.20 s。

专项联合回归 `FireCAEP24ResultsMaturityUiTests` 与 `FireCAEA13NativeResultsUiTests`：2/2 PASS。

## 5. 仍需后续完成

- 需要合法、独立生成的 FireCAE、Native FDS、PyroSim 三套结果，才能完成三方数值一致性验收；当前只完成了任意多算例叠加能力，不能声称已验证 PyroSim 数值一致。
- 原生 GPU 烟火体渲染、ISOF/Plot3D 解码仍未实现，外部 Smokeview 是明确保留的权威路径。
- P25 继续场景差异/结果入口、分类资源库、完整首选项和工程可靠性。

