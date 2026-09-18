# FireCAE P18 — 主工作区重构验收报告

日期：2026-08-29  
源码目录：`D:\FireCAE`  
验收程序：`D:\FireCAE\build\debug\FireCAE.exe`

## 1. 完成内容

- 中部统一为四个明确工作页：Model 3D、Plan 2D、FDS Record、Results。
- Plan 2D 使用独立 OpenCascade 视图，固定正交顶视图，并与 3D、模型树和属性面板按对象 UUID 双向同步。
- FDS Record 直接由当前 `FcProject/FcDocument` 通过 `FdsWriter` 实时生成；错误以 FDS 注释显示，不维护第二份模型数据。
- 左侧模型树增加搜索、类型过滤、楼层过滤、活动场景状态、可见性/锁定上下文操作。
- 大型对象分支采用延迟展开，默认每批加载 100 个对象；未加载对象仍可通过 `FcDocument::findObject(UUID)` 逐级物化并定位。
- 模型树显示名称列，列宽可调整，长名称使用中间省略并保留完整 Tooltip。
- 右侧包含可滚动对象属性、活动绘图工具、当前选择集、校验问题列表。
- 底部包含消息、FDS 输出摘要、求解任务中心和结果加载状态。
- “选择”工具已改为真实模式操作，不再显示未实现提示。
- Dock 状态、主窗口几何和当前工作页使用 `QSettings` 保存/恢复；恢复时按当前多显示器可用区域重新约束，避免窗口越界。
- 初始窗口按主屏幕可用区域的 86% 计算，不再使用固定 1280×820 上限。
- 中英文资源已补充 P18 新增工作区、检查器、加载状态和延迟加载文本。

## 2. 架构约束核对

- 模型树仍完全由 `FcProject/FcDocument/FcObject` 对象树生成。
- 业务树节点的 `Qt::UserRole` 保存 `FcObject` UUID。
- 树、3D、2D、属性和选择集的对象解析统一调用 `FcDocument::findObject(UUID)`。
- FDS Record、校验列表和输出摘要均从同一活动工程即时生成，无演示专用假数据。
- 没有引入 PyroSim 专有代码或资源。

## 3. 自动化验收

新增 CTest：

```text
FireCAEP18WorkspaceUiTests
  FireCAEUiTests.exe --p18-workspace-smoke
```

专项覆盖：

- 四个工作页及 3D/2D 控件存在；
- 属性在右侧，消息、任务、FDS 输出和结果状态在底部；
- 选择模式真实可用；
- activate_vents 工程切换到 FDS Record 后包含 `&HEAD`、`&MESH`、`&TAIL`；
- 2D 页面可见并可激活；
- 属性表单使用 `QScrollArea`；
- 250 节点业务树初始仅创建延迟入口，展开后为 100 个对象加一个继续加载项；
- 长名称有 Tooltip；
- 最后一个尚未加载对象可通过 UUID 正确定位。

最终 Debug 全量回归：

```text
100% tests passed, 0 tests failed out of 14
Total Test time (real) = 170.60 sec
```

其中七教程从空工程 GUI 重建测试耗时 124.04 秒并通过。

## 4. 构建说明

本机使用 Visual Studio 2026 开发环境、Qt 6.11.1、OpenCascade 7.8 配置编译。直接从普通 PowerShell 调用 Ninja 时，MSVC 可能找不到标准库头文件；构建应先加载：

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cmake --build D:\FireCAE\build\debug --target FireCAE FireCAEUiTests -j 4
```

## 5. 下一阶段

P18 已通过专项和全量回归，下一阶段进入 P19：2D/3D 专业绘图和持久化楼层系统。P19 将把当前通用 Floor 容器升级为带标高、层高、默认墙高、楼板厚度、背景图和裁剪状态的业务对象，并让墙体/房间/开口可在 2D 页面完整绘制和编辑。
