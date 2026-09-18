# FireCAE P32 后剩余差距

FireCAE 0.3.0 已能完成代表性 FDS 前处理、CPU 求解和后处理，但以下项目仍未完成，因此不能称为“完全实现 PyroSim”或“100% 功能一致”。

## P0

1. **独立 PyroSim 三方证据**：P30 已完成证据清单、哈希、路径去重、三组配对比较和防误报状态机；目前仍没有由 PyroSim 2023.3 独立建模、导出、求解的 `.psm/.fds/.smv/.out`，所以真实状态是 `PENDING_EVIDENCE`，不是 PASS。入口见 `three-way/README.md`。

## P1

1. **原生场结果渲染**：P31 已完成 classic/uncompressed SLCF 的大小端解码、真实 FDS 数据验证和二维 PNG 色彩图；compressed slice、Smoke3D、BNDF、PART、ISOF、Plot3D 及 GPU 渲染仍未齐。按照当前产品决定，Smokeview 仍是默认权威显示器，原生查看器没有重新暴露给普通用户。
2. **Species/Particles 专业深度**：P32 已完成喷淋/水喷嘴联合向导、`SPEC→PART→PROP→TABL→DEVC` UUID 链、粒子输出、原子撤销和真实 FDS 求解。Rosin–Rammler 粒径分布预览、破碎/碰撞、复杂多组分液滴、喷头数据库和批量布置仍需扩充。
3. **复杂控制表达式**：基础与/或/非、延时和 UUID 受控对象已实现，复杂数学表达式仍主要依赖高级参数页。
4. **深度 BIM 数据**：复杂 IFC 主链路已通过，但完整 Property Set 属性图谱浏览/编辑、分类映射规则库和大模型后台搜索索引仍可增强。
5. **几何建模深度**：复杂多墙自动修剪、约束尺寸系统和完整建筑布尔修复不等同于成熟 BIM 建模器。

## P2 / 工程化

1. 第二台物理 Windows 机器和更多缩放/显卡组合尚未验收；
2. 没有数字签名安装器，当前入口为便携 Release 目录；
3. `D:\FireCAE` 没有 Git 元数据，不能提供提交历史和可追溯版本标签；
4. 当前没有 GPU FDS 求解器；界面已正确隐藏 GPU 运行入口；
5. Restart 的跨目录检查点选择、复制和长时中断恢复向导仍需增强；
6. GLB 多材质/纹理、DXF 全实体和 CAD 大模型简化仍有限；未实现的 FBX/DAE/DWG 不在生产文件过滤器中。

## 推荐后续顺序

补齐真实 PyroSim 三方基线 → 其余场格式解码 → 粒径分布/喷头数据库与批量布置 → 第二台机器与签名安装器。
