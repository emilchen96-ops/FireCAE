# couch_smoke_12s — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Single Couch Tutorial - 12 s FireCAE Smoke Test`
- CHID：`couch_smoke_12s`
- 结束时间：`12 s`
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
- 新工程 UUID：`{d6436833-f354-4eff-bf17-d955caf6afac}`
- 参数：

  - `IJK` = 原始值 `25,25,12`
  - `XB` = 原始值 `0.0,2.5,0.0,2.5,0.0,1.2`
  - `MULT_ID` = 引用 &MULT `mesh` → UUID `{1abdc18d-8f2d-48b3-9bb1-dd598699e743}`

### 2. &MULT — 2 x 2 x 2 mesh array

- 分类：Meshes / 网格
- FDS ID：`mesh`
- 新工程 UUID：`{1abdc18d-8f2d-48b3-9bb1-dd598699e743}`
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
- 新工程 UUID：`{963bd174-c2a4-4c45-bce3-1c77c319c802}`
- 参数：

  - `NFRAMES` = 原始值 `60`
  - `DT_HRR` = 原始值 `1.`
  - `DT_DEVC` = 原始值 `1.`

### 4. &SPEC — Polyurethane

- 分类：Species / 组分
- FDS ID：`POLYURETHANE`
- 新工程 UUID：`{fb4a820a-3d3f-4871-9f6e-659f22a5e7f1}`
- 参数：

  - `FORMULA` = 文本 `C6.3H7.1N1.0O2.1`

### 5. &REAC — Polyurethane reaction

- 分类：Reactions / 反应
- FDS ID：``
- 新工程 UUID：`{eff806b5-6190-4f03-a0dd-a3976c00f4f2}`
- 参数：

  - `FUEL` = 引用 &SPEC `POLYURETHANE` → UUID `{fb4a820a-3d3f-4871-9f6e-659f22a5e7f1}`
  - `SOOT_YIELD` = 原始值 `0.01`
  - `HEAT_OF_COMBUSTION` = 原始值 `22700.`

### 6. &MATL — Fabric

- 分类：Materials / 材料
- FDS ID：`FABRIC`
- 新工程 UUID：`{293f6cce-1498-407d-a3a0-f27207817634}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `CONDUCTIVITY` = 原始值 `0.5`
  - `DENSITY` = 原始值 `50.`
  - `NU_SPEC` = 原始值 `1.`
  - `SPEC_ID` = 引用 &SPEC `POLYURETHANE` → UUID `{fb4a820a-3d3f-4871-9f6e-659f22a5e7f1}`
  - `REFERENCE_TEMPERATURE` = 原始值 `250.`
  - `HEAT_OF_REACTION` = 原始值 `500.`
  - `HEAT_OF_COMBUSTION` = 原始值 `16000.`

### 7. &MATL — Foam

- 分类：Materials / 材料
- FDS ID：`FOAM`
- 新工程 UUID：`{97e7b6bb-3850-4380-a29b-ed7ff9d0a207}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `CONDUCTIVITY` = 原始值 `0.1`
  - `DENSITY` = 原始值 `40.0`
  - `NU_SPEC` = 原始值 `1.`
  - `SPEC_ID` = 引用 &SPEC `POLYURETHANE` → UUID `{fb4a820a-3d3f-4871-9f6e-659f22a5e7f1}`
  - `REFERENCE_TEMPERATURE` = 原始值 `280.`
  - `HEAT_OF_REACTION` = 原始值 `800.`
  - `HEAT_OF_COMBUSTION` = 原始值 `22700.`

### 8. &MATL — Gypsum plaster

- 分类：Materials / 材料
- FDS ID：`GYPSUM PLASTER`
- 新工程 UUID：`{1f32c704-8b58-4495-a6c9-48073a3144a2}`
- 参数：

  - `CONDUCTIVITY` = 原始值 `0.5`
  - `SPECIFIC_HEAT` = 原始值 `1.0`
  - `DENSITY` = 原始值 `500.`

### 9. &SURF — Upholstery

- 分类：Surfaces / 表面
- FDS ID：`UPHOLSTERY`
- 新工程 UUID：`{01e501e0-10fc-4bea-a820-f92fea105f06}`
- 参数：

  - `FYI` = 文本 `Properties completely fabricated`
  - `BACKING` = 文本 `VOID`
  - `COLOR` = 文本 `PURPLE`
  - `BURN_AWAY` = 原始值 `.TRUE.`
  - `MATL_ID(1:2,1)` = 引用 &MATL `FABRIC` → UUID `{293f6cce-1498-407d-a3a0-f27207817634}`；&MATL `FOAM` → UUID `{97e7b6bb-3850-4380-a29b-ed7ff9d0a207}`
  - `THICKNESS(1:2)` = 原始值 `0.0005,0.1`

### 10. &SURF — Wall

- 分类：Surfaces / 表面
- FDS ID：`WALL`
- 新工程 UUID：`{68f8cf0b-3662-49fc-9d6d-342bc738bda5}`
- 参数：

  - `DEFAULT` = 原始值 `.TRUE.`
  - `RGB` = 原始值 `200,200,200`
  - `MATL_ID` = 引用 &MATL `GYPSUM PLASTER` → UUID `{1f32c704-8b58-4495-a6c9-48073a3144a2}`
  - `THICKNESS` = 原始值 `0.012`

