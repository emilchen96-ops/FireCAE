# activate_vents — 空工程 GUI 复现记录

> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。教程工厂只作为期望数据配方，不直接装载到目标工程。

## 项目设置

- 名称：`Test of VENT activation/deactivation`
- CHID：`activate_vents`
- 结束时间：`20 s`
- FDS Schema：`6.10`
- 从空工程创建对象数：`44`

## 界面操作顺序

1. 文件 → 新建，保持空工程。
2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。
3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。
4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；关联键为 UUID，不使用对象名称。
5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。

## 对象和参数

### 1. &MESH — Activation domain

- 分类：Meshes / 网格
- FDS ID：``
- 新工程 UUID：`{3e0f3559-af27-419b-8848-a10fb9cdb834}`
- 参数：

  - `IJK` = 原始值 `21,10,10`
  - `XB` = 原始值 `0.0,2.1,0.0,1.0,0.0,1.0`

### 2. &TIME — Time step

- 分类：Configuration / 仿真设置
- FDS ID：``
- 新工程 UUID：`{b09557d1-83c7-48fc-b820-9d6d501d0d30}`
- 参数：

  - `DT` = 原始值 `0.05`

### 3. &SURF — Blower 1

- 分类：Surfaces / 表面
- FDS ID：`BLOW 1`
- 新工程 UUID：`{82b7b7c7-f80b-44d2-9121-5b01e36b4599}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `PURPLE`
  - `PART_ID` = 引用 &PART `TRACER 1` → UUID `{7fb6bbd3-094e-4ecd-b106-04a2db431427}`

### 4. &SURF — Blower 2

- 分类：Surfaces / 表面
- FDS ID：`BLOW 2`
- 新工程 UUID：`{b1c86be6-fa93-4868-b730-5fa2826678cf}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `RED`
  - `PART_ID` = 引用 &PART `TRACER 2` → UUID `{148aa867-2905-4973-83e7-895a26e915e9}`

### 5. &SURF — Blower 3

- 分类：Surfaces / 表面
- FDS ID：`BLOW 3`
- 新工程 UUID：`{ce3cc3ef-0971-424a-b5bd-3d93a56bc4be}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `ORANGE`
  - `PART_ID` = 引用 &PART `TRACER 3` → UUID `{55dc1625-5fd8-43a0-8765-8b77247ba43f}`

### 6. &SURF — Blower 4

- 分类：Surfaces / 表面
- FDS ID：`BLOW 4`
- 新工程 UUID：`{4f600621-2e7a-49cb-beaa-059efde43a42}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `YELLOW`
  - `PART_ID` = 引用 &PART `TRACER 4` → UUID `{3acbe43e-22bf-4427-bd84-d8052b8e16ee}`

### 7. &SURF — Blower 5

- 分类：Surfaces / 表面
- FDS ID：`BLOW 5`
- 新工程 UUID：`{a0c883e2-da23-4eb6-9ed6-e94ff85566b2}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `GREEN`
  - `PART_ID` = 引用 &PART `TRACER 5` → UUID `{2118b3f1-f8d2-47c6-929e-bfb622b88c1f}`

### 8. &SURF — Blower 6

- 分类：Surfaces / 表面
- FDS ID：`BLOW 6`
- 新工程 UUID：`{359e5c28-4b76-4301-8256-704db39a2be6}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `CYAN`
  - `PART_ID` = 引用 &PART `TRACER 6` → UUID `{ac1caa5b-0c0a-4481-9b24-b47d8fb55996}`

### 9. &SURF — Blower 7

- 分类：Surfaces / 表面
- FDS ID：`BLOW 7`
- 新工程 UUID：`{9414f492-ce9e-4a97-a40a-0fdc78edfe6f}`
- 参数：

  - `VEL` = 原始值 `-0.2`
  - `COLOR` = 文本 `BLUE`
  - `PART_ID` = 引用 &PART `TRACER 7` → UUID `{8ce6a9e7-bd42-4ff8-aebf-67c828bfc87d}`

