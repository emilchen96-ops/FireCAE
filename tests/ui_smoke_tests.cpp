#include "app/MainWindow.h"
#include "comparison/FdsComparisonReport.h"
#include "comparison/FdsInputComparator.h"
#include "core/FcDocument.h"
#include "core/FcFloorObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FdsExamples.h"
#include "fds/FdsImporter.h"
#include "fds/FdsPropertyLibrary.h"
#include "fds/FdsSchema.h"
#include "fds/FdsWriter.h"
#include "fds/FcProjectSerializer.h"
#include "geometry/FcIfcObject.h"
#include "modeling/BuildingGeometryService.h"
#include "visualization/GeometryDisplayManager.h"
#include "visualization/OccViewWidget.h"
#include "ui/UiLanguage.h"
#include "ui/SmokeviewHostWidget.h"
#include "ui/FdsObjectEditorDialog.h"
#include "ui/FdsRecordEditor.h"
#include "ui/FdsWorkflowDialogs.h"
#include "ui/GeometryImportWizard.h"
#include "ui/ApplicationSettingsDialog.h"
#include "import/IfcImportService.h"
#include "ui/ModelTreeWidget.h"
#include "ui/NativeResultViewerWidget.h"
#include "ui/SimulationParametersDialog.h"
#include "ui/SimulationRunDialog.h"
#include "ui/StartPageWidget.h"
#include "ui/TutorialGuideWidget.h"
#include "simulation/FdsRunner.h"
#include "simulation/SimulationTaskManager.h"
#include "simulation/SolverBackend.h"
#include "results/SmokeviewFrameRenderer.h"
#include "results/SmokeviewLauncher.h"
#include "results/FdsResultComparator.h"
#include "reliability/CrashDiagnostics.h"
#include "reliability/ProjectRecoveryManager.h"
#include "settings/ApplicationSettings.h"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMenu>
#include <QMessageBox>
#include <QDockWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextStream>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

int runM08DeviceControlRegression();

namespace
{
bool hasVisibleLabel(const MainWindow& window, const QString& text)
{
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        if (label->isVisible() && label->text() == text) {
            return true;
        }
    }
    return false;
}

QAction* findAction(MainWindow& window, const QString& text)
{
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

QAction* findActionByShortcut(MainWindow& window, const QKeySequence& shortcut)
{
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == shortcut) return action;
    }
    return nullptr;
}

bool selectItem(QTreeWidget* tree, QTreeWidgetItem* item)
{
    if (!tree || !item) {
        return false;
    }
    tree->clearSelection();
    tree->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
    return true;
}

QTreeWidgetItem* findItemByUuid(QTreeWidgetItem* item, const QString& objectId)
{
    if (!item) return nullptr;
    if (item->data(0, Qt::UserRole).toString() == objectId) return item;
    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem* found = findItemByUuid(item->child(index), objectId)) {
            return found;
        }
    }
    return nullptr;
}

bool captureAcceptance(MainWindow& window, const QString& fileName)
{
    const QString outputDirectory =
        qEnvironmentVariable("FIRECAE_ACCEPTANCE_SCREENSHOT_DIR");
    if (outputDirectory.isEmpty()) return true;
    if (!QDir().mkpath(outputDirectory)) return false;
    QApplication::processEvents();
    QPixmap screenshot = window.grab();
    OccViewWidget* view = window.findChild<OccViewWidget*>(
        QStringLiteral("Model3DView"));
    const QString viewFilePath =
        QDir(outputDirectory).filePath(fileName + QStringLiteral(".view.png"));
    if (view && view->isVisible() && view->saveViewImage(viewFilePath)) {
        QPixmap viewImage(viewFilePath);
        if (!viewImage.isNull()) {
            QPainter painter(&screenshot);
            painter.drawPixmap(view->mapTo(&window, QPoint(0, 0)).x(),
                               view->mapTo(&window, QPoint(0, 0)).y(),
                               view->width(), view->height(), viewImage);
        }
        QFile::remove(viewFilePath);
    }
    return screenshot.save(QDir(outputDirectory).filePath(fileName), "PNG");
}

bool captureDialogAcceptance(QWidget& widget, const QString& fileName)
{
    const QString outputDirectory =
        qEnvironmentVariable("FIRECAE_ACCEPTANCE_SCREENSHOT_DIR");
    if (outputDirectory.isEmpty()) return true;
    if (!QDir().mkpath(outputDirectory)) return false;
    QApplication::processEvents();
    return widget.grab().save(QDir(outputDirectory).filePath(fileName), "PNG");
}

QTreeWidgetItem* findDisplayedDescendant(
    QTreeWidgetItem* item,
    const GeometryDisplayManager* displayManager)
{
    if (!item || !displayManager) {
        return nullptr;
    }
    const QString objectId = item->data(0, Qt::UserRole).toString();
    if (displayManager->contains(objectId)) {
        return item;
    }
    for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
        if (QTreeWidgetItem* result =
                findDisplayedDescendant(item->child(childIndex), displayManager)) {
            return result;
        }
    }
    return nullptr;
}

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

void acceptNextCreateBoxDialog()
{
    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("CreateBoxDialog")) {
                if (auto* dialog = qobject_cast<QDialog*>(widget)) dialog->accept();
                return;
            }
        }
    });
}

void acceptNextBuildingDialog(
    const QString& name,
    const std::function<void(QDialog*)>& configure = {})
{
    QTimer::singleShot(0, [name, configure]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("CreateBuildingElementDialog")) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog) return;
                if (auto* nameEdit = dialog->findChild<QLineEdit*>(
                        QStringLiteral("BuildingNameEdit"))) {
                    nameEdit->setText(name);
                }
                if (configure) configure(dialog);
                dialog->accept();
                return;
            }
        }
    });
}

void acceptNextWallSketchDialog(
    const QString& name,
    const std::function<void(QDialog*)>& configure = {})
{
    QTimer::singleShot(0, [name, configure]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("WallSketchSettingsDialog")) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog) return;
                if (auto* nameEdit = dialog->findChild<QLineEdit*>(
                        QStringLiteral("WallSketchNameEdit"))) {
                    nameEdit->setText(name);
                }
                if (configure) configure(dialog);
                dialog->accept();
                return;
            }
        }
    });
}

void acceptNextFdsDialog(const std::function<void(QDialog*)>& configure = {})
{
    QTimer::singleShot(0, [configure]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("FdsObjectEditorDialog")) {
                if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                    if (configure) configure(dialog);
                    dialog->accept();
                }
                return;
            }
        }
    });
}

void closeNextFdsPreviewDialog()
{
    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("FdsBlockPreviewDialog")) {
                if (auto* dialog = qobject_cast<QDialog*>(widget)) dialog->reject();
                return;
            }
        }
    });
}

void acceptNextNamedDialog(const QString& objectName,
                           const std::function<void(QDialog*)>& configure = {})
{
    QTimer::singleShot(0, [objectName, configure]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() != objectName) continue;
            if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                if (configure) configure(dialog);
                dialog->accept();
            }
            return;
        }
    });
}

bool inspectAndCloseNextGraph(const QString& objectName)
{
    bool* inspected = new bool(false);
    QTimer::singleShot(0, [objectName, inspected]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() != objectName) continue;
            if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                *inspected = dialog->findChild<QGraphicsView*>(
                                 QStringLiteral("FdsNetworkGraphicsView")) != nullptr;
                dialog->reject();
            }
            return;
        }
    });
    // The caller reads and frees this value after the modal action returns.
    qApp->setProperty("firecaeGraphInspectedPointer",
                      QVariant::fromValue<qulonglong>(
                          reinterpret_cast<qulonglong>(inspected)));
    return true;
}

struct TutorialObjectRecipe
{
    QString sourceUuid;
    FdsObjectEditorData editorData;
    int sequenceIndex = 0;
};

void collectTutorialObjectRecipes(
    const FcObject::Ptr& object,
    FcDocumentGroup group,
    std::vector<TutorialObjectRecipe>& recipes)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        recipes.push_back({namelist->id(),
                           {namelist->name(), namelist->keyword(), namelist->fdsId(),
                            group, namelist->parameters()},
                           namelist->sequenceIndex()});
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectTutorialObjectRecipes(child, group, recipes);
    }
}

std::vector<TutorialObjectRecipe> tutorialObjectRecipes(const FcProject& project)
{
    std::vector<TutorialObjectRecipe> recipes;
    if (!project.document()) return recipes;
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(FcDocumentGroup::Count); ++index) {
        const auto group = static_cast<FcDocumentGroup>(index);
        collectTutorialObjectRecipes(project.document()->group(group), group, recipes);
    }
    std::stable_sort(recipes.begin(), recipes.end(),
                     [](const TutorialObjectRecipe& left,
                        const TutorialObjectRecipe& right) {
                         return left.sequenceIndex < right.sequenceIndex;
                     });
    return recipes;
}

QString quotedFdsId(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

FdsObjectEditorData creationPassData(const TutorialObjectRecipe& recipe,
                                     const FcProject& sourceProject)
{
    FdsObjectEditorData data = recipe.editorData;
    for (FcFdsParameter& parameter : data.parameters) {
        if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
        QStringList identifiers;
        for (const QString& sourceUuid : parameter.targetObjectIds) {
            const auto target = std::dynamic_pointer_cast<FcFdsObject>(
                sourceProject.document()->findObject(sourceUuid));
            if (target && !target->fdsId().isEmpty()) {
                identifiers.append(quotedFdsId(target->fdsId()));
            }
        }
        parameter.kind = FcFdsParameterKind::Raw;
        parameter.value = identifiers.isEmpty()
                              ? parameter.value
                              : identifiers.join(QLatin1Char(','));
        parameter.targetObjectIds.clear();
    }
    return data;
}

bool remapReferenceData(FdsObjectEditorData& data,
                        const QHash<QString, QString>& targetUuidBySourceUuid)
{
    for (FcFdsParameter& parameter : data.parameters) {
        if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
        QStringList targetIds;
        for (const QString& sourceUuid : parameter.targetObjectIds) {
            const QString targetUuid = targetUuidBySourceUuid.value(sourceUuid);
            if (targetUuid.isEmpty()) return false;
            targetIds.append(targetUuid);
        }
        parameter.targetObjectIds = targetIds;
    }
    return true;
}

bool runFdsObjectDialog(QAction* action, const FdsObjectEditorData& data)
{
    if (!action || !action->isEnabled()) return false;
    const auto configured = std::make_shared<bool>(false);
    const auto accepted = std::make_shared<bool>(false);
    QTimer::singleShot(0, [configured, accepted, data]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = dynamic_cast<FdsObjectEditorDialog*>(widget);
            if (!dialog) continue;
            QObject::connect(dialog, &QDialog::finished,
                             [accepted](int result) {
                                 *accepted = result == QDialog::Accepted;
                             });
            dialog->setEditorData(data);
            *configured = true;
            QTimer::singleShot(0, []() {
                for (QWidget* topLevel : QApplication::topLevelWidgets()) {
                    if (auto* message = qobject_cast<QMessageBox*>(topLevel)) {
                        std::cerr << "FDS editor validation message: "
                                  << message->text().toStdString() << '\n';
                        message->reject();
                        return;
                    }
                }
            });
            const QPointer<FdsObjectEditorDialog> guard(dialog);
            QTimer::singleShot(250, [guard]() {
                if (guard && guard->isVisible()) guard->reject();
            });
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) {
                if (QAbstractButton* ok = buttons->button(QDialogButtonBox::Ok)) {
                    ok->click();
                    return;
                }
            }
            dialog->accept();
            return;
        }
    });
    action->trigger();
    QApplication::processEvents();
    return *configured && *accepted;
}

bool runProjectSettingsDialog(QAction* action, const FcProject& sourceProject)
{
    if (!action || !action->isEnabled()) return false;
    const auto configured = std::make_shared<bool>(false);
    const QString name = sourceProject.name();
    const QString chid = sourceProject.chid();
    const QString version = sourceProject.fdsVersion();
    const double endTime = sourceProject.endTime();
    const int displayUnit = static_cast<int>(sourceProject.displayUnit());
    QTimer::singleShot(0, [configured, name, chid, version, endTime, displayUnit]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog ||
                (dialog->objectName() != QStringLiteral("FdsProjectDialog") &&
                 dialog->objectName() != QStringLiteral("ProjectSettingsDialog"))) {
                continue;
            }
            dialog->findChild<QLineEdit*>(QStringLiteral("ProjectNameEdit"))
                ->setText(name);
            dialog->findChild<QLineEdit*>(QStringLiteral("ProjectChidEdit"))
                ->setText(chid);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("ProjectEndTimeSpin"))
                ->setValue(endTime);
            dialog->findChild<QComboBox*>(QStringLiteral("ProjectFdsVersionCombo"))
                ->setCurrentText(version);
            auto* unit = dialog->findChild<QComboBox*>(
                QStringLiteral("ProjectDisplayUnitCombo"));
            unit->setCurrentIndex(qMax(0, unit->findData(displayUnit)));
            *configured = true;
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) {
                if (QAbstractButton* ok = buttons->button(QDialogButtonBox::Ok)) {
                    ok->click();
                    return;
                }
            }
            dialog->accept();
            return;
        }
    });
    action->trigger();
    QApplication::processEvents();
    return *configured;
}

QString tutorialGroupLabel(FcDocumentGroup group)
{
    switch (group) {
    case FcDocumentGroup::Geometry: return QStringLiteral("Geometry / 几何");
    case FcDocumentGroup::Meshes: return QStringLiteral("Meshes / 网格");
    case FcDocumentGroup::Configuration: return QStringLiteral("Configuration / 仿真设置");
    case FcDocumentGroup::Species: return QStringLiteral("Species / 组分");
    case FcDocumentGroup::Materials: return QStringLiteral("Materials / 材料");
    case FcDocumentGroup::Surfaces: return QStringLiteral("Surfaces / 表面");
    case FcDocumentGroup::Reactions: return QStringLiteral("Reactions / 反应");
    case FcDocumentGroup::Particles: return QStringLiteral("Particles / 粒子");
    case FcDocumentGroup::Vents: return QStringLiteral("Vents / 通风口");
    case FcDocumentGroup::Devices: return QStringLiteral("Devices / 设备");
    case FcDocumentGroup::Controls: return QStringLiteral("Controls / 控制");
    case FcDocumentGroup::HVAC: return QStringLiteral("HVAC");
    case FcDocumentGroup::InitialConditions: return QStringLiteral("Initial Conditions / 初始条件");
    case FcDocumentGroup::Outputs: return QStringLiteral("Outputs / 输出");
    case FcDocumentGroup::Results: return QStringLiteral("Results / 结果");
    case FcDocumentGroup::Count: break;
    }
    return QStringLiteral("Unknown");
}

QString markdownText(QString value)
{
    value.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    value.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return value;
}

bool writeTutorialGuiGuide(
    const QString& caseDirectory,
    const QString& caseKey,
    const FcProject& sourceProject,
    const std::vector<TutorialObjectRecipe>& recipes,
    const QHash<QString, QString>& targetUuidBySourceUuid,
    const QString& officialPath,
    const QString& exportedPath)
{
    QSaveFile file(QDir(caseDirectory).filePath(
        QStringLiteral("GUI-RECONSTRUCTION.md")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "# " << caseKey << " — 空工程 GUI 复现记录\n\n"
           << "> 本记录由 `FireCAEA10BlankTutorialUiTests` 使用正式界面对话框生成。"
              "教程工厂只作为期望数据配方，不直接装载到目标工程。\n\n"
           << "## 项目设置\n\n"
           << "- 名称：`" << markdownText(sourceProject.name()) << "`\n"
           << "- CHID：`" << markdownText(sourceProject.chid()) << "`\n"
           << "- 结束时间：`" << QString::number(sourceProject.endTime(), 'g', 15)
           << " s`\n"
           << "- FDS Schema：`" << markdownText(sourceProject.fdsVersion()) << "`\n"
           << "- 从空工程创建对象数：`" << recipes.size() << "`\n\n"
           << "## 界面操作顺序\n\n"
           << "1. 文件 → 新建，保持空工程。\n"
           << "2. 模型 → 项目设置，填写上面的名称、CHID、结束时间和 FDS 版本。\n"
           << "3. 依次使用 模型 → 添加 FDS 对象，按下列顺序创建。\n"
           << "4. 对带‘引用’参数的对象再次双击编辑，通过引用选择器绑定目标对象；"
              "关联键为 UUID，不使用对象名称。\n"
           << "5. 点击校验模型，保存 `.firecae`，关闭并重新打开，再导出 `.fds`。\n\n"
           << "## 对象和参数\n\n";

    int step = 1;
    for (const TutorialObjectRecipe& recipe : recipes) {
        stream << "### " << step++ << ". &" << recipe.editorData.keyword
               << " — " << markdownText(recipe.editorData.name) << "\n\n"
               << "- 分类：" << tutorialGroupLabel(recipe.editorData.group) << "\n"
               << "- FDS ID：`" << markdownText(recipe.editorData.fdsId) << "`\n"
               << "- 新工程 UUID：`"
               << targetUuidBySourceUuid.value(recipe.sourceUuid) << "`\n";
        if (recipe.editorData.parameters.empty()) {
            stream << "- 参数：无\n\n";
            continue;
        }
        stream << "- 参数：\n\n";
        for (const FcFdsParameter& parameter : recipe.editorData.parameters) {
            stream << "  - `" << parameter.key << "` = ";
            if (parameter.kind == FcFdsParameterKind::Raw) {
                stream << "原始值 `" << markdownText(parameter.value) << "`";
            } else if (parameter.kind == FcFdsParameterKind::String) {
                stream << "文本 `" << markdownText(parameter.value) << "`";
            } else {
                QStringList labels;
                for (const QString& sourceUuid : parameter.targetObjectIds) {
                    const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
                        sourceProject.document()->findObject(sourceUuid));
                    labels.append(target
                                      ? QStringLiteral("&%1 `%2` → UUID `%3`")
                                            .arg(target->keyword(), target->fdsId(),
                                                 targetUuidBySourceUuid.value(sourceUuid))
                                      : QStringLiteral("UUID `%1`").arg(
                                            targetUuidBySourceUuid.value(sourceUuid)));
                }
                stream << "引用 " << labels.join(QStringLiteral("；"));
            }
            stream << "\n";
        }
        stream << "\n";
    }
    stream << "## 自动验收结论\n\n"
           << "- 项目保存/重开：通过；全部新工程 UUID 保持不变。\n"
           << "- 模型校验：通过。\n"
           << "- 官方输入：`" << QDir::toNativeSeparators(officialPath) << "`\n"
           << "- GUI 导出：`" << QDir::toNativeSeparators(exportedPath) << "`\n"
           << "- FDS 输入语义对比：通过。\n";
    return file.commit();
}

std::unique_ptr<FcProject> createFirstFireAcceptanceRecipe(bool multipleOutputs = false)
{
    auto project = std::make_unique<FcProject>(
        QStringLiteral("Your First Fire / 第一个火灾算例"));
    project->setChid(QStringLiteral("first_fire"));
    project->setEndTime(30.0);
    project->setFdsVersion(QStringLiteral("6.11.1"));
    FcDocument* document = project->document();

    const auto add = [](const std::shared_ptr<FcObjectGroup>& group,
                        const QString& name,
                        FcObjectType type,
                        const QString& keyword,
                        const QString& fdsId,
                        int sequence,
                        std::initializer_list<FcFdsParameter> parameters) {
        auto object = std::make_shared<FcFdsNamelist>(
            name, type, keyword, fdsId, sequence);
        object->setParameters(std::vector<FcFdsParameter>(parameters));
        group->addChild(object);
        return object;
    };
    const auto rawValue = [](const char* key, const char* value) {
        return FcFdsParameter{QString::fromLatin1(key), FcFdsParameterKind::Raw,
                              QString::fromLatin1(value), {}};
    };
    const auto textValue = [](const char* key, const char* value) {
        return FcFdsParameter{QString::fromLatin1(key), FcFdsParameterKind::String,
                              QString::fromLatin1(value), {}};
    };

    add(document->meshesGroup(), QStringLiteral("6 m × 4 m × 3 m Domain"),
        FcObjectType::Mesh, QStringLiteral("MESH"), QStringLiteral("DOMAIN"), 0,
        {rawValue("IJK", "30,20,15"), rawValue("XB", "0,6,0,4,0,3")});
    add(document->reactionsGroup(), QStringLiteral("Propane Reaction"),
        FcObjectType::Reaction, QStringLiteral("REAC"),
        QStringLiteral("PROPANE_REACTION"), 1,
        {textValue("FUEL", "PROPANE"), rawValue("SOOT_YIELD", "0.01"),
         rawValue("RADIATIVE_FRACTION", "0.35")});
    const auto burnerSurface = add(
        document->surfacesGroup(), QStringLiteral("500 kW Burner Surface"),
        FcObjectType::Surface, QStringLiteral("SURF"), QStringLiteral("BURNER"), 2,
        {rawValue("HRRPUA", "500"), textValue("COLOR", "RED")});
    add(document->geometryGroup(), QStringLiteral("Burner Base"),
        FcObjectType::Obstruction, QStringLiteral("OBST"),
        QStringLiteral("BURNER_BASE"), 3,
        {rawValue("XB", "2.5,3.5,1.5,2.5,0,0.2")});
    add(document->ventsGroup(), QStringLiteral("1 m² Fire Source"),
        FcObjectType::Vent, QStringLiteral("VENT"), QStringLiteral("FIRE"), 4,
        {rawValue("XB", "2.5,3.5,1.5,2.5,0.2,0.2"),
         {QStringLiteral("SURF_ID"), FcFdsParameterKind::ObjectReferences,
          {}, {burnerSurface->id()}}});
    add(document->ventsGroup(), QStringLiteral("Open Ceiling Boundary"),
        FcObjectType::Vent, QStringLiteral("VENT"), QStringLiteral("OPEN_TOP"), 5,
        {textValue("MB", "ZMAX"), textValue("SURF_ID", "OPEN")});
    add(document->devicesGroup(), QStringLiteral("Room Temperature"),
        FcObjectType::Device, QStringLiteral("DEVC"), QStringLiteral("TEMP_CENTER"), 6,
        {rawValue("XYZ", "3,2,1.5"), textValue("QUANTITY", "TEMPERATURE")});
    add(document->outputsGroup(), QStringLiteral("Temperature Slice at Z=1.5 m"),
        FcObjectType::Output, QStringLiteral("SLCF"), {}, 7,
        {rawValue("PBZ", "1.5"), textValue("QUANTITY", "TEMPERATURE")});
    add(document->outputsGroup(), QStringLiteral("One-second Output Intervals"),
        FcObjectType::Output, QStringLiteral("DUMP"), {}, 8,
        {rawValue("DT_HRR", "1"), rawValue("DT_DEVC", "1"),
         rawValue("DT_SLCF", "1")});

    if (multipleOutputs) {
        add(document->devicesGroup(), QStringLiteral("Upper Temperature Probe"),
            FcObjectType::Device, QStringLiteral("DEVC"), QStringLiteral("TEMP_HIGH"), 9,
            {rawValue("XYZ", "3,2,2.5"), textValue("QUANTITY", "TEMPERATURE")});
        add(document->outputsGroup(), QStringLiteral("Vertical Temperature Slice"),
            FcObjectType::Output, QStringLiteral("SLCF"), {}, 10,
            {rawValue("PBY", "2"), textValue("QUANTITY", "TEMPERATURE")});
        add(document->outputsGroup(), QStringLiteral("Horizontal Velocity Vectors"),
            FcObjectType::Output, QStringLiteral("SLCF"), {}, 11,
            {rawValue("PBZ", "1.5"), textValue("QUANTITY", "VELOCITY"), rawValue("VECTOR", ".TRUE.")});
        add(document->outputsGroup(), QStringLiteral("Vertical Velocity Vectors"),
            FcObjectType::Output, QStringLiteral("SLCF"), {}, 12,
            {rawValue("PBY", "2"), textValue("QUANTITY", "VELOCITY"), rawValue("VECTOR", ".TRUE.")});
    }

    project->setModified(false);
    return project;
}

int runP27FirstFireEndToEndAcceptance(bool multipleOutputs = false)
{
    QTemporaryDir temporaryDirectory;
    QString outputDirectory =
        qEnvironmentVariable("FIRECAE_P27_FIRST_FIRE_DIR");
    if (outputDirectory.isEmpty()) outputDirectory = temporaryDirectory.path();
    if (outputDirectory.isEmpty() || !QDir().mkpath(outputDirectory))
        return fail("P27 First Fire output directory is unavailable.");

    std::unique_ptr<FcProject> sourceProject = createFirstFireAcceptanceRecipe(multipleOutputs);
    const std::vector<TutorialObjectRecipe> recipes =
        tutorialObjectRecipes(*sourceProject);
    if (recipes.size() != (multipleOutputs ? 13u : 9u))
        return fail("P27 First Fire recipe does not contain the expected nine objects.");

    MainWindow window;
    window.resize(1600, 1000);
    window.show();
    QApplication::processEvents();
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-01-blank.png")))
        return fail("P27 First Fire blank-project screenshot failed.");

    auto* modelTree = window.findChild<ModelTreeWidget*>();
    QAction* projectSettings = window.findChild<QAction*>(
        QStringLiteral("ProjectSettingsAction"));
    QAction* addObject = window.findChild<QAction*>(
        QStringLiteral("AddFdsObjectAction"));
    QAction* editObject = findAction(window, QStringLiteral("Edit Selected Object..."));
    QAction* validate = window.findChild<QAction*>(QStringLiteral("ValidateModelAction"));
    QAction* run = window.findChild<QAction*>(QStringLiteral("RunFdsAction"));
    if (!modelTree || !projectSettings || !addObject || !editObject ||
        !validate || !run)
        return fail("P27 First Fire production actions are incomplete.");
    if (!runProjectSettingsDialog(projectSettings, *sourceProject))
        return fail("P27 First Fire project settings were not entered through the GUI.");
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-02-settings.png")))
        return fail("P27 First Fire settings screenshot failed.");

    QHash<QString, QString> targetUuidBySourceUuid;
    for (const TutorialObjectRecipe& recipe : recipes) {
        if (!runFdsObjectDialog(addObject,
                                creationPassData(recipe, *sourceProject)))
            return fail("P27 First Fire GUI object creation failed.");
        const QString targetUuid = modelTree->selectedObjectId();
        if (targetUuid.isEmpty() ||
            targetUuidBySourceUuid.values().contains(targetUuid))
            return fail("P27 First Fire GUI did not create a unique object UUID.");
        targetUuidBySourceUuid.insert(recipe.sourceUuid, targetUuid);
    }
    for (const TutorialObjectRecipe& recipe : recipes) {
        const bool hasReferences = std::any_of(
            recipe.editorData.parameters.cbegin(), recipe.editorData.parameters.cend(),
            [](const FcFdsParameter& parameter) {
                return parameter.kind == FcFdsParameterKind::ObjectReferences;
            });
        if (!hasReferences) continue;
        FdsObjectEditorData finalData = recipe.editorData;
        if (!remapReferenceData(finalData, targetUuidBySourceUuid) ||
            !modelTree->selectObjectById(
                targetUuidBySourceUuid.value(recipe.sourceUuid), true) ||
            !runFdsObjectDialog(editObject, finalData))
            return fail("P27 First Fire UUID reference binding failed.");
    }
    if (QAction* fit = findAction(window, QStringLiteral("Fit All"))) fit->trigger();
    QApplication::processEvents();
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-03-modeled.png")))
        return fail("P27 First Fire modeled screenshot failed.");

    const auto validationClosed = std::make_shared<bool>(false);
    QTimer::singleShot(0, [validationClosed]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                *validationClosed = true;
                message->accept();
                return;
            }
        }
    });
    validate->trigger();
    QApplication::processEvents();
    if (!*validationClosed ||
        !window.statusBar()->currentMessage().startsWith(
            QStringLiteral("Model validation passed")))
        return fail("P27 First Fire model validation failed.");

    const QString projectPath =
        QDir(outputDirectory).filePath(QStringLiteral("first_fire.firecae"));
    const QString fdsPath =
        QDir(outputDirectory).filePath(QStringLiteral("first_fire.fds"));
    if (!window.saveProjectFile(projectPath) ||
        !window.openProjectFile(projectPath))
        return fail("P27 First Fire project save/reopen failed.");
    for (const QString& uuid : targetUuidBySourceUuid) {
        if (!modelTree->selectObjectById(uuid))
            return fail("P27 First Fire UUID changed after save/reopen.");
    }
    if (!window.exportCurrentProjectToFds(fdsPath))
        return fail("P27 First Fire FDS export failed.");
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-04-exported.png")))
        return fail("P27 First Fire exported screenshot failed.");

    const auto queued = std::make_shared<bool>(false);
    QTimer::singleShot(0, [queued]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = dynamic_cast<SimulationRunDialog*>(widget);
            if (!dialog) continue;
            auto* backend = dialog->findChild<QComboBox*>(
                QStringLiteral("SimulationRunBackendCombo"));
            if (backend) {
                backend->setCurrentIndex(backend->findData(
                    static_cast<int>(SolverBackendKind::FdsSerialCpu)));
            }
            *queued = true;
            dialog->accept();
            return;
        }
    });
    run->trigger();
    QApplication::processEvents();
    if (!*queued) return fail("P27 First Fire run dialog was not queued.");

    auto* taskTable = window.findChild<QTableWidget*>(
        QStringLiteral("SimulationTaskTable"));
    if (!taskTable) return fail("P27 First Fire task center is unavailable.");
    QEventLoop taskLoop;
    QTimer taskPoll;
    QTimer taskTimeout;
    taskPoll.setInterval(100);
    taskTimeout.setSingleShot(true);
    QObject::connect(&taskPoll, &QTimer::timeout, &taskLoop,
                     [&taskLoop, taskTable]() {
                         if (taskTable->rowCount() == 0 ||
                             !taskTable->item(0, 0)) return;
                         const QString state = taskTable->item(0, 0)->text();
                         if (state == QStringLiteral("Completed") ||
                             state == QStringLiteral("Failed") ||
                             state == QStringLiteral("Cancelled"))
                             taskLoop.quit();
                     });
    QObject::connect(&taskTimeout, &QTimer::timeout,
                     &taskLoop, &QEventLoop::quit);
    taskPoll.start();
    // These are functional acceptance runs, also executed alongside retained
    // native reference jobs. Allow wall-clock contention without changing the
    // physical T_END or the required normal-completion/result checks.
    taskTimeout.start(600000);
    taskLoop.exec();
    taskPoll.stop();
    QApplication::processEvents();
    const QString taskState = taskTable->rowCount() > 0 && taskTable->item(0, 0)
                                  ? taskTable->item(0, 0)->text() : QString{};
    const auto* taskManager = window.findChild<SimulationTaskManager*>();
    if (!taskManager || taskManager->tasks().size() != 1)
        return fail("P27 First Fire did not retain exactly one identifiable task.");
    const FdsRunSummary completedRun = taskManager->tasks().constFirst().summary;
    const QString outPath = completedRun.outputFilePath;
    const QString smvPath = completedRun.smvFilePath;
    QFile outFile(outPath);
    if (taskState != QStringLiteral("Completed") || !completedRun.success ||
        !outFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !QString::fromUtf8(outFile.readAll()).contains(
            QStringLiteral("STOP: FDS completed successfully")) ||
        !QFileInfo::exists(smvPath))
        return fail("P27 First Fire real FDS calculation did not complete normally.");
    if (multipleOutputs) {
        QFile smv(smvPath);
        QFile devices(QFileInfo(smvPath).absoluteDir().filePath(QStringLiteral("first_fire_devc.csv")));
        if (!smv.open(QIODevice::ReadOnly) || !devices.open(QIODevice::ReadOnly))
            return fail("Multiple-output run is missing its own SMV or device CSV.");
        const QByteArray smokeviewData = smv.readAll();
        const QByteArray deviceData = devices.readAll();
        if (!smokeviewData.contains("TEMPERATURE") || !smokeviewData.contains("VELOCITY") ||
            !deviceData.contains("TEMP_CENTER") || !deviceData.contains("TEMP_HIGH") ||
            QFileInfo(smvPath).absoluteDir().entryList({QStringLiteral("*.sf")}, QDir::Files).size() < 4)
            return fail("Multiple-output run did not generate both quantities, both probes and all slice planes.");
    }
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-05-solved.png")))
        return fail("P27 First Fire solved-task screenshot failed.");

    if (!window.openResultFile(smvPath))
        return fail("P27 First Fire result set could not be loaded.");
    const QString resultCaseId = modelTree->selectedObjectId();
    if (resultCaseId.isEmpty() || !window.saveProjectFile(projectPath) ||
        !window.openProjectFile(projectPath) || !window.reloadResultCase(resultCaseId))
        return fail("P27 result case identity or reload failed after saving and reopening the solved project.");
    if (resultCaseId.isEmpty() ||
        !window.openResultInNativeViewer(resultCaseId))
        return fail("P27 First Fire native Results workspace could not open.");
    auto* viewer = window.findChild<NativeResultViewerWidget*>(
        QStringLiteral("NativeResultViewerWidget"));
    auto* resultFiles = viewer ? viewer->findChild<QListWidget*>(
                                    QStringLiteral("NativeResultFileList")) : nullptr;
    auto* play = viewer ? viewer->findChild<QPushButton*>(
                              QStringLiteral("NativeResultPlayButton")) : nullptr;
    if (!viewer || !viewer->hasOpenCase() || !resultFiles || !play)
        return fail("P27 First Fire HRR/result playback controls are incomplete.");
    int hrrRow = -1;
    for (int row = 0; row < resultFiles->count(); ++row) {
        if (resultFiles->item(row)->data(Qt::UserRole).toString().endsWith(
                QStringLiteral("first_fire_hrr.csv"), Qt::CaseInsensitive)) {
            hrrRow = row;
            break;
        }
    }
    if (hrrRow < 0)
        return fail("P27 First Fire HRR result file is missing from Results.");
    resultFiles->setCurrentRow(hrrRow);
    QApplication::processEvents();
    if (viewer->csvSeriesCount() < 1 || viewer->frameCount() < 2 ||
        !play->isEnabled())
        return fail("P27 First Fire HRR curve does not contain playable frames.");
    play->click();
    QEventLoop playbackLoop;
    QTimer::singleShot(600, &playbackLoop, &QEventLoop::quit);
    playbackLoop.exec();
    if (viewer->currentTime() <= 0.0)
        return fail("P27 First Fire result timeline did not advance.");
    const QString visibleCsv =
        QDir(outputDirectory).filePath(QStringLiteral("first_fire-visible.csv"));
    const QString resultPng =
        QDir(outputDirectory).filePath(QStringLiteral("first_fire-results.png"));
    if (!viewer->exportVisibleCsv(visibleCsv) ||
        !viewer->saveScreenshot(resultPng) ||
        QFileInfo(visibleCsv).size() <= 0 || QFileInfo(resultPng).size() <= 0)
        return fail("P27 First Fire result CSV/screenshot export failed.");
    if (!captureAcceptance(window, QStringLiteral("P27-first-fire-06-results.png")))
        return fail("P27 First Fire Results workspace screenshot failed.");

    if (qEnvironmentVariableIsSet("FIRECAE_P27_RUN_SMOKEVIEW")) {
        if (!window.launchResultAnimation(resultCaseId,
                                          SmokeviewLaunchMode::Standard))
            return fail("P27 First Fire Smokeview could not start.");
        QEventLoop smokeviewLoop;
        QTimer::singleShot(3500, &smokeviewLoop, &QEventLoop::quit);
        smokeviewLoop.exec();
        if (!captureAcceptance(
                window, QStringLiteral("P27-first-fire-07-smokeview.png")))
            return fail("P27 First Fire Smokeview screenshot failed.");
    }
    std::cerr << "P27 cleanup: close result case" << std::endl;
    if (!window.closeResultCase(resultCaseId))
        return fail("P27 First Fire result/Smokeview cleanup failed.");
    std::cerr << "P27 cleanup: close window" << std::endl;
    window.close();
    QApplication::processEvents();
    std::cerr << "P27 cleanup: leaving MainWindow scope" << std::endl;

    std::cout << "FireCAE P27 First Fire GUI/FDS/Results acceptance passed.\n";
    return 0;
}

