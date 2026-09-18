# tunnel_demo — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Example of a tunnel simulation`
- CHID：`tunnel_demo`
- 结束时间：`30 s`
- FDS Schema：`6.10`
- 从空工程创建对象数：`11`

## 界面操作顺序

1. 文件 → 新建，保持空工程。
2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。
3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。
4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；关联键为 UUID，不使用对象名称。
5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。

## 对象和参数

### 1. &MESH — Tunnel mesh

- 分类：Meshes / 网格
- FDS ID：``
- 新工程 UUID：`{c0ac12ef-4b70-48d7-b149-8be9848ed370}`
- 参数：

  - `IJK` = 原始值 `80,20,20`
  - `XB` = 原始值 `0.0,16.0,-2.0,2.0,0.0,4.0`
  - `MULT_ID` = 引用 &MULT `mesh` → UUID `{1c245ec7-199e-499a-aaec-893645d56e36}`

### 2. &MULT — Eight tunnel mesh sections

- 分类：Meshes / 网格
- FDS ID：`mesh`
- 新工程 UUID：`{1c245ec7-199e-499a-aaec-893645d56e36}`
- 参数：

  - `DX` = 原始值 `16.`
  - `I_UPPER` = 原始值 `7`

### 3. &MISC — Sloped gravity vector

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{d9ff3884-ee7c-4778-bd81-f040c8266c7d}`
- 参数：

  - `GVEC` = 原始值 `-1.70,0.0,-9.65`

### 4. &PRES — Tunnel pressure solver

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{2913bd31-37fa-4a32-8248-cea164786a35}`
- 参数：

  - `CHECK_POISSON` = 原始值 `T`
  - `TUNNEL_PRECONDITIONER` = 原始值 `T`

### 5. &DUMP — Velocity error output

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{6057d3f2-8875-4229-a36a-19e86d42789e}`
- 参数：

  - `VELOCITY_ERROR_FILE` = 原始值 `.TRUE.`

### 6. &REAC — Propane reaction

- 分类：Reactions / 反应
- FDS ID：``
- 新工程 UUID：`{78be9f24-48e8-4e44-8c21-dba83a305f74}`
- 参数：

  - `FUEL` = 文本 `PROPANE`
  - `SOOT_YIELD` = 原始值 `0.015`

### 7. &SURF — Tunnel fire

- 分类：Surfaces / 表面
- FDS ID：`fire`
- 新工程 UUID：`{1a1099dd-8762-4e17-9038-36dbb468ae60}`
- 参数：

  - `COLOR` = 文本 `RED`
  - `HRRPUA` = 原始值 `2000.0`

### 8. &VENT — Tunnel fire vent

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{fb27916f-bf5d-46dc-bd2d-d208b4225630}`
- 参数：

  - `SURF_ID` = 引用 &SURF `fire` → UUID `{1a1099dd-8762-4e17-9038-36dbb468ae60}`
  - `XB` = 原始值 `40.,42.,-1.0,1.0,0.0,0.0`
  - `COLOR` = 文本 `RED`

### 9. &VENT — Open tunnel portal

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{9faf8f25-611d-4446-9335-b292f6eb96e3}`
- 参数：

  - `SURF_ID` = 文本 `OPEN`
  - `PBX` = 原始值 `128.0`

### 10. &SLCF — Tunnel temperature vector slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{dd97da93-ab32-4d32-81ab-fc1ed1d683fe}`
- 参数：

  - `QUANTITY` = 文本 `TEMPERATURE`
  - `VECTOR` = 原始值 `.TRUE.`
  - `PBY` = 原始值 `0.`

### 11. &SLCF — Tunnel pressure head slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{633cdb6b-751e-462c-8b4e-8f6e44364056}`
- 参数：

  - `QUANTITY` = 文本 `H`
  - `CELL_CENTERED` = 原始值 `.TRUE.`
  - `PBY` = 原始值 `0.`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\tunnel_demo\tunnel_demo.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\tunnel_demo\tunnel_demo.fds`
- FDS 输入语义对比：通过。
