# FireCAE A16 — 输入与求解结果对比验收报告

验收日期：2026-08-29  
版本：FireCAE 0.3.0  
求解器：FDS 6.11.1 (`FDS-6.11.1-0-gff928db-release`)

## 结论

A16 已形成可复现的“输入语义 + 求解来源 + 数值曲线”对比链路，可输出 HTML、PDF 和 CSV。七个教程的 FireCAE 空工程 GUI 导出输入全部语义等价；七个 FireCAE 候选求解均正常结束。

五个具有完整官方结果基线的教程为“输入 PASS / 结果 PASS”。`couch` 与 `tunnel_demo` 的官方结果目录本身分别是约 10 s 中断样本和 28.621181 s 超时样本，而 FireCAE 候选分别正常计算到 600 s 和 30 s，因此严格来源门禁为 `FAIL / REVIEW`。这两个状态不是 FireCAE 求解失败，也不能改写为 PASS。

## 对比方法

- FDS 输入：独立解析 namelist，忽略记录顺序，比较记录数量、网格、总单元数、参数值和引用解析；原始文本差异单独保留。
- UUID 映射：从 `.firecae` 工程读取 `FireCAE UUID → FDS keyword/ID/name`，报告不以对象名称作为唯一关联键。
- 求解来源：记录输入路径与 SHA-256、FDS revision、求解完成时间、物理结束时间和是否正常终止。
- 数值结果：按物理量匹配 CSV，进行时间对齐/插值，比较末值、峰值、峰值时间、均值、RMSE、NRMSE 和最大绝对差。
- 证据：报告嵌入空工程 GUI 的最终截图，并列出可复现路径。

## 七教程矩阵

| 教程 | 输入语义 | FireCAE 求解 | 结果门禁 | 数值量 | 说明 |
|---|---:|---:|---:|---:|---|
| activate_vents | PASS | 正常结束 20 s | PASS | 14 | 完整官方基线 |
| bucket_test_2 | PASS | 正常结束 15 s | PASS | 13 | 完整官方基线 |
| couch | PASS | 正常结束 600 s | REVIEW | 13 | 官方目录为 `partial_600s_interrupted_at_12s`，物理结束约 10.007602 s |
| couch_smoke_12s | PASS | 正常结束 12 s | PASS | 13 | 完整短时基线 |
| HVAC_aircoil | PASS | 正常结束 1 s | PASS | 13 | 完整官方基线 |
| tunnel_demo | PASS | 正常结束 30 s | REVIEW | 13 | 官方目录为 `partial_30s_timeout_at_26s`，物理结束 28.621181 s |
| tunnel_smoke_10s | PASS | 正常结束 10 s | PASS | 13 | 完整短时基线 |

标准 `plume_average` 独立回归为“输入 PASS / 结果 PASS”，13 个数值量一致。

## 报告位置

七教程：

- `docs/acceptance/comparisons/<case>/<case>-official-vs-firecae.html`
- `docs/acceptance/comparisons/<case>/<case>-official-vs-firecae.pdf`
- `docs/acceptance/comparisons/<case>/<case>-official-vs-firecae.csv`

标准算例：

- `docs/acceptance/comparisons/plume_average/plume_average-official-vs-firecae.html`
- `docs/acceptance/comparisons/plume_average/plume_average-official-vs-firecae.pdf`
- `docs/acceptance/comparisons/plume_average/plume_average-official-vs-firecae.csv`

代表性 `activate_vents` PDF 已用 Poppler 逐页渲染检查。表格和曲线没有溢出；2800×1800 GUI 截图按比例缩放在单页内，未再被拆页或裁切。

七教程另有逐例 Smokeview 结果帧：`docs/acceptance/screenshots/Smokeview-seven-tutorials/`。这些图直接加载候选 `.smv` 和粒子/边界/Smoke3D/SLCF 结果，自动渲染 7/7 PASS，并经人工视觉检查确认结果场可见。

## 真实 FDS 候选求解证据

候选输出位于 `tests/data/gui-generated/<case>/`，每个 `.out` 尾部均包含 `STOP: FDS completed successfully`。FDS 总墙钟时间如下：

| 教程 | 墙钟时间 |
|---|---:|
| activate_vents | 10.700 s |
| bucket_test_2 | 32.628 s |
| couch | 4533.215 s |
| couch_smoke_12s | 57.444 s |
| HVAC_aircoil | 0.813 s |
| tunnel_demo | 708.057 s |
| tunnel_smoke_10s | 261.784 s |

空工程 GUI 新导出的输入已通过语义比较；上述候选输入与同一教程对象模型一致。因此没有为追求截图而重复运行耗时约 75.5 分钟的 `couch`，而是保留真实求解产物和完整来源链。

## PyroSim 对比边界

当前报告比较的是官方/原生 FDS 基线与 FireCAE。`tests/data/comparison/plume_average/pyrosim/` 只有结果占位说明，没有由 PyroSim 独立建模、导出并求解得到的 `.fds/.smv/.csv/.out` 结果。

因此本阶段不能宣称“已完成 PyroSim 数值对比”。取得合法、独立的 PyroSim 输出后，可用同一报告器增加第三组来源并生成三方报告；不能把官方 FDS 结果冒充 PyroSim 结果。