bool runP27ComplexIfcImportWizard(QAction* action, const QString& filePath)
{
    if (!action || !action->isEnabled() || !QFileInfo::exists(filePath)) return false;
    const auto configured = std::make_shared<bool>(false);
    const auto accepted = std::make_shared<bool>(false);
    const auto previewReady = std::make_shared<bool>(false);
    QTimer::singleShot(0, [configured, accepted, previewReady, filePath]() {
        GeometryImportWizard* wizard = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            wizard = dynamic_cast<GeometryImportWizard*>(widget);
            if (wizard) break;
        }
        if (!wizard) return;
        QObject::connect(wizard, &QDialog::finished,
                         [accepted](int result) {
                             *accepted = result == QDialog::Accepted;
                         });
        auto* fileEdit = wizard->findChild<QLineEdit*>(
            QStringLiteral("GeometryImportFileEdit"));
        if (!fileEdit) {
            wizard->reject();
            return;
        }
        fileEdit->setText(filePath);
        wizard->next();
        wizard->next();
        QApplication::processEvents();
        auto* typeTree = wizard->findChild<QTreeWidget*>(
            QStringLiteral("IfcImportTypeTree"));
        auto* merge = wizard->findChild<QComboBox*>(
            QStringLiteral("IfcImportMergeStrategyCombo"));
        auto* simplification = wizard->findChild<QComboBox*>(
            QStringLiteral("IfcImportSimplificationCombo"));
        auto* route = wizard->findChild<QComboBox*>(
            QStringLiteral("IfcImportConversionRouteCombo"));
        if (!typeTree || !merge || !simplification || !route) {
            wizard->reject();
            return;
        }
        const QSet<QString> acceptedClasses{
            QStringLiteral("IFCWALL"),
            QStringLiteral("IFCWALLSTANDARDCASE"),
            QStringLiteral("IFCSLAB"),
            QStringLiteral("IFCDOOR"),
            QStringLiteral("IFCWINDOW")};
        int checkedClassCount = 0;
        for (int index = 0; index < typeTree->topLevelItemCount(); ++index) {
            QTreeWidgetItem* item = typeTree->topLevelItem(index);
            const bool checked = item && acceptedClasses.contains(item->text(1));
            if (item) item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            if (checked) ++checkedClassCount;
        }
        const int mergeIndex = merge->findData(QStringLiteral("PRESERVE_HIERARCHY"));
        const int simplificationIndex =
            simplification->findData(QStringLiteral("BOUNDING_BOX"));
        const int routeIndex = route->findData(QStringLiteral("OBST"));
        if (checkedClassCount < 4 || mergeIndex < 0 || simplificationIndex < 0 ||
            routeIndex < 0) {
            wizard->reject();
            return;
        }
        merge->setCurrentIndex(mergeIndex);
        simplification->setCurrentIndex(simplificationIndex);
        route->setCurrentIndex(routeIndex);
        *configured = captureDialogAcceptance(
            *wizard, QStringLiteral("P27-ifc-01-filter-options.png"));
        wizard->next();

        const QPointer<GeometryImportWizard> guard(wizard);
        const auto poll = std::make_shared<std::function<void()>>();
        *poll = [guard, poll, previewReady]() {
            if (!guard) return;
            auto* progress = guard->findChild<QProgressBar*>(
                QStringLiteral("GeometryImportProgressBar"));
            auto* preview = guard->findChild<QPlainTextEdit*>(
                QStringLiteral("GeometryImportPreviewReport"));
            if (!progress || !preview) {
                guard->reject();
                return;
            }
            const QString text = preview->toPlainText();
            if (progress->value() == 100 &&
                text.contains(QStringLiteral("FDS conversion route: OBST")) &&
                text.contains(QStringLiteral("IFCWALL")) &&
                text.contains(QStringLiteral("IFCSLAB")) &&
                text.contains(QStringLiteral("IFCDOOR")) &&
                text.contains(QStringLiteral("IFCWINDOW"))) {
                *previewReady = captureDialogAcceptance(
                    *guard, QStringLiteral("P27-ifc-02-preview-ready.png"));
                guard->accept();
                return;
            }
            if (text.contains(QStringLiteral("failed"), Qt::CaseInsensitive)) {
                guard->reject();
                return;
            }
            QTimer::singleShot(100, guard, *poll);
        };
        QTimer::singleShot(100, guard, *poll);
    });
    action->trigger();
    QApplication::processEvents();
    return *configured && *previewReady && *accepted;
}

int runP27ComplexIfcEndToEndAcceptance()
{
    QTemporaryDir temporaryDirectory;
    QString outputDirectory = qEnvironmentVariable("FIRECAE_P27_IFC_DIR");
    if (outputDirectory.isEmpty()) outputDirectory = temporaryDirectory.path();
    if (outputDirectory.isEmpty() || !QDir().mkpath(outputDirectory))
        return fail("P27 IFC output directory is unavailable.");

    const QString ifcPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
        .filePath(QStringLiteral("Clinic_Architectural_IFC2x3.ifc"));
    const IfcPreflightReport preflight = IfcImportService::inspectFile(ifcPath);
    if (!preflight.success() || preflight.sourceBytes < 10000000 ||
        preflight.productCount < 1000 || preflight.storeyCount < 2)
        return fail("P27 complex IFC fixture is not sufficiently representative.");

    MainWindow window;
    window.resize(1600, 1000);
    window.show();
    QApplication::processEvents();
    FcProject settings(QStringLiteral("Complex IFC Conversion / 复杂 IFC 转换"));
    settings.setChid(QStringLiteral("p27_complex_ifc"));
    settings.setEndTime(0.1);
    settings.setFdsVersion(QStringLiteral("6.11.1"));
    QAction* projectSettings = window.findChild<QAction*>(
        QStringLiteral("ProjectSettingsAction"));
    QAction* importGeometry = window.findChild<QAction*>(
        QStringLiteral("ImportGeometryAction"));
    QAction* convert = window.findChild<QAction*>(
        QStringLiteral("ConvertGeometryToFdsAction"));
    QAction* addObject = window.findChild<QAction*>(
        QStringLiteral("AddFdsObjectAction"));
    QAction* validate = window.findChild<QAction*>(
        QStringLiteral("ValidateModelAction"));
    QAction* run = window.findChild<QAction*>(QStringLiteral("RunFdsAction"));
    auto* modelTree = window.findChild<ModelTreeWidget*>();
    auto* objectTree = modelTree ? modelTree->findChild<QTreeWidget*>(
                                      QStringLiteral("ModelTreeObjectTree")) : nullptr;
    if (!projectSettings || !importGeometry || !convert || !addObject ||
        !validate || !run || !modelTree || !objectTree)
        return fail("P27 complex IFC production actions are incomplete.");
    if (!runProjectSettingsDialog(projectSettings, settings))
        return fail("P27 complex IFC project settings failed.");
    if (!runP27ComplexIfcImportWizard(importGeometry, ifcPath))
        return fail("P27 complex IFC wizard import failed.");
    QApplication::processEvents();

    QTreeWidgetItem* projectItem = objectTree->topLevelItem(0);
    QTreeWidgetItem* geometryGroup = projectItem ? projectItem->child(0) : nullptr;
    QTreeWidgetItem* ifcRoot = geometryGroup && geometryGroup->childCount() > 0
                                   ? geometryGroup->child(0) : nullptr;
    const QString ifcRootId = ifcRoot
                                  ? ifcRoot->data(0, Qt::UserRole).toString()
                                  : QString{};
    if (ifcRootId.isEmpty() || !modelTree->selectObjectById(ifcRootId, true))
        return fail("P27 complex IFC root UUID was not created in the model tree.");
    if (QAction* fit = findAction(window, QStringLiteral("Fit All"))) fit->trigger();
    QApplication::processEvents();
    if (!captureAcceptance(window, QStringLiteral("P27-ifc-03-imported.png")))
        return fail("P27 complex IFC imported screenshot failed.");

    convert->trigger();
    QApplication::processEvents();
    projectItem = objectTree->topLevelItem(0);
    geometryGroup = projectItem ? projectItem->child(0) : nullptr;
    if (geometryGroup) geometryGroup->setExpanded(true);
    QApplication::processEvents();
    if (!geometryGroup ||
        window.statusBar()->currentMessage() !=
            QStringLiteral("FDS blocks generated."))
        return fail("P27 complex IFC did not generate FDS obstacles.");
    if (!captureAcceptance(window, QStringLiteral("P27-ifc-04-converted.png")))
        return fail("P27 complex IFC converted screenshot failed.");

    const FdsObjectEditorData meshData{
        QStringLiteral("Complex IFC coarse acceptance domain"),
        QStringLiteral("MESH"), QStringLiteral("IFC_DOMAIN"),
        FcDocumentGroup::Meshes,
        {{QStringLiteral("IJK"), FcFdsParameterKind::Raw,
          QStringLiteral("20,20,10"), {}},
         {QStringLiteral("XB"), FcFdsParameterKind::Raw,
          // The filtered Clinic geometry spans approximately
          // X[-52.31,0.38], Y[-1.00,13.54], Z[-56.52,9.59] m after IFC
          // placement conversion.  Keep a small margin around the actual
          // model instead of using an artificial kilometre-scale domain.
          QStringLiteral("-55,2,-2,15,-58,12"), {}}}};
    if (!runFdsObjectDialog(addObject, meshData))
        return fail("P27 complex IFC mesh was not created through the GUI.");

    const auto validationClosed = std::make_shared<bool>(false);
    QTimer::singleShot(0, [validationClosed]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                *validationClosed = true;
                message->accept();
                return;
            }
        }
    });
    validate->trigger();
    QApplication::processEvents();
    if (!*validationClosed ||
        !window.statusBar()->currentMessage().startsWith(
            QStringLiteral("Model validation passed")))
        return fail("P27 complex IFC model validation failed.");

    const QString projectPath =
        QDir(outputDirectory).filePath(QStringLiteral("p27_complex_ifc.firecae"));
    const QString fdsPath =
        QDir(outputDirectory).filePath(QStringLiteral("p27_complex_ifc.fds"));
    if (!window.saveProjectFile(projectPath) ||
        !window.openProjectFile(projectPath) ||
        !modelTree->selectObjectById(ifcRootId) ||
        !window.exportCurrentProjectToFds(fdsPath))
        return fail("P27 complex IFC save/reopen/UUID/export failed.");

    const auto queued = std::make_shared<bool>(false);
    QTimer::singleShot(0, [queued]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = dynamic_cast<SimulationRunDialog*>(widget);
            if (!dialog) continue;
            auto* backend = dialog->findChild<QComboBox*>(
                QStringLiteral("SimulationRunBackendCombo"));
            if (backend) backend->setCurrentIndex(backend->findData(
                static_cast<int>(SolverBackendKind::FdsSerialCpu)));
            *queued = true;
            dialog->accept();
            return;
        }
    });
    run->trigger();
    QApplication::processEvents();
    if (!*queued) return fail("P27 complex IFC solver task was not queued.");
    auto* taskTable = window.findChild<QTableWidget*>(
        QStringLiteral("SimulationTaskTable"));
    if (!taskTable) return fail("P27 complex IFC task center is unavailable.");
    QEventLoop taskLoop;
    QTimer taskPoll;
    QTimer taskTimeout;
    taskPoll.setInterval(100);
    taskTimeout.setSingleShot(true);
    QObject::connect(&taskPoll, &QTimer::timeout, &taskLoop,
                     [&taskLoop, taskTable]() {
                         if (taskTable->rowCount() == 0 || !taskTable->item(0, 0)) return;
                         const QString state = taskTable->item(0, 0)->text();
                         if (state == QStringLiteral("Completed") ||
                             state == QStringLiteral("Failed") ||
                             state == QStringLiteral("Cancelled")) taskLoop.quit();
                     });
    QObject::connect(&taskTimeout, &QTimer::timeout, &taskLoop, &QEventLoop::quit);
    taskPoll.start();
    taskTimeout.start(600000);
    taskLoop.exec();
    taskPoll.stop();
    QApplication::processEvents();
    const QString taskState = taskTable->rowCount() > 0 && taskTable->item(0, 0)
                                  ? taskTable->item(0, 0)->text() : QString{};
    const auto* taskManager = window.findChild<SimulationTaskManager*>();
    if (!taskManager || taskManager->tasks().size() != 1)
        return fail("P27 IFC did not retain exactly one identifiable task.");
    const FdsRunSummary completedRun = taskManager->tasks().constFirst().summary;
    const QString outPath = completedRun.outputFilePath;
    const QString smvPath = completedRun.smvFilePath;
    QFile outFile(outPath);
    if (taskState != QStringLiteral("Completed") || !completedRun.success ||
        !outFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !QString::fromUtf8(outFile.readAll()).contains(
            QStringLiteral("STOP: FDS completed successfully")) ||
        !QFileInfo::exists(smvPath))
        return fail("P27 complex IFC real FDS calculation did not complete normally.");
    if (!window.openResultFile(smvPath) ||
        !captureAcceptance(window, QStringLiteral("P27-ifc-05-solved-results.png")))
        return fail("P27 complex IFC result load/screenshot failed.");
    window.close();
    QApplication::processEvents();
    std::cout << "FireCAE P27 complex IFC GUI/FDS acceptance passed.\n";
    return 0;
}

FcFdsParameter p29Raw(const char* key, const char* value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::Raw,
            QString::fromLatin1(value), {}};
}

FcFdsParameter p29Text(const char* key, const char* value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::String,
            QString::fromLatin1(value), {}};
}

FcFdsParameter p29Reference(const char* key,
                            const std::shared_ptr<FcFdsNamelist>& target)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::ObjectReferences,
            {}, target ? QStringList{target->id()} : QStringList{}};
}

FcFdsParameter p29References(
    const char* key,
    std::initializer_list<std::shared_ptr<FcFdsNamelist>> targets)
{
    QStringList ids;
    for (const auto& target : targets) if (target) ids.append(target->id());
    return {QString::fromLatin1(key), FcFdsParameterKind::ObjectReferences,
            {}, ids};
}

std::shared_ptr<FcFdsNamelist> p29Add(
    const std::shared_ptr<FcObjectGroup>& group,
    const QString& name,
    FcObjectType type,
    const char* keyword,
    const QString& fdsId,
    int sequence,
    std::initializer_list<FcFdsParameter> parameters)
{
    auto object = std::make_shared<FcFdsNamelist>(
        name, type, QString::fromLatin1(keyword), fdsId, sequence);
    object->setParameters(std::vector<FcFdsParameter>(parameters));
    if (group) group->addChild(object);
    return object;
}

std::unique_ptr<FcProject> createP29BasicDataOutputRecipe()
{
    auto project = createFirstFireAcceptanceRecipe();
    project->setName(QStringLiteral("Basic Data Output / 基础数据输出"));
    project->setChid(QStringLiteral("basic_data_output"));
    project->setEndTime(2.0);
    FcDocument* document = project->document();
    p29Add(document->outputsGroup(), QStringLiteral("Velocity Vector Slice"),
           FcObjectType::Output, "SLCF", {}, 9,
           {p29Raw("PBX", "3"), p29Text("QUANTITY", "VELOCITY"),
            p29Raw("VECTOR", ".TRUE.")});
    p29Add(document->outputsGroup(), QStringLiteral("Wall Temperature Boundary"),
           FcObjectType::Output, "BNDF", {}, 10,
           {p29Text("QUANTITY", "WALL TEMPERATURE")});
    p29Add(document->outputsGroup(), QStringLiteral("Temperature Isosurfaces"),
           FcObjectType::Output, "ISOF", {}, 11,
           {p29Text("QUANTITY", "TEMPERATURE"),
            p29Raw("VALUE", "50,100,200")});
    p29Add(document->outputsGroup(), QStringLiteral("Temperature Plot3D"),
           FcObjectType::Output, "PL3D", {}, 12,
           {p29Text("QUANTITY", "TEMPERATURE")});
    for (const FcObject::Ptr& child : document->outputsGroup()->children()) {
        const auto output = std::dynamic_pointer_cast<FcFdsNamelist>(child);
        if (!output || output->keyword() != QStringLiteral("DUMP")) continue;
        output->addRawParameter(QStringLiteral("DT_BNDF"), QStringLiteral("0.5"));
        output->addRawParameter(QStringLiteral("DT_ISOF"), QStringLiteral("0.5"));
        output->addRawParameter(QStringLiteral("DT_PL3D"), QStringLiteral("1"));
        break;
    }
    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createP29ImportingGeometryRecipe()
{
    auto project = std::make_unique<FcProject>(
        QStringLiteral("Importing Geometry / 导入几何"));
    project->setChid(QStringLiteral("importing_geometry"));
    project->setEndTime(0.2);
    project->setFdsVersion(QStringLiteral("6.11.1"));
    FcDocument* document = project->document();
    p29Add(document->meshesGroup(), QStringLiteral("Imported Geometry Domain"),
           FcObjectType::Mesh, "MESH", QStringLiteral("DOMAIN"), 0,
           {p29Raw("IJK", "10,10,10"), p29Raw("XB", "0,2,0,2,0,2")});
    p29Add(document->geometryGroup(), QStringLiteral("Converted Tetra Envelope"),
           FcObjectType::Obstruction, "OBST", QStringLiteral("TETRA_OBST"), 1,
           {p29Raw("XB", "0,1,0,1,0,1"), p29Text("COLOR", "BLUE")});
    p29Add(document->ventsGroup(), QStringLiteral("Open X Boundary"),
           FcObjectType::Vent, "VENT", QStringLiteral("OPEN_XMAX"), 2,
           {p29Text("MB", "XMAX"), p29Text("SURF_ID", "OPEN")});
    p29Add(document->outputsGroup(), QStringLiteral("Output Cadence"),
           FcObjectType::Output, "DUMP", {}, 3,
           {p29Raw("NFRAMES", "2")});
    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createP29LayeredMaterialsRecipe()
{
    auto project = std::make_unique<FcProject>(
        QStringLiteral("Materials and Layered Surfaces / 材料与分层表面"));
    project->setChid(QStringLiteral("materials_layered_surfaces"));
    project->setEndTime(0.5);
    project->setFdsVersion(QStringLiteral("6.11.1"));
    FcDocument* document = project->document();
    p29Add(document->meshesGroup(), QStringLiteral("Layered Wall Domain"),
           FcObjectType::Mesh, "MESH", QStringLiteral("DOMAIN"), 0,
           {p29Raw("IJK", "12,12,8"), p29Raw("XB", "0,3,0,3,0,2")});
    const auto gypsum = p29Add(
        document->materialsGroup(), QStringLiteral("Gypsum Board"),
        FcObjectType::Material, "MATL", QStringLiteral("GYPSUM"), 1,
        {p29Raw("DENSITY", "800"), p29Raw("CONDUCTIVITY", "0.17"),
         p29Raw("SPECIFIC_HEAT", "1.09")});
    const auto concrete = p29Add(
        document->materialsGroup(), QStringLiteral("Concrete"),
        FcObjectType::Material, "MATL", QStringLiteral("CONCRETE"), 2,
        {p29Raw("DENSITY", "2280"), p29Raw("CONDUCTIVITY", "1.8"),
         p29Raw("SPECIFIC_HEAT", "1.04")});
    const auto wall = p29Add(
        document->surfacesGroup(), QStringLiteral("Gypsum + Concrete Wall"),
        FcObjectType::Surface, "SURF", QStringLiteral("LAYERED_WALL"), 3,
        {p29References("MATL_ID(1:2,1)", {gypsum, concrete}),
         p29Raw("THICKNESS(1:2)", "0.012,0.10"),
         p29Text("COLOR", "GRAY")});
    p29Add(document->geometryGroup(), QStringLiteral("Layered Wall Obstruction"),
           FcObjectType::Obstruction, "OBST", QStringLiteral("WALL"), 4,
           {p29Raw("XB", "1.25,1.5,0.5,2.5,0,2"),
            p29Reference("SURF_ID", wall)});
    p29Add(document->ventsGroup(), QStringLiteral("Open X Boundary"),
           FcObjectType::Vent, "VENT", QStringLiteral("OPEN_XMAX"), 5,
           {p29Text("MB", "XMAX"), p29Text("SURF_ID", "OPEN")});
    p29Add(document->devicesGroup(), QStringLiteral("Wall Temperature Probe"),
           FcObjectType::Device, "DEVC", QStringLiteral("WALL_TEMP"), 6,
           {p29Raw("XYZ", "1,1.5,1"), p29Text("QUANTITY", "TEMPERATURE")});
    p29Add(document->outputsGroup(), QStringLiteral("Output Cadence"),
           FcObjectType::Output, "DUMP", {}, 7,
           {p29Raw("DT_DEVC", "0.1")});
    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createP29ProtectionControlsRecipe()
{
    auto project = FdsExamples::createActivateVentsProject();
    project->setName(QStringLiteral(
        "Fire Protection Systems and Controls / 消防系统与控制"));
    project->setChid(QStringLiteral("fire_protection_controls"));
    project->setEndTime(12.2);
    project->setFdsVersion(QStringLiteral("6.11.1"));
    project->setModified(false);
    return project;
}

std::unique_ptr<FcProject> createP29DesignScenariosRecipe()
{
    auto project = createFirstFireAcceptanceRecipe();
    project->setName(QStringLiteral("Fire Design Scenarios / 火灾设计场景"));
    project->setChid(QStringLiteral("fire_design_scenarios"));
    project->setEndTime(0.5);
    project->setFdsVersion(QStringLiteral("6.11.1"));
    project->setModified(false);
    return project;
}

bool writeP29TetraStl(const QString& filePath)
{
    QFile file(filePath);
    const QByteArray contents(
        "solid tetra\n"
        "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n"
        "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 0 0 1\nvertex 1 0 0\nendloop\nendfacet\n"
        "facet normal -1 0 0\nouter loop\nvertex 0 0 0\nvertex 0 1 0\nvertex 0 0 1\nendloop\nendfacet\n"
        "facet normal 1 1 1\nouter loop\nvertex 1 0 0\nvertex 0 0 1\nvertex 0 1 0\nendloop\nendfacet\n"
        "endsolid tetra\n");
    return file.open(QIODevice::WriteOnly) &&
           file.write(contents) == contents.size();
}

bool runP29StlImportWizard(QAction* action, const QString& filePath,
                           const QString& screenshotName)
{
    if (!action || !action->isEnabled() || !QFileInfo::exists(filePath)) return false;
    const auto configured = std::make_shared<bool>(false);
    const auto accepted = std::make_shared<bool>(false);
    const auto previewReady = std::make_shared<bool>(false);
    QTimer::singleShot(0, [configured, accepted, previewReady, filePath,
                           screenshotName]() {
        GeometryImportWizard* wizard = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            wizard = dynamic_cast<GeometryImportWizard*>(widget);
            if (wizard) break;
        }
        if (!wizard) return;
        QObject::connect(wizard, &QDialog::finished,
                         [accepted](int result) {
                             *accepted = result == QDialog::Accepted;
                         });
        auto* fileEdit = wizard->findChild<QLineEdit*>(
            QStringLiteral("GeometryImportFileEdit"));
        if (!fileEdit) { wizard->reject(); return; }
        fileEdit->setText(filePath);
        *configured = true;
        wizard->next();
        wizard->next();
        wizard->next();
        const QPointer<GeometryImportWizard> guard(wizard);
        const auto poll = std::make_shared<std::function<void()>>();
        *poll = [guard, poll, previewReady, screenshotName]() {
            if (!guard) return;
            auto* progress = guard->findChild<QProgressBar*>(
                QStringLiteral("GeometryImportProgressBar"));
            auto* preview = guard->findChild<QPlainTextEdit*>(
                QStringLiteral("GeometryImportPreviewReport"));
            if (!progress || !preview) { guard->reject(); return; }
            if (progress->value() >= 100 &&
                preview->toPlainText().contains(QStringLiteral("Triangles: 4"))) {
                *previewReady = captureDialogAcceptance(*guard, screenshotName);
                guard->accept();
                return;
            }
            QTimer::singleShot(50, guard, *poll);
        };
        QTimer::singleShot(50, guard, *poll);
    });
    action->trigger();
    QApplication::processEvents();
    return *configured && *previewReady && *accepted;
}

bool waitForCompletedTasks(QTableWidget* table, int expectedRows,
                           int timeoutMilliseconds)
{
    if (!table) return false;
    const auto terminal = [table, expectedRows]() {
        if (table->rowCount() < expectedRows) return false;
        for (int row = 0; row < expectedRows; ++row) {
            if (!table->item(row, 0)) return false;
            const QString state = table->item(row, 0)->text();
            if (state != QStringLiteral("Completed") &&
                state != QStringLiteral("Failed") &&
                state != QStringLiteral("Cancelled")) return false;
        }
        return true;
    };
    if (!terminal()) {
        QEventLoop loop;
        QTimer poll;
        QTimer timeout;
        poll.setInterval(100);
        timeout.setSingleShot(true);
        QObject::connect(&poll, &QTimer::timeout, &loop,
                         [&]() { if (terminal()) loop.quit(); });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start();
        timeout.start(timeoutMilliseconds);
        loop.exec();
    }
    if (!terminal()) return false;
    for (int row = 0; row < expectedRows; ++row) {
        if (table->item(row, 0)->text() != QStringLiteral("Completed")) return false;
    }
    return true;
}

bool configureP29ScenarioStudy(QAction* action)
{
    if (!action || !action->isEnabled()) return false;
    const auto configured = std::make_shared<bool>(false);
    QTimer::singleShot(0, [configured]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("ScenarioManagerDialog")) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() == QStringLiteral("ScenarioManagerDialog")) {
                    dialog = qobject_cast<QDialog*>(widget); break;
                }
            }
        }
        if (!dialog) return;
        auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("ScenarioNameEdit"));
        auto* chid = dialog->findChild<QLineEdit*>(QStringLiteral("ScenarioChidEdit"));
        auto* study = dialog->findChild<QPushButton*>(
            QStringLiteral("ScenarioParameterStudyButton"));
        auto* list = dialog->findChild<QListWidget*>(QStringLiteral("ScenarioList"));
        if (!name || !chid || !study || !list) { dialog->reject(); return; }
        name->setText(QStringLiteral("Baseline"));
        chid->setText(QStringLiteral("fire_design_scenarios"));
        QTimer::singleShot(0, dialog, []() {
            QDialog* studyDialog = nullptr;
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() == QStringLiteral("ParameterStudyDialog")) {
                    studyDialog = qobject_cast<QDialog*>(widget); break;
                }
            }
            if (!studyDialog) return;
            auto* prefix = studyDialog->findChild<QLineEdit*>(
                QStringLiteral("ParameterStudyPrefixEdit"));
            auto* object = studyDialog->findChild<QComboBox*>(
                QStringLiteral("ParameterStudyFirstObjectCombo"));
            auto* parameter = studyDialog->findChild<QLineEdit*>(
                QStringLiteral("ParameterStudyFirstParameterEdit"));
            auto* values = studyDialog->findChild<QLineEdit*>(
                QStringLiteral("ParameterStudyFirstValuesEdit"));
            if (!prefix || !object || !parameter || !values) {
                studyDialog->reject(); return;
            }
            prefix->setText(QStringLiteral("Tutorial Variant"));
            int burnerIndex = -1;
            for (int index = 0; index < object->count(); ++index) {
                if (object->itemText(index).contains(
                        QStringLiteral("500 kW Burner Surface"))) {
                    burnerIndex = index; break;
                }
            }
            if (burnerIndex < 0) { studyDialog->reject(); return; }
            object->setCurrentIndex(burnerIndex);
            parameter->setText(QStringLiteral("HRRPUA"));
            values->setText(QStringLiteral("250; 500"));
            studyDialog->accept();
        });
        study->click();
        QApplication::processEvents();
        if (list->count() != 3) { dialog->reject(); return; }
        *configured = true;
        dialog->accept();
    });
    action->trigger();
    QApplication::processEvents();
    return *configured;
}

struct P29TutorialCase
{
    QString key;
    std::function<std::unique_ptr<FcProject>()> factory;
    bool importStl = false;
    bool scenarioStudy = false;
};

int runP29FiveTutorialEndToEndAcceptance()
{
    QTemporaryDir temporaryDirectory;
    QString acceptanceRoot = qEnvironmentVariable("FIRECAE_P29_TUTORIAL_DIR");
    if (acceptanceRoot.isEmpty()) acceptanceRoot = temporaryDirectory.path();
    if (acceptanceRoot.isEmpty() || !QDir().mkpath(acceptanceRoot))
        return fail("P29 tutorial acceptance directory is unavailable.");
    const QString stlPath = QDir(acceptanceRoot).filePath(QStringLiteral("tutorial_tetra.stl"));
    if (!writeP29TetraStl(stlPath)) return fail("P29 STL fixture could not be written.");

    const std::vector<P29TutorialCase> cases = {
        {QStringLiteral("basic_data_output"), createP29BasicDataOutputRecipe},
        {QStringLiteral("importing_geometry"), createP29ImportingGeometryRecipe, true},
        {QStringLiteral("materials_layered_surfaces"), createP29LayeredMaterialsRecipe},
        {QStringLiteral("fire_protection_controls"), createP29ProtectionControlsRecipe},
        {QStringLiteral("fire_design_scenarios"), createP29DesignScenariosRecipe,
         false, true}
    };

    for (const P29TutorialCase& tutorialCase : cases) {
        std::cerr << "P29 GUI tutorial starting: " << tutorialCase.key.toStdString()
                  << std::endl;
        std::unique_ptr<FcProject> sourceProject = tutorialCase.factory();
        if (!sourceProject || !sourceProject->document())
            return fail("P29 tutorial recipe is unavailable.");
        const std::vector<TutorialObjectRecipe> recipes =
            tutorialObjectRecipes(*sourceProject);
        if (recipes.empty()) return fail("P29 tutorial recipe has no FDS objects.");
        const QString caseDirectory = QDir(acceptanceRoot).filePath(tutorialCase.key);
        if (!QDir().mkpath(caseDirectory))
            return fail("P29 tutorial case directory could not be created.");

        MainWindow window;
        window.resize(1500, 920);
        window.show();
        QApplication::processEvents();
        const QString prefix = QStringLiteral("P29-%1-").arg(tutorialCase.key);
        if (!captureAcceptance(window, prefix + QStringLiteral("01-blank.png")))
            return fail("P29 blank-project screenshot failed.");
        auto* modelTree = window.findChild<ModelTreeWidget*>();
        QAction* projectSettings = window.findChild<QAction*>(
            QStringLiteral("ProjectSettingsAction"));
        QAction* addObject = window.findChild<QAction*>(
            QStringLiteral("AddFdsObjectAction"));
        QAction* editObject = findAction(window, QStringLiteral("Edit Selected Object..."));
        QAction* validate = window.findChild<QAction*>(
            QStringLiteral("ValidateModelAction"));
        QAction* run = window.findChild<QAction*>(QStringLiteral("RunFdsAction"));
        if (!modelTree || !projectSettings || !addObject || !editObject ||
            !validate || !run)
            return fail("P29 required production GUI actions are missing.");
        if (!runProjectSettingsDialog(projectSettings, *sourceProject))
            return fail("P29 project settings were not entered through the GUI.");

        if (tutorialCase.importStl) {
            QAction* importAction = window.findChild<QAction*>(
                QStringLiteral("ImportGeometryAction"));
            if (!runP29StlImportWizard(
                    importAction, stlPath, prefix + QStringLiteral("02-import-wizard.png")))
                return fail("P29 STL was not imported through the production wizard.");
        }

        QHash<QString, QString> targetUuidBySourceUuid;
        for (const TutorialObjectRecipe& recipe : recipes) {
            if (!runFdsObjectDialog(addObject,
                                    creationPassData(recipe, *sourceProject)))
                return fail("P29 FDS object creation dialog failed.");
            const QString targetUuid = modelTree->selectedObjectId();
            if (targetUuid.isEmpty() ||
                targetUuidBySourceUuid.values().contains(targetUuid))
                return fail("P29 GUI did not create a unique object UUID.");
            targetUuidBySourceUuid.insert(recipe.sourceUuid, targetUuid);
        }
        for (const TutorialObjectRecipe& recipe : recipes) {
            const bool hasReferences = std::any_of(
                recipe.editorData.parameters.cbegin(),
                recipe.editorData.parameters.cend(),
                [](const FcFdsParameter& parameter) {
                    return parameter.kind == FcFdsParameterKind::ObjectReferences;
                });
            if (!hasReferences) continue;
            FdsObjectEditorData finalData = recipe.editorData;
            if (!remapReferenceData(finalData, targetUuidBySourceUuid) ||
                !modelTree->selectObjectById(
                    targetUuidBySourceUuid.value(recipe.sourceUuid), true) ||
                !runFdsObjectDialog(editObject, finalData))
                return fail("P29 UUID reference binding failed.");
        }
        if (QAction* fit = findAction(window, QStringLiteral("Fit All"))) fit->trigger();
        QApplication::processEvents();
        if (!captureAcceptance(window, prefix + QStringLiteral("03-modeled.png")))
            return fail("P29 modeled-project screenshot failed.");

        const auto validationClosed = std::make_shared<bool>(false);
        QTimer::singleShot(0, [validationClosed]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                    *validationClosed = true; message->accept(); return;
                }
            }
        });
        validate->trigger();
        QApplication::processEvents();
        if (!*validationClosed ||
            !window.statusBar()->currentMessage().startsWith(
                QStringLiteral("Model validation passed")))
            return fail("P29 GUI model validation failed.");

        if (tutorialCase.scenarioStudy) {
            QAction* scenarios = window.findChild<QAction*>(
                QStringLiteral("ScenarioManagerAction"));
            if (!configureP29ScenarioStudy(scenarios))
                return fail("P29 scenario parameter study was not created through the GUI.");
        }

        const QString projectPath = QDir(caseDirectory).filePath(
            tutorialCase.key + QStringLiteral(".firecae"));
        const QString fdsPath = QDir(caseDirectory).filePath(
            tutorialCase.key + QStringLiteral(".fds"));
        if (!window.saveProjectFile(projectPath) ||
            !window.openProjectFile(projectPath) ||
            !window.exportCurrentProjectToFds(fdsPath))
            return fail("P29 project save/reopen/export failed.");
        for (const QString& uuid : targetUuidBySourceUuid) {
            if (!modelTree->selectObjectById(uuid))
                return fail("P29 an FDS object UUID changed after save/reopen.");
        }
        if (!captureAcceptance(window, prefix + QStringLiteral("04-exported.png")))
            return fail("P29 exported-project screenshot failed.");

        auto* taskTable = window.findChild<QTableWidget*>(
            QStringLiteral("SimulationTaskTable"));
        QStringList smvFiles;
        if (tutorialCase.scenarioStudy) {
            const QString batchRoot = QDir(caseDirectory).filePath(QStringLiteral("batch"));
            qputenv("FIRECAE_AUTOMATION_BATCH_ROOT", batchRoot.toLocal8Bit());
            qputenv("FIRECAE_AUTOMATION_BATCH_CONCURRENCY", "1");
            QAction* batch = window.findChild<QAction*>(
                QStringLiteral("RunAllScenariosAction"));
            if (!batch || !batch->isEnabled()) {
                qunsetenv("FIRECAE_AUTOMATION_BATCH_ROOT");
                qunsetenv("FIRECAE_AUTOMATION_BATCH_CONCURRENCY");
                return fail("P29 scenario batch action is unavailable.");
            }
            batch->trigger();
            qunsetenv("FIRECAE_AUTOMATION_BATCH_ROOT");
            qunsetenv("FIRECAE_AUTOMATION_BATCH_CONCURRENCY");
            if (!waitForCompletedTasks(taskTable, 3, 240000))
                return fail("P29 scenario batch did not complete three tasks.");
            QDirIterator iterator(batchRoot, {QStringLiteral("*.smv")},
                                  QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext()) smvFiles.append(iterator.next());
            if (smvFiles.size() != 3)
                return fail("P29 scenario batch did not produce three result cases.");
        } else {
            const auto queued = std::make_shared<bool>(false);
            QTimer::singleShot(0, [queued]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    auto* dialog = dynamic_cast<SimulationRunDialog*>(widget);
                    if (!dialog) continue;
                    auto* backend = dialog->findChild<QComboBox*>(
                        QStringLiteral("SimulationRunBackendCombo"));
                    if (backend) backend->setCurrentIndex(backend->findData(
                        static_cast<int>(SolverBackendKind::FdsSerialCpu)));
                    *queued = true; dialog->accept(); return;
                }
            });
            run->trigger();
            QApplication::processEvents();
            if (!*queued || !waitForCompletedTasks(taskTable, 1, 240000))
                return fail("P29 real FDS calculation did not complete.");
            const auto* taskManager = window.findChild<SimulationTaskManager*>();
            if (!taskManager || taskManager->tasks().size() != 1)
                return fail("P29 did not retain exactly one identifiable task.");
            const FdsRunSummary completedRun = taskManager->tasks().constFirst().summary;
            const QString smvPath = completedRun.smvFilePath;
            if (!completedRun.success || !QFileInfo::exists(smvPath))
                return fail("P29 real FDS result manifest is missing.");
            smvFiles.append(smvPath);
        }

        if (!window.openResultFile(smvFiles.constFirst()) ||
            !captureAcceptance(window, prefix + QStringLiteral("05-results.png")))
            return fail("P29 result load or screenshot failed.");

        QSaveFile report(QDir(caseDirectory).filePath(
            QStringLiteral("P29-ACCEPTANCE.md")));
        if (!report.open(QIODevice::WriteOnly | QIODevice::Text))
            return fail("P29 tutorial report could not be opened.");
        QTextStream stream(&report);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "# " << tutorialCase.key << " — P29 GUI/FDS 验收\n\n"
               << "- 起点：空工程\n"
               << "- 项目 Schema：`6.11.1`\n"
               << "- 通过正式 FDS 对象对话框创建：" << recipes.size() << " 个对象\n"
               << "- UUID 引用、保存/重开、校验、导出：PASS\n"
               << "- 实际 FDS 任务：" << (tutorialCase.scenarioStudy ? 3 : 1)
               << " 个，全部 Completed\n"
               << "- 结果加载：PASS\n"
               << "- 工程：`" << QDir::toNativeSeparators(projectPath) << "`\n"
               << "- FDS：`" << QDir::toNativeSeparators(fdsPath) << "`\n";
        if (!report.commit()) return fail("P29 tutorial report could not be committed.");
        window.close();
        QApplication::processEvents();
        std::cout << "P29 GUI/FDS tutorial passed: "
                  << tutorialCase.key.toStdString() << "\n";
    }
    std::cout << "FireCAE P29 five-tutorial GUI/FDS acceptance passed.\n";
    return 0;
}

