# HVAC_aircoil — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Test of aircoil`
- CHID：`HVAC_aircoil`
- 结束时间：`1 s`
- FDS Schema：`6.10`
- 从空工程创建对象数：`20`

## 界面操作顺序

1. 文件 → 新建，保持空工程。
2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。
3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。
4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；关联键为 UUID，不使用对象名称。
5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。

## 对象和参数

### 1. &MESH — Aircoil test domain

- 分类：Meshes / 网格
- FDS ID：``
- 新工程 UUID：`{0b6ea194-7765-4c6c-b710-fcff7711b61b}`
- 参数：

  - `IJK` = 原始值 `10,10,10`
  - `XB` = 原始值 `0.,1.,0.,1.,0.,1.`

### 2. &MISC — Disable stratification

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{eb9819d0-8fda-4f70-a34a-6bdc29e70fb5}`
- 参数：

  - `STRATIFICATION` = 原始值 `F`

### 3. &RADI — Disable radiation

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{7378e1e1-f15c-4ed5-b98b-89abd98ad448}`
- 参数：

  - `RADIATION` = 原始值 `F`

### 4. &DUMP — Output cadence

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{c290e26e-fc38-4ce9-b3f5-31b4191838b4}`
- 参数：

  - `NFRAMES` = 原始值 `20`

### 5. &VENT — Open XMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{5f4be4da-df31-4517-b107-cdccd18bf126}`
- 参数：

  - `MB` = 文本 `XMIN`
  - `SURF_ID` = 文本 `OPEN`

### 6. &VENT — Open YMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{a2bdf660-fb17-4764-8844-4ec132b23418}`
- 参数：

  - `MB` = 文本 `YMIN`
  - `SURF_ID` = 文本 `OPEN`

### 7. &VENT — Open XMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{3fb44c93-6b4e-4cb9-b820-1b99dc1e0f07}`
- 参数：

  - `MB` = 文本 `XMAX`
  - `SURF_ID` = 文本 `OPEN`

### 8. &VENT — Open YMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{bcbd7ac5-5fd8-4e52-92b8-cbbb94357032}`
- 参数：

  - `MB` = 文本 `YMAX`
  - `SURF_ID` = 文本 `OPEN`

### 9. &SURF — Null surface

- 分类：Surfaces / 表面
- FDS ID：`NULL`
- 新工程 UUID：`{4fd1d6fe-cc6d-4b06-b863-7bd611e01a0e}`
- 参数：无

### 10. &SPEC — Background species

- 分类：Species / 组分
- FDS ID：`SPECIES1`
- 新工程 UUID：`{f59ef25a-b9ce-417a-b8ac-6292fb60d13c}`
- 参数：

  - `MW` = 原始值 `28`
  - `SPECIFIC_HEAT` = 原始值 `1.`
  - `BACKGROUND` = 原始值 `T`

### 11. &VENT — HVAC inlet vent

- 分类：Vents / 通风口
- FDS ID：`INLET`
- 新工程 UUID：`{749e3a54-947b-4742-bdd8-600f3caa308e}`
- 参数：

  - `XB` = 原始值 `0.3,0.7,0.3,0.7,0.0,0.0`
  - `SURF_ID` = 文本 `HVAC`
  - `COLOR` = 文本 `RED`

### 12. &VENT — HVAC outlet vent

- 分类：Vents / 通风口
- FDS ID：`OUTLET`
- 新工程 UUID：`{21ca27e2-7420-4785-a0ea-faa10e7b26d7}`
- 参数：

  - `XB` = 原始值 `0.3,0.7,0.3,0.7,1.0,1.0`
  - `SURF_ID` = 文本 `HVAC`
  - `COLOR` = 文本 `GREEN`

### 13. &HVAC — Inlet HVAC node

- 分类：HVAC
- FDS ID：`INLET`
- 新工程 UUID：`{80603a9e-bd04-4d19-ab31-3c7eca481c25}`
- 参数：

  - `TYPE_ID` = 文本 `NODE`
  - `VENT_ID` = 引用 &VENT `INLET` → UUID `{749e3a54-947b-4742-bdd8-600f3caa308e}`
  - `DUCT_ID` = 引用 &HVAC `DUCT` → UUID `{df3f6aa7-6862-4f76-bae8-81aa01285228}`