### 10. &PART — Tracer 1

- 分类：Particles / 粒子
- FDS ID：`TRACER 1`
- 新工程 UUID：`{7fb6bbd3-094e-4ecd-b106-04a2db431427}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `PURPLE`

### 11. &PART — Tracer 2

- 分类：Particles / 粒子
- FDS ID：`TRACER 2`
- 新工程 UUID：`{148aa867-2905-4973-83e7-895a26e915e9}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `RED`

### 12. &PART — Tracer 3

- 分类：Particles / 粒子
- FDS ID：`TRACER 3`
- 新工程 UUID：`{55dc1625-5fd8-43a0-8765-8b77247ba43f}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `ORANGE`

### 13. &PART — Tracer 4

- 分类：Particles / 粒子
- FDS ID：`TRACER 4`
- 新工程 UUID：`{3acbe43e-22bf-4427-bd84-d8052b8e16ee}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `YELLOW`

### 14. &PART — Tracer 5

- 分类：Particles / 粒子
- FDS ID：`TRACER 5`
- 新工程 UUID：`{2118b3f1-f8d2-47c6-929e-bfb622b88c1f}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `GREEN`

### 15. &PART — Tracer 6

- 分类：Particles / 粒子
- FDS ID：`TRACER 6`
- 新工程 UUID：`{ac1caa5b-0c0a-4481-9b24-b47d8fb55996}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `CYAN`

### 16. &PART — Tracer 7

- 分类：Particles / 粒子
- FDS ID：`TRACER 7`
- 新工程 UUID：`{8ce6a9e7-bd42-4ff8-aebf-67c828bfc87d}`
- 参数：

  - `MASSLESS` = 原始值 `.TRUE.`
  - `COLOR` = 文本 `BLUE`
  - `DEVC_ID` = 引用 &DEVC `timer 7b` → UUID `{41401698-01a0-4238-8435-b861313003d2}`

### 17. &VENT — Activated vent 1

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{49fc91ef-f283-4f89-81c1-1696ecad0ec8}`
- 参数：

  - `XB` = 原始值 `0.10,0.20,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 1` → UUID `{82b7b7c7-f80b-44d2-9121-5b01e36b4599}`
  - `COLOR` = 文本 `PURPLE`
  - `CTRL_ID` = 引用 &CTRL `controller 1` → UUID `{98d3aad4-b316-4586-9cbc-d9ddc16b70a6}`

### 18. &VENT — Activated vent 2

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{2e9c2afa-9013-419e-9d66-2ed130cc4a97}`
- 参数：

  - `XB` = 原始值 `0.40,0.50,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 2` → UUID `{b1c86be6-fa93-4868-b730-5fa2826678cf}`
  - `COLOR` = 文本 `RED`
  - `DEVC_ID` = 引用 &DEVC `timer 2` → UUID `{6eadeaff-1e9b-422b-97aa-c368d6cce8e0}`

### 19. &VENT — Activated vent 3

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{1e22f1bb-7c3b-441b-a750-97357fc76e37}`
- 参数：

  - `XB` = 原始值 `0.70,0.80,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 3` → UUID `{ce3cc3ef-0971-424a-b5bd-3d93a56bc4be}`
  - `COLOR` = 文本 `ORANGE`
  - `CTRL_ID` = 引用 &CTRL `controller 3` → UUID `{d7946bc2-f0b7-4437-be9e-49f6fe44b125}`

### 20. &VENT — Activated vent 4

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{626607d2-8892-4cf6-8bdd-cc0a248f2db7}`
- 参数：

  - `XB` = 原始值 `1.00,1.10,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 4` → UUID `{4f600621-2e7a-49cb-beaa-059efde43a42}`
  - `COLOR` = 文本 `YELLOW`
  - `CTRL_ID` = 引用 &CTRL `controller 4` → UUID `{9b9b1b01-4030-4532-81cb-365454f391bb}`

