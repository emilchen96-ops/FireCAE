# FireCAE A09 建筑几何与 FDS 离散化验收报告

报告日期：2026-08-29  
验收程序：`D:\FireCAE\build\debug\FireCAE.exe`、`D:\FireCAE\build\gui-e2e\FireCAE.exe`  
范围：建筑构件、开口、表面引用、几何运算、背景图、FDS 网格对齐预览与转换。

## 一、完成内容

- 建筑对象：墙、板、屋面、柱、梁、房间、楼梯、坡道。
- 轮廓对象：多边形、折线扫掠、圆/圆柱、矩形轮廓、轮廓拉伸和路径扫掠。
- 开口对象：矩形/多边形开口、门、窗、楼板洞口和穿墙通风口；开口保存宿主 UUID，可选 `CTRL` UUID 实现动态控制。
- 墙体支持左、中心、右三种基准线；数值对话框使用工程单位。
- 新增“在视图中绘制墙体”：自动进入顶视图，两点拾取、半透明实时预览、网格捕捉、Shift 正交约束、Esc 取消；完成后恢复等轴测视图。
- 支持几何并集、差集、交集、分割和修复；创建前后执行 OCCT 有效性、闭合、退化边和非流形检查。
- 每个几何对象保存默认表面 UUID 以及 X-/X+/Y-/Y+/Z-/Z+ 六面表面 UUID；FDS 写出器生成 `SURF_ID` 或 `SURF_ID6`。
- 建筑几何独立转换为 FDS 对象，源对象保持不变：实体生成 `OBST`，门窗/洞口生成 `HOLE`，穿墙通风口生成 `VENT`；任意几何采用网格栅格化路线。
- “预览 FDS 块”同时显示 Requested XB、按网格捕捉后的 Actual XB、体积误差、薄构件丢失警告，并可通过源 UUID 定位对象。
- 背景图支持 PNG/JPG/BMP/GIF、XY/XZ/YZ 平面、标定尺寸、透明度、楼层、锁定以及资源嵌入。

## 二、从空工程复现两层建筑

1. 使用 `几何 → 创建墙体/创建房间/建筑构件/开口` 创建外墙、首层房间、首层板、二层房间、二层板、楼梯、门、窗和通风口。
2. 使用 `几何 → 在视图中绘制墙体`，设置墙厚和高度后在顶视图拾取两个端点。
3. 使用 `模型 → 网格`，设置原点、长度和单元数。本次验收网格为 `IJK=48,40,32`、`XB=0,12,0,10,0,8`。
4. 点击 `几何 → 预览 FDS 块` 检查 Requested/Actual 尺寸和网格离散误差。
5. 点击 `几何 → 生成 FDS 块`，确认模型树新增 `OBST`、`HOLE` 和 `VENT`，且建筑源对象仍存在。
6. 导出 `.fds`，保存 `.firecae`，关闭并重新打开工程，确认 UUID、参数、宿主/控制/表面引用和 BREP 几何保持。

源建筑界面：

![A09 两层建筑源模型](screenshots/A09-building/A09-01-building-source.png)

FDS 离散化预览：

![A09 FDS 块预览](screenshots/A09-building/A09-02-fds-block-preview.png)

保存并重新打开：

![A09 保存重开](screenshots/A09-building/A09-03-saved-reopened.png)

## 三、自动验收覆盖

专项命令：

```powershell
$env:FIRECAE_ACCEPTANCE_SCREENSHOT_DIR='D:\FireCAE\docs\acceptance\screenshots\A09-building'
D:\FireCAE\build\gui-e2e\FireCAEUiTests.exe --a09-building-smoke
```

专项验收从空工程通过真实菜单和模态对话框完成以下操作：

- 检查全部 A09 菜单入口；
- 创建 9 个数值建筑对象；
- 通过真实鼠标事件完成一面墙的两点拾取和预览；
- 创建网格、显示离散预览、生成 FDS 对象；
- 导出并检查 `MESH/OBST/HOLE/VENT` 记录；
- 保存项目并重新打开，核对对象数量和引用持久化；
- 自动保存三张验收截图。

## 四、回归结果

| 构建目录 | A09 专项 | 正式 CTest |
|---|---:|---:|
| `build\gui-e2e` | 通过 | 5/5 通过 |
| `build\debug` | 通过 | 5/5 通过 |

两套构建均未出现堆损坏、Debug Runtime Error 或异常退出。

## 五、技术边界

- A09 基线默认采用可控的 FDS 栅格化 `OBST` 路线；R01 扩展已增加按对象选择 `GEOM/OBST/Auto`，原生 `GEOM` 使用真实 `VERTS/FACES` 三角剖分，并已由随软件部署的 FDS 6.11.1 实算通过。详见 `R01_PROFESSIONAL_BUILDING_REPORT.md`。
- 建筑源几何与求解离散对象保持双层数据结构；预览/生成不会覆盖 CAD/IFC 源对象。
- 直接视图绘制首先覆盖高频墙体工作流，其余对象均可通过工程单位数值对话框精确创建和编辑。

## 六、结论

**A09 验收通过，可以进入 A10。**

FireCAE 已具备从建筑构件到 FDS `OBST/HOLE/VENT` 的可视化建模、UUID 引用、网格离散预览、生成、导出和保存重开闭环。