### 14. &HVAC — Outlet HVAC node

- 分类：HVAC
- FDS ID：`OUTLET`
- 新工程 UUID：`{3b4412b7-cf15-415a-9b07-d0912f14c0fa}`
- 参数：

  - `TYPE_ID` = 文本 `NODE`
  - `VENT_ID` = 引用 &VENT `OUTLET` → UUID `{21ca27e2-7420-4785-a0ea-faa10e7b26d7}`
  - `DUCT_ID` = 引用 &HVAC `DUCT` → UUID `{df3f6aa7-6862-4f76-bae8-81aa01285228}`

### 15. &HVAC — HVAC duct

- 分类：HVAC
- FDS ID：`DUCT`
- 新工程 UUID：`{df3f6aa7-6862-4f76-bae8-81aa01285228}`
- 参数：

  - `TYPE_ID` = 文本 `DUCT`
  - `NODE_ID` = 引用 &HVAC `INLET` → UUID `{80603a9e-bd04-4d19-ab31-3c7eca481c25}`；&HVAC `OUTLET` → UUID `{3b4412b7-cf15-415a-9b07-d0912f14c0fa}`
  - `LENGTH` = 原始值 `1`
  - `AREA` = 原始值 `0.1`
  - `VOLUME_FLOW` = 原始值 `1.0`
  - `AIRCOIL_ID` = 引用 &HVAC `AIRCOIL` → UUID `{ea2282bb-c04a-4f84-8ca5-6b4910e4ba16}`
  - `TAU_VF` = 原始值 `0.`

### 16. &HVAC — Aircoil

- 分类：HVAC
- FDS ID：`AIRCOIL`
- 新工程 UUID：`{ea2282bb-c04a-4f84-8ca5-6b4910e4ba16}`
- 参数：

  - `TYPE_ID` = 文本 `AIRCOIL`
  - `EFFICIENCY` = 原始值 `0.5`
  - `COOLANT_SPECIFIC_HEAT` = 原始值 `4.0`
  - `COOLANT_TEMPERATURE` = 原始值 `100.0`
  - `COOLANT_MASS_FLOW` = 原始值 `10.0`

### 17. &DEVC — Aircoil heat exchange

- 分类：Devices / 设备
- FDS ID：`FDS Q`
- 新工程 UUID：`{e560035d-cdb1-4cb9-8d0a-7fd9240061fe}`
- 参数：

  - `QUANTITY` = 文本 `AIRCOIL HEAT EXCHANGE`
  - `DUCT_ID` = 引用 &HVAC `DUCT` → UUID `{df3f6aa7-6862-4f76-bae8-81aa01285228}`

### 18. &DEVC — Outlet node temperature

- 分类：Devices / 设备
- FDS ID：`FDS T`
- 新工程 UUID：`{7b5b6dac-df67-4c23-b33f-c45fee3e97c0}`
- 参数：

  - `QUANTITY` = 文本 `NODE TEMPERATURE`
  - `NODE_ID` = 引用 &HVAC `OUTLET` → UUID `{3b4412b7-cf15-415a-9b07-d0912f14c0fa}`

### 19. &OBST — Central obstruction

- 分类：Geometry / 几何
- FDS ID：``
- 新工程 UUID：`{02789cca-e85d-4a4a-98db-053359262c14}`
- 参数：

  - `XB` = 原始值 `0,1,0,1,0.4,0.6`

### 20. &SLCF — Temperature vector slice

- 分类：Outputs / 输出
- FDS ID：``
- 新工程 UUID：`{b19b9b31-e94d-432a-96d4-0b1a35eca3b9}`
- 参数：

  - `PBY` = 原始值 `0.5`
  - `QUANTITY` = 文本 `TEMPERATURE`
  - `VECTOR` = 原始值 `T`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\HVAC_aircoil\HVAC_aircoil.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\HVAC_aircoil\HVAC_aircoil.fds`
- FDS 输入语义对比：通过。
