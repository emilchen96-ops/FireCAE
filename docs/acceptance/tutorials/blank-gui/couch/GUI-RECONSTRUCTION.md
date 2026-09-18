# couch — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Single Couch Test Case`
- CHID：`couch`
- 结束时间：`600 s`
- FDS Schema：`6.10`
- 从空工程创建对象数：`26`

## 界面操作顺序

1. 文件 → 新建，保持空工程。
2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。
3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。
4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；关联键为 UUID，不使用对象名称。
5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。

## 对象和参数

### 1. &MESH — Couch domain mesh

- 分类：Meshes / 网格
- FDS ID：``
- 新工程 UUID：`{6e6b3de9-a549-4013-9185-adcd5f35b9fd}`
- 参数：

  - `IJK` = 原始值 `25,25,12`
  - `XB` = 原始值 `0.0,2.5,0.0,2.5,0.0,1.2`
  - `MULT_ID` = 引用 &MULT `mesh` → UUID `{31a13f2e-f5ac-4a89-abfb-444bb81f3f85}`

### 2. &MULT — 2 x 2 x 2 mesh array

- 分类：Meshes / 网格
- FDS ID：`mesh`
- 新工程 UUID：`{31a13f2e-f5ac-4a89-abfb-444bb81f3f85}`
- 参数：

  - `DX` = 原始值 `2.5`
  - `DY` = 原始值 `2.5`
  - `DZ` = 原始值 `1.2`
  - `I_UPPER` = 原始值 `1`
  - `J_UPPER` = 原始值 `1`
  - `K_UPPER` = 原始值 `1`

### 3. &DUMP — Output intervals

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{e248d48f-40ab-4fa4-885b-dd0974b59f22}`
- 参数：

  - `NFRAMES` = 原始值 `3000`
  - `DT_HRR` = 原始值 `5.`
  - `DT_DEVC` = 原始值 `5.`

### 4. &SPEC — Polyurethane

- 分类：Species / 组分
- FDS ID：`POLYURETHANE`
- 新工程 UUID：`{5a903d8b-99c1-471d-bf90-5ee63500fd7e}`
- 参数：

  - `FORMULA` = 文本 `C6.3H7.1N1.0O2.1`

### 5. &REAC — Polyurethane reaction

- 分类：Reactions / 反应
- FDS ID：``
- 新工程 UUID：`{580e0cb5-e0e6-457c-a95d-bc9806b0a052}`
- 参数：

  - `FUEL` = 引用 &SPEC `POLYURETHANE` → UUID `{5a903d8b-99c1-471d-bf90-5ee63500fd7e}`
  - `SOOT_YIELD` = 原始值 `0.01`
  - `HEAT_OF_COMBUSTION` = 原始值 `22700.`

### 6. &MATL — Fabric

- 分类：Materials / 材料
- FDS ID：`FABRIC`
- 新工程 UUID：`{51ecfc33-9a35-4844-8b2b-ee1800ae092d}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `CONDUCTIVITY` = 原始值 `0.5`
  - `DENSITY` = 原始值 `50.`
  - `NU_SPEC` = 原始值 `1.`
  - `SPEC_ID` = 引用 &SPEC `POLYURETHANE` → UUID `{5a903d8b-99c1-471d-bf90-5ee63500fd7e}`
  - `REFERENCE_TEMPERATURE` = 原始值 `250.`
  - `HEAT_OF_REACTION` = 原始值 `500.`
  - `HEAT_OF_COMBUSTION` = 原始值 `16000.`

### 7. &MATL — Foam

- 分类：Materials / 材料
- FDS ID：`FOAM`
- 新工程 UUID：`{699917e2-d3c2-40d7-8434-7e9470914d0c}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `CONDUCTIVITY` = 原始值 `0.1`
  - `DENSITY` = 原始值 `40.0`
  - `NU_SPEC` = 原始值 `1.`
  - `SPEC_ID` = 引用 &SPEC `POLYURETHANE` → UUID `{5a903d8b-99c1-471d-bf90-5ee63500fd7e}`
  - `REFERENCE_TEMPERATURE` = 原始值 `280.`
  - `HEAT_OF_REACTION` = 原始值 `800.`
  - `HEAT_OF_COMBUSTION` = 原始值 `22700.`

### 8. &MATL — Gypsum plaster

- 分类：Materials / 材料
- FDS ID：`GYPSUM PLASTER`
- 新工程 UUID：`{fc822b50-6832-4cb8-a05d-853d01cd9ee4}`
- 参数：

  - `CONDUCTIVITY` = 原始值 `0.5`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `DENSITY` = 原始值 `500.`

### 9. &SURF — Upholstery

- 分类：Surfaces / 表面
- FDS ID：`UPHOLSTERY`
- 新工程 UUID：`{17ac6bc5-0e70-4bd8-b7d9-5227b2fa6797}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `BACKING` = 文本 `VOID`
  - `COLOR` = 文本 `PURPLE`
  - `BURN_AWAY` = 原始值 `.TRUE.`
  - `MATL_ID(1:2,1)` = 引用 &MATL `FABRIC` → UUID `{51ecfc33-9a35-4844-8b2b-ee1800ae092d}`；&MATL `FOAM` → UUID `{699917e2-d3c2-40d7-8434-7e9470914d0c}`
  - `THICKNESS(1:2)` = 原始值 `0.0005,0.1`

### 10. &SURF — Wall

