# Native FDS / FireCAE / PyroSim 三方证据入口

本目录用于接收同一算例由三条独立工作流产生的证据。它不会因为目录名或界面标签写了 `PyroSim` 就把结果认定为 PyroSim 证据。

## 目录约定

```text
three-way/
  comparison-manifest.json
  native-fds/
    case.fds
    case.smv
    case.out
    ...result files
  firecae/
    case.firecae
    case.fds
    case.smv
    case.out
    ...result files
  pyrosim/
    case.psm
    case.fds
    case.smv
    case.out
    ...result files
  reports/
```

要求：

1. 三个结果目录必须独立，不能让同一个物理 `.fds` 或 `.smv` 文件同时充当多个来源；
2. FireCAE 来源必须保留 `.firecae` 工程；
3. PyroSim 来源必须保留 `.psm` 工程、PyroSim 导出的 FDS 输入、实际求解输出和导出时间；
4. 三份 FDS 输入内容可以相同；这正是输入等价性验收的理想结果。系统会记录相同 SHA-256，同时保留各自独立路径；
5. 哈希用于防止验收后文件被无声替换，但不能单独构成“由某软件导出”的密码学证明。因此还需保留工程文件和过程截图；
6. 缺少任一真实来源时，报告必须是 `PENDING_EVIDENCE`，不能写成 PASS。

## 运行

```powershell
D:\FireCAE\build\release\FireCAEComparisonReporter.exe `
  --three-way-manifest D:\FireCAE\docs\acceptance\pyrosim-workflow\three-way\comparison-manifest.json `
  --output-directory D:\FireCAE\docs\acceptance\pyrosim-workflow\three-way\reports `
  --base-name p30-three-way-status
```

退出码：`0=PASS`、`6=PENDING_EVIDENCE`、`7=INVALID_EVIDENCE/INVALID_MANIFEST`、`8=COMPARISON_FAILED`。

当前尚未使用 PyroSim GUI 生成独立 `.psm` 和输出；这是遵守“暂时不接管鼠标键盘”的结果，不是程序故障。待允许控制后，将在本目录补齐真实证据并再次运行。