int runBlankTutorialGuiAcceptance()
{
    struct TutorialCase
    {
        QString key;
        std::function<std::unique_ptr<FcProject>()> factory;
    };
    const std::vector<TutorialCase> cases = {
        {QStringLiteral("activate_vents"), FdsExamples::createActivateVentsProject},
        {QStringLiteral("bucket_test_2"), FdsExamples::createBucketTest2Project},
        {QStringLiteral("couch"), FdsExamples::createCouchProject},
        {QStringLiteral("couch_smoke_12s"), FdsExamples::createCouchSmoke12sProject},
        {QStringLiteral("HVAC_aircoil"), FdsExamples::createHvacAircoilProject},
        {QStringLiteral("tunnel_demo"), FdsExamples::createTunnelDemoProject},
        {QStringLiteral("tunnel_smoke_10s"), FdsExamples::createTunnelSmoke10sProject},
    };

    QTemporaryDir temporaryAcceptanceDirectory;
    QString acceptanceRoot =
        qEnvironmentVariable("FIRECAE_TUTORIAL_ACCEPTANCE_DIR");
    if (acceptanceRoot.isEmpty()) {
        acceptanceRoot = temporaryAcceptanceDirectory.path();
    }
    if (acceptanceRoot.isEmpty() || !QDir().mkpath(acceptanceRoot)) {
        return fail("A10 blank-project tutorial acceptance directory could not be created.");
    }

    for (const TutorialCase& tutorialCase : cases) {
        std::cerr << "A10 blank GUI starting case: "
                  << tutorialCase.key.toStdString() << std::endl;
        std::unique_ptr<FcProject> sourceProject = tutorialCase.factory();
        if (!sourceProject || !sourceProject->document()) {
            return fail("A10 tutorial recipe project could not be created.");
        }
        const std::vector<TutorialObjectRecipe> recipes =
            tutorialObjectRecipes(*sourceProject);
        if (recipes.empty()) {
            return fail("A10 tutorial recipe contains no editable FDS objects.");
        }

        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-01-blank-project.png"))) {
            return fail("A10 blank-project screenshot capture failed.");
        }
        auto* modelTree = window.findChild<ModelTreeWidget*>();
        QAction* projectSettings = findAction(window, QStringLiteral("Project Settings..."));
        QAction* addObject = findAction(window, QStringLiteral("Add FDS Object..."));
        QAction* editObject = findAction(window, QStringLiteral("Edit Selected Object..."));
        QAction* validate = findAction(window, QStringLiteral("Validate Model"));
        if (!modelTree || !projectSettings || !addObject || !editObject || !validate) {
            return fail("A10 blank-project GUI actions are incomplete.");
        }
        if (!runProjectSettingsDialog(projectSettings, *sourceProject)) {
            return fail("A10 project settings were not entered through the GUI dialog.");
        }
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-02-project-settings.png"))) {
            return fail("A10 project-settings screenshot capture failed.");
        }

        QHash<QString, QString> targetUuidBySourceUuid;
        for (const TutorialObjectRecipe& recipe : recipes) {
            std::cerr << "  create &"
                      << recipe.editorData.keyword.toStdString() << ' '
                      << recipe.editorData.name.toStdString() << std::endl;
            const FdsObjectEditorData firstPass =
                creationPassData(recipe, *sourceProject);
            if (!runFdsObjectDialog(addObject, firstPass)) {
                return fail("A10 FDS object creation dialog was not completed.");
            }
            const QString targetUuid = modelTree->selectedObjectId();
            if (targetUuid.isEmpty() ||
                targetUuidBySourceUuid.values().contains(targetUuid)) {
                return fail("A10 GUI object creation did not produce a unique UUID mapping.");
            }
            targetUuidBySourceUuid.insert(recipe.sourceUuid, targetUuid);
        }
        if (targetUuidBySourceUuid.size() != static_cast<int>(recipes.size())) {
            return fail("A10 GUI object creation count does not match the tutorial recipe.");
        }
        if (QAction* fit = findAction(window, QStringLiteral("Fit All"))) fit->trigger();
        QApplication::processEvents();
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-03-objects-created.png"))) {
            return fail("A10 object-creation screenshot capture failed.");
        }

        for (const TutorialObjectRecipe& recipe : recipes) {
            const bool hasReferences = std::any_of(
                recipe.editorData.parameters.cbegin(),
                recipe.editorData.parameters.cend(),
                [](const FcFdsParameter& parameter) {
                    return parameter.kind == FcFdsParameterKind::ObjectReferences;
                });
            if (!hasReferences) continue;
            std::cerr << "  bind references &"
                      << recipe.editorData.keyword.toStdString() << ' '
                      << recipe.editorData.name.toStdString() << std::endl;
            FdsObjectEditorData finalData = recipe.editorData;
            if (!remapReferenceData(finalData, targetUuidBySourceUuid)) {
                return fail("A10 GUI UUID reference remapping is incomplete.");
            }
            if (!modelTree->selectObjectById(
                    targetUuidBySourceUuid.value(recipe.sourceUuid), true)) {
                return fail("A10 GUI could not select the created object by UUID.");
            }
            QApplication::processEvents();
            if (!runFdsObjectDialog(editObject, finalData)) {
                return fail("A10 reference edit dialog was not completed.");
            }
        }
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-04-uuid-references.png"))) {
            return fail("A10 UUID-reference screenshot capture failed.");
        }

        const auto validationClosed = std::make_shared<bool>(false);
        QTimer::singleShot(0, [validationClosed]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                    *validationClosed = true;
                    message->accept();
                    return;
                }
            }
        });
        validate->trigger();
        QApplication::processEvents();
        if (!*validationClosed ||
            !window.statusBar()->currentMessage().startsWith(
                QStringLiteral("Model validation passed"))) {
            return fail("A10 GUI validation did not pass for a blank-project tutorial.");
        }
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-05-validation-passed.png"))) {
            return fail("A10 validation screenshot capture failed.");
        }

        const QString caseDirectory = QDir(acceptanceRoot)
            .filePath(tutorialCase.key);
        if (!QDir().mkpath(caseDirectory)) {
            return fail("A10 tutorial acceptance case directory could not be created.");
        }
        const QString projectPath = QDir(caseDirectory).filePath(
            tutorialCase.key + QStringLiteral(".firecae"));
        const QString exportedPath = QDir(caseDirectory).filePath(
            tutorialCase.key + QStringLiteral(".fds"));
        if (!window.saveProjectFile(projectPath) ||
            !window.openProjectFile(projectPath)) {
            return fail("A10 GUI-built tutorial did not survive save/reopen.");
        }
        for (const QString& targetUuid : targetUuidBySourceUuid) {
            if (!modelTree->selectObjectById(targetUuid)) {
                return fail("A10 save/reopen changed an object UUID.");
            }
        }
        if (!window.exportCurrentProjectToFds(exportedPath)) {
            return fail("A10 GUI-built tutorial could not export FDS input.");
        }
        const QString officialPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-tutorials/%1/%1.fds")
                          .arg(tutorialCase.key));
        const FdsInputComparison comparison =
            FdsInputComparator::compareFiles(officialPath, exportedPath);
        if (!comparison.success() || !comparison.semanticallyEquivalent()) {
            std::cerr << "A10 semantic mismatch for "
                      << tutorialCase.key.toStdString() << ": "
                      << comparison.errorMessage.toStdString() << '\n';
            for (const FdsInputDifference& difference : comparison.differences) {
                if (!difference.acceptable) {
                    std::cerr << difference.category.toStdString() << " | "
                              << difference.recordKey.toStdString() << " | "
                              << difference.parameter.toStdString() << " | "
                              << difference.referenceValue.toStdString() << " -> "
                              << difference.candidateValue.toStdString() << '\n';
                }
            }
            return fail("A10 GUI-built tutorial is not semantically equivalent to the official FDS input.");
        }
        if (!writeTutorialGuiGuide(caseDirectory, tutorialCase.key,
                                   *sourceProject, recipes,
                                   targetUuidBySourceUuid, officialPath,
                                   exportedPath)) {
            return fail("A10 GUI reconstruction guide could not be written.");
        }
        if (QAction* fit = findAction(window, QStringLiteral("Fit All"))) fit->trigger();
        QApplication::processEvents();
        if (!captureAcceptance(
                window, tutorialCase.key + QStringLiteral("-06-reopened-exported.png"))) {
            return fail("A10 blank-project tutorial screenshot capture failed.");
        }
        std::cout << "A10 blank GUI tutorial passed: "
                  << tutorialCase.key.toStdString() << " ("
                  << recipes.size() << " objects)\n";
        window.close();
        QApplication::processEvents();
    }
    std::cout << "FireCAE A10 seven-tutorial blank-project GUI acceptance passed.\n";
    return 0;
}

