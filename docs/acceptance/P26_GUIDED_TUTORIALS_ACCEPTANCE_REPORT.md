# FireCAE P26 — 分步教程体系验收报告

验收日期：2026-08-30  
生产构建：`D:\FireCAE\build\release\FireCAE.exe`  
专项测试：`FireCAEP26GuidedTutorialUiTests`

## 交付

- 新增可停靠 `TutorialGuideWidget`，支持步骤列表、上一步/下一步、打开当前工具、检查、从头开始、退出和重新进入。
- 新增 13 个数据驱动教程：六个入门教程和七个现有 FDS 教程。
- 启动教程会创建真正空工程，不会装载 `FdsExamples` 或注入业务对象。
- 每一步包含目的、路径、具体参数、物理含义、设置原因、预期结果和常见错误。
- 检查器读取当前 `FcProject/FcDocument`，可验证空工程、工程设置、FDS 记录类型/数量、导入几何、场景数量、模型校验、正式保存、FDS 导出及结果加载。
- 当前目标 QAction 使用稳定对象名；工具栏上存在对应按钮时使用黄色边框提示，其他入口由菜单路径和“打开当前工具”按钮定位。
- 教程进度写入版本化应用设置，关闭面板后可在相同教程和工程中继续。
- 旧七个一键完成模型入口降级到 `Completed Examples (Reference)`，文字明确其只用于答案核对。
- Start Page 改为列出 13 个分步教程，不再双击后直接创建完成模型。

## 自动验收范围

- 目录中恰有 13 个唯一教程 ID；
- 每个教程元数据完整且不少于 6 个步骤；
- 每一步的操作、参数、物理解释、理由、预期和错误说明非空；
- First Fire 从空工程打开且无业务对象注入；
- 空工程检查通过，未完成工程设置不能通过下一检查；
- 退出并重新打开可保留步骤；
- 13 个正式 QAction 和七个参考示例入口同时存在；
- 生产窗口截图保存为 `docs/acceptance/screenshots/P26-01-guided-first-fire.png`。

## 验收结果

- 2026-08-30 重新编译 `FireCAE.exe` 与 `FireCAEUiTests.exe` 成功；仅保留既有 `QMouseEvent` 构造函数弃用警告，无编译错误。
- `FireCAEP26GuidedTutorialUiTests`：1/1 PASS，1.74 s。
- Release 全量 CTest：22/22 PASS，0 failed，总耗时 77.55 s。
- 截图 `docs/acceptance/screenshots/P26-01-guided-first-fire.png` 已实际生成（194,538 bytes），画面中可见空对象树、13 教程体系中的 First Fire 引导面板、步骤详情和检查/导航按钮。

## 边界

P26 不把教程目录或引导器中的预期结果当成实际求解证据。六个新教程和七个既有教程的正式 GUI 全流程、FDS 求解、结果播放和逐步截图属于 P27；只有生成真实文件后才能填写求解器版本、结束时间和数值比较结论。
