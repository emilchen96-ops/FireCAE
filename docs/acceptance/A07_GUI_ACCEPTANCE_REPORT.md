# FireCAE A07 GUI 验收与复现报告

验收日期：2026-08-28  
验收程序：`D:\FireCAE\build\debug\FireCAE.exe`  
范围：FDS 三维前处理场景、模型树联动、显示控制与七教程可视化。

> 本报告只验收 A07。这里的“教程跑通”是指教程对象由 FireCAE 业务对象树生成，并在主界面三维视图中正确显示。完整的“从零建模 → 生成 FDS → 求解 → 后处理”将在 A08–A16 完成后另行进行端到端验收，不能用本报告替代。

## 一、公共操作步骤

1. 启动 `D:\FireCAE\build\debug\FireCAE.exe`。
2. 确认主界面中央为灰色 OpenCascade 三维视图，左侧存在模型树，底部存在消息窗口。
3. 依次点击菜单 `模型 → 教程`。
4. 在二级菜单中选择需要验收的教程。
5. 等待窗口标题改变，并确认消息窗口出现：
   - `3D FDS scene synchronized: ...`
   - `Tutorial created from editable FireCAE objects: ...`
6. 点击工具栏“全部适配”，可重新把模型完整放入当前视口。
7. 在模型树中展开对象分组并选择对象，可在属性面板中查看业务属性；三维选择和树选择均按对象 UUID 关联。
8. 对对象或分组点击右键，可使用“隐藏 / 显示 / 仅显示此对象”检查显示控制。

教程入口如下图：

![七教程菜单](screenshots/A07-00-tutorial-menu.png)

## 二、七个教程逐项复现

### 1. activate_vents

菜单路径：`模型 → 教程 → 创建 activate_vents 教程`

预期结果：

- 项目标题为 `Test of VENT activation/deactivation`。
- 三维窗口显示青色计算域和沿底面排列的多个彩色 VENT。
- 消息窗口显示 `19 business objects, 19 presentations`。
- 工程对象总数为 44。

![activate_vents](screenshots/A07-01-activate_vents.png)

### 2. bucket_test_2

菜单路径：`模型 → 教程 → 创建 bucket_test_2 教程`

预期结果：

- 项目标题为 `Customized sprinkler test case`。
- 三维窗口显示计算域、上下设备点和内部边界面。
- 消息窗口显示 `7 business objects, 7 presentations`。
- 工程对象总数为 13。

![bucket_test_2](screenshots/A07-02-bucket_test_2.png)

### 3. couch

菜单路径：`模型 → 教程 → 创建 couch 教程`

预期结果：

- 项目标题为 `Single Couch Test Case`。
- 三维窗口显示多网格线框、沙发组合障碍物和开口面。
- 同一 MULT 业务对象可生成多个三维显示实例。
- 消息窗口显示 `10 business objects, 17 presentations`。
- 工程对象总数为 26。

![couch](screenshots/A07-03-couch.png)

### 4. couch_smoke_12s

菜单路径：`模型 → 教程 → 创建 couch_smoke_12s 教程`

预期结果：

- 项目标题为 `Single Couch Tutorial - 12 s FireCAE Smoke Test`。
- 前处理几何与 couch 一致，并包含短时烟气输出对象。
- 消息窗口显示 `10 business objects, 17 presentations`。
- 工程对象总数为 26。

![couch_smoke_12s](screenshots/A07-04-couch_smoke_12s.png)

### 5. HVAC_aircoil

菜单路径：`模型 → 教程 → 创建 HVAC_aircoil 教程`

预期结果：

- 项目标题为 `Test of aircoil`。
- 三维窗口显示计算域、内部水平面、HVAC 节点/连接和顶面对象。
- 消息窗口显示 `15 business objects, 15 presentations`。
- 工程对象总数为 20。

![HVAC_aircoil](screenshots/A07-05-HVAC_aircoil.png)

### 6. tunnel_demo

菜单路径：`模型 → 教程 → 创建 tunnel_demo 教程`

预期结果：

- 项目标题为 `Example of a tunnel simulation`。
- 三维窗口显示连续多段隧道网格、中央红色火源面和端部开口。
- MULT 网格展开后，一个业务 UUID 对应多个显示实例。
- 消息窗口显示 `5 business objects, 12 presentations`。
- 工程对象总数为 11。

![tunnel_demo](screenshots/A07-06-tunnel_demo.png)

### 7. tunnel_smoke_10s

菜单路径：`模型 → 教程 → 创建 tunnel_smoke_10s 教程`

预期结果：

- 项目标题为 `Tunnel Demo - 10 s FireCAE Smoke Test`。
- 前处理几何与 tunnel_demo 一致，并包含短时烟气输出对象。
- 消息窗口显示 `5 business objects, 12 presentations`。
- 工程对象总数为 11。

![tunnel_smoke_10s](screenshots/A07-07-tunnel_smoke_10s.png)

## 三、自动化验收结果

最终 Debug 构建执行：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir 'D:\FireCAE\build\debug' --output-on-failure
```

结果：5/5 通过。

| 测试 | 结果 | 覆盖内容 |
|---|---:|---|
| FireCAECoreTests | 通过 | FcProject/FcDocument/FcObject、导入与结果基础对象 |
| FireCAEUiTests | 通过 | 主窗口、UUID 树映射、教程切换、选择/隐藏/删除、稳定退出 |
| FireCAEFdsModelTests | 通过 | FDS 模型、序列化、导入与生成 |
| FireCAEFdsSceneTests | 通过 | 七教程场景、MULT、颜色、边界、可见性 |
| FireCAEFdsRunnerTests | 通过 | FDS 求解器启动、状态和停止路径 |

## 四、验收中发现并处理的问题

日常 `build\debug` 曾出现新旧静态库混用：结果对象树 UUID 正确，但属性面板把 `FDS Result Case` 错读为 `Reaction`。隔离构建正常，说明源代码无误而构建产物 ABI 不一致。执行完整干净重建后问题消失，最终 Debug 测试 5/5 通过。

如果今后修改 `FcObjectType` 等公共枚举或核心头文件后出现类型错位，应执行：

```powershell
cmake --build D:\FireCAE\build\debug --clean-first
```

## 五、A07 验收结论

- 七教程都可从主界面菜单创建。
- 三维场景不再为空，MESH、OBST、VENT、DEVC、HVAC、INIT、SLCF 等对象可视化已接通。
- 模型树和三维显示通过 FireCAE UUID 关联；同一业务对象支持多个三维显示实例。
- 增量显示控制不会清除已导入的 IFC/原生几何。
- A07 验收通过；求解和完整后处理的人工端到端验收留待 A08–A16 完成后执行。
