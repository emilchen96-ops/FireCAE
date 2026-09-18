# FireCAE P28 — FDS 6.11.1 Schema 与求解器版本一致性验收报告

验收日期：2026-09-03  
工程：`D:\FireCAE`  
目标运行时：FDS 6.11.1

## 1. 事实来源与边界

本阶段以 Firemodels 官方发布和源码为准，而不是依据 PyroSim UI 文案猜测字段：

- [NIST FDS/Smokeview manuals](https://pages.nist.gov/fds/manuals.html)
- [Firemodels FDS releases](https://github.com/firemodels/fds/releases)
- `FDS-6.10.1/Source/read.f90`
- `FDS-6.11.1/Source/read.f90`

P28 对齐 FireCAE **已经注册的 namelist 家族**在 FDS 6.11.1 的新增字段、类型与版本可见性。未知字段仍由现有无损往返机制保留。该结论不等于 FireCAE 已为 FDS 每个实验性字段提供专业 GUI。

## 2. 已交付能力

### 2.1 版本模型

- 新工程默认 Schema 从 6.10 改为 6.11.1；
- 可选版本为 6.7、6.8、6.9、6.10、6.11、6.11.1；
- 能从 `FDS-6.11.1-0-gff928db-release` 等 revision 文本提取版本；
- 6.10.1 可映射到同一兼容线 6.10；未知未来版本 6.12 不会被假定兼容；
- 6.11 字段带 `sinceVersion=6.11`，不会泄漏到 6.10 工程。

### 2.2 当前注册记录的 6.11.1 增量

| Namelist | P28 新增字段 |
|---|---|
| SURF | `DELAMINATION_DENSITY`、`DELAMINATION_TMP`、`HT3D_WEIGHT`、`MINIMUM_LAYER_MASS_FRACTION`、`MOISTURE_CONTENT`、`NODE_ID`、`SKIP_INRAD`、`VEG_LSET_WIND_HEIGHT`、`VEG_LSET_WIND_RAMP` |
| PROP | `CALIBRATION_CONSTANT`、`IGNITION_ZONE`、`PROBE_DIAMETER`、`SPECIFIC_HEAT_RAMP`、`TC` |
| RAMP | `CYCLING` |
| DEVC | `ELEM_ID` |
| SLCF | `DRY` |
| DUMP | `BINGEOM_DIR`、`DECIMAL_SPECIFIER`、`WRITE_CVODE_SUBSTEPS` |
| MISC | `PR_T`、`SC_T`、`TEST_NEW_KSGS_MODEL` |
| PRES | `MAX_PREDICTOR_PRESSURE_ITERATIONS`、`WRITE_PARCSRPCG_MATRIX` |
| RADI | `RANDOMIZE_RADIATION_DIRECTIONS` |
| COMB | `CVODE_ORDER`、`FUEL_ID_FOR_AFT`、`RAMP_ZETA_0`、`TURBULENT_FLAME_SPEED`、`USE_MIXED_ZN_AFT_TMP`、`VARIABLE_CFT` |

可选新字段默认保持空值，避免仅因打开旧工程就改变导出的 FDS 语义。

### 2.3 实际求解器探测

- `FdsRunner::probeVersion` 使用正式启动计划的 executable、PATH 和环境执行无窗口 `-v`；
- 运行对话框显示“Solver 6.11.1; project Schema 6.11.1”；
- 求解器与工程 Schema 不一致时显示非阻断告警；
- 探测结果按可执行文件缓存，避免反复启动版本查询进程。

### 2.4 旧工程迁移

- 新格式保存显式 `fdsVersion=6.11.1`；
- 缺少 `fdsVersion` 的历史工程继续以 6.10 兼容 Schema 打开，并在消息区记录告警；
- 可映射的补丁版本归一到同一兼容 Schema；
- 无法验证的未来版本保留原值并告警，不静默降级或伪称兼容。

## 3. 自动化证据

- Release 增量构建：PASS；
- P28 直接影响测试：5/5 PASS（P23、P27 First Fire、P27 Complex IFC、FDS Model、FDS Runner）；
- 迁移补充测试：3/3 PASS（P23、FDS Model、FDS Runner）；
- FDS Model 检查 6.10/6.11 字段隔离、版本解析、当前工程无迁移告警、旧工程 6.10 回退告警；
- FDS Runner 对随包求解器执行真实版本探测并断言 `6.11.1`；
- P27 两个生产 GUI 链路在版本升级后仍能保存、重开、导出和求解。

完整 Release 回归：24/24 PASS，0 failed，179.33 s。

## 4. 验收结论

P28 消除了“运行时是 6.11.1、工程 Schema 却最多为 6.10”的事实冲突，并为旧工程提供了可见、保守的迁移路径。下一阶段可以在这一版本基线上继续补齐其余五个教程的实际 GUI 建模与求解证据。
