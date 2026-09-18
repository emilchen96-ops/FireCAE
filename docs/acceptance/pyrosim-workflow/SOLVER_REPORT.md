# FireCAE P27 求解器报告

## 1. 绑定运行时

- FDS：`FDS-6.11.1-0-gff928db-release`
- Serial CPU：`fds.exe`
- OpenMP CPU：`fds_openmp.exe`，线程数由运行对话框设置
- MPI CPU：`mpiexec -n N fds.exe`
- GPU：没有绑定 GPU 求解器，生产界面不显示 GPU 运行入口

## 2. 生产流程

运行当前工程会依次执行保存策略、模型校验、FDS 导出、环境检查、命令预览、任务排队、stdout/stderr 捕获、进度/模拟时间更新和结果入口。停止、关闭窗口和失败路径均回收进程树。

## 3. 实际算例

| 算例 | 模式 | 物理结束时间 | 结果 |
|---|---|---:|---|
| First Fire | Serial CPU | 30.0 s | 正常 STOP；总墙钟约 39.504 s |
| Complex IFC | Serial CPU | 0.1 s | 正常 STOP；FDS 墙钟 0.841 s |
| activate_vents | CPU FDS | 20 s | 正常 STOP |
| bucket_test_2 | CPU FDS | 15 s | 正常 STOP |
| couch | 8 进程 MPI 证据 | 600 s | 正常 STOP |
| couch_smoke_12s | CPU FDS | 12 s | 正常 STOP |
| HVAC_aircoil | CPU FDS | 1 s | 正常 STOP |
| tunnel_demo | CPU FDS（结果证据未单独断言进程数） | 30 s | 正常 STOP |
| tunnel_smoke_10s | CPU FDS（结果证据未单独断言进程数） | 10 s | 正常 STOP |

P23 专项测试还验证了真实 Serial、2 线程 OpenMP 和 8 进程 MPI 的命令及生命周期。

## 4. 稳定性

求解启动/停止 5/5 通过；关闭后没有新增残留的 `fds`、`fds_openmp` 或 `mpiexec` 进程。Debug、Release 和 GUI-E2E 全套测试均通过。

## 5. 版本边界（P28 更新）

运行时和新工程默认 Schema 均为 FDS 6.11.1。运行对话框会调用实际求解器的 `-v`，同时显示求解器版本和工程 Schema；版本不一致时给出告警。`FdsSchemaRegistry` 当前支持 6.7–6.11.1，并已按官方 6.10.1/6.11.1 源码差异补齐当前注册记录的新增字段。旧工程缺少版本时仍以 6.10 打开并记录迁移告警，不做静默升级。
