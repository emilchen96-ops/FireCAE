# FireCAE A15 — 工程可靠性、恢复与发布验收报告

验收日期：2026-08-29  
版本：FireCAE 0.3.0  
结论：A15 核心功能通过；已生成可独立启动的 Windows x64 便携包。

## 1. 已实现范围

### 1.1 自动保存与崩溃恢复

- 定时自动保存，可配置启用状态、间隔和最多保留份数。
- 在覆盖/替换当前工程的重要操作前创建恢复点。
- 恢复副本包含工程快照、来源工程、时间、原因和运行设置元数据。
- 启动时检测恢复副本，提供“恢复、丢弃、查看路径”。
- 恢复工程以未命名副本方式打开，不覆盖正式工程；用户必须另存。
- 正常保存后清理该工程对应的恢复副本。
- 启动页同时列出全部可用恢复副本。

### 1.2 未保存保护

- 新建、打开、导入并替换 FDS、打开教程、打开标准算例和退出均统一使用“保存、丢弃、取消”保护。
- 场景管理在对话框工作副本中编辑，只有确认后才写回工程；切换后的工程标记为已修改，后续替换或退出仍受统一保护。
- 自动化测试专用的无提示分支只由显式环境变量启用，不影响正式软件。

### 1.3 崩溃诊断

- 安装 Qt 全局消息处理器、`std::terminate` 处理器和 Windows 未处理异常过滤器。
- 写入会话日志，保留最近 50 条高层操作。
- “帮助 > 诊断”显示并可复制：FireCAE/Qt/OCCT 版本、操作系统与架构、程序路径、FDS/Smokeview 路径、日志/恢复目录和最近操作。
- 报告不复制 IFC、FDS 或工程文件内容，避免泄露用户模型。

### 1.4 应用设置

- 版本化设置格式（当前格式版本 1）。
- 支持语言、默认单位、主题、3D 背景、自动保存、FDS、Smokeview、MPI、求解后自动打开结果、默认工作目录、日志级别和渲染质量。
- 最近工程列表去重并保持顺序。
- 配置路径失效时自动回退到软件内置 FDS/Smokeview。

### 1.5 工程资源与可移植工程包

- 扫描 IFC、通用几何、纹理/图像和结果引用，显示 UUID、属性名、绝对/相对状态与缺失状态。
- 资源重定位按“业务对象 UUID + 属性名”更新，不以显示名称作为关联键。
- 支持改为相对路径、复制工程到新目录、生成/解包 `.firecaepkg`。
- 包格式有魔数、格式版本和路径穿越校验。
- 默认不把大型结果文件嵌入工程包。
- “清理未使用结果”只允许删除确认位于结果根目录内的精确文件，并在界面明确说明不会进入回收站。

### 1.6 启动页

- 提供新建、打开、最近工程、七个教程、恢复副本、版本号、FDS 状态和 Smokeview 状态。
- 启动页截图：`docs/acceptance/screenshots/A15-01-start-page.png`。

## 2. Windows 便携发布包

输出：

- 目录：`D:\FireCAE\dist\FireCAE-0.3.0-windows-x64`
- ZIP：`D:\FireCAE\dist\FireCAE-0.3.0-windows-x64.zip`
- ZIP 大小：199,234,814 字节（约 190.01 MiB）
- ZIP SHA-256：`C7B184C6A7C5DD23FFFB6DFFE093D127A1AB0BAB3227D34D734686B7854BCE4A`
- 清单生成时间：`2026-08-29T01:46:10Z`
- 清单记录文件：518 个（`package-manifest.json`；清单文件自身不计入该数值）

包内包含：

- FireCAE Release 可执行文件；
- Qt 6 与 OpenCascade 运行时；
- IfcOpenShell `IfcConvert` 独立工作进程；
- FDS 串行/OpenMP/MPI 运行时；
- Smokeview 运行时；
- Microsoft VC++ x64 Redistributable；
- FDS、Smokeview、IfcOpenShell、Qt、OCCT 许可证/声明；
- 教程文档和七个教程输入（另含 FireCAE 回写对照输入，不含大型求解结果）。

`tools/package_portable.ps1` 会：

1. 校验所有必需运行时；
2. 在输出根目录内创建受约束的暂存目录；
3. 计算每个文件的 SHA-256 清单；
4. 将 `PATH` 临时缩减为 Windows 系统目录，并运行 `FireCAE.exe --startup-smoke`；
5. 只有启动成功后才生成 ZIP。

本次隔离启动测试退出码为 0。该路径初始化了 QApplication、主窗口、Qt 插件、OpenCascade 视图、设置、恢复管理器及内置运行时检测。

## 3. 自动化验收结果

当前源码已在 `build\gui-e2e`、`build\debug` 和 `build\release` 完整构建。最新完整 CTest 基线：

```text
GUI E2E: 13/13 PASS, 209.70 s
Debug:    13/13 PASS, 207.58 s
Release:  13/13 PASS, 112.01 s
```

覆盖：

- 核心对象模型、保存/重开、恢复快照、设置往返、资源打包/解包/复制；
- 常规 GUI、A10 工作流、A12 场景、A13 原生结果和缓存渲染；
- A14 几何导入；
- A15 启动页、设置、资源管理和诊断；
- FDS 模型、场景和真实 FDS Runner。

`D:\FireCAE\build\debug\FireCAE.exe`、`D:\FireCAE\build\release\FireCAE.exe` 和 `D:\FireCAE\build\gui-e2e\FireCAE.exe` 均已使用相同源码完成最终链接。PDF 分页修复后，A16 专项在三套构建中再次 3/3 PASS。

## 4. 发布边界

- 当前交付形式是免安装 ZIP 便携包，不是带数字签名的 MSI/EXE 安装器。
- 首次运行若系统缺少 VC++ 运行库，可执行包内 `vc_redist.x64.exe`。
- FireCAE 自身最终商业/开源许可证尚需项目所有者在公开发行前确定；第三方许可证已独立随包提供。
- 没有包含 PyroSim 代码，也没有包含商业 CAD SDK。
