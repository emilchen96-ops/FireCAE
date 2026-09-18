# r13 源码恢复与备份说明

## 备份范围

2026-09-18 将最新 r13 源码独立纳入 Git，目标仓库为 https://github.com/emilchen96-ops/FireCAE 。应用实现和测试代码不因此次备份而修改。

- `src/`：Qt 界面、对象模型、几何、FDS、求解与 Smokeview 集成。
- `CMakeLists.txt`、`resources/`、`tools/`、`tests/` 顶层源文件：构建、资源、工具和回归测试。
- `tests/data/fds-tutorials/<名称>/<名称>.fds`：七个教程参考输入；另保留已有小型 FDS 输入，以及 `fds-results` 顶层 demo/missing 测试夹具。
- `docs/`：已有教程和历史报告；r13 修复记录另存于 `docs/acceptance/reacceptance-2026-09-15/`。
- `licenses/`、IfcOpenShell 许可说明、`resources/smokeview-r13.ini`：已有第三方说明和 r13 交付包的显示默认配置（原始字节副本）。

不备份可重建的编译产物、第三方 SDK/EXE/DLL、安装包、大型 FDS 场结果、截图缓存、运行日志、用户偏好、会话恢复文件或登录凭据。本地这些文件未删除。GitHub 中的代码备份不能代替用户 `.firecae` 工程与外部结果文件的独立备份。

已有外部 IFC 示例的再分发来源/许可清单尚未完整核实，因此也不随本次公开提交上传；本地原文件保留。IFC 测试所需文件为 `tests/data/tessellated-item.ifc`、`tests/data/PCERT_Infra_Landscaping_IFC4.ifc`、`tests/data/Clinic_Architectural_IFC2x3.ifc`，迁移机器时须从合法来源或自有备份补齐。

本次基线对应的 Windows 程序 SHA-256 为：

```text
09AFB33F4BDD760424ACAB71E74F4197DBA625A6C9D43D70D271C42F5AEB6229
```

这是已有 r13 程序的指纹，不是承诺在不同机器上编译得到逐字节相同的 EXE。

## 从 GitHub 恢复

在一个新的空目录中执行（不要覆盖已有未提交工作）：

```powershell
git clone https://github.com/emilchen96-ops/FireCAE.git
cd FireCAE
git log -1 --oneline
git status --short
```

仓库根目录就是 CMake 源码目录，不需要原机器的多层 `work` 目录。不要把它与旧版 `D:\FireCAE` 源码混合。

## Windows 构建依赖

已验证基线使用 Windows x64、MSVC x64、CMake/Ninja、Qt 6.11.1、OpenCascade 7.8.0。Qt 必须含 Core、Widgets、PrintSupport、Concurrent；OCCT 需与 MSVC/架构兼容，并备齐对应第三方 DLL。

准备合法取得的独立运行时：

1. IfcOpenShell IfcConvert。原包来源与版本见 `third_party/ifcopenshell/NOTICE.md`。Windows CMake 配置强制检查 `IfcConvert.exe`，且同目录必须有 `COPYING`、`COPYING.LESSER`、`NOTICE.md`。
2. FDS：用于真实求解。原基线为 FDS 6.11.1；MPI/OpenMP 要匹配其运行时。`FIRECAE_FDS_RUNTIME_DIR` 指向含 `fds.exe`、`fds_openmp.exe`、`libiomp5md.dll` 和 `mpi` 子目录的发行版 `bin` 目录。
3. Smokeview：用于正式场结果显示。`FIRECAE_SMOKEVIEW_RUNTIME_DIR` 指向包含 `smokeview.exe` 的完整运行目录。保留配套资源；不能只复制单个 EXE。

也可按 CMake 默认路径将运行时放入 `third_party`，这些二进制由 `.gitignore` 排除。不要以本仓库中的少量许可证文件误认为运行时已齐全。

在已初始化 MSVC x64 的命令行中设置下列环境变量为本机实际安装路径，再运行 PowerShell 命令：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:FIRECAE_QT_SDK" `
  -DOpenCASCADE_DIR="$env:FIRECAE_OCCT_CMAKE_DIR" `
  -DFIRECAE_OCCT_3RDPARTY_ROOT="$env:FIRECAE_OCCT_THIRDPARTY_DIR" `
  -DFIRECAE_IFCCONVERT_EXECUTABLE="$env:FIRECAE_IFCCONVERT_EXE" `
  -DFIRECAE_FDS_RUNTIME_DIR="$env:FIRECAE_FDS_BIN" `
  -DFIRECAE_SMOKEVIEW_RUNTIME_DIR="$env:FIRECAE_SMOKEVIEW_DIR"
cmake --build build --parallel 4
ctest --test-dir build -N
```

配置成功不等于全部测试通过。与原包不同的 Qt/OCCT/FDS/Smokeview 组合需要重新验证。`tools/package_portable.ps1` 打包时还会从源码默认 `third_party/ifcopenshell` 读取许可证文件，仓库已保留这些文件。

恢复 r13 的 Smokeview 字体/色标默认值时，在安装好 Smokeview 并完成构建后，将 `resources/smokeview-r13.ini` 复制为构建/交付目录的 `smokeview/smokeview.ini`。先备份目标已有配置；工程自己的 INI 仍有优先权。本次没有改动 CMake 的运行时复制行为，后续重新部署外部运行时可能覆盖该文件。便携打包还需 `vc_redist.x64.exe` 以及 Qt/OCCT 许可目录；源码可编译与完整安装包可交付是两个不同条件。

## 测试与验收边界

- 基础代码和输入比较所需的小型 fixture 已保留；`tests/data/fds-results/demo_0001.sf` 是刻意保留的 46 字节合成夹具，不是真实完整切片结果。
- Core 中的 IFC 转换分支以及 IFC/UI 专项仍需要上面列出的外部 IFC 样本；未补齐样本时不能宣称整组 Core/UI 测试通过。
- A13 渲染缓存、真实场结果播放和部分 GUI 后处理专项依赖完整 `gui-generated/couch_smoke_12s` 等结果。需重新用对应 FDS 输入求解生成，或从另行保存的结果备份恢复；仅克隆本仓库不能运行所有后处理专项。
- `gui-generated/tunnel_smoke_10s/tunnel_smoke_10s.smv` 用于已有元数据分类测试；没有配套场数据时不能据此声明 Smokeview 可完整播放。
- FDS/MPI/IfcConvert/Smokeview 相关测试需安装相应运行时；GUI 测试还需要正确的平台插件和显示环境。
- 历史文档保留了当时状态及本机路径。缺失的图片、日志或外部证据链接不能视为通过证据。本次不重跑耗时仿真或宣称新增 GUI 验收。
- r13 仍有完整人工验收、教学视频、PyroSim 授权对照和第二台 Windows 验证等未完事项，见 r13 记录。

## 后续防丢流程

每完成一组代码修改，在此源码仓库目录中检查、提交并推送：

```powershell
git status --short
git diff
# 检查无敏感信息或大文件后，添加本次实际修改的文件：
git add src/具体文件.cpp
git commit -m "说明本次修改"
git push
git status -sb
```

只有 push 成功的提交才已存到 GitHub；本地 commit、未提交文件和聊天记录不等于远程备份。不要使用强制推送覆盖历史。当前没有建立定时自动备份任务。

第三方输入及运行时保留各自许可，FireCAE 自有源代码许可证仍待项目所有者决定，本次不新增授权条款。
