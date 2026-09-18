# FireCAE P30 — 三方对比证据入口验收

日期：2026-09-03

## 已完成

- 新增 Native FDS / FireCAE / PyroSim 三来源 JSON 清单；
- 为 `.fds`、`.smv`、FireCAE `.firecae`、PyroSim `.psm` 计算 SHA-256；
- 强制 FireCAE/PyroSim 生产者标识和专属工程文件；
- 强制 PyroSim ISO-8601 导出时间；
- 拒绝同一物理 `.fds` 或 `.smv` 路径冒充多个来源；
- 分别执行 Native↔FireCAE、Native↔PyroSim、FireCAE↔PyroSim 三组输入与结果比较；
- 输出可机器读取 JSON 和人工复核 Markdown；
- 命令行明确区分 PASS、待证据、证据无效和数值对比失败。

## 自动化验证

- 三个独立目录、工程证据齐全、结果相同：`PASS`；
- 缺少 PyroSim `.psm`：`PENDING_EVIDENCE`，不运行比较；
- 同一个 `.smv` 路径重复用作两个来源：`INVALID_EVIDENCE`；
- Core Release 测试：PASS。

## 当前真实验收状态

证据接收和防误报基础设施已经完成，但真实 PyroSim 第三来源仍是 **PENDING_EVIDENCE**。当前用户不允许接管鼠标键盘，因此本轮没有操作 PyroSim GUI，也没有伪造 `.psm` 或把既有 Native/FireCAE 结果重命名为 PyroSim 结果。

证据目录和操作说明：`pyrosim-workflow/three-way/README.md`。