### 21. &VENT — Activated vent 5

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{bca4c03e-a005-4467-841e-5ac7de4c130f}`
- 参数：

  - `XB` = 原始值 `1.30,1.40,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 5` → UUID `{a0c883e2-da23-4eb6-9ed6-e94ff85566b2}`
  - `COLOR` = 文本 `GREEN`
  - `DEVC_ID` = 引用 &DEVC `timer 5` → UUID `{a342639c-2a92-41aa-8705-d66b45e7689c}`

### 22. &VENT — Activated vent 6

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{305754fe-cf96-4a08-a121-e0cb29699b4f}`
- 参数：

  - `XB` = 原始值 `1.60,1.70,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 6` → UUID `{359e5c28-4b76-4301-8256-704db39a2be6}`
  - `COLOR` = 文本 `CYAN`
  - `DEVC_ID` = 引用 &DEVC `timer 6` → UUID `{70980d76-115a-41a6-9892-4b7ff96a9fcd}`

### 23. &VENT — Activated vent 7

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{207a5d85-098d-4468-bdb5-81434ae0fabe}`
- 参数：

  - `XB` = 原始值 `1.90,2.00,0.40,0.60,0.00,0.00`
  - `SURF_ID` = 引用 &SURF `BLOW 7` → UUID `{9414f492-ce9e-4a97-a40a-0fdc78edfe6f}`
  - `COLOR` = 文本 `BLUE`
  - `DEVC_ID` = 引用 &DEVC `timer 7` → UUID `{9b2e4000-09d6-43fa-9821-8c2ba9972c88}`

### 24. &DEVC — clock 1

- 分类：Devices / 设备
- FDS ID：`clock 1`
- 新工程 UUID：`{fbcf3727-b66b-4ef5-89ab-dbcb5bb263ce}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`

### 25. &DEVC — timer 2

- 分类：Devices / 设备
- FDS ID：`timer 2`
- 新工程 UUID：`{6eadeaff-1e9b-422b-97aa-c368d6cce8e0}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `5.0`

### 26. &DEVC — timer 5

- 分类：Devices / 设备
- FDS ID：`timer 5`
- 新工程 UUID：`{a342639c-2a92-41aa-8705-d66b45e7689c}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `5.0`

### 27. &DEVC — timer 6

- 分类：Devices / 设备
- FDS ID：`timer 6`
- 新工程 UUID：`{70980d76-115a-41a6-9892-4b7ff96a9fcd}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `6.0`

### 28. &DEVC — timer 7

- 分类：Devices / 设备
- FDS ID：`timer 7`
- 新工程 UUID：`{9b2e4000-09d6-43fa-9821-8c2ba9972c88}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `7.0`

### 29. &DEVC — timer 7b

- 分类：Devices / 设备
- FDS ID：`timer 7b`
- 新工程 UUID：`{41401698-01a0-4238-8435-b861313003d2}`
- 参数：

  - `XYZ` = 原始值 `0.1,0.1,0.1`
  - `QUANTITY` = 文本 `TIME`
  - `SETPOINT` = 原始值 `11.0`

### 30. &CTRL — Controller 1

- 分类：Controls / 控制
- FDS ID：`controller 1`
- 新工程 UUID：`{98d3aad4-b316-4586-9cbc-d9ddc16b70a6}`
- 参数：

  - `FUNCTION_TYPE` = 文本 `CUSTOM`
  - `INPUT_ID` = 引用 &DEVC `clock 1` → UUID `{fbcf3727-b66b-4ef5-89ab-dbcb5bb263ce}`
  - `RAMP_ID` = 引用 &RAMP `ramp 1` → UUID `{9e0b54bd-09c1-4869-ab4d-91f18b8e3397}`

### 31. &RAMP — Ramp 1 point 1

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{9e0b54bd-09c1-4869-ab4d-91f18b8e3397}`
- 参数：

  - `T` = 原始值 `0.00`
  - `F` = 原始值 `-1.`

