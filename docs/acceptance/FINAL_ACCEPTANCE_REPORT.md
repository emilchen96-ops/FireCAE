# FireCAE A08–A16 / P17–P32 持续验收报告

报告日期：2026-08-29  
当前版本：FireCAE 0.3.0  
项目目录：`D:\FireCAE`

## 一、总体结论

FireCAE 当前已经具备选定七个 FDS 教程的完整技术闭环：

`空工程 GUI 建模 → UUID 引用 → 校验 → 保存/重开 → 生成 FDS → 本机串行或 CPU/MPI 求解 → 结果加载 → Smokeview/原生后处理 → HTML/PDF/CSV 对比`

七教程共从空工程创建 151 个可编辑业务对象，导出输入 7/7 语义 PASS；七个 FireCAE 求解均由 FDS 6.11.1 正常结束。五个完整官方基线的数值门禁 PASS；两个官方长时基线本身中断/超时，按严格来源规则保留 REVIEW。

这证明选定算例链路可用，但不等于 FireCAE 已经实现 PyroSim 的全部功能。后续 P28–P32 又完成 FDS 6.11.1 Schema 对齐、五个入门教程、三方证据入口、原生 SLCF 解码和粒子/喷淋向导实算；A14 格式覆盖、其余原生三维后处理和 PyroSim 独立三方对比仍有明确边界。

最新 Release 全回归为 26/26 PASS（277.83 s）。P32 喷淋验收由正式 GUI 创建 `SPEC/PART/PROP/TABL/DEVC/DUMP`，FDS 6.11.1 求解到 2 s 并加载 25/25 个结果文件，详见 `P32_PARTICLE_SPRAY_WIZARD_REPORT.md`。

## 二、阶段状态

| 阶段 | 状态 | 核心证据 |
|---|---|---|
| A08 建模交互 | 通过 | 当前版本 10/10 连续 GUI 稳定性测试，60 张截图 |
| A09 建筑几何/FDS 离散 | 通过 | 两层建筑、OBST/HOLE/VENT、保存重开 |
| A10 FDS 对象建模 | 通过 | 七教程空工程 151 个对象，7/7 输入语义 PASS |
| A11 求解任务中心 | 通过 | 串行、8 MPI、停止、失败诊断、队列、重试 |
| A12 场景与参数研究 | 通过 | 差异场景、UUID 覆盖、批量任务 |
| A13 原生后处理 | 核心通过 | CSV/时间轴/统计/导出；Smoke3D/SLCF/BNDF/PART 帧缓存；ISOF/PL3D 与原生 GPU 体渲染未完成 |
| A14 IFC/CAD/BIM | 部分完成 | IFC、STL、OBJ、GLB、STEP、IGES、基础 ASCII DXF；FBX/DAE/DWG 和深层材质/Pset 未完成 |
| A15 可靠性/发布 | 通过 | 自动保存、恢复、诊断、工程包、便携发布 |
| A16 比较验证 | 通过（有两项 REVIEW） | 七教程与 plume_average 的 HTML/PDF/CSV |

## 三、自动测试和稳定性

最新完整测试基线：

| 构建 | 结果 | 总时间 |
|---|---:|---:|
| `build/gui-e2e` | 13/13 PASS | 209.70 s |
| `build/debug` | 13/13 PASS | 207.58 s |
| `build/release` | 13/13 PASS | 112.01 s |

A13 帧缓存曾暴露一次温度切片外部渲染抖动。生产渲染器现会清理不完整文件并自动重试一次；修改后该专项在 Debug 连续 5/5 PASS。最新 PDF 样式修改后，A16 在 Debug、Release、GUI E2E 三套构建均再次 PASS。

当前版本另执行 `FireCAEUiTests.exe` 10 个连续完整回合，10/10 退出码 0。证据位于：

`docs/acceptance/screenshots/A16-final-stability/run-01` 至 `run-10`，共 60 张截图。

## 四、七教程证据索引

- 手工逐步复现：`docs/acceptance/tutorials/blank-gui/<case>/GUI-RECONSTRUCTION.md`
- 可编辑工程：`docs/acceptance/tutorials/blank-gui/<case>/<case>.firecae`
- GUI 导出输入：`docs/acceptance/tutorials/blank-gui/<case>/<case>.fds`
- 六阶段截图：`docs/acceptance/tutorials/blank-gui/screenshots/`
- 真实求解：`tests/data/gui-generated/<case>/`
- 对比报告：`docs/acceptance/comparisons/<case>/`

总览说明：`docs/acceptance/SEVEN_TUTORIAL_GUI_ACCEPTANCE.md`。

## 五、七教程结果

| 教程 | 空工程 GUI 输入 | FireCAE FDS | 官方结果对比 |
|---|---:|---:|---:|
| activate_vents | PASS | 正常结束 20 s | PASS |
| bucket_test_2 | PASS | 正常结束 15 s | PASS |
| couch | PASS | 正常结束 600 s | REVIEW：官方约 10 s 中断 |
| couch_smoke_12s | PASS | 正常结束 12 s | PASS |
| HVAC_aircoil | PASS | 正常结束 1 s | PASS |
| tunnel_demo | PASS | 正常结束 30 s | REVIEW：官方 28.621181 s 超时 |
| tunnel_smoke_10s | PASS | 正常结束 10 s | PASS |

严格报告详见 `docs/acceptance/A16_COMPARISON_ACCEPTANCE_REPORT.md`。

## 六、发布产物

- Release 程序：`D:\FireCAE\build\release\FireCAE.exe`
- Debug 程序：`D:\FireCAE\build\debug\FireCAE.exe`
- 便携目录：`D:\FireCAE\dist\FireCAE-0.3.0-windows-x64`
- ZIP：`D:\FireCAE\dist\FireCAE-0.3.0-windows-x64.zip`
- ZIP 大小：199,234,814 字节
- ZIP SHA-256：`C7B184C6A7C5DD23FFFB6DFFE093D127A1AB0BAB3227D34D734686B7854BCE4A`
- 清单：518 个文件，默认不嵌入大型结果

最终 ZIP 已在本报告及教程文档同步后重新打包，且本机隔离 PATH 启动测试退出码为 0。

## 七、仍需人工或外部条件确认

1. **PyroSim 三方对比**：尚无 PyroSim 独立生成的 `.fds/.smv/.csv/.out`，不能宣称完成。
2. **人工播放录像**：七教程已从真实 `.smv` 逐例生成 7 张 Smokeview 结果帧并通过视觉检查；当前证据是自动化结果帧和分阶段 GUI 截图，不冒充人工鼠标操作录像。
3. **独立机器发布验证**：便携包已在本机隔离 PATH 启动通过，尚未在第二台 Windows 物理机验收。
4. **格式和后处理差距**：见 `docs/PYROSIM_PARITY_MATRIX.md`，未完成项不得标为已支持。

以上项目不会阻止已有代码和算例继续使用，但它们阻止“与 PyroSim 完全相同”的最终产品级声明。
