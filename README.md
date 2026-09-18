# FireCAE

## GitHub 源码备份（r13，2026-09-18）

本仓库保存最新 r13 试用版对应的应用源码、测试源码、七教程 FDS 输入、必要小型结果夹具和项目文档。**这是源码备份，不是完整安装包，也不表示全部功能已经最终验收。**

首次克隆、依赖准备、测试边界和后续提交方法见 [源码恢复与备份说明](docs/SOURCE_BACKUP.md)。当前状态见 [r13 修复复验记录](docs/acceptance/reacceptance-2026-09-15/R13_FIX_RETEST.md)。

不包含 Qt/OCCT SDK、FDS/Smokeview/IfcConvert 可执行文件、海量求解输出、私人会话或本机凭据。FireCAE 自有代码尚未指定开源许可证；公开备份不代表替项目所有者选择了 MIT/GPL 等许可证。

以下是原复验候选的工作流说明，部分路径指向原工作区，部分状态早于 r13；恢复构建时优先使用上面的备份说明。

FireCAE 是基于 Qt、OpenCascade 和 FDS 的 Windows 桌面火灾仿真工程软件。此源码目录是 **2026-09-15 复验候选**，包含建模、FDS 记录编辑、场景、任务运行和结果管理。它已超出早期 P03 阶段；本说明不代表最终验收通过。

## 当前界面与工作流

日常建模使用 **Model 3D（模型 3D）** 和 **FDS Record（FDS 记录）** 两个工作区。模型树通过对象 UUID 关联文档、几何和属性；在模型 3D 中创建或导入几何、调整视图并完成平面方向的操作，在 FDS 记录中查看和编辑求解输入。内部几何单位为 SI 米，显示单位与 FDS 输入单位应分别核对。

工程支持网格、障碍物、表面、反应、开口、设备、输出等 FDS 对象，以及场景覆盖、输入校验和 `.fds` 导出。典型流程是：创建或导入模型 → 设置 FDS 参数和场景 → 校验并检查导出记录 → 提交计算任务 → 查看任务日志和结果。

**Smokeview 是正式场结果查看入口**，在结果工作区内承载动画和播放控制。源代码仍保留 Plan 2D 和原生 Results 面板供兼容与专项验证；这两个旧标签在正常界面中隐藏。原生 CSV 曲线、切片读取或帧缓存的专项通过，不能替代 Smokeview 的实际场结果验收。

IFC 转换由独立的 IfcConvert 进程执行，几何由 OCCT 加载；导入、FDS 转换和求解网格表示属于不同步骤。具体对象转换范围和教程对照应以对应报告为准。

## 工程保存与旧文件边界

当前 `.firecae` 保存格式为 **v5**，保存模型 UUID、场景及具体 ResultCase/ResultFile 节点的类型、路径与结果元数据。新程序仍读取 v1–v4 工程；最高仅支持 v4 的旧程序不能读取新保存的 v5 文件。验证旧工程时应另存副本并保留原文件。

结果文件是外部路径引用，工程文件并不打包整套 FDS 输出。移动工程或结果目录后，需要重新核对实际文件路径和可用性。旧版本曾将结果保存为 Generic 普通节点，其已丢失的路径和类型不会自动恢复；明确选择真实 SMV 可以建立新结果节点，但不等于恢复旧 UUID 或自动迁移旧节点。

模型修复、旧结果重新关联和兼容检查见本轮[模型复查说明](../../../docs/acceptance/reacceptance-2026-09-15/review-model.md)。

## 计算任务与结果目录

每个任务在配置的输出根目录下创建独立运行目录：

```text
<输出根目录>/.firecae-runs/<任务 UUID>/
  <输入文件名>.fds       实际交给求解器的输入
  _source/              原始输入快照
  _inputs/              冻结的输入依赖，含 CSVF 网格配套文件
  _restart_source/      RESTART 任务最初冻结的继承基线
  run-manifest.json     来源、输入及依赖哈希清单
  run-result.json       任务结束状态与实际结果路径
  <CHID>.smv / .out / CSV / 场数据等
```

任务使用自身的输入快照和输出目录；重复运行或相同 CHID 的任务，应按任务 UUID 和实际 SMV 路径区分。批量场景还可在输出根下使用场景 UUID 子目录。打开结果时应选择对应任务中的 SMV，不能仅凭显示名称或输出根目录猜测案例。

候选已加入有界 RESTART 实现，**本轮真实复验尚未完成**：保持原物理模型，使用静态、显式单/多网格、未缩放时间，并继续到更晚且有限的 T_END。同 CHID 续算继承本例 checkpoint 与必要结果文件；用户明确指定新 CHID 和旧 RESTART_CHID 时，使用旧 checkpoint 生成独立结果。Retry 从该任务最初冻结的 `_restart_source/` 重试，不使用上次运行后已推进的 checkpoint。任务是否完成须有本次新增的正常结束记录和时间推进，继承 SMV 不变不单独构成失败或成功。