int runP32ParticleSprayWizardAcceptance()
{
    QTemporaryDir temporaryDirectory;
    QString acceptanceRoot = qEnvironmentVariable("FIRECAE_P32_ACCEPTANCE_DIR");
    if (acceptanceRoot.isEmpty()) acceptanceRoot = temporaryDirectory.path();
    if (acceptanceRoot.isEmpty() || !QDir().mkpath(acceptanceRoot))
        return fail("P32 acceptance output directory is unavailable.");

    MainWindow window;
    window.resize(1280, 800);
    window.show();
    window.loadSimpleTestBenchmark();
    QApplication::processEvents();

    QAction* parametersAction = window.findChild<QAction*>(
        QStringLiteral("SimulationParametersAction"));
    if (!parametersAction)
        return fail("P32 simulation parameters action is missing.");
    acceptNextNamedDialog(QStringLiteral("SimulationParametersDialog"),
                          [](QDialog* dialog) {
        if (auto* endTime = dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("SimulationEndTimeSpin"))) endTime->setValue(2.0);
    });
    parametersAction->trigger();
    QApplication::processEvents();

    QAction* wizardAction = window.findChild<QAction*>(
        QStringLiteral("ParticleSprayWizardAction"));
    if (!wizardAction) return fail("P32 particle/sprinkler wizard action is missing.");

    bool configured = false;
    QTimer::singleShot(0, &window, [&configured]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || dialog->objectName() !=
                               QStringLiteral("ParticleSprayWizardDialog")) continue;
            auto setText = [dialog](const char* objectName, const QString& value) {
                if (auto* edit = dialog->findChild<QLineEdit*>(
                        QString::fromLatin1(objectName))) edit->setText(value);
            };
            setText("ParticleSprayNameEdit", QStringLiteral("P32 Sprinkler"));
            setText("ParticleSpraySpeciesIdEdit", QStringLiteral("WATER VAPOR"));
            setText("ParticleSprayParticleIdEdit", QStringLiteral("P32_DROPLETS"));
            setText("ParticleSprayPropertyIdEdit", QStringLiteral("P32_SPRINKLER"));
            setText("ParticleSprayTableIdEdit", QStringLiteral("P32_SPRAY_TABLE"));
            setText("ParticleSprayDeviceIdEdit", QStringLiteral("P32_DEVICE"));
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayDiameterSpin"))->setValue(1250.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayFlowRateSpin"))->setValue(72.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayVelocitySpin"))->setValue(6.5);
            dialog->findChild<QSpinBox*>(
                QStringLiteral("ParticleSprayParticlesPerSecondSpin"))->setValue(12000);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayActivationTemperatureSpin"))->setValue(68.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayRtiSpin"))->setValue(35.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayXSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayYSpin"))->setValue(2.0);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayZSpin"))->setValue(2.2);
            dialog->findChild<QDoubleSpinBox*>(
                QStringLiteral("ParticleSprayOutputIntervalSpin"))->setValue(0.25);
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("ParticleSprayPatternTable"));
            if (!table || table->rowCount() != 2 || table->columnCount() != 6) return;
            configured = true;
            dialog->accept();
            return;
        }
    });
    wizardAction->trigger();
    QApplication::processEvents();
    if (!configured) return fail("P32 wizard could not be configured through Qt UI controls.");

    const QString firstFds = QDir(acceptanceRoot).filePath(QStringLiteral("p32-first.fds"));
    if (!window.exportCurrentProjectToFds(firstFds)) {
        window.saveProjectFile(QDir(acceptanceRoot).filePath(
            QStringLiteral("p32-export-failed.firecae")));
        for (const QPlainTextEdit* text : window.findChildren<QPlainTextEdit*>()) {
            if (!text->toPlainText().isEmpty())
                std::cerr << text->toPlainText().toStdString() << '\n';
        }
        return fail("P32 UUID-linked spray system could not export to FDS.");
    }
    QFile firstFile(firstFds);
    if (!firstFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail("P32 exported FDS input could not be read.");
    const QString firstText = QString::fromUtf8(firstFile.readAll());
    const QStringList required = {
        QStringLiteral("&SPEC ID='WATER VAPOR'"),
        QStringLiteral("&PART ID='P32_DROPLETS', SPEC_ID='WATER VAPOR'"),
        QStringLiteral("DIAMETER=1250"),
        QStringLiteral("&PROP ID='P32_SPRINKLER'"),
        QStringLiteral("PART_ID='P32_DROPLETS'"),
        QStringLiteral("PARTICLE_VELOCITY=6.5"),
        QStringLiteral("FLOW_RATE=72"),
        QStringLiteral("ACTIVATION_TEMPERATURE=68"),
        QStringLiteral("RTI=35"),
        QStringLiteral("PARTICLES_PER_SECOND=12000"),
        QStringLiteral("SPRAY_PATTERN_TABLE='P32_SPRAY_TABLE'"),
        QStringLiteral("&DEVC ID='P32_DEVICE', XYZ=1,2,2.2, PROP_ID='P32_SPRINKLER'"),
        QStringLiteral("DT_PART=0.25"), QStringLiteral("&TIME T_END=2")};
    for (const QString& token : required) {
        if (!firstText.contains(token)) {
            std::cerr << "P32 missing exported token: " << token.toStdString() << '\n';
            return fail("P32 exported FDS is missing a required record or parameter.");
        }
    }
    if (firstText.count(QStringLiteral("&TABL ID='P32_SPRAY_TABLE'")) != 2)
        return fail("P32 repeated TABL spray sectors were not exported as two records.");

    QAction* undoAction = findActionByShortcut(window, QKeySequence::Undo);
    QAction* redoAction = findActionByShortcut(window, QKeySequence::Redo);
    if (!undoAction || !redoAction || !undoAction->isEnabled())
        return fail("P32 atomic Undo/Redo command is unavailable.");
    undoAction->trigger();
    QApplication::processEvents();
    const QString undoneFds = QDir(acceptanceRoot).filePath(QStringLiteral("p32-undone.fds"));
    if (!window.exportCurrentProjectToFds(undoneFds))
        return fail("P32 project could not export after atomic undo.");
    QFile undoneFile(undoneFds);
    if (!undoneFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail("P32 undone FDS input could not be read.");
    if (QString::fromUtf8(undoneFile.readAll()).contains(QStringLiteral("P32_DEVICE")))
        return fail("P32 Undo left part of the spray object graph behind.");
    redoAction->trigger();
    QApplication::processEvents();

    const QString projectPath = QDir(acceptanceRoot).filePath(QStringLiteral("p32.firecae"));
    const QString reopenedFds = QDir(acceptanceRoot).filePath(QStringLiteral("p32-reopened.fds"));
    if (!window.saveProjectFile(projectPath) || !window.openProjectFile(projectPath) ||
        !window.exportCurrentProjectToFds(reopenedFds)) {
        return fail("P32 spray system did not survive project save/reopen.");
    }
    QFile reopenedFile(reopenedFds);
    if (!reopenedFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
        QString::fromUtf8(reopenedFile.readAll()) != firstText) {
        return fail("P32 FDS output changed after UUID-backed project round trip.");
    }
    QFile projectFile(projectPath);
    if (!projectFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail("P32 saved project could not be inspected.");
    const QString projectJson = QString::fromUtf8(projectFile.readAll());
    if (projectJson.count(QStringLiteral("\"kind\": 2")) < 4 ||
        !projectJson.contains(QStringLiteral("\"key\": \"SPRAY_PATTERN_TABLE\""))) {
        return fail("P32 project did not persist the UUID reference graph.");
    }

    std::cout << "FireCAE P32 particle/sprinkler GUI-to-FDS acceptance passed.\n";
    return 0;
}
}

int runUiFeedbackAcceptance();

int runM07ObjectTypeSwitchRegression();

int main(int argc, char* argv[])
{
    qputenv("FIRECAE_UI_LANGUAGE", "en");
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FireCAE"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAE"));
    // Keep every UI suite out of the user's preferences and recent-project list.
    QTemporaryDir automatedSettingsDirectory;
    if (!automatedSettingsDirectory.isValid())
        return fail("The isolated UI settings directory could not be created.");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       automatedSettingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       automatedSettingsDirectory.path());
    if (application.arguments().contains(QStringLiteral("--ui-feedback-smoke"))) {
        return runUiFeedbackAcceptance();
    }
    QTemporaryDir automatedRecoveryDirectory;
    if (automatedRecoveryDirectory.isValid()) {
        qputenv("FIRECAE_RECOVERY_DIRECTORY",
                automatedRecoveryDirectory.path().toLocal8Bit());
    }
    if (application.arguments().contains(QStringLiteral("--m07-object-type-switch"))) {
        return runM07ObjectTypeSwitchRegression();
    }
    if (application.arguments().contains(QStringLiteral("--reacceptance-result-cleanup"))) {
        const QString smvPath = qEnvironmentVariable("FIRECAE_REACCEPTANCE_EXISTING_SMV");
        if (!QFileInfo::exists(smvPath)) return fail("Cleanup regression requires an existing real SMV.");
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QApplication::processEvents();
        const QString replayProject = qEnvironmentVariable("FIRECAE_REACCEPTANCE_EXISTING_PROJECT");
        if (!replayProject.isEmpty() && !window.openProjectFile(replayProject))
            return fail("Cleanup could not reopen its real solved project.");
        for (int iteration = 0; iteration < 2; ++iteration) {
            std::cerr << "Cleanup iteration " << iteration << ": opening native results" << std::endl;
            if (!window.openResultFile(smvPath)) return fail("Cleanup result scan failed.");
            auto* tree = window.findChild<ModelTreeWidget*>();
            const QString id = tree ? tree->selectedObjectId() : QString{};
            if (id.isEmpty() || !window.openResultInNativeViewer(id))
                return fail("Cleanup native result view failed.");
            auto* viewer = window.findChild<NativeResultViewerWidget*>();
            auto* files = viewer->findChild<QListWidget*>(QStringLiteral("NativeResultFileList"));
            for (int row = 0; row < files->count(); ++row) {
                if (files->item(row)->data(Qt::UserRole).toString().endsWith(QStringLiteral("_hrr.csv"))) {
                    files->setCurrentRow(row);
                    break;
                }
            }
            viewer->findChild<QPushButton*>(QStringLiteral("NativeResultPlayButton"))->click();
            std::cerr << "Cleanup iteration " << iteration << ": opening Smokeview" << std::endl;
            if (!window.launchResultAnimation(id, SmokeviewLaunchMode::Standard))
                return fail("Cleanup Smokeview launch failed.");
            QEventLoop loop;
            QTimer::singleShot(3500, &loop, &QEventLoop::quit);
            loop.exec();
            std::cerr << "Cleanup iteration " << iteration << ": closing result" << std::endl;
            if (!window.closeResultCase(id)) return fail("Cleanup result close failed.");
            QApplication::processEvents();
            std::cerr << "Cleanup iteration " << iteration << ": closed" << std::endl;
        }
        std::cerr << "Cleanup regression: closing window" << std::endl;
        window.close();
        QApplication::processEvents();
        std::cerr << "Cleanup regression: completed" << std::endl;
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--p32-particle-spray-smoke"))) {
        return runP32ParticleSprayWizardAcceptance();
    }
    if (application.arguments().contains(QStringLiteral("--p18-workspace-smoke"))) {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QApplication::processEvents();

        auto* workspaceTabs = window.findChild<QTabWidget*>(
            QStringLiteral("MainWorkspaceTabs"));
        auto* model3d = window.findChild<OccViewWidget*>(
            QStringLiteral("Model3DView"));
        auto* plan2d = window.findChild<OccViewWidget*>(
            QStringLiteral("Plan2DView"));
        auto* startPage = window.findChild<StartPageWidget*>();
        auto* record = window.findChild<QPlainTextEdit*>(
            QStringLiteral("FdsRecordView"));
        auto* floorFilter = window.findChild<QComboBox*>(
            QStringLiteral("ModelTreeFloorFilter"));
        auto* scenarioLabel = window.findChild<QLabel*>(
            QStringLiteral("ModelTreeScenarioLabel"));
        auto* propertiesDock = window.findChild<QDockWidget*>(
            QStringLiteral("PropertiesDock"));
        auto* messagesDock = window.findChild<QDockWidget*>(
            QStringLiteral("MessagesDock"));
        auto* taskDock = window.findChild<QDockWidget*>(
            QStringLiteral("SimulationTaskCenterDock"));
        auto* inspectorDock = window.findChild<QDockWidget*>(
            QStringLiteral("WorkspaceInspectorDock"));
        auto* fdsOutputDock = window.findChild<QDockWidget*>(
            QStringLiteral("FdsOutputDock"));
        auto* resultStatusDock = window.findChild<QDockWidget*>(
            QStringLiteral("ResultStatusDock"));
        auto* selectionList = window.findChild<QListWidget*>(
            QStringLiteral("WorkspaceSelectionList"));
        auto* validationList = window.findChild<QListWidget*>(
            QStringLiteral("WorkspaceValidationList"));
        auto* propertiesScroll = window.findChild<QScrollArea*>(
            QStringLiteral("PropertiesScrollArea"));
        QAction* selection = window.findChild<QAction*>(
            QStringLiteral("SelectionModeAction"));
        if (!workspaceTabs || workspaceTabs->count() != 4 || !model3d || !plan2d ||
            !record || !record->isReadOnly() || !floorFilter || !scenarioLabel ||
            !propertiesDock || !messagesDock || !taskDock || !inspectorDock ||
            !fdsOutputDock || !resultStatusDock || !selectionList ||
            !validationList || !propertiesScroll || !selection || !startPage ||
            !selection->isCheckable()) {
            return fail("P18 professional workspace controls are incomplete.");
        }
        if (startPage->isVisible() || !model3d->isVisible()) {
            return fail("P18 application did not start directly in the modeling workspace.");
        }
        if (window.dockWidgetArea(propertiesDock) != Qt::RightDockWidgetArea ||
            window.dockWidgetArea(inspectorDock) != Qt::RightDockWidgetArea ||
            window.dockWidgetArea(messagesDock) != Qt::BottomDockWidgetArea ||
            window.dockWidgetArea(taskDock) != Qt::BottomDockWidgetArea ||
            window.dockWidgetArea(fdsOutputDock) != Qt::BottomDockWidgetArea ||
            window.dockWidgetArea(resultStatusDock) != Qt::BottomDockWidgetArea) {
            return fail("P18 default dock layout is not property-right/status-bottom.");
        }
        selection->trigger();
        QApplication::processEvents();
        if (!selection->isChecked()) {
            return fail("P18 selection tool did not enter a usable checked state.");
        }

        QAction* tutorial = window.findChild<QAction*>(
            QStringLiteral("ActivateVentsTutorialAction"));
        if (!tutorial) return fail("P18 workspace test tutorial action is missing.");
        tutorial->trigger();
        QApplication::processEvents();
        workspaceTabs->setCurrentIndex(2);
        QApplication::processEvents();
        const QString fdsRecord = record->toPlainText();
        if (!record->isVisible() || !fdsRecord.contains(QStringLiteral("&HEAD")) ||
            !fdsRecord.contains(QStringLiteral("&MESH")) ||
            !fdsRecord.contains(QStringLiteral("&TAIL"))) {
            return fail("P18 FDS record workspace did not render the active document.");
        }
        workspaceTabs->setCurrentIndex(0);
        QApplication::processEvents();
        if (workspaceTabs->isTabVisible(1) || workspaceTabs->isTabVisible(3) ||
            !model3d->isVisible()) {
            return fail("P18 legacy plan/native-result tabs were not hidden in favor of the 3D/Smokeview workflow.");
        }

        FcProject largeProject(QStringLiteral("P18 Large Tree"));
        FcObject::Ptr lastObject;
        for (int index = 0; index < 250; ++index) {
            auto object = std::make_shared<FcObject>(
                QStringLiteral("Large IFC member with a deliberately long display name %1")
                    .arg(index, 3, 10, QLatin1Char('0')),
                FcObjectType::IfcEntity);
            lastObject = object;
            largeProject.document()->geometryGroup()->addChild(object);
        }
        ModelTreeWidget largeTree;
        largeTree.resize(360, 640);
        largeTree.setProject(&largeProject);
        largeTree.show();
        QApplication::processEvents();
        auto* lazyTree = largeTree.findChild<QTreeWidget*>();
        QTreeWidgetItem* geometryGroup = lazyTree && lazyTree->topLevelItemCount() == 1
                                             ? lazyTree->topLevelItem(0)->child(0)
                                             : nullptr;
        if (!geometryGroup || geometryGroup->childCount() != 1) {
            return fail("P18 large model tree was not initially lazy.");
        }
        geometryGroup->setExpanded(true);
        QApplication::processEvents();
        if (geometryGroup->childCount() != 101 ||
            geometryGroup->child(0)->toolTip(0).isEmpty() ||
            !largeTree.selectObjectById(lastObject->id())) {
            return fail("P18 lazy tree batching, tooltip, or UUID materialization failed.");
        }
        std::cout << "FireCAE P18 professional workspace smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--p19-floor-drawing-smoke"))) {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QApplication::processEvents();
        QAction* createFloor = window.findChild<QAction*>(
            QStringLiteral("CreateFloorAction"));
        QAction* drawWall = window.findChild<QAction*>(
            QStringLiteral("DrawWallInViewAction"));
        if (!createFloor || !drawWall ||
            !window.findChild<QAction*>(QStringLiteral("MeasureSelectionAction")) ||
            !window.findChild<QAction*>(QStringLiteral("MirrorSelectedAction")) ||
            !window.findChild<QAction*>(QStringLiteral("AlignSelectedAction")) ||
            !window.findChild<QAction*>(QStringLiteral("CopyToFloorAction"))) {
            return fail("P19 floor and professional geometry actions are incomplete.");
        }
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("FloorEditorDialog"))
                    continue;
                dialog->findChild<QLineEdit*>(QStringLiteral("FloorNameEdit"))
                    ->setText(QStringLiteral("Level 02"));
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorBaseElevationSpin"))->setValue(4.2);
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorStoreyHeightSpin"))->setValue(3.6);
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorSlabThicknessSpin"))->setValue(0.22);
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorWallHeightSpin"))->setValue(3.3);
                dialog->findChild<QLineEdit*>(
                    QStringLiteral("FloorBackgroundImageEdit"))
                    ->setText(QStringLiteral("plans/level-02.png"));
                dialog->findChild<QCheckBox*>(
                    QStringLiteral("FloorClippingEnabledCheck"))->setChecked(true);
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorClipZMinSpin"))->setValue(4.2);
                dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("FloorClipZMaxSpin"))->setValue(7.8);
                dialog->accept();
            }
        });
        createFloor->trigger();
        QApplication::processEvents();
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        QTreeWidgetItem* geometryGroup = tree && tree->topLevelItemCount() == 1
                                             ? tree->topLevelItem(0)->child(0)
                                             : nullptr;
        QTreeWidgetItem* floorItem = geometryGroup && geometryGroup->childCount() == 1
                                         ? geometryGroup->child(0) : nullptr;
        if (!floorItem || floorItem->text(0) != QStringLiteral("Level 02")) {
            return fail("P19 floor editor did not create a typed floor in the model tree.");
        }
        const QString floorUuid = floorItem->data(0, Qt::UserRole).toString();
        selectItem(tree, floorItem);
        auto* workspaceTabs = window.findChild<QTabWidget*>(
            QStringLiteral("MainWorkspaceTabs"));
        auto* plan = window.findChild<OccViewWidget*>(QStringLiteral("Model3DView"));
        workspaceTabs->setCurrentIndex(0);
        QApplication::processEvents();
        if (!plan || !plan->isInitialized() || workspaceTabs->isTabVisible(1)) {
            return fail("P19 3D view did not initialize for top-view drawing.");
        }
        plan->setPerspective(false);
        plan->setOrientation(OccViewOrientation::Top);
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() !=
                                   QStringLiteral("WallSketchSettingsDialog"))
                    continue;
                dialog->findChild<QLineEdit*>(QStringLiteral("WallSketchNameEdit"))
                    ->setText(QStringLiteral("Level 02 Wall"));
                dialog->accept();
            }
        });
        drawWall->trigger();
        QApplication::processEvents();
        if (!plan->isWallSketchActive()) {
            return fail("P19 plan view did not enter the shared wall drawing state machine.");
        }
        const QPoint firstPoint(plan->width() / 2 - 100, plan->height() / 2);
        const QPoint secondPoint(plan->width() / 2 + 100, plan->height() / 2);
        QMouseEvent firstClick(QEvent::MouseButtonPress, QPointF(firstPoint),
                               QPointF(plan->mapToGlobal(firstPoint)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(plan, &firstClick);
        QMouseEvent secondClick(QEvent::MouseButtonPress, QPointF(secondPoint),
                                QPointF(plan->mapToGlobal(secondPoint)),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(plan, &secondClick);
        QApplication::processEvents();
        tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        geometryGroup = tree->topLevelItem(0)->child(0);
        floorItem = findItemByUuid(geometryGroup, floorUuid);
        if (!floorItem || floorItem->childCount() < 2 || plan->isWallSketchActive()) {
            return fail("P19 2D wall was not completed under the active floor.");
        }
        QString wallUuid;
        QString backgroundUuid;
        for (int childIndex = 0; childIndex < floorItem->childCount(); ++childIndex) {
            QTreeWidgetItem* child = floorItem->child(childIndex);
            if (child->text(0) == QStringLiteral("Level 02 Wall")) {
                wallUuid = child->data(0, Qt::UserRole).toString();
            } else if (child->text(0).contains(QStringLiteral("Background"))) {
                backgroundUuid = child->data(0, Qt::UserRole).toString();
            }
        }
        auto* model3d = window.findChild<OccViewWidget*>(QStringLiteral("Model3DView"));
        if (wallUuid.isEmpty() || backgroundUuid.isEmpty() ||
            !plan->displayManager()->contains(backgroundUuid) ||
            !plan->displayManager()->isObjectVisible(backgroundUuid) ||
            !model3d || !model3d->displayManager() ||
            !model3d->displayManager()->isObjectVisible(backgroundUuid)) {
            return fail("P19 floor background was not available in the active top view.");
        }
        QTemporaryDir directory;
        const QString projectPath = directory.filePath(QStringLiteral("p19-floor.firecae"));
        if (!directory.isValid() || !window.saveProjectFile(projectPath)) {
            return fail("P19 floor drawing project could not be saved.");
        }
        FcProjectLoadResult restored = FcProjectSerializer::load(projectPath);
        const auto floor = restored.success()
                               ? std::dynamic_pointer_cast<FcFloorObject>(
                                     restored.project->document()->findObject(floorUuid))
                               : std::shared_ptr<FcFloorObject>{};
        const auto wall = restored.success()
                              ? std::dynamic_pointer_cast<FcGeometryObject>(
                                    restored.project->document()->findObject(wallUuid))
                              : std::shared_ptr<FcGeometryObject>{};
        const auto background = restored.success()
                              ? std::dynamic_pointer_cast<FcGeometryObject>(
                                    restored.project->document()->findObject(backgroundUuid))
                              : std::shared_ptr<FcGeometryObject>{};
        if (!floor || !wall || wall->parent() != floor.get() ||
            !background || background->parent() != floor.get() ||
            background->geometryKind() != FcGeometryKind::BackgroundImage ||
            background->geometryParameters()
                    .value(QStringLiteral("fdsConversionRoute")).toString() !=
                QStringLiteral("REFERENCE") ||
            wall->floorName() != floor->name() ||
            qAbs(floor->baseElevation() - 4.2) > 1.0e-9 ||
            qAbs(floor->defaultWallHeight() - 3.3) > 1.0e-9 ||
            qAbs(BuildingGeometryService::requestFromParameters(
                     wall->geometryKind(), wall->geometryParameters()).z - 4.2) > 1.0e-9) {
            return fail("P19 floor properties, wall ownership, elevation, or UUID did not survive reopen.");
        }
        std::cout << "FireCAE P19 floor and 2D drawing smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--p20-professional-editors-smoke"))) {
        FcProject project(QStringLiteral("P20 Professional Editors"));

        auto mesh = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Mesh 001"), FcObjectType::Mesh,
            QStringLiteral("MESH"), QStringLiteral("MESH_001"), 1);
        mesh->addRawParameter(QStringLiteral("IJK"), QStringLiteral("20,20,12"));
        mesh->addRawParameter(QStringLiteral("XB"), QStringLiteral("0,10,0,10,0,3"));
        FdsObjectEditorDialog meshEditor(&project, QStringLiteral("MESH"), mesh.get());
        auto* meshTabs = meshEditor.findChild<QTabWidget*>(QStringLiteral("FdsEditorTabs"));
        auto* meshAdvanced = meshEditor.findChild<QTableWidget*>(QStringLiteral("FdsParameterTable"));
        if (!meshTabs || meshTabs->count() != 4 || !meshAdvanced ||
            !meshEditor.findChild<QWidget*>(QStringLiteral("MeshSettingsPage")) ||
            !meshEditor.findChild<QWidget*>(QStringLiteral("FdsBasicSchemaEditor")) ||
            !meshEditor.findChild<QWidget*>(QStringLiteral("FdsProfessionalSchemaEditor")) ||
            meshEditor.findChild<QWidget*>(QStringLiteral("Schema_IJK")) ||
            meshEditor.findChild<QWidget*>(QStringLiteral("Schema_XB")) ||
            meshAdvanced->rowCount() != 2) {
            return fail("P20 mesh editor did not preserve the Basic/Professional/Advanced separation.");
        }

        auto material = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Concrete"), FcObjectType::Material,
            QStringLiteral("MATL"), QStringLiteral("CONCRETE"), 2);
        material->setParameters(FdsSchemaRegistry::defaultParameters(QStringLiteral("MATL")));
        FdsObjectEditorDialog materialEditor(&project, QStringLiteral("MATL"), material.get());
        if (!materialEditor.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_DENSITY")) ||
            !materialEditor.findChild<QLineEdit*>(QStringLiteral("Schema_HEAT_OF_REACTION")) ||
            !materialEditor.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_BOILING_TEMPERATURE"))) {
            return fail("P20 material editor is missing readable basic, pyrolysis, or liquid-fuel fields.");
        }

        auto device = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Thermocouple"), FcObjectType::Device,
            QStringLiteral("DEVC"), QStringLiteral("TC_01"), 3);
        device->setParameters(FdsSchemaRegistry::defaultParameters(QStringLiteral("DEVC")));
        FdsObjectEditorDialog deviceEditor(&project, QStringLiteral("DEVC"), device.get());
        auto* quantity = deviceEditor.findChild<QComboBox*>(QStringLiteral("Schema_QUANTITY"));
        if (!quantity || quantity->findText(QStringLiteral("THERMOCOUPLE")) < 0 ||
            !deviceEditor.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_XYZ_0")) ||
            !deviceEditor.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_XYZ_1")) ||
            !deviceEditor.findChild<QDoubleSpinBox*>(QStringLiteral("Schema_XYZ_2"))) {
            return fail("P20 device editor did not expose quantity and separate XYZ business fields.");
        }

        auto hvac = std::make_shared<FcFdsNamelist>(
            QStringLiteral("Duct 01"), FcObjectType::HVAC,
            QStringLiteral("HVAC"), QStringLiteral("DUCT_01"), 4);
        hvac->addStringParameter(QStringLiteral("TYPE_ID"), QStringLiteral("DUCT"));
        project.document()->hvacGroup()->addChild(hvac);
        FdsObjectEditorDialog hvacEditor(&project, QStringLiteral("HVAC"), hvac.get());
        if (!hvacEditor.findChild<QComboBox*>(QStringLiteral("Schema_TYPE_ID")) ||
            !hvacEditor.findChild<QPushButton*>(QStringLiteral("SchemaChoose_NODE_ID"))) {
            return fail("P20 HVAC editor did not expose typed components and UUID reference selection.");
        }

        MeshEngineeringDialog meshAssistant(&project, {});
        auto* dStarLabel = meshAssistant.findChild<QLabel*>(QStringLiteral("MeshEngineeringDStarLabel"));
        auto* applyDStar = meshAssistant.findChild<QPushButton*>(QStringLiteral("MeshEngineeringApplyDStarButton"));
        auto* meshSummary = meshAssistant.findChild<QLabel*>(QStringLiteral("MeshEngineeringSummaryLabel"));
        if (!dStarLabel || !applyDStar || !meshSummary ||
            !meshAssistant.findChild<QCheckBox*>(QStringLiteral("MeshEngineeringAutoAdjustBoundsCheck")) ||
            !meshSummary->text().contains(QStringLiteral("D*/dx")) ||
            !meshSummary->text().contains(QStringLiteral("Alignment: PASS")) ||
            !meshSummary->text().contains(QStringLiteral("MPI"))) {
            return fail("P20 mesh engineering estimates, D*/dx, alignment, or MPI advice are incomplete.");
        }
        applyDStar->click();
        QApplication::processEvents();
        if (meshAssistant.blocks().empty() || dStarLabel->text().isEmpty()) {
            return fail("P20 D*/dx recommendation did not produce usable mesh blocks.");
        }

        FireSourceWizardDialog fireWizard(&project);
        auto* host = fireWizard.findChild<QComboBox*>(QStringLiteral("FireSourceHostCombo"));
        if (!host || !host->currentData().toString().isEmpty() ||
            !fireWizard.findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceXSpin"))->isEnabled() ||
            !fireWizard.findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceCoYieldSpin")) ||
            !fireWizard.findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceRadiativeFractionSpin"))) {
            return fail("P20 no-host fire source wizard cannot define a complete burner.");
        }

        DeviceControlWizardDialog deviceControlWizard(&project);
        auto* deviceType = deviceControlWizard.findChild<QComboBox*>(
            QStringLiteral("DeviceControlTypeCombo"));
        if (!deviceType || deviceType->count() != 12 ||
            deviceType->findData(QStringLiteral("THERMOCOUPLE")) < 0 ||
            deviceType->findData(QStringLiteral("PATH OBSCURATION")) < 0 ||
            !deviceControlWizard.findChild<QDoubleSpinBox*>(
                QStringLiteral("DeviceControlSetpointSpin"))) {
            return fail("P20 device/control wizard does not cover the required detector families.");
        }

        OutputWizardDialog outputWizard(&project);
        auto* outputKind = outputWizard.findChild<QComboBox*>(QStringLiteral("OutputKindCombo"));
        if (!outputKind || outputKind->count() != 10 ||
            outputKind->findData(QStringLiteral("SM3D")) < 0 ||
            outputKind->findData(QStringLiteral("DUMP_PART")) < 0 ||
            outputKind->findData(QStringLiteral("HVAC_DEVC")) < 0) {
            return fail("P20 output manager does not cover the required professional output families.");
        }

        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QApplication::processEvents();
        window.loadSimpleTestBenchmark();
        QApplication::processEvents();
        QAction* outputAction = window.findChild<QAction*>(QStringLiteral("OutputWizardAction"));
        if (!outputAction) return fail("P20 output wizard action is missing.");
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("OutputWizardDialog")) continue;
                auto* kind = dialog->findChild<QComboBox*>(QStringLiteral("OutputKindCombo"));
                kind->setCurrentIndex(kind->findData(QStringLiteral("SLCF_VECTOR")));
                dialog->accept();
            }
        });
        outputAction->trigger();
        QApplication::processEvents();
        QTemporaryDir exportDirectory;
        const QString outputPath = exportDirectory.filePath(QStringLiteral("p20-output.fds"));
        if (!window.exportCurrentProjectToFds(outputPath)) {
            return fail("P20 vector slice could not be exported through the production window.");
        }
        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("P20 exported FDS input could not be read.");
        }
        const QString outputText = QString::fromUtf8(outputFile.readAll());
        if (!outputText.contains(QStringLiteral("&SLCF")) ||
            !outputText.contains(QStringLiteral("VECTOR=.TRUE.")) ||
            outputText.contains(QStringLiteral("SLCF_VECTOR"))) {
            return fail("P20 output manager emitted a pseudo keyword instead of legal FDS syntax.");
        }

        QAction* fireAction = window.findChild<QAction*>(QStringLiteral("FireSourceWizardAction"));
        if (!fireAction) return fail("P20 fire source wizard action is missing.");
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("FireSourceWizardDialog")) continue;
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceCoYieldSpin"))
                    ->setValue(0.02);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("FireSourceRadiativeFractionSpin"))
                    ->setValue(0.4);
                dialog->accept();
            }
        });
        fireAction->trigger();
        QApplication::processEvents();

        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("OutputWizardDialog")) continue;
                auto* kind = dialog->findChild<QComboBox*>(QStringLiteral("OutputKindCombo"));
                kind->setCurrentIndex(kind->findData(QStringLiteral("DUMP_PART")));
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("OutputIntervalSpin"))
                    ->setValue(0.5);
                dialog->accept();
            }
        });
        outputAction->trigger();
        QApplication::processEvents();

        QAction* deviceControlAction = window.findChild<QAction*>(
            QStringLiteral("DeviceControlWizardAction"));
        if (!deviceControlAction) return fail("P20 device/control production action is missing.");
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("DeviceControlWizardDialog")) continue;
                auto* target = dialog->findChild<QComboBox*>(QStringLiteral("DeviceControlTargetCombo"));
                if (target->count() > 1) target->setCurrentIndex(1);
                dialog->findChild<QCheckBox*>(QStringLiteral("DeviceControlCreateControlCheck"))
                    ->setChecked(true);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("DeviceControlDelaySpin"))
                    ->setValue(2.0);
                dialog->accept();
            }
        });
        deviceControlAction->trigger();
        QApplication::processEvents();
        const QString completePath = exportDirectory.filePath(QStringLiteral("p20-complete.fds"));
        if (!window.exportCurrentProjectToFds(completePath)) {
            return fail("P20 fire source and particle output could not be exported.");
        }
        QFile completeFile(completePath);
        if (!completeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("P20 complete exported FDS input could not be read.");
        }
        const QString completeText = QString::fromUtf8(completeFile.readAll());
        if (!completeText.contains(QStringLiteral("CO_YIELD=0.02")) ||
            !completeText.contains(QStringLiteral("RADIATIVE_FRACTION=0.4")) ||
            !completeText.contains(QStringLiteral("&VENT")) ||
            !completeText.contains(QStringLiteral("&DUMP DT_PART=0.5")) ||
            !completeText.contains(QStringLiteral("&DEVC ID='TEMP_01'")) ||
            !completeText.contains(QStringLiteral("&CTRL ID='CTRL_01'")) ||
            !completeText.contains(QStringLiteral("CTRL_ID='CTRL_01'")) ||
            completeText.contains(QStringLiteral("DUMP_PART"))) {
            return fail("P20 fire/output assistants did not export complete legal FDS semantics.");
        }
        std::cout << "FireCAE P20 professional editor smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--m08-device-control"))) {
        return runM08DeviceControlRegression();
    }
    if (application.arguments().contains(QStringLiteral("--p21-record-source-map-smoke"))) {
        MainWindow window;
        window.resize(1280, 800);
        window.setInterfaceLanguage(UiLanguage::English);
        window.show();
        QApplication::processEvents();
        window.loadSimpleTestBenchmark();
        QApplication::processEvents();

        auto* record = window.findChild<FdsRecordEditor*>(
            QStringLiteral("FdsRecordView"));
        auto* lineNumbers = window.findChild<QWidget*>(
            QStringLiteral("FdsRecordLineNumberArea"));
        auto* search = window.findChild<QLineEdit*>(
            QStringLiteral("FdsRecordSearchEdit"));
        auto* next = window.findChild<QPushButton*>(
            QStringLiteral("FdsRecordFindNextButton"));
        auto* previous = window.findChild<QPushButton*>(
            QStringLiteral("FdsRecordFindPreviousButton"));
        auto* copy = window.findChild<QPushButton*>(
            QStringLiteral("FdsRecordCopyButton"));
        auto* advanced = window.findChild<QCheckBox*>(
            QStringLiteral("FdsRecordAdvancedModeCheck"));
        auto* saveDraft = window.findChild<QPushButton*>(
            QStringLiteral("FdsRecordSaveDraftButton"));
        auto* tabs = window.findChild<QTabWidget*>(
            QStringLiteral("MainWorkspaceTabs"));
        if (!record || !lineNumbers || !search || !next || !previous || !copy ||
            !advanced || !saveDraft || !tabs || !record->isReadOnly() ||
            record->lineWrapMode() != QPlainTextEdit::NoWrap ||
            !record->toPlainText().contains(QStringLiteral("&MESH"))) {
            return fail("P21 read-only record workspace, line numbers, or tools are incomplete.");
        }
        tabs->setCurrentWidget(record->parentWidget());
        QApplication::processEvents();

        bool hasHighlightFormat = false;
        for (QTextBlock block = record->document()->firstBlock();
             block.isValid(); block = block.next()) {
            if (block.text().contains(QStringLiteral("&MESH")) && block.layout() &&
                !block.layout()->formats().isEmpty()) {
                hasHighlightFormat = true;
                break;
            }
        }
        if (!hasHighlightFormat) {
            return fail("P21 FDS syntax highlighting did not format the MESH record.");
        }

        search->setText(QStringLiteral("SURF"));
        next->click();
        QApplication::processEvents();
        if (record->textCursor().selectedText().compare(
                QStringLiteral("SURF"), Qt::CaseInsensitive) != 0) {
            return fail("P21 forward record search did not select a match.");
        }
        QApplication::clipboard()->setText(QStringLiteral("P21_CLIPBOARD_PROBE"));
        QApplication::processEvents();
        const bool clipboardAvailable =
            QApplication::clipboard()->text() == QStringLiteral("P21_CLIPBOARD_PROBE");
        QApplication::clipboard()->clear();
        copy->click();
        QApplication::processEvents();
        if (clipboardAvailable && QApplication::clipboard()->text().compare(
                QStringLiteral("SURF"), Qt::CaseInsensitive) != 0) {
            std::cerr << "P21 selected text: "
                      << record->textCursor().selectedText().toStdString()
                      << " | clipboard: "
                      << QApplication::clipboard()->text().toStdString() << '\n';
            return fail("P21 record copy did not copy the current selection.");
        }
        if (!clipboardAvailable &&
            record->textCursor().selectedText() != QStringLiteral("SURF")) {
            return fail("P21 copy command lost its selected record text while the system clipboard was unavailable.");
        }
        previous->click();

        QString activatedUuid;
        QString activatedParameter;
        QObject::connect(record, &FdsRecordEditor::sourceActivated, &window,
                         [&activatedUuid, &activatedParameter](
                             const QString& uuid, const QString& parameter) {
                             activatedUuid = uuid;
                             activatedParameter = parameter;
                         });
        const int ijkPosition = record->toPlainText().indexOf(QStringLiteral("IJK="));
        QTextCursor ijkCursor(record->document());
        ijkCursor.setPosition(ijkPosition + 1);
        record->setTextCursor(ijkCursor);
        record->ensureCursorVisible();
        const QPoint clickPoint = record->cursorRect(ijkCursor).center();
        QMouseEvent pressEvent(QEvent::MouseButtonPress, QPointF(clickPoint),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(clickPoint),
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(record->viewport(), &pressEvent);
        QApplication::sendEvent(record->viewport(), &releaseEvent);
        QApplication::processEvents();
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (activatedUuid.isEmpty() || activatedParameter != QStringLiteral("IJK") ||
            !tree || !tree->currentItem() ||
            tree->currentItem()->data(0, Qt::UserRole).toString() != activatedUuid) {
            return fail("P21 clicking an FDS parameter did not select its UUID business object.");
        }

        const QString generatedText = record->toPlainText();
        QTimer::singleShot(0, &window, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* messageBox = qobject_cast<QMessageBox*>(widget);
                if (messageBox && messageBox->isVisible()) {
                    if (QAbstractButton* yesButton =
                            messageBox->button(QMessageBox::Yes)) {
                        yesButton->click();
                    }
                    return;
                }
            }
        });
        advanced->setChecked(true);
        QApplication::processEvents();
        if (record->isReadOnly() || !saveDraft->isEnabled()) {
            return fail("P21 risk-confirmed advanced draft mode was not enabled.");
        }
        record->appendPlainText(QStringLiteral("! UNSAVED TEST DRAFT"));
        advanced->setChecked(false);
        QApplication::processEvents();
        if (!record->isReadOnly() || saveDraft->isEnabled() ||
            record->toPlainText() != generatedText) {
            return fail("P21 leaving advanced mode did not restore the UUID-generated preview.");
        }

        QTemporaryDir directory;
        if (!directory.isValid()) return fail("P21 temporary directory is unavailable.");
        const QString unknownInput = directory.filePath(QStringLiteral("unknown.fds"));
        QFile unknownFile(unknownInput);
        if (!unknownFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
            unknownFile.write(
                "&HEAD CHID='unknown_ui' /\n"
                "&MESH ID='MESH_A', IJK=10,10,10, XB=0,1,0,1,0,1 /\n"
                "&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /\n"
                "&TIME T_END=5 /\n&TAIL /\n") < 0) {
            return fail("P21 unknown-record fixture could not be written.");
        }
        unknownFile.close();
        if (!window.importFdsInputFile(unknownInput)) {
            return fail("P21 unknown FDS record could not be imported.");
        }
        QApplication::processEvents();
        std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findByText =
            [&](QTreeWidgetItem* item, const QString& text) -> QTreeWidgetItem* {
                if (!item) return nullptr;
                if (item->text(0) == text) return item;
                for (int index = 0; index < item->childCount(); ++index) {
                    if (QTreeWidgetItem* found = findByText(item->child(index), text)) {
                        return found;
                    }
                }
                return nullptr;
            };
        QTreeWidgetItem* additional = findByText(
            tree->topLevelItem(0), QStringLiteral("Additional Records"));
        if (!additional || additional->childCount() != 1 ||
            !record->toPlainText().contains(
                QStringLiteral("&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /"))) {
            return fail("P21 unknown record was not shown under Additional Records or re-exported.");
        }
        const QString unknownProject = directory.filePath(QStringLiteral("unknown.firecae"));
        if (!window.saveProjectFile(unknownProject) ||
            !window.openProjectFile(unknownProject)) {
            return fail("P21 unknown record project could not round-trip.");
        }
        QApplication::processEvents();
        if (!record->toPlainText().contains(
                QStringLiteral("&ZZZZ ID='UNKNOWN_A', FOO=17, LABEL='keep me' /"))) {
            return fail("P21 unknown record was silently dropped after project reopen.");
        }

        const QString invalidInput = directory.filePath(QStringLiteral("invalid.fds"));
        QFile invalidFile(invalidInput);
        if (!invalidFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
            invalidFile.write(
                "&HEAD CHID='invalid_ui' /\n"
                "&MESH ID='BAD_MESH', XB=0,1,0,1,0,1 /\n"
                "&TIME T_END=5 /\n&TAIL /\n") < 0) {
            return fail("P21 validation fixture could not be written.");
        }
        invalidFile.close();
        if (!window.importFdsInputFile(invalidInput)) {
            return fail("P21 validation fixture could not be imported.");
        }
        QApplication::processEvents();
        auto* validation = window.findChild<QListWidget*>(
            QStringLiteral("WorkspaceValidationList"));
        if (!validation || validation->count() == 0 ||
            validation->item(0)->data(Qt::UserRole).toString().isEmpty() ||
            validation->item(0)->data(Qt::UserRole + 1).toString() !=
                QStringLiteral("IJK")) {
            return fail("P21 validation issue did not retain its UUID and parameter key.");
        }
        validation->item(0)->setSelected(true);
        validation->setCurrentItem(validation->item(0));
        QMetaObject::invokeMethod(validation, "itemClicked", Qt::DirectConnection,
                                  Q_ARG(QListWidgetItem*, validation->item(0)));
        QApplication::processEvents();
        if (!tree->currentItem() ||
            tree->currentItem()->data(0, Qt::UserRole).toString() !=
                validation->item(0)->data(Qt::UserRole).toString()) {
            return fail("P21 clicking a validation issue did not select its UUID object.");
        }

        std::cout << "FireCAE P21 record/source-map smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--p22-import-maturity-smoke"))) {
        const QString fdsText = QStringLiteral(
            "&HEAD CHID='p22_import' /\n"
            "&MESH ID='MESH_A', IJK=10,10,10, XB=0,1,0,1,0,1 /\n"
            "&DEVC ID='D1', QUANTITY='TEMPERATURE', XYZ=0.5,0.5,0.5, CTRL_ID='C1' /\n"
            "&CTRL ID='C1', FUNCTION_TYPE='ALL', INPUT_ID='D1', FUTURE_FIELD=9 /\n"
            "&ZZZZ ID='UNKNOWN', FOO=17 /\n"
            "&TIME T_END=5 /\n"
            "&TAIL /\n");
        FdsImportResult fdsImport = FdsImporter().importText(
            fdsText, QStringLiteral("p22-forward-reference.fds"));
        if (!fdsImport.success() ||
            !fdsImport.unsupportedRecords.contains(QStringLiteral("ZZZZ")) ||
            !fdsImport.unsupportedParameters.contains(
                QStringLiteral("CTRL.FUTURE_FIELD")) ||
            fdsImport.unsupportedRecordCount < 1 ||
            fdsImport.unsupportedParameterCount < 1 ||
            fdsImport.resolvedReferenceCount < 2 ||
            fdsImport.unresolvedReferenceCount != 0) {
            return fail("P22 FDS importer did not report unknown syntax or rebuild forward UUID references.");
        }
        const FdsWriteResult fdsRoundTrip = FdsWriter::render(*fdsImport.project);
        if (!fdsRoundTrip.success() ||
            !fdsRoundTrip.text.contains(QStringLiteral("&ZZZZ")) ||
            !fdsRoundTrip.text.contains(QStringLiteral("FUTURE_FIELD=9")) ||
            !fdsRoundTrip.text.contains(QStringLiteral("CTRL_ID='C1'")) ||
            !fdsRoundTrip.text.contains(QStringLiteral("INPUT_ID='D1'"))) {
            return fail("P22 FDS importer did not preserve unknown syntax and rebuilt references on export.");
        }

        const QString fileFilter = GeometryImportService::openFileFilter().toLower();
        const QStringList advertisedExtensions{
            QStringLiteral("*.ifc"), QStringLiteral("*.stl"),
            QStringLiteral("*.obj"), QStringLiteral("*.gltf"),
            QStringLiteral("*.glb"), QStringLiteral("*.step"),
            QStringLiteral("*.stp"), QStringLiteral("*.iges"),
            QStringLiteral("*.igs"), QStringLiteral("*.dxf")};
        for (const QString& extension : advertisedExtensions) {
            if (!fileFilter.contains(extension)) {
                return fail(qPrintable(
                    QStringLiteral("P22 supported format is missing from the import wizard: %1")
                        .arg(extension)));
            }
        }
        if (fileFilter.contains(QStringLiteral("*.fbx")) ||
            fileFilter.contains(QStringLiteral("*.dae")) ||
            fileFilter.contains(QStringLiteral("*.dwg"))) {
            return fail("P22 import wizard advertises a format that is not implemented.");
        }

        const QString ifcPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("tessellated-item.ifc"));
        const IfcPreflightReport preflight = IfcImportService::inspectFile(ifcPath);
        if (!preflight.success() || preflight.schema != QStringLiteral("IFC4") ||
            preflight.lengthUnit != QStringLiteral("mm") ||
            preflight.entityCount <= 0 || preflight.productCount <= 0 ||
            !preflight.productClasses.contains(
                QStringLiteral("IFCBUILDINGELEMENTPROXY"))) {
            return fail("P22 IFC preflight did not detect schema, units, entities, and component classes.");
        }
        const IfcImportResult cancelledImport = IfcImportService().importFile(
            ifcPath, {}, {}, []() { return true; });
        if (!cancelledImport.cancelled || cancelledImport.rootObject) {
            return fail("P22 IFC service cancellation did not stop before conversion.");
        }

        const QString complexIfcPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("Clinic_Architectural_IFC2x3.ifc"));
        const IfcPreflightReport complexPreflight =
            IfcImportService::inspectFile(complexIfcPath);
        if (!complexPreflight.success() || complexPreflight.sourceBytes < 1000000 ||
            complexPreflight.productCount < 100 || complexPreflight.storeyCount < 1 ||
            complexPreflight.materialRelationshipCount < 1 ||
            complexPreflight.propertySetCount < 1) {
            return fail("P22 complex IFC preflight omitted floors, products, materials, or Property Sets.");
        }
        std::atomic_bool cancelComplex{false};
        int maximumComplexProgress = 0;
        const IfcImportResult cancelledComplexImport = IfcImportService().importFile(
            complexIfcPath, {},
            [&cancelComplex, &maximumComplexProgress](int percent, const QString&) {
                maximumComplexProgress = qMax(maximumComplexProgress, percent);
                if (percent >= 10) cancelComplex.store(true);
            },
            [&cancelComplex]() { return cancelComplex.load(); });
        if (!cancelledComplexImport.cancelled ||
            cancelledComplexImport.rootObject || maximumComplexProgress < 10) {
            return fail("P22 large IFC conversion did not cancel and reclaim its worker process.");
        }

        GeometryImportWizard wizard(ifcPath);
        wizard.resize(900, 760);
        wizard.show();
        QApplication::processEvents();
        wizard.next();
        wizard.next();
        QApplication::processEvents();
        auto* optionsGroup = wizard.findChild<QGroupBox*>(
            QStringLiteral("IfcImportOptionsGroup"));
        auto* preflightSummary = wizard.findChild<QLabel*>(
            QStringLiteral("IfcImportPreflightSummary"));
        auto* typeTree = wizard.findChild<QTreeWidget*>(
            QStringLiteral("IfcImportTypeTree"));
        auto* mergeStrategy = wizard.findChild<QComboBox*>(
            QStringLiteral("IfcImportMergeStrategyCombo"));
        auto* simplification = wizard.findChild<QComboBox*>(
            QStringLiteral("IfcImportSimplificationCombo"));
        auto* conversionRoute = wizard.findChild<QComboBox*>(
            QStringLiteral("IfcImportConversionRouteCombo"));
        auto* propertySets = wizard.findChild<QCheckBox*>(
            QStringLiteral("IfcImportPropertySetsCheck"));
        auto* progress = wizard.findChild<QProgressBar*>(
            QStringLiteral("GeometryImportProgressBar"));
        auto* cancelButton = wizard.findChild<QPushButton*>(
            QStringLiteral("GeometryImportCancelButton"));
        auto* preview = wizard.findChild<QPlainTextEdit*>(
            QStringLiteral("GeometryImportPreviewReport"));
        if (!optionsGroup || !optionsGroup->isVisible() || !preflightSummary ||
            !preflightSummary->text().contains(QStringLiteral("IFC4")) ||
            !preflightSummary->text().contains(QStringLiteral("mm")) ||
            !typeTree || typeTree->topLevelItemCount() == 0 ||
            !mergeStrategy || !simplification || !conversionRoute ||
            !propertySets || !propertySets->isChecked() || !progress ||
            !cancelButton || !preview) {
            return fail("P22 IFC wizard is missing preflight, filtering, strategy, progress, or cancellation controls.");
        }
        mergeStrategy->setCurrentIndex(
            mergeStrategy->findData(QStringLiteral("MERGE_ALL")));
        simplification->setCurrentIndex(
            simplification->findData(QStringLiteral("BOUNDING_BOX")));
        conversionRoute->setCurrentIndex(
            conversionRoute->findData(QStringLiteral("GEOM")));
        wizard.next();
        QApplication::processEvents();
        if (!cancelButton->isEnabled()) {
            return fail("P22 IFC background import did not expose an active Cancel operation.");
        }

        QEventLoop waitLoop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(preview, &QPlainTextEdit::textChanged, &waitLoop,
                         [&waitLoop, preview]() {
                             const QString text = preview->toPlainText();
                             if (text.contains(QStringLiteral(
                                     "Entities / products / geometry objects")) ||
                                 text.contains(QStringLiteral("preview failed"))) {
                                 waitLoop.quit();
                             }
                         });
        QObject::connect(&timeout, &QTimer::timeout, &waitLoop,
                         &QEventLoop::quit);
        timeout.start(120000);
        if (!preview->toPlainText().contains(
                QStringLiteral("Entities / products / geometry objects"))) {
            waitLoop.exec();
        }
        const IfcImportResult& ifcResult = wizard.ifcImportResult();
        const QStringList rootTags = ifcResult.rootObject
                                         ? ifcResult.rootObject->tags()
                                         : QStringList{};
        if (!ifcResult.success() || ifcResult.geometryObjectCount != 1 ||
            !ifcResult.rootObject->hasShape() ||
            ifcResult.rootObject->fdsConversionRoute() != QStringLiteral("GEOM") ||
            !rootTags.contains(QStringLiteral("ifc-source-unit:mm")) ||
            !rootTags.contains(QStringLiteral("ifc-merge-strategy:MERGE_ALL")) ||
            !rootTags.contains(QStringLiteral("ifc-simplification:BOUNDING_BOX")) ||
            !rootTags.contains(QStringLiteral("fds-conversion-route:GEOM")) ||
            progress->value() != 100 || cancelButton->isEnabled() ||
            !preview->toPlainText().contains(QStringLiteral("IFC4 / mm")) ||
            !preview->toPlainText().contains(
                QStringLiteral("FDS conversion route: GEOM"))) {
            return fail(qPrintable(
                QStringLiteral("P22 asynchronous IFC import strategy failed: %1")
                    .arg(ifcResult.errorMessage)));
        }
        wizard.reject();
        QApplication::processEvents();

        std::cout << "FireCAE P22 import maturity smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--p23-simulation-task-smoke"))) {
        MainWindow window;
        window.resize(1440, 900);
        window.setInterfaceLanguage(UiLanguage::English);
        window.show();
        window.loadSimpleTestBenchmark();
        QApplication::processEvents();

        QAction* parametersAction = window.findChild<QAction*>(
            QStringLiteral("SimulationParametersAction"));
        QAction* openMpAction = window.findChild<QAction*>(
            QStringLiteral("RunFdsOpenMpAction"));
        QAction* mpiAction = findAction(
            window, QStringLiteral("Run Current Project with CPU/MPI..."));
        if (!parametersAction || !openMpAction || !mpiAction) {
            return fail("P23 simulation parameter or explicit CPU run actions are missing.");
        }

        bool parameterDialogInspected = false;
        QTimer::singleShot(0, &window, [&parameterDialogInspected]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() !=
                                   QStringLiteral("SimulationParametersDialog"))
                    continue;
                auto* tabs = dialog->findChild<QTabWidget*>(
                    QStringLiteral("SimulationParametersTabs"));
                auto* timeConfigured = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationTimeConfiguredCheck"));
                auto* start = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationStartTimeSpin"));
                auto* end = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationEndTimeSpin"));
                auto* step = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialTimeStepSpin"));
                auto* environment = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationEnvironmentConfiguredCheck"));
                auto* ambient = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationAmbientTemperatureSpin"));
                auto* pressure = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationAmbientPressureSpin"));
                auto* gravityZ = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationGravity2Spin"));
                auto* mode = dialog->findChild<QComboBox*>(
                    QStringLiteral("SimulationModeCombo"));
                auto* turbulence = dialog->findChild<QComboBox*>(
                    QStringLiteral("SimulationTurbulenceModelCombo"));
                auto* radiationConfigured = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationRadiationConfiguredCheck"));
                auto* radiationEnabled = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationRadiationEnabledCheck"));
                auto* radiationAngles = dialog->findChild<QSpinBox*>(
                    QStringLiteral("SimulationRadiationAnglesSpin"));
                auto* combustion = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationCombustionConfiguredCheck"));
                auto* mixTime = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationFixedMixTimeSpin"));
                auto* output = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationOutputConfiguredCheck"));
                auto* deviceInterval = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationDeviceOutputIntervalSpin"));
                auto* restart = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationRestartEnabledCheck"));
                auto* restartChid = dialog->findChild<QLineEdit*>(
                    QStringLiteral("SimulationRestartChidEdit"));
                auto* restartInterval = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationRestartIntervalSpin"));
                auto* wind = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationWindConfiguredCheck"));
                auto* windSpeed = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationWindSpeedSpin"));
                auto* initial = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationInitializationConfiguredCheck"));
                auto* initialXMin = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialXMinSpin"));
                auto* initialXMax = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialXMaxSpin"));
                auto* initialYMin = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialYMinSpin"));
                auto* initialYMax = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialYMaxSpin"));
                auto* initialZMin = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialZMinSpin"));
                auto* initialZMax = dialog->findChild<QDoubleSpinBox*>(
                    QStringLiteral("SimulationInitialZMaxSpin"));
                auto* numerics = dialog->findChild<QCheckBox*>(
                    QStringLiteral("SimulationNumericsConfiguredCheck"));
                auto* iterations = dialog->findChild<QSpinBox*>(
                    QStringLiteral("SimulationMaximumPressureIterationsSpin"));
                auto* geomSummary = dialog->findChild<QLabel*>(
                    QStringLiteral("SimulationGeomSummaryLabel"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>(
                    QStringLiteral("SimulationParametersButtons"));
                if (!tabs || tabs->count() < 6 || !timeConfigured || !start ||
                    !end || !step || !environment || !ambient || !pressure ||
                    !gravityZ || !mode || !turbulence || !radiationConfigured ||
                    !radiationEnabled || !radiationAngles || !combustion ||
                    !mixTime || !output || !deviceInterval || !restart ||
                    !restartChid || !restartInterval || !wind || !windSpeed ||
                    !initial || !initialXMin || !initialXMax || !initialYMin ||
                    !initialYMax || !initialZMin || !initialZMax || !numerics ||
                    !iterations || !geomSummary || !buttons) {
                    dialog->reject();
                    return;
                }
                parameterDialogInspected = true;
                timeConfigured->setChecked(true);
                start->setValue(0.25);
                end->setValue(2.0);
                step->setValue(0.01);
                environment->setChecked(true);
                ambient->setValue(23.0);
                pressure->setValue(100000.0);
                gravityZ->setValue(-9.81);
                mode->setCurrentText(QStringLiteral("LES"));
                turbulence->setCurrentText(QStringLiteral("VREMAN"));
                radiationConfigured->setChecked(true);
                radiationEnabled->setChecked(false);
                radiationAngles->setValue(144);
                combustion->setChecked(true);
                mixTime->setValue(0.3);
                output->setChecked(true);
                deviceInterval->setValue(0.2);
                restart->setChecked(true);
                restartChid->setText(QStringLiteral("checkpoint_source"));
                restartInterval->setValue(0.5);
                wind->setChecked(true);
                windSpeed->setValue(5.0);
                initial->setChecked(true);
                initialXMin->setValue(-1.0);
                initialXMax->setValue(1.0);
                initialYMin->setValue(-2.0);
                initialYMax->setValue(2.0);
                initialZMin->setValue(0.0);
                initialZMax->setValue(3.0);
                numerics->setChecked(true);
                iterations->setValue(1500);
                buttons->button(QDialogButtonBox::Ok)->click();
                return;
            }
        });
        parametersAction->trigger();
        QApplication::processEvents();
        if (!parameterDialogInspected) {
            return fail("P23 professional simulation-parameter dialog is incomplete.");
        }

        QTemporaryDir directory;
        const QString firstFds = directory.filePath(QStringLiteral("p23-settings.fds"));
        if (!directory.isValid() || !window.exportCurrentProjectToFds(firstFds)) {
            return fail("P23 configured simulation parameters could not be exported.");
        }
        QFile firstFile(firstFds);
        if (!firstFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("P23 exported FDS input could not be read.");
        }
        const QString configuredText = QString::fromUtf8(firstFile.readAll());
        const QStringList expectedFields{
            QStringLiteral("T_BEGIN=0.25"), QStringLiteral("DT=0.01"),
            QStringLiteral("TMPA=23"), QStringLiteral("P_INF=100000"),
            QStringLiteral("SIMULATION_MODE='LES'"),
            QStringLiteral("TURBULENCE_MODEL='VREMAN'"),
            QStringLiteral("RADIATION=.FALSE."),
            QStringLiteral("NUMBER_RADIATION_ANGLES=144"),
            QStringLiteral("FIXED_MIX_TIME=0.3"),
            QStringLiteral("DT_DEVC=0.2"),
            QStringLiteral("RESTART=.TRUE."),
            QStringLiteral("RESTART_CHID='checkpoint_source'"),
            QStringLiteral("DT_RESTART=0.5"), QStringLiteral("&WIND"),
            QStringLiteral("SPEED=5"), QStringLiteral("&INIT"),
            QStringLiteral("&PRES"), QStringLiteral("MAX_PRESSURE_ITERATIONS=1500")};
        for (const QString& field : expectedFields) {
            if (!configuredText.contains(field)) {
                return fail(qPrintable(QStringLiteral(
                    "P23 exported FDS input is missing professional field: %1")
                                            .arg(field)));
            }
        }

        QAction* undo = findActionByShortcut(window, QKeySequence::Undo);
        QAction* redo = findActionByShortcut(window, QKeySequence::Redo);
        if (!undo || !redo || !undo->isEnabled()) {
            return fail("P23 simulation-parameter edit did not enter Undo/Redo history.");
        }
        undo->trigger();
        redo->trigger();
        QApplication::processEvents();
        const QString projectPath = directory.filePath(QStringLiteral("p23.firecae"));
        if (!window.saveProjectFile(projectPath) || !window.openProjectFile(projectPath)) {
            return fail("P23 simulation parameters did not survive project save/reopen.");
        }
        const QString reopenedFds = directory.filePath(QStringLiteral("p23-reopened.fds"));
        if (!window.exportCurrentProjectToFds(reopenedFds)) {
            return fail("P23 reopened project could not export its simulation parameters.");
        }
        QFile reopenedFile(reopenedFds);
        if (!reopenedFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
            !QString::fromUtf8(reopenedFile.readAll()).contains(
                QStringLiteral("RESTART_CHID='checkpoint_source'"))) {
            return fail("P23 restart/output settings changed after project reopen.");
        }

        const QString sourceInput = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                        .filePath(QStringLiteral("solver_smoke.fds"));
        const QString runInput = directory.filePath(QStringLiteral("solver_smoke.fds"));
        if (!QFile::copy(sourceInput, runInput)) {
            return fail("P23 GUI solver smoke input could not be prepared.");
        }
        const QString executable = FdsRunner::detectExecutable();
        SimulationRunDialog runDialog(runInput, executable,
                                      FdsRunMode::Serial, 1,
                                      FdsSchemaRegistry::defaultVersion());
        auto* backend = runDialog.findChild<QComboBox*>(
            QStringLiteral("SimulationRunBackendCombo"));
        auto* cpuCount = runDialog.findChild<QSpinBox*>(
            QStringLiteral("SimulationRunCpuCountSpin"));
        auto* command = runDialog.findChild<QPlainTextEdit*>(
            QStringLiteral("SimulationRunCommandPreview"));
        auto* environmentCheck = runDialog.findChild<QLabel*>(
            QStringLiteral("SimulationRunEnvironmentCheck"));
        auto* solverVersion = runDialog.findChild<QLabel*>(
            QStringLiteral("SimulationRunSolverVersion"));
        auto* explanation = runDialog.findChild<QLabel*>(
            QStringLiteral("SimulationRunModeExplanation"));
        if (!backend || backend->count() != 3 || !cpuCount || !command ||
            !command->isReadOnly() || !environmentCheck || !solverVersion ||
            !solverVersion->text().contains(QStringLiteral("6.11.1")) ||
            !explanation ||
            !explanation->text().contains(
                QStringLiteral("No GPU solver is configured"))) {
            return fail("P23 CPU execution-mode dialog is incomplete or exposes GPU incorrectly.");
        }
        for (int index = 0; index < backend->count(); ++index) {
            if (backend->itemText(index).contains(QStringLiteral("GPU"),
                                                  Qt::CaseInsensitive)) {
                return fail("P23 execution-mode dialog exposes an unsupported GPU backend.");
            }
        }
        const int serialIndex = backend->findData(
            static_cast<int>(SolverBackendKind::FdsSerialCpu));
        const int openMpIndex = backend->findData(
            static_cast<int>(SolverBackendKind::FdsOpenMpCpu));
        const int mpiIndex = backend->findData(
            static_cast<int>(SolverBackendKind::FdsMpiCpu));
        backend->setCurrentIndex(serialIndex);
        if (runDialog.request().mode != FdsRunMode::Serial ||
            !command->toPlainText().contains(QStringLiteral("fds_openmp.exe"),
                                             Qt::CaseInsensitive)) {
            return fail("P23 Serial CPU preview is not a true one-thread FDS launch.");
        }
        backend->setCurrentIndex(openMpIndex);
        cpuCount->setValue(2);
        if (runDialog.request().mode != FdsRunMode::OpenMp ||
            runDialog.request().threadCount != 2 ||
            !command->toPlainText().contains(QStringLiteral("fds_openmp.exe"),
                                             Qt::CaseInsensitive)) {
            return fail("P23 OpenMP CPU preview does not use the requested thread count.");
        }
        backend->setCurrentIndex(mpiIndex);
        cpuCount->setValue(2);
        if (runDialog.request().mode != FdsRunMode::Mpi ||
            runDialog.request().processCount != 2 ||
            !command->toPlainText().contains(QStringLiteral("mpiexec.exe"),
                                             Qt::CaseInsensitive) ||
            !command->toPlainText().contains(QStringLiteral("-n 2"))) {
            return fail("P23 MPI CPU preview does not show its two-process command.");
        }

        auto* taskTable = window.findChild<QTableWidget*>(
            QStringLiteral("SimulationTaskTable"));
        auto* taskCommand = window.findChild<QPlainTextEdit*>(
            QStringLiteral("SimulationTaskCommandPreview"));
        auto* taskEnvironment = window.findChild<QLabel*>(
            QStringLiteral("SimulationTaskEnvironmentCheck"));
        auto* outputTabs = window.findChild<QTabWidget*>(
            QStringLiteral("SimulationTaskOutputTabs"));
        auto* standardOutput = window.findChild<QPlainTextEdit*>(
            QStringLiteral("SimulationTaskStandardOutput"));
        auto* standardError = window.findChild<QPlainTextEdit*>(
            QStringLiteral("SimulationTaskStandardError"));
        auto* openResults = window.findChild<QPushButton*>(
            QStringLiteral("SimulationTaskOpenResultsButton"));
        if (!taskTable || !taskCommand || !taskCommand->isReadOnly() ||
            !taskEnvironment || !outputTabs || outputTabs->count() != 1 ||
            !standardOutput || !standardError || !openResults) {
            return fail("P23 task center is missing command, environment, log channels, or result entry.");
        }
        if (!window.runFdsFile(runInput, FdsRunMode::OpenMp, 1, 2)) {
            return fail("P23 GUI could not enqueue a real OpenMP calculation.");
        }
        QEventLoop taskLoop;
        QTimer taskPoll;
        QTimer taskTimeout;
        taskPoll.setInterval(25);
        taskTimeout.setSingleShot(true);
        QObject::connect(&taskPoll, &QTimer::timeout, &taskLoop,
                         [&taskLoop, taskTable]() {
                             if (taskTable->rowCount() > 0 && taskTable->item(0, 0)) {
                                 const QString state = taskTable->item(0, 0)->text();
                                 if (state == QStringLiteral("Completed") ||
                                     state == QStringLiteral("Failed") ||
                                     state == QStringLiteral("Cancelled")) {
                                     taskLoop.quit();
                                 }
                             }
                         });
        QObject::connect(&taskTimeout, &QTimer::timeout,
                         &taskLoop, &QEventLoop::quit);
        taskPoll.start();
        taskTimeout.start(120000);
        taskLoop.exec();
        taskPoll.stop();
        QApplication::processEvents();
        const QString terminalState = taskTable->rowCount() > 0 &&
                                              taskTable->item(0, 0)
                                          ? taskTable->item(0, 0)->text()
                                          : QString{};
        if (terminalState != QStringLiteral("Completed") ||
            !taskCommand->toPlainText().contains(QStringLiteral("fds_openmp.exe"),
                                                 Qt::CaseInsensitive) ||
            !taskEnvironment->text().contains(QStringLiteral("Ready")) ||
            (standardOutput->toPlainText().isEmpty() &&
             standardError->toPlainText().isEmpty()) ||
            !openResults->isEnabled()) {
            return fail("P23 GUI task did not complete with command/environment/log/result evidence.");
        }

        qputenv("FIRECAE_UI_LANGUAGE", "zh_CN");
        SimulationRunDialog chineseRunDialog(runInput, executable,
                                             FdsRunMode::Serial, 1);
        SimulationParametersDialog chineseParametersDialog(nullptr);
        auto* chineseBackend = chineseRunDialog.findChild<QComboBox*>(
            QStringLiteral("SimulationRunBackendCombo"));
        const bool chineseUiReady =
            chineseRunDialog.windowTitle() == QStringLiteral("运行 FDS") &&
            chineseParametersDialog.windowTitle() == QStringLiteral("仿真参数") &&
            chineseBackend && chineseBackend->itemText(0).contains(
                                  QStringLiteral("CPU 串行"));
        qputenv("FIRECAE_UI_LANGUAGE", "en");
        if (!chineseUiReady) {
            return fail("P23 simulation/run dialogs are not available in Simplified Chinese.");
        }

        std::cout << "FireCAE P23 simulation/task-center smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--a10-blank-tutorials-smoke"))) {
        return runBlankTutorialGuiAcceptance();
    }
    if (application.arguments().contains(QStringLiteral("--a16-comparison-smoke"))) {
        const QString demoSmv = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-results/demo.smv"));
        const QString demoFds = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-results/demo.fds"));
        const FdsInputComparison input =
            FdsInputComparator::compareFiles(demoFds, demoFds);
        const FdsResultComparison result =
            FdsResultComparator::compareSmvFiles(demoSmv, demoSmv);
        // Exercise the exporter boundary independently of which warnings the
        // demo fixture currently emits, including HTML and CSV metacharacters.
        FdsResultComparison reportResult = result;
        const auto hrrQuantity = std::find_if(result.quantities.cbegin(), result.quantities.cend(),
            [](const auto& quantity) { return quantity.quantity == QStringLiteral("HRR"); });
        if (hrrQuantity == result.quantities.cend() ||
            hrrQuantity->unit != QStringLiteral("kW") || hrrQuantity->timeUnit != QStringLiteral("s"))
            return fail("A16 chart units are not retained from the compared CSV headers.");
        FdsInputComparison reportInput = input;
        const QString longDiffMarker = QStringLiteral("A16_LONG_RAW_DIFF_SENTINEL");
        const QString longDiffEnd = QStringLiteral("A16_RAW_DIFF_END");
        reportInput.rawTextDiff = longDiffMarker + QLatin1Char(' ') +
            QStringLiteral("controller_reference_12345 ").repeated(40) + longDiffEnd;
        reportInput.rawTextDiff += QStringLiteral("\n\nA16_TRACE_FIRST\n") +
            QStringLiteral("A16_TRACE_ROW source=reference candidate=unchanged \n").repeated(140) +
            QStringLiteral("A16_TRACE_LAST\n");
        const QString exclusionWarning = QStringLiteral(
            "Event log retained but excluded from numerical time-series comparison: "
            "\"<event>&controller,1.csv\"\n(reference)");
        reportResult.warnings.append(exclusionWarning);
        QTemporaryDir reportDirectory;
        FdsComparisonReportOptions options;
        options.outputDirectory = reportDirectory.path();
        options.baseFileName = QStringLiteral("a16-gui-comparison");
        options.title = QStringLiteral("A16 GUI comparison acceptance");
        options.fireCaeVersion = QCoreApplication::applicationVersion();
        options.fdsVersion = result.referenceProvenance.solverRevision;
        options.reproduciblePaths = {demoFds, demoSmv};
        const FdsComparisonReportArtifacts artifacts = FdsComparisonReport::write(
            &reportInput, reportResult, {}, options);
        QFile htmlFile(artifacts.htmlFile);
        QFile csvFile(artifacts.csvFile);
        if (!reportDirectory.isValid() || !input.semanticallyEquivalent() ||
            !result.passed() || !reportResult.passed() || !artifacts.success() ||
            !QFileInfo::exists(artifacts.pdfFile) ||
            QFileInfo(artifacts.pdfFile).size() < 1000 ||
            !htmlFile.open(QIODevice::ReadOnly) ||
            !csvFile.open(QIODevice::ReadOnly))
            return fail("A16 HTML/PDF/CSV comparison report bundle is incomplete.");
        const QByteArray htmlContents = htmlFile.readAll();
        const QByteArray csvContents = csvFile.readAll();
        const QByteArray escapedHtmlWarning = exclusionWarning.toHtmlEscaped()
            .replace(QLatin1Char('\n'), QStringLiteral("<br>")).toUtf8();
        QString escapedCsvWarning = exclusionWarning;
        escapedCsvWarning.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        const QByteArray csvWarningRow =
            QStringLiteral("\"%1\"\n").arg(escapedCsvWarning).toUtf8();
        if (!htmlContents.contains("FDS input comparison") ||
            !htmlContents.contains("Comparison scope and warnings") ||
            !htmlContents.contains("Time (s):") || !htmlContents.contains("HRR (kW):") ||
            !htmlContents.contains("not covered by the numerical PASS") ||
            !htmlContents.contains(escapedHtmlWarning) ||
            htmlContents.contains("<event>") ||
            !csvContents.contains("[FDS_RESULT_COMPARISON]") ||
            !csvContents.contains("[COMPARISON_SCOPE]") ||
            !csvContents.contains("[RESULT_WARNINGS]\nwarning\n") ||
            !csvContents.contains(csvWarningRow) ||
            !csvContents.contains(QStringLiteral("warning_count,%1\n")
                                      .arg(reportResult.warnings.size()).toUtf8()))
            return fail("A16 comparison reports omit or corrupt scope and exclusion warnings.");
        // Validate Qt's imported layout, not just the presence of CSS strings:
        // malformed percentage syntax used to discard later padding/pre-wrap.
        QTextDocument reportLayout;
        reportLayout.setHtml(QString::fromUtf8(htmlContents));
        reportLayout.setTextWidth(640.0);
        reportLayout.documentLayout()->documentSize();
        int reportTableCount = 0;
        for (QTextFrame* frame : reportLayout.rootFrame()->childFrames()) {
            if (auto* table = qobject_cast<QTextTable*>(frame)) {
                ++reportTableCount;
                if (table->format().cellPadding() < 4.0 ||
                    table->format().headerRowCount() != 1 ||
                    reportLayout.documentLayout()->frameBoundingRect(table).width() >
                        reportLayout.textWidth() + 1.0)
                    return fail("A16 comparison tables lose cell spacing or exceed the page width.");
            }
        }
        bool wrappedRawDiff = false;
        int traceParagraphs = 0;
        for (QTextBlock block = reportLayout.begin(); block.isValid(); block = block.next()) {
            if (block.text().startsWith(QStringLiteral("A16_TRACE_ROW "))) ++traceParagraphs;
            if (!block.text().contains(longDiffMarker)) continue;
            if (!block.text().contains(longDiffEnd) || !block.layout() ||
                block.blockFormat().nonBreakableLines() || block.layout()->lineCount() < 2)
                return fail("A16 long raw-diff text is truncated or cannot wrap.");
            for (int line = 0; line < block.layout()->lineCount(); ++line) {
                if (block.layout()->lineAt(line).naturalTextWidth() >
                    reportLayout.textWidth() + 1.0)
                    return fail("A16 wrapped raw-diff line still exceeds the page width.");
            }
            wrappedRawDiff = true;
        }
        if (reportTableCount < 6 || !wrappedRawDiff || traceParagraphs != 140 ||
            !reportLayout.toPlainText().contains(QStringLiteral("A16_TRACE_LAST")))
            return fail("A16 comparison layout fixture is missing tables or the raw diff.");
        MainWindow window;
        window.show();
        QApplication::processEvents();
        if (!window.findChild<QAction*>(QStringLiteral("CompareFdsResultsAction")))
            return fail("A16 comparison GUI action is missing.");
        std::cout << "FireCAE A16 comparison/report GUI smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a15-reliability-smoke"))) {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        QAction* startAction = window.findChild<QAction*>(QStringLiteral("StartPageAction"));
        if (!startAction) return fail("A15 start-page action is missing.");
        startAction->trigger();
        QApplication::processEvents();
        auto* startPage = window.findChild<StartPageWidget*>(
            QStringLiteral("StartPageWidget"));
        auto* tutorials = window.findChild<QListWidget*>(
            QStringLiteral("StartPageTutorialList"));
        if (!startPage || !startPage->isVisible() || !tutorials ||
            tutorials->count() != 13 ||
            !window.findChild<QPushButton*>(QStringLiteral("StartPageNewButton")) ||
            !window.findChild<QListWidget*>(QStringLiteral("StartPageRecentList")) ||
            !window.findChild<QListWidget*>(QStringLiteral("StartPageRecoveryList")) ||
            !window.findChild<QLabel*>(QStringLiteral("StartPageRuntimeStatus")))
            return fail("A15 start page is missing projects, tutorials, recovery, or runtime status.");
        if (!captureAcceptance(window, QStringLiteral("A15-01-start-page.png")))
            return fail("A15 start-page screenshot failed.");

        QAction* preferences = window.findChild<QAction*>(
            QStringLiteral("ApplicationPreferencesAction"));
        bool preferencesInspected = false;
        QTimer::singleShot(0, &window, [&preferencesInspected]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() !=
                                   QStringLiteral("ApplicationSettingsDialog"))
                    continue;
                preferencesInspected =
                    dialog->findChild<QComboBox*>(QStringLiteral("SettingsLanguageCombo")) &&
                    dialog->findChild<QComboBox*>(QStringLiteral("SettingsUnitCombo")) &&
                    dialog->findChild<QComboBox*>(QStringLiteral("SettingsThemeCombo")) &&
                    dialog->findChild<QCheckBox*>(QStringLiteral("SettingsAutoSaveCheck")) &&
                    dialog->findChild<QLineEdit*>(QStringLiteral("SettingsFdsExecutableEdit")) &&
                    dialog->findChild<QLineEdit*>(QStringLiteral("SettingsSmokeviewExecutableEdit")) &&
                    dialog->findChild<QSpinBox*>(QStringLiteral("SettingsMpiProcessCountSpin")) &&
                    dialog->findChild<QCheckBox*>(QStringLiteral("SettingsAutoOpenResultsCheck")) &&
                    dialog->findChild<QComboBox*>(QStringLiteral("SettingsRenderQualityCombo"));
                dialog->reject();
                return;
            }
        });
        if (!preferences) return fail("A15 Preferences action is missing.");
        preferences->trigger();
        if (!preferencesInspected)
            return fail("A15 versioned Preferences dialog is incomplete.");

        QAction* resources = window.findChild<QAction*>(
            QStringLiteral("ProjectResourcesAction"));
        bool resourcesInspected = false;
        QTimer::singleShot(0, &window, [&resourcesInspected]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() !=
                                   QStringLiteral("ProjectResourcesDialog"))
                    continue;
                resourcesInspected =
                    dialog->findChild<QTableWidget*>(QStringLiteral("ProjectResourceTable")) &&
                    dialog->findChild<QPushButton*>(QStringLiteral("ProjectResourceRelinkButton")) &&
                    dialog->findChild<QPushButton*>(QStringLiteral("ProjectResourceRelativeButton"));
                dialog->reject();
                return;
            }
        });
        if (!resources) return fail("A15 Project Resources action is missing.");
        resources->trigger();
        if (!resourcesInspected)
            return fail("A15 Project Resources dialog is incomplete.");

        for (const QString& actionName : {QStringLiteral("PackageProjectAction"),
                                          QStringLiteral("UnpackProjectAction"),
                                          QStringLiteral("CopyProjectAction"),
                                          QStringLiteral("CleanUnusedResultsAction")}) {
            if (!window.findChild<QAction*>(actionName))
                return fail("A15 package/copy/cleanup action is missing.");
        }
        QAction* diagnostics = window.findChild<QAction*>(
            QStringLiteral("DiagnosticsAction"));
        bool diagnosticsInspected = false;
        QTimer::singleShot(0, &window, [&diagnosticsInspected]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || dialog->objectName() != QStringLiteral("DiagnosticsDialog"))
                    continue;
                auto* report = dialog->findChild<QPlainTextEdit*>(
                    QStringLiteral("DiagnosticsReportEdit"));
                diagnosticsInspected = report &&
                    report->toPlainText().contains(QStringLiteral("FireCAE version")) &&
                    report->toPlainText().contains(QStringLiteral("OpenCascade")) &&
                    report->toPlainText().contains(QStringLiteral("Auto-save directory"));
                dialog->reject();
                return;
            }
        });
        if (!diagnostics) return fail("A15 Diagnostics action is missing.");
        diagnostics->trigger();
        if (!diagnosticsInspected)
            return fail("A15 diagnostic report omits required runtime information.");
        std::cout << "FireCAE A15 reliability GUI smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a14-import-smoke"))) {
        QTemporaryDir directory;
        const QString stlPath = directory.filePath(QStringLiteral("gui_tetra.stl"));
        QFile stl(stlPath);
        const QByteArray contents(
            "solid tetra\n"
            "facet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n"
            "facet normal 0 -1 0\nouter loop\nvertex 0 0 0\nvertex 0 0 1\nvertex 1 0 0\nendloop\nendfacet\n"
            "facet normal -1 0 0\nouter loop\nvertex 0 0 0\nvertex 0 1 0\nvertex 0 0 1\nendloop\nendfacet\n"
            "facet normal 1 1 1\nouter loop\nvertex 1 0 0\nvertex 0 0 1\nvertex 0 1 0\nendloop\nendfacet\nendsolid tetra\n");
        if (!directory.isValid() || !stl.open(QIODevice::WriteOnly) ||
            stl.write(contents) != contents.size())
            return fail("A14 GUI STL fixture could not be written.");
        stl.close();

        GeometryImportWizard wizard(stlPath);
        wizard.show();
        QApplication::processEvents();
        if (!wizard.findChild<QLineEdit*>(QStringLiteral("GeometryImportFileEdit")) ||
            !wizard.findChild<QComboBox*>(QStringLiteral("GeometryImportUnitCombo")) ||
            !wizard.findChild<QDoubleSpinBox*>(QStringLiteral("GeometryImportOriginXSpin")) ||
            !wizard.findChild<QDoubleSpinBox*>(QStringLiteral("GeometryImportLinearDeflectionSpin")))
            return fail("A14 import wizard is missing file/unit/origin/quality controls.");
        wizard.next();
        wizard.next();
        wizard.next();
        auto* importProgress = wizard.findChild<QProgressBar*>(
            QStringLiteral("GeometryImportProgressBar"));
        if (!importProgress)
            return fail("A14 import wizard is missing background progress.");
        if (importProgress->value() < 100) {
            QEventLoop importLoop;
            QTimer importTimeout;
            importTimeout.setSingleShot(true);
            QObject::connect(importProgress, &QProgressBar::valueChanged,
                             &importLoop, [&importLoop](int value) {
                                 if (value >= 100) importLoop.quit();
                             });
            QObject::connect(&importTimeout, &QTimer::timeout,
                             &importLoop, &QEventLoop::quit);
            importTimeout.start(30000);
            importLoop.exec();
        }
        auto* report = wizard.findChild<QPlainTextEdit*>(
            QStringLiteral("GeometryImportPreviewReport"));
        if (!report || !report->toPlainText().contains(QStringLiteral("Triangles: 4")))
            return fail("A14 import wizard did not create a real geometry preview report.");
        wizard.reject();

        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        if (!window.importGeometryFile(stlPath))
            return fail("A14 GUI could not commit imported STL geometry.");
        QTreeWidget* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!tree || tree->topLevelItemCount() != 1 ||
            tree->topLevelItem(0)->child(0)->childCount() != 1)
            return fail("A14 imported geometry did not enter the UUID model tree.");
        if (!captureAcceptance(window, QStringLiteral("A14-01-stl-import.png")))
            return fail("A14 import acceptance screenshot failed.");
        const QString glbPath = directory.filePath(QStringLiteral("gui_material.glb"));
        QProcess converter;
        converter.setProgram(IfcImportService().converterPath());
        converter.setArguments({
            QStringLiteral("--no-progress"), QStringLiteral("--use-element-guids"),
            QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                .filePath(QStringLiteral("tessellated-item.ifc")),
            glbPath});
        converter.start();
        if (!converter.waitForStarted(10000) || !converter.waitForFinished(120000) ||
            converter.exitStatus() != QProcess::NormalExit || converter.exitCode() != 0 ||
            !window.importGeometryFile(glbPath))
            return fail("A14 GUI could not import a material-bearing GLB.");
        QApplication::processEvents();
        if (OccViewWidget* view = window.findChild<OccViewWidget*>(
                QStringLiteral("Model3DView"))) view->fitAll();
        QApplication::processEvents();
        if (!captureAcceptance(window, QStringLiteral("A14-02-glb-material.png")))
            return fail("A14 GLB material acceptance screenshot failed.");
        const QString projectPath = directory.filePath(QStringLiteral("a14-import.firecae"));
        if (!window.saveProjectFile(projectPath))
            return fail("A14 imported project could not be saved.");
        MainWindow reopened;
        if (!reopened.openProjectFile(projectPath))
            return fail("A14 imported project could not be reopened.");
        QTreeWidget* reopenedTree = reopened.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!reopenedTree || reopenedTree->topLevelItem(0)->child(0)->childCount() != 2)
            return fail("A14 source geometry did not survive save/reopen.");
        std::cout << "FireCAE A14 geometry import GUI smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--a13-seven-tutorial-smokeview-evidence"))) {
        const QString outputDirectory = qEnvironmentVariable(
            "FIRECAE_ACCEPTANCE_SCREENSHOT_DIR",
            QDir::temp().filePath(QStringLiteral("firecae-seven-smokeview")));
        if (!QDir().mkpath(outputDirectory))
            return fail("Seven-tutorial Smokeview evidence directory could not be created.");
        struct EvidenceCase {
            QString name;
            FcResultFileType type;
            QString quantity;
            QString fieldFile;
            QString plane;
            double planeValue = 0.0;
            double time = 0.0;
        };
        const QVector<EvidenceCase> cases = {
            {QStringLiteral("activate_vents"), FcResultFileType::Particle,
             {}, {}, {}, 0.0, 12.0},
            {QStringLiteral("bucket_test_2"), FcResultFileType::Boundary,
             QStringLiteral("water drops AMPUA"), {}, {}, 0.0, 15.0},
            {QStringLiteral("couch"), FcResultFileType::Smoke3D,
             QStringLiteral("SOOT DENSITY"), {}, {}, 0.0, 255.0},
            {QStringLiteral("couch_smoke_12s"), FcResultFileType::Smoke3D,
             QStringLiteral("SOOT DENSITY"), {}, {}, 0.0, 12.0},
            {QStringLiteral("HVAC_aircoil"), FcResultFileType::Slice,
             QStringLiteral("TEMPERATURE"), QStringLiteral("HVAC_aircoil_1_1.sf"),
             QStringLiteral("PBY"), 0.5, 1.0},
            {QStringLiteral("tunnel_demo"), FcResultFileType::Smoke3D,
             QStringLiteral("SOOT DENSITY"), {}, {}, 0.0, 30.0},
            {QStringLiteral("tunnel_smoke_10s"), FcResultFileType::Smoke3D,
             QStringLiteral("SOOT DENSITY"), {}, {}, 0.0, 10.0},
        };
        for (const EvidenceCase& item : cases) {
            const QDir caseDirectory(QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                         .filePath(QStringLiteral("gui-generated/") +
                                                   item.name));
            SmokeviewFrameRenderRequest request;
            request.smvFilePath = caseDirectory.filePath(item.name +
                                                         QStringLiteral(".smv"));
            request.outputDirectory = outputDirectory;
            request.fieldType = item.type;
            request.quantity = item.quantity;
            if (!item.fieldFile.isEmpty())
                request.fieldFilePath = caseDirectory.filePath(item.fieldFile);
            request.slicePlaneKeyword = item.plane;
            request.slicePlaneValue = item.planeValue;
            request.times = {item.time};
            request.filePrefix = item.name + QStringLiteral("-smokeview");

            SmokeviewFrameRenderer renderer;
            bool success = false;
            QStringList images;
            QString failure;
            QEventLoop loop;
            QObject::connect(&renderer, &SmokeviewFrameRenderer::finished, &loop,
                             [&](bool ok, const QStringList& rendered,
                                 const QString& error) {
                                 success = ok;
                                 images = rendered;
                                 failure = error;
                                 loop.quit();
                             });
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
                renderer.cancel();
                failure = QStringLiteral("Smokeview evidence rendering timed out.");
                loop.quit();
            });
            QString startError;
            if (!renderer.start(request, &startError))
                return fail(QStringLiteral("%1 Smokeview evidence could not start: %2")
                                .arg(item.name, startError)
                                .toLocal8Bit().constData());
            timeout.start(150000);
            loop.exec();
            if (!success || images.size() != 1 ||
                !QFileInfo::exists(images.constFirst())) {
                return fail(QStringLiteral("%1 Smokeview evidence failed: %2")
                                .arg(item.name, failure)
                                .toLocal8Bit().constData());
            }
            std::cout << item.name.toStdString() << " Smokeview evidence: "
                      << images.constFirst().toStdString() << '\n';
        }
        std::cout << "FireCAE seven-tutorial Smokeview evidence passed.\n";
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--p26-guided-tutorial-smoke"))) {
        const QVector<TutorialDefinition> tutorials = TutorialGuideWidget::catalog();
        if (tutorials.size() != 13)
            return fail("P26 tutorial catalog does not contain six beginner and seven FDS tutorials.");
        QSet<QString> tutorialIds;
        for (const TutorialDefinition& tutorial : tutorials) {
            if (tutorial.id.isEmpty() || tutorial.title.isEmpty() ||
                tutorial.purpose.isEmpty() || tutorial.completedEffect.isEmpty() ||
                tutorial.prerequisites.isEmpty() || tutorial.finalProjectHint.isEmpty() ||
                tutorial.steps.size() < 6)
                return fail("P26 tutorial metadata or step sequence is incomplete.");
            tutorialIds.insert(tutorial.id);
            for (const TutorialStepDefinition& step : tutorial.steps) {
                if (step.title.isEmpty() || step.menuPath.isEmpty() ||
                    step.instructions.isEmpty() || step.parameters.isEmpty() ||
                    step.physicalMeaning.isEmpty() || step.rationale.isEmpty() ||
                    step.expected.isEmpty() || step.commonError.isEmpty())
                    return fail("P26 a guided step is missing operating or learning content.");
            }
        }
        if (tutorialIds.size() != 13 ||
            !tutorialIds.contains(QStringLiteral("first_fire")) ||
            !tutorialIds.contains(QStringLiteral("fire_design_scenarios")) ||
            !tutorialIds.contains(QStringLiteral("activate_vents")) ||
            !tutorialIds.contains(QStringLiteral("tunnel_smoke_10s")))
            return fail("P26 tutorial IDs are incomplete or duplicated.");

        MainWindow window;
        window.resize(1500, 920);
        window.show();
        QApplication::processEvents();
        window.startGuidedTutorial(QStringLiteral("first_fire"), true);
        QApplication::processEvents();
        auto* guide = window.findChild<TutorialGuideWidget*>(
            QStringLiteral("TutorialGuideWidget"));
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("TutorialGuideDock"));
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!guide || !dock || !dock->isVisible() ||
            guide->activeTutorialId() != QStringLiteral("first_fire") ||
            guide->stepCount() < 9 || !guide->currentStepComplete() || !tree)
            return fail("P26 First Fire guide did not open at a verified blank-project step.");
        if (!captureAcceptance(window, QStringLiteral("P26-01-guided-first-fire.png")))
            return fail("P26 guided tutorial screenshot capture failed.");
        QTreeWidgetItem* root = tree->topLevelItem(0);
        int objectCount = 0;
        if (root) {
            for (int i = 0; i < root->childCount(); ++i)
                objectCount += root->child(i)->childCount();
        }
        if (objectCount != 0)
            return fail("P26 guided tutorial injected completed business objects into the blank project.");
        auto* next = guide->findChild<QPushButton*>(QStringLiteral("TutorialGuideNextButton"));
        auto* openTool = guide->findChild<QPushButton*>(
            QStringLiteral("TutorialGuideOpenToolButton"));
        auto* stepDetails = guide->findChild<QLabel*>(
            QStringLiteral("TutorialGuideStepDetails"));
        if (!next || !next->isEnabled() || !openTool || !stepDetails ||
            !stepDetails->text().contains(QStringLiteral("菜单路径")))
            return fail("P26 guided tutorial navigation or detailed learning content is unavailable.");
        next->click();
        QApplication::processEvents();
        if (guide->currentStepIndex() != 1 || guide->currentStepComplete() ||
            !window.findChild<QAction*>(QStringLiteral("ProjectSettingsAction")))
            return fail("P26 step validation did not block an incomplete project-settings step.");
        dock->hide();
        window.startGuidedTutorial(QStringLiteral("first_fire"));
        QApplication::processEvents();
        if (!dock->isVisible() || guide->currentStepIndex() != 1)
            return fail("P26 tutorial could not exit and resume at the saved step.");
        int guidedActionCount = 0;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->objectName().startsWith(QStringLiteral("GuidedTutorial_")))
                ++guidedActionCount;
        }
        if (guidedActionCount != 13 ||
            !window.findChild<QAction*>(QStringLiteral("ActivateVentsTutorialAction")))
            return fail("P26 guided entries or completed-reference examples are missing.");

        // A narrow dock must expose the end of long instructions without
        // drawing over adjacent docks or hiding the tutorial actions.
        {
            FcProject compactProject(QStringLiteral("Untitled"));
            TutorialGuideWidget compactGuide;
            compactGuide.setAttribute(Qt::WA_DontShowOnScreen);
            compactGuide.setFixedSize(300, 200);
            compactGuide.setContext(&compactProject, {}, {}, false);
            compactGuide.startTutorial(QStringLiteral("tunnel_smoke_10s"), true);
            auto* compactSteps = compactGuide.findChild<QListWidget*>(
                QStringLiteral("TutorialGuideStepList"));
            auto* compactScroll = compactGuide.findChild<QScrollArea*>(
                QStringLiteral("TutorialGuideScrollArea"));
            auto* compactDetails = compactGuide.findChild<QLabel*>(
                QStringLiteral("TutorialGuideStepDetails"));
            auto* compactStatus = compactGuide.findChild<QLabel*>(
                QStringLiteral("TutorialGuideCheckStatus"));
            const auto compactButtons = compactGuide.findChildren<QPushButton*>();
            if (!compactSteps || !compactScroll || !compactDetails || !compactStatus ||
                compactButtons.size() != 6)
                return fail("P26 compact tutorial layout controls are unavailable.");
            compactSteps->setCurrentRow(1);
            compactGuide.show();
            const auto settleCompactLayout = []() {
                for (int pass = 0; pass < 3; ++pass) QApplication::processEvents();
            };
            settleCompactLayout();
            auto* compactCheck = compactGuide.findChild<QPushButton*>(
                QStringLiteral("TutorialGuideCheckButton"));
            auto* compactOpen = compactGuide.findChild<QPushButton*>(
                QStringLiteral("TutorialGuideOpenToolButton"));
            auto* compactNext = compactGuide.findChild<QPushButton*>(
                QStringLiteral("TutorialGuideNextButton"));
            auto* compactRestart = compactGuide.findChild<QPushButton*>(
                QStringLiteral("TutorialGuideRestartButton"));
            auto* compactExit = compactGuide.findChild<QPushButton*>(
                QStringLiteral("TutorialGuideExitButton"));
            if (!compactCheck || !compactOpen || !compactNext || !compactRestart || !compactExit)
                return fail("P26 compact tutorial actions are unavailable.");
            compactCheck->click();
            settleCompactLayout();
            for (QPushButton* button : compactButtons) {
                const QRect bounds(button->mapTo(&compactGuide, QPoint()), button->size());
                if (!compactGuide.rect().contains(bounds) ||
                    button->width() < button->minimumSizeHint().width())
                    return fail("P26 compact tutorial action is clipped or outside the dock.");
            }
            if (!compactGuide.rect().contains(compactScroll->geometry()) ||
                compactScroll->horizontalScrollBar()->maximum() != 0 ||
                compactScroll->verticalScrollBar()->maximum() <= 0 ||
                compactDetails->height() < compactDetails->heightForWidth(compactDetails->width()))
                return fail("P26 compact tutorial instructions cannot be read by scrolling.");
            compactScroll->verticalScrollBar()->setValue(compactScroll->verticalScrollBar()->maximum());
            settleCompactLayout();
            const QRect statusBounds(compactStatus->mapTo(compactScroll->viewport(), QPoint()),
                                     compactStatus->size());
            if (!compactScroll->viewport()->rect().contains(statusBounds) || compactNext->isEnabled())
                return fail("P26 compact tutorial status is clipped or an incomplete step can continue.");
            QString requestedTool;
            bool requestedExit = false;
            QObject::connect(&compactGuide, &TutorialGuideWidget::openActionRequested,
                             [&](const QString& action) { requestedTool = action; });
            QObject::connect(&compactGuide, &TutorialGuideWidget::closeRequested,
                             [&]() { requestedExit = true; });
            compactOpen->click();
            compactRestart->click();
            compactExit->click();
            if (requestedTool != QStringLiteral("AddFdsObjectAction") || !requestedExit ||
                compactGuide.currentStepIndex() != 0 || !compactGuide.currentStepComplete())
                return fail("P26 compact tutorial tool, restart or exit action changed behavior.");
        }
        std::cout << "FireCAE P26 guided tutorial system smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(
            QStringLiteral("--p27-first-fire-e2e"))) {
        return runP27FirstFireEndToEndAcceptance();
    }
    if (application.arguments().contains(QStringLiteral("--reacceptance-multiple-outputs-e2e"))) {
        return runP27FirstFireEndToEndAcceptance(true);
    }
    if (application.arguments().contains(
            QStringLiteral("--p27-complex-ifc-e2e"))) {
        return runP27ComplexIfcEndToEndAcceptance();
    }
    if (application.arguments().contains(
            QStringLiteral("--p29-five-tutorial-e2e"))) {
        return runP29FiveTutorialEndToEndAcceptance();
    }
    if (application.arguments().contains(
            QStringLiteral("--p25-scenario-reliability-smoke"))) {
        QTemporaryDir directory;
        if (!directory.isValid()) return fail("P25 temporary directory is unavailable.");

        ApplicationSettings expected;
        expected.language = QStringLiteral("zh_CN");
        expected.defaultUnit = QStringLiteral("mm");
        expected.fdsExecutable = directory.filePath(QStringLiteral("fds.exe"));
        expected.mpiExecutable = directory.filePath(QStringLiteral("mpiexec.exe"));
        expected.smokeviewExecutable = directory.filePath(QStringLiteral("smokeview.exe"));
        expected.autoSaveEnabled = false;
        expected.autoSaveIntervalMinutes = 17;
        expected.autoSaveMaximumFiles = 2;
        expected.backupBeforeOpen = true;
        expected.saveBeforeRun = false;
        expected.autoOpenResults = false;
        expected.defaultMeshCellSize = 0.125;
        expected.defaultMaterial = QStringLiteral("GYPSUM");
        expected.defaultColorScheme = QStringLiteral("viridis");
        expected.highDpiEnabled = false;
        expected.largeFileWarningMegabytes = 321;
        expected.logLevel = QStringLiteral("debug");
        const QString settingsPath = directory.filePath(QStringLiteral("p25.ini"));
        ApplicationSettingsStore store(settingsPath);
        QString error;
        if (!store.save(expected, &error))
            return fail(QStringLiteral("P25 versioned settings save failed: %1").arg(error)
                            .toLocal8Bit().constData());
        const ApplicationSettings actual = store.load();
        if (actual.formatVersion != ApplicationSettings::CurrentFormatVersion ||
            actual.language != expected.language || actual.defaultUnit != expected.defaultUnit ||
            actual.fdsExecutable != QDir::toNativeSeparators(expected.fdsExecutable) ||
            actual.mpiExecutable != QDir::toNativeSeparators(expected.mpiExecutable) ||
            actual.smokeviewExecutable != QDir::toNativeSeparators(expected.smokeviewExecutable) ||
            actual.autoSaveEnabled != expected.autoSaveEnabled ||
            actual.autoSaveIntervalMinutes != expected.autoSaveIntervalMinutes ||
            actual.autoSaveMaximumFiles != expected.autoSaveMaximumFiles ||
            actual.backupBeforeOpen != expected.backupBeforeOpen ||
            actual.saveBeforeRun != expected.saveBeforeRun ||
            actual.autoOpenResults != expected.autoOpenResults ||
            std::abs(actual.defaultMeshCellSize - expected.defaultMeshCellSize) > 1.0e-9 ||
            actual.defaultMaterial != expected.defaultMaterial ||
            actual.defaultColorScheme != expected.defaultColorScheme ||
            actual.highDpiEnabled != expected.highDpiEnabled ||
            actual.largeFileWarningMegabytes != expected.largeFileWarningMegabytes ||
            actual.logLevel != expected.logLevel)
            return fail("P25 versioned application settings did not round-trip.");

        ApplicationSettingsDialog settingsDialog(actual);
        if (!settingsDialog.findChild<QLineEdit*>(QStringLiteral("SettingsMpiExecutableEdit")) ||
            !settingsDialog.findChild<QCheckBox*>(QStringLiteral("SettingsBackupBeforeOpenCheck")) ||
            !settingsDialog.findChild<QCheckBox*>(QStringLiteral("SettingsSaveBeforeRunCheck")) ||
            !settingsDialog.findChild<QDoubleSpinBox*>(QStringLiteral("SettingsDefaultMeshCellSizeSpin")) ||
            !settingsDialog.findChild<QLineEdit*>(QStringLiteral("SettingsDefaultMaterialEdit")) ||
            !settingsDialog.findChild<QComboBox*>(QStringLiteral("SettingsDefaultColorSchemeCombo")) ||
            !settingsDialog.findChild<QCheckBox*>(QStringLiteral("SettingsHighDpiCheck")) ||
            !settingsDialog.findChild<QSpinBox*>(QStringLiteral("SettingsLargeFileWarningSpin")))
            return fail("P25 preferences dialog is missing required controls.");

        const QString projectPath = directory.filePath(QStringLiteral("reliable.firecae"));
        {
            QSaveFile project(projectPath);
            if (!project.open(QIODevice::WriteOnly) ||
                project.write("{\"format\":\"FireCAEProject\"}\n") < 0 ||
                !project.commit()) return fail("P25 backup source could not be created.");
        }
        ProjectRecoveryManager recovery(directory.filePath(QStringLiteral("recovery")));
        QString backup;
        for (int index = 0; index < 3; ++index) {
            if (!recovery.backupProjectFile(projectPath, 2, &backup, &error) ||
                !QFileInfo::exists(backup))
                return fail(QStringLiteral("P25 rotating backup failed: %1").arg(error)
                                .toLocal8Bit().constData());
        }
        const QString backupDirectory = QFileInfo(backup).absolutePath();
        if (QDir(backupDirectory).entryList({QStringLiteral("*.firecae")},
                                            QDir::Files).size() != 2)
            return fail("P25 pre-open backup rotation did not retain exactly two copies.");

        const QVector<FdsLibraryEntry> library = FdsPropertyLibrary::builtInEntries();
        QSet<QString> categories;
        for (const FdsLibraryEntry& entry : library) categories.insert(entry.category);
        for (const QString& required : {QStringLiteral("Materials"),
                                        QStringLiteral("Surfaces"),
                                        QStringLiteral("Reactions"),
                                        QStringLiteral("Species"),
                                        QStringLiteral("Particles"),
                                        QStringLiteral("Device Templates"),
                                        QStringLiteral("Sprinkler Templates"),
                                        QStringLiteral("Fire Source Curves")}) {
            if (!categories.contains(required))
                return fail(QStringLiteral("P25 library category missing: %1").arg(required)
                                .toLocal8Bit().constData());
        }

        const QString previousMpi = SolverBackendRegistry::configuredMpiExecutable();
        QFile fds(expected.fdsExecutable);
        QFile mpi(expected.mpiExecutable);
        if (!fds.open(QIODevice::WriteOnly) || !mpi.open(QIODevice::WriteOnly))
            return fail("P25 solver fixture executables could not be created.");
        fds.close();
        mpi.close();
        SolverBackendRegistry::setConfiguredMpiExecutable(expected.mpiExecutable);
        SolverLaunchContext context;
        context.inputFilePath = directory.filePath(QStringLiteral("case.fds"));
        context.fdsExecutablePath = expected.fdsExecutable;
        context.processCount = 4;
        const SolverLaunchPlan mpiPlan =
            SolverBackendRegistry::create(SolverBackendKind::FdsMpiCpu)
                ->createLaunchPlan(context);
        SolverBackendRegistry::setConfiguredMpiExecutable(previousMpi);
        if (!mpiPlan.valid() ||
            QFileInfo(mpiPlan.program).absoluteFilePath() != QFileInfo(expected.mpiExecutable).absoluteFilePath() ||
            !mpiPlan.commandLine.contains(QStringLiteral("-n 4"))) {
            std::cerr << "P25 MPI diagnostic: valid=" << mpiPlan.valid()
                      << "; program=" << mpiPlan.program.toStdString()
                      << "; expected=" << expected.mpiExecutable.toStdString()
                      << "; command=" << mpiPlan.commandLine.toStdString()
                      << "; error=" << mpiPlan.errorMessage.toStdString() << std::endl;
            return fail("P25 configured MPI launcher was not used by the CPU MPI backend.");
        }

        const QString diagnostics = CrashDiagnostics::diagnosticReport(
            expected.fdsExecutable, expected.mpiExecutable,
            expected.smokeviewExecutable, recovery.recoveryDirectory());
        if (!diagnostics.contains(QStringLiteral("MPI launcher:")) ||
            !diagnostics.contains(QStringLiteral("Required deployed runtimes:")) ||
            !diagnostics.contains(QStringLiteral("MISSING")))
            return fail("P25 runtime diagnostics do not expose launcher and missing-file state.");

        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QApplication::processEvents();
        window.loadSimpleTestBenchmark();
        QApplication::processEvents();
        QAction* scenarios = window.findChild<QAction*>(QStringLiteral("ScenarioManagerAction"));
        if (!scenarios) return fail("P25 scenario manager action is missing.");
        bool scenarioDialogVerified = false;
        acceptNextNamedDialog(QStringLiteral("ScenarioManagerDialog"),
                              [&scenarioDialogVerified](QDialog* dialog) {
            scenarioDialogVerified =
                dialog->findChild<QPushButton*>(QStringLiteral("ScenarioCompareButton")) &&
                dialog->findChild<QTreeWidget*>(QStringLiteral("ScenarioObjectTree")) &&
                dialog->findChild<QTableWidget*>(QStringLiteral("ScenarioOverrideTable"));
        });
        scenarios->trigger();
        QApplication::processEvents();
        if (!scenarioDialogVerified)
            return fail("P25 scenario compare, UUID state, or override UI is missing.");
        std::cout << "FireCAE P25 scenario, library, preferences, and reliability smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--p24-results-maturity-smoke"))) {
        NativeResultViewerWidget viewer;
        viewer.resize(1400, 900);
        viewer.show();
        const QString dataDirectory = QStringLiteral(FIRECAE_TEST_DATA_DIR);
        const QString smvPath = QDir(dataDirectory).filePath(
            QStringLiteral("fds-results/demo.smv"));
        QString error;
        if (!viewer.openCase(smvPath, &error))
            return fail(QStringLiteral("P24 result case open failed: %1").arg(error)
                            .toLocal8Bit().constData());
        QApplication::processEvents();
        auto* tree = viewer.findChild<QTreeWidget*>(
            QStringLiteral("NativeResultObjectTree"));
        if (!tree || tree->topLevelItemCount() != 11)
            return fail("P24 semantic result tree does not expose all 11 categories.");
        const QStringList categories = {
            QStringLiteral("Geometry"), QStringLiteral("3D Smoke/Fire"),
            QStringLiteral("Slice"), QStringLiteral("Vector Slice"),
            QStringLiteral("Boundary"), QStringLiteral("Isosurface"),
            QStringLiteral("Particles"), QStringLiteral("Plot3D"),
            QStringLiteral("Devices"), QStringLiteral("CSV"), QStringLiteral("HVAC")};
        for (int row = 0; row < categories.size(); ++row) {
            if (tree->topLevelItem(row)->text(0) != categories.at(row))
                return fail("P24 semantic result tree category order is incorrect.");
        }
        const FdsResultScanResult vectorScan = FdsResultScanner().scanSmvFile(
            QDir(dataDirectory).filePath(QStringLiteral(
                "gui-generated/tunnel_smoke_10s/tunnel_smoke_10s.smv")));
        const bool hasVectorSlice = std::any_of(
            vectorScan.files.cbegin(), vectorScan.files.cend(),
            [](const FdsResultFileInfo& file) {
                return file.type == FcResultFileType::Slice && file.vectorField;
            });
        if (!vectorScan.success() || !hasVectorSlice)
            return fail("P24 VECTOR=.TRUE. slice classification failed.");
        if (!viewer.findChild<QCheckBox*>(QStringLiteral("NativeResultAutoRefreshCheck")) ||
            !viewer.findChild<QPushButton*>(QStringLiteral("NativeResultRefreshButton")) ||
            !viewer.findChild<QPushButton*>(QStringLiteral("NativeResultExportVideoButton")) ||
            !viewer.findChild<QDoubleSpinBox*>(QStringLiteral("NativeResultRangeStart")) ||
            !viewer.findChild<QCheckBox*>(QStringLiteral("NativeResultGeometryVisibleCheck")))
            return fail("P24 refresh, range, geometry, or video controls are missing.");

        QTemporaryDir comparisonDirectory;
        if (!comparisonDirectory.isValid()) return fail("P24 comparison temp directory failed.");
        const QString sourceDirectory = QDir(dataDirectory).filePath(QStringLiteral("fds-results"));
        const auto copy = [&](const QString& sourceName, const QString& targetName) {
            return QFile::copy(QDir(sourceDirectory).filePath(sourceName),
                               comparisonDirectory.filePath(targetName));
        };
        if (!copy(QStringLiteral("demo.smv"), QStringLiteral("secondary.smv")) ||
            !copy(QStringLiteral("demo.fds"), QStringLiteral("secondary.fds")) ||
            !copy(QStringLiteral("demo_devc.csv"), QStringLiteral("demo_devc.csv")) ||
            !copy(QStringLiteral("demo_hrr.csv"), QStringLiteral("demo_hrr.csv")) ||
            !copy(QStringLiteral("demo_0001.sf"), QStringLiteral("demo_0001.sf")))
            return fail("P24 comparison case fixture copy failed.");
        if (!viewer.addComparisonCase(comparisonDirectory.filePath(
                                          QStringLiteral("secondary.smv")),
                                      QStringLiteral("PyroSim: independent case"), &error))
            return fail(QStringLiteral("P24 comparison overlay failed: %1").arg(error)
                            .toLocal8Bit().constData());
        const QString csvPath = comparisonDirectory.filePath(QStringLiteral("overlay.csv"));
        if (!viewer.exportVisibleCsv(csvPath))
            return fail("P24 comparison overlay CSV export failed.");
        QFile csv(csvPath);
        if (!csv.open(QIODevice::ReadOnly) ||
            !QString::fromUtf8(csv.readAll()).contains(QStringLiteral("PyroSim")))
            return fail("P24 exported CSV does not include the comparison overlay provenance.");

        const QString videoPath = comparisonDirectory.filePath(QStringLiteral("result.avi"));
        if (!viewer.exportVideo(videoPath, 5, &error))
            return fail(QStringLiteral("P24 AVI video export failed: %1").arg(error)
                            .toLocal8Bit().constData());
        QFile video(videoPath);
        if (!video.open(QIODevice::ReadOnly) || video.read(4) != QByteArray("RIFF") ||
            video.size() < 1024)
            return fail("P24 video export is not a non-empty RIFF/AVI stream.");

        tree->setCurrentItem(tree->topLevelItem(0));
        QApplication::processEvents();
        QWidget* geometryCanvas = viewer.findChild<QWidget*>(
            QStringLiteral("NativeResultGeometryCanvas"));
        if (!geometryCanvas || !geometryCanvas->isVisible())
            return fail("P24 FireCAE native geometry display is unavailable.");
        QComboBox* scene = viewer.findChild<QComboBox*>(
            QStringLiteral("ResultSceneModeCombo"));
        if (!scene || scene->count() != 4) return fail("P24 geometry provenance modes are incomplete.");
        scene->setCurrentIndex(2);
        QApplication::processEvents();
        if (!viewer.saveScreenshot(comparisonDirectory.filePath(QStringLiteral("p24.png"))))
            return fail("P24 result workspace screenshot failed.");
        std::cout << "FireCAE P24 mature Results workspace smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a13-render-cache-smoke"))) {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        const QString smvPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral(
                "gui-generated/couch_smoke_12s/couch_smoke_12s.smv"));
        if (!window.openResultFile(smvPath))
            return fail("A13 couch Smoke3D case could not be loaded.");
        QAction* nativeAction = window.findChild<QAction*>(
            QStringLiteral("OpenNativeResultsAction"));
        nativeAction->trigger();
        QApplication::processEvents();
        auto* viewer = window.findChild<NativeResultViewerWidget*>(
            QStringLiteral("NativeResultViewerWidget"));
        auto* files = viewer ? viewer->findChild<QListWidget*>(
                                  QStringLiteral("NativeResultFileList")) : nullptr;
        const auto findFieldRow = [files](FcResultFileType type,
                                          const QString& quantity = {}) {
            if (!files) return -1;
            for (int row = 0; row < files->count(); ++row) {
                if (static_cast<FcResultFileType>(
                        files->item(row)->data(Qt::UserRole + 1).toInt()) ==
                        type &&
                    (quantity.isEmpty() ||
                     files->item(row)->data(Qt::UserRole + 2).toString() == quantity))
                    return row;
            }
            return -1;
        };
        const int smokeRow = findFieldRow(FcResultFileType::Smoke3D,
                                          QStringLiteral("SOOT DENSITY"));
        if (!viewer || !files || smokeRow < 0)
            return fail("A13 Smoke3D quantity metadata was not exposed in the native viewer.");
        QPushButton* render = viewer->findChild<QPushButton*>(
            QStringLiteral("NativeResultRenderFieldButton"));
        SmokeviewFrameRenderer* renderer = viewer->findChild<SmokeviewFrameRenderer*>();
        if (!render || !renderer)
            return fail("A13 binary field render-cache controls are unavailable.");
        const auto renderField = [&](int row) {
            if (row < 0) return false;
            files->setCurrentRow(row);
            QApplication::processEvents();
            if (!render->isEnabled()) return false;
            bool success = false;
            QStringList images;
            QEventLoop loop;
            QObject::connect(renderer, &SmokeviewFrameRenderer::finished, &loop,
                             [&](bool ok, const QStringList& rendered, const QString&) {
                                 success = ok; images = rendered; loop.quit();
                             });
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(60000);
            render->click();
            loop.exec();
            if (!success || images.isEmpty() ||
                !QFileInfo::exists(images.constFirst()))
                return false;
            // Smokeview's first frame is normally t=0 and contains only the
            // static geometry.  Move to the last cached frame so acceptance
            // evidence demonstrates the requested result field, not merely a
            // successfully opened case.
            if (auto* timeline = viewer->findChild<QSlider*>(
                    QStringLiteral("NativeResultTimeline"))) {
                timeline->setValue(timeline->maximum());
                QApplication::processEvents();
            }
            return true;
        };
        if (!renderField(smokeRow))
            return fail("A13 Smoke3D frame cache did not render real result frames.");
        QLabel* frame = viewer->findChild<QLabel*>(QStringLiteral("NativeResultRenderedFrame"));
        if (!frame || frame->pixmap(Qt::ReturnByValue).isNull())
            return fail("A13 rendered Smoke3D frame was not displayed in the native workspace.");
        if (!captureAcceptance(window, QStringLiteral("A13-02-smoke3d-frame.png")))
            return fail("A13 Smoke3D acceptance screenshot failed.");
        const int sliceRow = findFieldRow(FcResultFileType::Slice,
                                          QStringLiteral("TEMPERATURE"));
        files->setCurrentRow(sliceRow);
        QApplication::processEvents();
        const double initialZoom = frame->property("zoomFactor").toDouble();
        QWheelEvent zoomEvent(QPointF(frame->width() / 2.0, frame->height() / 2.0),
                              frame->mapToGlobal(frame->rect().center()),
                              QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(frame, &zoomEvent);
        QApplication::processEvents();
        if (sliceRow < 0 || frame->pixmap(Qt::ReturnByValue).isNull() ||
            frame->property("zoomFactor").toDouble() <= initialZoom)
            return fail("P31 native SLCF frame did not display or respond to wheel zoom.");
        if (files->item(sliceRow)->data(Qt::UserRole + 4).toString().isEmpty() ||
            !renderField(sliceRow))
            return fail("A13 temperature slice frame cache did not render.");
        if (!captureAcceptance(window, QStringLiteral("A13-03-temperature-slice.png")))
            return fail("A13 slice acceptance screenshot failed.");
        if (!renderField(findFieldRow(FcResultFileType::Boundary,
                                      QStringLiteral("WALL TEMPERATURE"))))
            return fail("A13 boundary frame cache did not render.");
        if (!captureAcceptance(window, QStringLiteral("A13-04-wall-boundary.png")))
            return fail("A13 boundary acceptance screenshot failed.");
        if (!renderField(findFieldRow(FcResultFileType::Particle)))
            return fail("A13 particle frame cache did not render.");
        if (!captureAcceptance(window, QStringLiteral("A13-05-particles.png")))
            return fail("A13 particle acceptance screenshot failed.");
        std::cout << "FireCAE A13 Smoke3D/slice/boundary/particle render-cache test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a13-native-results-smoke"))) {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QApplication::processEvents();
        const QString smvPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-results/demo.smv"));
        if (!window.openResultFile(smvPath))
            return fail("A13 result case could not be loaded.");
        QAction* nativeAction = window.findChild<QAction*>(
            QStringLiteral("OpenNativeResultsAction"));
        if (!nativeAction || !nativeAction->isEnabled())
            return fail("A13 native result viewer action is missing or disabled.");
        nativeAction->trigger();
        QApplication::processEvents();
        auto* viewer = window.findChild<NativeResultViewerWidget*>(
            QStringLiteral("NativeResultViewerWidget"));
        if (!viewer || !viewer->isVisible() || !viewer->hasOpenCase() ||
            viewer->frameCount() != 3 || viewer->csvSeriesCount() != 1)
            return fail("A13 native result viewer did not load CSV frames and quantities.");
        if (!captureAcceptance(window, QStringLiteral("A13-01-native-chart.png")))
            return fail("A13 chart acceptance screenshot failed.");
        if (!viewer->findChild<QSlider*>(QStringLiteral("NativeResultTimeline")) ||
            !viewer->findChild<QDoubleSpinBox*>(QStringLiteral("NativeResultTimeSpin")) ||
            !viewer->findChild<QPushButton*>(QStringLiteral("NativeResultPlayButton")) ||
            !viewer->findChild<QTableWidget*>(QStringLiteral("NativeResultStatisticsTable")) ||
            !viewer->findChild<QListWidget*>(QStringLiteral("NativeResultFileList")))
            return fail("A13 native result workspace controls are incomplete.");
        QTemporaryDir directory;
        if (!directory.isValid() ||
            !viewer->exportVisibleCsv(directory.filePath(QStringLiteral("visible.csv"))) ||
            !viewer->saveScreenshot(directory.filePath(QStringLiteral("native.png"))) ||
            QFileInfo(directory.filePath(QStringLiteral("visible.csv"))).size() <= 0 ||
            QFileInfo(directory.filePath(QStringLiteral("native.png"))).size() <= 0)
            return fail("A13 native CSV or PNG export failed.");
        QPushButton* play = viewer->findChild<QPushButton*>(
            QStringLiteral("NativeResultPlayButton"));
        play->click();
        QEventLoop loop;
        QTimer::singleShot(260, &loop, &QEventLoop::quit);
        loop.exec();
        if (viewer->currentTime() <= 0.0)
            return fail("A13 native timeline did not advance during playback.");
        std::cout << "FireCAE A13 native result viewer smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a12-scenario-smoke"))) {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        window.loadSimpleTestBenchmark();
        QApplication::processEvents();
        QAction* scenarios = window.findChild<QAction*>(QStringLiteral("ScenarioManagerAction"));
        QAction* batch = window.findChild<QAction*>(QStringLiteral("RunAllScenariosAction"));
        if (!scenarios || !batch) return fail("A12 scenario and batch actions are missing.");
        acceptNextNamedDialog(QStringLiteral("ScenarioManagerDialog"), [](QDialog* dialog) {
            auto* list = dialog->findChild<QListWidget*>(QStringLiteral("ScenarioList"));
            auto* chid = dialog->findChild<QLineEdit*>(QStringLiteral("ScenarioChidEdit"));
            auto* objects = dialog->findChild<QTreeWidget*>(
                QStringLiteral("ScenarioObjectTree"));
            auto* overrides = dialog->findChild<QTableWidget*>(
                QStringLiteral("ScenarioOverrideTable"));
            if (list && list->count() == 1 && chid && objects && overrides) {
                chid->setText(QStringLiteral("a12_gui_scenario"));
            }
        });
        scenarios->trigger();
        QTemporaryDir directory;
        const QString fdsPath = directory.filePath(QStringLiteral("a12_gui_scenario.fds"));
        if (!window.exportCurrentProjectToFds(fdsPath)) {
            return fail("A12 active scenario did not export.");
        }
        QFile file(fdsPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
            !file.readAll().contains("CHID='a12_gui_scenario'")) {
            return fail("A12 scenario CHID was not applied through the GUI.");
        }
        std::cout << "FireCAE A12 scenario GUI smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a10-workflow-smoke"))) {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QAction* fire = window.findChild<QAction*>(QStringLiteral("FireSourceWizardAction"));
        QAction* output = window.findChild<QAction*>(QStringLiteral("OutputWizardAction"));
        QAction* controls = window.findChild<QAction*>(QStringLiteral("ControlLogicGraphAction"));
        QAction* hvac = window.findChild<QAction*>(QStringLiteral("HvacNetworkGraphAction"));
        QAction* library = window.findChild<QAction*>(QStringLiteral("FdsPropertyLibraryAction"));
        QAction* meshAssistant = window.findChild<QAction*>(
            QStringLiteral("MeshEngineeringAction"));
        if (!fire || !output || !controls || !hvac || !library || !meshAssistant) {
            return fail("A10 professional assistant actions are incomplete.");
        }
        QAction* createBox = findAction(window, QStringLiteral("Create Box"));
        if (!createBox) return fail("A10 workflow could not create a fire host.");
        acceptNextCreateBoxDialog();
        createBox->trigger();
        acceptNextNamedDialog(QStringLiteral("FireSourceWizardDialog"), [](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("FireSourceHostCombo"));
            if (host && host->count() > 1) host->setCurrentIndex(1);
        });
        fire->trigger();
        acceptNextNamedDialog(QStringLiteral("OutputWizardDialog"));
        output->trigger();
        acceptNextNamedDialog(QStringLiteral("FdsPropertyLibraryDialog"));
        library->trigger();
        acceptNextNamedDialog(QStringLiteral("MeshEngineeringDialog"), [](QDialog* dialog) {
            if (auto* splitX = dialog->findChild<QSpinBox*>(
                    QStringLiteral("MeshEngineeringSplitXSpin"))) {
                splitX->setValue(2);
            }
        });
        meshAssistant->trigger();
        QApplication::processEvents();
        QTreeWidget* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!tree || tree->topLevelItemCount() != 1) {
            return fail("A10 workflow did not keep a project tree.");
        }
        QTreeWidgetItem* root = tree->topLevelItem(0);
        if (root->child(4)->childCount() != 1 ||
            root->child(5)->childCount() != 1 ||
            root->child(6)->childCount() != 1 ||
            root->child(10)->childCount() != 3 ||
            root->child(13)->childCount() != 1) {
            return fail("Fire/output wizards did not create the expected independent objects.");
        }
        if (root->child(1)->childCount() != 2) {
            return fail("Mesh engineering assistant did not create two aligned meshes.");
        }
        QTemporaryDir directory;
        const QString fdsPath = directory.filePath(QStringLiteral("a10_workflow.fds"));
        if (!window.exportCurrentProjectToFds(fdsPath)) {
            return fail("A10 wizard-created objects did not export to FDS.");
        }
        QFile fds(fdsPath);
        if (!fds.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("A10 workflow FDS output could not be read.");
        }
        const QByteArray text = fds.readAll();
        if (!text.contains("&REAC") || !text.contains("&SURF") ||
            !text.contains("RAMP_Q='FIRE_RAMP'") || !text.contains("&SLCF")) {
            return fail("A10 wizard-created FDS content is incomplete.");
        }
        inspectAndCloseNextGraph(QStringLiteral("ControlLogicGraphDialog"));
        controls->trigger();
        auto* inspected = reinterpret_cast<bool*>(
            qApp->property("firecaeGraphInspectedPointer").toULongLong());
        const bool controlInspected = inspected && *inspected;
        delete inspected;
        if (!controlInspected) return fail("Control graph did not render its graphics view.");
        inspectAndCloseNextGraph(QStringLiteral("HvacNetworkGraphDialog"));
        hvac->trigger();
        inspected = reinterpret_cast<bool*>(
            qApp->property("firecaeGraphInspectedPointer").toULongLong());
        const bool hvacInspected = inspected && *inspected;
        delete inspected;
        if (!hvacInspected) return fail("HVAC graph did not render its graphics view.");
        std::cout << "FireCAE A10 workflow assistant smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--mesh-editor-smoke"))) {
        FdsObjectEditorDialog meshEditor(nullptr, QStringLiteral("MESH"));
        auto* table = meshEditor.findChild<QTableWidget*>(
            QStringLiteral("FdsParameterTable"));
        auto* originX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshOriginXSpin"));
        auto* lengthX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshLengthXSpin"));
        auto* cellsX = meshEditor.findChild<QSpinBox*>(
            QStringLiteral("MeshCellCountXSpin"));
        auto* resolutionMode = meshEditor.findChild<QComboBox*>(
            QStringLiteral("MeshResolutionModeCombo"));
        auto* targetX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshTargetCellSizeXSpin"));
        auto* summary = meshEditor.findChild<QLabel*>(
            QStringLiteral("MeshSummaryLabel"));
        auto* schemaEditor = meshEditor.findChild<QWidget*>(
            QStringLiteral("FdsProfessionalSchemaEditor"));
        auto* schemaTabs = meshEditor.findChild<QTabWidget*>(
            QStringLiteral("FdsSchemaCategoryTabs"));
        auto* schemaVersion = meshEditor.findChild<QLabel*>(
            QStringLiteral("FdsSchemaVersionLabel"));
        if (!table || table->rowCount() != 2 || !originX || !lengthX ||
            !cellsX || !resolutionMode || !targetX || !summary ||
            !meshEditor.findChild<QWidget*>(QStringLiteral("MeshPreviewWidget")) ||
            !schemaEditor || !schemaTabs || schemaTabs->count() < 1 ||
            !schemaVersion || !schemaVersion->text().contains(QStringLiteral("6.11.1"))) {
            return fail("Specialized mesh editor controls are incomplete.");
        }
        originX->setValue(2.0);
        lengthX->setValue(8.0);
        cellsX->setValue(16);
        if (table->item(0, 2)->text() != QStringLiteral("16,20,12") ||
            table->item(1, 2)->text() != QStringLiteral("2,10,0,10,0,3")) {
            return fail("Mesh controls did not generate IJK/XB.");
        }
        resolutionMode->setCurrentIndex(1);
        targetX->setValue(0.25);
        if (cellsX->value() != 32 ||
            !summary->text().contains(QStringLiteral("IJK=32,20,12"))) {
            return fail("Target cell size did not calculate cell counts.");
        }
        table->item(1, 2)->setText(QStringLiteral("-1,3,4,8,0,2"));
        if (qAbs(originX->value() + 1.0) > 1.0e-9 ||
            qAbs(lengthX->value() - 4.0) > 1.0e-9) {
            return fail("Advanced XB did not update mesh controls.");
        }
        std::cout << "FireCAE specialized mesh editor smoke test passed.\n";
        return 0;
    }
    if (application.arguments().contains(QStringLiteral("--a09-building-smoke"))) {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QAction* projectSettingsAction =
            findAction(window, QStringLiteral("Project Settings..."));
        if (!projectSettingsAction) return fail("R01 project settings action is missing.");
        QTimer::singleShot(0, []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() != QStringLiteral("ProjectSettingsDialog")) continue;
                auto* dialog = qobject_cast<QDialog*>(widget);
                dialog->findChild<QLineEdit*>(QStringLiteral("ProjectNameEdit"))
                    ->setText(QStringLiteral("R01 Professional Building"));
                dialog->findChild<QLineEdit*>(QStringLiteral("ProjectChidEdit"))
                    ->setText(QStringLiteral("r01_building"));
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("ProjectEndTimeSpin"))
                    ->setValue(0.1);
                dialog->accept();
                return;
            }
        });
        projectSettingsAction->trigger();
        QApplication::processEvents();
        const QStringList actionNames = {
            QStringLiteral("Create Wall"), QStringLiteral("Draw Wall in View..."),
            QStringLiteral("Create Room"),
            QStringLiteral("Create Slab"), QStringLiteral("Create Roof"),
            QStringLiteral("Create Column"), QStringLiteral("Create Beam"),
            QStringLiteral("Create Polygon"), QStringLiteral("Create Polyline Sweep"),
            QStringLiteral("Create Circle / Cylinder"),
            QStringLiteral("Create Rectangle Profile"),
            QStringLiteral("Extrude 2D Profile"), QStringLiteral("Sweep Along Path"),
            QStringLiteral("Create Stair"), QStringLiteral("Create Ramp"),
            QStringLiteral("Create Rectangular Opening"),
            QStringLiteral("Create Polygonal Opening"), QStringLiteral("Create Door"),
            QStringLiteral("Create Window"), QStringLiteral("Create Slab Opening"),
            QStringLiteral("Create Wall Vent"),
            QStringLiteral("Assign Surfaces..."),
            QStringLiteral("Preview FDS Blocks..."),
            QStringLiteral("Generate FDS Blocks"),
            QStringLiteral("Boolean Union"), QStringLiteral("Boolean Difference"),
            QStringLiteral("Boolean Intersection"),
            QStringLiteral("Heal Selected Geometry")};
        for (const QString& name : actionNames) {
            if (!findAction(window, name)) return fail("A09 geometry action is missing.");
        }

        QAction* wallAction = findAction(window, QStringLiteral("Create Wall"));
        acceptNextBuildingDialog(QStringLiteral("Exterior Wall"));
        wallAction->trigger();
        QAction* slabAction = findAction(window, QStringLiteral("Create Slab"));
        acceptNextBuildingDialog(QStringLiteral("Ground Slab"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(0.2);
        });
        slabAction->trigger();
        QAction* doorAction = findAction(window, QStringLiteral("Create Door"));
        acceptNextBuildingDialog(QStringLiteral("Door 01"), [](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("OpeningHostCombo"));
            if (host && host->count() > 1) host->setCurrentIndex(1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(-0.1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(2.1);
        });
        doorAction->trigger();
        QAction* roomAction = findAction(window, QStringLiteral("Create Room"));
        acceptNextBuildingDialog(QStringLiteral("First Floor Room"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(10.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(8.0);
        });
        roomAction->trigger();
        acceptNextBuildingDialog(QStringLiteral("Second Floor Slab"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(3.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(10.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(8.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(0.2);
        });
        slabAction->trigger();
        acceptNextBuildingDialog(QStringLiteral("Second Floor Room"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(3.4);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(10.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(8.0);
        });
        roomAction->trigger();
        QAction* roofAction = findAction(window, QStringLiteral("Create Roof"));
        acceptNextBuildingDialog(QStringLiteral("Detached GEOM Roof"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(7.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(8.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(6.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(0.2);
            auto* route = dialog->findChild<QComboBox*>(
                QStringLiteral("FdsGeometryConversionRouteCombo"));
            if (route) route->setCurrentIndex(route->findData(QStringLiteral("GEOM")));
        });
        roofAction->trigger();
        QAction* stairAction = findAction(window, QStringLiteral("Create Stair"));
        acceptNextBuildingDialog(QStringLiteral("Main Stair"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(4.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(2.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(1.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(4.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingRiseSpin"))->setValue(3.0);
            dialog->findChild<QSpinBox*>(QStringLiteral("BuildingStepCountSpin"))->setValue(12);
        });
        stairAction->trigger();
        QAction* windowAction = findAction(window, QStringLiteral("Create Window"));
        acceptNextBuildingDialog(QStringLiteral("Window 01"), [](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("OpeningHostCombo"));
            if (host && host->count() > 1) host->setCurrentIndex(1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(2.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(-0.1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(1.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(1.5);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(1.2);
        });
        windowAction->trigger();
        QAction* wallVentAction = findAction(window, QStringLiteral("Create Wall Vent"));
        acceptNextBuildingDialog(QStringLiteral("Exhaust Vent"), [](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("OpeningHostCombo"));
            if (host && host->count() > 1) host->setCurrentIndex(1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(3.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(-0.1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(2.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(0.6);
        });
        wallVentAction->trigger();
        QAction* slabOpeningAction = findAction(window, QStringLiteral("Create Slab Opening"));
        acceptNextBuildingDialog(QStringLiteral("Stair Slab Opening"), [](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("OpeningHostCombo"));
            const int slabIndex = host ? host->findText(QStringLiteral("Ground Slab")) : -1;
            if (host && slabIndex >= 0) host->setCurrentIndex(slabIndex);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(4.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(2.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingBaseZSpin"))->setValue(0.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(1.4);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(1.4);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(0.2);
        });
        slabOpeningAction->trigger();
        QApplication::processEvents();

        QAction* deviceAction = findAction(window, QStringLiteral("Device"));
        acceptNextFdsDialog([](QDialog* dialog) {
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsObjectNameEdit"))
                ->setText(QStringLiteral("Door Temperature Device"));
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsIdEdit"))
                ->setText(QStringLiteral("DOOR_TEMP"));
        });
        deviceAction->trigger();
        QApplication::processEvents();
        QTreeWidget* dynamicTree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        QTreeWidgetItem* dynamicProject = dynamicTree->topLevelItem(0);
        const QString deviceUuid =
            dynamicProject->child(9)->child(0)->data(0, Qt::UserRole).toString();
        QAction* controlAction = findAction(window, QStringLiteral("Control"));
        acceptNextFdsDialog([deviceUuid](QDialog* dialog) {
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsObjectNameEdit"))
                ->setText(QStringLiteral("Door Release Control"));
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsIdEdit"))
                ->setText(QStringLiteral("DOOR_RELEASE"));
            auto* preset = dialog->findChild<QComboBox*>(
                QStringLiteral("FdsRecommendedParameterCombo"));
            auto* addRecommended = dialog->findChild<QPushButton*>(
                QStringLiteral("AddRecommendedFdsParameterButton"));
            const int inputPreset = preset ? preset->findData(QStringLiteral("INPUT_ID")) : -1;
            if (preset && addRecommended && inputPreset >= 0) {
                preset->setCurrentIndex(inputPreset);
                addRecommended->click();
            }
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("FdsParameterTable"));
            for (int row = 0; table && row < table->rowCount(); ++row) {
                if (table->item(row, 0) &&
                    table->item(row, 0)->text() == QStringLiteral("INPUT_ID")) {
                    table->item(row, 2)->setText(deviceUuid);
                    table->item(row, 2)->setData(Qt::UserRole,
                                                 QStringList{deviceUuid});
                }
            }
        });
        controlAction->trigger();
        QApplication::processEvents();
        dynamicTree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        dynamicProject = dynamicTree->topLevelItem(0);
        const QString controlUuid =
            dynamicProject->child(10)->child(0)->data(0, Qt::UserRole).toString();
        acceptNextBuildingDialog(QStringLiteral("Dynamic Door"), [controlUuid](QDialog* dialog) {
            auto* host = dialog->findChild<QComboBox*>(QStringLiteral("OpeningHostCombo"));
            if (host && host->count() > 1) host->setCurrentIndex(1);
            auto* control = dialog->findChild<QComboBox*>(QStringLiteral("OpeningControlCombo"));
            if (control) control->setCurrentIndex(control->findData(controlUuid));
            dialog->findChild<QCheckBox*>(QStringLiteral("DynamicOpeningCheck"))->setChecked(true);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartXSpin"))->setValue(3.7);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingStartYSpin"))->setValue(-0.1);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingWidthSpin"))->setValue(0.6);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingDepthSpin"))->setValue(0.2);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingHeightSpin"))->setValue(2.1);
        });
        doorAction->trigger();
        QApplication::processEvents();

        QAction* drawWallAction = findAction(window, QStringLiteral("Draw Wall in View..."));
        acceptNextWallSketchDialog(QStringLiteral("Sketch Wall"));
        drawWallAction->trigger();
        QApplication::processEvents();
        OccViewWidget* sketchView = window.findChild<OccViewWidget*>(
            QStringLiteral("Model3DView"));
        if (!sketchView || !sketchView->isWallSketchActive()) {
            return fail("A09 interactive wall tool did not enter two-point sketch mode.");
        }
        const QPoint firstPoint(sketchView->width() / 2 - 100,
                                sketchView->height() / 2);
        const QPoint secondPoint(sketchView->width() / 2 + 100,
                                 sketchView->height() / 2);
        QMouseEvent firstClick(QEvent::MouseButtonPress, QPointF(firstPoint),
                               QPointF(sketchView->mapToGlobal(firstPoint)),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sketchView, &firstClick);
        QMouseEvent previewMove(QEvent::MouseMove, QPointF(secondPoint),
                                QPointF(sketchView->mapToGlobal(secondPoint)),
                                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(sketchView, &previewMove);
        QMouseEvent secondClick(QEvent::MouseButtonPress, QPointF(secondPoint),
                                QPointF(sketchView->mapToGlobal(secondPoint)),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sketchView, &secondClick);
        QApplication::processEvents();
        if (sketchView->isWallSketchActive()) {
            return fail("A09 interactive wall tool did not finish after two picked points.");
        }

        acceptNextWallSketchDialog(QStringLiteral("Continuous Wall"), [](QDialog* dialog) {
            if (auto* continuous = dialog->findChild<QCheckBox*>(
                    QStringLiteral("WallSketchContinuousCheck"))) {
                continuous->setChecked(true);
            }
        });
        drawWallAction->trigger();
        QApplication::processEvents();
        if (!sketchView->isWallSketchActive()) {
            return fail("R01 continuous wall tool did not enter sketch mode.");
        }
        const QPoint thirdPoint(sketchView->width() / 2 + 100,
                                sketchView->height() / 2 + 100);
        QMouseEvent continuousFirst(QEvent::MouseButtonPress, QPointF(firstPoint),
                                    QPointF(sketchView->mapToGlobal(firstPoint)),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent continuousSecond(QEvent::MouseButtonPress, QPointF(secondPoint),
                                     QPointF(sketchView->mapToGlobal(secondPoint)),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent continuousThird(QEvent::MouseButtonPress, QPointF(thirdPoint),
                                    QPointF(sketchView->mapToGlobal(thirdPoint)),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(sketchView, &continuousFirst);
        QApplication::sendEvent(sketchView, &continuousSecond);
        QApplication::sendEvent(sketchView, &continuousThird);
        QApplication::processEvents();
        if (!sketchView->isWallSketchActive()) {
            return fail("R01 continuous wall tool stopped before Escape.");
        }
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(sketchView, &escape);
        QApplication::processEvents();
        if (sketchView->isWallSketchActive()) {
            return fail("R01 continuous wall tool did not finish on Escape.");
        }

        QTreeWidget* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!tree || tree->topLevelItemCount() != 1 ||
            tree->topLevelItem(0)->child(0)->childCount() != 15) {
            return fail("A09 building dialogs did not create the two-floor source building through the object tree.");
        }
        QTreeWidgetItem* projectItem = tree->topLevelItem(0);
        selectItem(tree, projectItem->child(0)->child(0));
        acceptNextNamedDialog(QStringLiteral("EditBuildingElementDialog"), [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("BuildingEndXSpin"))
                ->setValue(4.5);
        });
        QAction* editBuildingAction = findAction(window, QStringLiteral("Edit Selected Object..."));
        if (!editBuildingAction || !editBuildingAction->isEnabled()) {
            return fail("R01 wall endpoint editor is unavailable for the selected wall.");
        }
        editBuildingAction->trigger();
        QApplication::processEvents();
        findActionByShortcut(window, QKeySequence::Undo)->trigger();
        findActionByShortcut(window, QKeySequence::Redo)->trigger();
        QApplication::processEvents();
        QAction* surfaceAction = findAction(window, QStringLiteral("Surface"));
        acceptNextFdsDialog([](QDialog* dialog) {
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsObjectNameEdit"))
                ->setText(QStringLiteral("R01 Blue Surface"));
            dialog->findChild<QLineEdit*>(QStringLiteral("FdsIdEdit"))
                ->setText(QStringLiteral("R01_BLUE"));
        });
        surfaceAction->trigger();
        QApplication::processEvents();
        projectItem = tree->topLevelItem(0);
        if (!projectItem || projectItem->child(5)->childCount() != 1) {
            return fail("R01 GUI did not create the surface used by batch assignment.");
        }
        const QString assignedSurfaceUuid =
            projectItem->child(5)->child(0)->data(0, Qt::UserRole).toString();
        const QString surfaceCopySourceUuid =
            projectItem->child(0)->child(0)->data(0, Qt::UserRole).toString();
        tree->clearSelection();
        tree->setCurrentItem(projectItem->child(0)->child(0));
        projectItem->child(0)->child(0)->setSelected(true);
        projectItem->child(0)->child(1)->setSelected(true);
        QApplication::processEvents();
        QAction* assignSurfaceAction = findAction(window, QStringLiteral("Assign Surfaces..."));
        if (!assignSurfaceAction || !assignSurfaceAction->isEnabled()) {
            return fail("R01 batch surface assignment is not enabled for multi-selected geometry.");
        }
        acceptNextNamedDialog(QStringLiteral("SurfaceAssignmentDialog"),
                              [assignedSurfaceUuid](QDialog* dialog) {
            auto* defaultSurface = dialog->findChild<QComboBox*>(
                QStringLiteral("BatchDefaultSurfaceCombo"));
            auto* faceChoice = dialog->findChild<QComboBox*>(
                QStringLiteral("BatchFaceChoiceCombo"));
            auto* faceSurface = dialog->findChild<QComboBox*>(
                QStringLiteral("BatchFaceSurfaceCombo"));
            defaultSurface->setCurrentIndex(defaultSurface->findData(assignedSurfaceUuid));
            faceChoice->setCurrentIndex(faceChoice->findData(
                QStringLiteral("__ALL_TOPOLOGY__")));
            faceSurface->setCurrentIndex(faceSurface->findData(assignedSurfaceUuid));
        });
        assignSurfaceAction->trigger();
        QApplication::processEvents();
        projectItem = tree->topLevelItem(0);
        tree->clearSelection();
        tree->setCurrentItem(projectItem->child(0)->child(2));
        projectItem->child(0)->child(2)->setSelected(true);
        QApplication::processEvents();
        acceptNextNamedDialog(QStringLiteral("SurfaceAssignmentDialog"),
                              [surfaceCopySourceUuid](QDialog* dialog) {
            auto* mode = dialog->findChild<QComboBox*>(
                QStringLiteral("SurfaceAssignmentModeCombo"));
            mode->setCurrentIndex(mode->findData(QStringLiteral("copy")));
            auto* source = dialog->findChild<QComboBox*>(
                QStringLiteral("SurfaceCopySourceCombo"));
            source->setCurrentIndex(source->findData(surfaceCopySourceUuid));
        });
        assignSurfaceAction->trigger();
        QApplication::processEvents();
        projectItem = tree->topLevelItem(0);
        tree->clearSelection();
        tree->setCurrentItem(projectItem->child(0)->child(2));
        projectItem->child(0)->child(2)->setSelected(true);
        QApplication::processEvents();
        acceptNextNamedDialog(QStringLiteral("SurfaceAssignmentDialog"),
                              [](QDialog* dialog) {
            auto* defaultSurface = dialog->findChild<QComboBox*>(
                QStringLiteral("BatchDefaultSurfaceCombo"));
            defaultSurface->setCurrentIndex(defaultSurface->findData(QString()));
            auto* faceChoice = dialog->findChild<QComboBox*>(
                QStringLiteral("BatchFaceChoiceCombo"));
            faceChoice->setCurrentIndex(faceChoice->findData(
                QStringLiteral("__CLEAR_ALL__")));
        });
        assignSurfaceAction->trigger();
        QApplication::processEvents();
        findActionByShortcut(window, QKeySequence::Undo)->trigger();
        QApplication::processEvents();
        QAction* meshAction = findAction(window, QStringLiteral("Mesh"));
        acceptNextFdsDialog([](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshOriginXSpin"))->setValue(-1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshOriginYSpin"))->setValue(-1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshOriginZSpin"))->setValue(-1.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshLengthXSpin"))->setValue(14.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshLengthYSpin"))->setValue(12.0);
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("MeshLengthZSpin"))->setValue(10.0);
            dialog->findChild<QSpinBox*>(QStringLiteral("MeshCellCountXSpin"))->setValue(70);
            dialog->findChild<QSpinBox*>(QStringLiteral("MeshCellCountYSpin"))->setValue(60);
            dialog->findChild<QSpinBox*>(QStringLiteral("MeshCellCountZSpin"))->setValue(50);
        });
        meshAction->trigger();
        QApplication::processEvents();
        if (tree->topLevelItem(0)->child(1)->childCount() != 1) {
            return fail("A09 GUI did not create a mesh for block preview.");
        }
        if (!captureAcceptance(window, QStringLiteral("A09-01-building-source.png"))) {
            return fail("A09 source-building screenshot failed.");
        }

        QAction* previewAction = findAction(window, QStringLiteral("Preview FDS Blocks..."));
        closeNextFdsPreviewDialog();
        previewAction->trigger();
        QApplication::processEvents();
        if (!captureAcceptance(window, QStringLiteral("A09-02-fds-block-preview.png"))) {
            return fail("A09 FDS block preview screenshot failed.");
        }

        findAction(window, QStringLiteral("Generate FDS Blocks"))->trigger();
        QApplication::processEvents();
        if (tree->topLevelItem(0)->child(0)->childCount() != 46 ||
            tree->topLevelItem(0)->child(8)->childCount() != 1) {
            return fail("A09 generated FDS OBST/HOLE records are missing from the GUI tree.");
        }
        QTemporaryDir directory;
        const QString artifactDirectory =
            qEnvironmentVariable("FIRECAE_R01_ARTIFACT_DIR");
        if (!artifactDirectory.isEmpty() && !QDir().mkpath(artifactDirectory)) {
            return fail("R01 artifact directory could not be created.");
        }
        const QString fdsPath = artifactDirectory.isEmpty()
                                    ? directory.filePath(QStringLiteral("a09_building.fds"))
                                    : QDir(artifactDirectory).filePath(
                                          QStringLiteral("r01_building.fds"));
        if (!window.exportCurrentProjectToFds(fdsPath)) {
            const auto logs = window.findChildren<QPlainTextEdit*>();
            for (QPlainTextEdit* log : logs) {
                if (log && !log->toPlainText().isEmpty()) {
                    std::cerr << log->toPlainText().toStdString() << '\n';
                }
            }
            return fail("A09 generated building did not pass validation and export to FDS.");
        }
        QFile fdsFile(fdsPath);
        if (!fdsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("A09 exported FDS file could not be inspected.");
        }
        const QString fdsText = QString::fromUtf8(fdsFile.readAll());
        if (!fdsText.contains(QStringLiteral("&MESH")) ||
            !fdsText.contains(QStringLiteral("&OBST")) ||
            !fdsText.contains(QStringLiteral("&HOLE")) ||
            !fdsText.contains(QStringLiteral("&VENT")) ||
            !fdsText.contains(QStringLiteral("&GEOM")) ||
            !fdsText.contains(QStringLiteral("CTRL_ID='DOOR_RELEASE'")) ||
            !fdsText.contains(QStringLiteral("VERTS=")) ||
            !fdsText.contains(QStringLiteral("FACES="))) {
            return fail("R01 export is missing MESH/OBST/HOLE/VENT or native GEOM records.");
        }
        const QString projectPath = artifactDirectory.isEmpty()
                                        ? directory.filePath(QStringLiteral("a09_building.firecae"))
                                        : QDir(artifactDirectory).filePath(
                                              QStringLiteral("r01_building.firecae"));
        if (!window.saveProjectFile(projectPath) || !window.openProjectFile(projectPath)) {
            return fail("A09 building project did not save and reopen.");
        }
        QFile savedProject(projectPath);
        if (!savedProject.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail("R01 saved project could not be inspected for surface UUIDs.");
        }
        const QString savedProjectText = QString::fromUtf8(savedProject.readAll());
        const QString defaultSurfaceEntry =
            QStringLiteral("\"defaultSurfaceUuid\": \"%1\"")
                .arg(assignedSurfaceUuid);
        if (savedProjectText.count(defaultSurfaceEntry) < 3 ||
            !savedProjectText.contains(QStringLiteral("TopoFace:")) ||
            !savedProjectText.contains(QStringLiteral("connectedStartWallUuid")) ||
            !savedProjectText.contains(QStringLiteral("\"endX\": 4.5"))) {
            return fail("R01 batch/copy/inherited surface UUID assignments did not persist through Undo and reopen.");
        }
        QApplication::processEvents();
        tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
        if (!tree || tree->topLevelItem(0)->child(0)->childCount() != 46 ||
            tree->topLevelItem(0)->child(8)->childCount() != 1 ||
            !captureAcceptance(window, QStringLiteral("A09-03-saved-reopened.png"))) {
            return fail("A09 save/reopen did not preserve building and converted FDS blocks.");
        }
        std::cout << "FireCAE A09 building GUI smoke test passed.\n";
        return 0;
    }
    // The scenario intentionally creates hundreds of Qt/OCCT temporaries.  Keep
    // the long-lived top-level window off that monolithic stack frame and ensure
    // it is destroyed before QApplication, matching normal Qt ownership order.
    auto windowOwner = std::make_unique<MainWindow>();
    MainWindow& window = *windowOwner;
    window.show();
    QApplication::processEvents();

    {
        // Keep this short-lived editor inside its own lifetime.  Leaving a hidden
        // top-level QWidget on the stack for the entire long-running UI scenario
        // lets Qt's native top-level bookkeeping outlive unrelated nested dialogs
        // and triggers MSVC's stack guard during process teardown.
        FdsObjectEditorDialog meshEditor(nullptr, QStringLiteral("MESH"));
        QTableWidget* meshParameterTable = meshEditor.findChild<QTableWidget*>(
            QStringLiteral("FdsParameterTable"));
        if (!meshParameterTable || meshParameterTable->rowCount() != 2 ||
            !meshParameterTable->item(0, 0) ||
            meshParameterTable->item(0, 0)->text() != QStringLiteral("IJK") ||
            !meshParameterTable->item(1, 0) ||
            meshParameterTable->item(1, 0)->text() != QStringLiteral("XB")) {
            return fail("The MESH editor did not provide editable IJK/XB defaults.");
        }
        if (!meshEditor.findChild<QLabel*>(QStringLiteral("FdsSpecializedEditorHint")) ||
            !meshEditor.findChild<QComboBox*>(
                QStringLiteral("FdsRecommendedParameterCombo")) ||
            !meshEditor.findChild<QPushButton*>(
                QStringLiteral("AddRecommendedFdsParameterButton")) ||
            !meshEditor.findChild<QPushButton*>(
                QStringLiteral("MoveFdsParameterUpButton")) ||
            !meshEditor.findChild<QPushButton*>(
                QStringLiteral("MoveFdsParameterDownButton"))) {
            return fail("The assisted FDS parameter editor controls were not created.");
        }
        auto* originX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshOriginXSpin"));
        auto* lengthX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshLengthXSpin"));
        auto* cellsX = meshEditor.findChild<QSpinBox*>(
            QStringLiteral("MeshCellCountXSpin"));
        auto* resolutionMode = meshEditor.findChild<QComboBox*>(
            QStringLiteral("MeshResolutionModeCombo"));
        auto* targetX = meshEditor.findChild<QDoubleSpinBox*>(
            QStringLiteral("MeshTargetCellSizeXSpin"));
        auto* summary = meshEditor.findChild<QLabel*>(
            QStringLiteral("MeshSummaryLabel"));
        if (!originX || !lengthX || !cellsX || !resolutionMode || !targetX ||
            !summary || !meshEditor.findChild<QWidget*>(
                            QStringLiteral("MeshPreviewWidget"))) {
            return fail("The specialized mesh position/resolution editor was not created.");
        }
        originX->setValue(2.0);
        lengthX->setValue(8.0);
        cellsX->setValue(16);
        if (meshParameterTable->item(0, 2)->text() != QStringLiteral("16,20,12") ||
            meshParameterTable->item(1, 2)->text() !=
                QStringLiteral("2,10,0,10,0,3")) {
            return fail("Mesh controls did not generate synchronized IJK/XB values.");
        }
        resolutionMode->setCurrentIndex(1);
        targetX->setValue(0.25);
        if (cellsX->value() != 32 ||
            !summary->text().contains(QStringLiteral("IJK=32,20,12"))) {
            return fail("Target mesh cell size did not calculate the required cell count.");
        }
        meshParameterTable->item(1, 2)->setText(QStringLiteral("-1,3,4,8,0,2"));
        if (qAbs(originX->value() + 1.0) > 1.0e-9 ||
            qAbs(lengthX->value() - 4.0) > 1.0e-9) {
            return fail("Advanced XB editing did not update the mesh controls.");
        }
    }
    if (!findAction(window, QStringLiteral("Add FDS Object...")) ||
        !findAction(window, QStringLiteral("Validate Model")) ||
        !findAction(window, QStringLiteral("Run Current Project"))) {
        return fail("The end-to-end modeling actions were not created.");
    }

    if (window.width() < window.minimumWidth() ||
        window.height() < window.minimumHeight()) {
        return fail("The initial window is smaller than its usable minimum size.");
    }
    if (QScreen* screen = window.screen()) {
        const QRect available = screen->availableGeometry();
        if (window.width() > available.width() || window.height() > available.height()) {
            return fail("The initial window exceeds the available desktop area.");
        }
    }

    QAction* runFdsAction =
        window.findChild<QAction*>(QStringLiteral("RunFdsAction"));
    QAction* runFdsParallelAction =
        window.findChild<QAction*>(QStringLiteral("RunFdsParallelAction"));
    QAction* stopFdsAction =
        window.findChild<QAction*>(QStringLiteral("StopFdsAction"));
    if (!runFdsAction || !runFdsParallelAction || !stopFdsAction ||
        !runFdsAction->isEnabled() || !runFdsParallelAction->isEnabled() ||
        stopFdsAction->isEnabled() ||
        runFdsParallelAction->text() !=
            QStringLiteral("Run Current Project with CPU/MPI...")) {
        return fail("FDS run actions were not initialized correctly.");
    }

    OccViewWidget* occView = window.findChild<OccViewWidget*>(
        QStringLiteral("Model3DView"));
    if (!occView || !occView->isInitialized() || !occView->displayManager()) {
        return fail("OpenCascade viewer did not initialize.");
    }

    const QPointF emptyViewPoint(occView->width() / 2.0,
                                 occView->height() / 2.0);
    const QPointF emptyViewGlobalPoint =
        occView->mapToGlobal(emptyViewPoint.toPoint());
    QMouseEvent emptyViewPress(QEvent::MouseButtonPress,
                               emptyViewPoint,
                               emptyViewPoint,
                               emptyViewGlobalPoint,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
    QApplication::sendEvent(occView, &emptyViewPress);
    QMouseEvent emptyViewRelease(QEvent::MouseButtonRelease,
                                 emptyViewPoint,
                                 emptyViewPoint,
                                 emptyViewGlobalPoint,
                                 Qt::LeftButton,
                                 Qt::NoButton,
                                 Qt::NoModifier);
    QApplication::sendEvent(occView, &emptyViewRelease);
    QApplication::processEvents();
    if (!occView->displayManager()->selectedObjectId().isEmpty()) {
        return fail("Clicking the empty 3D view did not keep the selection clear.");
    }

    QDockWidget* modelTreeDock = window.findChild<QDockWidget*>(
        QStringLiteral("ModelTreeDock"));
    QDockWidget* propertiesDock = window.findChild<QDockWidget*>(
        QStringLiteral("PropertiesDock"));
    if (!modelTreeDock || !propertiesDock || modelTreeDock->width() < 190 ||
        propertiesDock->width() < 190 || modelTreeDock->width() > 300 ||
        propertiesDock->width() > 300) {
        return fail("The default left dock width is outside its responsive range.");
    }

    SmokeviewHostWidget* smokeviewHost = window.findChild<SmokeviewHostWidget*>(
        QStringLiteral("SmokeviewHostWidget"));
    if (!smokeviewHost ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewFirstButton")) ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewPreviousButton")) ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewPlayButton")) ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewNextButton")) ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewZoomInButton")) ||
        !smokeviewHost->findChild<QPushButton*>(QStringLiteral("SmokeviewZoomOutButton"))) {
        return fail("Embedded Smokeview playback and zoom controls were not created.");
    }
    QMenu* resultsMenu = nullptr;
    for (QMenu* menu : window.findChildren<QMenu*>()) {
        if (menu->title() == QStringLiteral("Results")) {
            resultsMenu = menu;
            break;
        }
    }
    QStringList resultActions;
    if (resultsMenu) {
        for (QAction* action : resultsMenu->actions()) {
            if (action && !action->isSeparator()) resultActions.append(action->text());
        }
    }
    QAction* startPageAction = window.findChild<QAction*>(
        QStringLiteral("StartPageAction"));
    if (!resultsMenu || !resultActions.contains(QStringLiteral("Open in Smokeview")) ||
        resultActions.contains(QStringLiteral("Open in Native Result Viewer")) ||
        resultActions.contains(QStringLiteral("Open Smoke/Fire Animation")) ||
        resultActions.contains(QStringLiteral("Open Slice Animation")) ||
        resultActions.contains(QStringLiteral("Open Particle Animation")) ||
        !startPageAction || startPageAction->isVisible()) {
        return fail("Results/start-page menus were not simplified to the Smokeview workflow.");
    }
    if (!window.findChild<QDockWidget*>(QStringLiteral("SimulationTaskCenterDock")) ||
        !window.findChild<QTableWidget*>(QStringLiteral("SimulationTaskTable")) ||
        !window.findChild<QPushButton*>(QStringLiteral("SimulationTaskCancelButton")) ||
        !window.findChild<QPushButton*>(QStringLiteral("SimulationTaskRetryButton"))) {
        return fail("The A11 simulation task center is incomplete.");
    }

    QTreeWidget* tree = window.findChild<QTreeWidget*>(QStringLiteral("ModelTreeObjectTree"));
    if (!tree || tree->topLevelItemCount() != 1) {
        return fail("Project root was not created.");
    }
    if (tree->indentation() != 12 || tree->textElideMode() != Qt::ElideMiddle ||
        tree->horizontalScrollBarPolicy() != Qt::ScrollBarAsNeeded ||
        !window.findChild<QToolButton*>(QStringLiteral("ModelTreeCollapseAllButton")) ||
        !window.findChild<QToolButton*>(QStringLiteral("ModelTreeLocateSelectionButton"))) {
        return fail("The large-model tree navigation layout is incomplete.");
    }

    QTreeWidgetItem* projectItem = tree->topLevelItem(0);
    const QStringList groups = {QStringLiteral("Geometry"),
                                QStringLiteral("Meshes"),
                                QStringLiteral("Configuration"),
                                QStringLiteral("Species"),
                                QStringLiteral("Materials"),
                                QStringLiteral("Surfaces"),
                                QStringLiteral("Reactions"),
                                QStringLiteral("Particles"),
                                QStringLiteral("Vents"),
                                QStringLiteral("Devices"),
                                QStringLiteral("Controls"),
                                QStringLiteral("HVAC"),
                                QStringLiteral("Initial Conditions"),
                                QStringLiteral("Outputs"),
                                QStringLiteral("Results")};
    if (projectItem->text(0) != QStringLiteral("Untitled") ||
        projectItem->childCount() != groups.size()) {
        return fail("The standard project tree is incorrect.");
    }

    if (!selectItem(tree, projectItem) ||
        !hasVisibleLabel(window, QStringLiteral("Project"))) {
        return fail("Project properties did not update.");
    }
    if (!selectItem(tree, projectItem->child(0)) ||
        !hasVisibleLabel(window, QStringLiteral("Group")) ||
        !hasVisibleLabel(window, QStringLiteral("0"))) {
        return fail("Geometry group properties did not update.");
    }

    QAction* createBoxAction = findAction(window, QStringLiteral("Create Box"));
    if (!createBoxAction) {
        return fail("Create Box action was not found in the Geometry menu.");
    }
    QToolBar* mainToolBar =
        window.findChild<QToolBar*>(QStringLiteral("MainToolbar"));
    if (!mainToolBar || mainToolBar->actions().contains(createBoxAction)) {
        return fail("Create Box should not occupy the main toolbar.");
    }
    acceptNextCreateBoxDialog();
    createBoxAction->trigger();
    acceptNextCreateBoxDialog();
    createBoxAction->trigger();
    QApplication::processEvents();

    projectItem = tree->topLevelItem(0);
    QTreeWidgetItem* geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 2 ||
        geometryItem->child(0)->text(0) != QStringLiteral("Box_001") ||
        geometryItem->child(1)->text(0) != QStringLiteral("Box_002") ||
        occView->displayManager()->displayedObjectCount() != 2 ||
        window.statusBar()->currentMessage() != QStringLiteral("Created Box_002")) {
        return fail("Test boxes did not flow through model, tree, and viewer.");
    }
    if (!captureAcceptance(window, QStringLiteral("A08-01-created-boxes.png"))) {
        return fail("A08 created-boxes acceptance screenshot could not be saved.");
    }

    const QStringList rectangleSelection = occView->selectRectangle(
        QRect(QPoint(0, 0), occView->size()).adjusted(1, 1, -1, -1));
    QApplication::processEvents();
    if (rectangleSelection.size() != 2 ||
        occView->displayManager()->selectedObjectIds().size() != 2) {
        return fail("A08 rectangle selection did not select both visible boxes.");
    }
    if (tree->selectionMode() != QAbstractItemView::ExtendedSelection) {
        return fail("A08 multi-selection mode was not enabled in the model tree.");
    }
    tree->clearSelection();
    tree->setCurrentItem(geometryItem->child(1));
    geometryItem->child(0)->setSelected(true);
    geometryItem->child(1)->setSelected(true);
    QApplication::processEvents();
    if (occView->displayManager()->selectedObjectIds().size() != 2 ||
        !window.statusBar()->currentMessage().contains(QStringLiteral("2 object(s) selected"))) {
        return fail("A08 tree multi-selection did not synchronize to the 3D UUID selection.");
    }
    if (!captureAcceptance(window, QStringLiteral("A08-02-multi-selection.png"))) {
        return fail("A08 multi-selection acceptance screenshot could not be saved.");
    }
    QAction* undoAction = findActionByShortcut(window, QKeySequence::Undo);
    QAction* redoAction = findActionByShortcut(window, QKeySequence::Redo);
    if (!undoAction || !redoAction || !undoAction->isEnabled()) {
        return fail("A08 real Undo/Redo actions were not connected to the command stack.");
    }
    undoAction->trigger();
    QApplication::processEvents();
    if (tree->topLevelItem(0)->child(0)->childCount() != 1 ||
        occView->displayManager()->displayedObjectCount() != 1 || !redoAction->isEnabled()) {
        return fail("A08 Undo did not remove the most recently created box.");
    }
    redoAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 2) {
        return fail("A08 Redo did not restore the box with its presentation.");
    }
    QAction* lockAction = findAction(window, QStringLiteral("Lock Selected"));
    QAction* guardedTransformAction =
        findAction(window, QStringLiteral("Transform Selected..."));
    QAction* guardedDeleteAction = findAction(window, QStringLiteral("Delete"));
    if (!lockAction || !guardedTransformAction || !guardedDeleteAction) {
        return fail("A08 lock regression actions were not available.");
    }
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (!selectItem(tree, geometryItem) || !lockAction->isEnabled()) {
        return fail("A08 Geometry group could not be selected for hierarchy locking.");
    }
    lockAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (!selectItem(tree, geometryItem->child(0)) ||
        guardedTransformAction->isEnabled() || guardedDeleteAction->isEnabled()) {
        return fail("A08 locked parent did not protect its geometry descendants.");
    }
    undoAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (!selectItem(tree, geometryItem->child(0)) ||
        !guardedTransformAction->isEnabled() || !guardedDeleteAction->isEnabled()) {
        return fail("A08 Undo did not restore descendant modification after unlocking.");
    }
    selectItem(tree, geometryItem->child(0));
    QAction* gizmoAction = findAction(window, QStringLiteral("Transform Gizmo"));
    QLabel* snapStatus = window.findChild<QLabel*>(QStringLiteral("SnapStatusLabel"));
    QAction* snapEnabledAction = findAction(window, QStringLiteral("Snap Enabled"));
    QAction* snapSettingsAction = findAction(window, QStringLiteral("Snap Settings..."));
    if (!gizmoAction || !gizmoAction->isEnabled() || !snapStatus ||
        !snapStatus->text().contains(QStringLiteral("Snap")) ||
        !snapEnabledAction || !snapSettingsAction) {
        return fail("A08 transform gizmo or snap controls were not available.");
    }
    snapEnabledAction->setChecked(false);
    if (!snapStatus->text().contains(QStringLiteral("Off"))) {
        return fail("A08 temporary snap suppression did not update the status bar.");
    }
    snapEnabledAction->setChecked(true);
    if (!qEnvironmentVariableIsSet("FIRECAE_SKIP_GIZMO_TEST")) {
        gizmoAction->setChecked(true);
        QApplication::processEvents();
        if (!occView->isTransformManipulatorVisible() ||
            !captureAcceptance(window, QStringLiteral("A08-03-transform-gizmo.png"))) {
            return fail("A08 transform gizmo did not attach visibly to selected geometry.");
        }
        gizmoAction->setChecked(false);
    }
    QAction* transformAction = findAction(window, QStringLiteral("Transform Selected..."));
    if (!transformAction || !transformAction->isEnabled()) {
        return fail("A08 geometry transform action was not enabled for a selected box.");
    }
    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || dialog->objectName() != QStringLiteral("GeometryTransformDialog")) continue;
            const auto values = dialog->findChildren<QDoubleSpinBox*>();
            if (!values.isEmpty()) values.constFirst()->setValue(2.0);
            if (values.size() > 5) values.at(5)->setValue(17.0);
            dialog->accept();
            return;
        }
    });
    transformAction->trigger();
    QApplication::processEvents();
    if (!undoAction->isEnabled() ||
        !undoAction->text().contains(QStringLiteral("Transform"))) {
        return fail("A08 exact transform was not added to Undo/Redo.");
    }
    undoAction->trigger();
    redoAction->trigger();
    QApplication::processEvents();

    // Undo/redo rebuilds the UUID-driven tree, so old QTreeWidgetItem pointers
    // are invalid by design. Resolve the items again before continuing.
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (!captureAcceptance(window, QStringLiteral("A08-03-moved-rotated-snapped.png"))) {
        return fail("A08 transformed acceptance screenshot could not be saved.");
    }

    selectItem(tree, geometryItem->child(0));
    QAction* copyMoveAction = findAction(window, QStringLiteral("Copy and Move..."));
    if (!copyMoveAction || !copyMoveAction->isEnabled()) {
        return fail("A08 Copy and Move action was not enabled.");
    }
    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || dialog->objectName() != QStringLiteral("GeometryTransformDialog")) continue;
            const auto values = dialog->findChildren<QDoubleSpinBox*>();
            if (!values.isEmpty()) values.constFirst()->setValue(3.0);
            dialog->accept();
            return;
        }
    });
    copyMoveAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 3 ||
        occView->displayManager()->displayedObjectCount() != 3) {
        return fail("A08 Copy and Move did not create a UUID-backed geometry copy.");
    }

    tree->clearSelection();
    tree->setCurrentItem(geometryItem->child(1));
    geometryItem->child(0)->setSelected(true);
    geometryItem->child(1)->setSelected(true);
    QApplication::processEvents();
    QAction* groupAction = findAction(window, QStringLiteral("Group Selected"));
    if (!groupAction || !groupAction->isEnabled()) {
        return fail("A08 Group Selected action was not enabled.");
    }
    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QInputDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            dialog->setTextValue(QStringLiteral("A08_Group"));
            dialog->accept();
            return;
        }
    });
    groupAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    QTreeWidgetItem* groupedItem = nullptr;
    for (int index = 0; index < geometryItem->childCount(); ++index) {
        if (geometryItem->child(index)->text(0) == QStringLiteral("A08_Group")) {
            groupedItem = geometryItem->child(index);
            break;
        }
    }
    if (!groupedItem || groupedItem->childCount() != 2) {
        return fail("A08 grouping did not preserve both geometry children.");
    }
    const QString groupedUuid = groupedItem->data(0, Qt::UserRole).toString();
    const QString groupedChildUuid = groupedItem->child(0)->data(0, Qt::UserRole).toString();
    QAction* hideAction = findAction(window, QStringLiteral("Hide Selected"));
    if (!hideAction || !hideAction->isEnabled()) {
        return fail("A08 Hide Selected action was not enabled for the group.");
    }
    hideAction->trigger();
    QApplication::processEvents();
    if (occView->displayManager()->isObjectVisible(groupedChildUuid)) {
        return fail("A08 hiding a group did not hide its UUID-mapped child geometry.");
    }
    undoAction->trigger();
    redoAction->trigger();
    undoAction->trigger();
    QApplication::processEvents();
    if (!occView->displayManager()->isObjectVisible(groupedChildUuid) ||
        !captureAcceptance(window, QStringLiteral("A08-04-copy-group-undo-redo.png"))) {
        return fail("A08 visibility Undo/Redo or group acceptance screenshot failed.");
    }

    QTemporaryDir a08RoundTripDirectory;
    const QString a08ProjectPath =
        a08RoundTripDirectory.filePath(QStringLiteral("a08_modeling.firecae"));
    if (!window.saveProjectFile(a08ProjectPath) ||
        !window.openProjectFile(a08ProjectPath)) {
        return fail("A08 grouped native geometry project did not save and reopen.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    QTreeWidgetItem* reopenedGroup = findItemByUuid(projectItem, groupedUuid);
    QTreeWidgetItem* reopenedChild = findItemByUuid(projectItem, groupedChildUuid);
    if (!reopenedGroup || !reopenedChild || reopenedGroup->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 3 ||
        !captureAcceptance(window, QStringLiteral("A08-05-saved-reopened.png"))) {
        return fail("A08 reopen did not preserve geometry, UUIDs, hierarchy, or screenshot.");
    }

    QAction* newProjectAction = findAction(window, QStringLiteral("New"));
    if (!newProjectAction) return fail("A08 reset New action was not found.");
    newProjectAction->trigger();
    acceptNextCreateBoxDialog();
    createBoxAction->trigger();
    acceptNextCreateBoxDialog();
    createBoxAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 2) {
        return fail("A08 acceptance cleanup did not restore the two-box test state.");
    }

    if (!selectItem(tree, geometryItem->child(0)) ||
        !hasVisibleLabel(window, QStringLiteral("Box_001")) ||
        !hasVisibleLabel(window, QStringLiteral("Geometry")) ||
        !hasVisibleLabel(window, QStringLiteral("ID:"))) {
        return fail("Geometry properties did not update.");
    }
    const QString firstBoxId = geometryItem->child(0)->data(0, Qt::UserRole).toString();
    if (firstBoxId.isEmpty() ||
        occView->displayManager()->selectedObjectId() != firstBoxId) {
        return fail("Tree selection did not highlight its UUID-mapped presentation.");
    }

    const QString ifcPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                .filePath(QStringLiteral("tessellated-item.ifc"));
    if (!window.importIfcFile(ifcPath)) {
        return fail("IFC fixture could not be imported through MainWindow.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 3 ||
        occView->displayManager()->displayedObjectCount() != 3) {
        return fail("IFC model did not flow through model, tree, and viewer.");
    }
    QTreeWidgetItem* ifcProjectItem = geometryItem->child(2);
    if (ifcProjectItem->text(0) != QStringLiteral("proxy with tessellation") ||
        ifcProjectItem->childCount() != 1 ||
        ifcProjectItem->child(0)->text(0) != QStringLiteral("Test Building") ||
        ifcProjectItem->child(0)->childCount() != 1 ||
        ifcProjectItem->child(0)->child(0)->text(0) != QStringLiteral("P-1")) {
        return fail("IFC semantic decomposition tree was not preserved.");
    }
    const QString ifcRootId = ifcProjectItem->data(0, Qt::UserRole).toString();
    QTreeWidgetItem* ifcProductItem = ifcProjectItem->child(0)->child(0);
    const QString ifcProductId = ifcProductItem->data(0, Qt::UserRole).toString();
    if (ifcRootId.isEmpty() || ifcProductId.isEmpty() ||
        occView->displayManager()->contains(ifcRootId) ||
        !occView->displayManager()->contains(ifcProductId)) {
        return fail("IFC geometry was not keyed by the product FireCAE UUID.");
    }
    if (!selectItem(tree, ifcProductItem) ||
        !hasVisibleLabel(window, QStringLiteral("IFC Entity")) ||
        !hasVisibleLabel(window, QStringLiteral("IfcBuildingElementProxy")) ||
        !hasVisibleLabel(window, QStringLiteral("1kTvXnbbzCWw8lcMd1dR4o"))) {
        return fail("IFC identity properties did not update from UUID selection.");
    }
    GeometryDisplayManager* displayManager = occView->displayManager();
    const Handle(AIS_InteractiveObject) ifcPresentation =
        displayManager->presentationForObject(ifcProductId);
    if (displayManager->selectedObjectId() != ifcProductId ||
        ifcPresentation.IsNull() ||
        displayManager->objectIdForPresentation(ifcPresentation) != ifcProductId ||
        displayManager->reverseMappingCount() != 3) {
        return fail("IFC Tree-to-Viewer UUID selection mapping is inconsistent.");
    }

    displayManager->clearSelection();
    tree->clearSelection();
    const bool viewerSignalInvoked = QMetaObject::invokeMethod(occView,
                                   "objectSelected",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, ifcProductId));
    if (!viewerSignalInvoked || !tree->currentItem() ||
        tree->currentItem()->data(0, Qt::UserRole).toString() != ifcProductId) {
        return fail("Viewer-to-Tree UUID selection did not locate the IFC product.");
    }

    if (!QMetaObject::invokeMethod(occView,
                                   "selectionCleared",
                                   Qt::DirectConnection) ||
        tree->currentItem() != nullptr ||
        !displayManager->selectedObjectId().isEmpty() ||
        !hasVisibleLabel(window, QStringLiteral("No object selected"))) {
        return fail("Blank Viewer selection did not clear Tree, Viewer, and Properties.");
    }

    if (!selectItem(tree, ifcProductItem) ||
        displayManager->selectedObjectId() != ifcProductId) {
        return fail("IFC product could not be reselected before deletion regression.");
    }

    QAction* deleteAction = findAction(window, QStringLiteral("Delete"));
    if (!deleteAction || deleteAction->isEnabled()) {
        return fail("Delete must be disabled for IFC semantic child nodes.");
    }
    const QString ifcObjectId = ifcRootId;
    if (ifcObjectId.isEmpty() ||
        window.removeIfcObject(
            ifcProjectItem->child(0)->child(0)->data(0, Qt::UserRole).toString(), false)) {
        return fail("An IFC semantic child node was incorrectly removable.");
    }
    if (!selectItem(tree, ifcProjectItem) || !deleteAction->isEnabled()) {
        return fail("Delete was not enabled for an imported IFC model root.");
    }
    if (!selectItem(tree, ifcProductItem) || deleteAction->isEnabled()) {
        return fail("IFC product selection was not restored for deletion regression.");
    }
    if (!window.removeIfcObject(ifcObjectId, false)) {
        return fail("Imported IFC model could not be removed.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 2 ||
        occView->displayManager()->reverseMappingCount() != 2 ||
        !occView->displayManager()->selectedObjectId().isEmpty() ||
        deleteAction->isEnabled() ||
        !hasVisibleLabel(window, QStringLiteral("No object selected")) ||
        window.statusBar()->currentMessage() !=
            QStringLiteral("Removed proxy with tessellation") ||
        !QFileInfo::exists(ifcPath)) {
        return fail("IFC removal did not update model, tree, viewer, and properties safely.");
    }
    undoAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 3 ||
        occView->displayManager()->displayedObjectCount() != 3 ||
        occView->displayManager()->reverseMappingCount() != 3 ||
        !findItemByUuid(projectItem, ifcObjectId)) {
        return fail("A08 Undo did not restore the imported IFC object and presentations.");
    }
    redoAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    if (geometryItem->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 2 ||
        occView->displayManager()->reverseMappingCount() != 2 ||
        findItemByUuid(projectItem, ifcObjectId)) {
        return fail("A08 Redo did not remove the imported IFC object again.");
    }

    const QString multiProductIfcPath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("PCERT_Infra_Landscaping_IFC4.ifc"));
    if (!window.importIfcFile(multiProductIfcPath)) {
        return fail("Multi-product IFC fixture could not be imported.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    geometryItem = projectItem->child(0);
    QTreeWidgetItem* multiProductRoot = geometryItem->child(2);
    const QString multiProductRootId =
        multiProductRoot->data(0, Qt::UserRole).toString();
    if (geometryItem->childCount() != 3 ||
        occView->displayManager()->displayedObjectCount() != 103 ||
        occView->displayManager()->reverseMappingCount() != 103 ||
        multiProductRootId.isEmpty() ||
        occView->displayManager()->contains(multiProductRootId)) {
        return fail("Multi-product IFC shapes were not displayed independently by UUID.");
    }
    QTreeWidgetItem* multiProductItem =
        findDisplayedDescendant(multiProductRoot, occView->displayManager());
    const QString multiProductId = multiProductItem
                                       ? multiProductItem->data(0, Qt::UserRole).toString()
                                       : QString();
    if (!selectItem(tree, multiProductItem) || multiProductId.isEmpty() ||
        occView->displayManager()->selectedObjectId() != multiProductId ||
        occView->displayManager()->objectIdForPresentation(
            occView->displayManager()->presentationForObject(multiProductId)) !=
            multiProductId) {
        return fail("Multi-product IFC selection was not isolated by FireCAE UUID.");
    }
    if (!window.removeIfcObject(multiProductRootId, false)) {
        return fail("Multi-product IFC model could not be removed.");
    }
    QApplication::processEvents();
    if (tree->topLevelItem(0)->child(0)->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 2 ||
        occView->displayManager()->reverseMappingCount() != 2 ||
        !occView->displayManager()->selectedObjectId().isEmpty() ||
        !QFileInfo::exists(multiProductIfcPath)) {
        return fail("Multi-product IFC presentations were not removed as one model.");
    }

    const QString smvPath = QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
                                .filePath(QStringLiteral("fds-results/demo.smv"));
    if (!window.openResultFile(smvPath)) {
        return fail("FDS result fixture could not be opened through MainWindow.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    QTreeWidgetItem* resultsGroupItem = projectItem->child(14);
    if (!resultsGroupItem || resultsGroupItem->text(0) != QStringLiteral("Results") ||
        resultsGroupItem->childCount() != 1) {
        return fail("FDS result case was not generated from the Results object tree.");
    }
    QTreeWidgetItem* resultCaseItem = resultsGroupItem->child(0);
    const QString resultCaseId = resultCaseItem->data(0, Qt::UserRole).toString();
    const bool hasResultCaseLabel =
        hasVisibleLabel(window, QStringLiteral("FDS Result Case"));
    const bool hasReadyLabel = hasVisibleLabel(window, QStringLiteral("Ready"));
    const bool hasEndTimeLabel = hasVisibleLabel(window, QStringLiteral("2.0 s"));
    if (resultCaseId.isEmpty() || resultCaseItem->text(0) != QStringLiteral("demo") ||
        resultCaseItem->childCount() != 4 || !hasResultCaseLabel || !hasReadyLabel ||
        !hasEndTimeLabel) {
        std::cerr << "Result case diagnostics: id=" << resultCaseId.toStdString()
                  << ", text=" << resultCaseItem->text(0).toStdString()
                  << ", children=" << resultCaseItem->childCount()
                  << ", type-label=" << hasResultCaseLabel
                  << ", ready-label=" << hasReadyLabel
                  << ", end-time-label=" << hasEndTimeLabel << '\n';
        for (const QLabel* label : window.findChildren<QLabel*>()) {
            if (label->isVisible()) {
                std::cerr << "  visible label: " << label->text().toStdString() << '\n';
            }
        }
        return fail("FDS result case properties or UUID tree mapping is incorrect.");
    }
    QTreeWidgetItem* resultFileItem = resultCaseItem->child(0)->child(0);
    const QString resultFileId = resultFileItem->data(0, Qt::UserRole).toString();
    if (!selectItem(tree, resultFileItem) || resultFileId.isEmpty() ||
        resultFileId == resultCaseId ||
        !hasVisibleLabel(window, QStringLiteral("FDS Result File")) ||
        !hasVisibleLabel(window, QStringLiteral("Yes"))) {
        return fail("FDS result file did not resolve through its independent UUID.");
    }
    if (!window.reloadResultCase(resultFileId)) {
        return fail("FDS result case could not be reloaded from a child selection.");
    }
    QApplication::processEvents();
    resultsGroupItem = tree->topLevelItem(0)->child(14);
    if (resultsGroupItem->childCount() != 1 ||
        resultsGroupItem->child(0)->data(0, Qt::UserRole).toString() != resultCaseId) {
        return fail("Reload Results did not preserve the result case UUID.");
    }
    if (!window.closeResultCase(resultCaseId)) {
        return fail("FDS result case could not be closed.");
    }
    QApplication::processEvents();
    if (tree->topLevelItem(0)->child(14)->childCount() != 0 ||
        !hasVisibleLabel(window, QStringLiteral("No object selected")) ||
        !QFileInfo::exists(smvPath)) {
        return fail("Close Results did not safely clear the object tree and properties.");
    }
    if (!window.openResultFile(smvPath)) {
        return fail("FDS result case could not be reopened before New Project.");
    }
    QApplication::processEvents();

    const QList<QPair<QString, QString>> viewActions = {
        {QStringLiteral("Fit All"), QStringLiteral("Fit All")},
        {QStringLiteral("Front"), QStringLiteral("Front View")},
        {QStringLiteral("Back"), QStringLiteral("Back View")},
        {QStringLiteral("Left"), QStringLiteral("Left View")},
        {QStringLiteral("Right"), QStringLiteral("Right View")},
        {QStringLiteral("Top"), QStringLiteral("Top View")},
        {QStringLiteral("Bottom"), QStringLiteral("Bottom View")},
        {QStringLiteral("Isometric"), QStringLiteral("Isometric View")},
    };
    for (const auto& actionAndStatus : viewActions) {
        QAction* action = findAction(window, actionAndStatus.first);
        if (!action) {
            return fail("A standard view action was not found.");
        }
        action->trigger();
        QApplication::processEvents();
        if (window.statusBar()->currentMessage() != actionAndStatus.second) {
            return fail("A standard view action is still a placeholder.");
        }
    }

    QAction* newAction = findAction(window, QStringLiteral("New"));
    if (!newAction) {
        return fail("New action was not found.");
    }
    newAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->child(0)->childCount() != 0 ||
        projectItem->child(14)->childCount() != 0 ||
        occView->displayManager()->displayedObjectCount() != 0 ||
        occView->displayManager()->reverseMappingCount() != 0 ||
        !occView->displayManager()->selectedObjectId().isEmpty() ||
        !hasVisibleLabel(window, QStringLiteral("No object selected")) ||
        window.statusBar()->currentMessage() != QStringLiteral("New project created.")) {
        return fail("New Project did not clear model, tree, and viewer.");
    }

    acceptNextCreateBoxDialog();
    createBoxAction->trigger();
    QApplication::processEvents();
    if (tree->topLevelItem(0)->child(0)->childCount() != 1 ||
        tree->topLevelItem(0)->child(0)->child(0)->text(0) != QStringLiteral("Box_001") ||
        occView->displayManager()->displayedObjectCount() != 1) {
        return fail("Geometry creation did not recover after New Project.");
    }

    QAction* simpleTestAction = findAction(window, QStringLiteral("Create Simple Test Benchmark"));
    QAction* exportFdsAction = findAction(window, QStringLiteral("Export FDS Input..."));
    if (!simpleTestAction || !exportFdsAction) {
        return fail("Simple test creation or FDS export action was not created.");
    }
    window.loadSimpleTestBenchmark();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) != QStringLiteral("Simple demonstration case.") ||
        projectItem->child(0)->childCount() != 1 ||
        projectItem->child(1)->childCount() != 1 ||
        projectItem->child(5)->childCount() != 1 ||
        projectItem->child(6)->childCount() != 1 ||
        projectItem->child(8)->childCount() != 2 ||
        projectItem->child(13)->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 5 ||
        occView->displayManager()->reverseMappingCount() != 5) {
        return fail("Simple test benchmark did not populate the object tree.");
    }
    QTreeWidgetItem* benchmarkMesh = projectItem->child(1)->child(0);
    if (benchmarkMesh->data(0, Qt::UserRole).toString().isEmpty() ||
        !selectItem(tree, benchmarkMesh) ||
        !hasVisibleLabel(window, QStringLiteral("MESH_1"))) {
        return fail("Benchmark mesh did not preserve UUID selection and FDS identity.");
    }
    QTemporaryDir exportDirectory;
    const QString exportedFds = exportDirectory.filePath(QStringLiteral("simple_test.fds"));
    if (!window.exportCurrentProjectToFds(exportedFds) ||
        !QFileInfo::exists(exportedFds)) {
        return fail("Simple test benchmark could not be exported from MainWindow.");
    }

    QAction* activateVentsAction =
        window.findChild<QAction*>(QStringLiteral("ActivateVentsTutorialAction"));
    if (!activateVentsAction) {
        return fail("activate_vents tutorial action was not created.");
    }
    window.loadActivateVentsTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) !=
            QStringLiteral("Test of VENT activation/deactivation") ||
        projectItem->child(1)->childCount() != 1 ||
        projectItem->child(2)->childCount() != 1 ||
        projectItem->child(5)->childCount() != 7 ||
        projectItem->child(7)->childCount() != 7 ||
        projectItem->child(8)->childCount() != 12 ||
        projectItem->child(9)->childCount() != 6 ||
        projectItem->child(10)->childCount() != 10 ||
        occView->displayManager()->displayedObjectCount() != 19) {
        return fail("activate_vents tutorial did not populate all editable object groups.");
    }
    const QString preservedSurfaceUuid =
        projectItem->child(5)->child(0)->data(0, Qt::UserRole).toString();
    QTemporaryDir projectRoundTripDirectory;
    const QString savedProject = projectRoundTripDirectory.filePath(
        QStringLiteral("activate_vents.firecae"));
    if (preservedSurfaceUuid.isEmpty() ||
        !window.saveProjectFile(savedProject) || !QFileInfo::exists(savedProject)) {
        return fail("activate_vents project could not be saved through MainWindow.");
    }
    window.loadSimpleTestBenchmark();
    if (!window.openProjectFile(savedProject)) {
        return fail("activate_vents project could not be reopened through MainWindow.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->child(5)->child(0)->data(0, Qt::UserRole).toString() !=
        preservedSurfaceUuid) {
        return fail("Project reopen changed the tutorial object UUID mapping.");
    }
    const QString reopenedFds = projectRoundTripDirectory.filePath(
        QStringLiteral("activate_vents.fds"));
    if (!window.exportCurrentProjectToFds(reopenedFds)) {
        return fail("Reopened activate_vents project could not generate FDS.");
    }
    QFile reopenedInput(reopenedFds);
    if (!reopenedInput.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail("Generated activate_vents FDS file could not be read.");
    }
    const QString reopenedText = QString::fromUtf8(reopenedInput.readAll());
    if (reopenedText.count(QStringLiteral("&RAMP ID='ramp 1'")) != 7 ||
        reopenedText.count(QStringLiteral("&VENT")) != 12 ||
        !reopenedText.contains(QStringLiteral("T_END=20"))) {
        return fail("Generated activate_vents FDS semantics are incomplete.");
    }

    QAction* bucketTest2Action =
        window.findChild<QAction*>(QStringLiteral("BucketTest2TutorialAction"));
    if (!bucketTest2Action) {
        return fail("bucket_test_2 tutorial action was not created.");
    }
    window.loadBucketTest2Tutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) != QStringLiteral("Customized sprinkler test case") ||
        projectItem->child(1)->childCount() != 1 ||
        projectItem->child(3)->childCount() != 1 ||
        projectItem->child(7)->childCount() != 2 ||
        projectItem->child(8)->childCount() != 4 ||
        projectItem->child(9)->childCount() != 2 ||
        projectItem->child(10)->childCount() != 2 ||
        projectItem->child(13)->childCount() != 1 ||
        occView->displayManager()->displayedObjectCount() != 7) {
        return fail("bucket_test_2 tutorial did not populate all editable object groups.");
    }
    const QString bucketFds = projectRoundTripDirectory.filePath(
        QStringLiteral("bucket_test_2.fds"));
    if (!window.exportCurrentProjectToFds(bucketFds)) {
        return fail("bucket_test_2 project could not generate FDS.");
    }

    QAction* couchAction =
        window.findChild<QAction*>(QStringLiteral("CouchTutorialAction"));
    if (!couchAction) {
        return fail("couch tutorial action was not created.");
    }
    window.loadCouchTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) != QStringLiteral("Single Couch Test Case") ||
        projectItem->child(0)->childCount() != 5 ||
        projectItem->child(1)->childCount() != 2 ||
        projectItem->child(2)->childCount() != 1 ||
        projectItem->child(3)->childCount() != 1 ||
        projectItem->child(4)->childCount() != 3 ||
        projectItem->child(5)->childCount() != 3 ||
        projectItem->child(6)->childCount() != 1 ||
        projectItem->child(7)->childCount() != 1 ||
        projectItem->child(8)->childCount() != 1 ||
        projectItem->child(12)->childCount() != 1 ||
        projectItem->child(13)->childCount() != 7 ||
        occView->displayManager()->displayedObjectCount() != 17 ||
        occView->displayManager()->presentationsForObject(
            projectItem->child(1)->child(0)->data(0, Qt::UserRole).toString()).size() != 8) {
        return fail("couch tutorial did not populate all editable object groups.");
    }
    const QString couchFds = projectRoundTripDirectory.filePath(
        QStringLiteral("couch.fds"));
    if (!window.exportCurrentProjectToFds(couchFds)) {
        return fail("couch project could not generate FDS.");
    }

    QAction* couchSmokeAction =
        window.findChild<QAction*>(QStringLiteral("CouchSmoke12sTutorialAction"));
    if (!couchSmokeAction) {
        return fail("couch_smoke_12s tutorial action was not created.");
    }
    window.loadCouchSmoke12sTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) !=
            QStringLiteral("Single Couch Tutorial - 12 s FireCAE Smoke Test") ||
        projectItem->child(0)->childCount() != 5 ||
        projectItem->child(1)->childCount() != 2 ||
        projectItem->child(2)->childCount() != 1 ||
        projectItem->child(3)->childCount() != 1 ||
        projectItem->child(4)->childCount() != 3 ||
        projectItem->child(5)->childCount() != 3 ||
        projectItem->child(6)->childCount() != 1 ||
        projectItem->child(7)->childCount() != 1 ||
        projectItem->child(8)->childCount() != 1 ||
        projectItem->child(12)->childCount() != 1 ||
        projectItem->child(13)->childCount() != 7 ||
        occView->displayManager()->displayedObjectCount() != 17) {
        return fail("couch_smoke_12s tutorial object groups are incomplete.");
    }

    QAction* hvacAircoilAction =
        window.findChild<QAction*>(QStringLiteral("HvacAircoilTutorialAction"));
    if (!hvacAircoilAction) {
        return fail("HVAC_aircoil tutorial action was not created.");
    }
    window.loadHvacAircoilTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) != QStringLiteral("Test of aircoil") ||
        projectItem->child(0)->childCount() != 1 ||
        projectItem->child(1)->childCount() != 1 ||
        projectItem->child(2)->childCount() != 3 ||
        projectItem->child(3)->childCount() != 1 ||
        projectItem->child(5)->childCount() != 1 ||
        projectItem->child(8)->childCount() != 6 ||
        projectItem->child(9)->childCount() != 2 ||
        projectItem->child(11)->childCount() != 4 ||
        projectItem->child(13)->childCount() != 1 ||
        occView->displayManager()->displayedObjectCount() != 15) {
        return fail("HVAC_aircoil tutorial object groups are incomplete.");
    }

    QAction* tunnelDemoAction =
        window.findChild<QAction*>(QStringLiteral("TunnelDemoTutorialAction"));
    QAction* tunnelSmokeAction =
        window.findChild<QAction*>(QStringLiteral("TunnelSmoke10sTutorialAction"));
    if (!tunnelDemoAction || !tunnelSmokeAction) {
        return fail("Tunnel tutorial actions were not created.");
    }
    window.loadTunnelDemoTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) != QStringLiteral("Example of a tunnel simulation") ||
        projectItem->child(1)->childCount() != 2 ||
        projectItem->child(2)->childCount() != 3 ||
        projectItem->child(5)->childCount() != 1 ||
        projectItem->child(6)->childCount() != 1 ||
        projectItem->child(8)->childCount() != 2 ||
        projectItem->child(13)->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 12) {
        return fail("tunnel_demo tutorial object groups are incomplete.");
    }
    window.loadTunnelSmoke10sTutorial();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->text(0) !=
            QStringLiteral("Tunnel Demo - 10 s FireCAE Smoke Test") ||
        projectItem->child(1)->childCount() != 2 ||
        projectItem->child(8)->childCount() != 2 ||
        projectItem->child(13)->childCount() != 2 ||
        occView->displayManager()->displayedObjectCount() != 12) {
        return fail("tunnel_smoke_10s tutorial object groups are incomplete.");
    }

    QAction* duplicateObjectAction =
        window.findChild<QAction*>(QStringLiteral("DuplicateObjectAction"));
    QAction* moveObjectUpAction =
        window.findChild<QAction*>(QStringLiteral("MoveObjectUpAction"));
    QAction* moveObjectDownAction =
        window.findChild<QAction*>(QStringLiteral("MoveObjectDownAction"));
    QTreeWidgetItem* firstTunnelMesh = projectItem->child(1)->child(0);
    const QString originalTunnelMeshUuid =
        firstTunnelMesh->data(0, Qt::UserRole).toString();
    if (occView->displayManager()->presentationsForObject(originalTunnelMeshUuid).size() != 8) {
        return fail("One tunnel mesh UUID did not map to all eight AIS instances.");
    }
    ModelTreeWidget* modelTree = window.findChild<ModelTreeWidget*>();
    if (!modelTree ||
        !QMetaObject::invokeMethod(modelTree, "visibilityChangeRequested",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, originalTunnelMeshUuid),
                                   Q_ARG(bool, false)) ||
        occView->displayManager()->isObjectVisible(originalTunnelMeshUuid)) {
        return fail("Tree visibility did not hide every presentation for one UUID.");
    }
    if (!QMetaObject::invokeMethod(modelTree, "visibilityChangeRequested",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, originalTunnelMeshUuid),
                                   Q_ARG(bool, true)) ||
        !occView->displayManager()->isObjectVisible(originalTunnelMeshUuid)) {
        return fail("Tree visibility did not restore every presentation for one UUID.");
    }
    projectItem = tree->topLevelItem(0);
    firstTunnelMesh = projectItem->child(1)->child(0);
    if (!duplicateObjectAction || !moveObjectUpAction || !moveObjectDownAction ||
        !selectItem(tree, firstTunnelMesh) || !duplicateObjectAction->isEnabled() ||
        !moveObjectUpAction->isEnabled() || !moveObjectDownAction->isEnabled()) {
        return fail("FDS object duplicate/reorder actions were not enabled by UUID selection.");
    }
    duplicateObjectAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    QTreeWidgetItem* meshGroup = projectItem->child(1);
    if (meshGroup->childCount() != 3) {
        return fail("Duplicating an FDS object did not add an editable tree object.");
    }
    const QString duplicateTunnelMeshUuid =
        meshGroup->child(1)->data(0, Qt::UserRole).toString();
    if (duplicateTunnelMeshUuid.isEmpty() ||
        duplicateTunnelMeshUuid == originalTunnelMeshUuid ||
        tree->currentItem() != meshGroup->child(1)) {
        return fail("Duplicated FDS object did not receive/select a new stable UUID.");
    }
    moveObjectDownAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    meshGroup = projectItem->child(1);
    if (meshGroup->child(2)->data(0, Qt::UserRole).toString() !=
        duplicateTunnelMeshUuid) {
        return fail("Moving an FDS object down did not update UUID-backed tree order.");
    }
    moveObjectUpAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    meshGroup = projectItem->child(1);
    if (meshGroup->child(1)->data(0, Qt::UserRole).toString() !=
        duplicateTunnelMeshUuid) {
        return fail("Moving an FDS object up did not restore UUID-backed tree order.");
    }
    const QString duplicatedProject = projectRoundTripDirectory.filePath(
        QStringLiteral("tunnel_smoke_10s_duplicated.firecae"));
    if (!window.saveProjectFile(duplicatedProject) ||
        !window.openProjectFile(duplicatedProject)) {
        return fail("Project containing a duplicated/reordered object did not round-trip.");
    }
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    meshGroup = projectItem->child(1);
    if (meshGroup->childCount() != 3 ||
        meshGroup->child(1)->data(0, Qt::UserRole).toString() !=
            duplicateTunnelMeshUuid) {
        return fail("Duplicate UUID or reordered tree position changed after project reopen.");
    }

    // A tutorial must never silently replace an edited project.
    if (!selectItem(tree, meshGroup->child(0))) {
        return fail("Could not select a mesh for the unsaved tutorial regression.");
    }
    duplicateObjectAction->trigger();
    QApplication::processEvents();
    bool sawUnsavedPrompt = false;
    qunsetenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED");
    QTimer::singleShot(0, &window, [&sawUnsavedPrompt]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (messageBox && messageBox->windowTitle() == QStringLiteral("Unsaved Project")) {
                sawUnsavedPrompt = true;
                messageBox->done(QMessageBox::Discard);
                return;
            }
        }
    });
    window.loadBucketTest2Tutorial();
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED", "1");
    QApplication::processEvents();
    if (!sawUnsavedPrompt || tree->topLevelItem(0)->text(0) !=
                                  QStringLiteral("Customized sprinkler test case")) {
        return fail("Tutorial replacement did not protect unsaved project changes.");
    }

    projectItem = tree->topLevelItem(0);
    QTreeWidgetItem* bucketVentGroup = projectItem->child(8);
    QTreeWidgetItem* bucketVent = bucketVentGroup->child(0);
    if (!selectItem(tree, bucketVent) || !deleteAction->isEnabled()) {
        return fail("A tutorial FDS object could not be selected for deletion.");
    }
    QTimer::singleShot(0, &window, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (messageBox && messageBox->isVisible()) {
                if (QAbstractButton* yesButton = messageBox->button(QMessageBox::Yes)) {
                    yesButton->click();
                }
                return;
            }
        }
    });
    deleteAction->trigger();
    QApplication::processEvents();
    projectItem = tree->topLevelItem(0);
    if (projectItem->child(8)->childCount() != 3 ||
        occView->displayManager()->displayedObjectCount() != 6) {
        return fail("Deleting an FDS object did not rebuild the UUID scene.");
    }

    QTreeWidgetItem* deviceGroup = projectItem->child(9);
    const QString deviceGroupId = deviceGroup->data(0, Qt::UserRole).toString();
    const QString deviceId = deviceGroup->child(0)->data(0, Qt::UserRole).toString();
    const QString bucketMeshId =
        projectItem->child(1)->child(0)->data(0, Qt::UserRole).toString();
    if (!QMetaObject::invokeMethod(modelTree, "visibilityChangeRequested",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, bucketMeshId),
                                   Q_ARG(bool, false)) ||
        occView->displayManager()->isObjectVisible(bucketMeshId)) {
        return fail("Persistent object hiding did not update the 3D scene.");
    }
    if (!QMetaObject::invokeMethod(modelTree, "isolateObjectRequested",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, deviceGroupId)) ||
        !modelTree->isIsolationActive() ||
        !occView->displayManager()->isObjectVisible(deviceId) ||
        occView->displayManager()->isObjectVisible(bucketMeshId)) {
        return fail("Show Only did not apply group visibility to the 3D scene.");
    }
    if (!QMetaObject::invokeMethod(modelTree, "restoreVisibilityRequested",
                                   Qt::DirectConnection) ||
        modelTree->isIsolationActive() ||
        occView->displayManager()->isObjectVisible(bucketMeshId) ||
        !occView->displayManager()->isObjectVisible(deviceId)) {
        return fail("Exiting isolation did not restore the prior intentional visibility state.");
    }
    if (!QMetaObject::invokeMethod(modelTree, "restoreVisibilityRequested",
                                   Qt::DirectConnection) ||
        !occView->displayManager()->isObjectVisible(bucketMeshId)) {
        return fail("Show All Objects did not restore persistently hidden objects in one step.");
    }

    std::cout << "FireCAE A07 UI smoke tests passed.\n";
    return 0;
}
