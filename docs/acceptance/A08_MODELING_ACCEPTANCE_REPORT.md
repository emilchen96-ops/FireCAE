# FireCAE A08 建模交互验收与复现报告

报告日期：2026-08-29  
验收程序：`D:\FireCAE\build\debug\FireCAE.exe`  
范围：原生几何创建、对象选择、精确变换、复制与阵列、分组与楼层、Undo/Redo、显示控制、单位、捕捉和工程持久化。

> A08 已完成最终验收。OpenCascade 三维变换操纵器启用状态下连续执行 10 轮完整 GUI 测试均通过；`debug` 与 `gui-e2e` 两套构建随后分别完成 5/5 正式测试，未出现 Debug Runtime Error、堆损坏、异常退出或新增残留 GUI 进程。

## 一、从空工程开始

1. 启动 `D:\FireCAE\build\debug\FireCAE.exe`。
2. 点击 `文件 → 新建`，创建空工程。
3. 确认左侧模型树、中央三维视图、属性面板和底部消息窗口正常显示。
4. 如果三维模型超出视口，点击工具栏“全部适配”。

## 二、创建原生几何

1. 点击 `几何 → 创建方块`。
2. 在对话框中填写名称、原点和 X/Y/Z 尺寸，确认后创建第一个方块。
3. 重复操作创建第二个方块，并给它设置不同的位置和尺寸。
4. 展开模型树的“几何”节点；两个方块应分别拥有独立 UUID，并可在属性面板查看身份、状态和组织信息。

预期界面：

![创建两个原生方块](screenshots/A08-01-created-boxes.png)

## 三、树与三维多选联动

1. 单击模型树中的第一个方块，三维对象应高亮。
2. 按住 `Ctrl` 再选择第二个方块，两个对象应同时进入选择集。
3. 也可以在三维视图中按住鼠标左键拖出矩形框进行框选。
4. 按 `Esc` 清空选择。
5. 树节点与三维显示必须通过对象 UUID 联动，不能使用名称作为唯一关联键。

![多对象选择](screenshots/A08-02-multi-selection.png)

## 四、三维操纵器与精确变换

1. 选择一个方块。
2. 点击工具栏“变换操纵器”，显示移动、旋转、缩放手柄。
3. 拖动红、绿、蓝轴可沿对应轴移动；旋转环和缩放手柄分别用于旋转与缩放。
4. 如需精确输入，点击 `几何 → 变换选中对象`。
5. 在对话框中输入平移、旋转和缩放参数；可选择对象中心或用户指定枢轴。
6. 开启角度捕捉时，旋转值按设置的角度步长吸附；例如输入 Z 轴旋转 17°，步长为 15° 时应得到 15°。

![三维变换操纵器](screenshots/A08-03-transform-gizmo.png)

![精确移动、旋转与捕捉](screenshots/A08-03-moved-rotated-snapped.png)

## 五、捕捉设置

1. 点击 `几何 → 捕捉设置`。
2. 可分别开启世界网格、FDS 网格、顶点、边中点、边、面、对象中心、正交和角度捕捉。
3. 设置长度步长和角度步长。
4. 按 `F9` 可快速启用或停用捕捉；状态栏持续显示捕捉状态和已启用模式。
5. 临时停用捕捉只影响当前操作，不应修改保存的捕捉配置。
6. 操纵器拖动结束时，原始 OCCT 变换会提交给 `SnapManager`；系统根据当前设置执行正交、网格、顶点、边中点、边、面、对象中心和角度捕捉，再把最终结果写回 UUID 对应的业务几何，状态栏显示捕捉目标、坐标和角度。

## 六、复制、阵列、分组、楼层和层级移动

1. 选择对象后执行 `几何 → 复制并移动`，输入偏移量生成独立 UUID 的副本。
2. 执行 `几何 → 阵列复制`，设置 X/Y/Z 方向数量和间距，生成规则阵列。
3. 在模型树或“几何”菜单创建文件夹或楼层。
4. 将对象拖入文件夹或楼层，层级和父子 UUID 关系应立即更新。
5. 对多个对象执行“分组”后，可整体选择、隐藏、锁定或移动。
6. 批量重命名只修改显示名称，不改变 UUID。

![复制、分组与撤销重做](screenshots/A08-04-copy-group-undo-redo.png)

## 七、Undo/Redo 与显示控制

以下操作应进入统一命令栈，可通过 `Ctrl+Z` / `Ctrl+Y` 撤销和重做：

- 创建、删除对象；
- 创建、导入、复制、排序和删除 FDS/IFC 业务对象；
- 移动、旋转和缩放；
- 复制、阵列、分组和树层级移动；
- 隐藏、显示、仅显示选中对象、锁定；
- 批量重命名；
- 项目设置和 FDS 对象属性编辑。

锁定对象不可被三维交互误移动，也不能通过菜单、双击、快捷操作或底层业务入口被编辑、复制、排序、重分组或删除；锁定文件夹/楼层后，该锁定会约束其子对象。隐藏对象不参与普通选择。右键菜单提供隐藏、显示和仅显示选中对象。

## 八、单位、视图和裁剪

1. 在项目设置中切换显示单位：m、cm、mm、ft、in。
2. 内部数据始终以 SI 单位保存，切换显示单位不应改变模型真实尺寸。
3. 可切换透视/正交投影以及前、后、左、右、顶、底视图。
4. 可保存和恢复视图，设置背景颜色，开启裁剪面，并显示世界坐标轴。
5. 可设置当前旋转中心，使后续轨迹球旋转围绕目标位置进行。

## 九、保存与重新打开

