# FireCAE A10 — FDS 业务对象建模与七教程空工程复现报告

验收日期：2026-08-29  
版本：FireCAE 0.3.0

## 结论

A10 的通用 FDS 对象建模闭环已经通过自动化 GUI 验收。七个教程均以新的空工程为目标工程，通过正式“项目设置”“添加 FDS 对象”“编辑 FDS 对象”和 UUID 引用选择界面创建；测试没有把教程 `.fds` 导入目标工程，也没有调用教程菜单直接填充目标对象树。

教程工厂仅作为期望数据配方，便于逐项驱动正式对话框和核对结果。

## 已验证的界面链路

1. 从空工程打开项目设置，填写标题、CHID、结束时间和 FDS Schema。
2. 通过“添加 FDS 对象”对话框逐个创建 MESH、TIME、SPEC、MATL、SURF、REAC、PART、PROP、TABL、OBST、VENT、DEVC、CTRL、RAMP、INIT、MULT、HVAC、SLCF、BNDF、DUMP、MISC、PRES 等对象。
3. 原始值、文本值和引用值使用统一参数表编辑；MESH 的 IJK/XB 使用专用网格助手。
4. 第一遍创建对象；第二遍通过正式编辑对话框把引用参数绑定到新对象 UUID。
5. 执行模型校验，保存 `.firecae`，重新打开，核对 UUID 和引用持久化。
6. 从重开的业务对象树导出 `.fds`，与官方输入做忽略记录顺序的语义比较。

## 七教程结果

| 教程 | 从空工程创建对象 | 项目保存/重开 | UUID 引用 | 导出语义对比 |
|---|---:|---:|---:|---:|
| activate_vents | 44 | 通过 | 通过 | PASS |
| bucket_test_2 | 13 | 通过 | 通过 | PASS |
| couch | 26 | 通过 | 通过 | PASS |
| couch_smoke_12s | 26 | 通过 | 通过 | PASS |
| HVAC_aircoil | 20 | 通过 | 通过 | PASS |
| tunnel_demo | 11 | 通过 | 通过 | PASS |
| tunnel_smoke_10s | 11 | 通过 | 通过 | PASS |
| **合计** | **151** | **7/7** | **7/7** | **7/7** |

专项测试：`FireCAEA10BlankTutorialUiTests`。完整 Debug、Release 和 GUI E2E 测试集中均包含该测试。

## 可复现产物

每个教程目录 `docs/acceptance/tutorials/blank-gui/<case>/` 包含：

- `<case>.firecae`：由 GUI 创建并保存、重开的工程；
- `<case>.fds`：重开后由当前业务对象树导出的输入；
- `GUI-RECONSTRUCTION.md`：从空工程开始的逐对象、逐参数操作记录，包含新 UUID 和引用目标。

截图目录 `docs/acceptance/tutorials/blank-gui/screenshots/` 包含每个教程的六个阶段：

1. 空工程；
2. 项目设置；
3. 对象创建完成；
4. UUID 引用完成；
5. 校验通过；
6. 保存重开并导出。

七个教程共 42 张阶段截图；另有 7 张最终模型截图，共 49 张。

## 验收边界

- 这里证明的是正式 GUI 业务链路能够从空工程创建并重建七套输入；自动化驱动对话框与人工逐次点击使用相同生产槽函数和校验路径。
- 教程专用按钮仍可作为学习快捷入口，但不计入本报告的“空工程 GUI 复现”成绩。
- 通用参数编辑器已经覆盖七教程所需记录；要达到 PyroSim 的易用性，仍需继续增加面向材料、反应、控制、HVAC 和输出的专用表单、单位说明与上下文帮助。