### 11. &OBST — Couch obstruction 1

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{06574ecd-dc29-4fde-b116-578804b8a26d}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 3.80, 4.60, 0.00, 0.40`

### 12. &OBST — Couch obstruction 2

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{f8aefd42-75f2-424d-9e3f-705526ec5f7c}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 3.80, 4.60, 0.40, 0.60`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{01e501e0-10fc-4bea-a820-f92fea105f06}`
  - `BULK_DENSITY` = 原始值 `40.`

### 13. &OBST — Couch obstruction 3

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{f92a780c-26db-475b-80c9-a69336b5a8f5}`
- 参数：

  - `XB` = 原始值 `1.30, 1.50, 3.80, 4.60, 0.00, 0.90`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{01e501e0-10fc-4bea-a820-f92fea105f06}`
  - `BULK_DENSITY` = 原始值 `40.`

### 14. &OBST — Couch obstruction 4

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{07e74fd8-19ed-443f-958b-4fa8b2969efe}`
- 参数：

  - `XB` = 原始值 `3.10, 3.30, 3.80, 4.60, 0.00, 0.90`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{01e501e0-10fc-4bea-a820-f92fea105f06}`
  - `BULK_DENSITY` = 原始值 `40.`

### 15. &OBST — Couch obstruction 5

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{25a1dccf-b786-4d14-835b-36558bcac745}`
- 参数：

  - `XB` = 原始值 `1.50, 3.10, 4.40, 4.60, 0.60, 1.20`
  - `SURF_ID` = 引用 &SURF `UPHOLSTERY` → UUID `{01e501e0-10fc-4bea-a820-f92fea105f06}`
  - `BULK_DENSITY` = 原始值 `40.`

### 16. &PART — Ignitor particle

- 分类：Particles / 粒子
- FDS ID：`ignitor particle`
- 新工程 UUID：`{70bbee18-bb51-4613-a279-fdf6e9923de1}`
- 参数：

  - `SURF_ID` = 引用 &SURF `ignitor` → UUID `{35e9bb0e-9925-4f5c-8130-543a76651111}`
  - `STATIC` = 原始值 `.TRUE.`

### 17. &SURF — Ignitor surface

- 分类：Surfaces / 表面
- FDS ID：`ignitor`
- 新工程 UUID：`{35e9bb0e-9925-4f5c-8130-543a76651111}`
- 参数：

  - `TMP_FRONT` = 原始值 `1000.`
  - `EMISSIVITY` = 原始值 `1.`
  - `GEOMETRY` = 文本 `CYLINDRICAL`
  - `LENGTH` = 原始值 `0.15`
  - `RADIUS` = 原始值 `0.01`

### 18. &INIT — Ignitor particle region

- 分类：Initial Conditions / 初始条件
- FDS ID：``
- 新工程 UUID：`{ab009b65-fc2e-4c3a-bc56-cc7f9910ed57}`
- 参数：

  - `XB` = 原始值 `2.4,2.7,4.1,4.4,0.60,0.70`
  - `PART_ID` = 引用 &PART `ignitor particle` → UUID `{70bbee18-bb51-4613-a279-fdf6e9923de1}`
  - `N_PARTICLES_PER_CELL` = 原始值 `1`
  - `CELL_CENTERED` = 原始值 `T`

### 19. &VENT — Open room boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{a4f5401a-e899-4f15-92fb-fae856b46e81}`
- 参数：

  - `XB` = 原始值 `1,4,0,0,0,2`
  - `SURF_ID` = 文本 `OPEN`

### 20. &BNDF — RADIATIVE HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{ebfc45ce-45ed-47ad-b7c0-1f9de96111a4}`
- 参数：

  - `QUANTITY` = 文本 `RADIATIVE HEAT FLUX`

### 21. &BNDF — CONVECTIVE HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{6dfcf1e2-7057-4e92-a327-98ea3df8236f}`
- 参数：

  - `QUANTITY` = 文本 `CONVECTIVE HEAT FLUX`

### 22. &BNDF — NET HEAT FLUX

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{74e16362-439c-450d-b3be-aaef5f3b810f}`
- 参数：

  - `QUANTITY` = 文本 `NET HEAT FLUX`

### 23. &BNDF — WALL TEMPERATURE

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{ce2b0719-a92f-447c-963b-1027a5814607}`
- 参数：

  - `QUANTITY` = 文本 `WALL TEMPERATURE`

### 24. &BNDF — BURNING RATE

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{6894ae4c-0e8d-4477-9bf2-7250d86a5ebf}`
- 参数：

  - `QUANTITY` = 文本 `BURNING RATE`

### 25. &SLCF — Temperature vector slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{bd7d98b7-457a-4d96-8f66-e6e64814d9e0}`
- 参数：

  - `PBX` = 原始值 `2.50`
  - `QUANTITY` = 文本 `TEMPERATURE`
  - `VECTOR` = 原始值 `.TRUE.`
  - `CELL_CENTERED` = 原始值 `.TRUE.`

### 26. &SLCF — HRRPUV slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{13e3829a-8435-4ce0-9897-14dce4444b7e}`
- 参数：

  - `PBX` = 原始值 `2.50`
  - `QUANTITY` = 文本 `HRRPUV`
  - `CELL_CENTERED` = 原始值 `.TRUE.`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\couch_smoke_12s\couch_smoke_12s.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\couch_smoke_12s\couch_smoke_12s.fds`
- FDS 输入语义对比：通过。
