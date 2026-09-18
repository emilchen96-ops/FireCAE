# FireCAE GUI 端到端建模验收标准

## 总目标

每个教程必须从空项目开始，仅通过 FireCAE 界面完成以下闭环：

1. 创建工程与 FDS 对象；
2. 编辑对象参数；
3. 通过 UUID 选择并保存对象引用；
4. 执行模型校验，并在界面中定位错误；
5. 由当前对象树生成 `.fds` 文件；
6. 点击“运行当前项目”启动 FDS；
7. 计算结束后自动加载 `.smv` 及关联结果；
8. 从 FireCAE 启动 Smokeview 检查动态结果；
9. 与原生 FDS/PyroSim 同一算例比较关键输入与结果。

直接打开或复制教程自带 `.fds` 文件只能用于参考、回归和结果对照，不能计为“界面端到端建模通过”。

## 当前基础设施状态

| 能力 | 状态 | 验收证据 |
|---|---|---|
| 通用 FDS 对象创建 | 已实现 | 工具栏“添加 FDS 对象”和模型菜单中的分类创建命令 |
| 参数编辑 | 已实现（通用编辑器） | 参数名、Raw/Text/Reference 类型和值可编辑 |
| UUID 引用选择 | 已实现 | Reference 参数保存目标对象 UUID；选择器按参数所需类型过滤并显示 UUID，不以名称作为关联键 |
| 对象编辑、复制、排序与删除 | 已实现 | 树节点双击/右键编辑；复制生成新 UUID 和唯一 FDS ID；同级对象可上移/下移；被引用对象删除前报告引用方 |
| 模型校验 | 已实现 | 覆盖必填值、重复 ID、悬空/错误类型引用、CTRL 循环、HVAC 拓扑、MULT 展开、网格重叠/间隙/边界不连续和 SLCF 空间范围；错误与警告包含 UUID，消息双击可定位对象 |
| 生成 FDS | 已实现 | “运行当前项目”从当前 FcDocument 对象树导出 |
| 点击求解 | 已实现 | 单核运行和并行运行入口 |
| 自动加载结果 | 已实现 | 求解结束扫描 `.smv` 及关联文件并加入结果树 |
| 结果数值对比 | 已实现 | “结果 > 对比 FDS 结果”按 CSV 语义匹配不同 CHID，时间插值后比较终值、峰值、RMSE/NRMSE，并可导出 CSV 报告 |
| 首个 GUI 冒烟闭环 | 已通过 | `gui_mesh_e2e.fds`：MESH 由界面创建、校验、求解并加载结果 |
| 工程保存/重新打开 | 已实现 | `.firecae` JSON 保存对象树、稳定 UUID、参数顺序和 UUID 引用；模型单元测试覆盖往返 |
| 七教程模板入口 | 已实现并通过编译级 UI 测试 | 菜单从空工程创建七套真实 `FcObject`/`FcFdsNamelist` 对象树，不导入参考 `.fds` |

## 教程验收矩阵

2026-08-29 已新增 `FireCAEA10BlankTutorialUiTests`：目标工程从空项目开始，通过正式生产对话框逐个创建对象、编辑参数、选择 UUID 引用、校验、保存、重开和导出。教程配方只提供期望数据，不直接装载目标对象树；参考 `.fds` 只用于导出后的独立语义比较。

| 教程/案例 | 前处理对象覆盖重点 | 界面实现 | FireCAE 生成输入 | 求解 | 后处理/对照 |
|---|---|---:|---:|---:|---:|
| activate_vents | MESH、SURF、VENT、DEVC、CTRL | 空工程 GUI 通过，44 对象 | 输入语义 PASS | 20 s 完成 | 结果 PASS，状态变化日志与对比报告已生成 |
| bucket_test_2 | MESH、MATL、SURF、OBST、REAC、PROP、TABL、输出 | 空工程 GUI 通过，13 对象 | 输入语义 PASS | 15 s 完成 | 结果 PASS；落地水量 4.958014 kg |
| couch | SPEC、MATL、多层 SURF、PART、INIT、MULT、RAMP、燃尽 | 空工程 GUI 通过，26 对象 | 输入语义 PASS | 完整 600 s、8 MPI 完成 | FireCAE 正常；官方结果仅约 10 s，中断基线为 REVIEW |
| couch_smoke_12s | 烟气、SLCF、BNDF、PART、可视化输出 | 空工程 GUI 通过，26 对象 | 输入语义 PASS | 12 s 完成 | 结果 PASS；末值 HRR 538.27895 kW |
| HVAC_aircoil | HVAC NODE/DUCT/AIRCOIL、DEVC | 空工程 GUI 通过，20 对象 | 输入语义 PASS | 1 s 完成 | 结果 PASS；换热量 45.243122 kW |
| tunnel_demo | MULT 多网格、MISC、PRES、DUMP、火源、通风 | 空工程 GUI 通过，11 对象 | 输入语义 PASS | 完整 30 s、8 MPI 完成 | FireCAE 正常；官方 28.621181 s 超时基线为 REVIEW |
| tunnel_smoke_10s | 多网格、烟气、切片/边界输出 | 空工程 GUI 通过，11 对象 | 输入语义 PASS | 10 s、8 MPI 完成 | 结果 PASS；末值 HRR 8313.3098 kW |

每个已完成案例的证据位于 `tests/data/gui-generated/<case>/ACCEPTANCE.md`、`semantic-comparison.csv` 和 `key-results.csv`。七个案例现在都已有对应报告。

## 每个教程的通过条件

- 所有业务对象都能在模型树中看到，并拥有稳定 UUID。
- 对象引用在 UI 中选择，并在导出时解析为目标对象的 FDS ID。
- 保存、重新打开工程后，对象、参数和引用不丢失。
- 校验器至少覆盖必填参数、数值范围、空间范围、重复 ID、悬空引用和循环引用。
- 导出的 `.fds` 可由项目所带 FDS 正常完成计算，且无 FDS `ERROR`。
- 结果对象自动进入模型树，时间范围和主要结果类型可见。
- Smokeview 可从 FireCAE 打开并播放该案例结果。
- 与原生 FDS/PyroSim 的输入对象数量、关键参数和选定结果指标形成可复查记录。

## 尚未宣称通过的项目

- 自动化 GUI 已通过正式动作和对话框完成七教程从空工程的六阶段复现，并保存 42 张阶段截图；这证明生产 GUI 链路可重复，但不伪装成录像式人工鼠标操作。
- Smokeview 中已有代表性 Smoke3D、切片、边界和粒子播放截图；七教程还已从真实 `.smv` 逐例生成 7 张结果帧并完成非空白视觉检查。人工鼠标操作录像不冒充自动化证据。
- PyroSim 独立建模、导出和求解后的数值对比：必须取得 PyroSim 生成的独立输入/结果，当前不能用官方 FDS 结果代替。
