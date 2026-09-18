# FireCAE P23 — 仿真参数、CPU 求解模式与任务中心验收报告

验收日期：2026-08-30  
构建：`D:\FireCAE\build\debug\FireCAE.exe`  
专项测试：`FireCAEP23SimulationTaskUiTests`、`FireCAEFdsRunnerTests`、`FireCAEFdsModelTests`

## 1. 已交付能力

### 1.1 专业仿真参数

“仿真 > 仿真参数...”现在提供面向工程用户的专业分页，而不是要求普通用户手写 namelist：

- 时间：标题、CHID、开始/结束时间、初始时间步；
- 环境与流动：环境温度、环境压力、三方向重力、相对湿度、仿真模式、湍流模型；
- 辐射与燃烧：辐射开关、辐射角度数、熄灭模型、固定混合时间；
- 输出与续算：设备/HRR/切片/边界/粒子输出间隔、Restart 源 CHID、检查点间隔；
- 风场、初始条件与 GEOM：风速/方向/粗糙度/参考高度、均匀初始区域、GEOM 记录摘要和正式转换入口说明；
- 数值方法：最大压力迭代次数、速度容差及高级编辑边界。

对话框执行标题/CHID、时间范围、初始区域范围和 Restart 参数校验。一次提交作为单一 Undo/Redo 命令进入工程历史。

### 1.2 FDS 数据模型与导出

- `FcProject` 新增 `FcSimulationParameters`，仿真参数属于工程业务模型而不是界面临时值。
- `.firecae` 格式版本升级到 4；旧格式继续读取，新参数可保存、重开和再次导出。
- `FdsWriter` 根据启用状态生成合法的 `TIME`、`MISC`、`RADI`、`COMB`、`DUMP`、`WIND`、`INIT` 和 `PRES` 记录。
- Restart 使用 FDS 的 `MISC RESTART/RESTART_CHID` 和 `DUMP DT_RESTART` 语义。
- `FdsSchemaRegistry` 增加上述记录的已验证字段；未启用专业设置时不强行覆盖原有默认值。

### 1.3 明确的 CPU 求解模式

运行对话框只显示真实可用的三种原生 FDS CPU 模式：

| 模式 | 启动程序 | 并行含义 |
|---|---|---|
| Serial CPU | `fds.exe` | 单进程、`OMP_NUM_THREADS=1` |
| OpenMP CPU | `fds_openmp.exe` | 单进程、用户指定共享内存线程数 |
| MPI CPU | `mpiexec.exe -n N fds.exe` | 多 CPU 进程，适用于多网格 |

没有 GPU 入口，也不把 OpenMP/MPI 标记为 GPU。对话框在排队前显示只读命令、工作目录、求解器和 `OMP_NUM_THREADS` 环境检查；不可用的运行方式不能提交。

### 1.4 任务中心

任务中心现在显示：

- 状态、工程/CHID、场景、具体求解器、进程数、线程数；
- 开始时间、FDS 仿真时间、进度、已用时间和预计剩余时间；
- 输出目录、完整命令预览和运行环境检查；
- 合并日志、标准输出和标准错误三个独立页；
- 停止、失败/取消后重试、打开输出目录和计算完成后打开结果。

队列仍支持限制并发数和多场景任务。运行前的工程导出与模型校验继续由主窗口正式流程执行，任务不绕过业务对象模型。

### 1.5 关闭与进程回收

专项测试发现并修复了一个真实生命周期缺陷：`QProcess::waitForFinished()` 在关闭阶段可能同步发出 `finished()`，旧实现会在 `finishRun()` 清空进程指针后继续解引用该指针并崩溃。

现在关闭路径先断开完成回调，再同步终止 FDS 进程树并等待退出，最后释放 Qt 进程对象。Windows 测试验证活动 FDS 进程 PID 在 `FdsRunner` 析构后不存在。

## 2. 双语界面

P23 新增的参数页、校验文本、Serial/OpenMP/MPI 运行对话框、命令/环境说明、任务中心线程列、输出通道和结果入口均提供英文与简体中文。语言测试直接创建两个生产对话框验证中文标题和 CPU 模式文本。

## 3. 自动化验收

`FireCAEP23SimulationTaskUiTests` 覆盖：

1. 生产菜单打开六页专业仿真参数对话框；
2. 从 GUI 设置时间、环境、重力、LES/VREMAN、辐射、燃烧、输出、Restart、风场、INIT 和 PRES；
3. 导出 FDS 并逐项检查专业字段；
4. Undo/Redo、保存工程、重新打开和再次导出；
5. Serial/OpenMP/MPI 三种命令预览、CPU 数量语义和无 GPU 入口；
6. 从主窗口排队并实际完成两线程 OpenMP 小算例；
7. 任务中心命令、环境、stdout/stderr、完成状态和结果入口；
8. 简体中文参数及运行对话框。

`FireCAEFdsRunnerTests` 覆盖：

- Serial、两线程 OpenMP、八进程 MPI 的真实 FDS 运行；
- SMV/OUT 结果、命令、工作目录、线程/进程元数据；
- 主动停止、失败诊断、排队、取消、重试；
- 活动求解器析构时的 Windows 进程树回收。

## 4. 验收结果

- P23 GUI 端到端专项：PASS。
- FDS Runner 真实求解及进程回收专项：PASS。
- P21 剪贴板环境兼容复验：PASS；系统剪贴板被外部程序占用时不再把 OS 状态误判为业务回归。
- 全量 Debug CTest：19/19 PASS，0 failed，总耗时 168.34 s。

## 5. 阶段边界

- P23 完成的是 FDS 仿真参数、真实 CPU 运行模式和任务生命周期，不宣称存在 GPU 求解器。
- Restart 参数已生成并持久化；长时续算的跨目录检查点选择、文件配套复制和中断恢复向导仍可继续增强。
- P24 负责把结果按物理含义组织并扩充原生结果显示；Smokeview 仍保留为完整 FDS 场结果的权威外部查看路径。