1. 点击 `文件 → 保存`，保存为 `.firecae` 工程。
2. 关闭当前工程并重新打开该文件。
3. 检查几何 BREP、对象 UUID、名称、父子层级、楼层、标签、隐藏和锁定状态、单位设置是否保持。
4. 选择树节点，确认仍能通过 UUID 找回并高亮正确业务对象。

![保存并重新打开](screenshots/A08-05-saved-reopened.png)

## 十、自动化验证记录

2026-08-28 已在 Debug 构建中运行以下不启动窗口的测试：

```powershell
ctest --test-dir D:\FireCAE\build\debug -C Debug `
  -R '^(FireCAECoreTests|FireCAEFdsModelTests|FireCAEFdsSceneTests|FireCAEFdsRunnerTests)$' `
  --output-on-failure
```

结果：`build\debug` 与 `build\gui-e2e` 两套构建均为 4/4 通过。

| 测试 | 结果 | A08 相关覆盖 |
|---|---:|---|
| FireCAECoreTests | 通过 | 对象树、UUID、单位、几何持久化、SnapManager |
| FireCAEFdsModelTests | 通过 | FDS 对象模型、序列化和生成基础 |
| FireCAEFdsSceneTests | 通过 | FDS 对象到三维场景的映射 |
| FireCAEFdsRunnerTests | 通过 | 求解器状态和进程管理基础 |

同日首次增量更新 `build\gui-e2e` 后，三个后台测试出现堆损坏/段错误；比较两个构建目录后确认 CMake、编译器、Qt 和 OpenCascade 配置一致。全量重建后两个目录均恢复 4/4 通过。

进一步定位发现，CMake 4.1 自动探测到的中文 MSVC `/showIncludes` 前缀发生乱码，Ninja 因而没有记录头文件依赖，公共头文件修改后可能继续链接旧 ABI 对象。`CMakeLists.txt` 现已为中文 MSVC/Ninja 设置正确前缀；重新生成并全量重建后，`ninja -t deps` 显示 `OccViewWidget.cpp.obj` 的 642 项依赖有效，并明确包含 `GeometryDisplayManager.h`。后续公共头文件修改可以可靠触发增量重编译。

最新后台验证还补齐了统一 Undo/Redo 的遗漏入口：创建 FDS 对象、复制和排序 FDS 对象、导入/删除 IFC、关闭结果、右键显示/隐藏与“仅显示选中对象”均进入 `QUndoStack`；混合选择或锁定层级不能再绕过修改保护。

最新构建产物：

- `D:\FireCAE\build\debug\FireCAE.exe`：2026-08-28 23:09:40，4,393,472 字节；
- `D:\FireCAE\build\gui-e2e\FireCAE.exe`：2026-08-28 23:09:43，4,393,472 字节。

此前已完成 AddressSanitizer 场景运行以及跳过操纵器的五轮 UI 重复测试。定位结果显示，间歇性堆损坏集中在 `AIS_Manipulator` 的挂接/解绑生命周期；最新补丁增加了停止变换、清除检测/选择、停用模式和解绑的完整顺序，并避免 `Attach` 与显式模式启用造成重复敏感实体；显示管理器析构时也会主动解绑操纵器。

上述顺序已对照 [OpenCascade 7.8 `AIS_Manipulator` 官方接口文档](https://dev.opencascade.org/doc/occt-7.8.0/refman/html/classAIS__Manipulator.html)：鼠标变换链为 `StartTransform` / `Transform` / `StopTransform`，结束后通过 `DeactivateCurrentMode` 重置活动模式，最后使用 `Detach` 从业务对象和上下文解绑。

## 十一、最终 GUI 稳定性验收

2026-08-29 00:31 至 00:39，在用户授权桌面操作后执行：

1. 三维操纵器保持启用，`FireCAEUiTests.exe` 连续运行 10 轮：10/10 通过。
2. 每轮独立保存 6 张截图到 `screenshots/A08-stability/run-01` 至 `run-10`，共 60 张。
3. `build/debug` 完整测试：5/5 通过，UI 场景耗时 16.46 s。
4. `build/gui-e2e` 完整测试：5/5 通过，UI 场景耗时 17.69 s。
5. 两套最终证据分别保存到 `screenshots/A08-final-debug` 与 `screenshots/A08-final-gui-e2e`。
6. 十轮重复测试及两套最终测试后均未产生新的 FireCAE/GUI 测试残留进程。

最终构建产物：

- `D:\FireCAE\build\debug\FireCAE.exe`：4,540,416 字节；
- `D:\FireCAE\build\gui-e2e\FireCAE.exe`：4,540,416 字节。

## 十二、A08 最终结论

**A08 最终验收通过，可以进入 A09。**

- 多选、框选、树与三维 UUID 联动通过；
- 操纵器、精确变换、捕捉、复制、阵列、分组和 Undo/Redo 通过；
- 锁定层级保护、显示控制、保存重开和 IFC 删除撤销通过；
- Debug 与 GUI E2E 两套构建均稳定通过全部测试。

## 十三、A16 收口阶段的当前版本复验

完成 A09–A16 集成后，又对当前 `FireCAEUiTests.exe` 连续执行 10 个完整回合：10/10 退出码为 0，每轮保存 6 张截图，共 60 张，位于 `screenshots/A16-final-stability/run-01` 至 `run-10`。

当前完整测试基线为：GUI E2E 13/13、Debug 13/13、Release 13/13。该轮复验覆盖复杂 IFC（约 62,391 个模型树节点）和七教程入口，未出现新的堆损坏、CRT Debug Error 或异常退出。