### 32. &RAMP — Ramp 1 point 2

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{891330ac-7f70-4b2a-b31b-15f15bda7fef}`
- 参数：

  - `T` = 原始值 `2.99`
  - `F` = 原始值 `-1.`

### 33. &RAMP — Ramp 1 point 3

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{72142795-022c-4b01-98fd-0bd9ef560382}`
- 参数：

  - `T` = 原始值 `3.01`
  - `F` = 原始值 `1.`

### 34. &RAMP — Ramp 1 point 4

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{0aa4bd48-971e-4226-9aca-86035797b34e}`
- 参数：

  - `T` = 原始值 `5.99`
  - `F` = 原始值 `1.`

### 35. &RAMP — Ramp 1 point 5

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{cd133943-97de-483e-91c2-0d14dc1de128}`
- 参数：

  - `T` = 原始值 `6.01`
  - `F` = 原始值 `-1.`

### 36. &RAMP — Ramp 1 point 6

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{d1e0f866-017c-4694-b35a-6cf585dd8fa8}`
- 参数：

  - `T` = 原始值 `11.99`
  - `F` = 原始值 `-1.`

### 37. &RAMP — Ramp 1 point 7

- 分类：Controls / 控制
- FDS ID：`ramp 1`
- 新工程 UUID：`{c1f96f2e-c783-4582-b92b-62caeae2af26}`
- 参数：

  - `T` = 原始值 `12.01`
  - `F` = 原始值 `1.`

### 38. &CTRL — Controller 3

- 分类：Controls / 控制
- FDS ID：`controller 3`
- 新工程 UUID：`{d7946bc2-f0b7-4437-be9e-49f6fe44b125}`
- 参数：

  - `FUNCTION_TYPE` = 文本 `TIME_DELAY`
  - `INPUT_ID` = 引用 &DEVC `timer 2` → UUID `{6eadeaff-1e9b-422b-97aa-c368d6cce8e0}`
  - `DELAY` = 原始值 `3.`

### 39. &CTRL — Controller 4

- 分类：Controls / 控制
- FDS ID：`controller 4`
- 新工程 UUID：`{9b9b1b01-4030-4532-81cb-365454f391bb}`
- 参数：

  - `FUNCTION_TYPE` = 文本 `ALL`
  - `INPUT_ID` = 引用 &CTRL `controller 1` → UUID `{98d3aad4-b316-4586-9cbc-d9ddc16b70a6}`；&CTRL `controller 3` → UUID `{d7946bc2-f0b7-4437-be9e-49f6fe44b125}`

### 40. &VENT — Open XMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{95c6cc97-6e86-4810-beef-31e07534007b}`
- 参数：

  - `MB` = 文本 `XMIN`
  - `SURF_ID` = 文本 `OPEN`

### 41. &VENT — Open XMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{7a61ad9d-f56b-42a4-abb7-1d11148fe7c7}`
- 参数：

  - `MB` = 文本 `XMAX`
  - `SURF_ID` = 文本 `OPEN`

### 42. &VENT — Open YMIN boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{cb284ba0-0eb5-4f3f-9377-c09d034be053}`
- 参数：

  - `MB` = 文本 `YMIN`
  - `SURF_ID` = 文本 `OPEN`

### 43. &VENT — Open YMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{83feac3d-96a6-4f83-bbb0-066ee8bb971a}`
- 参数：

  - `MB` = 文本 `YMAX`
  - `SURF_ID` = 文本 `OPEN`

### 44. &VENT — Open ZMAX boundary

- 分类：Vents / 通风口
- FDS ID：``
- 新工程 UUID：`{4b782f14-e444-410a-824f-bcc8c8fff2ee}`
- 参数：

  - `MB` = 文本 `ZMAX`
  - `SURF_ID` = 文本 `OPEN`

## 自动验收结论

- 项目保存/重开：通过；全部新工程 UUID 保持不变。
- 模型校验：通过。
- 官方输入：`D:\FireCAE\tests\data\fds-tutorials\activate_vents\activate_vents.fds`
- GUI 导出：`D:\FireCAE\docs\acceptance\tutorials\blank-gui\activate_vents\activate_vents.fds`
- FDS 输入语义对比：通过。