继承来源优先使用显式来源目录或输入同目录的 checkpoint；自动来源在配置的输出根目录中，按同一输入绝对路径和精确 CHID 选择最近一次成功任务，并记录父任务及文件哈希。真实 Serial/OpenMP/MPI 常规续算、Retry 和失败对照已取得通过证据；**输入目录 A 与输出根目录 B 分离的自动查找已取得失败红证，候选已修正搜索根，尚待修复后绿测及相对输出根复验**。MULT 网格扩展、时间缩放、动态外部控制、输出重定向及回退覆盖旧时间段不在当前声明范围内。实现与验证状态见 [RESTART 候选记录](../../../docs/acceptance/reacceptance-2026-09-15/restart-implementation-r4.md)和[二审核对](../../../docs/acceptance/reacceptance-2026-09-15/restart-second-review.md)。

输入依赖快照的高级模式限制、任务成功判定和数值比较边界见[求解与结果复查](../../../docs/acceptance/reacceptance-2026-09-15/review-solver-results.md)及[任务路径协议](../../../docs/acceptance/reacceptance-2026-09-15/task-path-integration.md)。这些限制仍须按本轮验收台账逐项关闭。

## 依赖与构建

- Windows x64、MSVC、C++17、CMake 3.21 或更新版本、Ninja。
- Qt 6 Core、Widgets、PrintSupport、Concurrent。
- OpenCascade 7.8 或更新的兼容配置，以及该 OCCT 包要求的第三方 DLL。
- IfcOpenShell IfcConvert 及其许可文件；当前 CMake 在 Windows 配置时检查它们。
- 真实计算和场结果验收需要可用的 FDS、MPI（并行任务）及 Smokeview 运行时。

从本候选源码目录执行命令时，**本轮已配置的构建目录为 `../build`，程序为 `../build/FireCAE.exe`**。本轮配置/构建脚本位于同级的 `../configure.cmd`、`../build.cmd`，其中 SDK 路径是构建机配置，应在另一台机器上调整。源码目录、可执行文件和对应测试日志必须来自同一个候选版本。

在已初始化 MSVC x64 编译环境的 PowerShell 中，可以复用已配置的候选构建：

```powershell
cmake --build ../build --parallel 4
ctest --test-dir ../build --output-on-failure
& ../build/FireCAE.exe
```

另一台机器应新建构建目录。以下命令中的环境变量需事先设置为本机 Qt SDK、包含 OpenCASCADEConfig.cmake 的目录及 OCCT 第三方运行时根目录：

```powershell
cmake -S . -B ../build-local -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:FIRECAE_QT_SDK" `
  -DOpenCASCADE_DIR="$env:FIRECAE_OCCT_CMAKE_DIR" `
  -DFIRECAE_OCCT_3RDPARTY_ROOT="$env:FIRECAE_OCCT_THIRDPARTY_DIR"
cmake --build ../build-local --parallel 4
ctest --test-dir ../build-local --output-on-failure
& ../build-local/FireCAE.exe
```

CMake 从当前源码的 `third_party` 部署已配置的 FDS、Smokeview、IfcConvert，并在构建 FireCAE 后部署 Qt、中文 Qt 翻译和所需 OCCT DLL。可通过 CMake 缓存中的运行时路径选项改为其他合法安装位置。交付时应保留程序旁的 DLL、插件、翻译和工具目录；仅复制 EXE 不构成完整部署。实际交付包还需单独启动验证。

为复验隔离应用偏好，可在启动前将 `FIRECAE_SETTINGS_DIRECTORY` 环境变量设为独立可写目录的绝对路径。UI 自动测试和真实窗口验收有不同的显示环境要求；不要把 offscreen 测试通过当作窗口布局与交互通过。

## 本轮验收依据

本轮报告保存在工作区 `docs/acceptance/reacceptance-2026-09-15`；以下相对链接对应当前 `work/reacceptance-2026-09-15/source` 布局。单独移交源码时应同时附上该报告目录和对应构建记录。

- [版本与构建基线](../../../docs/acceptance/reacceptance-2026-09-15/G0_BASELINE.md)：来源、输入哈希、工具链及候选构建约定。
- [功能验收矩阵](../../../docs/acceptance/reacceptance-2026-09-15/FUNCTION_ACCEPTANCE.md)：逐项检查及证据；未验项目应保留未验状态。
- [缺陷与复验台账](../../../docs/acceptance/reacceptance-2026-09-15/DEFECT_LEDGER.md)：失败基线、修复与复验状态。
- [教程对照协议](../../../docs/acceptance/reacceptance-2026-09-15/TUTORIAL_COMPARISON_PROTOCOL.md)：真实 FDS 运行和对照证据要求。
- [结果面板清理专项](../../../docs/acceptance/reacceptance-2026-09-15/review-native-viewer-cleanup.md)：本轮端到端清理崩溃的定位与验证状态。

历史阶段报告和既有样例仍在 `docs`、`tests/data` 中，属于背景或测试输入；最终结论以本轮修复后候选、实际执行日志和交付程序指纹为准。
