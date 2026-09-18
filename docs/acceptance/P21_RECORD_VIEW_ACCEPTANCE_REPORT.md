# FireCAE P21 — FDS Record View 与双向定位验收报告

验收日期：2026-08-30  
构建：`D:\FireCAE\build\debug\FireCAE.exe`  
专项测试：`FireCAEP21RecordSourceMapUiTests`、`FireCAEFdsModelTests`

## 1. 已交付能力

- “FDS Record”工作区直接从当前 `FcDocument/FcProject` 生成只读 FDS 输入；对象树发生生产变更后，记录视图跟随刷新。
- 编辑器通过公共前缀/后缀比较只替换变化文本区间，保持未变化文本、光标和滚动位置；高级草稿编辑期间不会被后台刷新覆盖。
- `FdsWriter` 为生成行保存一基行号、零基字段列范围、`FcObject UUID` 与参数键。MESH、REAC、SURF、OBST、VENT、输出以及通用 namelist 均进入 Source Map。
- 点击 FDS 行或字段会通过 UUID 调用 `FcDocument::findObject(UUID)`，同步选择模型树并显示属性；字段范围优先于整行对象范围。
- 校验消息包含对象 UUID；Validation 列表把 UUID 和参数键分别保存到 `Qt::UserRole` 与 `Qt::UserRole + 1`。单击定位对象，双击打开业务编辑器并聚焦具体 Schema 字段或高级参数行。
- 记录视图提供行号、FDS namelist/参数/字符串/数值/注释高亮、前后搜索、复制、等宽字体和禁止自动换行的只读预览。
- “高级编辑（有风险）”必须经过确认。编辑内容仅是临时草稿，不修改 UUID 对象模型，也不替换常规导出；用户只能把草稿单独另存为 `.fds`。退出高级模式后恢复对象模型生成的正式文本。
- FDS 导入器把未识别 namelist 放入 Configuration 下的 `Additional Records` UUID 对象组；未知关键字、ID 和参数仍可在通用编辑器中查看、编辑和导出。
- 未知记录经过 `.firecae` 保存/重开后仍参与按原序列号导出，不会被静默丢弃。

## 2. Source Map 规则

- 行号从 1 开始，列范围从 0 开始并采用左闭右开区间。
- 每个有业务来源的记录至少有一条整行对象映射；已知参数另有更精确的字段映射。
- `HEAD` 的 CHID/TITLE 和标准 `TIME T_END` 当前属于项目级属性，不伪造对象 UUID。
- 模型校验失败时，错误定位由 Validation 列表承担；错误文本不会再插入记录预览，因此不会破坏 Source Map 行号。

## 3. 未知记录保留边界

- P21 保证未知 namelist 及其 ID/参数的语义保留、工程持久化和再次导出。
- 注释、空白和原始换行格式会被规范化，不能把该能力描述为逐字节文本无损；这不会造成未知记录或参数的静默丢失。
- 已识别对象引用继续保存 UUID，导出时才解析为 FDS ID；未知参数则保持原始值或字符串值。

## 4. 自动化验收

`FireCAEP21RecordSourceMapUiTests` 覆盖：

1. FDS Record 页、行号区、只读/无换行、搜索、前后定位和复制；
2. MESH 语法高亮；
3. 点击 `IJK` 字段后取得 Source Map UUID，并选中同 UUID 模型树节点；
4. 高级模式风险确认、草稿可编辑、退出后恢复正式生成文本；
5. 导入未知 `&ZZZZ` 记录，在 `Additional Records` 显示并重新导出；
6. 保存/重开工程后未知记录仍存在；
7. 无 `IJK` 的非法 MESH 生成带 UUID/字段键的错误，点击错误定位对象。

`FireCAEFdsModelTests` 额外验证：

- `IJK` 文本列范围精确映射到 MESH UUID；
- 未知 `&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /` 导入、导出及工程重开保持。

结果：PASS。

## 5. 全量回归证据

- Debug 全量 CTest：17/17 PASS，0 failed。
- 总耗时：198.72 s。
- 七教程从空工程逐对象 GUI 重建：PASS（116.70 s）。
- P18 工作区、P19 楼层绘图、P20 专业编辑器、A10/A12–A16、FDS 模型/场景/运行器全部通过。

## 6. 阶段边界

- P21 不允许用高级文本草稿反向覆盖业务对象树；要实现安全的文本反解析与冲突合并，需要单独的数据迁移设计。
- P21 未扩展 IFC/CAD 后台任务、构件过滤、取消与失败构件报告；这些进入 P22。