- 分类：Surfaces / 表面
- FDS ID：`WALL`
- 新工程 UUID：`{2f0a4b00-d910-4d50-b9a3-a3df55c3887a}`
- 参数：

  - `DEFAULT` = 原始值 `.TRUE.`
  - `RGB` = 原始值 `200,200,200`
  - `MATL_ID` = 引用 &MATL `GYPSUM PLASTER` → UUID `{fc822b50-6832-4cb8-a05d-853d01cd9ee4}`
  - `THICKNESS` = 原始值 `0.012`

### 11. &OBST — Couch obstruction 1

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{775d15b1-5823-43dc-a192-56dc303ba73b}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 3.80, 4.60, 0.00, 0.40`

### 12. &OBST — Couch obstruction 2

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{18a0bbef-f7c1-4ce2-9a05-cccf0b27c8cc}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 3.80, 4.60, 0.40, 0.60`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{17ac6bc5-0e70-4bd8-b7d9-5227b2fa6797}`
  - `BULK_DENSITY` = 原始值 `40.`

### 13. &OBST — Couch obstruction 3

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{dcdf148e-4a79-49c3-b07d-fae5c1b5dad7}`
- 参数：

  - `XB` = 原始值 `1.30, 1.50, 3.80, 4.60, 0.00, 0.90`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{17ac6bc5-0e70-4bd8-b7d9-5227b2fa6797}`
  - `BULK_DENSITY` = 原始值 `40.`

### 14. &OBST — Couch obstruction 4

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{2a47cb69-5cdb-44fd-bbad-746adb0a7ba3}`
- 参数：

  - `XB` = 原始值 `3.10, 3.30, 3.80, 4.60, 0.00, 0.90`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{17ac6bc5-0e70-4bd8-b7d9-5227b2fa6797}`
  - `BULK_DENSITY` = 原始值 `40.`

### 15. &OBST — Couch obstruction 5

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{45f34d35-65d2-4665-b294-bf7b76fa64b5}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 4.40, 4.60, 0.60, 1.20`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{17ac6bc5-0e70-4bd8-b7d9-5227b2fa6797}`
  - `BULK_DENSITY` = 原始值 `40.`

### 16. &PART — Ignitor particle

- 分类：Particles / 粒子
- FDS ID：`ignitor particle`
- 新工程 UUID：`{0bc26df9-48c7-4cdc-adaa-e153fcbfc792}`
- 参数：

  - `SURF_ID` = 引用 &SURF `ignitor` → UUID `{ea00e861-2fcc-4c17-9537-c2a2c6507543}`
  - `STATIC` = 原始值 `.TRUE.`

### 17. &SURF — Ignitor surface

- 分类：Surfaces / 表面
- FDS ID：`ignitor`
- 新工程 UUID：`{ea00e861-2fcc-4c17-9537-c2a2c6507543}`
- 参数：

  - `TMP_FRONT` = 原始值 `1000.`
  - `EMISSIVITY` = 原始值 `1.`
  - `GEOMETRY` = 文本 `CYLINDRICAL`
  - `LENGTH` = 原始值 `0.15`
  - `RADIUS` = 原始值 `0.01`

### 18. &INIT — Ignitor particle region

- 分类：Initial Conditions / 初始条件
- FDS ID：``
- 新工程 UUID：`{0d278322-3cdd-464e-b163-f0b324411fac}`
- 参数：

  - `XB` = 原始值 `2.4,2.7,4.1,4.4,0.60,0.70`
  - `PART_ID` = 引用 &PART `ignitor particle` → UUID `{0bc26df9-48c7-4cdc-adaa-e153fcbfc792}`
  - `N_PARTICLES_PER_CELL` = 原始值 `1`
  - `CELL_CENTERED` = 原始值 `T`

### 19. &VENT — Open room boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{eef4b5e5-8a47-4029-b8fa-c5e251f379c4}`
- 参数：

  - `XB` = 原始值 `1,4,0,0,0,2`
  - `SURF_ID` = 文本 `OPEN`

### 20. &BNDF — RADIATIVE HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{ee7fcc76-0083-48fd-89b5-8cdd7f7e8aea}`
- 参数：

  - `QUANTITY` = 文本 `RADIATIVE HEAT FLUX`

### 21. &BNDF — CONVECTIVE HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{a8e4a7db-66cf-44d8-8ec7-70182ddb27f3}`
- 参数：

  - `QUANTITY` = 文本 `CONVECTIVE HEAT FLUX`

### 22. &BNDF — NET HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{2c9bb4cb-09bf-4f88-80bf-cc24df2c6175}`
- 参数：

  - `QUANTITY` = 文本 `NET HEAT FLUX`

### 23. &BNDF — WALL TEMPERATURE

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{9f983df8-be3e-4165-870f-311d8efa8a43}`
- 参数：

  - `QUANTITY` = 文本 `WALL TEMPERATURE`

### 24. &BNDF — BURNING RATE

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{20b6c732-c9eb-4f72-8575-d155f8014141}`
- 参数：

  - `QUANTITY` = 文本 `BURNING RATE`

### 25. &SLCF — Temperature vector slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{62b56e8f-204f-4f98-8457-0770956ce13b}`
- 参数：

  - `PBX` = 原始值 `2.50`
  - `QUANTITY` = 文本 `TEMPERATURE`
  - `VECTOR` = 原始值 `.TRUE.`
  - `CELL_CENTERED` = 原始值 `.TRUE.`

### 26. &SLCF — HRRPUV slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{0935ca09-1c31-4330-8ac2-806632a230fa}`
- 参数：

  - `PBX` = 原始值 `2.50`
  - `QUANTITY` = 文本 `HRRPUV`
  - `CELL_CENTERED` = 原始值 `.TRUE.`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\couch\couch.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\couch\couch.fds`
- FDS 输入语义对比：通过。
