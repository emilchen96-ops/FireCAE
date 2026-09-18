# FireCAE A13 原生后处理阶段报告

日期：2026-08-29

## 双后处理路径

FireCAE 现在同时保留：

1. 独立 Smokeview；
2. 主窗口内的 FireCAE 原生结果工作区。

两条路径均从模型树中的同一 `FcResultCase` 启动。

## 原生数据与图表

- FDS 双表头 CSV 解析，保留物理量名称和单位；
- 时间插值、最小值、最大值、平均值和绝对峰值；
- 多曲线、不同单位双 Y 轴、图例、鼠标读数和滚轮缩放；
- 播放、暂停、停止、上一帧、下一帧、时间跳转、循环和 0.1–16 倍速度；
- 可见曲线 CSV 导出、工作区 PNG 截图和无损 PNG 帧序列；
- 自动/手动范围、对数选项、色带、透明度、全局色标和设置持久化；
- 已有结果比较器提供不同 CHID 的语义匹配、时间插值、RMSE、NRMSE 和容差报告。

## 二进制场结果

`.smv` 扫描器为 Smoke3D、SLCF、BNDF、PART、ISOF 和 PL3D 建立文件类型、网格、物理量和单位索引。SLCF 还从原始 FDS 记录恢复 PBX/PBY/PBZ 平面。

Smoke3D、SLCF、BNDF 和 PART 可在原生工作区点击“生成场数据帧缓存”，由项目捆绑的开源 Smokeview 渲染后回到 FireCAE 内部时间轴播放。渲染任务可取消，显示缓存进度，并将帧按算例/结果类型隔离保存。

未实现的原生二进制解码器不会显示为已支持：ISOF/PL3D 当前仍明确引导到 Smokeview。真正的 GPU 原生体渲染、相机路径编辑和视频编码仍需继续实现，不能据此宣称与 Smokeview 完全等价。

## 自动验收

- `FireCAEA13NativeResultsUiTests`：结果打开、CSV 数据、时间轴播放、统计、CSV 和 PNG 导出；
- `FireCAEA13Smoke3dRenderTests`：对真实 `couch_smoke_12s` 结果依次加载并渲染 Smoke3D、温度切片、壁面温度边界和粒子；
- 当前全量 CTest：Debug、Release、GUI E2E 均为 13/13 通过；
- `build/debug/FireCAE.exe` 和 `build/release/FireCAE.exe` 均已同步编译。

一次完整 Debug 回归中曾出现温度切片外部帧生成未完成。生产 `SmokeviewFrameRenderer` 现在会删除不完整缓存、等待 250 ms 并自动重试一次；取消操作不会重试。修改后 `FireCAEA13Smoke3dRenderTests` 在 Debug 连续 5/5 通过，避免把偶发的外部 Smokeview 启动抖动直接升级为界面失败。

截图文件由带 `FIRECAE_ACCEPTANCE_SCREENSHOT_DIR` 的验收运行生成，统一纳入最终七教程报告。

七个教程还通过 `--a13-seven-tutorial-smokeview-evidence` 顺序调用生产 `SmokeviewFrameRenderer`，从真实候选结果各渲染一张最终/代表时刻帧，7/7 通过。截图位于 `screenshots/Smokeview-seven-tutorials/`，覆盖粒子、AMPUA 边界场、SOOT DENSITY 烟气场和 HVAC 温度切片。
