# Your First Fire — 从空工程完成第一个 FireCAE 算例

适用版本：FireCAE 0.3.0（P27 验收版）/ FDS 6.11.1  
正式入口：`模型 → Tutorials → Your First Fire / 第一个火灾算例`  
原则：教程面板只提示并检查，不创建任何预制业务对象。

## 1. 教学目的与完成效果

本教程建立一个 `6 m × 4 m × 3 m` 的小型计算域、500 kW 丙烷火源、顶部开放边界、温度点设备和温度切片。完成后，用户能够校验并保存 FireCAE 工程、生成 FDS、运行串行 CPU 求解、加载 `.smv`、播放烟气和查看 HRR/温度曲线。

前置条件：在“文件 → 首选项 → 应用程序”确认 FDS 和 Smokeview 状态可用；工程单位使用 m。

## 2. 分步操作

### 步骤 1：启动空工程

1. 打开 `模型 → Tutorials → Your First Fire`。
2. FireCAE 新建空工程并打开“分步教程”停靠面板。
3. 点击“检查当前步骤”。实时检查应显示 FDS 对象数为 0。

为什么：空工程是证明所有对象都由正式 GUI 创建、而非预制工程注入的验收基线。

### 步骤 2：工程设置

菜单：`模型 → 项目设置`

| 参数 | 值 | 含义 |
|---|---:|---|
| Title | Your First Fire | 工程可读标题 |
| CHID | first_fire | 所有 FDS 结果文件的稳定前缀 |
| End Time | 30 s | 模拟结束时间 |

CHID 只使用字母、数字和下划线。完成后回到教程面板点击“检查当前步骤”。

### 步骤 3：计算网格

菜单：`模型 → 网格`

选择“按目标网格尺寸定义”：

| 字段 | 值 |
|---|---:|
| X 最小/最大 | 0 / 6 m |
| Y 最小/最大 | 0 / 4 m |
| Z 最小/最大 | 0 / 3 m |
| 目标 X/Y/Z 单元尺寸 | 0.20 / 0.20 / 0.20 m |

预期约为 `30 × 20 × 15 = 9000` 个单元。`XB` 表示六个空间边界，`IJK` 表示三个方向的单元数量；普通流程不需要手工填写缩写。正式工程应使用 D*/dx 助手并进行网格敏感性分析，本教程的 0.20 m 用于快速演示。

### 步骤 4：反应、燃烧表面和火源

菜单：`模型 → Professional Assistants → Fire Source Wizard`

| 参数 | 值 |
|---|---:|
| Fuel | PROPANE |
| Fire size | 1 m × 1 m |
| Position | 房间地面中心 |
| Total HRR | 500 kW |
| HRRPUA | 500 kW/m²（由面积计算） |
| Ignition time | 0 s |
| Soot yield | 0.01 |
| Radiative fraction | 0.35 |

REAC 定义燃料化学和产物，SURF 定义燃烧面，VENT 将燃烧面放到空间位置。引用以 UUID 保存，生成 FDS 时解析成 FDS ID。不要同时输入互相矛盾的总 HRR 与 HRRPUA。

### 步骤 5：开放边界和输出

1. 使用 `模型 → Vent` 在 `Z=3 m` 顶面创建 `OPEN` VENT。
2. 使用 `模型 → Professional Assistants → Output Wizard` 创建：

| 输出 | 参数 |
|---|---|
| 温度点设备 | `QUANTITY=TEMPERATURE`, `XYZ=3,2,1.5 m` |
| 温度切片 | `QUANTITY=TEMPERATURE`, `PBZ=1.5 m` |
| 输出间隔 | 1 s |

DEVC 产生点时程 CSV，SLCF 产生平面场数据；二者回答不同问题。切片必须位于网格内部。

### 步骤 6：校验

菜单：`仿真 → Validate Model`

必须清除所有 Error。双击问题可按 UUID 定位业务对象和字段。常见错误包括重复 FDS ID、丢失引用、火源 VENT 没有宿主面或切片在网格外。

### 步骤 7：保存与重开

菜单：`文件 → 另存为`

保存到：`D:\FireCAE\docs\acceptance\pyrosim-workflow\projects\first_fire.firecae`。关闭并重新打开，确认 UUID、模型树和 Record View 不变。

### 步骤 8：生成 FDS

菜单：`文件 → Export FDS Input`

导出到：`D:\FireCAE\docs\acceptance\pyrosim-workflow\exported-fds\first_fire.fds`。在 Record View 搜索 `&MESH`、`&REAC`、`&SURF`、`&VENT`、`&DEVC`、`&SLCF` 和 `&TAIL`。

### 步骤 9：求解

菜单：`仿真 → Run Current Project`

第一次使用串行 CPU。检查任务中心中的命令预览、工作目录、FDS 路径和标准输出。正常完成必须产生 `.smv`、`_hrr.csv`、`_devc.csv` 和切片数据。运行失败时先查看错误输出，不要删除校验规则。

### 步骤 10：后处理和报告

1. `结果 → Open FDS Results` 打开 `first_fire.smv`。
2. 在 Results 中播放、暂停、拖动时间滑块。
3. 在 CSV 分类选择 HRR 和 TEMPERATURE 曲线；检查最大值、最小值和平均值。
4. 选择温度 Slice；需要权威体渲染时启动外部 Smokeview。
5. 导出 CSV、PNG、AVI 和比较报告。

预期：HRR 向设定值发展；顶部开放边界允许烟气排出；1.5 m 温度随羽流经过而升高。数值结果必须以实际 FDS 输出为准，教程不预先伪造结果。

## 3. 截图与验收证据

- 教程引导入口：`docs/acceptance/screenshots/P26-01-guided-first-fire.png`
- 最终工程：`docs/acceptance/pyrosim-workflow/results/first_fire/first_fire.firecae`
- 实际 FDS 输入：`docs/acceptance/pyrosim-workflow/results/first_fire/first_fire.fds`
- 实际求解输出：`docs/acceptance/pyrosim-workflow/results/first_fire/first_fire.out`、`first_fire.smv`、`first_fire_hrr.csv`、`first_fire_devc.csv`
- Results 导出：`docs/acceptance/pyrosim-workflow/results/first_fire/first_fire-visible.csv`、`first_fire-results.png`
- 逐步截图目录：`docs/acceptance/pyrosim-workflow/screenshots/first-fire/`
  - `P27-first-fire-01-blank.png`：空工程
  - `P27-first-fire-02-settings.png`：工程设置
  - `P27-first-fire-03-modeled.png`：GUI 建模完成
  - `P27-first-fire-04-exported.png`：校验和导出
  - `P27-first-fire-05-solved.png`：FDS 正常完成
  - `P27-first-fire-06-results.png`：Results 曲线与播放
  - `P27-first-fire-07-smokeview.png`：Smokeview 场结果

本机 P27 实测：FDS 6.11.1 正常计算至 `30.0 s`；HRR 共 31 个采样点，最终值 `497.63843 kW`，峰值 `501.31335 kW`，平均值 `473.181403 kW`；FDS 墙钟时间 `39.504 s`。这些数值来自上述实际输出文件，不是教程预设值。
