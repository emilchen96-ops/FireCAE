#include "ui/TutorialGuideWidget.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsWriter.h"
#include "ui/UiLanguage.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{
QString trUi(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

TutorialStepDefinition step(const QString& title,
                            const QString& menuPath,
                            const QString& action,
                            const QString& instructions,
                            const QString& parameters,
                            const QString& meaning,
                            const QString& rationale,
                            const QString& expected,
                            const QString& error,
                            TutorialCheckKind check,
                            const QMap<QString, int>& keywords = {},
                            int minimum = 0)
{
    return {title, menuPath, action, instructions, parameters, meaning, rationale,
            expected, error, check, keywords, minimum};
}

QVector<TutorialStepDefinition> closingSteps()
{
    return {
        step(QStringLiteral("校验模型"), QStringLiteral("仿真 → 校验模型"),
             QStringLiteral("ValidateModelAction"),
             QStringLiteral("执行完整模型校验，双击任何问题定位到 UUID 对象与字段。"),
             QStringLiteral("不需要额外参数；必须清除所有 Error。"),
             QStringLiteral("校验检查网格、范围、FDS ID、UUID 引用和记录兼容性。"),
             QStringLiteral("在求解前发现输入错误，避免浪费计算时间。"),
             QStringLiteral("状态栏显示校验通过，Record View 可生成完整 FDS。"),
             QStringLiteral("重复 FDS ID、引用对象已删除、输出平面位于网格外。"),
             TutorialCheckKind::ModelValid),
        step(QStringLiteral("保存工程"), QStringLiteral("文件 → 另存为"),
             QStringLiteral("SaveProjectAsAction"),
             QStringLiteral("将可编辑工程保存为 `.firecae`，然后关闭并重新打开一次。"),
             QStringLiteral("选择有写入权限且路径尽量简短的算例目录。"),
             QStringLiteral("`.firecae` 保存业务对象、稳定 UUID、场景差异和运行设置。"),
             QStringLiteral("保存/重开是验证工程可重现性的必要步骤。"),
             QStringLiteral("标题栏不再显示未保存标记，工程路径有效。"),
             QStringLiteral("不要把 `.fds` 当成可编辑的 FireCAE 工程文件。"),
             TutorialCheckKind::ProjectSaved),
        step(QStringLiteral("生成 FDS"), QStringLiteral("文件 → 导出 FDS"),
             QStringLiteral("ExportFdsAction"),
             QStringLiteral("导出 `.fds`，在 FDS Record View 中搜索 CHID、MESH 和关键输出记录。"),
             QStringLiteral("导出文件建议与工程同目录，文件名与 CHID 一致。"),
             QStringLiteral("FDS 输入是求解器的最终契约；UUID 引用在此处解析为 FDS ID。"),
             QStringLiteral("导出前审阅记录能确认 GUI 参数语义。"),
             QStringLiteral("生成非空 `.fds`，且工程校验仍通过。"),
             QStringLiteral("如导出失败，先从验证问题列表定位，不要直接手改生成文件来隐藏错误。"),
             TutorialCheckKind::FdsExported),
        step(QStringLiteral("运行 FDS"), QStringLiteral("仿真 → 运行当前工程"),
             QStringLiteral("RunFdsAction"),
             QStringLiteral("在任务中心确认命令、工作目录和环境检查，启动 CPU 串行求解。"),
             QStringLiteral("入门先使用串行；多网格算例再选 CPU/MPI 和合理进程数。"),
             QStringLiteral("求解器将输入离散成网格单元上的瞬态质量、动量和能量方程。"),
             QStringLiteral("先看环境检查和命令预览，可区分模型错误与运行库问题。"),
             QStringLiteral("任务中心显示已完成（Completed），本次 OUT 确认达到教程规定的 T_END，输出目录保留本次 `.smv` 及按请求生成的数据。“检查步骤”仅作人工确认，不证明求解完成或达到终点。"),
             QStringLiteral("FDS 路径缺失、CHID 与文件名不一致、MPI 进程数与网格数不合理。"),
             TutorialCheckKind::Manual),
        step(QStringLiteral("加载与查看结果"), QStringLiteral("结果 → 打开 FDS 结果"),
             QStringLiteral("OpenResultsAction"),
             QStringLiteral("在任务中心选择本次已完成任务并点击“打开结果”，或通过“结果 → 打开 FDS 结果”选择本次输出目录的 `.smv`。进入 Smokeview 后，载入本教程已请求且实际生成的场数据，再用上方播放、暂停和逐帧按钮观察。点击“返回建模”，在任务中心点击“打开输出目录”，按本次实际生成的 CSV 种类核对数值。"),
             QStringLiteral("确认结果的 CHID 对应本次运行；按所请求的 CSV 种类和文件表头单位核对时间范围与物理量。采样时程与事件记录分别检查，不能用最后事件时间代替 T_END。"),
             QStringLiteral("烟气、温度、流速和设备时程是对模型假设的数值响应。"),
             QStringLiteral("场数据用于查看时间变化与空间分布；CSV 用于核对已请求物理量的采样数值或事件记录。"),
             QStringLiteral("结果算例已加载；已请求的场数据可在 Smokeview 中查看，输出目录保留本次实际生成的 CSV。“检查步骤”只确认结果加载，场数据播放与数值仍需人工核对。"),
             QStringLiteral("如已请求场输出但 Smokeview 只显示几何，先载入本次实际生成的场数据，再检查求解是否正常结束；缺少某类 CSV 时，先核对模型是否请求了相应输出。"),
             TutorialCheckKind::ResultsLoaded)};
}

TutorialDefinition guided(const QString& id,
                          const QString& title,
                          const QString& purpose,
                          const QString& effect,
                          const QString& prerequisites,
                          QVector<TutorialStepDefinition> setup,
                          const QString& finalHint)
{
    setup.prepend(step(QStringLiteral("从空工程开始"),
                       QStringLiteral("文件 → 新建工程"),
                       QStringLiteral("NewProjectAction"),
                       QStringLiteral("确认当前模型树中没有 FDS 业务对象。引导模式不会替你注入预制模型。"),
                       QStringLiteral("工程名、CHID 和结束时间将在下一步设置。"),
                       QStringLiteral("空工程是确认所有对象确实由正式 GUI 创建的基线。"),
                       QStringLiteral("从零开始能发现丢失的对象类型、引用和默认值。"),
                       QStringLiteral("模型树只有空分类节点。"),
                       QStringLiteral("如已有未保存工程，先保存或取消启动教程。"),
                       TutorialCheckKind::BlankProject));
    const QVector<TutorialStepDefinition> close = closingSteps();
    setup += close;
    return {id, title, purpose, effect, prerequisites, finalHint, setup};
}

QVector<TutorialDefinition> buildCatalog()
{
    QVector<TutorialDefinition> tutorials;
    tutorials.append(guided(
        QStringLiteral("first_fire"), QStringLiteral("Your First Fire / 第一个火灾算例"),
        QStringLiteral("从空工程建立一个带火源、开放边界和温度/HRR 输出的房间。"),
        QStringLiteral("完成后可生成并运行 FDS，在 Smokeview 查看场结果，并从输出目录核对 HRR 和温度设备 CSV。"),
        QStringLiteral("已配置 FDS/Smokeview；熟悉米、秒、kW 基本单位。"),
        {
            step(QStringLiteral("设置工程"), QStringLiteral("模型 → 项目设置"),
                 QStringLiteral("ProjectSettingsAction"),
                 QStringLiteral("输入标题 `Your First Fire`、CHID `first_fire`、结束时间 30 s。"),
                 QStringLiteral("Title=Your First Fire; CHID=first_fire; End Time=30 s"),
                 QStringLiteral("CHID 是输入与所有结果文件的稳定前缀；结束时间定义模拟时窗。"),
                 QStringLiteral("30 s 足够观察入门算例的烟气发展，同时保持较短求解时间。"),
                 QStringLiteral("标题栏显示 Your First Fire，CHID 为 first_fire。"),
                 QStringLiteral("CHID 只用字母、数字和下划线，不要包含空格或中文。"),
                 TutorialCheckKind::ProjectConfigured),
            step(QStringLiteral("创建网格"), QStringLiteral("模型 → 网格"),
                 QStringLiteral("MeshAction"),
                 QStringLiteral("用专业网格表单定义 X=0–6 m、Y=0–4 m、Z=0–3 m，目标单元尺寸 0.20 m。"),
                 QStringLiteral("X min/max=0/6; Y min/max=0/4; Z min/max=0/3; target dx=0.20 m（约 30×20×15=9000 单元）"),
                 QStringLiteral("网格是 FDS 求解域；单元尺寸决定几何分辨率、流场精度和成本。"),
                 QStringLiteral("0.20 m 用于快速教学，正式设计应使用 D*/dx 分辨率助手并做网格无关性分析。"),
                 QStringLiteral("3D 视图显示完整包围房间的网格框。"),
                 QStringLiteral("不要将单元数 IJK 误当成几何坐标 XB。"),
                 TutorialCheckKind::KeywordMinimums, {{QStringLiteral("MESH"), 1}}),
            step(QStringLiteral("创建反应、表面和火源"),
                 QStringLiteral("模型 → 专业向导 → 火源向导"),
                 QStringLiteral("FireSourceWizardAction"),
                 QStringLiteral("创建 PROPANE 反应；在地面中心建 1 m×1 m 火源，总 HRR=500 kW，点火 0 s。"),
                 QStringLiteral("Fuel=PROPANE; fire area=1 m²; Total HRR=500 kW; HRRPUA=500 kW/m²; soot yield=0.01; radiative fraction=0.35"),
                 QStringLiteral("REAC 定义燃料化学和产物；SURF/VENT 把热释放率赋给空间中的面。"),
                 QStringLiteral("指定总 HRR 时向导会根据面积计算 HRRPUA，避免两者冲突。"),
                 QStringLiteral("模型树出现 REAC、SURF 及火源 VENT/OBST，引用以 UUID 保存。"),
                 QStringLiteral("火源 VENT 必须位于固体面或网格边界；辐射份额应在 0–1 之间。"),
                 TutorialCheckKind::KeywordMinimums,
                 {{QStringLiteral("REAC"), 1}, {QStringLiteral("SURF"), 1},
                  {QStringLiteral("VENT"), 1}}),
            step(QStringLiteral("开放边界与输出"),
                 QStringLiteral("模型 → 通风口；模型 → 专业向导 → 输出向导；仿真 → 仿真参数 → 输出与续算"),
                 QStringLiteral("OutputWizardAction"),
                 QStringLiteral("先用通风口表单创建顶部 OPEN 边界；再分别打开输出向导创建温度点设备和温度切片。最后在仿真参数的“输出与续算”页启用自定义输出频率，将设备 CSV、HRR CSV 和切片间隔设为 1 s。“打开当前工具”在此步打开输出向导。"),
                 QStringLiteral("Open VENT: XB=0,6,0,4,3,3; SURF_ID=OPEN; DEVC quantity=TEMPERATURE, XYZ=3,2,1.5; SLCF quantity=TEMPERATURE, PBZ=1.5; DT_DEVC=DT_HRR=DT_SLCF=1 s"),
                 QStringLiteral("开放边界允许质量和热量进出；DEVC 是点数据，SLCF 是平面场数据。"),
                 QStringLiteral("同时保留点数据 CSV 和平面场数据，便于核对时间变化与空间分布。"),
                 QStringLiteral("模型树包含至少两个 VENT、1 个 DEVC 和 1 个 SLCF。"),
                 QStringLiteral("切片平面必须在网格范围内；OPEN 表面不应赋给内部障碍物。"),
                 TutorialCheckKind::KeywordMinimums,
                 {{QStringLiteral("VENT"), 2}, {QStringLiteral("DEVC"), 1},
                  {QStringLiteral("SLCF"), 1}})
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    tutorials.append(guided(
        QStringLiteral("basic_data_output"), QStringLiteral("Basic Data Output / 基础数据输出"),
        QStringLiteral("学习点设备、切片、矢量切片、边界量、等值面和 Plot3D 的选择原则。"),
        QStringLiteral("完成后可在 Smokeview 查看已请求的场结果，并从输出目录核对设备 CSV 的物理量、单位和时间范围。"),
        QStringLiteral("已完成 Your First Fire 或具备网格和火源基础。"),
        {step(QStringLiteral("建立计算域与源项"), QStringLiteral("模型 → 网格；模型 → 专业向导 → 火源向导"),
              QStringLiteral("MeshAction"), QStringLiteral("创建至少一个 MESH 和一个有效火源。"),
              QStringLiteral("网格尺寸与算例相符；火源 HRRPUA>0。"),
              QStringLiteral("没有流场和源项就无法解释输出。"),
              QStringLiteral("先建立物理基线，然后再比较不同输出。"),
              QStringLiteral("MESH、SURF 和 VENT 存在。"),
              QStringLiteral("输出量名称必须与 FDS 6.11.1 兼容。"),
              TutorialCheckKind::KeywordMinimums,
              {{QStringLiteral("MESH"), 1}, {QStringLiteral("SURF"), 1},
               {QStringLiteral("VENT"), 1}}),
         step(QStringLiteral("配置多类输出"), QStringLiteral("模型 → 专业向导 → 输出向导"),
              QStringLiteral("OutputWizardAction"),
              QStringLiteral("添加 TEMPERATURE 点设备、TEMPERATURE 切片、VELOCITY 矢量切片、WALL TEMPERATURE 边界量和温度等值面。"),
              QStringLiteral("DEVC XYZ 在网格内; SLCF PBX/PBY/PBZ 选一; VECTOR=.TRUE.; BNDF quantity=WALL TEMPERATURE; ISOF levels 按预期范围设置"),
              QStringLiteral("不同结果类型回答点、面、边界或三维区域的不同问题。"),
              QStringLiteral("只输出解析需要的数据，避免过大结果文件。"),
              QStringLiteral("输出分类中至少出现 DEVC、SLCF、BNDF 和 ISOF。"),
              QStringLiteral("不要将输出间隔设得远小于流场特征时间，否则磁盘开销会急剧增加。"),
              TutorialCheckKind::KeywordMinimums,
              {{QStringLiteral("DEVC"), 1}, {QStringLiteral("SLCF"), 1},
               {QStringLiteral("BNDF"), 1}, {QStringLiteral("ISOF"), 1}})
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    tutorials.append(guided(
        QStringLiteral("importing_geometry"), QStringLiteral("Importing Geometry / 导入几何"),
        QStringLiteral("通过导入向导检查单位、坐标、简化和 FDS 转换策略。"),
        QStringLiteral("完成后 CAD/IFC 参考几何可见，并有一部分被转换为可求解 FDS 对象。"),
        QStringLiteral("准备 IFC/STL/OBJ/GLB/STEP/IGES/DXF 之一；了解源文件单位。"),
        {step(QStringLiteral("导入并检查几何"), QStringLiteral("几何 → 导入几何"),
              QStringLiteral("ImportGeometryAction"),
              QStringLiteral("选择文件，在向导中确认单位、轴向、原点、简化和类型过滤，导入后检查包围盒。"),
              QStringLiteral("Scale=按源单位; translation/rotation=按建筑坐标; IFC 按楼层与类型过滤; 大模型启用简化"),
              QStringLiteral("坐标与单位决定几何是否与网格重合；简化决定显示和转换成本。"),
              QStringLiteral("先作参考几何检查，再选择性转换，避免把整个 BIM 盲目体素化。"),
              QStringLiteral("模型树出现带 UUID 的几何/IFC 根对象，3D 包围盒尺寸正确。"),
              QStringLiteral("常见错误是 mm 被当成 m，或 BIM 大坐标导致浮点精度/视图定位异常。"),
              TutorialCheckKind::GeometryImported),
         step(QStringLiteral("转换为 FDS 对象"), QStringLiteral("几何 → 预览/转换 FDS 块"),
              QStringLiteral("ConvertGeometryToFdsAction"),
              QStringLiteral("选中墙/板/门窗，分别选择 OBST、HOLE、GEOM、忽略或仅参考策略，使用预览检查网格吸附。"),
              QStringLiteral("墙/楼板通常 OBST; 门窗开口为 HOLE; 复杂斜面可 GEOM; 装饰细节忽略"),
              QStringLiteral("FDS 传统障碍物最终按网格体素表示，复杂外形与小特征可能被改变。"),
              QStringLiteral("明确转换策略才能把 BIM 语义变成可校验的 FDS 记录。"),
              QStringLiteral("几何仍保留为参考，模型树另外出现 OBST/HOLE/GEOM 等业务对象。"),
              QStringLiteral("转换前先创建网格；检查比单元尺寸更小的薄壁和孔洞。"),
              TutorialCheckKind::KeywordMinimums, {{QStringLiteral("OBST"), 1}})
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    tutorials.append(guided(
        QStringLiteral("materials_layered_surfaces"), QStringLiteral("Materials and Layered Surfaces / 材料与分层表面"),
        QStringLiteral("创建物性材料，按层组合表面并赋给固体几何。"),
        QStringLiteral("完成后墙体具有可导出的多层热传导边界。"),
        QStringLiteral("准备材料的密度、导热率、比热和层厚。"),
        {step(QStringLiteral("创建材料与分层表面"), QStringLiteral("模型 → 材料 / 表面"),
              QStringLiteral("MaterialAction"),
              QStringLiteral("创建石膏板和混凝土 MATL，再创建两层 SURF，按从暴露面到背面的顺序选择材料 UUID。"),
              QStringLiteral("Gypsum: density=800 kg/m³, k=0.17 W/(m·K), cp=1.09 kJ/(kg·K), thickness=0.012 m; Concrete: density=2280, k=1.8, cp=1.04, thickness=0.10 m"),
              QStringLiteral("热惯性取决于密度与比热，导热率和厚度决定热量穿透速率。"),
              QStringLiteral("多层次序与厚度必须对应实际构造，材料引用必须使用 UUID。"),
              QStringLiteral("至少两个 MATL 和一个 SURF，导出后 MATL_ID 与 THICKNESS 数组顺序一致。"),
              QStringLiteral("比热单位不要把 J/(kg·K) 与 FDS 表单中的 kJ/(kg·K) 混淆。"),
              TutorialCheckKind::KeywordMinimums,
              {{QStringLiteral("MATL"), 2}, {QStringLiteral("SURF"), 1}})
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    tutorials.append(guided(
        QStringLiteral("fire_protection_controls"), QStringLiteral("Fire Protection Systems and Controls / 消防系统与控制"),
        QStringLiteral("用设备、阈值、延时和激活/停用动作构建消防联动。"),
        QStringLiteral("完成后烟感/温感或喷淋会在计算中触发 VENT/设备状态变化。"),
        QStringLiteral("已有网格、火源和一个可控对象。"),
        {step(QStringLiteral("创建设备、控制和动作"), QStringLiteral("模型 → 专业向导 → 设备与控制向导"),
              QStringLiteral("DeviceControlWizardAction"),
              QStringLiteral("在天花下创建温度或烟感 DEVC，设定阈值与延时；创建 CTRL，将触发器和受控 VENT 通过 UUID 关联。"),
              QStringLiteral("Detector XYZ 在顶棚下 0.1–0.2 m; temperature threshold=68 °C 或 smoke detector template; delay=0–5 s; action=activate/deactivate target"),
              QStringLiteral("DEVC 产生状态，CTRL 组合逻辑，受控对象通过激活 ID 改变状态。"),
              QStringLiteral("在 GUI 选择触发器和目标，避免手输 ID 导致断引用。"),
              QStringLiteral("存在 DEVC、CTRL 及至少一个被控 VENT，Record View 可双向定位。"),
              QStringLiteral("控制循环和相互矛盾的激活/停用动作必须在逻辑图中清除。"),
              TutorialCheckKind::KeywordMinimums,
              {{QStringLiteral("DEVC"), 1}, {QStringLiteral("CTRL"), 1},
               {QStringLiteral("VENT"), 1}})
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    tutorials.append(guided(
        QStringLiteral("fire_design_scenarios"), QStringLiteral("Fire Design Scenarios / 火灾设计场景"),
        QStringLiteral("在同一 UUID 对象树上建立基准和参数变体。"),
        QStringLiteral("完成后可批量运行多场景，并对比实际 CSV 结果。"),
        QStringLiteral("已有一个可校验的基准工程和可覆盖的火源/边界对象。"),
        {step(QStringLiteral("创建基准与变体"), QStringLiteral("仿真 → 场景管理器"),
              QStringLiteral("ScenarioManagerAction"),
              QStringLiteral("把当前场景设为 Default，复制两个场景；修改 CHID/输出目录，用 UUID 覆盖 HRRPUA 或禁用一个输出对象。"),
              QStringLiteral("Baseline CHID=design_base; Variant A HRRPUA=250; Variant B HRRPUA=500; each output directory unique; solver/processes explicit"),
              QStringLiteral("场景只保存差异，业务对象 UUID 在所有场景间稳定。"),
              QStringLiteral("差异模型避免多份工程逐渐偏离，也能自动生成参数矩阵。"),
              QStringLiteral("场景列表至少有 3 项，每项 CHID/输出目录唯一。"),
              QStringLiteral("不同场景不得写入同一输出目录；禁用对象时不要破坏其他对象的必需引用。"),
              TutorialCheckKind::ScenarioMinimum, {}, 3)
        }, QStringLiteral("以“另存为”选择的实际 .firecae 路径为准")));

    struct ExistingTutorial {
        const char* id;
        const char* title;
        const char* purpose;
        const char* effect;
        const char* focus;
        QMap<QString, int> minimums;
    };
    const ExistingTutorial existing[] = {
        {"activate_vents", "activate_vents 通风口激活/停用", "学习 DEVC/CTRL 如何在时间和温度条件下改变 VENT 状态。", "可在输出中核对通风口动作时间。", "MESH、SURF、VENT、DEVC、CTRL 及 UUID 激活引用", {{"MESH",1},{"SURF",2},{"VENT",2},{"DEVC",2},{"CTRL",1}}},
        {"bucket_test_2", "bucket_test_2 喷淋桶试验", "学习喷淋 PROP/DEVC、热灵敏性和水滴粒子。", "可从本次设备输出核对喷淋启动，并在 Smokeview 查看已生成的粒子轨迹。", "MESH、PART、PROP、DEVC、VENT 和喷淋输出", {{"MESH",1},{"PART",1},{"PROP",1},{"DEVC",1},{"VENT",1}}},
        {"couch", "couch 沙发燃烧", "学习材料热解、分层表面、火源和长时间输出。", "可运行完整 600 s 沙发燃烧算例。", "MATL、SURF、REAC、OBST、VENT、DEVC、SLCF/BNDF", {{"MESH",1},{"MATL",1},{"SURF",2},{"REAC",1},{"OBST",1},{"VENT",1},{"DEVC",1},{"SLCF",1}}},
        {"couch_smoke_12s", "couch_smoke_12s 短时烟气", "在保留沙发物理对象的同时建立 12 s 快速 Smoke3D 验收算例。", "可快速生成烟气和温度切片，并在 Smokeview 加载查看。", "与 couch 同类对象，使用 12 s 时窗；按参数表创建 MULT、DUMP 和切片，无需另建 SM3D 对象", {{"MESH",1},{"MULT",1},{"DUMP",1},{"MATL",1},{"SURF",2},{"REAC",1},{"OBST",1},{"VENT",1},{"SLCF",1}}},
        {"HVAC_aircoil", "HVAC_aircoil 空气盘管", "学习 HVAC 节点、风管、风机/盘管和网络连通性。", "可从本次请求的 HVAC 输出核对流量与温度，并在“模型 → 专业向导 → HVAC 网络图”查看连接。", "MESH、HVAC NODE/DUCT/FAN/AIRCOIL、VENT 与 HVAC 输出", {{"MESH",1},{"HVAC",4},{"VENT",1}}},
        {"tunnel_demo", "tunnel_demo 多网格隧道", "学习 MULT 复制网格、隧道火源、边界和 MPI 分配。", "可生成八个展开网格的隧道算例。", "MESH+MULT、OBST、SURF、REAC、VENT、SLCF 及 MPI 负载", {{"MESH",1},{"MULT",1},{"OBST",1},{"SURF",1},{"REAC",1},{"VENT",1},{"SLCF",1}}},
        {"tunnel_smoke_10s", "tunnel_smoke_10s 短时隧道烟气", "建立 10 s 的多网格 Smoke3D/矢量切片验收算例。", "可在 Smokeview 加载本次生成的隧道烟气和速度矢量并播放。", "MESH+MULT、火源、DUMP、VECTOR SLCF 及 CPU/MPI 求解；按参数表创建，无需另建 SM3D 对象", {{"MESH",1},{"MULT",1},{"DUMP",1},{"SURF",1},{"REAC",1},{"VENT",1},{"SLCF",1}}}
    };
    for (const ExistingTutorial& item : existing) {
        tutorials.append(guided(
            QString::fromLatin1(item.id), QString::fromUtf8(item.title),
            QString::fromUtf8(item.purpose), QString::fromUtf8(item.effect),
            QStringLiteral("按照 FDS 手册中的公开算例参数；请使用专业编辑器，只在必要时进入高级参数。"),
            {step(QStringLiteral("逐项创建教程对象"),
                  QStringLiteral("模型 → 网格/材料/表面/反应/设备/控制/HVAC/输出"),
                  QStringLiteral("AddFdsObjectAction"),
                  QStringLiteral("依照教程的 GUI-RECONSTRUCTION 参数表，用生产对话框逐个创建对象；先创建被引用对象，再在引用选择器中选 UUID。"),
                  QStringLiteral("重点对象：%1。每个记录的完整参数见源码包内的 docs/acceptance/tutorials/blank-gui/%2/GUI-RECONSTRUCTION.md（相对源码根目录）。")
                      .arg(QString::fromUtf8(item.focus), QString::fromLatin1(item.id)),
                  QStringLiteral("对象分类表示 FDS 物理记录，UUID 是 FireCAE 编辑和重开时的唯一关联键。"),
                  QStringLiteral("逐项创建使你能理解每条 FDS 记录的作用，也是本教程的验收方式。"),
                  QStringLiteral("指定的各类记录均已出现，模型树与三维视图、FDS 记录视图一致。"),
                  QStringLiteral("不要通过“完成算例示例”入口替换空工程；该入口只用于核对最终答案。"),
                  TutorialCheckKind::KeywordMinimums, item.minimums)},
            QStringLiteral("建议另存为 %1.firecae；以你选择的实际保存路径为准")
                .arg(QString::fromLatin1(item.id))));
    }
    return tutorials;
}

const QVector<TutorialDefinition>& tutorialCatalog()
{
    static const QVector<TutorialDefinition> value = buildCatalog();
    return value;
}

void collectKeywordCounts(const FcObject::Ptr& object, QMap<QString, int>& counts,
                          int& fdsCount)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        ++counts[namelist->keyword().trimmed().toUpper()];
        ++fdsCount;
    }
    for (const FcObject::Ptr& child : object->children())
        collectKeywordCounts(child, counts, fdsCount);
}
}

TutorialGuideWidget::TutorialGuideWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TutorialGuideWidget"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("TutorialGuideScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 6, 0);
    contentLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto makeLabel = [content](const QString& name) {
        auto* label = new QLabel(content);
        label->setObjectName(name);
        label->setWordWrap(true);
        // Long file paths must not impose their unwrapped width on the dock.
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        return label;
    };
    m_title = makeLabel(QStringLiteral("TutorialGuideTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 3);
    m_title->setFont(titleFont);
    contentLayout->addWidget(m_title);
    m_overview = makeLabel(QStringLiteral("TutorialGuideOverview"));
    m_overview->setTextFormat(Qt::RichText);
    contentLayout->addWidget(m_overview);
    m_steps = new QListWidget(content);
    m_steps->setObjectName(QStringLiteral("TutorialGuideStepList"));
    m_steps->setMinimumHeight(m_steps->fontMetrics().lineSpacing() * 3 + 8);
    m_steps->setMaximumHeight(m_steps->fontMetrics().lineSpacing() * 5 + 8);
    contentLayout->addWidget(m_steps);
    m_stepTitle = makeLabel(QStringLiteral("TutorialGuideStepTitle"));
    QFont stepFont = m_stepTitle->font();
    stepFont.setBold(true);
    m_stepTitle->setFont(stepFont);
    contentLayout->addWidget(m_stepTitle);
    m_stepDetails = makeLabel(QStringLiteral("TutorialGuideStepDetails"));
    m_stepDetails->setTextFormat(Qt::RichText);
    m_stepDetails->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    contentLayout->addWidget(m_stepDetails);
    m_checkStatus = makeLabel(QStringLiteral("TutorialGuideCheckStatus"));
    contentLayout->addWidget(m_checkStatus);
    contentLayout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // Keep every action accessible while long instructions scroll above it.
    // Two columns also fit a narrow dock at larger Windows display scales.
    auto* navigation = new QGridLayout;
    m_back = new QPushButton(trUi("Previous Step"), this);
    m_back->setObjectName(QStringLiteral("TutorialGuideBackButton"));
    m_openTool = new QPushButton(trUi("Open Next Tool"), this);
    m_openTool->setObjectName(QStringLiteral("TutorialGuideOpenToolButton"));
    m_check = new QPushButton(trUi("Check Step"), this);
    m_check->setObjectName(QStringLiteral("TutorialGuideCheckButton"));
    m_next = new QPushButton(trUi("Next"), this);
    m_next->setObjectName(QStringLiteral("TutorialGuideNextButton"));
    m_restart = new QPushButton(trUi("Start Over"), this);
    m_restart->setObjectName(QStringLiteral("TutorialGuideRestartButton"));
    m_exit = new QPushButton(trUi("Exit Tutorial"), this);
    m_exit->setObjectName(QStringLiteral("TutorialGuideExitButton"));
    navigation->addWidget(m_back, 0, 0);
    navigation->addWidget(m_next, 0, 1);
    navigation->addWidget(m_openTool, 1, 0);
    navigation->addWidget(m_check, 1, 1);
    navigation->addWidget(m_restart, 2, 0);
    navigation->addWidget(m_exit, 2, 1);
    root->addLayout(navigation);

    connect(m_steps, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_tutorial || row < 0 || row >= m_tutorial->steps.size()) return;
        m_stepIndex = row;
        saveProgress();
        showCurrentStep();
    });
    connect(m_back, &QPushButton::clicked, this, [this]() { changeStep(-1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { changeStep(1); });
    connect(m_check, &QPushButton::clicked, this, [this]() {
        QString detail;
        const bool passed = currentStepComplete(&detail);
        m_checkStatus->setText(
            QStringLiteral("<b style='color:%1'>%2</b><br>%3")
                .arg(passed ? QStringLiteral("#188038") : QStringLiteral("#b3261e"),
                     passed ? QStringLiteral("✓ 当前步骤已通过")
                            : QStringLiteral("当前步骤尚未通过"),
                     detail.toHtmlEscaped()));
        m_next->setEnabled(passed && m_stepIndex + 1 < m_tutorial->steps.size());
        if (passed) {
            QListWidgetItem* item = m_steps->item(m_stepIndex);
            if (item && !item->text().startsWith(QStringLiteral("✓ ")))
                item->setText(QStringLiteral("✓ ") + item->text());
        }
    });
    connect(m_openTool, &QPushButton::clicked, this, [this]() {
        if (!m_tutorial) return;
        emit openActionRequested(m_tutorial->steps.at(m_stepIndex).actionObjectName);
    });
    connect(m_restart, &QPushButton::clicked, this, [this]() {
        if (m_tutorial) startTutorial(m_tutorial->id, true);
    });
    connect(m_exit, &QPushButton::clicked, this, &TutorialGuideWidget::closeRequested);
}

QVector<TutorialDefinition> TutorialGuideWidget::catalog()
{
    return tutorialCatalog();
}

const TutorialDefinition* TutorialGuideWidget::findTutorial(const QString& id)
{
    const auto& values = tutorialCatalog();
    const auto found = std::find_if(values.cbegin(), values.cend(),
                                    [&id](const TutorialDefinition& value) {
                                        return value.id.compare(id, Qt::CaseInsensitive) == 0;
                                    });
    return found == values.cend() ? nullptr : &*found;
}

void TutorialGuideWidget::startTutorial(const QString& id, bool restart)
{
    m_tutorial = findTutorial(id);
    if (!m_tutorial) return;
    QSettings settings;
    const QString key = QStringLiteral("Tutorials/%1/Step").arg(m_tutorial->id);
    m_stepIndex = restart ? 0 : qBound(0, settings.value(key, 0).toInt(),
                                      static_cast<int>(m_tutorial->steps.size()) - 1);
    if (restart) settings.setValue(key, 0);
    rebuildTutorialList();
    showCurrentStep();
}

void TutorialGuideWidget::setContext(const FcProject* project,
                                     const QString& projectFilePath,
                                     const QString& fdsFilePath,
                                     bool resultsLoaded)
{
    m_project = project;
    m_projectFilePath = projectFilePath;
    m_fdsFilePath = fdsFilePath;
    m_resultsLoaded = resultsLoaded;
    if (m_tutorial) showCurrentStep();
}

QString TutorialGuideWidget::activeTutorialId() const
{
    return m_tutorial ? m_tutorial->id : QString{};
}

int TutorialGuideWidget::currentStepIndex() const { return m_stepIndex; }
int TutorialGuideWidget::stepCount() const
{
    return m_tutorial ? m_tutorial->steps.size() : 0;
}

bool TutorialGuideWidget::currentStepComplete(QString* detail) const
{
    if (!m_tutorial || !m_project) {
        if (detail) *detail = QStringLiteral("没有活动工程。");
        return false;
    }
    const TutorialStepDefinition& current = m_tutorial->steps.at(m_stepIndex);
    QMap<QString, int> counts;
    int fdsCount = 0;
    for (const auto& group : m_project->document()->groups())
        collectKeywordCounts(group, counts, fdsCount);
    bool passed = false;
    QString status;
    switch (current.checkKind) {
    case TutorialCheckKind::BlankProject:
        passed = fdsCount == 0;
        status = QStringLiteral("当前 FDS 对象数：%1。").arg(fdsCount);
        break;
    case TutorialCheckKind::ProjectConfigured:
        passed = !m_project->chid().trimmed().isEmpty() &&
                 m_project->chid().compare(QStringLiteral("untitled"),
                                           Qt::CaseInsensitive) != 0 &&
                 m_project->name().compare(QStringLiteral("Untitled"),
                                           Qt::CaseInsensitive) != 0;
        status = QStringLiteral("工程名：%1；CHID：%2；结束时间：%3 s。")
                     .arg(m_project->name(), m_project->chid())
                     .arg(m_project->endTime());
        break;
    case TutorialCheckKind::KeywordMinimums: {
        QStringList missing;
        for (auto iterator = current.keywordMinimums.cbegin();
             iterator != current.keywordMinimums.cend(); ++iterator) {
            if (counts.value(iterator.key()) < iterator.value())
                missing.append(QStringLiteral("%1 %2/%3")
                                   .arg(iterator.key())
                                   .arg(counts.value(iterator.key()))
                                   .arg(iterator.value()));
        }
        passed = missing.isEmpty();
        status = passed ? QStringLiteral("所需 FDS 记录类型与数量已满足。")
                        : QStringLiteral("仍缺少：%1。").arg(missing.join(QStringLiteral("；")));
        break;
    }
    case TutorialCheckKind::GeometryImported:
        passed = m_project->document()->geometryGroup() &&
                 !m_project->document()->geometryGroup()->children().empty();
        status = passed ? QStringLiteral("已找到导入几何对象。")
                        : QStringLiteral("几何分组中还没有导入对象。");
        break;
    case TutorialCheckKind::ScenarioMinimum:
        passed = m_project->scenarios().size() >= current.minimumCount;
        status = QStringLiteral("当前场景数：%1，要求至少 %2。")
                     .arg(m_project->scenarios().size()).arg(current.minimumCount);
        break;
    case TutorialCheckKind::ModelValid: {
        const FdsWriteResult result = FdsWriter::render(*m_project);
        passed = result.success();
        status = passed ? QStringLiteral("模型可生成有效 FDS。")
                        : QStringLiteral("错误：%1").arg(result.errors.join(QStringLiteral(" | ")));
        break;
    }
    case TutorialCheckKind::ProjectSaved:
        passed = QFileInfo::exists(m_projectFilePath);
        status = passed ? QStringLiteral("工程已保存：%1").arg(m_projectFilePath)
                        : QStringLiteral("尚未找到正式 `.firecae` 路径。");
        break;
    case TutorialCheckKind::FdsExported:
        passed = QFileInfo::exists(m_fdsFilePath) && QFileInfo(m_fdsFilePath).size() > 0;
        status = passed ? QStringLiteral("FDS 已生成：%1").arg(m_fdsFilePath)
                        : QStringLiteral("尚未找到已导出的 `.fds` 文件。");
        break;
    case TutorialCheckKind::ResultsLoaded:
        passed = m_resultsLoaded;
        status = passed ? QStringLiteral("结果算例已加载。")
                        : QStringLiteral("尚未在当前工程加载 FDS 结果。");
        break;
    case TutorialCheckKind::Manual:
        passed = true;
        status = QStringLiteral("此检查仅作人工确认，不证明求解完成或达到 T_END；请核对任务中心 Completed 状态和本次 OUT。");
        break;
    }
    if (detail) *detail = status;
    return passed;
}

void TutorialGuideWidget::rebuildTutorialList()
{
    m_steps->clear();
    if (!m_tutorial) return;
    for (int index = 0; index < m_tutorial->steps.size(); ++index) {
        m_steps->addItem(QStringLiteral("%1. %2").arg(index + 1)
                             .arg(m_tutorial->steps.at(index).title));
    }
    m_steps->setCurrentRow(m_stepIndex);
}

void TutorialGuideWidget::showCurrentStep()
{
    if (!m_tutorial || m_tutorial->steps.isEmpty()) return;
    m_stepIndex = qBound(0, m_stepIndex,
                         static_cast<int>(m_tutorial->steps.size()) - 1);
    const TutorialStepDefinition& current = m_tutorial->steps.at(m_stepIndex);
    m_title->setText(m_tutorial->title);
    m_overview->setText(
        QStringLiteral("<b>教学目的：</b>%1<br><b>完成效果：</b>%2<br>"
                       "<b>前置条件：</b>%3<br><b>最终工程：</b><code>%4</code>")
            .arg(m_tutorial->purpose.toHtmlEscaped(),
                 m_tutorial->completedEffect.toHtmlEscaped(),
                 m_tutorial->prerequisites.toHtmlEscaped(),
                 m_tutorial->finalProjectHint.toHtmlEscaped()));
    m_stepTitle->setText(QStringLiteral("第 %1/%2 步：%3")
                             .arg(m_stepIndex + 1).arg(m_tutorial->steps.size())
                             .arg(current.title));
    m_stepDetails->setText(
        QStringLiteral("<b>菜单路径：</b><code>%1</code><br><br>"
                       "<b>操作：</b>%2<br><br><b>输入参数：</b>%3<br><br>"
                       "<b>物理含义：</b>%4<br><br><b>为什么这样设置：</b>%5<br><br>"
                       "<b>预期结果：</b>%6<br><br><b>常见错误：</b>%7")
            .arg(current.menuPath.toHtmlEscaped(), current.instructions.toHtmlEscaped(),
                 current.parameters.toHtmlEscaped(), current.physicalMeaning.toHtmlEscaped(),
                 current.rationale.toHtmlEscaped(), current.expected.toHtmlEscaped(),
                 current.commonError.toHtmlEscaped()));
    QString status;
    const bool complete = currentStepComplete(&status);
    m_checkStatus->setText(
        QStringLiteral("<b>实时检查：</b>%1").arg(status.toHtmlEscaped()));
    m_back->setEnabled(m_stepIndex > 0);
    m_next->setEnabled(complete && m_stepIndex + 1 < m_tutorial->steps.size());
    m_openTool->setEnabled(!current.actionObjectName.isEmpty());
    emit stepTargetChanged(current.actionObjectName, current.menuPath);
}

void TutorialGuideWidget::saveProgress() const
{
    if (!m_tutorial) return;
    QSettings().setValue(QStringLiteral("Tutorials/%1/Step").arg(m_tutorial->id),
                         m_stepIndex);
}

void TutorialGuideWidget::changeStep(int delta)
{
    if (!m_tutorial) return;
    const int target = qBound(0, m_stepIndex + delta,
                              static_cast<int>(m_tutorial->steps.size()) - 1);
    if (target == m_stepIndex) return;
    m_stepIndex = target;
    saveProgress();
    m_steps->setCurrentRow(m_stepIndex);
    showCurrentStep();
}
