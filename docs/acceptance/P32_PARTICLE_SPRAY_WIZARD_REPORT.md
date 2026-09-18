# FireCAE P32 — 粒子/喷淋系统 GUI→FDS→求解验收

日期：2026-09-03  
版本：FireCAE 0.3.0 / FDS 6.11.1

## 结论

P32 已完成粒子和喷淋系统的专用建模入口，并通过正式 GUI 控件配置、工程保存重开、FDS 导出、实际 FDS 求解和结果扫描。验收算例由打包的 FDS 6.11.1 串行求解到 2.0 s，输出：

`STOP: FDS completed successfully (CHID: simple_test)`

FireCAE 结果扫描器识别 25/25 个结果文件；包含 `.smv`、SLCF、BNDF、Smoke3D、PART 和 CSV。2 s 内喷淋器温度仍为 20°C，未达到 68°C 动作温度，FDS 报告 `No Activation`，与输入设定一致。

## 新增功能

- “模型 → 专业助手 → 粒子与喷淋系统向导”；
- 喷淋器/水喷嘴类型；
- `SPEC → PART → PROP → DEVC` UUID 引用链；
- 液滴直径、最大寿命、流量、粒子初速度、每秒粒子数；
- 动作温度、RTI 或指定时间动作；
- 可编辑喷雾扇区表，每一行生成独立 UUID 的 `TABL`，共享同一 FDS ID；
- 设备 XYZ 位置；
- 自动创建或更新 `DUMP / DT_PART`；
- 中英文界面；
- 整套对象原子撤销/重做；
- 重复 ID、角度顺序、半径、权重和空表校验。

实现字段已对照 [FDS 官方 `read.f90`](https://github.com/firemodels/fds/blob/master/Source/read.f90) 中 `SPEC`、`PART`、`PROP` 的 Namelist 声明。

## 同阶段修复的底层问题

1. **混合对象写出丢失**：原写出器只要发现一个通用 Namelist，就会忽略工程中的旧版强类型网格、反应、表面、几何、通风口和输出。现已统一写出两类对象，并增加回归测试。
2. **内置组分名称误改**：`WATER VAPOR` 曾被通用 ID 清理规则改为 `WATER_VAPOR`，FDS 将其识别为缺少液相参数的自定义组分。现在保留内置组分名称中的空格。
3. **已有 DUMP 合并**：向导发现已有 `DUMP` 时更新 `DT_PART`，不会盲目创建冲突记录；撤销时恢复原参数。

## 自动化门禁

- `FireCAEP32ParticleSprayUiTests`：PASS；
- `FireCAEFdsModelTests` 混合对象回归：PASS；
- GUI 配置后导出的 `SPEC/PART/PROP/TABL/DEVC/DUMP` 参数逐项检查：PASS；
- 两条同 ID `TABL`：PASS；
- 原子 Undo/Redo：PASS；
- `.firecae` 保存/重开后 FDS 文本完全一致：PASS；
- FdsRunner 实算与结果扫描：PASS（25/25）。

## 证据

- 工程：`pyrosim-workflow/p32-particle-spray/p32.firecae`
- 首次导出：`pyrosim-workflow/p32-particle-spray/p32-first.fds`
- 重开导出：`pyrosim-workflow/p32-particle-spray/p32-reopened.fds`
- 求解日志：`pyrosim-workflow/p32-particle-spray/simple_test.out`
- Smokeview 案例：`pyrosim-workflow/p32-particle-spray/simple_test.smv`
- 粒子结果：`pyrosim-workflow/p32-particle-spray/simple_test_1.prt5`

SHA-256：

- `p32.firecae`：`E218D6FCA3D1D58C0101B8556617F7F43FB7A7953B82919AE2F7E9C3D7DC535E`
- `p32-reopened.fds`：`8368E67599FB9F239C89A94403585A5F0D73106B676F3C425890410AE0D125F8`
- `simple_test.smv`：`91EC7D0F2871EFACEE75AB4F614EFABF0A22B5EA34230DD974429B8507DA51BF`

## 边界

P32 建立的是可靠的常用喷淋/水喷嘴工作流，不代表已覆盖 FDS `PART/PROP` 的全部高级模型。Rosin–Rammler 分布预览、破碎/碰撞、复杂多组分液滴、喷头数据库和大规模喷头布置仍需继续开发。
