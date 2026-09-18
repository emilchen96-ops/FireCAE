# bucket_test_2 — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Customized sprinkler test case`
- CHID：`bucket_test_2`
- 结束时间：`15 s`
- FDS Schema：`6.10`
- 从空工程创建对象数：`13`

## 界面操作顺序

1. 文件 → 新建，保持空工程。
2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。
3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。
4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；关联键为 UUID，不使用对象名称。
5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。

## 对象和参数

### 1. &MESH — Sprinkler test domain

- 分类：Meshes / 网格
- FDS ID：``
- 新工程 UUID：`{c323c40e-0057-4226-91e0-65a579df31bd}`
- 参数：

  - `IJK` = 原始值 `50,50,25`
  - `XB` = 原始值 `-5.0,5.0,-5.0,5.0,0.0,5.0`

### 2. &SPEC — Water vapor

- 分类：Species / 组分
- FDS ID：`WATER VAPOR`
- 新工程 UUID：`{8755937b-06a1-44af-98b0-d6b617a03316}`
- 参数：无

### 3. &PART — Water drops

- 分类：Particles / 粒子
- FDS ID：`water drops`
- 新工程 UUID：`{a9d266ca-460b-48ee-8dc7-be23e6219db1}`
- 参数：

  - `SPEC_ID` = 引用 &SPEC `WATER VAPOR` → UUID `{8755937b-06a1-44af-98b0-d6b617a03316}`
  - `QUANTITIES(1)` = 文本 `PARTICLE DIAMETER`
  - `DIAMETER` = 原始值 `1750.`

### 4. &PROP — K-11 sprinkler property

- 分类：Particles / 粒子
- FDS ID：`K-11`
- 新工程 UUID：`{5aac57b7-e115-467d-883e-7155358bd1a5}`
- 参数：

  - `QUANTITY` = 文本 `SPRINKLER LINK TEMPERATURE`
  - `PARTICLE_VELOCITY` = 原始值 `5.`
  - `PART_ID` = 引用 &PART `water drops` → UUID `{a9d266ca-460b-48ee-8dc7-be23e6219db1}`
  - `FLOW_RATE` = 原始值 `60.`
  - `SPRAY_PATTERN_TABLE` = 引用 &TABL `TABLE1` → UUID `{4a9ea315-5552-4e45-bf69-716345965b82}`
  - `SMOKEVIEW_ID` = 文本 `sprinkler_upright`
  - `PARTICLES_PER_SECOND` = 原始值 `10000`

### 5. &TABL — TABLE1 lower hemisphere

- 分类：Controls / 控制
- FDS ID：`TABLE1`
- 新工程 UUID：`{4a9ea315-5552-4e45-bf69-716345965b82}`
- 参数：

  - `TABLE_DATA` = 原始值 `30,31,  0,  1,5,0.2`

### 6. &TABL — TABLE1 upper hemisphere

- 分类：Controls / 控制
- FDS ID：`TABLE1`
- 新工程 UUID：`{c4d46fcf-5b6a-4e80-a5e1-5e16d5e8b37f}`
- 参数：

  - `TABLE_DATA` = 原始值 `30,31,179,180,5,0.8`

### 7. &DEVC — Spr_1

- 分类：Devices / 设备
- FDS ID：`Spr_1`
- 新工程 UUID：`{29196038-4041-464f-ae2d-fbccba622785}`
- 参数：

  - `XYZ` = 原始值 `0.0,0.0,4.9`
  - `PROP_ID` = 引用 &PROP `K-11` → UUID `{5aac57b7-e115-467d-883e-7155358bd1a5}`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `5.`
  - `INITIAL_STATE` = 原始值 `.TRUE.`

### 8. &VENT — Open XMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{e4b39b2f-f83c-4803-ab03-0abf706a6a2f}`
- 参数：

  - `MB` = 文本 `XMIN`
  - `SURF_ID` = 文本 `OPEN`

### 9. &VENT — Open XMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{3f5e68e0-c510-4a2a-b6cf-2cfec5105d55}`
- 参数：

  - `MB` = 文本 `XMAX`
  - `SURF_ID` = 文本 `OPEN`

### 10. &VENT — Open YMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{b8d9c112-ff60-4c0a-91b3-6e08ce989713}`
- 参数：

  - `MB` = 文本 `YMIN`
  - `SURF_ID` = 文本 `OPEN`

### 11. &VENT — Open YMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{5e63f0b2-a250-46b1-ae1d-2c6a19f75d59}`
- 参数：

  - `MB` = 文本 `YMAX`
  - `SURF_ID` = 文本 `OPEN`

### 12. &DEVC — Accumulated water mass

- 分类：Devices / 设备
- FDS ID：`Mass`
- 新工程 UUID：`{5a7650fd-caf7-4134-8e11-18298455c8f7}`
- 参数：

  - `QUANTITY` = 文本 `AMPUA`
  - `PART_ID` = 引用 &PART `water drops` → UUID `{a9d266ca-460b-48ee-8dc7-be23e6219db1}`
  - `SPATIAL_STATISTIC` = 文本 `SURFACE INTEGRAL`
  - `XB` = 原始值 `-5,5,-5,5,0,0`

### 13. &BNDF — Accumulated mass per unit area

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{d169b05f-0b20-4db9-bf2d-d02eab85d677}`
- 参数：

  - `QUANTITY` = 文本 `AMPUA`
  - `PART_ID` = 引用 &PART `water drops` → UUID `{a9d266ca-460b-48ee-8dc7-be23e6219db1}`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\bucket_test_2\bucket_test_2.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\bucket_test_2\bucket_test_2.fds`
- FDS 输入语义对比：通过。
