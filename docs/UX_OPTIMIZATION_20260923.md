# FireCAE UX 优化 — 2026-09-23

构建标识：`UX-BCD.20260923`。本轮包含 A（布局）、B（场景交互）、C（属性与工具）、D（直接编辑）。

## 功能变更

- 导入向导自适应布局；默认收起任务/诊断面板并提供紧凑运行状态；模型树缩进、调宽、滚动和布局持久化。
- IFC 构件/面级 RGBA、颜色来源与类型回退配色；特征轮廓抑制共面三角网格内边，高亮后恢复外观。
- 三维单选/多选/空白右键菜单：隐藏、隔离、退出隔离、显示全部、定位、适配及适用编辑操作。隐藏不改变求解转换范围。
- 树和三维双击/右键共用属性入口；常规、几何、表面、高级四页；锁定对象仍可切页查看，IFC/非参数化 CAD 提供只读查看。
- XYZ 平面轮廓、法向/指定方向拉伸、顶点表格行操作、实际面级表面；六项经过校验的 OBST 附加字段；建模工具栏与轴对齐宿主直墙开孔。
- 支持方块、直墙、板的单侧尺寸手柄，以及支持的拉伸体 U/V 顶点和挤出距离编辑；预览、捕捉、精确输入、Esc 取消和单步撤销。
- 属性/直接编辑同步更新几何、参数、宿主依赖和已有派生 FDS 记录；适用的整体变换与复制保持参数一致。

## 导出与数据保护

- 不覆盖手改、来源不明或结构已变的派生记录；不猜测拓扑变化后的旧表面归属。
- OBST 六向与 GEOM 实际面赋值按转换路线验证；部分面赋值需默认表面，防止赋值被丢弃或扩展到其他面。
- 锁定/越界开孔、未知依赖、参数与形状不一致、未明确功率保持规则的火源面积变化均阻止提交。
- 精确网格对齐边界不再因显示容差额外扩大一层计算网格；真正离网格仍按原包络策略吸附。
- 方块共享顶点按唯一拓扑点计数为 8，避免虚假的重复顶点报告。

## 验证与复现

- Release 构建成功。19 项针对性 CTest 各连续执行 3 次，57 次全部通过；最终一次用时 42.87 秒。
- 中文静态检查覆盖 24 个界面文件、1705 处调用、1677 条词典项，无静态缺词、重复键或占位符不匹配。
- 四页属性可编辑/只读共 8 张离屏截图经过检查；不代表原生桌面 GUI 验收。
- `FireCAEUxSolverCase` 使用生产编辑、转换与导出代码生成“墙高 2 m 改为 2.2 m + 穿墙矩形开孔”算例。真实 FDS 6.11.1 单线程推进至 1 秒、33 步，退出码 0，并生成 Smokeview 数据。最终构建生成的 FDS 输入与实际求解输入哈希一致。
- FDS 输入 SHA256：`BF9ABD8C578C2B00588C120DCACE5DE68F567B70D3D595DE33A9475144CACC28`。
- 已测试应用 SHA256：`DC8DAB16146CC6DCE8A6F1CED006122AEAEC10134562D472347B7C8D32FC7E97`。重新构建的二进制不保证相同哈希。

按仓库 README 配置 Qt/OpenCascade 等依赖后，可设置 CMake 的 `FIRECAE_BUILD_LABEL` 和 `FIRECAE_APPLICATION_OUTPUT_DIRECTORY` 隔离构建。构建完成后运行：

```powershell
ctest --test-dir <build> --output-on-failure --repeat until-fail:3 -R "^(FireCAECoreTests|FireCAEFdsModelTests|FireCAEFdsSceneTests|FireCAEReacceptanceModelTests|FireCAEUx.*|FireCAEGeometryImportLayoutTests_.*|FireCAEUiLanguageTests|FireCAEUiFeedbackTests|FireCAEReacceptanceTaskCenterTests|FireCAEReacceptanceLayoutTutorialTests)$"
```

需保证测试进程可找到依赖 DLL，且 CMake 的离屏 Qt 插件配置与本机 Qt 一致。IFC 外观集成测试还使用 `tests/data/tessellated-item.ifc` 与 `third_party/ifcopenshell/IfcConvert.exe`；按 [SOURCE_BACKUP.md](SOURCE_BACKUP.md) 的既有说明另行准备这些未纳入仓库的外部依赖，不为本次提交上传许可未核实的样例或第三方程序。因此仓库不是无需准备依赖即可运行的自包含发行包。本机绝对路径报告、运行日志、安装备份、截图、求解结果、二进制及绑定本机路径的构建辅助脚本未上传。

## 明确限制

- 待实屏验收：体育馆 IFC 同视角/同可见范围对比、OCCT 手柄命中和拖动、混合 DPI/多屏及 Smokeview 结果播放。
- 暂不支持完整纹理原点、任意 IFC 自由变形、轴对齐直墙之外的可靠宿主开孔、带宿主对象复制、复杂拓扑变化后的自动表面重绑定。
- 旧版偏大包络派生记录可能需显式重新转换，不静默覆盖可能手改的数据。
- 此次短时求解不是物理精度验证；未宣称七教程全流程、十轮 GUI 稳定性、历史全部任务或 PyroSim 全功能对等验收完成。
