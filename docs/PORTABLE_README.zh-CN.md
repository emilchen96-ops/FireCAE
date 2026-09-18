# FireCAE Windows 便携包

本包用于 FireCAE 的独立运行与复验。解压整个 ZIP 后，在包根目录启动 **FireCAE.exe**。程序旁的 DLL、插件、translations、fds、smokeview 和 ifcopenshell 目录应保持完整。

## 启动与依赖

适用环境为 Windows x64。Qt、OCCT、FDS、Smokeview 和 IfcConvert 的已配置运行组件随包提供；如系统提示缺少 Microsoft Visual C++ 运行库，可运行包内 `vc_redist.x64.exe`。随包声明见 [licenses/THIRD_PARTY_NOTICES.md](licenses/THIRD_PARTY_NOTICES.md) 及同目录中的许可证文件。

正式建模入口为 **Model 3D（模型 3D）** 和 **FDS Record（FDS 记录）**；场结果通过 **Smokeview** 查看。Plan 2D 和原生 Results 仍保留兼容能力，其隐藏面板专项测试不能代替正式入口验收。

教程输入位于 `examples/fds-tutorials`。建议先复制到独立、可写的工作目录，再修改或计算，保留随包示例原件。建模与示例说明见 [FDS 教程](docs/FDS_TUTORIAL_CASES.md)；历史[界面验收说明](docs/GUI_END_TO_END_ACCEPTANCE.md) 不代表本包已经通过最终验收。

## 工程与结果

当前工程保存格式为 **v5**，新程序可读取 v1–v4 工程；旧程序不能保证读取 v5。首次验证旧工程时应另存副本。旧 Generic 结果节点中已经丢失的路径和类型不能自动恢复，需要明确选择真实 SMV 建立新结果节点。

`.firecae` 文件记录外部结果路径，并不包含整套计算输出。每个任务使用输出根目录下的 `.firecae-runs/<任务 UUID>`，其中包含输入快照、来源清单及实际结果。重复计算时应按任务和完整结果路径区分案例。移动工程或结果目录后，应重新核对引用路径。

## 隔离复验配置

为避免读取已有应用偏好，可从包根目录在 PowerShell 中设置一个独立的绝对配置路径后启动，例如：

```powershell
$env:FIRECAE_SETTINGS_DIRECTORY = Join-Path $env:LOCALAPPDATA 'FireCAE-Acceptance-Profile'
& .\FireCAE.exe
```

计算输入、输出和验收证据也应使用独立可写目录。使用完此会话后可关闭 PowerShell；新窗口不会继承本次设置。

## 验收范围与证据

`--startup-smoke` 仅走正常程序启动、绘制和退出路径，约 1.5 秒后自动结束；它不验证实际 FDS 计算、MPI、IFC 导入、Smokeview 播放或工程保存重开。最终通过状态必须与本包版本和 EXE 哈希对应。

若打包时附加了本轮报告，入口为 [便携包报告索引](docs/acceptance/PORTABLE_REPORT_INDEX.md)。该目录只含文字报告和 CSV 清单；原始日志、截图、工程、计算输出及补丁通过索引中的**外部交付证据目录**提供。若本包没有附加报告目录，则本包不包含最终验收结论，应以交付总索引为准。

未执行项目、已知兼容边界和待复验项应按最终功能矩阵与缺陷台账保留，不能因打包成功或启动成功而改记通过。便携包的 `package-manifest.json` 列出打包负载文件及 SHA-256；ZIP 本身的哈希由外部交付清单记录。
