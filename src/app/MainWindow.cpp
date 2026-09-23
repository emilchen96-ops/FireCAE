#include "app/MainWindow.h"

#include "core/FcDocument.h"
#include "core/FcFloorObject.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "comparison/FdsComparisonReport.h"
#include "comparison/FdsInputComparator.h"
#include "fds/FdsExamples.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/GeometryDerivedUpdateService.h"
#include "modeling/GeometryEditDependencyService.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsImporter.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsWriter.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "import/IfcImportService.h"
#include "import/GeometryImportService.h"
#include "modeling/SnapManager.h"
#include "modeling/BuildingGeometryService.h"
#include "reliability/ProjectRecoveryManager.h"
#include "reliability/CrashDiagnostics.h"
#include "resources/ProjectResourceManager.h"
#include "settings/ApplicationSettings.h"
#include "simulation/SolverBackend.h"
#include "results/FcResultCase.h"
#include "results/FcResultFile.h"
#include "results/FdsResultComparator.h"
#include "results/FdsResultScanner.h"
#include "results/SmokeviewLauncher.h"
#include "simulation/FdsRunner.h"
#include "simulation/SimulationTaskManager.h"
#include "ui/MessageWidget.h"
#include "ui/BuildingElementDialog.h"
#include "ui/ApplicationSettingsDialog.h"
#include "ui/FdsObjectEditorDialog.h"
#include "ui/FdsRecordEditor.h"
#include "ui/GeometryImportWizard.h"
#include "ui/FdsWorkflowDialogs.h"
#include "ui/FloorEditorDialog.h"
#include "ui/ModelTreeWidget.h"
#include "ui/ObjectReferenceDialog.h"
#include "ui/NativeResultViewerWidget.h"
#include "ui/PropertiesWidget.h"
#include "ui/ProjectResourcesDialog.h"
#include "ui/ScenarioManagerDialog.h"
#include "ui/SurfaceAssignmentDialog.h"
#include "ui/SmokeviewHostWidget.h"
#include "ui/StartPageWidget.h"
#include "ui/TutorialGuideWidget.h"
#include "ui/SimulationTaskCenterWidget.h"
#include "ui/SimulationStatusWidget.h"
#include "ui/SimulationParametersDialog.h"
#include "ui/SimulationRunDialog.h"
#include "ui/UiLanguage.h"
#include "visualization/GeometryDisplayManager.h"
#include "visualization/FdsSceneSynchronizer.h"
#include "visualization/OccViewWidget.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScreen>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QThread>
#include <QVBoxLayout>
#include <QUrl>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <functional>

namespace
{
constexpr int kWindowFallbackWidth = 1280;
constexpr int kWindowFallbackHeight = 820;
constexpr int kMinimumWindowWidth = 1000;
constexpr int kMinimumWindowHeight = 650;
constexpr int kStatusMessageDurationMs = 5000;
constexpr int kLeftDockMinimumWidth = 190;
constexpr int kLeftDockMaximumWidth = 280;
constexpr int kMessagesDockMinimumHeight = 140;
constexpr int kMessagesDockMaximumHeight = 190;
constexpr qreal kInitialScreenFraction = 0.86;

QString u(const char* englishText);

class FunctionalUndoCommand final : public QUndoCommand
{
public:
    FunctionalUndoCommand(QString text,
                          std::function<void()> redoFunction,
                          std::function<void()> undoFunction)
        : QUndoCommand(std::move(text))
        , m_redoFunction(std::move(redoFunction))
        , m_undoFunction(std::move(undoFunction))
    {
    }

    void redo() override { if (m_redoFunction) m_redoFunction(); }
    void undo() override { if (m_undoFunction) m_undoFunction(); }

private:
    std::function<void()> m_redoFunction;
    std::function<void()> m_undoFunction;
};

enum class TransformPivot
{
    ObjectCenter,
    WorldOrigin,
    Custom
};

struct GeometryTransformParameters
{
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double scale = 1.0;
    TransformPivot pivot = TransformPivot::ObjectCenter;
    gp_Pnt customPivot;
    bool snapTranslation = true;
    double snapStep = 0.1;
    bool snapAngle = true;
    double angleStep = 15.0;
};

QDoubleSpinBox* createEngineeringSpinBox(QWidget* parent,
                                         double minimum,
                                         double maximum,
                                         double value,
                                         const QString& suffix = {})
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(6);
    spinBox->setSingleStep(0.1);
    spinBox->setValue(value);
    spinBox->setSuffix(suffix);
    return spinBox;
}

double snapped(double value, double step)
{
    return step > 0.0 ? std::round(value / step) * step : value;
}

std::shared_ptr<FcFdsMesh> meshForGeometryConversion(const FcObject::Ptr& object)
{
    if (!object) return {};
    if (const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(object)) {
        return mesh;
    }
    const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
    if (!namelist || namelist->keyword() != QStringLiteral("MESH")) return {};

    const QStringList ijkParts = namelist->parameterValue(QStringLiteral("IJK"))
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList xbParts = namelist->parameterValue(QStringLiteral("XB"))
                                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (ijkParts.size() != 3 || xbParts.size() != 6) return {};

    std::array<int, 3> cells{};
    FcFdsBounds bounds;
    double* coordinates[] = {&bounds.xMin, &bounds.xMax, &bounds.yMin,
                             &bounds.yMax, &bounds.zMin, &bounds.zMax};
    for (int index = 0; index < 3; ++index) {
        bool ok = false;
        cells[index] = ijkParts[index].trimmed().toInt(&ok);
        if (!ok || cells[index] <= 0) return {};
    }
    for (int index = 0; index < 6; ++index) {
        bool ok = false;
        *coordinates[index] = xbParts[index].trimmed().toDouble(&ok);
        if (!ok) return {};
    }
    if (!(bounds.xMin < bounds.xMax && bounds.yMin < bounds.yMax &&
          bounds.zMin < bounds.zMax)) {
        return {};
    }
    auto mesh = std::make_shared<FcFdsMesh>(namelist->name(), namelist->fdsId(),
                                            cells, bounds);
    return mesh;
}

gp_Pnt shapeCenter(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return gp_Pnt(0.0, 0.0, 0.0);
    Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
    Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    return gp_Pnt((xMin + xMax) * 0.5,
                  (yMin + yMax) * 0.5,
                  (zMin + zMax) * 0.5);
}

TopoDS_Shape applyTransform(const TopoDS_Shape& source,
                            const GeometryTransformParameters& parameters,
                            gp_Trsf* combinedTransform = nullptr)
{
    TopoDS_Shape result = source;
    const gp_Pnt pivot = parameters.pivot == TransformPivot::ObjectCenter
                             ? shapeCenter(source)
                             : parameters.pivot == TransformPivot::WorldOrigin
                                   ? gp_Pnt(0.0, 0.0, 0.0)
                                   : parameters.customPivot;
    if(combinedTransform) *combinedTransform=gp_Trsf();
    const auto apply = [&result, combinedTransform](const gp_Trsf& transform) {
        result = BRepBuilderAPI_Transform(result, transform, Standard_True).Shape();
        if(combinedTransform) combinedTransform->PreMultiply(transform);
    };
    if (parameters.scale != 1.0) {
        gp_Trsf transform;
        transform.SetScale(pivot, parameters.scale);
        apply(transform);
    }
    constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;
    const std::array<std::pair<double, gp_Dir>, 3> rotations = {{
        {parameters.rx, gp_Dir(1.0, 0.0, 0.0)},
        {parameters.ry, gp_Dir(0.0, 1.0, 0.0)},
        {parameters.rz, gp_Dir(0.0, 0.0, 1.0)}}};
    for (const auto& [angle, direction] : rotations) {
        if (angle == 0.0) continue;
        gp_Trsf transform;
        transform.SetRotation(gp_Ax1(pivot, direction), angle * degreesToRadians);
        apply(transform);
    }
    if (parameters.dx != 0.0 || parameters.dy != 0.0 || parameters.dz != 0.0) {
        gp_Trsf transform;
        transform.SetTranslation(gp_Vec(parameters.dx, parameters.dy, parameters.dz));
        apply(transform);
    }
    return result;
}

bool requestGeometryTransform(QWidget* parent,
                              const FcProject& project,
                              const QString& title,
                              GeometryTransformParameters& parameters)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("GeometryTransformDialog"));
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    const QString unit = QStringLiteral(" %1").arg(project.displayUnitSymbol());
    QDoubleSpinBox* dx = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9,
                                                   project.metersToDisplay(parameters.dx), unit);
    QDoubleSpinBox* dy = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9,
                                                   project.metersToDisplay(parameters.dy), unit);
    QDoubleSpinBox* dz = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9,
                                                   project.metersToDisplay(parameters.dz), unit);
    QDoubleSpinBox* rx = createEngineeringSpinBox(&dialog, -36000.0, 36000.0,
                                                   parameters.rx, QStringLiteral("°"));
    QDoubleSpinBox* ry = createEngineeringSpinBox(&dialog, -36000.0, 36000.0,
                                                   parameters.ry, QStringLiteral("°"));
    QDoubleSpinBox* rz = createEngineeringSpinBox(&dialog, -36000.0, 36000.0,
                                                   parameters.rz, QStringLiteral("°"));
    QDoubleSpinBox* scale = createEngineeringSpinBox(&dialog, 0.000001, 1000000.0,
                                                      parameters.scale);
    auto* pivot = new QComboBox(&dialog);
    pivot->addItem(u("Object Center"), static_cast<int>(TransformPivot::ObjectCenter));
    pivot->addItem(u("World Origin"), static_cast<int>(TransformPivot::WorldOrigin));
    pivot->addItem(u("Custom Point"), static_cast<int>(TransformPivot::Custom));
    auto* px = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* py = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* pz = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* snapTranslation = new QCheckBox(u("Snap translation to step"), &dialog);
    snapTranslation->setChecked(parameters.snapTranslation);
    auto* snapStep = createEngineeringSpinBox(&dialog, 0.000001, 1.0e9,
                                               project.metersToDisplay(parameters.snapStep), unit);
    auto* snapAngle = new QCheckBox(u("Snap rotation to angle"), &dialog);
    snapAngle->setChecked(parameters.snapAngle);
    auto* angleStep = createEngineeringSpinBox(&dialog, 0.001, 360.0,
                                                parameters.angleStep, QStringLiteral("°"));
    form->addRow(u("Move X:"), dx);
    form->addRow(u("Move Y:"), dy);
    form->addRow(u("Move Z:"), dz);
    form->addRow(u("Rotate X:"), rx);
    form->addRow(u("Rotate Y:"), ry);
    form->addRow(u("Rotate Z:"), rz);
    form->addRow(u("Uniform Scale:"), scale);
    form->addRow(u("Pivot:"), pivot);
    form->addRow(u("Pivot X:"), px);
    form->addRow(u("Pivot Y:"), py);
    form->addRow(u("Pivot Z:"), pz);
    form->addRow(snapTranslation, snapStep);
    form->addRow(snapAngle, angleStep);
    layout->addLayout(form);
    const auto updatePivotFields = [pivot, px, py, pz]() {
        const bool custom = pivot->currentData().toInt() ==
                            static_cast<int>(TransformPivot::Custom);
        px->setEnabled(custom); py->setEnabled(custom); pz->setEnabled(custom);
    };
    QObject::connect(pivot, &QComboBox::currentIndexChanged, &dialog,
                     [updatePivotFields]() { updatePivotFields(); });
    updatePivotFields();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return false;
    parameters.dx = project.displayToMeters(dx->value());
    parameters.dy = project.displayToMeters(dy->value());
    parameters.dz = project.displayToMeters(dz->value());
    parameters.rx = rx->value(); parameters.ry = ry->value(); parameters.rz = rz->value();
    parameters.scale = scale->value();
    parameters.pivot = static_cast<TransformPivot>(pivot->currentData().toInt());
    parameters.customPivot = gp_Pnt(project.displayToMeters(px->value()),
                                    project.displayToMeters(py->value()),
                                    project.displayToMeters(pz->value()));
    parameters.snapTranslation = snapTranslation->isChecked();
    parameters.snapStep = project.displayToMeters(snapStep->value());
    parameters.snapAngle = snapAngle->isChecked();
    parameters.angleStep = angleStep->value();
    if (parameters.snapTranslation) {
        parameters.dx = snapped(parameters.dx, parameters.snapStep);
        parameters.dy = snapped(parameters.dy, parameters.snapStep);
        parameters.dz = snapped(parameters.dz, parameters.snapStep);
    }
    if (parameters.snapAngle) {
        parameters.rx = snapped(parameters.rx, parameters.angleStep);
        parameters.ry = snapped(parameters.ry, parameters.angleStep);
        parameters.rz = snapped(parameters.rz, parameters.angleStep);
    }
    return true;
}

QString u(const char* englishText)
{
    return UiLanguageManager::text(QString::fromUtf8(englishText));
}

int descendantCount(const FcObject& object)
{
    int count = 1;
    for (const FcObject::Ptr& child : object.children()) {
        if (child) {
            count += descendantCount(*child);
        }
    }
    return count;
}

std::shared_ptr<FcResultCase> resultCaseForObject(FcObject* object)
{
    for (FcObject* current = object; current; current = current->parent()) {
        if (current->type() == FcObjectType::ResultCase) {
            return std::dynamic_pointer_cast<FcResultCase>(
                current->parent() ? current->parent()->findChild(current->id(), false)
                                  : FcObject::Ptr{});
        }
    }
    return {};
}

FcObjectType fdsObjectTypeForKeyword(const QString& value)
{
    const QString keyword = value.trimmed().toUpper();
    if (keyword == QStringLiteral("MESH")) return FcObjectType::Mesh;
    if (keyword == QStringLiteral("MULT")) return FcObjectType::MeshMultiplier;
    if (keyword == QStringLiteral("SPEC")) return FcObjectType::Species;
    if (keyword == QStringLiteral("MATL")) return FcObjectType::Material;
    if (keyword == QStringLiteral("SURF")) return FcObjectType::Surface;
    if (keyword == QStringLiteral("REAC")) return FcObjectType::Reaction;
    if (keyword == QStringLiteral("OBST")) return FcObjectType::Obstruction;
    if (keyword == QStringLiteral("VENT")) return FcObjectType::Vent;
    if (keyword == QStringLiteral("PART")) return FcObjectType::Particle;
    if (keyword == QStringLiteral("PROP")) return FcObjectType::Property;
    if (keyword == QStringLiteral("TABL")) return FcObjectType::Table;
    if (keyword == QStringLiteral("RAMP")) return FcObjectType::Ramp;
    if (keyword == QStringLiteral("DEVC")) return FcObjectType::Device;
    if (keyword == QStringLiteral("CTRL")) return FcObjectType::Control;
    if (keyword == QStringLiteral("HVAC")) return FcObjectType::HVAC;
    if (keyword == QStringLiteral("INIT")) return FcObjectType::InitialCondition;
    if (keyword == QStringLiteral("SLCF") || keyword == QStringLiteral("BNDF") ||
        keyword == QStringLiteral("ISOF") || keyword == QStringLiteral("PROF") ||
        keyword == QStringLiteral("PL3D") || keyword == QStringLiteral("SM3D") ||
        keyword == QStringLiteral("DUMP")) return FcObjectType::Output;
    return FcObjectType::SimulationParameter;
}

void updateMaximumSequence(const FcObject::Ptr& object, int& maximum)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        maximum = qMax(maximum, namelist->sequenceIndex());
    }
    for (const FcObject::Ptr& child : object->children()) {
        updateMaximumSequence(child, maximum);
    }
}

void shiftSequenceAfter(const FcObject::Ptr& object, int sequenceIndex, int delta)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        if (namelist->sequenceIndex() > sequenceIndex) {
            namelist->setSequenceIndex(namelist->sequenceIndex() + delta);
        }
    }
    for (const FcObject::Ptr& child : object->children()) {
        shiftSequenceAfter(child, sequenceIndex, delta);
    }
}

bool isLockedForModification(const FcObject* object)
{
    for (const FcObject* current = object; current; current = current->parent()) {
        if (current->isLocked()) return true;
    }
    return false;
}

void removePresentationsRecursive(const FcObject::Ptr& object,
                                  GeometryDisplayManager* displayManager)
{
    if (!object || !displayManager) return;
    displayManager->removeObject(object->id());
    for (const FcObject::Ptr& child : object->children()) {
        removePresentationsRecursive(child, displayManager);
    }
}

void restoreGeometryPresentationsRecursive(const FcObject::Ptr& object,
                                           GeometryDisplayManager* displayManager)
{
    if (!object || !displayManager) return;
    if (object->type() == FcObjectType::IfcModel) {
        if (const auto ifcRoot = std::dynamic_pointer_cast<FcIfcObject>(object)) {
            displayManager->displayIfcModel(ifcRoot);
            return;
        }
    }
    if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        if (geometry->hasShape()) displayManager->displayObject(geometry);
    }
    for (const FcObject::Ptr& child : object->children()) {
        restoreGeometryPresentationsRecursive(child, displayManager);
    }
}

void collectObjectSubtree(const FcObject::Ptr& object, QVector<FcObject::Ptr>& objects)
{
    if (!object) return;
    objects.append(object);
    for (const FcObject::Ptr& child : object->children()) {
        collectObjectSubtree(child, objects);
    }
}

void collectGeometryObjects(const FcObject::Ptr& object,
                            QVector<std::shared_ptr<FcGeometryObject>>& geometry)
{
    if (!object) return;
    if (const auto item = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        geometry.append(item);
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectGeometryObjects(child, geometry);
    }
}

struct HostedGeometryState
{
    std::shared_ptr<FcGeometryObject> object;
    TopoDS_Shape shape;
    QVariantMap parameters;
};

void updateDisplayVisibilityRecursive(const FcObject::Ptr& object,
                                      GeometryDisplayManager* displayManager)
{
    if (!object || !displayManager) return;
    bool visible = true;
    for (const FcObject* current = object.get(); current; current = current->parent()) {
        if (!current->isVisible()) {
            visible = false;
            break;
        }
    }
    if (!std::dynamic_pointer_cast<FcFdsObject>(object) &&
        displayManager->contains(object->id())) {
        visible ? displayManager->showObject(object->id())
                : displayManager->hideObject(object->id());
    }
    for (const FcObject::Ptr& child : object->children()) {
        updateDisplayVisibilityRecursive(child, displayManager);
    }
}

void setTemporaryDisplayVisibilityRecursive(const FcObject::Ptr& object,
                                            GeometryDisplayManager* displayManager,
                                            const QSet<QString>& visibleObjectIds)
{
    if (!object || !displayManager) return;
    if (displayManager->contains(object->id())) {
        visibleObjectIds.contains(object->id())
            ? displayManager->showObject(object->id())
            : displayManager->hideObject(object->id());
    }
    for (const FcObject::Ptr& child : object->children()) {
        setTemporaryDisplayVisibilityRecursive(child, displayManager,
                                               visibleObjectIds);
    }
}

void restoreDisplayVisibilityRecursive(const FcObject::Ptr& object,
                                       GeometryDisplayManager* displayManager)
{
    if (!object || !displayManager) return;
    bool visible = true;
    for (const FcObject* current = object.get(); current; current = current->parent()) {
        if (!current->isVisible()) {
            visible = false;
            break;
        }
    }
    if (displayManager->contains(object->id())) {
        visible ? displayManager->showObject(object->id())
                : displayManager->hideObject(object->id());
    }
    for (const FcObject::Ptr& child : object->children()) {
        restoreDisplayVisibilityRecursive(child, displayManager);
    }
}

bool containsFdsId(const FcObject::Ptr& object,
                   const QString& keyword,
                   const QString& fdsId)
{
    if (!object) return false;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        if (namelist->keyword() == keyword &&
            namelist->fdsId().compare(fdsId, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    for (const FcObject::Ptr& child : object->children()) {
        if (containsFdsId(child, keyword, fdsId)) return true;
    }
    return false;
}

QString uniqueCopyFdsId(const FcDocument& document,
                        const QString& keyword,
                        const QString& sourceId)
{
    if (sourceId.isEmpty()) return {};
    const auto exists = [&document, &keyword](const QString& candidate) {
        for (const auto& group : document.groups()) {
            if (containsFdsId(group, keyword, candidate)) return true;
        }
        return false;
    };
    QString candidate = sourceId + QStringLiteral("_COPY");
    int suffix = 2;
    while (exists(candidate)) {
        candidate = sourceId + QStringLiteral("_COPY_%1").arg(suffix++);
    }
    return candidate;
}

void replaceResultCaseContents(FcResultCase& resultCase,
                               const FdsResultScanResult& scan)
{
    QStringList childIds;
    for (const FcObject::Ptr& child : resultCase.children()) {
        if (child) {
            childIds.append(child->id());
        }
    }
    for (const QString& childId : childIds) {
        resultCase.removeChild(childId);
    }

    resultCase.setName(scan.caseName);
    resultCase.updateMetadata(scan.resultDirectory,
                              scan.smvFilePath,
                              scan.fdsInputFilePath,
                              scan.status,
                              static_cast<int>(scan.files.size()),
                              scan.startTime,
                              scan.endTime,
                              scan.scanTime,
                              scan.warnings.size());

    QHash<QString, std::shared_ptr<FcObjectGroup>> categories;
    for (const FdsResultFileInfo& file : scan.files) {
        const QString categoryName = resultFileCategoryName(file.type);
        std::shared_ptr<FcObjectGroup> category = categories.value(categoryName);
        if (!category) {
            category = std::make_shared<FcObjectGroup>(categoryName);
            categories.insert(categoryName, category);
            resultCase.addChild(category);
        }
        category->addChild(std::make_shared<FcResultFile>(file.name,
                                                         file.type,
                                                         file.filePath,
                                                         file.fileSize,
                                                         file.exists,
                                                         file.lastModified,
                                                         file.columnCount,
                                                         file.dataRowCount,
                                                         file.startTime,
                                                         file.endTime,
                                                         file.columnNames));
    }
}

FcGeometryKind geometryKindForIfcClass(const QString& ifcClass)
{
    const QString value = ifcClass.trimmed().toUpper();
    if (value == QStringLiteral("IFCWALL") || value == QStringLiteral("IFCWALLSTANDARDCASE"))
        return FcGeometryKind::Wall;
    if (value == QStringLiteral("IFCSLAB")) return FcGeometryKind::Slab;
    if (value == QStringLiteral("IFCROOF")) return FcGeometryKind::Roof;
    if (value == QStringLiteral("IFCCOLUMN")) return FcGeometryKind::Column;
    if (value == QStringLiteral("IFCBEAM")) return FcGeometryKind::Beam;
    if (value == QStringLiteral("IFCDOOR")) return FcGeometryKind::Door;
    if (value == QStringLiteral("IFCWINDOW")) return FcGeometryKind::Window;
    if (value == QStringLiteral("IFCOPENINGELEMENT"))
        return FcGeometryKind::RectangularOpening;
    if (value == QStringLiteral("IFCSTAIR") || value == QStringLiteral("IFCSTAIRFLIGHT"))
        return FcGeometryKind::Stair;
    if (value == QStringLiteral("IFCSPACE")) return FcGeometryKind::Room;
    return FcGeometryKind::Generic;
}

FcDisplayUnit displayUnitFromSetting(const QString& unit)
{
    if (unit == QStringLiteral("cm")) return FcDisplayUnit::Centimeters;
    if (unit == QStringLiteral("mm")) return FcDisplayUnit::Millimeters;
    if (unit == QStringLiteral("ft")) return FcDisplayUnit::Feet;
    if (unit == QStringLiteral("in")) return FcDisplayUnit::Inches;
    return FcDisplayUnit::Meters;
}

std::shared_ptr<FcGeometryObject> ifcConversionProxy(
    const std::shared_ptr<FcIfcObject>& ifc)
{
    if (!ifc || !ifc->hasShape()) return {};
    auto proxy = std::make_shared<FcGeometryObject>(ifc->name(), ifc->shape());
    proxy->setGeometryKind(geometryKindForIfcClass(ifc->ifcClass()));
    QVariantMap parameters;
    parameters.insert(QStringLiteral("sourceIfcUuid"), ifc->id());
    parameters.insert(QStringLiteral("sourceIfcGlobalId"), ifc->globalId());
    parameters.insert(QStringLiteral("sourceIfcClass"), ifc->ifcClass());
    parameters.insert(QStringLiteral("sourceFile"), ifc->sourceFile());
    QString conversionRoute = ifc->fdsConversionRoute().trimmed().toUpper();
    if (conversionRoute == QStringLiteral("HOLE")) {
        proxy->setGeometryKind(FcGeometryKind::RectangularOpening);
        conversionRoute = QStringLiteral("AUTO");
    }
    parameters.insert(QStringLiteral("fdsConversionRoute"), conversionRoute);
    proxy->setGeometryParameters(parameters);
    proxy->setVisible(ifc->isVisible());
    proxy->restorePersistentId(ifc->id());
    return proxy;
}

FloorEditorData floorEditorData(const FcFloorObject& floor)
{
    FloorEditorData data;
    data.name = floor.name();
    data.baseElevation = floor.baseElevation();
    data.storeyHeight = floor.defaultStoreyHeight();
    data.slabThickness = floor.defaultSlabThickness();
    data.wallHeight = floor.defaultWallHeight();
    data.backgroundImagePath = floor.backgroundImagePath();
    data.clippingEnabled = floor.clippingEnabled();
    data.clippingRange = floor.clippingRange();
    return data;
}

void applyFloorEditorData(FcFloorObject& floor, const FloorEditorData& data)
{
    floor.setName(data.name);
    floor.setFloorName(data.name);
    floor.setBaseElevation(data.baseElevation);
    floor.setDefaultStoreyHeight(data.storeyHeight);
    floor.setDefaultSlabThickness(data.slabThickness);
    floor.setDefaultWallHeight(data.wallHeight);
    floor.setBackgroundImagePath(data.backgroundImagePath);
    floor.setClippingEnabled(data.clippingEnabled);
    floor.setClippingRange(data.clippingRange);
}

void synchronizeFloorBackground(FcFloorObject& floor)
{
    std::shared_ptr<FcGeometryObject> background;
    for (const FcObject::Ptr& child : floor.children()) {
        const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(child);
        if (geometry && geometry->geometryKind() == FcGeometryKind::BackgroundImage &&
            geometry->geometryParameters()
                    .value(QStringLiteral("floorBackgroundOwnerUuid")).toString() == floor.id()) {
            background = geometry;
            break;
        }
    }
    if (!background) {
        background = std::make_shared<FcGeometryObject>(
            floor.name() + QStringLiteral(" Background"));
        background->setGeometryKind(FcGeometryKind::BackgroundImage);
        floor.addChild(background);
    }
    const FcFloorClipRange range = floor.clippingRange();
    const double xMin = floor.clippingEnabled() ? range.xMin : -10.0;
    const double xMax = floor.clippingEnabled() ? range.xMax : 10.0;
    const double yMin = floor.clippingEnabled() ? range.yMin : -10.0;
    const double yMax = floor.clippingEnabled() ? range.yMax : 10.0;
    const double thickness = 0.001;
    background->setName(floor.name() + QStringLiteral(" Background"));
    background->setFloorName(floor.name());
    background->setShape(BRepPrimAPI_MakeBox(
        gp_Pnt(xMin, yMin, floor.baseElevation() - thickness),
        std::max(xMax - xMin, thickness),
        std::max(yMax - yMin, thickness), thickness).Shape());
    QVariantMap parameters{
        {QStringLiteral("floorBackgroundOwnerUuid"), floor.id()},
        {QStringLiteral("resourcePath"), floor.backgroundImagePath()},
        {QStringLiteral("extension"), QFileInfo(floor.backgroundImagePath()).suffix().toLower()},
        {QStringLiteral("plane"), QStringLiteral("XY")},
        {QStringLiteral("x"), xMin}, {QStringLiteral("y"), yMin},
        {QStringLiteral("z"), floor.baseElevation() - thickness},
        {QStringLiteral("width"), xMax - xMin},
        {QStringLiteral("height"), yMax - yMin},
        {QStringLiteral("opacity"), 0.55},
        {QStringLiteral("conversion"), QStringLiteral("REFERENCE")},
        {QStringLiteral("fdsConversionRoute"), QStringLiteral("REFERENCE")}};
    QFile image(floor.backgroundImagePath());
    if (image.open(QIODevice::ReadOnly)) {
        parameters.insert(QStringLiteral("embeddedImageBase64"),
                          QString::fromLatin1(image.readAll().toBase64()));
    }
    background->setGeometryParameters(parameters);
    background->setVisible(!floor.backgroundImagePath().isEmpty());
    background->setLocked(true);
}

void copyGeometrySemantics(const FcGeometryObject& source,
                           FcGeometryObject& destination)
{
    destination.setGeometryKind(source.geometryKind());
    destination.setGeometryParameters(source.geometryParameters());
    destination.setHostObjectId(source.hostObjectId());
    destination.setControlObjectId(source.controlObjectId());
    destination.setDynamicOpening(source.isDynamicOpening());
    destination.setDefaultSurfaceId(source.defaultSurfaceId());
    destination.setFaceSurfaceIds(source.faceSurfaceIds());
    destination.setFloorName(source.floorName());
    destination.setTags(source.tags());
    destination.setVisible(source.isVisible());
}

bool updateCopiedGeometryParameters(const FcDocument& document,
                                    const FcGeometryObject& source,
                                    FcGeometryObject& copy, const gp_Trsf& transform,
                                    QString* error)
{
    QVector<std::shared_ptr<FcGeometryObject>> allGeometry;
    collectGeometryObjects(document.geometryGroup(), allGeometry);
    bool hasHostedObjects = !source.hostObjectId().isEmpty();
    for (const auto& geometry : allGeometry)
        hasHostedObjects = hasHostedObjects || geometry->hostObjectId() == source.id();
    if (hasHostedObjects) {
        *error = u("Copying hosted geometry requires rebuilding its host references. Copy the plain geometry and recreate its openings instead.");
        return false;
    }
    const auto before = BuildingGeometryService::requestFromParameters(
        source.geometryKind(), source.geometryParameters());
    if (GeometryEditService::handles(before).isEmpty()) return true;
    BuildingGeometryRequest after;
    if (!GeometryEditService::matchesShape(before, source.shape(), 1e-6, error) ||
        !GeometryEditService::transformRequest(before, transform, &after, error)) return false;
    QSet<QString> faceKeys;
    for (const auto& face : BuildingGeometryService::faceInfos(copy.shape())) faceKeys.insert(face.key);
    for (auto it = copy.faceSurfaceIds().cbegin(); it != copy.faceSurfaceIds().cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("TopoFace:")) && !faceKeys.contains(it.key())) {
            *error = u("Geometry changed a surface-assigned face. Reassign or clear the affected faces in Properties first.");
            return false;
        }
    }
    copy.setGeometryParameters(BuildingGeometryService::requestToParameters(after));
    return true;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_snapManager(std::make_unique<SnapManager>())
{
    UiLanguageManager::initialize();
    setWindowTitle(QStringLiteral("FireCAE"));
    setMinimumSize(kMinimumWindowWidth, kMinimumWindowHeight);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QSize initialSize(
            qMin(available.width(),
                 qMax(kMinimumWindowWidth,
                      qRound(available.width() * kInitialScreenFraction))),
            qMin(available.height(),
                 qMax(kMinimumWindowHeight,
                      qRound(available.height() * kInitialScreenFraction))));
        setGeometry(QStyle::alignedRect(Qt::LeftToRight,
                                        Qt::AlignCenter,
                                        initialSize,
                                        available));
    } else {
        resize(kWindowFallbackWidth, kWindowFallbackHeight);
    }
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);
    // A logical-pixel drag target remains discoverable next to the native OCCT view.
    // Keep this local so changing application theme does not remove the separator.
    setStyleSheet(QStringLiteral(
        "QMainWindow::separator { width: 7px; height: 7px; background: palette(midlight); }"
        "QMainWindow::separator:hover { background: palette(highlight); }"));

    m_undoStack = new QUndoStack(this);
    createActions();
    createCentralView();
    m_taskManager = new SimulationTaskManager(this);
    createDockWidgets();
    createMenus();
    createToolBars();
    connectActions();
    resetDefaultLayout();
    restoreSavedLayout();
    createApplicationStatusBar();
    createNewProject();

    const ApplicationSettings settings = ApplicationSettingsStore().load();
    applyApplicationSettings(settings);
    m_recoveryManager = std::make_unique<ProjectRecoveryManager>();
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(settings.autoSaveIntervalMinutes * 60 * 1000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        if (m_project && m_project->isModified()) performAutoSave(QStringLiteral("Timed auto-save"));
    });
    if (settings.autoSaveEnabled) {
        m_autoSaveTimer->start();
    }
    const QString executableName = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    if (!executableName.contains(QStringLiteral("Tests"), Qt::CaseInsensitive) &&
        !qEnvironmentVariableIsSet("FIRECAE_DISABLE_RECOVERY_PROMPT")) {
        QTimer::singleShot(0, this, &MainWindow::offerRecoveryOnStartup);
    }
}

MainWindow::~MainWindow()
{
    // Release every AIS presentation while the OCCT context and native view
    // are still alive.  Leaving a large multi-instance FDS scene for QObject
    // child teardown can make OCCT release selection owners after the Win32
    // view has already disappeared.
    // The window may already be closed; releasing presentations must not ask
    // the OpenGL driver to redraw its former rendering target.
    m_fdsSceneSynchronizer.reset();
    m_planSceneSynchronizer.reset();
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear(false);
    }
    if (m_planViewWidget && m_planViewWidget->displayManager()) {
        m_planViewWidget->displayManager()->clear(false);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!event) return;
    if (!confirmProjectReplacement(u("exiting FireCAE"))) {
        event->ignore();
        return;
    }
    saveCurrentLayout();
    clearCurrentRecovery();
    QMainWindow::closeEvent(event);
}

void MainWindow::createActions()
{
    m_newAction = new QAction(QStringLiteral("New"), this);
    m_newAction->setObjectName(QStringLiteral("NewProjectAction"));
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));

    m_openAction = new QAction(QStringLiteral("Open"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));

    m_saveAction = new QAction(QStringLiteral("Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));

    m_saveAsAction = new QAction(QStringLiteral("Save As"), this);
    m_saveAsAction->setObjectName(QStringLiteral("SaveProjectAsAction"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_importFdsAction = new QAction(QStringLiteral("Import FDS Input..."), this);
    m_exportFdsAction = new QAction(QStringLiteral("Export FDS Input..."), this);
    m_exportFdsAction->setObjectName(QStringLiteral("ExportFdsAction"));
    m_exitAction = new QAction(QStringLiteral("Exit"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_preferencesAction = new QAction(QStringLiteral("Preferences..."), this);
    m_preferencesAction->setObjectName(QStringLiteral("ApplicationPreferencesAction"));
    m_manageResourcesAction = new QAction(QStringLiteral("Project Resources..."), this);
    m_manageResourcesAction->setObjectName(QStringLiteral("ProjectResourcesAction"));
    m_packageProjectAction = new QAction(QStringLiteral("Package Project..."), this);
    m_packageProjectAction->setObjectName(QStringLiteral("PackageProjectAction"));
    m_unpackProjectAction = new QAction(QStringLiteral("Unpack Project..."), this);
    m_unpackProjectAction->setObjectName(QStringLiteral("UnpackProjectAction"));
    m_copyProjectAction = new QAction(QStringLiteral("Copy Project to Directory..."), this);
    m_copyProjectAction->setObjectName(QStringLiteral("CopyProjectAction"));
    m_cleanResultsAction = new QAction(QStringLiteral("Clean Unused Results..."), this);
    m_cleanResultsAction->setObjectName(QStringLiteral("CleanUnusedResultsAction"));
    m_startPageAction = new QAction(QStringLiteral("Start Page"), this);
    m_startPageAction->setObjectName(QStringLiteral("StartPageAction"));
    m_startPageAction->setVisible(false);

    m_undoAction = m_undoStack->createUndoAction(this, QStringLiteral("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoStack->createRedoAction(this, QStringLiteral("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_deleteAction = new QAction(QStringLiteral("Delete"), this);
    m_deleteAction->setObjectName(QStringLiteral("DeleteObjectAction"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_deleteAction->setEnabled(false);
    m_selectAllAction = new QAction(QStringLiteral("Select All Objects"), this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    m_invertSelectionAction = new QAction(QStringLiteral("Invert Selection"), this);
    m_invertSelectionAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    m_selectByTypeAction = new QAction(QStringLiteral("Select by Type..."), this);
    m_selectByPropertyAction = new QAction(QStringLiteral("Select by Property..."), this);
    m_selectByFloorAction = new QAction(QStringLiteral("Select by Floor/Group..."), this);
    m_transformAction = new QAction(QStringLiteral("Transform Selected..."), this);
    m_transformAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    m_transformGizmoAction = new QAction(QStringLiteral("Transform Gizmo"), this);
    m_transformGizmoAction->setCheckable(true);
    m_transformGizmoAction->setShortcut(QKeySequence(QStringLiteral("G")));
    m_copyMoveAction = new QAction(QStringLiteral("Copy and Move..."), this);
    m_copyMoveAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    m_arrayAction = new QAction(QStringLiteral("Array Copy..."), this);
    m_measureAction = new QAction(QStringLiteral("Measure Selection"), this);
    m_measureAction->setObjectName(QStringLiteral("MeasureSelectionAction"));
    m_measureAction->setShortcut(QKeySequence(QStringLiteral("M")));
    m_mirrorAction = new QAction(QStringLiteral("Mirror Selected..."), this);
    m_mirrorAction->setObjectName(QStringLiteral("MirrorSelectedAction"));
    m_alignAction = new QAction(QStringLiteral("Align Selected..."), this);
    m_alignAction->setObjectName(QStringLiteral("AlignSelectedAction"));
    m_copyToFloorAction = new QAction(QStringLiteral("Copy to Floor..."), this);
    m_copyToFloorAction->setObjectName(QStringLiteral("CopyToFloorAction"));
    m_groupAction = new QAction(QStringLiteral("Group Selected"), this);
    m_groupAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_createFolderAction = new QAction(QStringLiteral("New Geometry Folder..."), this);
    m_createFloorAction = new QAction(QStringLiteral("New Floor..."), this);
    m_createFloorAction->setObjectName(QStringLiteral("CreateFloorAction"));
    m_hideSelectedAction = new QAction(QStringLiteral("Hide Selected"), this);
    m_hideSelectedAction->setShortcut(QKeySequence(QStringLiteral("H")));
    m_lockSelectedAction = new QAction(QStringLiteral("Lock/Unlock Selected"), this);
    m_batchRenameAction = new QAction(QStringLiteral("Batch Rename..."), this);
    m_assignSurfacesAction = new QAction(QStringLiteral("Assign Surfaces..."), this);
    m_assignSurfacesAction->setObjectName(QStringLiteral("AssignSurfacesAction"));
    m_snapEnabledAction = new QAction(QStringLiteral("Snap Enabled"), this);
    m_snapEnabledAction->setCheckable(true);
    m_snapEnabledAction->setChecked(true);
    m_snapEnabledAction->setShortcut(QKeySequence(Qt::Key_F9));
    m_snapSettingsAction = new QAction(QStringLiteral("Snap Settings..."), this);
    for (QAction* action : {m_transformAction, m_transformGizmoAction,
                            m_copyMoveAction, m_arrayAction, m_measureAction,
                            m_mirrorAction, m_alignAction, m_copyToFloorAction,
                            m_groupAction, m_hideSelectedAction, m_lockSelectedAction,
                            m_batchRenameAction, m_assignSurfacesAction}) {
        action->setEnabled(false);
    }
    m_editObjectAction = new QAction(QStringLiteral("Edit Selected Object..."), this);
    m_editObjectAction->setShortcut(QKeySequence(Qt::Key_Return));
    m_editObjectAction->setEnabled(false);
    m_duplicateAction = new QAction(QStringLiteral("Duplicate Object"), this);
    m_duplicateAction->setObjectName(QStringLiteral("DuplicateObjectAction"));
    m_duplicateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    m_duplicateAction->setEnabled(false);
    m_moveUpAction = new QAction(QStringLiteral("Move Up"), this);
    m_moveUpAction->setObjectName(QStringLiteral("MoveObjectUpAction"));
    m_moveUpAction->setShortcut(QKeySequence(QStringLiteral("Alt+Up")));
    m_moveUpAction->setEnabled(false);
    m_moveDownAction = new QAction(QStringLiteral("Move Down"), this);
    m_moveDownAction->setObjectName(QStringLiteral("MoveObjectDownAction"));
    m_moveDownAction->setShortcut(QKeySequence(QStringLiteral("Alt+Down")));
    m_moveDownAction->setEnabled(false);

    m_selectAction = new QAction(QStringLiteral("Select"), this);
    m_selectAction->setObjectName(QStringLiteral("SelectionModeAction"));
    m_selectAction->setCheckable(true);
    m_selectAction->setChecked(true);
    m_fitAllAction = new QAction(QStringLiteral("Fit All"), this);
    m_restoreVisibilityAction = new QAction(QStringLiteral("Show All Objects"), this);
    m_restoreVisibilityAction->setObjectName(QStringLiteral("RestoreModelVisibilityAction"));
    m_fitSelectionAction = new QAction(QStringLiteral("Fit Selection"), this);
    m_perspectiveAction = new QAction(QStringLiteral("Perspective"), this);
    m_orthographicAction = new QAction(QStringLiteral("Orthographic"), this);
    m_perspectiveAction->setCheckable(true);
    m_orthographicAction->setCheckable(true);
    m_orthographicAction->setChecked(true);
    auto* projectionGroup = new QActionGroup(this);
    projectionGroup->setExclusive(true);
    projectionGroup->addAction(m_perspectiveAction);
    projectionGroup->addAction(m_orthographicAction);
    m_saveViewAction = new QAction(QStringLiteral("Save View"), this);
    m_restoreViewAction = new QAction(QStringLiteral("Restore View"), this);
    m_rotationCenterAction = new QAction(QStringLiteral("Set Rotation Center..."), this);
    m_axesAction = new QAction(QStringLiteral("World Axes"), this);
    m_axesAction->setCheckable(true);
    m_axesAction->setChecked(false);
    m_backgroundAction = new QAction(QStringLiteral("Background Color..."), this);
    m_clippingPlaneAction = new QAction(QStringLiteral("Clipping Plane..."), this);
    m_frontAction = new QAction(QStringLiteral("Front"), this);
    m_backAction = new QAction(QStringLiteral("Back"), this);
    m_leftAction = new QAction(QStringLiteral("Left"), this);
    m_rightAction = new QAction(QStringLiteral("Right"), this);
    m_topAction = new QAction(QStringLiteral("Top"), this);
    m_bottomAction = new QAction(QStringLiteral("Bottom"), this);
    m_isometricAction = new QAction(QStringLiteral("Isometric"), this);
    m_resetLayoutAction = new QAction(QStringLiteral("Reset Layout"), this);
    m_resetLayoutAction->setObjectName(QStringLiteral("ResetLayoutAction"));

    m_createBoxAction = new QAction(QStringLiteral("Create Box"), this);
    m_directEditAction = new QAction(u("Edit Dimensions in View"), this);
    m_directEditAction->setObjectName(QStringLiteral("DirectGeometryEditAction"));
    m_directEditAction->setCheckable(true);
    m_directEditAction->setEnabled(false);
    m_createWallAction = new QAction(QStringLiteral("Create Wall"), this);
    m_createWallAction->setData(static_cast<int>(FcGeometryKind::Wall));
    m_drawWallAction = new QAction(QStringLiteral("Draw Wall in View..."), this);
    m_drawWallAction->setObjectName(QStringLiteral("DrawWallInViewAction"));
    m_createRoomAction = new QAction(QStringLiteral("Create Room"), this);
    m_createRoomAction->setData(static_cast<int>(FcGeometryKind::Room));
    const auto createBuildingAction = [this](const char* text, FcGeometryKind kind) {
        auto* action = new QAction(QString::fromLatin1(text), this);
        action->setData(static_cast<int>(kind));
        action->setProperty("firecaeEnglishText", QString::fromLatin1(text));
        m_buildingGeometryActions.append(action);
        return action;
    };
    createBuildingAction("Create Slab", FcGeometryKind::Slab);
    createBuildingAction("Create Roof", FcGeometryKind::Roof);
    createBuildingAction("Create Column", FcGeometryKind::Column);
    createBuildingAction("Create Beam", FcGeometryKind::Beam);
    createBuildingAction("Create Polygon", FcGeometryKind::PolygonPrism);
    createBuildingAction("Create Polyline Sweep", FcGeometryKind::PolylineSweep);
    createBuildingAction("Create Circle / Cylinder", FcGeometryKind::Cylinder);
    createBuildingAction("Create Rectangle Profile", FcGeometryKind::RectangleProfile);
    createBuildingAction("Extrude 2D Profile", FcGeometryKind::ProfileExtrusion);
    createBuildingAction("Sweep Along Path", FcGeometryKind::PathSweep);
    createBuildingAction("Create Stair", FcGeometryKind::Stair);
    createBuildingAction("Create Ramp", FcGeometryKind::Ramp);
    createBuildingAction("Create Rectangular Opening", FcGeometryKind::RectangularOpening);
    createBuildingAction("Create Polygonal Opening", FcGeometryKind::PolygonalOpening);
    createBuildingAction("Create Door", FcGeometryKind::Door);
    createBuildingAction("Create Window", FcGeometryKind::Window);
    createBuildingAction("Create Slab Opening", FcGeometryKind::SlabOpening);
    createBuildingAction("Create Wall Vent", FcGeometryKind::WallVent);
    m_previewFdsBlocksAction = new QAction(QStringLiteral("Preview FDS Blocks..."), this);
    m_convertGeometryToFdsAction = new QAction(QStringLiteral("Generate FDS Blocks"), this);
    m_convertGeometryToFdsAction->setObjectName(
        QStringLiteral("ConvertGeometryToFdsAction"));
    m_healGeometryAction = new QAction(QStringLiteral("Heal Selected Geometry"), this);
    m_unionGeometryAction = new QAction(QStringLiteral("Boolean Union"), this);
    m_cutGeometryAction = new QAction(QStringLiteral("Boolean Difference"), this);
    m_intersectGeometryAction = new QAction(QStringLiteral("Boolean Intersection"), this);
    m_splitGeometryAction = new QAction(QStringLiteral("Split with Selected Tool"), this);
    m_backgroundImageAction = new QAction(QStringLiteral("Import Background Image..."), this);
    m_importGeometryAction = new QAction(QStringLiteral("Import Geometry"), this);
    m_importGeometryAction->setObjectName(QStringLiteral("ImportGeometryAction"));
    m_importGeometryAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));

    m_projectSettingsAction = new QAction(QStringLiteral("Project Settings..."), this);
    m_projectSettingsAction->setObjectName(QStringLiteral("ProjectSettingsAction"));
    m_simulationParametersAction = new QAction(
        QStringLiteral("Simulation Parameters..."), this);
    m_simulationParametersAction->setObjectName(
        QStringLiteral("SimulationParametersAction"));
    m_meshAction = new QAction(QStringLiteral("Mesh"), this);
    m_meshAction->setObjectName(QStringLiteral("MeshAction"));
    m_speciesAction = new QAction(QStringLiteral("Species"), this);
    m_materialAction = new QAction(QStringLiteral("Material"), this);
    m_materialAction->setObjectName(QStringLiteral("MaterialAction"));
    m_surfaceAction = new QAction(QStringLiteral("Surface"), this);
    m_reactionAction = new QAction(QStringLiteral("Reaction"), this);
    m_particleAction = new QAction(QStringLiteral("Particle"), this);
    m_ventAction = new QAction(QStringLiteral("Vent"), this);
    m_deviceAction = new QAction(QStringLiteral("Device"), this);
    m_controlAction = new QAction(QStringLiteral("Control"), this);
    m_hvacAction = new QAction(QStringLiteral("HVAC"), this);
    m_initialConditionAction = new QAction(QStringLiteral("Initial Condition"), this);
    m_outputAction = new QAction(QStringLiteral("Output"), this);
    m_addFdsObjectAction = new QAction(QStringLiteral("Add FDS Object..."), this);
    m_addFdsObjectAction->setObjectName(QStringLiteral("AddFdsObjectAction"));
    m_meshEngineeringAction = new QAction(
        QStringLiteral("Mesh Engineering Assistant..."), this);
    m_meshEngineeringAction->setObjectName(QStringLiteral("MeshEngineeringAction"));
    m_fireSourceWizardAction = new QAction(QStringLiteral("Fire Source Wizard..."), this);
    m_fireSourceWizardAction->setObjectName(QStringLiteral("FireSourceWizardAction"));
    m_particleSprayWizardAction = new QAction(
        QStringLiteral("Particle and Sprinkler Wizard..."), this);
    m_particleSprayWizardAction->setObjectName(
        QStringLiteral("ParticleSprayWizardAction"));
    m_deviceControlWizardAction = new QAction(
        QStringLiteral("Device and Control Wizard..."), this);
    m_deviceControlWizardAction->setObjectName(
        QStringLiteral("DeviceControlWizardAction"));
    m_outputWizardAction = new QAction(QStringLiteral("Output Wizard..."), this);
    m_outputWizardAction->setObjectName(QStringLiteral("OutputWizardAction"));
    m_controlGraphAction = new QAction(QStringLiteral("Control Logic Graph..."), this);
    m_controlGraphAction->setObjectName(QStringLiteral("ControlLogicGraphAction"));
    m_hvacGraphAction = new QAction(QStringLiteral("HVAC Network Graph..."), this);
    m_hvacGraphAction->setObjectName(QStringLiteral("HvacNetworkGraphAction"));
    m_propertyLibraryAction = new QAction(QStringLiteral("FDS Property Library..."), this);
    m_propertyLibraryAction->setObjectName(QStringLiteral("FdsPropertyLibraryAction"));
    m_scenarioManagerAction = new QAction(QStringLiteral("Scenario Manager..."), this);
    m_scenarioManagerAction->setObjectName(QStringLiteral("ScenarioManagerAction"));
    m_runAllScenariosAction = new QAction(QStringLiteral("Run All Scenarios..."), this);
    m_runAllScenariosAction->setObjectName(QStringLiteral("RunAllScenariosAction"));
    m_simpleTestAction = new QAction(QStringLiteral("Create Simple Test Benchmark"), this);
    m_activateVentsAction = new QAction(
        QStringLiteral("Create activate_vents Tutorial"), this);
    m_activateVentsAction->setObjectName(QStringLiteral("ActivateVentsTutorialAction"));
    m_bucketTest2Action = new QAction(
        QStringLiteral("Create bucket_test_2 Tutorial"), this);
    m_bucketTest2Action->setObjectName(QStringLiteral("BucketTest2TutorialAction"));
    m_couchAction = new QAction(QStringLiteral("Create couch Tutorial"), this);
    m_couchAction->setObjectName(QStringLiteral("CouchTutorialAction"));
    m_couchSmoke12sAction = new QAction(
        QStringLiteral("Create couch_smoke_12s Tutorial"), this);
    m_couchSmoke12sAction->setObjectName(QStringLiteral("CouchSmoke12sTutorialAction"));
    m_hvacAircoilAction = new QAction(
        QStringLiteral("Create HVAC_aircoil Tutorial"), this);
    m_hvacAircoilAction->setObjectName(QStringLiteral("HvacAircoilTutorialAction"));
    m_tunnelDemoAction = new QAction(
        QStringLiteral("Create tunnel_demo Tutorial"), this);
    m_tunnelDemoAction->setObjectName(QStringLiteral("TunnelDemoTutorialAction"));
    m_tunnelSmoke10sAction = new QAction(
        QStringLiteral("Create tunnel_smoke_10s Tutorial"), this);
    m_tunnelSmoke10sAction->setObjectName(QStringLiteral("TunnelSmoke10sTutorialAction"));
    for (const TutorialDefinition& tutorial : TutorialGuideWidget::catalog()) {
        auto* action = new QAction(tutorial.title, this);
        action->setObjectName(QStringLiteral("GuidedTutorial_%1").arg(tutorial.id));
        action->setData(tutorial.id);
        action->setToolTip(tutorial.purpose);
        m_guidedTutorialActions.append(action);
    }

    m_runAction = new QAction(QStringLiteral("Run Current Project"), this);
    m_runAction->setObjectName(QStringLiteral("RunFdsAction"));
    m_runAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_runOpenMpAction = new QAction(
        QStringLiteral("Run Current Project with CPU/OpenMP..."), this);
    m_runOpenMpAction->setObjectName(QStringLiteral("RunFdsOpenMpAction"));
    m_runParallelAction = new QAction(
        QStringLiteral("Run Current Project with CPU/MPI..."), this);
    m_runParallelAction->setObjectName(QStringLiteral("RunFdsParallelAction"));
    m_stopAction = new QAction(QStringLiteral("Stop FDS"), this);
    m_stopAction->setObjectName(QStringLiteral("StopFdsAction"));
    m_stopAction->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopAction->setEnabled(false);
    m_validateAction = new QAction(QStringLiteral("Validate Model"), this);
    m_validateAction->setObjectName(QStringLiteral("ValidateModelAction"));

    m_openResultsAction = new QAction(QStringLiteral("Open FDS Results..."), this);
    m_openResultsAction->setObjectName(QStringLiteral("OpenResultsAction"));
    m_compareResultsAction = new QAction(QStringLiteral("Compare FDS Results..."), this);
    m_compareResultsAction->setObjectName(QStringLiteral("CompareFdsResultsAction"));
    m_reloadResultsAction = new QAction(QStringLiteral("Reload Results"), this);
    m_closeResultsAction = new QAction(QStringLiteral("Close Results"), this);
    m_openSmokeviewAction = new QAction(QStringLiteral("Open in Smokeview"), this);
    m_openNativeResultsAction = new QAction(QStringLiteral("Open in Native Result Viewer"), this);
    m_openNativeResultsAction->setObjectName(QStringLiteral("OpenNativeResultsAction"));
    m_openSmokeAnimationAction = new QAction(QStringLiteral("Open Smoke/Fire Animation"), this);
    m_openSliceAnimationAction = new QAction(QStringLiteral("Open Slice Animation"), this);
    m_openParticleAnimationAction = new QAction(QStringLiteral("Open Particle Animation"), this);
    m_configureSmokeviewAction = new QAction(QStringLiteral("Configure Smokeview..."), this);
    m_reloadResultsAction->setEnabled(false);
    m_closeResultsAction->setEnabled(false);
    m_openSmokeviewAction->setEnabled(false);
    m_openNativeResultsAction->setEnabled(false);
    m_openSmokeAnimationAction->setEnabled(false);
    m_openSliceAnimationAction->setEnabled(false);
    m_openParticleAnimationAction->setEnabled(false);
    m_languageActionGroup = new QActionGroup(this);
    m_languageActionGroup->setExclusive(true);
    m_englishAction = new QAction(QStringLiteral("English"), m_languageActionGroup);
    m_chineseAction = new QAction(QStringLiteral("Simplified Chinese"), m_languageActionGroup);
    m_englishAction->setCheckable(true);
    m_chineseAction->setCheckable(true);
    (UiLanguageManager::currentLanguage() == UiLanguage::ChineseSimplified
         ? m_chineseAction
         : m_englishAction)->setChecked(true);
    m_aboutAction = new QAction(QStringLiteral("About FireCAE"), this);
    m_diagnosticsAction = new QAction(QStringLiteral("Diagnostics..."), this);
    m_diagnosticsAction->setObjectName(QStringLiteral("DiagnosticsAction"));

    retranslateUi();
}

void MainWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    m_fileMenu->addAction(m_newAction);
    m_fileMenu->addAction(m_openAction);
    m_fileMenu->addAction(m_saveAction);
    m_fileMenu->addAction(m_saveAsAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_manageResourcesAction);
    m_fileMenu->addAction(m_packageProjectAction);
    m_fileMenu->addAction(m_unpackProjectAction);
    m_fileMenu->addAction(m_copyProjectAction);
    m_fileMenu->addAction(m_cleanResultsAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_importFdsAction);
    m_fileMenu->addAction(m_exportFdsAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    m_editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    m_editMenu->addAction(m_undoAction);
    m_editMenu->addAction(m_redoAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_selectAllAction);
    m_editMenu->addAction(m_invertSelectionAction);
    m_editMenu->addAction(m_selectByTypeAction);
    m_editMenu->addAction(m_selectByPropertyAction);
    m_editMenu->addAction(m_selectByFloorAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_snapEnabledAction);
    m_editMenu->addAction(m_snapSettingsAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_transformAction);
    m_editMenu->addAction(m_transformGizmoAction);
    m_editMenu->addAction(m_copyMoveAction);
    m_editMenu->addAction(m_arrayAction);
    m_editMenu->addAction(m_mirrorAction);
    m_editMenu->addAction(m_alignAction);
    m_editMenu->addAction(m_copyToFloorAction);
    m_editMenu->addAction(m_measureAction);
    m_editMenu->addAction(m_createFolderAction);
    m_editMenu->addAction(m_createFloorAction);
    m_editMenu->addAction(m_groupAction);
    m_editMenu->addAction(m_hideSelectedAction);
    m_editMenu->addAction(m_lockSelectedAction);
    m_editMenu->addAction(m_batchRenameAction);
    m_editMenu->addAction(m_assignSurfacesAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_editObjectAction);
    m_editMenu->addAction(m_duplicateAction);
    m_editMenu->addAction(m_moveUpAction);
    m_editMenu->addAction(m_moveDownAction);
    m_editMenu->addAction(m_deleteAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_preferencesAction);

    m_viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    m_viewMenu->addAction(m_modelTreeDock->toggleViewAction());
    m_viewMenu->addAction(m_propertiesDock->toggleViewAction());
    m_viewMenu->addAction(m_messagesDock->toggleViewAction());
    m_viewMenu->addAction(m_taskCenterDock->toggleViewAction());
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_fitAllAction);
    m_viewMenu->addAction(m_fitSelectionAction);
    m_viewMenu->addAction(m_restoreVisibilityAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_perspectiveAction);
    m_viewMenu->addAction(m_orthographicAction);
    m_viewMenu->addAction(m_frontAction);
    m_viewMenu->addAction(m_backAction);
    m_viewMenu->addAction(m_leftAction);
    m_viewMenu->addAction(m_rightAction);
    m_viewMenu->addAction(m_topAction);
    m_viewMenu->addAction(m_bottomAction);
    m_viewMenu->addAction(m_isometricAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_saveViewAction);
    m_viewMenu->addAction(m_restoreViewAction);
    m_viewMenu->addAction(m_rotationCenterAction);
    m_viewMenu->addAction(m_axesAction);
    m_viewMenu->addAction(m_backgroundAction);
    m_viewMenu->addAction(m_clippingPlaneAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_resetLayoutAction);

    m_geometryMenu = menuBar()->addMenu(QStringLiteral("Geometry"));
    m_geometryMenu->addAction(m_createBoxAction);
    m_geometryMenu->addAction(m_createWallAction);
    m_geometryMenu->addAction(m_drawWallAction);
    m_geometryMenu->addAction(m_createRoomAction);
    const auto buildingAction = [this](FcGeometryKind kind) -> QAction* {
        for (QAction* action : m_buildingGeometryActions) {
            if (action && action->data().toInt() == static_cast<int>(kind)) return action;
        }
        return nullptr;
    };
    m_buildingElementsMenu = m_geometryMenu->addMenu(QStringLiteral("Building Elements"));
    for (FcGeometryKind kind : {FcGeometryKind::Slab, FcGeometryKind::Roof,
                                FcGeometryKind::Column, FcGeometryKind::Beam,
                                FcGeometryKind::Stair, FcGeometryKind::Ramp}) {
        m_buildingElementsMenu->addAction(buildingAction(kind));
    }
    m_profileGeometryMenu = m_geometryMenu->addMenu(QStringLiteral("Profiles and Sweeps"));
    for (FcGeometryKind kind : {FcGeometryKind::PolygonPrism,
                                FcGeometryKind::PolylineSweep,
                                FcGeometryKind::Cylinder,
                                FcGeometryKind::RectangleProfile,
                                FcGeometryKind::ProfileExtrusion,
                                FcGeometryKind::PathSweep}) {
        m_profileGeometryMenu->addAction(buildingAction(kind));
    }
    m_openingsMenu = m_geometryMenu->addMenu(QStringLiteral("Openings"));
    for (FcGeometryKind kind : {FcGeometryKind::RectangularOpening,
                                FcGeometryKind::PolygonalOpening,
                                FcGeometryKind::Door, FcGeometryKind::Window,
                                FcGeometryKind::SlabOpening,
                                FcGeometryKind::WallVent}) {
        m_openingsMenu->addAction(buildingAction(kind));
    }
    m_geometryMenu->addAction(m_assignSurfacesAction);
    m_geometryOperationsMenu = m_geometryMenu->addMenu(QStringLiteral("Geometry Operations"));
    m_geometryOperationsMenu->addAction(m_unionGeometryAction);
    m_geometryOperationsMenu->addAction(m_cutGeometryAction);
    m_geometryOperationsMenu->addAction(m_intersectGeometryAction);
    m_geometryOperationsMenu->addAction(m_splitGeometryAction);
    m_geometryOperationsMenu->addSeparator();
    m_geometryOperationsMenu->addAction(m_healGeometryAction);
    m_geometryMenu->addSeparator();
    m_geometryMenu->addAction(m_previewFdsBlocksAction);
    m_geometryMenu->addAction(m_convertGeometryToFdsAction);
    m_geometryMenu->addSeparator();
    m_geometryMenu->addAction(m_backgroundImageAction);
    m_geometryMenu->addAction(m_importGeometryAction);

    m_modelMenu = menuBar()->addMenu(QStringLiteral("Model"));
    m_modelMenu->addAction(m_projectSettingsAction);
    m_modelMenu->addSeparator();
    m_modelMenu->addAction(m_meshAction);
    m_modelMenu->addAction(m_speciesAction);
    m_modelMenu->addAction(m_materialAction);
    m_modelMenu->addAction(m_surfaceAction);
    m_modelMenu->addAction(m_reactionAction);
    m_modelMenu->addAction(m_particleAction);
    m_modelMenu->addAction(m_ventAction);
    m_modelMenu->addAction(m_deviceAction);
    m_modelMenu->addAction(m_controlAction);
    m_modelMenu->addAction(m_hvacAction);
    m_modelMenu->addAction(m_initialConditionAction);
    m_modelMenu->addAction(m_outputAction);
    m_modelMenu->addSeparator();
    m_modelMenu->addAction(m_addFdsObjectAction);
    m_modelAssistantsMenu = m_modelMenu->addMenu(QStringLiteral("Professional Assistants"));
    m_modelAssistantsMenu->addAction(m_meshEngineeringAction);
    m_modelAssistantsMenu->addSeparator();
    m_modelAssistantsMenu->addAction(m_fireSourceWizardAction);
    m_modelAssistantsMenu->addAction(m_particleSprayWizardAction);
    m_modelAssistantsMenu->addAction(m_deviceControlWizardAction);
    m_modelAssistantsMenu->addAction(m_outputWizardAction);
    m_modelAssistantsMenu->addSeparator();
    m_modelAssistantsMenu->addAction(m_controlGraphAction);
    m_modelAssistantsMenu->addAction(m_hvacGraphAction);
    m_modelAssistantsMenu->addSeparator();
    m_modelAssistantsMenu->addAction(m_propertyLibraryAction);
    m_modelMenu->addSeparator();
    m_modelMenu->addAction(m_simpleTestAction);
    m_tutorialMenu = m_modelMenu->addMenu(QStringLiteral("Tutorials"));
    for (QAction* action : m_guidedTutorialActions) m_tutorialMenu->addAction(action);
    m_tutorialMenu->addSeparator();
    m_tutorialExamplesMenu = m_tutorialMenu->addMenu(
        QStringLiteral("Completed Examples (Reference)"));
    m_tutorialExamplesMenu->addAction(m_activateVentsAction);
    m_tutorialExamplesMenu->addAction(m_bucketTest2Action);
    m_tutorialExamplesMenu->addAction(m_couchAction);
    m_tutorialExamplesMenu->addAction(m_couchSmoke12sAction);
    m_tutorialExamplesMenu->addAction(m_hvacAircoilAction);
    m_tutorialExamplesMenu->addAction(m_tunnelDemoAction);
    m_tutorialExamplesMenu->addAction(m_tunnelSmoke10sAction);

    m_simulationMenu = menuBar()->addMenu(QStringLiteral("Simulation"));
    m_simulationMenu->addAction(m_simulationParametersAction);
    m_simulationMenu->addSeparator();
    m_simulationMenu->addAction(m_scenarioManagerAction);
    m_simulationMenu->addAction(m_runAllScenariosAction);
    m_simulationMenu->addSeparator();
    m_simulationMenu->addAction(m_runAction);
    m_simulationMenu->addAction(m_runOpenMpAction);
    m_simulationMenu->addAction(m_runParallelAction);
    m_simulationMenu->addAction(m_validateAction);
    m_simulationMenu->addSeparator();
    m_simulationMenu->addAction(m_stopAction);

    m_resultsMenu = menuBar()->addMenu(QStringLiteral("Results"));
    m_resultsMenu->addAction(m_openResultsAction);
    m_resultsMenu->addAction(m_compareResultsAction);
    m_resultsMenu->addAction(m_reloadResultsAction);
    m_resultsMenu->addAction(m_closeResultsAction);
    m_resultsMenu->addSeparator();
    m_resultsMenu->addAction(m_openSmokeviewAction);
    m_resultsMenu->addSeparator();
    m_resultsMenu->addAction(m_configureSmokeviewAction);

    m_languageMenu = menuBar()->addMenu(QStringLiteral("Language"));
    m_languageMenu->addAction(m_chineseAction);
    m_languageMenu->addAction(m_englishAction);

    m_helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    m_helpMenu->addAction(m_diagnosticsAction);
    m_helpMenu->addSeparator();
    m_helpMenu->addAction(m_aboutAction);
    retranslateUi();
}

void MainWindow::createToolBars()
{
    m_mainToolBar = addToolBar(u("Main Toolbar"));
    m_mainToolBar->setObjectName(QStringLiteral("MainToolbar"));
    m_mainToolBar->setMovable(true);

    m_mainToolBar->addAction(m_newAction);
    m_mainToolBar->addAction(m_openAction);
    m_mainToolBar->addAction(m_saveAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_selectAction);
    m_mainToolBar->addAction(m_fitAllAction);
    m_mainToolBar->addAction(m_restoreVisibilityAction);
    m_mainToolBar->addAction(m_transformAction);
    m_mainToolBar->addAction(m_transformGizmoAction);
    m_mainToolBar->addAction(m_snapEnabledAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_importGeometryAction);
    m_mainToolBar->addAction(m_addFdsObjectAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_validateAction);
    m_mainToolBar->addAction(m_runAction);
    m_mainToolBar->addAction(m_stopAction);
    m_modelingToolBar = new QToolBar(u("Modeling Tools"), this);
    m_modelingToolBar->setObjectName(QStringLiteral("ModelingToolbar"));
    m_modelingToolBar->setMovable(true);
    m_modelingToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBar(Qt::LeftToolBarArea, m_modelingToolBar);
    m_modelingToolBar->addAction(m_drawWallAction);
    m_modelingToolBar->addAction(m_createBoxAction);
    for (QAction* action : m_buildingGeometryActions) {
        const auto kind = static_cast<FcGeometryKind>(action->data().toInt());
        if (kind == FcGeometryKind::Slab || kind == FcGeometryKind::PolygonPrism ||
            kind == FcGeometryKind::RectangularOpening) m_modelingToolBar->addAction(action);
    }
    m_modelingToolBar->addSeparator();
    for (QAction* action : {m_directEditAction, m_transformAction, m_assignSurfacesAction, m_measureAction})
        m_modelingToolBar->addAction(action);
    for (QAction* action : m_modelingToolBar->actions()) {
        if (!action->isSeparator()) action->setToolTip(action->text());
    }
    retranslateUi();
}

void MainWindow::createDockWidgets()
{
    constexpr auto dockFeatures = QDockWidget::DockWidgetMovable |
                                  QDockWidget::DockWidgetFloatable |
                                  QDockWidget::DockWidgetClosable;

    m_modelTreeDock = new QDockWidget(QStringLiteral("Model Tree"), this);
    m_modelTreeDock->setObjectName(QStringLiteral("ModelTreeDock"));
    m_modelTreeDock->setFeatures(dockFeatures);
    m_modelTreeWidget = new ModelTreeWidget(m_modelTreeDock);
    m_modelTreeWidget->setMinimumWidth(kLeftDockMinimumWidth);
    m_modelTreeDock->setWidget(m_modelTreeWidget);
    m_modelTreeDock->setMinimumWidth(kLeftDockMinimumWidth);

    m_propertiesDock = new QDockWidget(QStringLiteral("Properties"), this);
    m_propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_propertiesDock->setFeatures(dockFeatures);
    m_propertiesWidget = new PropertiesWidget(m_propertiesDock);
    m_propertiesWidget->setMinimumWidth(kLeftDockMinimumWidth);
    m_propertiesDock->setWidget(m_propertiesWidget);
    m_propertiesDock->setMinimumWidth(kLeftDockMinimumWidth);

    m_messagesDock = new QDockWidget(QStringLiteral("Messages"), this);
    m_messagesDock->setObjectName(QStringLiteral("MessagesDock"));
    m_messagesDock->setFeatures(dockFeatures);
    m_messageWidget = new MessageWidget(m_messagesDock);
    m_messageWidget->setObjectActivationHandler([this](const QString& objectId) {
        if (m_modelTreeWidget && m_modelTreeWidget->selectObjectById(objectId, true)) {
            statusBar()->showMessage(u("Selected validation object."),
                                     kStatusMessageDurationMs);
        }
    });
    m_messagesDock->setWidget(m_messageWidget);
    m_messageWidget->appendMessage(QStringLiteral("FireCAE started."));

    m_taskCenterDock = new QDockWidget(QStringLiteral("Simulation Task Center"), this);
    m_taskCenterDock->setObjectName(QStringLiteral("SimulationTaskCenterDock"));
    m_taskCenterDock->setFeatures(dockFeatures);
    m_taskCenterWidget = new SimulationTaskCenterWidget(m_taskCenterDock);
    m_taskCenterWidget->setManager(m_taskManager);
    m_taskCenterDock->setWidget(m_taskCenterWidget);

    m_inspectorDock = new QDockWidget(QStringLiteral("Workspace Inspector"), this);
    m_inspectorDock->setObjectName(QStringLiteral("WorkspaceInspectorDock"));
    m_inspectorDock->setFeatures(dockFeatures);
    m_inspectorTabs = new QTabWidget(m_inspectorDock);
    m_inspectorTabs->setObjectName(QStringLiteral("WorkspaceInspectorTabs"));

    auto* drawingPage = new QWidget(m_inspectorTabs);
    auto* drawingLayout = new QVBoxLayout(drawingPage);
    m_activeToolLabel = new QLabel(drawingPage);
    m_activeToolLabel->setObjectName(QStringLiteral("ActiveDrawingToolLabel"));
    m_activeToolLabel->setWordWrap(true);
    m_activeToolLabel->setText(u("Active tool: Selection"));
    auto* drawingHelp = new QLabel(
        u("Drawing dimensions and snapping are edited in the active tool dialog."),
        drawingPage);
    drawingHelp->setWordWrap(true);
    auto* selectionModeButton = new QPushButton(u("Return to Selection"), drawingPage);
    selectionModeButton->setObjectName(QStringLiteral("InspectorSelectionModeButton"));
    connect(selectionModeButton, &QPushButton::clicked, m_selectAction,
            &QAction::trigger);
    drawingLayout->addWidget(m_activeToolLabel);
    drawingLayout->addWidget(drawingHelp);
    drawingLayout->addWidget(selectionModeButton);
    drawingLayout->addStretch();

    auto* selectionPage = new QWidget(m_inspectorTabs);
    auto* selectionLayout = new QVBoxLayout(selectionPage);
    m_selectionList = new QListWidget(selectionPage);
    m_selectionList->setObjectName(QStringLiteral("WorkspaceSelectionList"));
    m_selectionList->setSelectionMode(QAbstractItemView::SingleSelection);
    selectionLayout->addWidget(m_selectionList);
    connect(m_selectionList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (!item || !m_modelTreeWidget) return;
                m_modelTreeWidget->selectObjectById(
                    item->data(Qt::UserRole).toString(), true);
            });

    auto* validationPage = new QWidget(m_inspectorTabs);
    auto* validationLayout = new QVBoxLayout(validationPage);
    m_validationList = new QListWidget(validationPage);
    m_validationList->setObjectName(QStringLiteral("WorkspaceValidationList"));
    m_validationList->setWordWrap(true);
    validationLayout->addWidget(m_validationList);
    connect(m_validationList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                if (!item || !m_project || !m_project->document()) return;
                const QString objectId = item->data(Qt::UserRole).toString();
                const QString parameterKey = item->data(Qt::UserRole + 1).toString();
                if (objectId.isEmpty()) return;
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                if (!object) return;
                m_modelTreeWidget->selectObjectById(objectId, true);
                m_propertiesWidget->showObject(object.get());
                statusBar()->showMessage(
                    parameterKey.isEmpty()
                        ? QStringLiteral("UUID %1").arg(objectId)
                        : QStringLiteral("UUID %1 — %2").arg(objectId, parameterKey),
                    kStatusMessageDurationMs);
            });
    connect(m_validationList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (!item || !m_project || !m_project->document()) return;
                const QString objectId = item->data(Qt::UserRole).toString();
                const QString parameterKey = item->data(Qt::UserRole + 1).toString();
                const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(
                    m_project->document()->findObject(objectId));
                if (!namelist) return;
                QTimer::singleShot(0, this, [parameterKey]() {
                    for (QWidget* widget : QApplication::topLevelWidgets()) {
                        if (widget->objectName() != QStringLiteral("FdsObjectEditorDialog")) continue;
                        if (QWidget* field = widget->findChild<QWidget*>(
                                QStringLiteral("Schema_%1").arg(parameterKey))) {
                            field->setFocus();
                            field->ensurePolished();
                            return;
                        }
                        auto* table = widget->findChild<QTableWidget*>(
                            QStringLiteral("FdsParameterTable"));
                        if (!table) return;
                        for (int row = 0; row < table->rowCount(); ++row) {
                            if (table->item(row, 0) &&
                                table->item(row, 0)->text().compare(
                                    parameterKey, Qt::CaseInsensitive) == 0) {
                                table->selectRow(row);
                                table->setFocus();
                                return;
                            }
                        }
                    }
                });
                editFdsObject(objectId);
            });

    m_inspectorTabs->addTab(drawingPage, u("Drawing Tool"));
    m_inspectorTabs->addTab(selectionPage, u("Selection Set"));
    m_inspectorTabs->addTab(validationPage, u("Validation Issues"));
    m_inspectorDock->setWidget(m_inspectorTabs);

    // These are ordinary, user-dockable panels. Repair only unreadable or
    // off-screen floating geometry, including geometry from older settings.
    for (QDockWidget* dock : {m_messagesDock, m_inspectorDock}) {
        connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
            if (!floating) dock->setMinimumSize(0, 0);
            QTimer::singleShot(0, this, [this, dock]() {
                ensureFloatingFeedbackDockGeometry(dock);
            });
        });
        connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible) {
            if (!visible) return;
            QTimer::singleShot(0, this, [this, dock]() {
                ensureFloatingFeedbackDockGeometry(dock);
            });
        });
    }

    m_fdsOutputDock = new QDockWidget(QStringLiteral("FDS Output"), this);
    m_fdsOutputDock->setObjectName(QStringLiteral("FdsOutputDock"));
    m_fdsOutputDock->setFeatures(dockFeatures);
    m_fdsOutputLog = new QPlainTextEdit(m_fdsOutputDock);
    m_fdsOutputLog->setObjectName(QStringLiteral("FdsOutputLog"));
    m_fdsOutputLog->setReadOnly(true);
    m_fdsOutputLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_fdsOutputDock->setWidget(m_fdsOutputLog);

    m_resultStatusDock = new QDockWidget(QStringLiteral("Result Status"), this);
    m_resultStatusDock->setObjectName(QStringLiteral("ResultStatusDock"));
    m_resultStatusDock->setFeatures(dockFeatures);
    auto* resultStatusPage = new QWidget(m_resultStatusDock);
    auto* resultStatusLayout = new QVBoxLayout(resultStatusPage);
    m_resultStatusLabel = new QLabel(u("No result case is loaded."), resultStatusPage);
    m_resultStatusLabel->setObjectName(QStringLiteral("ResultLoadingStatusLabel"));
    m_resultStatusLabel->setWordWrap(true);
    resultStatusLayout->addWidget(m_resultStatusLabel);
    resultStatusLayout->addStretch();
    m_resultStatusDock->setWidget(resultStatusPage);

    m_tutorialGuideDock = new QDockWidget(QStringLiteral("Guided Tutorial"), this);
    m_tutorialGuideDock->setObjectName(QStringLiteral("TutorialGuideDock"));
    m_tutorialGuideDock->setFeatures(dockFeatures);
    m_tutorialGuideWidget = new TutorialGuideWidget(m_tutorialGuideDock);
    m_tutorialGuideDock->setWidget(m_tutorialGuideWidget);
    m_tutorialGuideDock->hide();
}

void MainWindow::createCentralView()
{
    m_centralStack = new QStackedWidget(this);
    m_centralStack->setObjectName(QStringLiteral("CentralWorkspaceStack"));
    // QStackedWidget includes inactive pages in its minimum-size hint. The
    // legacy results/record controls can request >2000 px, pinning the model
    // dock to its 190 px minimum even while the resizable 3D page is active.
    // Do not let those hidden pages reserve the entire horizontal workspace.
    m_centralStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_centralStack->setMinimumWidth(280);
    m_startPageWidget = new StartPageWidget(m_centralStack);
    m_workspaceTabs = new QTabWidget(m_centralStack);
    m_workspaceTabs->setObjectName(QStringLiteral("MainWorkspaceTabs"));
    m_workspaceTabs->setDocumentMode(true);
    m_workspaceTabs->setMovable(false);
    m_occViewWidget = new OccViewWidget(m_workspaceTabs);
    m_occViewWidget->setObjectName(QStringLiteral("Model3DView"));
    m_planViewWidget = new OccViewWidget(m_workspaceTabs);
    m_planViewWidget->setObjectName(QStringLiteral("Plan2DView"));
    auto* recordPage = new QWidget(m_workspaceTabs);
    recordPage->setObjectName(QStringLiteral("FdsRecordWorkspace"));
    auto* recordLayout = new QVBoxLayout(recordPage);
    recordLayout->setContentsMargins(0, 0, 0, 0);
    auto* recordTools = new QHBoxLayout;
    m_recordSearchEdit = new QLineEdit(recordPage);
    m_recordSearchEdit->setObjectName(QStringLiteral("FdsRecordSearchEdit"));
    m_recordSearchEdit->setPlaceholderText(u("Search FDS records..."));
    auto* findPrevious = new QPushButton(u("Previous"), recordPage);
    findPrevious->setObjectName(QStringLiteral("FdsRecordFindPreviousButton"));
    auto* findNext = new QPushButton(u("Next"), recordPage);
    findNext->setObjectName(QStringLiteral("FdsRecordFindNextButton"));
    auto* copy = new QPushButton(u("Copy"), recordPage);
    copy->setObjectName(QStringLiteral("FdsRecordCopyButton"));
    m_recordAdvancedMode = new QCheckBox(u("Advanced Edit (Risk)"), recordPage);
    m_recordAdvancedMode->setObjectName(QStringLiteral("FdsRecordAdvancedModeCheck"));
    m_recordApplyButton = new QPushButton(u("Save Advanced Draft As..."), recordPage);
    m_recordApplyButton->setObjectName(QStringLiteral("FdsRecordSaveDraftButton"));
    m_recordApplyButton->setEnabled(false);
    recordTools->addWidget(m_recordSearchEdit, 1);
    recordTools->addWidget(findPrevious);
    recordTools->addWidget(findNext);
    recordTools->addWidget(copy);
    recordTools->addWidget(m_recordAdvancedMode);
    recordTools->addWidget(m_recordApplyButton);
    recordLayout->addLayout(recordTools);
    m_recordView = new FdsRecordEditor(recordPage);
    m_recordView->setPlaceholderText(
        u("The generated FDS input will appear here."));
    recordLayout->addWidget(m_recordView, 1);
    const auto findRecord = [this](bool backwards) {
        if (!m_recordView || !m_recordSearchEdit) return;
        QTextDocument::FindFlags flags;
        if (backwards) flags |= QTextDocument::FindBackward;
        if (!m_recordView->find(m_recordSearchEdit->text(), flags)) {
            QTextCursor cursor(m_recordView->document());
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            m_recordView->setTextCursor(cursor);
            m_recordView->find(m_recordSearchEdit->text(), flags);
        }
    };
    connect(findPrevious, &QPushButton::clicked, this,
            [findRecord]() { findRecord(true); });
    connect(findNext, &QPushButton::clicked, this,
            [findRecord]() { findRecord(false); });
    connect(m_recordSearchEdit, &QLineEdit::returnPressed, this,
            [findRecord]() { findRecord(false); });
    connect(copy, &QPushButton::clicked, this, [this]() {
        if (!m_recordView) return;
        const QString selectedText = m_recordView->textCursor().selectedText();
        if (!selectedText.isEmpty()) QApplication::clipboard()->setText(selectedText);
    });
    connect(m_recordView, &FdsRecordEditor::sourceActivated, this,
            [this](const QString& objectId, const QString& parameterKey) {
                if (!m_project || !m_project->document()) return;
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                if (!object) return;
                m_modelTreeWidget->selectObjectById(objectId, true);
                m_propertiesWidget->showObject(object.get());
                statusBar()->showMessage(
                    parameterKey.isEmpty()
                        ? QStringLiteral("FDS source UUID %1").arg(objectId)
                        : QStringLiteral("FDS source UUID %1 — field %2")
                              .arg(objectId, parameterKey),
                    kStatusMessageDurationMs);
            });
    connect(m_recordAdvancedMode, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, u("Advanced FDS Editing"),
                u("Advanced edits are a temporary draft only. They do not modify the UUID object model or the normal FDS export. Save the draft separately if needed."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                m_recordAdvancedMode->setChecked(false);
                return;
            }
        }
        m_recordView->setReadOnly(!enabled);
        m_recordApplyButton->setEnabled(enabled);
        if (!enabled) refreshRecordView();
    });
    connect(m_recordApplyButton, &QPushButton::clicked,
            this, &MainWindow::saveAdvancedRecordDraft);
    m_nativeResultViewerWidget = new NativeResultViewerWidget(m_workspaceTabs);
    m_nativeResultViewerWidget->setProject(m_project.get());
    m_workspaceTabs->addTab(m_occViewWidget, u("Model 3D"));
    m_workspaceTabs->addTab(m_planViewWidget, u("Plan 2D"));
    m_workspaceTabs->addTab(recordPage, u("FDS Record"));
    m_workspaceTabs->addTab(m_nativeResultViewerWidget, u("Results"));
    // The product workflow now uses the 3D view for plan-oriented work and
    // Smokeview as the authoritative field-result viewer.  Keep the legacy
    // widgets alive for compatibility and future development, but do not
    // expose duplicate workspaces in the normal interface.
    m_workspaceTabs->setTabVisible(1, false);
    m_workspaceTabs->setTabVisible(3, false);
    m_smokeviewHostWidget = new SmokeviewHostWidget(m_centralStack);
    m_centralStack->addWidget(m_startPageWidget);
    m_centralStack->addWidget(m_workspaceTabs);
    m_centralStack->addWidget(m_smokeviewHostWidget);
    m_centralStack->setCurrentWidget(m_workspaceTabs);
    m_workspaceTabs->setCurrentWidget(m_occViewWidget);
    setCentralWidget(m_centralStack);
}

void MainWindow::createApplicationStatusBar()
{
    statusBar()->showMessage(QStringLiteral("Ready"));
    m_simulationStatusWidget = new SimulationStatusWidget(statusBar());
    statusBar()->addPermanentWidget(m_simulationStatusWidget);
    m_simulationStatusWidget->setManager(m_taskManager);
    connect(m_simulationStatusWidget, &SimulationStatusWidget::detailsRequested,
            this, [this](const QString& taskId) {
                m_taskCenterWidget->selectTaskById(taskId);
                m_taskCenterWidget->setDetailsExpanded(true);
                m_taskCenterDock->show();
                m_taskCenterDock->raise();
            });
    m_snapStatusLabel = new QLabel(statusBar());
    m_snapStatusLabel->setObjectName(QStringLiteral("SnapStatusLabel"));
    m_snapStatusLabel->setMaximumWidth(260);
    statusBar()->addPermanentWidget(m_snapStatusLabel);
    updateSnapStatus();
}

void MainWindow::connectActions()
{
    connect(m_selectAction, &QAction::triggered, this, [this]() {
        m_selectAction->setChecked(true);
        if (m_occViewWidget) {
            m_occViewWidget->cancelWallSketch();
            m_occViewWidget->hideTransformManipulator();
        }
        if (m_planViewWidget) m_planViewWidget->cancelWallSketch();
        if (m_transformGizmoAction) m_transformGizmoAction->setChecked(false);
        if (m_activeToolLabel) m_activeToolLabel->setText(u("Active tool: Selection"));
        showModelWorkspace(0);
        statusBar()->showMessage(u("Selection mode active."),
                                 kStatusMessageDurationMs);
    });

    connect(m_newAction, &QAction::triggered, this, [this]() {
        if (!confirmProjectReplacement(u("creating a new project"))) return;
        createNewProject();
    });
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseAndOpenProject);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveCurrentProject);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveCurrentProjectAs);
    connect(m_importFdsAction, &QAction::triggered, this, &MainWindow::chooseAndImportFds);
    connect(m_exportFdsAction, &QAction::triggered, this, &MainWindow::chooseAndExportFds);
    connect(m_projectSettingsAction, &QAction::triggered,
            this, &MainWindow::editProjectSettings);
    connect(m_simulationParametersAction, &QAction::triggered,
            this, &MainWindow::editSimulationParameters);
    connect(m_meshAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("MESH")); });
    connect(m_speciesAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("SPEC")); });
    connect(m_materialAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("MATL")); });
    connect(m_surfaceAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("SURF")); });
    connect(m_reactionAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("REAC")); });
    connect(m_particleAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("PART")); });
    connect(m_ventAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("VENT")); });
    connect(m_deviceAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("DEVC")); });
    connect(m_controlAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("CTRL")); });
    connect(m_hvacAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("HVAC")); });
    connect(m_initialConditionAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("INIT")); });
    connect(m_outputAction, &QAction::triggered,
            this, [this]() { createFdsObject(QStringLiteral("SLCF")); });
    connect(m_addFdsObjectAction, &QAction::triggered,
            this, [this]() { createFdsObject(); });
    connect(m_meshEngineeringAction, &QAction::triggered,
            this, &MainWindow::createMeshesWithAssistant);
    connect(m_fireSourceWizardAction, &QAction::triggered,
            this, &MainWindow::createFireSourceWithWizard);
    connect(m_particleSprayWizardAction, &QAction::triggered,
            this, &MainWindow::createParticleSprayWithWizard);
    connect(m_deviceControlWizardAction, &QAction::triggered,
            this, &MainWindow::createDeviceControlWithWizard);
    connect(m_outputWizardAction, &QAction::triggered,
            this, &MainWindow::createOutputWithWizard);
    connect(m_controlGraphAction, &QAction::triggered,
            this, &MainWindow::showControlLogicGraph);
    connect(m_hvacGraphAction, &QAction::triggered,
            this, &MainWindow::showHvacNetworkGraph);
    connect(m_propertyLibraryAction, &QAction::triggered,
            this, &MainWindow::openPropertyLibrary);
    connect(m_scenarioManagerAction, &QAction::triggered,
            this, &MainWindow::manageScenarios);
    connect(m_runAllScenariosAction, &QAction::triggered,
            this, &MainWindow::runAllScenarios);
    connect(m_simpleTestAction, &QAction::triggered, this, &MainWindow::loadSimpleTestBenchmark);
    connect(m_activateVentsAction, &QAction::triggered,
            this, &MainWindow::loadActivateVentsTutorial);
    connect(m_bucketTest2Action, &QAction::triggered,
            this, &MainWindow::loadBucketTest2Tutorial);
    connect(m_couchAction, &QAction::triggered,
            this, &MainWindow::loadCouchTutorial);
    connect(m_couchSmoke12sAction, &QAction::triggered,
            this, &MainWindow::loadCouchSmoke12sTutorial);
    connect(m_hvacAircoilAction, &QAction::triggered,
            this, &MainWindow::loadHvacAircoilTutorial);
    connect(m_tunnelDemoAction, &QAction::triggered,
            this, &MainWindow::loadTunnelDemoTutorial);
    connect(m_tunnelSmoke10sAction, &QAction::triggered,
            this, &MainWindow::loadTunnelSmoke10sTutorial);
    for (QAction* action : m_guidedTutorialActions) {
        connect(action, &QAction::triggered, this, [this, action]() {
            startGuidedTutorial(action->data().toString());
        });
    }
    connect(m_tutorialGuideWidget, &TutorialGuideWidget::openActionRequested,
            this, [this](const QString& objectName) {
        QAction* action = findChild<QAction*>(objectName);
        if (!action || !action->isEnabled()) {
            statusBar()->showMessage(
                u("The tool for this tutorial step is not currently available."),
                kStatusMessageDurationMs);
            return;
        }
        action->trigger();
        QTimer::singleShot(0, this, &MainWindow::refreshTutorialContext);
    });
    connect(m_tutorialGuideWidget, &TutorialGuideWidget::stepTargetChanged,
            this, &MainWindow::highlightTutorialAction);
    connect(m_tutorialGuideWidget, &TutorialGuideWidget::closeRequested,
            m_tutorialGuideDock, &QDockWidget::hide);
    connect(m_createBoxAction, &QAction::triggered, this, &MainWindow::createTestBox);
    connect(m_createWallAction, &QAction::triggered, this,
            [this]() { createBuildingElement(static_cast<int>(FcGeometryKind::Wall)); });
    connect(m_drawWallAction, &QAction::triggered,
            this, &MainWindow::startInteractiveWallDrawing);
    connect(m_createRoomAction, &QAction::triggered, this,
            [this]() { createBuildingElement(static_cast<int>(FcGeometryKind::Room)); });
    for (QAction* action : m_buildingGeometryActions) {
        connect(action, &QAction::triggered, this, [this, action]() {
            createBuildingElement(action->data().toInt());
        });
    }
    connect(m_previewFdsBlocksAction, &QAction::triggered,
            this, &MainWindow::previewFdsBlocks);
    connect(m_convertGeometryToFdsAction, &QAction::triggered,
            this, &MainWindow::convertBuildingGeometryToFds);
    connect(m_healGeometryAction, &QAction::triggered,
            this, &MainWindow::healSelectedGeometry);
    connect(m_unionGeometryAction, &QAction::triggered, this, [this]() {
        performBooleanOperation(static_cast<int>(FcBooleanOperation::Union));
    });
    connect(m_cutGeometryAction, &QAction::triggered, this, [this]() {
        performBooleanOperation(static_cast<int>(FcBooleanOperation::Difference));
    });
    connect(m_intersectGeometryAction, &QAction::triggered, this, [this]() {
        performBooleanOperation(static_cast<int>(FcBooleanOperation::Intersection));
    });
    connect(m_splitGeometryAction, &QAction::triggered, this, [this]() {
        performBooleanOperation(static_cast<int>(FcBooleanOperation::Split));
    });
    connect(m_backgroundImageAction, &QAction::triggered,
            this, &MainWindow::importBackgroundImage);
    connect(m_occViewWidget, &OccViewWidget::wallSketchCompleted,
            this, &MainWindow::finishInteractiveWallDrawing);
    connect(m_planViewWidget, &OccViewWidget::wallSketchCompleted,
            this, &MainWindow::finishInteractiveWallDrawing);
    const auto showWallCursor = [this](double x, double y, double length,
                                       double angle, const QString& snapTarget) {
        if (!m_project) return;
        statusBar()->showMessage(
            QStringLiteral("X=%1, Y=%2 %3 | %4=%5 %3 | %6=%7°%8")
                .arg(m_project->metersToDisplay(x), 0, 'f', 3)
                .arg(m_project->metersToDisplay(y), 0, 'f', 3)
                .arg(m_project->displayUnitSymbol(), u("Length"))
                .arg(m_project->metersToDisplay(length), 0, 'f', 3)
                .arg(u("Angle")).arg(angle, 0, 'f', 1)
                .arg(snapTarget.isEmpty()
                         ? QString() : QStringLiteral(" | %1: %2").arg(u("Snap"), snapTarget)));
    };
    connect(m_occViewWidget, &OccViewWidget::wallSketchCursorChanged,
            this, showWallCursor);
    connect(m_planViewWidget, &OccViewWidget::wallSketchCursorChanged,
            this, showWallCursor);
    const auto wallSketchCancelled = [this](OccViewWidget* view) {
        if (view == m_occViewWidget) {
            m_occViewWidget->setOrientation(OccViewOrientation::Isometric);
        }
        m_wallSketchView = nullptr;
        if (m_activeToolLabel) m_activeToolLabel->setText(u("Active tool: Selection"));
        statusBar()->showMessage(
            m_wallSketchSegmentNumber > 0 ? u("Wall drawing finished.")
                                          : u("Wall drawing cancelled."),
            kStatusMessageDurationMs);
    };
    connect(m_occViewWidget, &OccViewWidget::wallSketchCancelled, this,
            [this, wallSketchCancelled]() { wallSketchCancelled(m_occViewWidget); });
    connect(m_planViewWidget, &OccViewWidget::wallSketchCancelled, this,
            [this, wallSketchCancelled]() { wallSketchCancelled(m_planViewWidget); });
    connect(m_importGeometryAction, &QAction::triggered, this, &MainWindow::importGeometry);
    connect(m_runAction, &QAction::triggered, this, &MainWindow::runCurrentProject);
    connect(m_runOpenMpAction, &QAction::triggered,
            this, &MainWindow::runCurrentProjectOpenMp);
    connect(m_runParallelAction, &QAction::triggered,
            this, &MainWindow::runCurrentProjectParallel);
    connect(m_validateAction, &QAction::triggered,
            this, &MainWindow::validateCurrentProject);
    connect(m_editObjectAction, &QAction::triggered, this, [this]() {
        if (m_modelTreeWidget) openObjectProperties(m_modelTreeWidget->selectedObjectId());
    });
    connect(m_directEditAction, &QAction::toggled, this, &MainWindow::toggleDirectGeometryEditing);
    connect(m_occViewWidget, &OccViewWidget::directEditEnded, this, [this]() {
        const QSignalBlocker blocker(m_directEditAction);
        m_directEditAction->setChecked(false);
    });
    connect(m_occViewWidget, &OccViewWidget::directEditStatus, this,
            [this](const QString& text) { statusBar()->showMessage(text); });
    connect(m_occViewWidget, &OccViewWidget::geometryEditRequested, this,
        [this](const QString& id, const QVariantMap& parameters) {
            const QString objectId=id; // endDirectEditing clears its internal ID synchronously.
            QString error;
            if (!commitGeometryParameters(objectId, parameters, &error)) {
                QMessageBox::warning(this, u("Invalid Geometry"), UiLanguageManager::text(error));
            }
            toggleDirectGeometryEditing(true);
        });
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopFdsCase);
    connect(m_taskManager, &SimulationTaskManager::taskAdded, this,
            [this](const QString& taskId) {
                const SimulationTaskRecord* task = m_taskManager->task(taskId);
                if (!task) return;
                m_stopAction->setEnabled(true);
                m_messageWidget->appendMessage(
                    QStringLiteral("[Info] FDS task queued: %1; backend=%2; processes=%3")
                        .arg(QDir::toNativeSeparators(task->request.inputFilePath),
                             task->backendName)
                        .arg(task->request.processCount));
                statusBar()->showMessage(task->environmentCheck.startsWith(QStringLiteral("Ready:"))
                    ? u("Environment check passed. FDS calculation queued...")
                    : u("FDS calculation queued..."), kStatusMessageDurationMs);
            });
    connect(m_taskManager, &SimulationTaskManager::taskUpdated, this,
            [this](const QString& taskId) {
                m_stopAction->setEnabled(m_taskManager->hasActiveTasks());
                const SimulationTaskRecord* task = m_taskManager->task(taskId);
                if (task && task->state == SimulationTaskState::Running) {
                    statusBar()->showMessage(u("FDS calculation is running..."));
                }
            });
    connect(m_taskManager, &SimulationTaskManager::taskOutput, this,
            [this](const QString&, const QString& output) {
                const QString text = output.trimmed();
                if (!text.isEmpty()) m_messageWidget->appendMessage(text);
            });
    connect(m_taskManager, &SimulationTaskManager::taskFinished, this,
            [this](const QString&, const FdsRunSummary& summary) {
                handleFdsRunFinished(summary);
            });
    connect(m_taskCenterWidget,
            &SimulationTaskCenterWidget::openResultsRequested,
            this, [this](const QString& path) {
                if (openResultFile(path)) openSelectedInSmokeview();
            });
    connect(m_openResultsAction, &QAction::triggered, this, &MainWindow::chooseAndOpenResults);
    connect(m_compareResultsAction, &QAction::triggered,
            this, &MainWindow::compareFdsResults);
    connect(m_reloadResultsAction, &QAction::triggered, this, &MainWindow::reloadSelectedResults);
    connect(m_closeResultsAction, &QAction::triggered, this, &MainWindow::closeSelectedResults);
    connect(m_openSmokeviewAction, &QAction::triggered, this, &MainWindow::openSelectedInSmokeview);
    connect(m_openNativeResultsAction, &QAction::triggered,
            this, &MainWindow::openSelectedInNativeViewer);
    connect(m_openSmokeAnimationAction, &QAction::triggered,
            this, &MainWindow::openSelectedSmokeAnimation);
    connect(m_openSliceAnimationAction, &QAction::triggered,
            this, &MainWindow::openSelectedSliceAnimation);
    connect(m_openParticleAnimationAction, &QAction::triggered,
            this, &MainWindow::openSelectedParticleAnimation);
    connect(m_configureSmokeviewAction, &QAction::triggered, this, &MainWindow::configureSmokeview);
    connect(m_englishAction, &QAction::triggered, this, [this]() {
        setInterfaceLanguage(UiLanguage::English);
    });
    connect(m_chineseAction, &QAction::triggered, this, [this]() {
        setInterfaceLanguage(UiLanguage::ChineseSimplified);
    });
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedObject);
    connect(m_selectAllAction, &QAction::triggered,
            this, &MainWindow::selectAllModelObjects);
    connect(m_invertSelectionAction, &QAction::triggered,
            this, &MainWindow::invertModelSelection);
    connect(m_selectByTypeAction, &QAction::triggered,
            this, &MainWindow::selectObjectsByType);
    connect(m_selectByPropertyAction, &QAction::triggered,
            this, &MainWindow::selectObjectsByProperty);
    connect(m_selectByFloorAction, &QAction::triggered,
            this, &MainWindow::selectObjectsByFloor);
    connect(m_transformAction, &QAction::triggered,
            this, &MainWindow::transformSelectedGeometry);
    connect(m_transformGizmoAction, &QAction::toggled, this, [this](bool enabled) {
        if (!enabled) {
            m_occViewWidget->hideTransformManipulator();
            statusBar()->showMessage(u("Transform gizmo hidden."),
                                     kStatusMessageDurationMs);
            return;
        }
        QStringList geometryIds;
        for (const QString& objectId : m_modelTreeWidget->selectedObjectIds()) {
            const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
                m_project->document()->findObject(objectId));
            if (object && !isLockedForModification(object.get()) && object->isVisible()) {
                geometryIds.append(objectId);
            }
        }
        if (!m_occViewWidget->showTransformManipulator(geometryIds)) {
            m_transformGizmoAction->setChecked(false);
            statusBar()->showMessage(u("Select visible, unlocked geometry first."),
                                     kStatusMessageDurationMs);
        } else {
            statusBar()->showMessage(
                u("Drag a gizmo axis to move, rotate, or scale the selection."),
                kStatusMessageDurationMs);
        }
    });
    connect(m_occViewWidget, &OccViewWidget::manipulatorTransformFinished,
            this, &MainWindow::commitManipulatorTransform);
    connect(m_copyMoveAction, &QAction::triggered,
            this, &MainWindow::copyMoveSelectedGeometry);
    connect(m_arrayAction, &QAction::triggered,
            this, &MainWindow::arraySelectedGeometry);
    connect(m_measureAction, &QAction::triggered,
            this, &MainWindow::measureSelectedGeometry);
    connect(m_mirrorAction, &QAction::triggered,
            this, &MainWindow::mirrorSelectedGeometry);
    connect(m_alignAction, &QAction::triggered,
            this, &MainWindow::alignSelectedGeometry);
    connect(m_copyToFloorAction, &QAction::triggered,
            this, &MainWindow::copySelectedGeometryToFloor);
    connect(m_groupAction, &QAction::triggered,
            this, &MainWindow::groupSelectedObjects);
    connect(m_createFolderAction, &QAction::triggered, this,
            [this]() { createGeometryContainer(false); });
    connect(m_createFloorAction, &QAction::triggered, this,
            [this]() { createGeometryContainer(true); });
    connect(m_hideSelectedAction, &QAction::triggered,
            this, &MainWindow::hideSelectedObjects);
    connect(m_lockSelectedAction, &QAction::triggered,
            this, &MainWindow::toggleSelectedObjectsLocked);
    connect(m_batchRenameAction, &QAction::triggered,
            this, &MainWindow::batchRenameSelectedObjects);
    connect(m_assignSurfacesAction, &QAction::triggered,
            this, &MainWindow::assignSurfacesToSelectedGeometry);
    connect(m_snapEnabledAction, &QAction::toggled, this, [this](bool enabled) {
        SnapSettings settings = m_snapManager->settings();
        settings.enabled = enabled;
        m_snapManager->setSettings(settings);
        updateSnapStatus();
        statusBar()->showMessage(enabled ? u("Snapping enabled.")
                                         : u("Snapping temporarily disabled."),
                                 kStatusMessageDurationMs);
    });
    connect(m_snapSettingsAction, &QAction::triggered,
            this, &MainWindow::configureSnapping);
    connect(m_duplicateAction, &QAction::triggered,
            this, &MainWindow::duplicateSelectedObject);
    connect(m_moveUpAction, &QAction::triggered,
            this, &MainWindow::moveSelectedObjectUp);
    connect(m_moveDownAction, &QAction::triggered,
            this, &MainWindow::moveSelectedObjectDown);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);
    connect(m_preferencesAction, &QAction::triggered,
            this, &MainWindow::showApplicationSettings);
    connect(m_manageResourcesAction, &QAction::triggered,
            this, &MainWindow::manageProjectResources);
    connect(m_packageProjectAction, &QAction::triggered,
            this, &MainWindow::packageCurrentProject);
    connect(m_unpackProjectAction, &QAction::triggered,
            this, &MainWindow::unpackProjectPackage);
    connect(m_copyProjectAction, &QAction::triggered,
            this, &MainWindow::copyCurrentProjectToDirectory);
    connect(m_cleanResultsAction, &QAction::triggered,
            this, &MainWindow::cleanUnusedResults);
    connect(m_startPageAction, &QAction::triggered,
            this, &MainWindow::showStartPage);
    connect(m_startPageWidget, &StartPageWidget::newProjectRequested, this, [this]() {
        if (confirmProjectReplacement(u("creating a new project"))) createNewProject();
    });
    connect(m_startPageWidget, &StartPageWidget::openProjectRequested,
            this, &MainWindow::chooseAndOpenProject);
    connect(m_startPageWidget, &StartPageWidget::recentProjectRequested,
            this, [this](const QString& path) {
        if (!QFileInfo::exists(path)) {
            ApplicationSettingsStore().removeRecentProject(path);
            refreshStartPage();
            QMessageBox::warning(this, u("Open Project"),
                                 u("The recent project file no longer exists."));
            return;
        }
        if (confirmProjectReplacement(u("opening another project"))) openProjectFile(path);
    });
    connect(m_startPageWidget, &StartPageWidget::tutorialRequested,
            this, [this](const QString& id) {
        startGuidedTutorial(id);
    });
    connect(m_startPageWidget, &StartPageWidget::recoveryRequested,
            this, [this](const QString& snapshotPath) {
        if (!confirmProjectReplacement(u("recovering another project"))) return;
        if (openProjectFile(snapshotPath)) {
            m_currentProjectPath.clear();
            m_lastRecoverySnapshotPath = snapshotPath;
            m_project->setModified(true);
            updateWindowTitle();
        }
    });
    connect(m_startPageWidget, &StartPageWidget::discardRecoveryRequested,
            this, [this](const QString& snapshotPath) {
        if (m_recoveryManager) m_recoveryManager->discard(snapshotPath);
        refreshStartPage();
    });
    connect(m_resetLayoutAction, &QAction::triggered, this, &MainWindow::resetDefaultLayout);

    connect(m_smokeviewHostWidget,
            &SmokeviewHostWidget::returnToModelRequested,
            this,
            [this]() {
                showModelWorkspace(0);
                statusBar()->showMessage(u("Returned to the modeling workspace."),
                                         kStatusMessageDurationMs);
            });
    connect(m_nativeResultViewerWidget,
            &NativeResultViewerWidget::returnToModelRequested,
            this,
            [this]() {
                showModelWorkspace(0);
                statusBar()->showMessage(u("Returned to the modeling workspace."),
                                         kStatusMessageDurationMs);
            });
    connect(m_nativeResultViewerWidget,
            &NativeResultViewerWidget::openInSmokeviewRequested,
            this,
            [this](const QString& smvFilePath) {
                const SmokeviewLaunchResult launch = SmokeviewLauncher::launch(smvFilePath);
                if (!launch.success) {
                    m_messageWidget->appendMessage(
                        QStringLiteral("[Error] Smokeview launch failed: %1")
                            .arg(launch.errorMessage));
                    return;
                }
                m_messageWidget->appendMessage(
                    QStringLiteral("[Info] Smokeview started from the native result workspace (PID %1)")
                        .arg(launch.processId));
            });
    connect(m_smokeviewHostWidget,
            &SmokeviewHostWidget::viewerEmbedded,
            this,
            [this](qint64 processId) {
                m_messageWidget->appendMessage(
                    QStringLiteral("[Info] Smokeview result window embedded (PID %1)")
                        .arg(processId));
                statusBar()->showMessage(u("Smokeview result window embedded."),
                                         kStatusMessageDurationMs);
            });
    connect(m_smokeviewHostWidget,
            &SmokeviewHostWidget::embeddingFailed,
            this,
            [this](const QString& errorMessage) {
                m_messageWidget->appendMessage(
                    QStringLiteral("[Warning] %1").arg(errorMessage));
                statusBar()->showMessage(errorMessage, kStatusMessageDurationMs);
            });
    connect(m_smokeviewHostWidget,
            &SmokeviewHostWidget::viewerClosed,
            this,
            [this]() {
                showModelWorkspace(0);
                statusBar()->showMessage(u("Smokeview result window closed."),
                                         kStatusMessageDurationMs);
            });

    connect(m_fitAllAction, &QAction::triggered, this, [this]() {
        m_occViewWidget->fitAll();
        statusBar()->showMessage(QStringLiteral("Fit All"), kStatusMessageDurationMs);
    });
    connect(m_restoreVisibilityAction, &QAction::triggered,
            this, &MainWindow::restoreModelVisibility);
    connect(m_occViewWidget, &OccViewWidget::objectActivated,
            this, &MainWindow::openObjectProperties);
    connect(m_planViewWidget, &OccViewWidget::objectActivated,
            this, &MainWindow::openObjectProperties);
    m_occViewWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_occViewWidget, &QWidget::customContextMenuRequested,
            this, [this](const QPoint& position) {
        if (!m_occViewWidget || !m_occViewWidget->displayManager()) return;
        const QString hit = m_occViewWidget->prepareContextSelection(position);
        const QStringList selectedIds = hit.isEmpty() ? QStringList{} :
            m_occViewWidget->displayManager()->selectedObjectIds();
        std::unique_ptr<QMenu> menu(createModelContextMenu(selectedIds));
        menu->exec(m_occViewWidget->mapToGlobal(position));
    });
    connect(m_fitSelectionAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(
            m_occViewWidget->fitSelection() ? u("Fit Selection")
                                            : u("No displayed object is selected."),
            kStatusMessageDurationMs);
    });
    connect(m_perspectiveAction, &QAction::triggered, this,
            [this]() { m_occViewWidget->setPerspective(true); });
    connect(m_orthographicAction, &QAction::triggered, this,
            [this]() { m_occViewWidget->setPerspective(false); });
    connect(m_saveViewAction, &QAction::triggered, this, [this]() {
        m_occViewWidget->saveView();
        statusBar()->showMessage(u("View saved."), kStatusMessageDurationMs);
    });
    connect(m_restoreViewAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(
            m_occViewWidget->restoreView() ? u("View restored.")
                                           : u("No saved view is available."),
            kStatusMessageDurationMs);
    });
    connect(m_rotationCenterAction, &QAction::triggered, this, [this]() {
        if (!m_project) return;
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("RotationCenterDialog"));
        dialog.setWindowTitle(u("Set Rotation Center"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
        auto* x = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
        auto* y = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
        auto* z = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
        form->addRow(QStringLiteral("X:"), x);
        form->addRow(QStringLiteral("Y:"), y);
        form->addRow(QStringLiteral("Z:"), z);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted) return;
        m_occViewWidget->setRotationCenter(
            m_project->displayToMeters(x->value()),
            m_project->displayToMeters(y->value()),
            m_project->displayToMeters(z->value()));
        statusBar()->showMessage(u("Rotation center updated."),
                                 kStatusMessageDurationMs);
    });
    connect(m_axesAction, &QAction::toggled, this,
            [this](bool visible) { m_occViewWidget->showTrihedron(visible); });
    connect(m_backgroundAction, &QAction::triggered, this, [this]() {
        const QColor color = QColorDialog::getColor(QColor(31, 36, 46), this,
                                                     u("Background Color"));
        if (color.isValid()) m_occViewWidget->setBackgroundColor(color);
    });
    connect(m_clippingPlaneAction, &QAction::triggered, this, [this]() {
        if (!m_project) return;
        QDialog dialog(this);
        dialog.setWindowTitle(u("Clipping Plane"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;
        auto* enabled = new QCheckBox(u("Enable clipping plane"), &dialog);
        enabled->setChecked(true);
        auto* axis = new QComboBox(&dialog);
        axis->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
        auto* position = createEngineeringSpinBox(
            &dialog, -1.0e9, 1.0e9, 0.0,
            QStringLiteral(" %1").arg(m_project->displayUnitSymbol()));
        form->addRow(enabled);
        form->addRow(u("Normal Axis:"), axis);
        form->addRow(u("Position:"), position);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() == QDialog::Accepted) {
            m_occViewWidget->setClippingPlane(
                enabled->isChecked(), axis->currentIndex(),
                m_project->displayToMeters(position->value()));
        }
    });
    const auto connectViewAction = [this](QAction* action,
                                          OccViewOrientation orientation,
                                          const QString& statusMessage) {
        connect(action, &QAction::triggered, this, [this, orientation, statusMessage]() {
            m_occViewWidget->setOrientation(orientation);
            statusBar()->showMessage(statusMessage, kStatusMessageDurationMs);
        });
    };
    connectViewAction(m_frontAction, OccViewOrientation::Front, QStringLiteral("Front View"));
    connectViewAction(m_backAction, OccViewOrientation::Back, QStringLiteral("Back View"));
    connectViewAction(m_leftAction, OccViewOrientation::Left, QStringLiteral("Left View"));
    connectViewAction(m_rightAction, OccViewOrientation::Right, QStringLiteral("Right View"));
    connectViewAction(m_topAction, OccViewOrientation::Top, QStringLiteral("Top View"));
    connectViewAction(m_bottomAction, OccViewOrientation::Bottom, QStringLiteral("Bottom View"));
    connectViewAction(
        m_isometricAction, OccViewOrientation::Isometric, QStringLiteral("Isometric View"));

    connect(m_occViewWidget,
            &OccViewWidget::viewerInitializationFinished,
            this,
            [this](bool success) {
                m_messageWidget->appendMessage(
                    success
                        ? QStringLiteral("[Info] OpenCascade viewer initialized.")
                        : QStringLiteral("[Error] Failed to initialize OpenCascade viewer."));
                if (success) {
                    m_fdsSceneSynchronizer = std::make_unique<FdsSceneSynchronizer>(
                        m_occViewWidget->displayManager());
                    rebuildFdsScene(true);
                }
            });
    connect(m_planViewWidget,
            &OccViewWidget::viewerInitializationFinished,
            this,
            [this](bool success) {
                m_messageWidget->appendMessage(
                    success
                        ? QStringLiteral("[Info] OpenCascade plan viewer initialized.")
                        : QStringLiteral("[Error] Failed to initialize OpenCascade plan viewer."));
                if (!success) return;
                m_planViewWidget->setPerspective(false);
                m_planViewWidget->setOrientation(OccViewOrientation::Top);
                m_planViewWidget->showTrihedron(false);
                m_planSceneSynchronizer = std::make_unique<FdsSceneSynchronizer>(
                    m_planViewWidget->displayManager());
                rebuildFdsScene(true);
            });
    connect(m_planViewWidget, &OccViewWidget::objectSelected,
            this, [this](const QString& objectId) {
                if (m_modelTreeWidget) {
                    m_modelTreeWidget->selectObjectById(objectId, true);
                }
            });
    connect(m_planViewWidget, &OccViewWidget::objectsSelected,
            this, [this](const QStringList& objectIds) {
                if (m_modelTreeWidget) {
                    m_modelTreeWidget->selectObjectsByIds(objectIds, true);
                }
            });
    connect(m_planViewWidget, &OccViewWidget::selectionCleared,
            this, [this]() {
                if (m_modelTreeWidget) m_modelTreeWidget->clearSelection();
                if (m_occViewWidget && m_occViewWidget->displayManager()) {
                    m_occViewWidget->displayManager()->clearSelection();
                }
                m_propertiesWidget->clear();
                updateMultiSelection({}, false, false);
            });
    connect(m_workspaceTabs, &QTabWidget::currentChanged,
            this, [this](int index) {
                if (index == 1 && m_planViewWidget) {
                    m_planViewWidget->setPerspective(false);
                    m_planViewWidget->setOrientation(OccViewOrientation::Top);
                } else if (index == 2) {
                    refreshRecordView();
                }
            });

    connect(m_modelTreeWidget,
            &ModelTreeWidget::projectSelected,
            this,
            [this]() {
                applyFloorViewState({});
                if (GeometryDisplayManager* displayManager =
                        m_occViewWidget->displayManager()) {
                    displayManager->clearSelection();
                }
                if (m_planViewWidget && m_planViewWidget->displayManager()) {
                    m_planViewWidget->displayManager()->clearSelection();
                }
                m_deleteAction->setEnabled(false);
                m_editObjectAction->setEnabled(false);
                m_duplicateAction->setEnabled(false);
                m_moveUpAction->setEnabled(false);
                m_moveDownAction->setEnabled(false);
                m_reloadResultsAction->setEnabled(false);
                m_closeResultsAction->setEnabled(false);
                m_openSmokeviewAction->setEnabled(false);
                m_openNativeResultsAction->setEnabled(false);
                m_openSmokeAnimationAction->setEnabled(false);
                m_openSliceAnimationAction->setEnabled(false);
                m_openParticleAnimationAction->setEnabled(false);
                m_propertiesWidget->showProject(m_project.get());
                updateMultiSelection({}, false, false);
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::objectSelected,
            this,
            [this](const QString& objectId) {
                if (!m_project || !m_project->document()) {
                    if (GeometryDisplayManager* displayManager =
                            m_occViewWidget->displayManager()) {
                        displayManager->clearSelection();
                    }
                    m_propertiesWidget->clear();
                    return;
                }
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                QString floorId;
                for (FcObject* current = object.get(); current; current = current->parent()) {
                    if (current->type() == FcObjectType::Floor) {
                        floorId = current->id();
                        break;
                    }
                }
                applyFloorViewState(floorId);
                const std::shared_ptr<FcResultCase> resultCase =
                    resultCaseForObject(object.get());
                const bool objectUnlocked = object &&
                    !isLockedForModification(object.get());
                const bool resultEditable = resultCase &&
                    !isLockedForModification(resultCase.get());
                if (GeometryDisplayManager* displayManager =
                        m_occViewWidget->displayManager()) {
                    if (!object || !displayManager->contains(objectId)) {
                        displayManager->clearSelection();
                    } else {
                        displayManager->selectObject(objectId);
                    }
                }
                if (m_planViewWidget && m_planViewWidget->displayManager()) {
                    GeometryDisplayManager* planManager =
                        m_planViewWidget->displayManager();
                    if (!object || !planManager->contains(objectId)) {
                        planManager->clearSelection();
                    } else {
                        planManager->selectObject(objectId);
                    }
                }
                m_deleteAction->setEnabled(
                    objectUnlocked && (object->type() == FcObjectType::IfcModel ||
                               object->type() == FcObjectType::ResultCase ||
                               static_cast<bool>(
                                   std::dynamic_pointer_cast<FcFdsNamelist>(object))));
                m_editObjectAction->setEnabled(
                    objectUnlocked &&
                    (static_cast<bool>(std::dynamic_pointer_cast<FcFdsNamelist>(object)) ||
                     static_cast<bool>(std::dynamic_pointer_cast<FcGeometryObject>(object)) ||
                     static_cast<bool>(std::dynamic_pointer_cast<FcFloorObject>(object))));
                const bool editableNamelist = objectUnlocked && static_cast<bool>(
                    std::dynamic_pointer_cast<FcFdsNamelist>(object));
                m_duplicateAction->setEnabled(editableNamelist);
                m_moveUpAction->setEnabled(editableNamelist);
                m_moveDownAction->setEnabled(editableNamelist);
                m_reloadResultsAction->setEnabled(resultEditable);
                m_closeResultsAction->setEnabled(resultEditable);
                m_openSmokeviewAction->setEnabled(static_cast<bool>(resultCase));
                m_openNativeResultsAction->setEnabled(static_cast<bool>(resultCase));
                m_openSmokeAnimationAction->setEnabled(static_cast<bool>(resultCase));
                m_openSliceAnimationAction->setEnabled(static_cast<bool>(resultCase));
                m_openParticleAnimationAction->setEnabled(static_cast<bool>(resultCase));
                m_propertiesWidget->showObject(object.get());
                updateMultiSelection({objectId}, false, false);
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::objectsSelected,
            this,
            [this](const QStringList& objectIds) {
                if (objectIds.size() > 1) {
                    updateMultiSelection(objectIds, false, true);
                }
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::selectionCleared,
            this,
            [this]() {
                applyFloorViewState({});
                if (GeometryDisplayManager* displayManager =
                        m_occViewWidget->displayManager()) {
                    displayManager->clearSelection();
                }
                if (m_planViewWidget && m_planViewWidget->displayManager()) {
                    m_planViewWidget->displayManager()->clearSelection();
                }
                m_deleteAction->setEnabled(false);
                m_editObjectAction->setEnabled(false);
                m_duplicateAction->setEnabled(false);
                m_moveUpAction->setEnabled(false);
                m_moveDownAction->setEnabled(false);
                m_reloadResultsAction->setEnabled(false);
                m_closeResultsAction->setEnabled(false);
                m_openSmokeviewAction->setEnabled(false);
                m_openNativeResultsAction->setEnabled(false);
                m_openSmokeAnimationAction->setEnabled(false);
                m_openSliceAnimationAction->setEnabled(false);
                m_openParticleAnimationAction->setEnabled(false);
                m_propertiesWidget->clear();
                updateMultiSelection({}, false, false);
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::deleteObjectRequested,
            this,
            [this](const QString& objectId) { removeObject(objectId); });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::editObjectRequested,
            this, &MainWindow::openObjectProperties);
    connect(m_modelTreeWidget,
            &ModelTreeWidget::duplicateObjectRequested,
            this,
            [this](const QString& objectId) { duplicateObject(objectId); });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::moveObjectRequested,
            this,
            [this](const QString& objectId, int offset) {
                moveObject(objectId, offset);
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::visibilityChangeRequested,
            this,
            [this](const QString& objectId, bool visible) {
                setObjectVisibility(objectId, visible);
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::isolateObjectRequested,
            this,
            [this](const QString& objectId) { isolateObject(objectId); });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::restoreVisibilityRequested,
            this, &MainWindow::restoreModelVisibility);
    connect(m_modelTreeWidget,
            &ModelTreeWidget::viewNormalRequested,
            this,
            [this](const QString& objectId) { viewNormalToObject(objectId); });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::lockChangeRequested,
            this,
            [this](const QString& objectId, bool locked) {
                if (!m_project || !m_project->document()) return;
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                if (!object || object->isLocked() == locked) return;
                const bool before = object->isLocked();
                const auto apply = [this, object](bool value) {
                    object->setLocked(value);
                    m_project->setModified(true);
                    m_modelTreeWidget->refresh();
                    m_modelTreeWidget->selectObjectById(object->id());
                    updateWindowTitle();
                };
                m_undoStack->push(new FunctionalUndoCommand(
                    locked ? u("Lock Object") : u("Unlock Object"),
                    [apply, locked]() { apply(locked); },
                    [apply, before]() { apply(before); }));
            });
    connect(m_modelTreeWidget,
            &ModelTreeWidget::reparentObjectRequested,
            this,
            [this](const QString& objectId, const QString& parentObjectId, int childIndex) {
                if (!m_project || !m_project->document()) return;
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                const FcObject::Ptr newParent = m_project->document()->findObject(parentObjectId);
                if (!object || !newParent || !object->parent() ||
                    isLockedForModification(object.get()) ||
                    isLockedForModification(newParent.get()) ||
                    object.get() == newParent.get()) {
                    m_modelTreeWidget->refresh();
                    return;
                }
                for (FcObject* ancestor = newParent.get(); ancestor; ancestor = ancestor->parent()) {
                    if (ancestor == object.get()) {
                        m_modelTreeWidget->refresh();
                        return;
                    }
                }
                FcObject* oldParent = object->parent();
                const auto& oldChildren = oldParent->children();
                const auto oldPosition = std::find_if(
                    oldChildren.cbegin(), oldChildren.cend(),
                    [&objectId](const FcObject::Ptr& child) {
                        return child && child->id() == objectId;
                    });
                if (oldPosition == oldChildren.cend()) {
                    m_modelTreeWidget->refresh();
                    return;
                }
                FcObject* oldRoot = oldParent;
                while (oldRoot->parent()) oldRoot = oldRoot->parent();
                FcObject* newRoot = newParent.get();
                while (newRoot->parent()) newRoot = newRoot->parent();
                const bool parentCanOrganize = newParent->type() == FcObjectType::Group ||
                    newParent->type() == FcObjectType::Folder ||
                    newParent->type() == FcObjectType::Floor;
                if (!parentCanOrganize || oldRoot != newRoot) {
                    m_modelTreeWidget->refresh();
                    statusBar()->showMessage(
                        u("Objects can only be moved within their standard model group."),
                        kStatusMessageDurationMs);
                    return;
                }
                const std::size_t oldIndex = static_cast<std::size_t>(
                    std::distance(oldChildren.cbegin(), oldPosition));
                const std::size_t newIndex = static_cast<std::size_t>(qMax(0, childIndex));
                const auto apply = [this, object, oldParent, oldIndex, newParent, newIndex](bool forward) {
                    if (object->parent()) object->parent()->removeChild(object->id());
                    FcObject* target = forward ? newParent.get() : oldParent;
                    target->insertChild(object, forward ? newIndex : oldIndex);
                    m_project->setModified(true);
                    m_currentFdsPath.clear();
                    m_modelTreeWidget->refresh();
                    m_modelTreeWidget->selectObjectById(object->id());
                    updateWindowTitle();
                };
                m_undoStack->push(new FunctionalUndoCommand(
                    u("Move Object in Tree"),
                    [apply]() { apply(true); },
                    [apply]() { apply(false); }));
            });

    connect(m_occViewWidget,
            &OccViewWidget::objectSelected,
            this,
            [this](const QString& objectId) {
                if (!m_project || !m_project->document()) {
                    return;
                }
                const FcObject::Ptr object = m_project->document()->findObject(objectId);
                GeometryDisplayManager* displayManager =
                    m_occViewWidget->displayManager();
                if (!object || !displayManager || !displayManager->contains(objectId) ||
                    !m_modelTreeWidget->selectObjectById(objectId)) {
                    if (displayManager) {
                        displayManager->clearSelection();
                    }
                    m_modelTreeWidget->clearSelection();
                    m_deleteAction->setEnabled(false);
                    m_editObjectAction->setEnabled(false);
                    m_duplicateAction->setEnabled(false);
                    m_moveUpAction->setEnabled(false);
                    m_moveDownAction->setEnabled(false);
                    m_reloadResultsAction->setEnabled(false);
                    m_closeResultsAction->setEnabled(false);
                    m_openSmokeviewAction->setEnabled(false);
                    m_openNativeResultsAction->setEnabled(false);
                    m_openSmokeAnimationAction->setEnabled(false);
                    m_openSliceAnimationAction->setEnabled(false);
                    m_openParticleAnimationAction->setEnabled(false);
                    m_propertiesWidget->clear();
                    return;
                }

                displayManager->selectObject(objectId);
                const bool objectUnlocked = !isLockedForModification(object.get());
                m_deleteAction->setEnabled(
                    objectUnlocked && (object->type() == FcObjectType::IfcModel ||
                    static_cast<bool>(std::dynamic_pointer_cast<FcFdsNamelist>(object))));
                m_editObjectAction->setEnabled(
                    objectUnlocked && static_cast<bool>(
                        std::dynamic_pointer_cast<FcFdsNamelist>(object)));
                const bool editableNamelist = objectUnlocked && static_cast<bool>(
                    std::dynamic_pointer_cast<FcFdsNamelist>(object));
                m_duplicateAction->setEnabled(editableNamelist);
                m_moveUpAction->setEnabled(editableNamelist);
                m_moveDownAction->setEnabled(editableNamelist);
                m_reloadResultsAction->setEnabled(false);
                m_closeResultsAction->setEnabled(false);
                m_openSmokeviewAction->setEnabled(false);
                m_openNativeResultsAction->setEnabled(false);
                m_openSmokeAnimationAction->setEnabled(false);
                m_openSliceAnimationAction->setEnabled(false);
                m_openParticleAnimationAction->setEnabled(false);
                m_propertiesWidget->showObject(object.get());
                updateMultiSelection({objectId}, false, false);
            });
    connect(m_occViewWidget,
            &OccViewWidget::objectsSelected,
            this,
            [this](const QStringList& objectIds) {
                if (objectIds.size() > 1) {
                    updateMultiSelection(objectIds, true, false);
                }
            });
    connect(m_occViewWidget,
            &OccViewWidget::selectionCleared,
            this,
            [this]() {
                if (GeometryDisplayManager* displayManager =
                        m_occViewWidget->displayManager()) {
                    displayManager->clearSelection();
                }
                m_modelTreeWidget->clearSelection();
                m_deleteAction->setEnabled(false);
                m_editObjectAction->setEnabled(false);
                m_duplicateAction->setEnabled(false);
                m_moveUpAction->setEnabled(false);
                m_moveDownAction->setEnabled(false);
                m_reloadResultsAction->setEnabled(false);
                m_closeResultsAction->setEnabled(false);
                m_openSmokeviewAction->setEnabled(false);
                m_openNativeResultsAction->setEnabled(false);
                m_openSmokeAnimationAction->setEnabled(false);
                m_openSliceAnimationAction->setEnabled(false);
                m_openParticleAnimationAction->setEnabled(false);
                m_propertiesWidget->clear();
            });

    connect(m_aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this,
                           u("About FireCAE"),
                           QStringLiteral("FireCAE\n\n%1\n\n%2 %3")
                               .arg(u("Fire Dynamics Simulation Platform"),
                                    u("Version"), QCoreApplication::applicationVersion()));
    });
    connect(m_diagnosticsAction, &QAction::triggered,
            this, &MainWindow::showDiagnostics);
}

void MainWindow::createNewProject()
{
    if(m_occViewWidget) {m_occViewWidget->endDirectEditing();m_occViewWidget->cancelWallSketch();}
    if (m_smokeviewHostWidget) {
        m_smokeviewHostWidget->closeViewer();
    }
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = std::make_unique<FcProject>(QStringLiteral("Untitled"));
    m_project->setDisplayUnit(
        displayUnitFromSetting(ApplicationSettingsStore().load().defaultUnit));
    if (m_undoStack) m_undoStack->clear();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    m_projectResultDirectory.clear();
    m_projectSolverExecutable = FdsRunner::configuredExecutable();
    m_projectParallelProcessCount = 1;
    m_nextBoxNumber = 1;
    m_nextGroupNumber = 1;
    m_propertiesWidget->clear();
    m_reloadResultsAction->setEnabled(false);
    m_closeResultsAction->setEnabled(false);
    m_openSmokeviewAction->setEnabled(false);
    m_openNativeResultsAction->setEnabled(false);
    m_openSmokeAnimationAction->setEnabled(false);
    m_openSliceAnimationAction->setEnabled(false);
    m_openParticleAnimationAction->setEnabled(false);
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(QStringLiteral("[Info] Project created: Untitled"));
    CrashDiagnostics::recordOperation(QStringLiteral("New project created"));
    statusBar()->showMessage(QStringLiteral("New project created."),
                             kStatusMessageDurationMs);
}

void MainWindow::resetProjectRuntimeSettings()
{
    m_projectResultDirectory.clear();
    m_projectSolverExecutable = FdsRunner::configuredExecutable();
    m_projectParallelProcessCount = 1;
}

FcProjectRuntimeSettings MainWindow::currentRuntimeSettings() const
{
    FcProjectRuntimeSettings runtimeSettings;
    runtimeSettings.resultDirectory = m_projectResultDirectory;
    if (runtimeSettings.resultDirectory.isEmpty() && !m_currentFdsPath.isEmpty()) {
        runtimeSettings.resultDirectory = QFileInfo(m_currentFdsPath).absolutePath();
    }
    runtimeSettings.solverExecutable = FdsRunner::configuredExecutable();
    runtimeSettings.parallelProcessCount = m_projectParallelProcessCount;
    return runtimeSettings;
}

void MainWindow::performAutoSave(const QString& reason)
{
    if (!m_project || !m_recoveryManager || !m_project->isModified()) return;
    QString snapshotPath;
    QString error;
    if (!m_recoveryManager->writeSnapshot(*m_project,
                                          currentRuntimeSettings(),
                                          m_currentProjectPath,
                                          reason,
                                          &snapshotPath,
                                          &error)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Warning] Recovery snapshot failed: %1").arg(error));
        return;
    }
    // Retire the previous snapshot of this window only after the new copy is safe.
    // This also handles unnamed projects without grouping unrelated Untitled work.
    if (!m_lastRecoverySnapshotPath.isEmpty()) {
        QString discardError;
        if (!m_recoveryManager->discard(m_lastRecoverySnapshotPath, &discardError))
            m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(discardError));
    }
    m_lastRecoverySnapshotPath = snapshotPath;
    QSettings settings;
    const int maximumFiles = qBound(
        1, settings.value(QStringLiteral("Recovery/MaximumFiles"), 10).toInt(), 100);
    m_recoveryManager->prune(maximumFiles);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Recovery snapshot created: %1")
            .arg(QDir::toNativeSeparators(snapshotPath)));
    CrashDiagnostics::recordOperation(QStringLiteral("Recovery snapshot created"));
    refreshStartPage();
}

void MainWindow::clearCurrentRecovery()
{
    if (!m_recoveryManager) return;
    if (!m_lastRecoverySnapshotPath.isEmpty()) {
        m_recoveryManager->discard(m_lastRecoverySnapshotPath);
        m_lastRecoverySnapshotPath.clear();
    }
    if (!m_currentProjectPath.isEmpty()) {
        m_recoveryManager->discardForProject(m_currentProjectPath);
    }
}

bool MainWindow::confirmProjectReplacement(const QString& operationDescription)
{
    const auto finish = [this](bool confirmed) {
        if (confirmed) {
            if (m_transformGizmoAction) m_transformGizmoAction->setChecked(false);
            for (OccViewWidget* view : {m_occViewWidget, m_planViewWidget}) {
                if (!view) continue;
                view->endDirectEditing(); view->cancelWallSketch();
            }
        }
        return confirmed;
    };
    if (!m_project || !m_project->isModified()) return finish(true);
    performAutoSave(QStringLiteral("Before %1").arg(operationDescription));
    if (qEnvironmentVariable("FIRECAE_AUTOMATION_DISCARD_UNSAVED") ==
        QStringLiteral("1")) {
        clearCurrentRecovery();
        return finish(true);
    }
    QMessageBox prompt(QMessageBox::Warning, u("Unsaved Project"),
                       u("The current project has unsaved changes. Save them before %1?")
                           .arg(operationDescription),
                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                       this);
    prompt.setObjectName(QStringLiteral("UnsavedProjectMessageBox"));
    prompt.setTextFormat(Qt::PlainText);
    prompt.setDefaultButton(QMessageBox::Save);
    prompt.setEscapeButton(QMessageBox::Cancel);
    const auto answer = static_cast<QMessageBox::StandardButton>(prompt.exec());
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Discard) {
        clearCurrentRecovery();
        return finish(true);
    }
    saveCurrentProject();
    return finish(m_project && !m_project->isModified());
}

void MainWindow::offerRecoveryOnStartup()
{
    if (!m_recoveryManager) return;
    QList<ProjectRecoveryEntry> available;
    for (const auto& item : m_recoveryManager->entries()) {
        if (!item.acknowledged) available.append(item);
    }
    if (available.isEmpty()) return;
    const ProjectRecoveryEntry entry = available.constFirst();
    QMessageBox dialog(QMessageBox::Warning,
                       u("Recover Project"),
                       u("A recovery copy of '%1' from %2 is available.\n\nRecovery copies never overwrite the formal project.")
                           .arg(entry.projectName,
                                QLocale::system().toString(entry.createdAt.toLocalTime(),
                                                           QLocale::ShortFormat)),
                       QMessageBox::NoButton,
                       this);
    QPushButton* recoverButton = dialog.addButton(u("Recover"), QMessageBox::AcceptRole);
    QPushButton* discardButton = dialog.addButton(u("Delete This Recovery Copy"), QMessageBox::DestructiveRole);
    QPushButton* keepButton = dialog.addButton(u("Keep Copies, Stop Reminding"), QMessageBox::ActionRole);
    QPushButton* pathButton = dialog.addButton(u("View Path"), QMessageBox::ActionRole);
    dialog.addButton(QMessageBox::Cancel);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setInformativeText(u("%1 recovery copies await review. Kept copies remain available on the start page; new recovery copies will still prompt.").arg(available.size()));
    dialog.exec();
    if (dialog.clickedButton() == recoverButton) {
        if (openProjectFile(entry.snapshotPath)) {
            m_currentProjectPath.clear();
            m_lastRecoverySnapshotPath = entry.snapshotPath;
            m_project->setModified(true);
            updateWindowTitle();
            m_messageWidget->appendMessage(
                QStringLiteral("[Info] Recovered project copy. Use Save As to keep it."));
        }
    } else if (dialog.clickedButton() == discardButton) {
        QString error;
        if (!m_recoveryManager->discard(entry.snapshotPath, &error))
            QMessageBox::warning(this, u("Recover Project"), error);
    } else if (dialog.clickedButton() == keepButton) {
        for (const auto& item : available) {
            QString error;
            if (!m_recoveryManager->acknowledge(item.snapshotPath, &error)) {
                QMessageBox::warning(this, u("Recover Project"), error);
                break;
            }
        }
    } else if (dialog.clickedButton() == pathButton) {
        QMessageBox::information(this,
                                 u("Recovery Path"),
                                 QDir::toNativeSeparators(entry.snapshotPath));
    }
    refreshStartPage();
}

void MainWindow::addRecentProject(const QString& filePath)
{
    ApplicationSettingsStore().addRecentProject(filePath);
    refreshStartPage();
}

bool MainWindow::confirmTutorialReplacement()
{
    return confirmProjectReplacement(u("opening the tutorial"));
}

void MainWindow::rebuildFdsScene(bool fitAll, const QString& selectedObjectId)
{
    if(m_occViewWidget) m_occViewWidget->endDirectEditing();
    if (!m_project || !m_occViewWidget || !m_occViewWidget->displayManager()) return;
    m_isolationActive = false;
    if (m_modelTreeWidget) m_modelTreeWidget->setIsolationActive(false);
    if (m_restoreVisibilityAction) {
        m_restoreVisibilityAction->setText(u("Show All Objects"));
    }
    if (m_nativeResultViewerWidget) m_nativeResultViewerWidget->setProject(m_project.get());
    if (!m_fdsSceneSynchronizer) {
        m_fdsSceneSynchronizer = std::make_unique<FdsSceneSynchronizer>(
            m_occViewWidget->displayManager());
    }
    // Geometry presentations are not owned by FdsSceneSynchronizer.  Clear the
    // view before rebuilding so Undo/delete cannot leave an orphaned AIS_Shape
    // behind after its FcObject has left the document tree.
    m_occViewWidget->displayManager()->clear();
    const FdsSceneSyncResult result = m_fdsSceneSynchronizer->rebuild(*m_project);
    GeometryDisplayManager* displayManager = m_occViewWidget->displayManager();
    const auto displayProjectGeometry = [this](GeometryDisplayManager* manager,
                                                bool planView) {
        if (!manager) return;
        QVector<std::shared_ptr<FcGeometryObject>> allGeometry;
        collectGeometryObjects(m_project->document()->geometryGroup(), allGeometry);
        const std::function<void(const FcObject::Ptr&, bool)> displayGeometry =
        [&](const FcObject::Ptr& object, bool parentVisible) {
            if (!object) return;
            const bool visible = parentVisible && object->isVisible();
            if (object->type() == FcObjectType::IfcModel) {
                if (visible) {
                    if (const auto ifcRoot =
                            std::dynamic_pointer_cast<FcIfcObject>(object)) {
                        manager->displayIfcModel(ifcRoot);
                        updateDisplayVisibilityRecursive(object, manager);
                    }
                }
                return;
            }
            if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
                if (geometry->hasShape()) {
                    const TopoDS_Shape displayShape=GeometryEditDependencyService::displayShape(*geometry,allGeometry);
                    auto presentation=std::make_shared<FcGeometryObject>(geometry->name(),displayShape);
                    copyGeometrySemantics(*geometry,*presentation);
                    presentation->restorePersistentId(geometry->id());
                    manager->displayObject(presentation);
                    if (!visible || (!planView &&
                        geometry->geometryKind() == FcGeometryKind::BackgroundImage)) {
                        manager->hideObject(geometry->id());
                    }
                }
            }
            for (const FcObject::Ptr& child : object->children()) {
                displayGeometry(child, visible);
            }
        };
        displayGeometry(m_project->document()->geometryGroup(), true);
    };
    displayProjectGeometry(displayManager, false);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] 3D FDS scene synchronized: %1 business objects, %2 presentations.")
            .arg(result.businessObjectCount)
            .arg(result.presentationCount));
    for (const QString& warning : result.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    if (!selectedObjectId.isEmpty() &&
        m_occViewWidget->displayManager()->contains(selectedObjectId)) {
        m_occViewWidget->displayManager()->selectObject(selectedObjectId);
    }
    if (fitAll && displayManager->displayedObjectCount() > 0) m_occViewWidget->fitAll();

    if (m_planViewWidget && m_planViewWidget->displayManager()) {
        if (!m_planSceneSynchronizer) {
            m_planSceneSynchronizer = std::make_unique<FdsSceneSynchronizer>(
                m_planViewWidget->displayManager());
        }
        m_planViewWidget->displayManager()->clear();
        m_planSceneSynchronizer->rebuild(*m_project);
        GeometryDisplayManager* planManager = m_planViewWidget->displayManager();
        displayProjectGeometry(planManager, true);
        if (!selectedObjectId.isEmpty() && planManager->contains(selectedObjectId)) {
            planManager->selectObject(selectedObjectId);
        }
        if (fitAll && planManager->displayedObjectCount() > 0) {
            m_planViewWidget->setPerspective(false);
            m_planViewWidget->setOrientation(OccViewOrientation::Top);
            m_planViewWidget->fitAll();
        }
    }
    applyFloorViewState(m_activeFloorId);
    refreshRecordView();
    refreshTutorialContext();
}

void MainWindow::applyFloorViewState(const QString& floorObjectId)
{
    m_activeFloorId = floorObjectId;
    std::shared_ptr<FcFloorObject> floor;
    if (m_project && m_project->document() && !floorObjectId.isEmpty()) {
        floor = std::dynamic_pointer_cast<FcFloorObject>(
            m_project->document()->findObject(floorObjectId));
    }
    const bool clipping = floor && floor->clippingEnabled() &&
                          floor->clippingRange().isValid();
    const FcFloorClipRange range = floor ? floor->clippingRange() : FcFloorClipRange{};
    if (m_occViewWidget) {
        m_occViewWidget->setClippingBox(clipping, range.xMin, range.xMax,
                                       range.yMin, range.yMax,
                                       range.zMin, range.zMax);
    }
    if (m_planViewWidget) {
        m_planViewWidget->setClippingBox(clipping, range.xMin, range.xMax,
                                        range.yMin, range.yMax,
                                        range.zMin, range.zMax);
    }
    if (!m_project || !m_project->document()) return;
    GeometryDisplayManager* planManager = m_planViewWidget
        ? m_planViewWidget->displayManager() : nullptr;
    GeometryDisplayManager* modelManager = m_occViewWidget
        ? m_occViewWidget->displayManager() : nullptr;
    const std::function<void(const FcObject::Ptr&)> updateBackgrounds =
        [&](const FcObject::Ptr& object) {
            if (!object) return;
            const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object);
            if (geometry && geometry->geometryKind() == FcGeometryKind::BackgroundImage) {
                const QString owner = geometry->geometryParameters()
                                          .value(QStringLiteral("floorBackgroundOwnerUuid"))
                                          .toString();
                if (!owner.isEmpty()) {
                    if (modelManager && modelManager->contains(geometry->id())) {
                        if (geometry->isVisible() && owner == floorObjectId) {
                            modelManager->showObject(geometry->id());
                        } else {
                            modelManager->hideObject(geometry->id());
                        }
                    }
                    if (planManager && planManager->contains(geometry->id())) {
                        if (geometry->isVisible() && owner == floorObjectId) {
                            planManager->showObject(geometry->id());
                        } else {
                            planManager->hideObject(geometry->id());
                        }
                    }
                }
            }
            for (const FcObject::Ptr& child : object->children()) updateBackgrounds(child);
        };
    updateBackgrounds(m_project->document()->geometryGroup());
}

QMenu* MainWindow::createModelContextMenu(const QStringList& objectIds)
{
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("ModelContextMenu"));
    QStringList ids;
    if (m_project && m_project->document()) {
        for (const QString& id : objectIds) {
            if (!ids.contains(id) && m_project->document()->findObject(id)) ids.append(id);
        }
    }
    const auto add = [menu](const QString& name, const QString& text,
                            const std::function<void()>& callback) {
        auto* action = menu->addAction(text);
        action->setObjectName(name);
        QObject::connect(action, &QAction::triggered, menu, callback);
        return action;
    };
    if (!ids.isEmpty()) {
        // The tree and all existing edit commands refer to the same UUID set.
        updateMultiSelection(ids, true, true);
        add(QStringLiteral("ContextHideSelected"), u("Hide Selected"),
            [this]() { hideSelectedObjects(); });
        add(QStringLiteral("ContextIsolateSelected"), u("Show Only Selected"),
            [this, ids]() { isolateObjects(ids); });
    }
    if (m_isolationActive) {
        add(QStringLiteral("ContextExitIsolation"), u("Exit Isolation"),
            [this]() { if (m_isolationActive) restoreModelVisibility(); });
    }
    add(QStringLiteral("ContextShowAll"), u("Show All Objects"),
        [this]() { showAllObjects(); });
    menu->addAction(m_fitAllAction);
    if (!ids.isEmpty()) {
        menu->addAction(m_fitSelectionAction);
        add(QStringLiteral("ContextLocateInTree"), u("Locate in Model Tree"), [this, ids]() {
            m_modelTreeDock->show(); m_modelTreeDock->raise();
            m_modelTreeWidget->selectObjectsByIds(ids);
            if (ids.size() == 1) m_modelTreeWidget->selectObjectById(ids.constFirst(), true);
        });
        menu->addSeparator();
        // Only offer operations backed by an applicable command, not placeholders.
        for (QAction* action : {m_transformAction, m_copyMoveAction, m_mirrorAction,
                                m_groupAction, m_batchRenameAction, m_assignSurfacesAction,
                                m_duplicateAction, m_deleteAction}) {
            if (action && action->isEnabled()) menu->addAction(action);
        }
        if (ids.size() == 1) {
            const QString id = ids.constFirst();
            add(QStringLiteral("ContextSelectSameType"), u("Select Same Type"), [this, id]() {
                const auto source = m_project->document()->findObject(id);
                if (!source) return;
                const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(source);
                const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(source);
                QStringList matching;
                std::function<void(const FcObject::Ptr&)> visit = [&](const FcObject::Ptr& item) {
                    if (!item) return;
                    bool same = item->type() == source->type();
                    if (same && ifc) {
                        const auto candidate = std::dynamic_pointer_cast<FcIfcObject>(item);
                        same = candidate && candidate->ifcClass() == ifc->ifcClass();
                    }
                    if (same && geometry) {
                        const auto candidate = std::dynamic_pointer_cast<FcGeometryObject>(item);
                        same = candidate && candidate->geometryKind() == geometry->geometryKind();
                    }
                    if (same) matching.append(item->id());
                    for (const auto& child : item->children()) visit(child);
                };
                for (const auto& group : m_project->document()->groups()) visit(group);
                updateMultiSelection(matching, true, true);
            });
            menu->addSeparator();
            add(QStringLiteral("ContextViewNormal"), u("View Normal to Plane"),
                [this, id]() { viewNormalToObject(id); });
            add(QStringLiteral("ContextProperties"), u("Properties..."),
                [this, id]() { openObjectProperties(id); });
        }
    }
    menu->addAction(m_restoreViewAction);
    return menu;
}

void MainWindow::openObjectProperties(const QString& objectId)
{
    if (!m_project || !m_project->document()) return;
    const auto object = m_project->document()->findObject(objectId);
    if (!object) return;
    if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object);
        geometry && isLockedForModification(object.get()) &&
        geometry->geometryKind() != FcGeometryKind::Generic &&
        geometry->geometryKind() != FcGeometryKind::BackgroundImage) {
        BuildingElementDialog dialog(geometry->geometryKind(), m_project.get(), this);
        dialog.setExistingObject(geometry);
        dialog.setReadOnly(u("This object is locked. Properties are read-only."));
        connectGeometryPreview(dialog);
        dialog.exec(); m_occViewWidget->clearGeometryPreview(); return;
    }
    if (!isLockedForModification(object.get())) {
        if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object);
            geometry && geometry->geometryKind() != FcGeometryKind::Generic) {
            editBuildingGeometry(objectId); return;
        }
        if (std::dynamic_pointer_cast<FcFloorObject>(object)) {
            editFloorObject(objectId); return;
        }
        if (std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
            editFdsObject(objectId); return;
        }
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ReadOnlyObjectPropertiesDialog"));
    dialog.setProperty("objectId", objectId);
    dialog.setWindowTitle(u("Properties...") + QStringLiteral(" — ") + object->name());
    dialog.resize(640, 580);
    auto* layout = new QVBoxLayout(&dialog);
    auto* reason = new QLabel(isLockedForModification(object.get())
        ? u("This object is locked. Properties are read-only.")
        : std::dynamic_pointer_cast<FcGeometryObject>(object)
            ? u("This geometry has no editable construction parameters. Its properties are read-only.")
            : u("IFC/reference information is read-only. It is not an editable FDS obstruction."), &dialog);
    reason->setWordWrap(true); layout->addWidget(reason);
    auto* properties = new PropertiesWidget(&dialog);
    properties->showObject(object.get()); layout->addWidget(properties, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::setObjectVisibility(const QString& objectId, bool visible)
{
    if (!m_project || !m_project->document()) return;
    if (m_isolationActive) restoreModelVisibility();
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (!object) return;
    QVector<FcObject::Ptr> objects;
    collectObjectSubtree(object, objects);
    QVector<bool> before;
    before.reserve(objects.size());
    for (const FcObject::Ptr& current : objects) before.append(current->isVisible());
    const QVector<bool> after(objects.size(), visible);
    const auto applyVisibility = [this, objects, objectId](const QVector<bool>& values) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setVisible(values[index]);
        }
        m_project->setModified(true);
        rebuildFdsScene(false, objects.constFirst()->isVisible() ? objectId : QString{});
        for (const auto& group : m_project->document()->groups()) {
            updateDisplayVisibilityRecursive(group, m_occViewWidget->displayManager());
        }
        m_modelTreeWidget->refresh();
        if (objects.constFirst()->isVisible()) {
            m_modelTreeWidget->selectObjectById(objectId);
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        visible ? u("Show Object") : u("Hide Object"),
        [applyVisibility, after]() { applyVisibility(after); },
        [applyVisibility, before]() { applyVisibility(before); }));
    statusBar()->showMessage(visible ? u("Object shown.") : u("Object hidden."),
                             kStatusMessageDurationMs);
}

void MainWindow::isolateObject(const QString& objectId)
{
    isolateObjects({objectId});
}

void MainWindow::isolateObjects(const QStringList& objectIds)
{
    if (!m_project || !m_project->document()) return;
    QSet<QString> visibleIds;
    for (const QString& objectId : objectIds) {
        const FcObject::Ptr object = m_project->document()->findObject(objectId);
        if (!object) continue;
        QVector<FcObject::Ptr> selectedSubtree;
        collectObjectSubtree(object, selectedSubtree);
        for (const FcObject::Ptr& current : selectedSubtree) visibleIds.insert(current->id());
        for (FcObject* ancestor = object->parent(); ancestor; ancestor = ancestor->parent()) {
            visibleIds.insert(ancestor->id());
        }
    }
    if (visibleIds.isEmpty()) return;
    for (const auto& group : m_project->document()->groups()) {
        setTemporaryDisplayVisibilityRecursive(
            group, m_occViewWidget ? m_occViewWidget->displayManager() : nullptr,
            visibleIds);
        setTemporaryDisplayVisibilityRecursive(
            group, m_planViewWidget ? m_planViewWidget->displayManager() : nullptr,
            visibleIds);
    }
    m_isolationActive = true;
    m_modelTreeWidget->setIsolationActive(true);
    m_restoreVisibilityAction->setText(u("Exit Isolation"));
    m_modelTreeWidget->selectObjectsByIds(objectIds);
    statusBar()->showMessage(u("Object isolated."), kStatusMessageDurationMs);
}

void MainWindow::showAllObjects()
{
    if (m_isolationActive) restoreModelVisibility();
    restoreModelVisibility();
}

void MainWindow::restoreModelVisibility()
{
    if (!m_project || !m_project->document()) return;
    if (m_isolationActive) {
        for (const auto& group : m_project->document()->groups()) {
            restoreDisplayVisibilityRecursive(
                group, m_occViewWidget ? m_occViewWidget->displayManager() : nullptr);
            restoreDisplayVisibilityRecursive(
                group, m_planViewWidget ? m_planViewWidget->displayManager() : nullptr);
        }
        m_isolationActive = false;
        m_modelTreeWidget->setIsolationActive(false);
        m_restoreVisibilityAction->setText(u("Show All Objects"));
        statusBar()->showMessage(u("Isolation exited; previous visibility restored."),
                                 kStatusMessageDurationMs);
        return;
    }

    QVector<FcObject::Ptr> objects;
    for (const auto& group : m_project->document()->groups()) {
        collectObjectSubtree(group, objects);
    }
    QVector<bool> before;
    before.reserve(objects.size());
    bool hasHiddenObjects = false;
    for (const FcObject::Ptr& current : objects) {
        before.append(current->isVisible());
        hasHiddenObjects = hasHiddenObjects || !current->isVisible();
    }
    if (!hasHiddenObjects) {
        statusBar()->showMessage(u("All objects are already visible."),
                                 kStatusMessageDurationMs);
        return;
    }
    const QVector<bool> after(objects.size(), true);
    const auto applyVisibility = [this, objects](const QVector<bool>& values) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setVisible(values[index]);
        }
        m_project->setModified(true);
        rebuildFdsScene(false, m_modelTreeWidget->selectedObjectId());
        m_modelTreeWidget->refresh();
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Show All Objects"),
        [applyVisibility, after]() { applyVisibility(after); },
        [applyVisibility, before]() { applyVisibility(before); }));
    statusBar()->showMessage(u("All objects shown."), kStatusMessageDurationMs);
}

void MainWindow::viewNormalToObject(const QString& objectId)
{
    if (!m_project || !m_project->document() || !m_occViewWidget) return;
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (!object) return;

    std::array<double, 3> normal{0.0, 0.0, 0.0};
    bool planar = false;
    const auto normalFromBounds = [&normal, &planar](const FcFdsBounds& bounds) {
        constexpr double tolerance = 1.0e-9;
        const bool thinX = std::abs(bounds.xMax - bounds.xMin) <= tolerance;
        const bool thinY = std::abs(bounds.yMax - bounds.yMin) <= tolerance;
        const bool thinZ = std::abs(bounds.zMax - bounds.zMin) <= tolerance;
        if (static_cast<int>(thinX) + static_cast<int>(thinY) +
                static_cast<int>(thinZ) != 1) {
            return;
        }
        normal = thinX ? std::array<double, 3>{1.0, 0.0, 0.0}
                       : thinY ? std::array<double, 3>{0.0, 1.0, 0.0}
                               : std::array<double, 3>{0.0, 0.0, 1.0};
        planar = true;
    };

    if (std::dynamic_pointer_cast<FcFloorObject>(object)) {
        normal = {0.0, 0.0, 1.0};
        planar = true;
    } else if (const auto vent = std::dynamic_pointer_cast<FcFdsVent>(object)) {
        normalFromBounds(vent->bounds());
    } else if (const auto obstruction =
                   std::dynamic_pointer_cast<FcFdsObstruction>(object)) {
        normalFromBounds(obstruction->bounds());
    } else if (const auto namelist =
                   std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        const QStringList xb = namelist->parameterValue(QStringLiteral("XB"))
                                   .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (xb.size() == 6) {
            FcFdsBounds bounds;
            double* values[] = {&bounds.xMin, &bounds.xMax, &bounds.yMin,
                                &bounds.yMax, &bounds.zMin, &bounds.zMax};
            bool valid = true;
            for (int index = 0; index < 6; ++index) {
                bool ok = false;
                *values[index] = xb[index].trimmed().toDouble(&ok);
                valid = valid && ok;
            }
            if (valid) normalFromBounds(bounds);
        }
        if (!planar) {
            for (const auto& axis : std::array<std::pair<const char*,
                                                         std::array<double, 3>>, 3>{{
                     {"PBX", {1.0, 0.0, 0.0}},
                     {"PBY", {0.0, 1.0, 0.0}},
                     {"PBZ", {0.0, 0.0, 1.0}}}}) {
                if (!namelist->parameterValue(QString::fromLatin1(axis.first))
                         .trimmed().isEmpty()) {
                    normal = axis.second;
                    planar = true;
                    break;
                }
            }
        }
        if (!planar) {
            QString boundary = namelist->parameterValue(QStringLiteral("MB"))
                                   .remove(QLatin1Char('\''))
                                   .remove(QLatin1Char('"'))
                                   .trimmed().toUpper();
            if (boundary.startsWith(QLatin1Char('X'))) normal = {1.0, 0.0, 0.0};
            if (boundary.startsWith(QLatin1Char('Y'))) normal = {0.0, 1.0, 0.0};
            if (boundary.startsWith(QLatin1Char('Z'))) normal = {0.0, 0.0, 1.0};
            planar = normal[0] != 0.0 || normal[1] != 0.0 || normal[2] != 0.0;
        }
    } else if (const auto geometry =
                   std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        if (geometry->geometryKind() == FcGeometryKind::Wall ||
            BuildingGeometryService::isOpeningKind(geometry->geometryKind())) {
            const BuildingGeometryRequest request =
                BuildingGeometryService::requestFromParameters(
                    geometry->geometryKind(), geometry->geometryParameters());
            const double dx = request.endX - request.x;
            const double dy = request.endY - request.y;
            const double length = std::hypot(dx, dy);
            if (length > 1.0e-9) {
                normal = {dy / length, -dx / length, 0.0};
                planar = true;
            }
        } else if (geometry->geometryKind() == FcGeometryKind::Slab ||
                   geometry->geometryKind() == FcGeometryKind::Roof ||
                   geometry->geometryKind() == FcGeometryKind::BackgroundImage) {
            normal = {0.0, 0.0, 1.0};
            planar = true;
        } else if (geometry->hasShape()) {
            const QVector<GeometryFaceInfo> faces =
                BuildingGeometryService::faceInfos(geometry->shape());
            if (faces.size() == 1) {
                normal = faces.constFirst().normal;
                planar = true;
            }
        }
    } else if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
        if (ifc->hasShape()) {
            const QVector<GeometryFaceInfo> faces =
                BuildingGeometryService::faceInfos(ifc->shape());
            if (faces.size() == 1) {
                normal = faces.constFirst().normal;
                planar = true;
            }
        }
    }

    if (!planar) {
        statusBar()->showMessage(
            u("The selected object is not a single planar object."),
            kStatusMessageDurationMs);
        return;
    }

    showModelWorkspace(0);
    m_occViewWidget->saveView();
    m_occViewWidget->setPerspective(false);
    m_occViewWidget->setProjectionDirection(normal[0], normal[1], normal[2]);
    if (m_occViewWidget->displayManager() &&
        m_occViewWidget->displayManager()->contains(objectId)) {
        m_occViewWidget->displayManager()->selectObject(objectId);
    }
    if (!m_occViewWidget->fitSelection()) m_occViewWidget->fitAll();
    statusBar()->showMessage(
        u("Viewing normal to the selected plane. Use Restore View to return."),
        kStatusMessageDurationMs);
}

bool MainWindow::openProjectFile(const QString& filePath)
{
    const ApplicationSettings applicationSettings = ApplicationSettingsStore().load();
    const QFileInfo openingFile(filePath);
    const qint64 warningBytes = static_cast<qint64>(
        applicationSettings.largeFileWarningMegabytes) * 1024 * 1024;
    if (openingFile.exists() && openingFile.size() > warningBytes &&
        !qEnvironmentVariableIsSet("FIRECAE_AUTOMATION_DISCARD_UNSAVED")) {
        const auto answer = QMessageBox::warning(
            this, u("Large Project"),
            u("This project is %1 MB. Opening and rebuilding its geometry may take time. Continue?")
                .arg(openingFile.size() / (1024.0 * 1024.0), 0, 'f', 1),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return false;
    }
    if (applicationSettings.backupBeforeOpen && openingFile.exists()) {
        ProjectRecoveryManager backupManager;
        QString backupPath;
        QString backupError;
        if (backupManager.backupProjectFile(openingFile.absoluteFilePath(),
                                            applicationSettings.autoSaveMaximumFiles,
                                            &backupPath, &backupError)) {
            if (m_messageWidget) m_messageWidget->appendMessage(
                QStringLiteral("[Info] Pre-open project backup: %1")
                    .arg(QDir::toNativeSeparators(backupPath)));
        } else if (m_messageWidget) {
            m_messageWidget->appendMessage(
                QStringLiteral("[Warning] Pre-open backup failed: %1").arg(backupError));
        }
    }
    FcProjectLoadResult loaded = FcProjectSerializer::load(filePath);
    if (!loaded.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Project open failed: %1").arg(loaded.errorMessage));
        statusBar()->showMessage(u("Project open failed."), kStatusMessageDurationMs);
        return false;
    }
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    m_occViewWidget->endDirectEditing();
    m_occViewWidget->cancelWallSketch();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = std::move(loaded.project);
    if (m_undoStack) m_undoStack->clear();
    m_currentProjectPath = QFileInfo(filePath).absoluteFilePath();
    m_currentFdsPath.clear();
    m_projectResultDirectory = loaded.runtimeSettings.resultDirectory;
    m_projectSolverExecutable = loaded.runtimeSettings.solverExecutable;
    m_projectParallelProcessCount = loaded.runtimeSettings.parallelProcessCount;
    if (!m_projectSolverExecutable.isEmpty() &&
        FdsRunner::validateExecutable(m_projectSolverExecutable).isEmpty()) {
        FdsRunner::setConfiguredExecutable(m_projectSolverExecutable);
    }
    m_nextBoxNumber = 1;
    m_nextGroupNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Project opened: %1")
            .arg(QDir::toNativeSeparators(m_currentProjectPath)));
    for (const QString& warning : loaded.warnings) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Warning] %1").arg(warning));
    }
    CrashDiagnostics::recordOperation(QStringLiteral("Project opened"));
    addRecentProject(m_currentProjectPath);
    statusBar()->showMessage(u("Project opened."), kStatusMessageDurationMs);
    return true;
}

bool MainWindow::saveProjectFile(const QString& filePath)
{
    if (!m_project || filePath.trimmed().isEmpty()) return false;
    QString normalizedPath = filePath;
    if (QFileInfo(normalizedPath).suffix().isEmpty()) {
        normalizedPath += QStringLiteral(".firecae");
    }
    QString error;
    const FcProjectRuntimeSettings runtimeSettings = currentRuntimeSettings();
    if (!FcProjectSerializer::save(*m_project, normalizedPath, runtimeSettings,
                                   &error)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Project save failed: %1").arg(error));
        statusBar()->showMessage(u("Project save failed."), kStatusMessageDurationMs);
        return false;
    }
    m_currentProjectPath = QFileInfo(normalizedPath).absoluteFilePath();
    m_project->setModified(false);
    clearCurrentRecovery();
    addRecentProject(m_currentProjectPath);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Project saved: %1")
            .arg(QDir::toNativeSeparators(m_currentProjectPath)));
    CrashDiagnostics::recordOperation(QStringLiteral("Project saved"));
    statusBar()->showMessage(u("Project saved."), kStatusMessageDurationMs);
    refreshTutorialContext();
    return true;
}

void MainWindow::chooseAndOpenProject()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, u("Open Project"), QString(),
        QStringLiteral("FireCAE Project (*.firecae *.fcae);;All Files (*.*)"));
    if (filePath.isEmpty()) return;
    if (!confirmProjectReplacement(u("opening another project"))) return;
    if (!openProjectFile(filePath)) {
        QMessageBox::critical(this, u("Project Open Failed"),
                              u("See the Messages panel for details."));
    }
}

void MainWindow::saveCurrentProject()
{
    if (m_currentProjectPath.isEmpty()) {
        saveCurrentProjectAs();
        return;
    }
    if (!saveProjectFile(m_currentProjectPath)) {
        QMessageBox::critical(this, u("Project Save Failed"),
                              u("See the Messages panel for details."));
    }
}

void MainWindow::saveCurrentProjectAs()
{
    if (!m_project) return;
    const QString suggested = m_currentProjectPath.isEmpty()
                                  ? m_project->chid() + QStringLiteral(".firecae")
                                  : m_currentProjectPath;
    const QString filePath = QFileDialog::getSaveFileName(
        this, u("Save Project As"), suggested,
        QStringLiteral("FireCAE Project (*.firecae);;All Files (*.*)"));
    if (filePath.isEmpty()) return;
    if (!saveProjectFile(filePath)) {
        QMessageBox::critical(this, u("Project Save Failed"),
                              u("See the Messages panel for details."));
    }
}

void MainWindow::applyApplicationSettings(const ApplicationSettings& settings)
{
    UiLanguage language = UiLanguage::English;
    if (settings.language == QStringLiteral("zh_CN") ||
        (settings.language == QStringLiteral("system") &&
         QLocale::system().language() == QLocale::Chinese)) {
        language = UiLanguage::ChineseSimplified;
    }
    setInterfaceLanguage(language);

    QString fdsExecutable = settings.fdsExecutable;
    if (!FdsRunner::validateExecutable(fdsExecutable).isEmpty()) {
        fdsExecutable = FdsRunner::detectExecutable();
    }
    FdsRunner::setConfiguredExecutable(fdsExecutable);
    SolverBackendRegistry::setConfiguredMpiExecutable(settings.mpiExecutable);
    QString smokeviewExecutable = settings.smokeviewExecutable;
    if (!SmokeviewLauncher::validateExecutable(smokeviewExecutable).isEmpty()) {
        smokeviewExecutable = SmokeviewLauncher::detectExecutable();
    }
    SmokeviewLauncher::setConfiguredExecutable(smokeviewExecutable);
    if (m_currentProjectPath.isEmpty()) {
        m_projectParallelProcessCount = settings.mpiProcessCount;
    }
    if (m_autoSaveTimer) {
        m_autoSaveTimer->setInterval(settings.autoSaveIntervalMinutes * 60 * 1000);
        settings.autoSaveEnabled ? m_autoSaveTimer->start() : m_autoSaveTimer->stop();
    }
    if (m_occViewWidget) {
        const QColor background(settings.backgroundColor);
        if (background.isValid()) m_occViewWidget->setBackgroundColor(background);
    }
    if (settings.theme == QStringLiteral("dark")) {
        qApp->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #2b2d31; color: #e6e6e6; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QTreeView,QTableView,QListView {"
            " background-color: #202226; color: #f0f0f0; }"));
    } else if (settings.theme == QStringLiteral("light")) {
        qApp->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #f5f5f5; color: #202020; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QTreeView,QTableView,QListView {"
            " background-color: white; color: #202020; }"));
    } else {
        qApp->setStyleSheet(QString{});
    }
}

void MainWindow::showApplicationSettings()
{
    ApplicationSettingsStore store;
    ApplicationSettingsDialog dialog(store.load(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    const ApplicationSettings updated = dialog.settings();
    if (!store.save(updated, &error)) {
        QMessageBox::critical(this, u("Settings Save Failed"), error);
        return;
    }
    applyApplicationSettings(updated);
    refreshStartPage();
    CrashDiagnostics::recordOperation(QStringLiteral("Application preferences updated"));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Versioned application settings were updated."));
    statusBar()->showMessage(u("Application settings updated."),
                             kStatusMessageDurationMs);
}

void MainWindow::showDiagnostics()
{
    const QString report = CrashDiagnostics::diagnosticReport(
        FdsRunner::configuredExecutable(),
        SolverBackendRegistry::configuredMpiExecutable(),
        SmokeviewLauncher::configuredExecutable(),
        m_recoveryManager ? m_recoveryManager->recoveryDirectory() : QString{});
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("DiagnosticsDialog"));
    dialog.setWindowTitle(u("FireCAE Diagnostics"));
    dialog.resize(850, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(report, &dialog);
    text->setObjectName(QStringLiteral("DiagnosticsReportEdit"));
    text->setReadOnly(true);
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* copy = buttons->addButton(u("Copy Report"),
                                           QDialogButtonBox::ActionRole);
    QPushButton* openLogs = buttons->addButton(u("Open Log Folder"),
                                               QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, &dialog, [report]() {
        QGuiApplication::clipboard()->setText(report);
    });
    connect(openLogs, &QPushButton::clicked, &dialog, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(CrashDiagnostics::logDirectory()));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::manageProjectResources()
{
    if (!m_project) return;
    ProjectResourcesDialog dialog(m_project.get(), m_currentProjectPath, this);
    dialog.exec();
    if (dialog.projectChanged()) {
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        updateWindowTitle();
        CrashDiagnostics::recordOperation(QStringLiteral("Project resources relinked"));
    }
}

void MainWindow::packageCurrentProject()
{
    if (!m_project) return;
    const ApplicationSettings settings = ApplicationSettingsStore().load();
    const QString initialDirectory = m_currentProjectPath.isEmpty()
        ? settings.defaultWorkingDirectory
        : QFileInfo(m_currentProjectPath).absolutePath();
    QString filePath = QFileDialog::getSaveFileName(
        this, u("Package Project"),
        QDir(initialDirectory).filePath(m_project->chid() + QStringLiteral(".firecaepkg")),
        QStringLiteral("FireCAE Package (*.firecaepkg);;All Files (*.*)"));
    if (filePath.isEmpty()) return;
    if (QFileInfo(filePath).suffix().isEmpty()) filePath += QStringLiteral(".firecaepkg");
    QString error;
    if (!ProjectResourceManager::packageProject(*m_project,
                                                currentRuntimeSettings(),
                                                m_currentProjectPath,
                                                filePath,
                                                &error)) {
        QMessageBox::critical(this, u("Package Project Failed"), error);
        return;
    }
    CrashDiagnostics::recordOperation(QStringLiteral("Portable project package created"));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Portable project package created (large results excluded): %1")
            .arg(QDir::toNativeSeparators(filePath)));
    statusBar()->showMessage(u("Project package created."), kStatusMessageDurationMs);
}

void MainWindow::unpackProjectPackage()
{
    const ApplicationSettings settings = ApplicationSettingsStore().load();
    const QString packagePath = QFileDialog::getOpenFileName(
        this, u("Unpack Project"), settings.defaultWorkingDirectory,
        QStringLiteral("FireCAE Package (*.firecaepkg);;All Files (*.*)"));
    if (packagePath.isEmpty()) return;
    const QString destination = QFileDialog::getExistingDirectory(
        this, u("Choose Unpack Directory"), QFileInfo(packagePath).absolutePath());
    if (destination.isEmpty()) return;
    if (!confirmProjectReplacement(u("opening an unpacked project"))) return;
    QString projectPath;
    QString error;
    if (!ProjectResourceManager::unpackProject(packagePath, destination,
                                               &projectPath, &error) ||
        !openProjectFile(projectPath)) {
        QMessageBox::critical(this, u("Unpack Project Failed"),
                              error.isEmpty() ? u("The unpacked project could not be opened.")
                                              : error);
        return;
    }
    CrashDiagnostics::recordOperation(QStringLiteral("Project package unpacked"));
    statusBar()->showMessage(u("Project package unpacked."), kStatusMessageDurationMs);
}

void MainWindow::copyCurrentProjectToDirectory()
{
    if (!m_project) return;
    const ApplicationSettings settings = ApplicationSettingsStore().load();
    const QString destination = QFileDialog::getExistingDirectory(
        this, u("Copy Project to Directory"),
        m_currentProjectPath.isEmpty() ? settings.defaultWorkingDirectory
                                       : QFileInfo(m_currentProjectPath).absolutePath());
    if (destination.isEmpty()) return;
    QString copiedPath;
    QString error;
    if (!ProjectResourceManager::copyProjectToDirectory(
            *m_project, currentRuntimeSettings(), m_currentProjectPath,
            destination, &copiedPath, &error)) {
        QMessageBox::critical(this, u("Copy Project Failed"), error);
        return;
    }
    CrashDiagnostics::recordOperation(QStringLiteral("Project copied to directory"));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Self-contained project copy created: %1")
            .arg(QDir::toNativeSeparators(copiedPath)));
    statusBar()->showMessage(u("Project copied."), kStatusMessageDurationMs);
}

void MainWindow::cleanUnusedResults()
{
    if (!m_project) return;
    QString root = m_projectResultDirectory;
    if (root.isEmpty() && !m_currentFdsPath.isEmpty()) {
        root = QFileInfo(m_currentFdsPath).absolutePath();
    }
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        QMessageBox::information(this, u("Clean Unused Results"),
                                 u("No project result directory is configured."));
        return;
    }
    const QStringList unused = ProjectResourceManager::unusedResultFiles(*m_project, root);
    if (unused.isEmpty()) {
        QMessageBox::information(this, u("Clean Unused Results"),
                                 u("No unused result files were found."));
        return;
    }
    const auto answer = QMessageBox::warning(
        this, u("Clean Unused Results"),
        u("Permanently remove %1 unreferenced file(s) from the result directory?\n"
          "This operation does not use the Recycle Bin.").arg(unused.size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    QStringList failures;
    const int removed = ProjectResourceManager::removeFiles(unused, root, &failures);
    CrashDiagnostics::recordOperation(
        QStringLiteral("Unused result cleanup: %1 removed, %2 failed")
            .arg(removed).arg(failures.size()));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Permanently removed %1 unused result file(s); %2 failed.")
            .arg(removed).arg(failures.size()));
    statusBar()->showMessage(u("Unused result cleanup completed."),
                             kStatusMessageDurationMs);
}

void MainWindow::refreshStartPage()
{
    if (!m_startPageWidget) return;
    m_startPageWidget->setRecentProjects(ApplicationSettingsStore().recentProjects());
    QStringList recoveryNames;
    QStringList recoveryPaths;
    if (m_recoveryManager) {
        for (const ProjectRecoveryEntry& entry : m_recoveryManager->entries()) {
            recoveryNames.append(QStringLiteral("%1 — %2 — %3")
                                     .arg(entry.projectName,
                                          entry.createdAt.toLocalTime().toString(
                                              QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                          entry.reason));
            recoveryPaths.append(entry.snapshotPath);
        }
    }
    m_startPageWidget->setRecoveryEntries(recoveryNames, recoveryPaths);
    const QString fdsPath = FdsRunner::configuredExecutable();
    const QString fdsError = FdsRunner::validateExecutable(fdsPath);
    const QString smokeviewPath = SmokeviewLauncher::configuredExecutable();
    const QString smokeviewError = SmokeviewLauncher::validateExecutable(smokeviewPath);
    m_startPageWidget->setRuntimeStatus(
        fdsError.isEmpty()
            ? QStringLiteral("ready (%1)").arg(QFileInfo(fdsPath).fileName())
            : fdsError,
        smokeviewError.isEmpty()
            ? QStringLiteral("ready (%1)").arg(QFileInfo(smokeviewPath).fileName())
            : smokeviewError);
}

void MainWindow::showStartPage()
{
    if (!m_centralStack || !m_startPageWidget) return;
    refreshStartPage();
    m_centralStack->setCurrentWidget(m_startPageWidget);
    statusBar()->showMessage(u("Start page opened."), kStatusMessageDurationMs);
}

void MainWindow::startGuidedTutorial(const QString& tutorialId, bool restart)
{
    if (!m_tutorialGuideWidget || !TutorialGuideWidget::findTutorial(tutorialId)) return;
    const bool resuming = !restart &&
        m_tutorialGuideWidget->activeTutorialId().compare(
            tutorialId, Qt::CaseInsensitive) == 0;
    if (!resuming) {
        if (!confirmProjectReplacement(u("starting a guided tutorial"))) return;
        createNewProject();
        m_tutorialGuideWidget->startTutorial(tutorialId, true);
    }
    refreshTutorialContext();
    showModelWorkspace(0);
    m_tutorialGuideDock->show();
    m_tutorialGuideDock->raise();
    CrashDiagnostics::recordOperation(
        QStringLiteral("Guided tutorial opened: %1").arg(tutorialId));
    statusBar()->showMessage(
        u("Guided tutorial started from an empty project."),
        kStatusMessageDurationMs);
}

void MainWindow::refreshTutorialContext()
{
    if (!m_tutorialGuideWidget) return;
    m_tutorialGuideWidget->setContext(
        m_project.get(), m_currentProjectPath, m_currentFdsPath,
        !m_projectResultDirectory.isEmpty());
}

void MainWindow::highlightTutorialAction(const QString& actionObjectName,
                                         const QString& menuPath)
{
    for (QToolButton* button : findChildren<QToolButton*>()) {
        if (!button->property("firecaeTutorialHighlight").toBool()) continue;
        button->setStyleSheet(QString{});
        button->setProperty("firecaeTutorialHighlight", false);
    }
    QAction* target = findChild<QAction*>(actionObjectName);
    if (target) {
        target->setProperty("firecaeTutorialNext", true);
        for (QToolButton* button : findChildren<QToolButton*>()) {
            if (button->defaultAction() != target) continue;
            button->setProperty("firecaeTutorialHighlight", true);
            button->setStyleSheet(QStringLiteral(
                "QToolButton { background:#fff3a6; border:2px solid #e6a700; }"));
        }
    }
    statusBar()->showMessage(
        u("Tutorial next step: %1").arg(menuPath), kStatusMessageDurationMs);
}

void MainWindow::loadSimpleTestBenchmark()
{
    if (!confirmProjectReplacement(u("opening the simple-test example"))) return;
    if (m_smokeviewHostWidget) {
        m_smokeviewHostWidget->closeViewer();
    }
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }

    m_project = FdsExamples::createSimpleTestProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Standard benchmark created from FireCAE objects: simple_test"));
    statusBar()->showMessage(u("Simple test benchmark created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadActivateVentsTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createActivateVentsProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "activate_vents (44 objects)"));
    statusBar()->showMessage(u("activate_vents tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadBucketTest2Tutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createBucketTest2Project();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "bucket_test_2 (13 objects)"));
    statusBar()->showMessage(u("bucket_test_2 tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadCouchTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createCouchProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "couch (26 objects)"));
    statusBar()->showMessage(u("couch tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadCouchSmoke12sTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createCouchSmoke12sProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "couch_smoke_12s (26 objects)"));
    statusBar()->showMessage(u("couch_smoke_12s tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadHvacAircoilTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createHvacAircoilProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "HVAC_aircoil (20 objects)"));
    statusBar()->showMessage(u("HVAC_aircoil tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadTunnelDemoTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createTunnelDemoProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "tunnel_demo (11 objects)"));
    statusBar()->showMessage(u("tunnel_demo tutorial created."),
                             kStatusMessageDurationMs);
}

void MainWindow::loadTunnelSmoke10sTutorial()
{
    if (!confirmTutorialReplacement()) return;
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = FdsExamples::createTunnelSmoke10sProject();
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Tutorial created from editable FireCAE objects: "
                       "tunnel_smoke_10s (11 objects)"));
    statusBar()->showMessage(u("tunnel_smoke_10s tutorial created."),
                             kStatusMessageDurationMs);
}

bool MainWindow::importFdsInputFile(const QString& filePath)
{
    FdsImporter importer;
    FdsImportResult imported = importer.importFile(filePath);
    if (!imported.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] FDS input import failed: %1")
                .arg(imported.errorMessage));
        statusBar()->showMessage(u("FDS input import failed."),
                                 kStatusMessageDurationMs);
        return false;
    }

    if (m_smokeviewHostWidget) m_smokeviewHostWidget->closeViewer();
    showModelWorkspace(0);
    if (m_occViewWidget && m_occViewWidget->displayManager()) {
        m_occViewWidget->displayManager()->clear();
    }
    m_project = std::move(imported.project);
    m_currentProjectPath.clear();
    m_currentFdsPath.clear();
    resetProjectRuntimeSettings();
    m_projectResultDirectory = QFileInfo(filePath).absolutePath();
    m_nextBoxNumber = 1;
    m_propertiesWidget->clear();
    m_modelTreeWidget->setProject(m_project.get());
    rebuildFdsScene(true);
    updateWindowTitle();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS input imported as %1 UUID business objects: %2")
            .arg(imported.objectCount)
            .arg(QDir::toNativeSeparators(filePath)));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS import report: %1 unsupported record(s), "
                       "%2 unsupported field(s), %3 resolved and %4 unresolved reference(s).")
            .arg(imported.unsupportedRecordCount)
            .arg(imported.unsupportedParameterCount)
            .arg(imported.resolvedReferenceCount)
            .arg(imported.unresolvedReferenceCount));
    CrashDiagnostics::recordOperation(QStringLiteral("FDS input imported"));
    for (const QString& warning : imported.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    statusBar()->showMessage(u("FDS input imported."), kStatusMessageDurationMs);
    return true;
}

void MainWindow::chooseAndImportFds()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        u("Import FDS Input"),
        QString(),
        QStringLiteral("FDS Input (*.fds);;All Files (*.*)"));
    if (filePath.isEmpty()) return;
    if (!confirmProjectReplacement(u("replacing it with an imported FDS case"))) return;
    if (!importFdsInputFile(filePath)) {
        QMessageBox::critical(this,
                              u("FDS Import Failed"),
                              u("See the Messages panel for details."));
    }
}

bool MainWindow::exportCurrentProjectToFds(const QString& filePath)
{
    if (!m_project || filePath.trimmed().isEmpty()) {
        return false;
    }
    const FdsWriteResult validation = FdsWriter::render(*m_project);
    for (const QString& warning : validation.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    if (!validation.success()) {
        for (const QString& validationError : validation.errors) {
            m_messageWidget->appendMessage(
                QStringLiteral("[Error] %1").arg(validationError));
        }
        statusBar()->showMessage(u("FDS export failed."), kStatusMessageDurationMs);
        return false;
    }
    QString error;
    if (!FdsWriter::writeFile(*m_project, filePath, &error)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] FDS export failed: %1").arg(error));
        statusBar()->showMessage(u("FDS export failed."), kStatusMessageDurationMs);
        return false;
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS input exported: %1")
            .arg(QDir::toNativeSeparators(filePath)));
    m_currentFdsPath = QFileInfo(filePath).absoluteFilePath();
    statusBar()->showMessage(u("FDS input exported."), kStatusMessageDurationMs);
    refreshTutorialContext();
    return true;
}

void MainWindow::chooseAndExportFds()
{
    if (!m_project) {
        return;
    }
    const QString suggestedName = m_project->chid() + QStringLiteral(".fds");
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        u("Export FDS Input"),
        suggestedName,
        QStringLiteral("FDS Input (*.fds);;All Files (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }
    if (!exportCurrentProjectToFds(filePath)) {
        QMessageBox::critical(this,
                              u("FDS Export Failed"),
                              u("See the Messages panel for details."));
    }
}

void MainWindow::editProjectSettings()
{
    if (!m_project) return;
    FdsProjectDialog dialog(m_project.get(), this);
    dialog.setObjectName(QStringLiteral("ProjectSettingsDialog"));
    if (dialog.exec() != QDialog::Accepted) return;
    const QString beforeName = m_project->name();
    const QString beforeChid = m_project->chid();
    const double beforeEndTime = m_project->endTime();
    const QString beforeFdsVersion = m_project->fdsVersion();
    const FcDisplayUnit beforeUnit = m_project->displayUnit();
    const QString afterName = dialog.projectName();
    const QString afterChid = dialog.chid();
    const double afterEndTime = dialog.endTime();
    const QString afterFdsVersion = dialog.fdsVersion();
    const FcDisplayUnit afterUnit = dialog.displayUnit();
    const auto applySettings = [this](const QString& name, const QString& chid,
                                      double endTime, const QString& fdsVersion,
                                      FcDisplayUnit unit) {
        m_project->setName(name);
        m_project->setChid(chid);
        m_project->setEndTime(endTime);
        m_project->setFdsVersion(fdsVersion);
        m_project->setDisplayUnit(unit);
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        m_propertiesWidget->showProject(m_project.get());
        updateSnapStatus();
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Edit Project Settings"),
        [applySettings, afterName, afterChid, afterEndTime, afterFdsVersion, afterUnit]() {
            applySettings(afterName, afterChid, afterEndTime, afterFdsVersion, afterUnit);
        },
        [applySettings, beforeName, beforeChid, beforeEndTime, beforeFdsVersion, beforeUnit]() {
            applySettings(beforeName, beforeChid, beforeEndTime, beforeFdsVersion, beforeUnit);
        }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Project settings updated: CHID=%1, T_END=%2, FDS Schema=%3")
            .arg(m_project->chid())
            .arg(m_project->endTime(), 0, 'g', 15)
            .arg(m_project->fdsVersion()));
    statusBar()->showMessage(u("Project settings updated."),
                             kStatusMessageDurationMs);
}

void MainWindow::editSimulationParameters()
{
    if (!m_project) return;
    SimulationParametersDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString beforeName = m_project->name();
    const QString beforeChid = m_project->chid();
    const double beforeEndTime = m_project->endTime();
    const FcSimulationParameters beforeParameters =
        m_project->simulationParameters();
    const QString afterName = dialog.projectName();
    const QString afterChid = dialog.chid();
    const double afterEndTime = dialog.endTime();
    FcSimulationParameters afterParameters = dialog.parameters();
    if (afterParameters.restartEnabled &&
        afterParameters.restartChid.trimmed().isEmpty()) {
        afterParameters.restartChid = afterChid;
    }
    const auto apply = [this](const QString& name, const QString& chid,
                              double endTime,
                              const FcSimulationParameters& parameters) {
        m_project->setName(name);
        m_project->setChid(chid);
        m_project->setEndTime(endTime);
        m_project->setSimulationParameters(parameters);
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        m_propertiesWidget->showProject(m_project.get());
        refreshRecordView();
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Edit Simulation Parameters"),
        [apply, afterName, afterChid, afterEndTime, afterParameters]() {
            apply(afterName, afterChid, afterEndTime, afterParameters);
        },
        [apply, beforeName, beforeChid, beforeEndTime, beforeParameters]() {
            apply(beforeName, beforeChid, beforeEndTime, beforeParameters);
        }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Simulation parameters updated: T_END=%1 s; "
                       "environment=%2; radiation=%3; restart=%4")
            .arg(afterEndTime, 0, 'g', 15)
            .arg(afterParameters.environmentConfigured ? QStringLiteral("on")
                                                        : QStringLiteral("default"))
            .arg(afterParameters.radiationConfigured
                     ? afterParameters.radiationEnabled
                           ? QStringLiteral("on") : QStringLiteral("off")
                     : QStringLiteral("default"))
            .arg(afterParameters.restartEnabled ? QStringLiteral("on")
                                                 : QStringLiteral("off")));
    statusBar()->showMessage(u("Simulation parameters updated."),
                             kStatusMessageDurationMs);
}

int MainWindow::nextFdsSequenceIndex() const
{
    if (!m_project || !m_project->document()) return 0;
    int maximum = -1;
    for (const auto& group : m_project->document()->groups()) {
        updateMaximumSequence(group, maximum);
    }
    return maximum + 1;
}

void MainWindow::createFdsObject(const QString& initialKeyword)
{
    if (!m_project || !m_project->document()) return;
    FdsObjectEditorDialog dialog(m_project.get(), initialKeyword, nullptr, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FdsObjectEditorData editorData = dialog.editorData();
    auto object = std::make_shared<FcFdsNamelist>(
        editorData.name,
        fdsObjectTypeForKeyword(editorData.keyword),
        editorData.keyword,
        editorData.fdsId,
        nextFdsSequenceIndex());
    object->setParameters(editorData.parameters);
    const auto group = m_project->document()->group(editorData.group);
    if (!group || isLockedForModification(group.get())) {
        QMessageBox::critical(this,
                              u("FDS Object Could Not Be Created"),
                              u("See the Messages panel for details."));
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Could not add FDS object: %1").arg(editorData.name));
        return;
    }
    const auto applyObject = [this, group, object](bool present) {
        if (present) {
            if (!object->parent()) group->addChild(object);
        } else if (object->parent()) {
            object->parent()->removeChild(object->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? object->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(object->id());
            m_propertiesWidget->showObject(object.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create FDS Object"),
        [applyObject]() { applyObject(true); },
        [applyObject]() { applyObject(false); }));
    m_deleteAction->setEnabled(true);
    m_editObjectAction->setEnabled(true);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS object created: &%1 %2 (UUID %3)")
            .arg(editorData.keyword, editorData.name, object->id()));
    statusBar()->showMessage(u("FDS object created."), kStatusMessageDurationMs);
}

void MainWindow::editSelectedFdsObject()
{
    if (!m_modelTreeWidget) return;
    editFdsObject(m_modelTreeWidget->selectedObjectId());
}

void MainWindow::editFdsObject(const QString& objectId)
{
    if (!m_project || !m_project->document() || objectId.isEmpty()) return;
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
    if (!namelist || isLockedForModification(namelist.get())) {
        if (namelist) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return;
    }

    FdsObjectEditorDialog dialog(m_project.get(), {}, namelist.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FdsObjectEditorData editorData = dialog.editorData();
    const QString beforeName = namelist->name();
    const QString beforeFdsId = namelist->fdsId();
    const std::vector<FcFdsParameter> beforeParameters = namelist->parameters();
    const auto applyData = [this, namelist](
                               const QString& name,
                               const QString& fdsId,
                               const std::vector<FcFdsParameter>& parameters) {
        namelist->setName(name);
        namelist->setFdsId(fdsId);
        namelist->setParameters(parameters);
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, namelist->id());
        m_modelTreeWidget->selectObjectById(namelist->id());
        m_propertiesWidget->showObject(namelist.get());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Edit FDS Object"),
        [applyData, editorData]() {
            applyData(editorData.name, editorData.fdsId, editorData.parameters);
        },
        [applyData, beforeName, beforeFdsId, beforeParameters]() {
            applyData(beforeName, beforeFdsId, beforeParameters);
        }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS object updated: &%1 %2 (UUID %3)")
            .arg(namelist->keyword(), namelist->name(), namelist->id()));
    statusBar()->showMessage(u("FDS object updated."), kStatusMessageDurationMs);
}

void MainWindow::validateCurrentProject()
{
    if (!m_project) return;
    refreshRecordView();
    if (m_inspectorDock) {
        m_inspectorDock->show();
        ensureFloatingFeedbackDockGeometry(m_inspectorDock);
        m_inspectorDock->raise();
    }
    if (m_inspectorTabs && m_inspectorTabs->count() > 2) {
        m_inspectorTabs->setCurrentIndex(2);
    }
    const FdsWriteResult validation = FdsWriter::render(*m_project);
    m_messagesDock->show();
    ensureFloatingFeedbackDockGeometry(m_messagesDock);
    m_messagesDock->raise();
    for (const QString& warning : validation.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    if (validation.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Info] Model validation passed: %1")
                .arg(m_project->chid()));
        statusBar()->showMessage(validation.warnings.isEmpty()
                                     ? u("Model validation passed.")
                                     : u("Model validation passed with warnings."),
                                 kStatusMessageDurationMs);
        if (validation.warnings.isEmpty()) {
            QMessageBox::information(
                this, u("Model Validation"),
                u("The model is valid and can be written as an FDS input file."));
        } else {
            QMessageBox::warning(
                this, u("Model Validation Warnings"),
                u("The model is valid, but the following items should be reviewed:") +
                    QStringLiteral("\n\n") +
                    validation.warnings.join(QLatin1Char('\n')));
        }
        return;
    }
    for (const QString& error : validation.errors) {
        m_messageWidget->appendMessage(QStringLiteral("[Error] %1").arg(error));
    }
    statusBar()->showMessage(u("Model validation failed."),
                             kStatusMessageDurationMs);
    QMessageBox::warning(this,
                         u("Model Validation Failed"),
                         validation.errors.join(QLatin1Char('\n')));
}

void MainWindow::createMeshesWithAssistant()
{
    if (!m_project || !m_project->document() || !m_modelTreeWidget) return;
    MeshEngineeringDialog dialog(m_project.get(),
                                 m_modelTreeWidget->selectedObjectIds(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<MeshEngineeringBlock> blocks = dialog.blocks();
    std::vector<std::shared_ptr<FcFdsNamelist>> meshes;
    meshes.reserve(blocks.size());
    const int sequence = nextFdsSequenceIndex();
    for (int index = 0; index < static_cast<int>(blocks.size()); ++index) {
        const MeshEngineeringBlock& block = blocks[static_cast<std::size_t>(index)];
        auto mesh = std::make_shared<FcFdsNamelist>(
            block.name, FcObjectType::Mesh, QStringLiteral("MESH"),
            block.fdsId, sequence + index);
        mesh->addRawParameter(
            QStringLiteral("IJK"),
            QStringLiteral("%1,%2,%3")
                .arg(block.cells[0]).arg(block.cells[1]).arg(block.cells[2]));
        QStringList bounds;
        bounds.reserve(6);
        for (double coordinate : block.bounds) {
            bounds.append(QString::number(coordinate, 'g', 15));
        }
        mesh->addRawParameter(QStringLiteral("XB"), bounds.join(QLatin1Char(',')));
        meshes.push_back(mesh);
    }

    const auto group = m_project->document()->meshesGroup();
    const auto apply = [this, group, meshes](bool present) {
        if (present) {
            for (const auto& mesh : meshes) {
                if (!mesh->parent()) group->addChild(mesh);
            }
        } else {
            for (auto iterator = meshes.rbegin(); iterator != meshes.rend(); ++iterator) {
                if ((*iterator)->parent()) (*iterator)->parent()->removeChild((*iterator)->id());
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        const QString selectedId = present && !meshes.empty()
                                       ? meshes.back()->id() : QString{};
        rebuildFdsScene(false, selectedId);
        if (!selectedId.isEmpty()) {
            m_modelTreeWidget->selectObjectById(selectedId);
            m_propertiesWidget->showObject(meshes.back().get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Engineered Meshes"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    qint64 totalCells = 0;
    for (const MeshEngineeringBlock& block : blocks) {
        totalCells += static_cast<qint64>(block.cells[0]) *
                      block.cells[1] * block.cells[2];
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Mesh engineering assistant created %1 aligned mesh(es), "
                       "%2 total cells.")
            .arg(static_cast<qlonglong>(blocks.size())).arg(totalCells));
    statusBar()->showMessage(u("Engineered meshes created."),
                             kStatusMessageDurationMs);
}

void MainWindow::createFireSourceWithWizard()
{
    if (!m_project || !m_project->document()) return;
    FireSourceWizardDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FireSourceWizardData data = dialog.data();
    const int sequence = nextFdsSequenceIndex();

    auto reaction = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Reaction"), FcObjectType::Reaction,
        QStringLiteral("REAC"), data.reactionFdsId, sequence);
    reaction->addStringParameter(QStringLiteral("FUEL"), data.fuel);
    reaction->addRawParameter(QStringLiteral("SOOT_YIELD"),
                              QString::number(data.sootYield, 'g', 15));
    reaction->addRawParameter(QStringLiteral("CO_YIELD"),
                              QString::number(data.coYield, 'g', 15));
    reaction->addRawParameter(QStringLiteral("RADIATIVE_FRACTION"),
                              QString::number(data.radiativeFraction, 'g', 15));

    std::vector<std::shared_ptr<FcFdsNamelist>> ramps;
    if (data.createRamp) {
        const std::array<std::pair<double, double>, 3> points = {{
            {data.startTime, 0.0}, {data.peakTime, 1.0}, {data.endTime, 0.0}}};
        for (int index = 0; index < static_cast<int>(points.size()); ++index) {
            auto ramp = std::make_shared<FcFdsNamelist>(
                QStringLiteral("%1 Ramp Point %2").arg(data.name).arg(index + 1),
                FcObjectType::Ramp, QStringLiteral("RAMP"), data.rampFdsId,
                sequence + 1 + index);
            ramp->addRawParameter(QStringLiteral("T"),
                                  QString::number(points[index].first, 'g', 15));
            ramp->addRawParameter(QStringLiteral("F"),
                                  QString::number(points[index].second, 'g', 15));
            ramps.push_back(ramp);
        }
    }

    auto surface = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Surface"), FcObjectType::Surface,
        QStringLiteral("SURF"), data.surfaceFdsId,
        sequence + 1 + static_cast<int>(ramps.size()));
    surface->addRawParameter(QStringLiteral("HRRPUA"),
                             QString::number(data.hrrpua, 'g', 15));
    surface->addStringParameter(QStringLiteral("COLOR"), data.color);
    if (!ramps.empty()) {
        surface->addReferenceParameter(QStringLiteral("RAMP_Q"),
                                       {ramps.front()->id()});
    }

    using Addition = std::pair<FcDocument::GroupPtr, std::shared_ptr<FcFdsNamelist>>;
    std::vector<Addition> additions;
    additions.push_back({m_project->document()->reactionsGroup(), reaction});
    for (const auto& ramp : ramps) {
        additions.push_back({m_project->document()->controlsGroup(), ramp});
    }
    additions.push_back({m_project->document()->surfacesGroup(), surface});

    if (data.hostObjectId.isEmpty()) {
        auto burner = std::make_shared<FcFdsNamelist>(
            data.name + QStringLiteral(" Burner"), FcObjectType::Vent,
            QStringLiteral("VENT"), data.surfaceFdsId + QStringLiteral("_BURNER"),
            sequence + 2 + static_cast<int>(ramps.size()));
        burner->addRawParameter(
            QStringLiteral("XB"),
            QStringLiteral("%1,%2,%3,%4,%5,%5")
                .arg(data.x, 0, 'g', 15).arg(data.x + data.width, 0, 'g', 15)
                .arg(data.y, 0, 'g', 15).arg(data.y + data.depth, 0, 'g', 15)
                .arg(data.z, 0, 'g', 15));
        burner->addReferenceParameter(QStringLiteral("SURF_ID"), {surface->id()});
        additions.push_back({m_project->document()->ventsGroup(), burner});
    }

    const FcObject::Ptr host = data.hostObjectId.isEmpty()
                                   ? FcObject::Ptr{}
                                   : m_project->document()->findObject(data.hostObjectId);
    const auto hostNamelist = std::dynamic_pointer_cast<FcFdsNamelist>(host);
    const auto hostGeometry = std::dynamic_pointer_cast<FcGeometryObject>(host);
    const std::vector<FcFdsParameter> hostBefore = hostNamelist
                                                       ? hostNamelist->parameters()
                                                       : std::vector<FcFdsParameter>{};
    std::vector<FcFdsParameter> hostAfter = hostBefore;
    if (hostNamelist) {
        bool replaced = false;
        for (FcFdsParameter& parameter : hostAfter) {
            if (parameter.key.trimmed().toUpper() != QStringLiteral("SURF_ID")) continue;
            parameter.kind = FcFdsParameterKind::ObjectReferences;
            parameter.value.clear();
            parameter.targetObjectIds = {surface->id()};
            replaced = true;
            break;
        }
        if (!replaced) {
            hostAfter.push_back({QStringLiteral("SURF_ID"),
                                 FcFdsParameterKind::ObjectReferences, {},
                                 {surface->id()}});
        }
    }
    const QString geometrySurfaceBefore = hostGeometry
                                              ? hostGeometry->defaultSurfaceId()
                                              : QString{};

    const auto apply = [this, additions, hostNamelist, hostGeometry,
                        hostBefore, hostAfter, geometrySurfaceBefore,
                        surface](bool present) {
        if (present) {
            for (const Addition& addition : additions) {
                if (!addition.second->parent()) addition.first->addChild(addition.second);
            }
            if (hostNamelist) hostNamelist->setParameters(hostAfter);
            if (hostGeometry) hostGeometry->setDefaultSurfaceId(surface->id());
        } else {
            if (hostNamelist) hostNamelist->setParameters(hostBefore);
            if (hostGeometry) hostGeometry->setDefaultSurfaceId(geometrySurfaceBefore);
            for (auto iterator = additions.rbegin(); iterator != additions.rend(); ++iterator) {
                if (iterator->second->parent()) {
                    iterator->second->parent()->removeChild(iterator->second->id());
                }
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? surface->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(surface->id());
            m_propertiesWidget->showObject(surface.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Fire Source"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Fire source created: %1; HRRPUA=%2 kW/m2; host UUID=%3")
            .arg(data.name).arg(data.hrrpua, 0, 'g', 15)
            .arg(data.hostObjectId.isEmpty() ? QStringLiteral("none")
                                             : data.hostObjectId));
    statusBar()->showMessage(u("Fire source created."), kStatusMessageDurationMs);
}

void MainWindow::createParticleSprayWithWizard()
{
    if (!m_project || !m_project->document()) return;
    ParticleSprayWizardDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const ParticleSprayWizardData data = dialog.data();
    const int sequence = nextFdsSequenceIndex();

    auto species = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Species"), FcObjectType::Species,
        QStringLiteral("SPEC"), data.speciesFdsId, sequence);
    if (!data.speciesFormula.isEmpty()) {
        species->addStringParameter(QStringLiteral("FORMULA"), data.speciesFormula);
    }

    auto particle = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Droplets"), FcObjectType::Particle,
        QStringLiteral("PART"), data.particleFdsId, sequence + 1);
    particle->addReferenceParameter(QStringLiteral("SPEC_ID"), {species->id()});
    particle->addStringParameter(QStringLiteral("QUANTITIES(1)"),
                                 QStringLiteral("PARTICLE DIAMETER"));
    particle->addRawParameter(QStringLiteral("DIAMETER"),
                              QString::number(data.particleDiameter, 'g', 15));
    if (data.particleAge > 0.0) {
        particle->addRawParameter(QStringLiteral("AGE"),
                                  QString::number(data.particleAge, 'g', 15));
    }

    std::vector<std::shared_ptr<FcFdsNamelist>> tables;
    tables.reserve(data.sprayPattern.size());
    for (int index = 0; index < static_cast<int>(data.sprayPattern.size()); ++index) {
        const SprayPatternRow& row = data.sprayPattern[static_cast<std::size_t>(index)];
        auto table = std::make_shared<FcFdsNamelist>(
            QStringLiteral("%1 Spray Sector %2").arg(data.name).arg(index + 1),
            FcObjectType::Table, QStringLiteral("TABL"), data.tableFdsId,
            sequence + 2 + index);
        table->addRawParameter(
            QStringLiteral("TABLE_DATA"),
            QStringLiteral("%1,%2,%3,%4,%5,%6")
                .arg(row.elevationMinimum, 0, 'g', 15)
                .arg(row.elevationMaximum, 0, 'g', 15)
                .arg(row.azimuthMinimum, 0, 'g', 15)
                .arg(row.azimuthMaximum, 0, 'g', 15)
                .arg(row.radius, 0, 'g', 15)
                .arg(row.weight, 0, 'g', 15));
        tables.push_back(table);
    }

    auto property = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Property"), FcObjectType::Property,
        QStringLiteral("PROP"), data.propertyFdsId,
        sequence + 2 + static_cast<int>(tables.size()));
    property->addStringParameter(QStringLiteral("QUANTITY"), data.propertyQuantity);
    property->addRawParameter(QStringLiteral("PARTICLE_VELOCITY"),
                              QString::number(data.particleVelocity, 'g', 15));
    property->addReferenceParameter(QStringLiteral("PART_ID"), {particle->id()});
    property->addRawParameter(QStringLiteral("FLOW_RATE"),
                              QString::number(data.flowRate, 'g', 15));
    if (!data.activateAtTime) {
        property->addRawParameter(
            QStringLiteral("ACTIVATION_TEMPERATURE"),
            QString::number(data.activationTemperature, 'g', 15));
        property->addRawParameter(QStringLiteral("RTI"),
                                  QString::number(data.responseTimeIndex, 'g', 15));
    }
    property->addReferenceParameter(QStringLiteral("SPRAY_PATTERN_TABLE"),
                                    {tables.front()->id()});
    if (!data.smokeviewId.isEmpty()) {
        property->addStringParameter(QStringLiteral("SMOKEVIEW_ID"), data.smokeviewId);
    }
    property->addRawParameter(QStringLiteral("PARTICLES_PER_SECOND"),
                              QString::number(data.particlesPerSecond));

    auto device = std::make_shared<FcFdsNamelist>(
        data.name + QStringLiteral(" Device"), FcObjectType::Device,
        QStringLiteral("DEVC"), data.deviceFdsId,
        sequence + 3 + static_cast<int>(tables.size()));
    device->addRawParameter(
        QStringLiteral("XYZ"),
        QStringLiteral("%1,%2,%3").arg(data.x, 0, 'g', 15)
            .arg(data.y, 0, 'g', 15).arg(data.z, 0, 'g', 15));
    device->addReferenceParameter(QStringLiteral("PROP_ID"), {property->id()});
    if (data.activateAtTime) {
        device->addStringParameter(QStringLiteral("QUANTITY"), QStringLiteral("TIME"));
        device->addRawParameter(QStringLiteral("SETPOINT"),
                                QString::number(data.activationTime, 'g', 15));
        device->addRawParameter(QStringLiteral("INITIAL_STATE"),
                                QStringLiteral(".TRUE."));
    }

    std::shared_ptr<FcFdsNamelist> existingDump;
    for (const FcObject::Ptr& child : m_project->document()->outputsGroup()->children()) {
        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(child);
        if (namelist && namelist->keyword() == QStringLiteral("DUMP")) {
            existingDump = namelist;
            break;
        }
    }
    std::vector<FcFdsParameter> dumpBefore;
    std::vector<FcFdsParameter> dumpAfter;
    std::shared_ptr<FcFdsNamelist> newDump;
    if (data.createParticleOutput) {
        if (existingDump) {
            dumpBefore = existingDump->parameters();
            dumpAfter = dumpBefore;
            dumpAfter.erase(std::remove_if(
                dumpAfter.begin(), dumpAfter.end(), [](const FcFdsParameter& parameter) {
                    return parameter.key.trimmed().toUpper() == QStringLiteral("DT_PART");
                }), dumpAfter.end());
            dumpAfter.push_back({QStringLiteral("DT_PART"), FcFdsParameterKind::Raw,
                                 QString::number(data.particleOutputInterval, 'g', 15), {}});
        } else {
            newDump = std::make_shared<FcFdsNamelist>(
                data.name + QStringLiteral(" Particle Output"), FcObjectType::Output,
                QStringLiteral("DUMP"), QString{},
                sequence + 4 + static_cast<int>(tables.size()));
            newDump->addRawParameter(
                QStringLiteral("DT_PART"),
                QString::number(data.particleOutputInterval, 'g', 15));
        }
    }

    using Addition = std::pair<FcDocument::GroupPtr, std::shared_ptr<FcFdsNamelist>>;
    std::vector<Addition> additions;
    additions.push_back({m_project->document()->speciesGroup(), species});
    additions.push_back({m_project->document()->particlesGroup(), particle});
    additions.push_back({m_project->document()->particlesGroup(), property});
    for (const auto& table : tables) {
        additions.push_back({m_project->document()->controlsGroup(), table});
    }
    additions.push_back({m_project->document()->devicesGroup(), device});
    if (newDump) additions.push_back({m_project->document()->outputsGroup(), newDump});

    const auto apply = [this, additions, existingDump, dumpBefore, dumpAfter,
                        device](bool present) {
        if (present) {
            for (const Addition& addition : additions) {
                if (!addition.second->parent()) addition.first->addChild(addition.second);
            }
            if (existingDump && !dumpAfter.empty()) existingDump->setParameters(dumpAfter);
        } else {
            if (existingDump && !dumpAfter.empty()) existingDump->setParameters(dumpBefore);
            for (auto iterator = additions.rbegin(); iterator != additions.rend(); ++iterator) {
                if (iterator->second->parent()) {
                    iterator->second->parent()->removeChild(iterator->second->id());
                }
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? device->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(device->id());
            m_propertiesWidget->showObject(device.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Particle and Sprinkler System"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Particle/sprinkler system created: %1; SPEC=%2; PART=%3; PROP=%4; TABL rows=%5; DEVC=%6")
            .arg(data.name, data.speciesFdsId, data.particleFdsId,
                 data.propertyFdsId)
            .arg(static_cast<int>(tables.size()))
            .arg(data.deviceFdsId));
    statusBar()->showMessage(u("Particle and sprinkler system created."),
                             kStatusMessageDurationMs);
}

void MainWindow::createDeviceControlWithWizard()
{
    if (!m_project || !m_project->document()) return;
    DeviceControlWizardDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const DeviceControlWizardData wizardData = dialog.data();
    const int sequence = nextFdsSequenceIndex();
    auto device = std::make_shared<FcFdsNamelist>(
        wizardData.name, FcObjectType::Device, QStringLiteral("DEVC"),
        wizardData.deviceFdsId, sequence);
    device->addStringParameter(QStringLiteral("QUANTITY"), wizardData.quantity);
    device->addRawParameter(
        QStringLiteral("XYZ"),
        QStringLiteral("%1,%2,%3")
            .arg(wizardData.x, 0, 'g', 15)
            .arg(wizardData.y, 0, 'g', 15)
            .arg(wizardData.z, 0, 'g', 15));
    device->addRawParameter(QStringLiteral("SETPOINT"),
                            QString::number(wizardData.setpoint, 'g', 15));
    device->addRawParameter(QStringLiteral("TRIP_DIRECTION"),
                            QString::number(wizardData.tripDirection));
    if (!wizardData.propertyObjectId.isEmpty()) {
        device->addReferenceParameter(QStringLiteral("PROP_ID"),
                                      {wizardData.propertyObjectId});
    }

    std::shared_ptr<FcFdsNamelist> control;
    if (wizardData.createControl) {
        control = std::make_shared<FcFdsNamelist>(
            wizardData.name + QStringLiteral(" Control"), FcObjectType::Control,
            QStringLiteral("CTRL"), wizardData.controlFdsId, sequence + 1);
        control->addStringParameter(
            QStringLiteral("FUNCTION_TYPE"),
            wizardData.delay > 0.0 ? QStringLiteral("TIME_DELAY")
                                    : QStringLiteral("ANY"));
        control->addReferenceParameter(QStringLiteral("INPUT_ID"), {device->id()});
        if (wizardData.delay > 0.0) {
            control->addRawParameter(QStringLiteral("DELAY"),
                                     QString::number(wizardData.delay, 'g', 15));
        }
    }

    const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
        m_project->document()->findObject(wizardData.controlledObjectId));
    const std::vector<FcFdsParameter> targetBefore = target
        ? target->parameters() : std::vector<FcFdsParameter>{};
    std::vector<FcFdsParameter> targetAfter;
    if (target) {
        // FDS takes an OBST/VENT initial state from its DEVC or CTRL.
        // With control logic, keep the detector false until it trips and
        // put the requested target state on the controller instead.
        const auto& stateOwner = control ? control : device;
        stateOwner->addRawParameter(
            QStringLiteral("INITIAL_STATE"),
            wizardData.activateTarget ? QStringLiteral(".FALSE.")
                                      : QStringLiteral(".TRUE."));
        for (const FcFdsParameter& parameter : targetBefore) {
            const QString key = parameter.key.section(QLatin1Char('('), 0, 0)
                                    .trimmed().toUpper();
            if (key == QStringLiteral("CTRL_ID") ||
                key == QStringLiteral("DEVC_ID") ||
                key == QStringLiteral("INITIAL_STATE")) continue;
            targetAfter.push_back(parameter);
        }
        targetAfter.push_back({control ? QStringLiteral("CTRL_ID")
                                       : QStringLiteral("DEVC_ID"),
                               FcFdsParameterKind::ObjectReferences, {},
                               {control ? control->id() : device->id()}});
    }

    const auto deviceGroup = m_project->document()->devicesGroup();
    const auto controlGroup = m_project->document()->controlsGroup();
    const auto apply = [this, deviceGroup, controlGroup, device, control, target,
                        targetBefore, targetAfter](bool present) {
        if (present) {
            if (!device->parent()) deviceGroup->addChild(device);
            if (control && !control->parent()) controlGroup->addChild(control);
            if (target) target->setParameters(targetAfter);
        } else {
            if (target) target->setParameters(targetBefore);
            if (control && control->parent()) {
                control->parent()->removeChild(control->id());
            }
            if (device->parent()) device->parent()->removeChild(device->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? device->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(device->id());
            m_propertiesWidget->showObject(device.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Device and Control"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Device created: %1; quantity=%2; target UUID=%3")
            .arg(wizardData.deviceFdsId, wizardData.quantity,
                 wizardData.controlledObjectId.isEmpty()
                     ? QStringLiteral("none") : wizardData.controlledObjectId));
    statusBar()->showMessage(u("Device and control created."),
                             kStatusMessageDurationMs);
}

void MainWindow::createOutputWithWizard()
{
    if (!m_project || !m_project->document()) return;
    OutputWizardDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const OutputWizardData data = dialog.data();
    QString keyword = data.keyword;
    if (keyword == QStringLiteral("SLCF_VECTOR")) keyword = QStringLiteral("SLCF");
    if (keyword == QStringLiteral("DUMP_PART")) keyword = QStringLiteral("DUMP");
    if (keyword == QStringLiteral("HVAC_DEVC")) keyword = QStringLiteral("DEVC");
    auto output = std::make_shared<FcFdsNamelist>(
        data.name,
        keyword == QStringLiteral("DEVC") ? FcObjectType::Device
                                           : FcObjectType::Output,
        keyword, data.fdsId,
        nextFdsSequenceIndex());
    if (keyword != QStringLiteral("DUMP")) {
        output->addStringParameter(QStringLiteral("QUANTITY"), data.quantity);
    }
    if (keyword == QStringLiteral("SLCF")) {
        output->addRawParameter(QStringLiteral("PB") + data.axis,
                                QString::number(data.coordinate, 'g', 15));
    }
    if (keyword == QStringLiteral("DEVC") || keyword == QStringLiteral("PROF")) {
        output->addRawParameter(
            QStringLiteral("XYZ"),
            QStringLiteral("%1,%2,%3")
                .arg(data.x, 0, 'g', 15).arg(data.y, 0, 'g', 15)
                .arg(data.z, 0, 'g', 15));
    }
    if (keyword == QStringLiteral("DUMP")) {
        output->addRawParameter(QStringLiteral("DT_PART"),
                                QString::number(data.interval, 'g', 15));
    }
    if (data.keyword == QStringLiteral("HVAC_DEVC")) {
        const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
            m_project->document()->findObject(data.targetObjectId));
        if (target) {
            const QString type = target->parameterValue(
                QStringLiteral("TYPE_ID")).trimmed().toUpper();
            output->addReferenceParameter(
                type == QStringLiteral("NODE") ? QStringLiteral("NODE_ID")
                                                : QStringLiteral("DUCT_ID"),
                {target->id()});
        }
    }
    if (data.vector) output->addRawParameter(QStringLiteral("VECTOR"), QStringLiteral(".TRUE."));
    const auto group = keyword == QStringLiteral("DEVC")
                           ? m_project->document()->devicesGroup()
                           : m_project->document()->outputsGroup();
    const auto apply = [this, group, output](bool present) {
        if (present) {
            if (!output->parent()) group->addChild(output);
        } else if (output->parent()) {
            output->parent()->removeChild(output->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? output->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(output->id());
            m_propertiesWidget->showObject(output.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Output"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Output created: &%1 QUANTITY='%2' (UUID %3)")
            .arg(keyword, data.quantity, output->id()));
    statusBar()->showMessage(u("Output created."), kStatusMessageDurationMs);
}

void MainWindow::showControlLogicGraph()
{
    if (!m_project) return;
    FdsNetworkGraphDialog dialog(m_project.get(), FdsNetworkGraphKind::Controls, this);
    dialog.exec();
}

void MainWindow::showHvacNetworkGraph()
{
    if (!m_project) return;
    FdsNetworkGraphDialog dialog(m_project.get(), FdsNetworkGraphKind::Hvac, this);
    dialog.exec();
}

void MainWindow::openPropertyLibrary()
{
    if (!m_project || !m_project->document()) return;
    FdsPropertyLibraryDialog dialog({}, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FdsLibraryEntry entry = dialog.selectedEntry();
    if (entry.libraryId.isEmpty()) return;
    const FcObjectType type = fdsObjectTypeForKeyword(entry.keyword);
    auto object = std::make_shared<FcFdsNamelist>(
        entry.name, type, entry.keyword, entry.fdsId, nextFdsSequenceIndex());
    object->setParameters(entry.parameters);
    FcDocument::GroupPtr group;
    switch (type) {
    case FcObjectType::Mesh:
    case FcObjectType::MeshMultiplier: group = m_project->document()->meshesGroup(); break;
    case FcObjectType::Species: group = m_project->document()->speciesGroup(); break;
    case FcObjectType::Material: group = m_project->document()->materialsGroup(); break;
    case FcObjectType::Surface: group = m_project->document()->surfacesGroup(); break;
    case FcObjectType::Reaction: group = m_project->document()->reactionsGroup(); break;
    case FcObjectType::Obstruction: group = m_project->document()->geometryGroup(); break;
    case FcObjectType::Vent: group = m_project->document()->ventsGroup(); break;
    case FcObjectType::Particle:
    case FcObjectType::Property: group = m_project->document()->particlesGroup(); break;
    case FcObjectType::Table:
    case FcObjectType::Ramp:
    case FcObjectType::Control: group = m_project->document()->controlsGroup(); break;
    case FcObjectType::Device: group = m_project->document()->devicesGroup(); break;
    case FcObjectType::HVAC: group = m_project->document()->hvacGroup(); break;
    case FcObjectType::InitialCondition: group = m_project->document()->initialConditionsGroup(); break;
    case FcObjectType::Output: group = m_project->document()->outputsGroup(); break;
    default: group = m_project->document()->configurationGroup(); break;
    }
    const auto apply = [this, group, object](bool present) {
        if (present) {
            if (!object->parent()) group->addChild(object);
        } else if (object->parent()) {
            object->parent()->removeChild(object->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? object->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(object->id());
            m_propertiesWidget->showObject(object.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Copy Library Object to Project"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Library object copied to project: %1 (&%2, UUID %3)")
            .arg(entry.name, entry.keyword, object->id()));
    statusBar()->showMessage(u("Library object copied to project."),
                             kStatusMessageDurationMs);
}

void MainWindow::manageScenarios()
{
    if (!m_project) return;
    ScenarioManagerDialog dialog(m_project.get(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_currentFdsPath.clear();
    m_modelTreeWidget->refresh();
    rebuildFdsScene(true);
    updateWindowTitle();
    const FcScenario* scenario = m_project->activeScenario();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Active scenario: %1; CHID=%2; disabled=%3; overrides=%4")
            .arg(scenario ? scenario->name : QStringLiteral("Default"),
                 scenario ? scenario->chid : m_project->chid())
            .arg(scenario ? scenario->disabledObjectIds.size() : 0)
            .arg(scenario ? scenario->parameterOverrides.size() : 0));
    statusBar()->showMessage(u("Active scenario changed."),
                             kStatusMessageDurationMs);
}

void MainWindow::runAllScenarios()
{
    if (!m_project || !m_taskManager || m_project->scenarios().isEmpty()) return;
    QString root = qEnvironmentVariable("FIRECAE_AUTOMATION_BATCH_ROOT").trimmed();
    if (root.isEmpty()) {
        root = QFileDialog::getExistingDirectory(
            this, u("Choose Scenario Batch Output Directory"),
            m_projectResultDirectory.isEmpty()
                ? QFileInfo(m_currentProjectPath).absolutePath()
                : m_projectResultDirectory);
    }
    if (root.isEmpty()) return;
    bool accepted = false;
    int concurrency = qEnvironmentVariableIntValue(
        "FIRECAE_AUTOMATION_BATCH_CONCURRENCY", &accepted);
    if (!accepted) {
        concurrency = QInputDialog::getInt(
            this, u("Scenario Batch Concurrency"),
            u("Maximum simultaneous tasks:"), 1, 1, 32, 1, &accepted);
    } else {
        concurrency = qBound(1, concurrency, 32);
    }
    if (!accepted) return;
    m_taskManager->setMaximumConcurrentTasks(concurrency);
    const QString originalScenarioId = m_project->activeScenarioId();
    int queued = 0;
    QStringList errors;
    const QVector<FcScenario> scenariosToRun = m_project->scenarios();
    for (const FcScenario& scenarioValue : scenariosToRun) {
        if (!m_project->setActiveScenario(scenarioValue.id)) continue;
        const FcScenario* scenario = m_project->activeScenario();
        if (!scenario) continue;
        QString directoryPath = scenario->outputDirectory.trimmed();
        if (directoryPath.isEmpty()) directoryPath = QDir(root).filePath(scenario->name);
        else if (QDir::isRelativePath(directoryPath)) directoryPath = QDir(root).filePath(directoryPath);
        const QString resolvedOutputRoot = QDir(directoryPath).absolutePath();
        // Different scenarios must not overwrite each other's exported inputs,
        // even when the user selects the same output root and CHID.
        directoryPath = QDir(directoryPath).filePath(scenario->id);
        if (!QDir().mkpath(directoryPath)) {
            errors.append(QStringLiteral("%1: output directory could not be created.")
                              .arg(scenario->name));
            continue;
        }
        const QString inputPath = QDir(directoryPath).filePath(
            (scenario->chid.isEmpty() ? m_project->chid() : scenario->chid) +
            QStringLiteral(".fds"));
        QString writeError;
        if (!FdsWriter::writeFile(*m_project, inputPath, &writeError)) {
            errors.append(QStringLiteral("%1: %2").arg(scenario->name, writeError));
            continue;
        }
        FdsRunRequest request;
        request.inputFilePath = inputPath;
        request.mode = scenario->solverBackendId == QStringLiteral("fds.mpi.cpu")
                           ? FdsRunMode::Mpi
                           : scenario->solverBackendId ==
                                     QStringLiteral("fds.openmp.cpu")
                                 ? FdsRunMode::OpenMp
                                 : FdsRunMode::Serial;
        request.processCount = request.mode == FdsRunMode::Mpi
                                   ? qMax(2, scenario->processCount) : 1;
        request.threadCount = request.mode == FdsRunMode::OpenMp
                                  ? qMax(1, scenario->processCount) : 1;
        QString queueError;
        if (m_taskManager->enqueue(request, m_project->name(), scenario->name,
                                   m_project->endTime(), &queueError).isEmpty()) {
            errors.append(QStringLiteral("%1: %2").arg(scenario->name, queueError));
        } else {
            ++queued;
            FcScenario updatedScenario = *scenario;
            updatedScenario.outputDirectory = resolvedOutputRoot;
            m_project->updateScenario(updatedScenario);
        }
    }
    m_project->setActiveScenario(originalScenarioId);
    m_project->setModified(true);
    m_modelTreeWidget->refresh();
    rebuildFdsScene(true);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Scenario batch queued: %1 task(s), concurrency=%2, root=%3")
            .arg(queued).arg(concurrency).arg(QDir::toNativeSeparators(root)));
    for (const QString& error : errors) {
        m_messageWidget->appendMessage(QStringLiteral("[Error] %1").arg(error));
    }
    if (!errors.isEmpty()) {
        m_messagesDock->show();
        m_messagesDock->raise();
    }
    statusBar()->showMessage(u("Scenario batch queued."),
                             kStatusMessageDurationMs);
}

void MainWindow::updateMultiSelection(const QStringList& objectIds,
                                      bool synchronizeTree,
                                      bool synchronizeViewer)
{
    if (!m_project || !m_project->document()) return;
    QStringList validIds;
    QStringList names;
    QStringList unlockedGeometryIds;
    bool hasUnlockedGeometry = false;
    bool allLocked = !objectIds.isEmpty();
    bool anyLocked = false;
    bool allRemovable = !objectIds.isEmpty();
    bool allGeometry = !objectIds.isEmpty();
    for (const QString& id : objectIds) {
        const FcObject::Ptr object = m_project->document()->findObject(id);
        if (!object) continue;
        validIds.append(id);
        names.append(object->name());
        allLocked = allLocked && object->isLocked();
        anyLocked = anyLocked || isLockedForModification(object.get());
        const bool removable = object->type() == FcObjectType::IfcModel ||
            object->type() == FcObjectType::ResultCase ||
            object->type() == FcObjectType::Geometry ||
            object->type() == FcObjectType::Folder ||
            object->type() == FcObjectType::Floor ||
            static_cast<bool>(std::dynamic_pointer_cast<FcFdsNamelist>(object));
        allRemovable = allRemovable && removable;
        const bool geometry = static_cast<bool>(
            std::dynamic_pointer_cast<FcGeometryObject>(object));
        allGeometry = allGeometry && geometry;
        if (!isLockedForModification(object.get()) && geometry) {
            hasUnlockedGeometry = true;
            unlockedGeometryIds.append(id);
        }
    }
    if (synchronizeTree) m_modelTreeWidget->selectObjectsByIds(validIds);
    if (synchronizeViewer) {
        if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
            manager->selectObjects(validIds);
        }
        if (m_planViewWidget && m_planViewWidget->displayManager()) {
            m_planViewWidget->displayManager()->selectObjects(validIds);
        }
    }
    const bool hasSelection = !validIds.isEmpty();
    m_transformAction->setEnabled(hasUnlockedGeometry);
    m_transformGizmoAction->setEnabled(hasUnlockedGeometry);
    m_copyMoveAction->setEnabled(hasUnlockedGeometry);
    m_arrayAction->setEnabled(hasUnlockedGeometry);
    m_measureAction->setEnabled(allGeometry && hasSelection);
    m_mirrorAction->setEnabled(hasUnlockedGeometry);
    m_alignAction->setEnabled(validIds.size() > 1 && allGeometry && !anyLocked);
    m_copyToFloorAction->setEnabled(hasUnlockedGeometry);
    m_groupAction->setEnabled(validIds.size() > 1 && !anyLocked && allGeometry);
    m_hideSelectedAction->setEnabled(hasSelection);
    m_lockSelectedAction->setEnabled(hasSelection);
    m_batchRenameAction->setEnabled(hasSelection && !anyLocked);
    m_assignSurfacesAction->setEnabled(hasUnlockedGeometry);
    m_lockSelectedAction->setText(allLocked ? u("Unlock Selected") : u("Lock Selected"));
    m_deleteAction->setEnabled(
        hasSelection && allRemovable && !anyLocked &&
        (validIds.size() == 1 || allGeometry));
    bool singleEditable = false;
    if (validIds.size() == 1) {
        const FcObject::Ptr selected = m_project->document()->findObject(validIds.constFirst());
        singleEditable = static_cast<bool>(std::dynamic_pointer_cast<FcFdsNamelist>(selected)) ||
                         static_cast<bool>(std::dynamic_pointer_cast<FcGeometryObject>(selected)) ||
                         static_cast<bool>(std::dynamic_pointer_cast<FcFloorObject>(selected)) ||
                         static_cast<bool>(std::dynamic_pointer_cast<FcIfcObject>(selected));
    }
    m_editObjectAction->setEnabled(singleEditable);
    bool directSupported = false;
    if (validIds.size() == 1 && !anyLocked) {
        const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(validIds.constFirst()));
        directSupported = geometry && !GeometryEditService::handles(
            BuildingGeometryService::requestFromParameters(geometry->geometryKind(), geometry->geometryParameters())).isEmpty();
    }
    m_directEditAction->setEnabled(directSupported);
    const QString editingId = m_occViewWidget->directEditingObjectId();
    if (!editingId.isEmpty() && (!directSupported || validIds.size()!=1 || validIds.constFirst()!=editingId))
        m_occViewWidget->endDirectEditing();
    if (m_transformGizmoAction->isChecked()) {
        if (!m_occViewWidget->showTransformManipulator(unlockedGeometryIds)) {
            m_transformGizmoAction->setChecked(false);
        }
    }
    if (validIds.size() == 1) {
        m_propertiesWidget->showObject(
            m_project->document()->findObject(validIds.constFirst()).get());
    } else if (validIds.size() > 1) {
        m_propertiesWidget->clear();
    }
    if (m_selectionList) {
        m_selectionList->clear();
        if (validIds.isEmpty()) {
            auto* emptyItem = new QListWidgetItem(u("No objects selected."),
                                                  m_selectionList);
            emptyItem->setFlags(Qt::NoItemFlags);
        } else {
            for (int index = 0; index < validIds.size(); ++index) {
                const QString id = validIds.at(index);
                const QString name = index < names.size() ? names.at(index) : id;
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1  [%2]").arg(name, id.left(8)), m_selectionList);
                item->setData(Qt::UserRole, id);
                item->setToolTip(QStringLiteral("UUID: %1").arg(id));
            }
        }
        if (m_inspectorTabs && m_inspectorTabs->count() > 1) {
            m_inspectorTabs->setTabText(
                1, QStringLiteral("%1 (%2)").arg(u("Selection Set"))
                       .arg(validIds.size()));
        }
    }
    const QString summary = names.size() <= 3
                                ? names.join(QStringLiteral(", "))
                                : names.mid(0, 3).join(QStringLiteral(", ")) +
                                      QStringLiteral(", ...");
    statusBar()->showMessage(
        QStringLiteral("%1 object(s) selected: %2").arg(validIds.size()).arg(summary));
}

void MainWindow::selectAllModelObjects()
{
    if (!m_project || !m_project->document()) return;
    QStringList ids;
    GeometryDisplayManager* manager = m_occViewWidget->displayManager();
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& object, bool parentVisible) {
            if (!object) return;
            const bool visible = parentVisible && object->isVisible();
            if (visible && manager && manager->contains(object->id())) ids.append(object->id());
            for (const FcObject::Ptr& child : object->children()) collect(child, visible);
        };
    for (const auto& group : m_project->document()->groups()) collect(group, true);
    updateMultiSelection(ids, true, true);
}

void MainWindow::invertModelSelection()
{
    if (!m_project || !m_project->document()) return;
    const QStringList selectedList = m_modelTreeWidget->selectedObjectIds();
    const QSet<QString> selected(selectedList.cbegin(), selectedList.cend());
    QStringList ids;
    GeometryDisplayManager* manager = m_occViewWidget->displayManager();
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& object, bool parentVisible) {
            if (!object) return;
            const bool visible = parentVisible && object->isVisible();
            if (visible && manager && manager->contains(object->id()) &&
                !selected.contains(object->id())) ids.append(object->id());
            for (const FcObject::Ptr& child : object->children()) collect(child, visible);
        };
    for (const auto& group : m_project->document()->groups()) collect(group, true);
    updateMultiSelection(ids, true, true);
}

void MainWindow::selectObjectsByType()
{
    if (!m_project || !m_project->document()) return;
    const QStringList labels = {u("Geometry"), QStringLiteral("IFC"),
                                QStringLiteral("MESH"), QStringLiteral("OBST"),
                                QStringLiteral("VENT"), QStringLiteral("DEVC"),
                                QStringLiteral("HVAC"), u("Output")};
    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, u("Select by Type"), u("Object Type:"), labels, 0, false, &accepted);
    if (!accepted) return;
    const int index = labels.indexOf(selected);
    const auto matches = [index](const FcObject::Ptr& object) {
        if (!object) return false;
        switch (index) {
        case 0: return object->type() == FcObjectType::Geometry;
        case 1: return object->type() == FcObjectType::IfcModel ||
                       object->type() == FcObjectType::IfcEntity;
        case 2: return object->type() == FcObjectType::Mesh;
        case 3: return object->type() == FcObjectType::Obstruction;
        case 4: return object->type() == FcObjectType::Vent;
        case 5: return object->type() == FcObjectType::Device;
        case 6: return object->type() == FcObjectType::HVAC;
        case 7: return object->type() == FcObjectType::Output;
        default: return false;
        }
    };
    QStringList ids;
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& object, bool parentVisible) {
            if (!object) return;
            const bool visible = parentVisible && object->isVisible();
            if (visible && matches(object)) ids.append(object->id());
            for (const auto& child : object->children()) collect(child, visible);
        };
    for (const auto& group : m_project->document()->groups()) collect(group, true);
    updateMultiSelection(ids, true, true);
}

void MainWindow::selectObjectsByProperty()
{
    if (!m_project || !m_project->document()) return;
    bool accepted = false;
    const QString query = QInputDialog::getText(
        this, u("Select by Property"),
        u("Name, UUID, tag, floor, or FDS parameter contains:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || query.isEmpty()) return;
    QStringList ids;
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& object, bool parentVisible) {
            if (!object) return;
            const bool visible = parentVisible && object->isVisible();
            bool match = object->name().contains(query, Qt::CaseInsensitive) ||
                         object->id().contains(query, Qt::CaseInsensitive) ||
                         object->floorName().contains(query, Qt::CaseInsensitive) ||
                         object->tags().join(QStringLiteral(" ")).contains(
                             query, Qt::CaseInsensitive);
            if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
                match = match || namelist->keyword().contains(query, Qt::CaseInsensitive) ||
                        namelist->fdsId().contains(query, Qt::CaseInsensitive);
                for (const FcFdsParameter& parameter : namelist->parameters()) {
                    match = match || parameter.key.contains(query, Qt::CaseInsensitive) ||
                            parameter.value.contains(query, Qt::CaseInsensitive);
                }
            }
            if (visible && match) ids.append(object->id());
            for (const auto& child : object->children()) collect(child, visible);
        };
    for (const auto& group : m_project->document()->groups()) collect(group, true);
    updateMultiSelection(ids, true, true);
}

void MainWindow::selectObjectsByFloor()
{
    if (!m_project || !m_project->document()) return;
    bool accepted = false;
    const QString query = QInputDialog::getText(
        this, u("Select by Floor/Group"), u("Floor or group name contains:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || query.isEmpty()) return;
    QStringList ids;
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& object, bool ancestorMatches) {
            if (!object) return;
            const bool matches = ancestorMatches ||
                object->floorName().contains(query, Qt::CaseInsensitive) ||
                object->name().contains(query, Qt::CaseInsensitive) &&
                    (object->type() == FcObjectType::Folder ||
                     object->type() == FcObjectType::Floor ||
                     object->type() == FcObjectType::Group);
            if (object->isVisible() && matches &&
                object->type() != FcObjectType::Group &&
                object->type() != FcObjectType::Folder &&
                object->type() != FcObjectType::Floor) {
                ids.append(object->id());
            }
            for (const auto& child : object->children()) collect(child, matches);
        };
    for (const auto& group : m_project->document()->groups()) collect(group, false);
    updateMultiSelection(ids, true, true);
}

void MainWindow::configureSnapping()
{
    if (!m_snapManager) return;
    SnapSettings settings = m_snapManager->settings();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("SnapSettingsDialog"));
    dialog.setWindowTitle(u("Snap Settings"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    const auto createMode = [&dialog](const QString& text, bool checked) {
        auto* checkBox = new QCheckBox(text, &dialog);
        checkBox->setChecked(checked);
        return checkBox;
    };
    auto* worldGrid = createMode(u("World coordinate grid"), settings.worldGrid);
    auto* fdsGrid = createMode(u("FDS grid nodes"), settings.fdsGrid);
    auto* vertex = createMode(u("Vertices"), settings.vertex);
    auto* midpoint = createMode(u("Edge midpoints"), settings.edgeMidpoint);
    auto* intersection = createMode(u("Intersections"), settings.intersection);
    auto* edge = createMode(u("Edges"), settings.edge);
    auto* face = createMode(u("Faces"), settings.face);
    auto* center = createMode(u("Object centers"), settings.objectCenter);
    auto* orthogonal = createMode(u("Orthogonal constraint"), settings.orthogonal);
    auto* angle = createMode(u("Angle increment"), settings.angle);
    const QString unit = m_project
        ? QStringLiteral(" %1").arg(m_project->displayUnitSymbol())
        : QStringLiteral(" m");
    auto* worldStep = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9,
        m_project ? m_project->metersToDisplay(settings.worldGridStep)
                  : settings.worldGridStep,
        unit);
    auto* fdsStep = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9,
        m_project ? m_project->metersToDisplay(settings.fdsGridStep)
                  : settings.fdsGridStep,
        unit);
    auto* angleStep = createEngineeringSpinBox(
        &dialog, 0.001, 360.0, settings.angleStepDegrees, QStringLiteral("°"));
    form->addRow(worldGrid, worldStep);
    form->addRow(fdsGrid, fdsStep);
    form->addRow(vertex);
    form->addRow(midpoint);
    form->addRow(intersection);
    form->addRow(edge);
    form->addRow(face);
    form->addRow(center);
    form->addRow(orthogonal);
    form->addRow(angle, angleStep);
    layout->addLayout(form);
    auto* hint = new QLabel(
        u("Press F9 to toggle snapping temporarily; exact transforms report the snapped coordinate in the status bar."),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    settings.worldGrid = worldGrid->isChecked();
    settings.fdsGrid = fdsGrid->isChecked();
    settings.vertex = vertex->isChecked();
    settings.edgeMidpoint = midpoint->isChecked();
    settings.intersection = intersection->isChecked();
    settings.edge = edge->isChecked();
    settings.face = face->isChecked();
    settings.objectCenter = center->isChecked();
    settings.orthogonal = orthogonal->isChecked();
    settings.angle = angle->isChecked();
    settings.worldGridStep = m_project
        ? m_project->displayToMeters(worldStep->value()) : worldStep->value();
    settings.fdsGridStep = m_project
        ? m_project->displayToMeters(fdsStep->value()) : fdsStep->value();
    settings.angleStepDegrees = angleStep->value();
    m_snapManager->setSettings(settings);
    updateSnapStatus();
}

void MainWindow::updateSnapStatus(const QString& detail)
{
    if (!m_snapStatusLabel || !m_snapManager) return;
    const SnapSettings& settings = m_snapManager->settings();
    if (!settings.enabled || m_snapManager->isTemporarilyDisabled()) {
        m_snapStatusLabel->setText(u("Snap: Off"));
        m_snapStatusLabel->setToolTip(u("Snap: Off"));
        return;
    }
    QStringList modes;
    if (settings.worldGrid) modes.append(u("Grid"));
    if (settings.fdsGrid) modes.append(u("FDS"));
    if (settings.vertex) modes.append(u("Vertex"));
    if (settings.edgeMidpoint) modes.append(u("Midpoint"));
    if (settings.intersection) modes.append(u("Intersection"));
    if (settings.edge) modes.append(u("Edge"));
    if (settings.face) modes.append(u("Face"));
    if (settings.objectCenter) modes.append(u("Center"));
    if (settings.orthogonal) modes.append(u("Ortho"));
    if (settings.angle) modes.append(u("Angle"));
    QString text = QStringLiteral("%1: %2").arg(u("Snap"), modes.join(QStringLiteral("/")));
    if (!detail.isEmpty()) text += QStringLiteral(" | ") + detail;
    m_snapStatusLabel->setText(text);
    m_snapStatusLabel->setToolTip(text);
}

void MainWindow::commitManipulatorTransform(const QStringList& objectIds)
{
    if (!m_project || !m_project->document() || objectIds.isEmpty()) return;
    GeometryDisplayManager* displayManager = m_occViewWidget->displayManager();
    if (!displayManager) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    QVector<TopoDS_Shape> before;
    QStringList transformedIds;
    gp_Trsf rawTransform;
    bool hasRawTransform = false;
    for (const QString& objectId : objectIds) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(objectId));
        if (!object || isLockedForModification(object.get())) continue;
        const gp_Trsf transformation =
            displayManager->objectLocalTransformation(objectId);
        if (transformation.Form() == gp_Identity) continue;
        objects.append(object);
        before.append(object->shape());
        transformedIds.append(objectId);
        if (!hasRawTransform) {
            rawTransform = transformation;
            hasRawTransform = true;
        }
    }
    m_occViewWidget->hideTransformManipulator();
    if (objects.isEmpty()) return;

    // AIS_Manipulator applies one local transform to the attached sequence and
    // uses the first presentation as its leading object.  Reconstruct that
    // transform around the same pivot so engineering snap settings constrain
    // the committed business geometry instead of being a settings-only UI.
    const gp_Pnt pivot = shapeCenter(before.constFirst());
    const gp_Pnt rawPivot = pivot.Transformed(rawTransform);
    gp_Vec committedTranslation(pivot, rawPivot);
    SnapTarget snapTarget = SnapTarget::None;
    gp_Pnt snappedPivot = rawPivot;

    if (m_snapManager && committedTranslation.SquareMagnitude() > 1.0e-18) {
        committedTranslation =
            m_snapManager->constrainTranslation(committedTranslation);
        const gp_Pnt constrainedPivot = pivot.Translated(committedTranslation);

        QVector<TopoDS_Shape> snapCandidates;
        const QSet<QString> movingIds(transformedIds.cbegin(), transformedIds.cend());
        const std::function<void(const FcObject::Ptr&, bool)> collectCandidates =
            [&](const FcObject::Ptr& candidate, bool parentVisible) {
                if (!candidate) return;
                const bool visible = parentVisible && candidate->isVisible();
                if (visible && !movingIds.contains(candidate->id())) {
                    if (const auto geometry =
                            std::dynamic_pointer_cast<FcGeometryObject>(candidate)) {
                        if (!geometry->shape().IsNull()) {
                            snapCandidates.append(geometry->shape());
                        }
                    }
                }
                for (const FcObject::Ptr& child : candidate->children()) {
                    collectCandidates(child, visible);
                }
            };
        for (const auto& group : m_project->document()->groups()) {
            collectCandidates(group, true);
        }

        const SnapSettings& settings = m_snapManager->settings();
        double tolerance = 0.15;
        if (settings.worldGrid || settings.fdsGrid) {
            const double activeStep = settings.fdsGrid
                ? settings.fdsGridStep : settings.worldGridStep;
            tolerance = std::max(1.0e-6, activeStep * 1.5);
        }
        const SnapResult snap = m_snapManager->snapPoint(
            constrainedPivot, snapCandidates, tolerance);
        if (snap.snapped) {
            snappedPivot = snap.point;
            snapTarget = snap.target;
            committedTranslation = gp_Vec(pivot, snappedPivot);
        } else {
            snappedPivot = constrainedPivot;
            if (!committedTranslation.IsEqual(
                    gp_Vec(pivot, rawPivot), 1.0e-9, 1.0e-9)) {
                snapTarget = SnapTarget::Orthogonal;
            }
        }
    }

    gp_XYZ rotationAxis;
    Standard_Real rotationRadians = 0.0;
    const bool hasRotation = rawTransform.GetRotation(rotationAxis, rotationRadians);
    double committedAngleDegrees = rotationRadians * 180.0 /
                                   3.14159265358979323846;
    if (hasRotation && m_snapManager) {
        const double snappedAngle = m_snapManager->snapAngle(committedAngleDegrees);
        if (std::abs(snappedAngle - committedAngleDegrees) > 1.0e-9) {
            snapTarget = SnapTarget::Angle;
        }
        committedAngleDegrees = snappedAngle;
        rotationRadians = committedAngleDegrees * 3.14159265358979323846 / 180.0;
    }

    QVector<TopoDS_Shape> after;
    after.reserve(before.size());
    const double scale = rawTransform.ScaleFactor();
    gp_Trsf committedTransform;
    for (const TopoDS_Shape& source : before) {
        TopoDS_Shape result = source;
        gp_Trsf combined;
        const auto apply = [&result, &combined](const gp_Trsf& transform) {
            result = BRepBuilderAPI_Transform(result, transform, Standard_True).Shape();
            combined.PreMultiply(transform);
        };
        if (std::abs(scale - 1.0) > 1.0e-12) {
            gp_Trsf scaleTransform;
            scaleTransform.SetScale(pivot, scale);
            apply(scaleTransform);
        }
        if (hasRotation && std::abs(rotationRadians) > 1.0e-12 &&
            rotationAxis.SquareModulus() > 1.0e-18) {
            gp_Trsf rotationTransform;
            rotationTransform.SetRotation(
                gp_Ax1(pivot, gp_Dir(rotationAxis)), rotationRadians);
            apply(rotationTransform);
        }
        if (committedTranslation.SquareMagnitude() > 1.0e-18) {
            gp_Trsf translationTransform;
            translationTransform.SetTranslation(committedTranslation);
            apply(translationTransform);
        }
        after.append(result);
        committedTransform=combined;
    }
    bool hasParametric=false;
    for(const auto& object:objects) hasParametric=hasParametric || !GeometryEditService::handles(
        BuildingGeometryService::requestFromParameters(object->geometryKind(),object->geometryParameters())).isEmpty();
    if(hasParametric) {
        for(const auto& object:objects) displayManager->displayObject(object);
        commitParametricTransforms(transformedIds,QVector<gp_Trsf>(transformedIds.size(),committedTransform));
        return;
    }
    const auto applyShapes = [this, objects, transformedIds](
                                 const QVector<TopoDS_Shape>& shapes) {
        GeometryDisplayManager* manager = m_occViewWidget->displayManager();
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setShape(shapes[index]);
            if (manager) manager->displayObject(objects[index]);
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        updateMultiSelection(transformedIds, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Transform with Gizmo"),
        [applyShapes, after]() { applyShapes(after); },
        [applyShapes, before]() { applyShapes(before); }));

    const QString unit = m_project->displayUnitSymbol();
    const QString snapName = UiLanguageManager::text(
        SnapManager::targetName(snapTarget));
    updateSnapStatus(
        QStringLiteral("%1 @ (%2, %3, %4) %5; %6=%7°")
            .arg(snapName)
            .arg(m_project->metersToDisplay(snappedPivot.X()), 0, 'f', 3)
            .arg(m_project->metersToDisplay(snappedPivot.Y()), 0, 'f', 3)
            .arg(m_project->metersToDisplay(snappedPivot.Z()), 0, 'f', 3)
            .arg(unit)
            .arg(u("Angle"))
            .arg(committedAngleDegrees, 0, 'f', 2));
    statusBar()->showMessage(
        QStringLiteral("%1 %2").arg(u("Gizmo transform committed:"))
            .arg(transformedIds.size()),
        kStatusMessageDurationMs);
}

void MainWindow::transformSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) objects.append(object);
    }
    if (objects.isEmpty()) return;
    GeometryTransformParameters parameters;
    const SnapSettings snapSettings = m_snapManager->settings();
    parameters.snapTranslation = snapSettings.enabled &&
        (snapSettings.worldGrid || snapSettings.fdsGrid);
    parameters.snapStep = snapSettings.fdsGrid
        ? snapSettings.fdsGridStep : snapSettings.worldGridStep;
    parameters.snapAngle = snapSettings.enabled && snapSettings.angle;
    parameters.angleStep = snapSettings.angleStepDegrees;
    if (!requestGeometryTransform(this, *m_project, u("Transform Selected Geometry"),
                                  parameters)) return;
    QVector<TopoDS_Shape> before;
    QVector<TopoDS_Shape> after;
    QVector<gp_Trsf> transforms;
    QStringList ids;
    for (const auto& object : objects) {
        before.append(object->shape());
        gp_Trsf transform;
        after.append(applyTransform(object->shape(), parameters, &transform));
        transforms.append(transform);
        ids.append(object->id());
    }
    bool hasParametric=false;
    for(const auto& object:objects) hasParametric=hasParametric || !GeometryEditService::handles(
        BuildingGeometryService::requestFromParameters(object->geometryKind(),object->geometryParameters())).isEmpty();
    if(hasParametric) {commitParametricTransforms(ids,transforms);return;}
    const auto applyShapes = [this, objects, ids](const QVector<TopoDS_Shape>& shapes) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setShape(shapes[index]);
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
                manager->displayObject(objects[index]);
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        updateMultiSelection(ids, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Transform Geometry"),
        [applyShapes, after]() { applyShapes(after); },
        [applyShapes, before]() { applyShapes(before); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Transformed %1 geometry object(s); snap step=%2 m, angle=%3 deg.")
            .arg(objects.size()).arg(parameters.snapStep).arg(parameters.angleStep));
    updateSnapStatus(QStringLiteral("Δ=(%1, %2, %3) m; R=(%4°, %5°, %6°)")
                         .arg(parameters.dx).arg(parameters.dy).arg(parameters.dz)
                         .arg(parameters.rx).arg(parameters.ry).arg(parameters.rz));
}

void MainWindow::copyMoveSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }
    QVector<std::shared_ptr<FcGeometryObject>> sources;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) sources.append(object);
    }
    if (sources.isEmpty()) return;
    GeometryTransformParameters parameters;
    parameters.dx = 1.0;
    const SnapSettings snapSettings = m_snapManager->settings();
    parameters.snapTranslation = snapSettings.enabled &&
        (snapSettings.worldGrid || snapSettings.fdsGrid);
    parameters.snapStep = snapSettings.fdsGrid
        ? snapSettings.fdsGridStep : snapSettings.worldGridStep;
    parameters.snapAngle = snapSettings.enabled && snapSettings.angle;
    parameters.angleStep = snapSettings.angleStepDegrees;
    if (!requestGeometryTransform(this, *m_project, u("Copy and Move"), parameters)) return;
    QVector<std::shared_ptr<FcGeometryObject>> copies;
    QVector<FcObject*> copyParents;
    QStringList ids;
    for (const auto& source : sources) {
        gp_Trsf transform;
        auto copy = std::make_shared<FcGeometryObject>(
            source->name() + QStringLiteral(" Copy"),
            applyTransform(source->shape(), parameters, &transform));
        copyGeometrySemantics(*source, *copy);
        QString error;
        if (!updateCopiedGeometryParameters(*m_project->document(), *source, *copy, transform, &error)) {
            QMessageBox::warning(this, u("Invalid Geometry"), UiLanguageManager::text(error)); return;
        }
        copies.append(copy);
        copyParents.append(source->parent());
        ids.append(copy->id());
    }
    const auto applyCopies = [this, copies, copyParents, ids](bool present) {
        for (qsizetype index = 0; index < copies.size(); ++index) {
            const auto& copy = copies[index];
            if (present) {
                FcObject* parent = copyParents[index];
                if (!copy->parent() && parent) parent->addChild(copy);
            } else {
                if (copy->parent()) copy->parent()->removeChild(copy->id());
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        updateMultiSelection(present ? ids : QStringList{}, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Copy and Move"),
        [applyCopies]() { applyCopies(true); },
        [applyCopies]() { applyCopies(false); }));
    updateSnapStatus(QStringLiteral("Copy Δ=(%1, %2, %3) m")
                         .arg(parameters.dx).arg(parameters.dy).arg(parameters.dz));
}

void MainWindow::arraySelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }
    QVector<std::shared_ptr<FcGeometryObject>> sources;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) sources.append(object);
    }
    if (sources.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ArrayCopyDialog"));
    dialog.setWindowTitle(u("Array Copy"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto createCount = [&dialog](int value) {
        auto* box = new QSpinBox(&dialog);
        box->setRange(1, 100); box->setValue(value); return box;
    };
    auto* nx = createCount(2); auto* ny = createCount(1); auto* nz = createCount(1);
    const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
    auto* sx = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9,
                                         m_project->metersToDisplay(1.5), unit);
    auto* sy = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* sz = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    form->addRow(u("Count X:"), nx); form->addRow(u("Count Y:"), ny);
    form->addRow(u("Count Z:"), nz); form->addRow(u("Step X:"), sx);
    form->addRow(u("Step Y:"), sy); form->addRow(u("Step Z:"), sz);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    QVector<std::shared_ptr<FcGeometryObject>> copies;
    QVector<FcObject*> copyParents;
    QStringList ids;
    for (int ix = 0; ix < nx->value(); ++ix) {
        for (int iy = 0; iy < ny->value(); ++iy) {
            for (int iz = 0; iz < nz->value(); ++iz) {
                if (ix == 0 && iy == 0 && iz == 0) continue;
                GeometryTransformParameters parameters;
                parameters.snapTranslation = false;
                parameters.snapAngle = false;
                parameters.dx = m_project->displayToMeters(sx->value()) * ix;
                parameters.dy = m_project->displayToMeters(sy->value()) * iy;
                parameters.dz = m_project->displayToMeters(sz->value()) * iz;
                for (const auto& source : sources) {
                    gp_Trsf transform;
                    auto copy = std::make_shared<FcGeometryObject>(
                        QStringLiteral("%1 [%2,%3,%4]").arg(source->name()).arg(ix).arg(iy).arg(iz),
                        applyTransform(source->shape(), parameters, &transform));
                    copyGeometrySemantics(*source, *copy);
                    QString error;
                    if (!updateCopiedGeometryParameters(*m_project->document(), *source, *copy, transform, &error)) {
                        QMessageBox::warning(this, u("Invalid Geometry"), UiLanguageManager::text(error)); return;
                    }
                    copies.append(copy);
                    copyParents.append(source->parent());
                    ids.append(copy->id());
                }
            }
        }
    }
    if (copies.isEmpty()) return;
    const auto applyCopies = [this, copies, copyParents, ids](bool present) {
        for (qsizetype index = 0; index < copies.size(); ++index) {
            const auto& copy = copies[index];
            if (present) {
                FcObject* parent = copyParents[index];
                if (!copy->parent() && parent) parent->addChild(copy);
            } else {
                if (copy->parent()) copy->parent()->removeChild(copy->id());
            }
        }
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        updateMultiSelection(present ? ids : QStringList{}, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Array Copy"), [applyCopies]() { applyCopies(true); },
        [applyCopies]() { applyCopies(false); }));
}

void MainWindow::measureSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && object->hasShape()) objects.append(object);
    }
    if (objects.isEmpty()) return;
    const QString unit = m_project->displayUnitSymbol();
    QString message;
    if (objects.size() == 1) {
        Bnd_Box bounds;
        BRepBndLib::Add(objects.constFirst()->shape(), bounds);
        double xMin = 0.0, yMin = 0.0, zMin = 0.0;
        double xMax = 0.0, yMax = 0.0, zMax = 0.0;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        message = QStringLiteral("%1\nX: %2 %5\nY: %3 %5\nZ: %4 %5")
                      .arg(objects.constFirst()->name())
                      .arg(m_project->metersToDisplay(xMax - xMin), 0, 'f', 4)
                      .arg(m_project->metersToDisplay(yMax - yMin), 0, 'f', 4)
                      .arg(m_project->metersToDisplay(zMax - zMin), 0, 'f', 4)
                      .arg(unit);
    } else {
        const gp_Pnt first = shapeCenter(objects.constFirst()->shape());
        const gp_Pnt last = shapeCenter(objects.constLast()->shape());
        const double dx = last.X() - first.X();
        const double dy = last.Y() - first.Y();
        const double dz = last.Z() - first.Z();
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        message = QStringLiteral("%1 → %2\nΔX: %3 %7\nΔY: %4 %7\nΔZ: %5 %7\n%6: %8 %7")
                      .arg(objects.constFirst()->name(), objects.constLast()->name())
                      .arg(m_project->metersToDisplay(dx), 0, 'f', 4)
                      .arg(m_project->metersToDisplay(dy), 0, 'f', 4)
                      .arg(m_project->metersToDisplay(dz), 0, 'f', 4)
                      .arg(u("Distance"))
                      .arg(unit)
                      .arg(m_project->metersToDisplay(distance), 0, 'f', 4);
    }
    updateSnapStatus(message.simplified());
    QMessageBox::information(this, u("Measure Selection"), message);
}

void MainWindow::mirrorSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) objects.append(object);
    }
    if (objects.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("MirrorGeometryDialog"));
    dialog.setWindowTitle(u("Mirror Selected"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* plane = new QComboBox(&dialog);
    plane->setObjectName(QStringLiteral("MirrorPlaneCombo"));
    plane->addItem(QStringLiteral("YZ (X)"), 0);
    plane->addItem(QStringLiteral("XZ (Y)"), 1);
    plane->addItem(QStringLiteral("XY (Z)"), 2);
    auto* coordinate = createEngineeringSpinBox(
        &dialog, -1.0e9, 1.0e9, 0.0,
        QStringLiteral(" %1").arg(m_project->displayUnitSymbol()));
    coordinate->setObjectName(QStringLiteral("MirrorCoordinateSpin"));
    form->addRow(u("Mirror Plane:"), plane);
    form->addRow(u("Plane Coordinate:"), coordinate);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const int axis = plane->currentData().toInt();
    const double value = m_project->displayToMeters(coordinate->value());
    const gp_Pnt location(axis == 0 ? value : 0.0,
                          axis == 1 ? value : 0.0,
                          axis == 2 ? value : 0.0);
    const gp_Dir normal(axis == 0 ? 1.0 : 0.0,
                        axis == 1 ? 1.0 : 0.0,
                        axis == 2 ? 1.0 : 0.0);
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(location, normal));
    QVector<TopoDS_Shape> before;
    QVector<TopoDS_Shape> after;
    QStringList ids;
    for (const auto& object : objects) {
        before.append(object->shape());
        after.append(BRepBuilderAPI_Transform(
                         object->shape(), mirror, Standard_True).Shape());
        ids.append(object->id());
    }
    const auto apply = [this, objects, ids](const QVector<TopoDS_Shape>& shapes) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setShape(shapes[index]);
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        rebuildFdsScene(false);
        m_modelTreeWidget->refresh();
        updateMultiSelection(ids, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Mirror Geometry"), [apply, after]() { apply(after); },
        [apply, before]() { apply(before); }));
}

void MainWindow::alignSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) objects.append(object);
    }
    if (objects.size() < 2) return;
    bool accepted = false;
    const QStringList axes = {u("Center X"), u("Center Y"), u("Center Z")};
    const QString selectedAxis = QInputDialog::getItem(
        this, u("Align Selected"), u("Alignment:"), axes, 0, false, &accepted);
    if (!accepted) return;
    const int axis = axes.indexOf(selectedAxis);
    const gp_Pnt anchor = shapeCenter(objects.constFirst()->shape());
    const double target = axis == 0 ? anchor.X() : axis == 1 ? anchor.Y() : anchor.Z();
    QVector<TopoDS_Shape> before;
    QVector<TopoDS_Shape> after;
    QStringList ids;
    for (const auto& object : objects) {
        const gp_Pnt center = shapeCenter(object->shape());
        gp_Vec delta(0.0, 0.0, 0.0);
        if (axis == 0) delta.SetX(target - center.X());
        else if (axis == 1) delta.SetY(target - center.Y());
        else delta.SetZ(target - center.Z());
        gp_Trsf transform;
        transform.SetTranslation(delta);
        before.append(object->shape());
        after.append(BRepBuilderAPI_Transform(
                         object->shape(), transform, Standard_True).Shape());
        ids.append(object->id());
    }
    const auto apply = [this, objects, ids](const QVector<TopoDS_Shape>& shapes) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setShape(shapes[index]);
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        rebuildFdsScene(false);
        m_modelTreeWidget->refresh();
        updateMultiSelection(ids, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Align Geometry"), [apply, after]() { apply(after); },
        [apply, before]() { apply(before); }));
}

void MainWindow::copySelectedGeometryToFloor()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> sources;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto source = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (source && !isLockedForModification(source.get())) sources.append(source);
    }
    if (sources.isEmpty()) return;
    QVector<std::shared_ptr<FcFloorObject>> floors;
    const std::function<void(const FcObject::Ptr&)> collectFloors =
        [&](const FcObject::Ptr& object) {
            if (!object) return;
            if (const auto floor = std::dynamic_pointer_cast<FcFloorObject>(object)) {
                floors.append(floor);
            }
            for (const FcObject::Ptr& child : object->children()) collectFloors(child);
        };
    collectFloors(m_project->document()->geometryGroup());
    if (floors.isEmpty()) {
        QMessageBox::information(this, u("Copy to Floor"),
                                 u("Create a floor before copying geometry."));
        return;
    }
    QStringList labels;
    for (const auto& floor : floors) {
        labels.append(QStringLiteral("%1  [%2]").arg(floor->name(), floor->id().left(8)));
    }
    bool accepted = false;
    const QString label = QInputDialog::getItem(
        this, u("Copy to Floor"), u("Target Floor:"), labels, 0, false, &accepted);
    if (!accepted) return;
    const int targetIndex = labels.indexOf(label);
    if (targetIndex < 0) return;
    const auto targetFloor = floors[targetIndex];
    QVector<std::shared_ptr<FcGeometryObject>> copies;
    QStringList ids;
    for (const auto& source : sources) {
        double sourceElevation = 0.0;
        for (FcObject* parent = source->parent(); parent; parent = parent->parent()) {
            const auto sourceFloor = std::dynamic_pointer_cast<FcFloorObject>(
                m_project->document()->findObject(parent->id()));
            if (sourceFloor) {
                sourceElevation = sourceFloor->baseElevation();
                break;
            }
        }
        const double dz = targetFloor->baseElevation() - sourceElevation;
        gp_Trsf transform;
        transform.SetTranslation(gp_Vec(0.0, 0.0, dz));
        auto copy = std::make_shared<FcGeometryObject>(
            QStringLiteral("%1 @ %2").arg(source->name(), targetFloor->name()),
            BRepBuilderAPI_Transform(source->shape(), transform, Standard_True).Shape());
        copyGeometrySemantics(*source, *copy);
        copy->setFloorName(targetFloor->name());
        QString error;
        if (!updateCopiedGeometryParameters(*m_project->document(), *source, *copy, transform, &error)) {
            QMessageBox::warning(this, u("Invalid Geometry"), UiLanguageManager::text(error)); return;
        }
        copies.append(copy);
        ids.append(copy->id());
    }
    const auto apply = [this, targetFloor, copies, ids](bool present) {
        for (const auto& copy : copies) {
            if (present) {
                if (!copy->parent()) targetFloor->addChild(copy);
            } else if (copy->parent()) {
                copy->parent()->removeChild(copy->id());
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        rebuildFdsScene(false);
        m_modelTreeWidget->refresh();
        updateMultiSelection(present ? ids : QStringList{}, true, true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Copy to Floor"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
}

void MainWindow::groupSelectedObjects()
{
    if (!m_project || !m_project->document()) return;
    QVector<FcObject::Ptr> objects;
    QVector<FcObject*> parents;
    QVector<std::size_t> indices;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const FcObject::Ptr object = m_project->document()->findObject(id);
        if (!object || !object->parent() || isLockedForModification(object.get()) ||
            !std::dynamic_pointer_cast<FcGeometryObject>(object)) continue;
        FcObject* parent = object->parent();
        const auto& children = parent->children();
        const auto position = std::find_if(children.cbegin(), children.cend(),
            [&id](const FcObject::Ptr& child) { return child && child->id() == id; });
        if (position == children.cend()) continue;
        objects.append(object); parents.append(parent);
        indices.append(static_cast<std::size_t>(std::distance(children.cbegin(), position)));
    }
    if (objects.size() < 2) return;
    const QString name = QInputDialog::getText(
        this, u("Group Selected"), u("Group Name:"), QLineEdit::Normal,
        QStringLiteral("Group_%1").arg(m_nextGroupNumber, 3, 10, QLatin1Char('0')));
    if (name.trimmed().isEmpty()) return;
    ++m_nextGroupNumber;
    auto group = std::make_shared<FcObject>(name.trimmed(), FcObjectType::Folder);
    const auto applyGroup = [this, objects, parents, indices, group](bool grouped) {
        if (grouped) {
            for (const auto& object : objects) {
                if (object->parent()) object->parent()->removeChild(object->id());
            }
            if (!group->parent()) m_project->document()->geometryGroup()->addChild(group);
            for (const auto& object : objects) group->addChild(object);
        } else {
            for (const auto& object : objects) {
                if (object->parent()) object->parent()->removeChild(object->id());
            }
            if (group->parent()) group->parent()->removeChild(group->id());
            for (qsizetype index = 0; index < objects.size(); ++index) {
                parents[index]->insertChild(objects[index], indices[index]);
            }
        }
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        m_modelTreeWidget->selectObjectById(grouped ? group->id() : objects.constFirst()->id());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Group Selected"), [applyGroup]() { applyGroup(true); },
        [applyGroup]() { applyGroup(false); }));
}

void MainWindow::createGeometryContainer(bool floor)
{
    if (!m_project || !m_project->document()) return;
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }
    const QString defaultName = floor
        ? QStringLiteral("Floor_%1").arg(static_cast<int>(
                                             m_project->document()->geometryGroup()->children().size()) + 1,
                                         2, 10, QLatin1Char('0'))
        : QStringLiteral("Folder_%1").arg(m_nextGroupNumber, 3, 10, QLatin1Char('0'));
    FcObject::Ptr container;
    if (floor) {
        auto floorObject = std::make_shared<FcFloorObject>(defaultName);
        double suggestedElevation = 0.0;
        for (const FcObject::Ptr& child :
             m_project->document()->geometryGroup()->children()) {
            const auto existing = std::dynamic_pointer_cast<FcFloorObject>(child);
            if (!existing) continue;
            suggestedElevation = std::max(
                suggestedElevation,
                existing->baseElevation() + existing->defaultStoreyHeight());
        }
        floorObject->setBaseElevation(suggestedElevation);
        FloorEditorDialog dialog(m_project.get(), this);
        dialog.setFloor(*floorObject);
        if (dialog.exec() != QDialog::Accepted) return;
        applyFloorEditorData(*floorObject, dialog.data());
        synchronizeFloorBackground(*floorObject);
        container = floorObject;
    } else {
        const QString name = QInputDialog::getText(
            this, u("New Geometry Folder"), u("Name:"),
            QLineEdit::Normal, defaultName).trimmed();
        if (name.isEmpty()) return;
        ++m_nextGroupNumber;
        container = std::make_shared<FcObject>(name, FcObjectType::Folder);
    }
    const auto applyContainer = [this, container](bool present) {
        if (present) {
            if (!container->parent()) {
                m_project->document()->geometryGroup()->addChild(container);
            }
        } else if (container->parent()) {
            container->parent()->removeChild(container->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        if (present) m_modelTreeWidget->selectObjectById(container->id());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        floor ? u("Create Floor") : u("Create Geometry Folder"),
        [applyContainer]() { applyContainer(true); },
        [applyContainer]() { applyContainer(false); }));
}

void MainWindow::editFloorObject(const QString& objectId)
{
    if (!m_project || !m_project->document()) return;
    const auto floor = std::dynamic_pointer_cast<FcFloorObject>(
        m_project->document()->findObject(objectId));
    if (!floor || isLockedForModification(floor.get())) return;
    FloorEditorDialog dialog(m_project.get(), this);
    dialog.setFloor(*floor);
    if (dialog.exec() != QDialog::Accepted) return;
    const FloorEditorData before = floorEditorData(*floor);
    const FloorEditorData after = dialog.data();
    const auto apply = [this, floor](const FloorEditorData& values) {
        const QString previousName = floor->name();
        applyFloorEditorData(*floor, values);
        synchronizeFloorBackground(*floor);
        const std::function<void(const FcObject::Ptr&)> updateChildren =
            [&](const FcObject::Ptr& child) {
                if (!child) return;
                if (child->floorName().isEmpty() ||
                    child->floorName() == previousName) {
                    child->setFloorName(values.name);
                }
                for (const FcObject::Ptr& descendant : child->children()) {
                    updateChildren(descendant);
                }
            };
        for (const FcObject::Ptr& child : floor->children()) updateChildren(child);
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        m_modelTreeWidget->selectObjectById(floor->id(), true);
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Edit Floor"), [apply, after]() { apply(after); },
        [apply, before]() { apply(before); }));
}

void MainWindow::hideSelectedObjects()
{
    if (!m_project || !m_project->document()) return;
    if (m_isolationActive) restoreModelVisibility();
    QVector<FcObject::Ptr> objects;
    QVector<bool> before;
    QSet<QString> visited;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const FcObject::Ptr object = m_project->document()->findObject(id);
        QVector<FcObject::Ptr> subtree;
        if (object) collectObjectSubtree(object, subtree);
        for (const auto& child : subtree) {
            if (visited.contains(child->id())) continue;
            visited.insert(child->id()); objects.append(child); before.append(child->isVisible());
        }
    }
    if (objects.isEmpty()) return;
    const auto applyVisibility = [this, objects](const QVector<bool>& values) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setVisible(values[index]);
        }
        for (const auto& group : m_project->document()->groups()) {
            restoreDisplayVisibilityRecursive(group, m_occViewWidget->displayManager());
            restoreDisplayVisibilityRecursive(group, m_planViewWidget->displayManager());
        }
        m_project->setModified(true); m_modelTreeWidget->refresh(); updateWindowTitle();
    };
    QVector<bool> hidden(objects.size(), false);
    m_undoStack->push(new FunctionalUndoCommand(
        u("Hide Selected"), [applyVisibility, hidden]() { applyVisibility(hidden); },
        [applyVisibility, before]() { applyVisibility(before); }));
}

void MainWindow::toggleSelectedObjectsLocked()
{
    if (!m_project || !m_project->document()) return;
    QVector<FcObject::Ptr> objects;
    QVector<bool> before;
    bool allLocked = true;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const FcObject::Ptr object = m_project->document()->findObject(id);
        if (object) {
            objects.append(object); before.append(object->isLocked());
            allLocked = allLocked && object->isLocked();
        }
    }
    if (objects.isEmpty()) return;
    QVector<bool> after(objects.size(), !allLocked);
    const auto applyLock = [this, objects](const QVector<bool>& values) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setLocked(values[index]);
        }
        m_project->setModified(true); m_modelTreeWidget->refresh(); updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        allLocked ? u("Unlock Selected") : u("Lock Selected"),
        [applyLock, after]() { applyLock(after); },
        [applyLock, before]() { applyLock(before); }));
}

void MainWindow::batchRenameSelectedObjects()
{
    if (!m_project || !m_project->document()) return;
    QVector<FcObject::Ptr> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const FcObject::Ptr object = m_project->document()->findObject(id);
        if (object && !isLockedForModification(object.get())) objects.append(object);
    }
    if (objects.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("BatchRenameDialog"));
    dialog.setWindowTitle(u("Batch Rename"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* pattern = new QLineEdit(QStringLiteral("Object_{n}"), &dialog);
    auto* start = new QSpinBox(&dialog);
    start->setRange(-1000000, 1000000); start->setValue(1);
    form->addRow(u("Pattern ({n} is the number):"), pattern);
    form->addRow(u("Start Number:"), start);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || pattern->text().trimmed().isEmpty()) return;
    QStringList before;
    QStringList after;
    for (qsizetype index = 0; index < objects.size(); ++index) {
        before.append(objects[index]->name());
        QString nextName = pattern->text().trimmed();
        const QString number = QString::number(start->value() + static_cast<int>(index));
        if (nextName.contains(QStringLiteral("{n}"))) nextName.replace(QStringLiteral("{n}"), number);
        else nextName += QStringLiteral("_") + number;
        after.append(nextName);
    }
    const auto applyNames = [this, objects](const QStringList& names) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setName(names[index]);
        }
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh(); updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Batch Rename"), [applyNames, after]() { applyNames(after); },
        [applyNames, before]() { applyNames(before); }));
}

void MainWindow::assignSurfacesToSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> objects;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (object && !isLockedForModification(object.get())) objects.append(object);
    }
    if (objects.isEmpty()) return;

    SurfaceAssignmentDialog dialog(m_project.get(), objects.size(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    struct Assignment {
        QString defaultSurface;
        QMap<QString, QString> faces;
    };
    QVector<Assignment> before;
    QVector<Assignment> after;
    before.reserve(objects.size());
    after.reserve(objects.size());
    for (const auto& object : objects) {
        Assignment current{object->defaultSurfaceId(), object->faceSurfaceIds()};
        before.append(current);
        Assignment next = current;
        if (dialog.copyMode()) {
            const auto source = std::dynamic_pointer_cast<FcGeometryObject>(
                m_project->document()->findObject(dialog.sourceGeometryId()));
            if (source) {
                next.defaultSurface = source->defaultSurfaceId();
                next.faces.clear();
                QSet<QString> targetTopology;
                for (const GeometryFaceInfo& face :
                     BuildingGeometryService::faceInfos(object->shape())) {
                    targetTopology.insert(face.key);
                }
                for (auto it = source->faceSurfaceIds().cbegin();
                     it != source->faceSurfaceIds().cend(); ++it) {
                    const bool directional =
                        !it.key().startsWith(QStringLiteral("TopoFace:"));
                    if (directional || (dialog.copyTopologyOverrides() &&
                                        targetTopology.contains(it.key()))) {
                        next.faces.insert(it.key(), it.value());
                    }
                }
            }
        } else {
            if (dialog.defaultChoice() != SurfaceAssignmentDialog::keepValue()) {
                next.defaultSurface = dialog.defaultChoice();
            }
            if (dialog.replaceOverrides()) next.faces.clear();
            const QString face = dialog.faceChoice();
            if (face == SurfaceAssignmentDialog::clearAllFacesValue()) {
                next.faces.clear();
            } else if (face == SurfaceAssignmentDialog::allTopologyFacesValue()) {
                for (const GeometryFaceInfo& info :
                     BuildingGeometryService::faceInfos(object->shape())) {
                    if (dialog.faceSurfaceId().isEmpty()) next.faces.remove(info.key);
                    else next.faces.insert(info.key, dialog.faceSurfaceId());
                }
            } else if (face != SurfaceAssignmentDialog::keepValue()) {
                if (dialog.faceSurfaceId().isEmpty()) next.faces.remove(face);
                else next.faces.insert(face, dialog.faceSurfaceId());
            }
        }
        after.append(next);
    }
    const auto apply = [this, objects](const QVector<Assignment>& assignments) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            objects[index]->setDefaultSurfaceId(assignments[index].defaultSurface);
            objects[index]->setFaceSurfaceIds(assignments[index].faces);
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        if (objects.size() == 1) {
            m_propertiesWidget->showObject(objects.constFirst().get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Assign Surfaces"), [apply, after]() { apply(after); },
        [apply, before]() { apply(before); }));
}

void MainWindow::createTestBox()
{
    if (!m_project || !m_project->document()) {
        m_messageWidget->appendMessage(QStringLiteral("[Error] No active project."));
        return;
    }
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }

    FcObject::Ptr insertionParent = m_project->document()->geometryGroup();
    FcObject::Ptr selected = m_project->document()->findObject(
        m_modelTreeWidget ? m_modelTreeWidget->selectedObjectId() : QString{});
    for (FcObject* current = selected.get(); current; current = current->parent()) {
        if (current->type() == FcObjectType::Floor) {
            insertionParent = m_project->document()->findObject(current->id());
            break;
        }
    }
    const auto activeFloor = std::dynamic_pointer_cast<FcFloorObject>(insertionParent);
    const QString defaultName =
        QStringLiteral("Box_%1").arg(m_nextBoxNumber, 3, 10, QLatin1Char('0'));
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CreateBoxDialog"));
    dialog.setWindowTitle(u("Create Box"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* nameEdit = new QLineEdit(defaultName, &dialog);
    const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
    auto* x = createEngineeringSpinBox(
        &dialog, -1.0e9, 1.0e9,
        m_project->metersToDisplay((m_nextBoxNumber - 1) * 1.5), unit);
    auto* y = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* z = createEngineeringSpinBox(
        &dialog, -1.0e9, 1.0e9,
        m_project->metersToDisplay(activeFloor ? activeFloor->baseElevation() : 0.0),
        unit);
    auto* width = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(1.0), unit);
    auto* depth = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(1.0), unit);
    auto* height = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(1.0), unit);
    form->addRow(u("Name:"), nameEdit);
    form->addRow(QStringLiteral("X:"), x);
    form->addRow(QStringLiteral("Y:"), y);
    form->addRow(QStringLiteral("Z:"), z);
    form->addRow(u("Width:"), width);
    form->addRow(u("Depth:"), depth);
    form->addRow(u("Height:"), height);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString objectName = nameEdit->text().trimmed().isEmpty()
                                   ? defaultName
                                   : nameEdit->text().trimmed();

    try {
        const TopoDS_Shape shape = BRepPrimAPI_MakeBox(
            gp_Pnt(m_project->displayToMeters(x->value()),
                   m_project->displayToMeters(y->value()),
                   m_project->displayToMeters(z->value())),
            m_project->displayToMeters(width->value()),
            m_project->displayToMeters(depth->value()),
            m_project->displayToMeters(height->value())).Shape();
        auto geometryObject = std::make_shared<FcGeometryObject>(objectName, shape);
        geometryObject->setGeometryKind(FcGeometryKind::Box);
        BuildingGeometryRequest request;
        request.kind = FcGeometryKind::Box;
        request.x = m_project->displayToMeters(x->value());
        request.y = m_project->displayToMeters(y->value());
        request.z = m_project->displayToMeters(z->value());
        request.width = m_project->displayToMeters(width->value());
        request.depth = m_project->displayToMeters(depth->value());
        request.height = m_project->displayToMeters(height->value());
        geometryObject->setGeometryParameters(
            BuildingGeometryService::requestToParameters(request));
        if (activeFloor) geometryObject->setFloorName(activeFloor->name());
        ++m_nextBoxNumber;
        const auto refresh = [this, geometryObject, insertionParent](bool present) {
            if (present) {
                if (!geometryObject->parent() && insertionParent) {
                    insertionParent->addChild(geometryObject);
                }
            } else {
                if (geometryObject->parent()) {
                    geometryObject->parent()->removeChild(geometryObject->id());
                }
            }
            m_project->setModified(true);
            m_currentFdsPath.clear();
            m_modelTreeWidget->refresh();
            rebuildFdsScene(false);
            if (present) m_modelTreeWidget->selectObjectById(geometryObject->id());
            updateWindowTitle();
        };
        m_undoStack->push(new FunctionalUndoCommand(
            u("Create Box"),
            [refresh]() { refresh(true); },
            [refresh]() { refresh(false); }));
        m_occViewWidget->fitAll();
        m_messageWidget->appendMessage(
            QStringLiteral("[Info] Geometry created: %1 (UUID %2)")
                .arg(objectName, geometryObject->id()));
        statusBar()->showMessage(QStringLiteral("Created %1").arg(objectName),
                                 kStatusMessageDurationMs);
    } catch (const Standard_Failure&) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Failed to create geometry: %1").arg(objectName));
        statusBar()->showMessage(
            QStringLiteral("Failed to create %1").arg(objectName), kStatusMessageDurationMs);
    }
}

void MainWindow::createBuildingElement(int geometryKind)
{
    if (!m_project || !m_project->document()) return;
    const FcGeometryKind kind = static_cast<FcGeometryKind>(geometryKind);
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }
    FcObject::Ptr insertionParent = m_project->document()->geometryGroup();
    FcObject::Ptr selected = m_project->document()->findObject(
        m_modelTreeWidget ? m_modelTreeWidget->selectedObjectId() : QString{});
    for (FcObject* current = selected.get(); current; current = current->parent()) {
        if (current->type() == FcObjectType::Floor) {
            insertionParent = m_project->document()->findObject(current->id());
            break;
        }
    }
    const auto activeFloor = std::dynamic_pointer_cast<FcFloorObject>(insertionParent);
    BuildingElementDialog dialog(kind, m_project.get(), this);
    dialog.setObjectName(QStringLiteral("CreateBuildingElementDialog"));
    if (activeFloor) {
        BuildingGeometryRequest defaults;
        defaults.kind = kind;
        defaults.z = activeFloor->baseElevation();
        defaults.height = activeFloor->defaultWallHeight();
        defaults.rise = activeFloor->defaultStoreyHeight();
        if (kind == FcGeometryKind::Slab) {
            defaults.height = activeFloor->defaultSlabThickness();
            defaults.thickness = activeFloor->defaultSlabThickness();
        }
        dialog.setInitialRequest(defaults);
        if(auto* groups=dialog.findChild<QComboBox*>(QStringLiteral("GeometryGroupCombo")))
            groups->setCurrentIndex(groups->findData(activeFloor->id()));
    }
    if (BuildingGeometryService::isOpeningKind(kind)) {
        const auto host=std::dynamic_pointer_cast<FcGeometryObject>(selected);
        if (host && host->geometryKind()==FcGeometryKind::Wall) {
            const auto wall=BuildingGeometryService::requestFromParameters(host->geometryKind(),host->geometryParameters());
            const double length=std::hypot(wall.endX-wall.x,wall.endY-wall.y);
            if(length>1e-6) {
                BuildingGeometryRequest opening;
                opening.kind=kind; opening.width=std::min(1.0,length*0.5);
                opening.height=std::min(2.0,wall.height*0.7); opening.depth=wall.thickness+0.02;
                const double side=wall.baseline==FcWallBaseline::Center ? -wall.thickness/2 :
                                  wall.baseline==FcWallBaseline::Right ? -wall.thickness : 0;
                const double tx=(wall.endX-wall.x)/length,ty=(wall.endY-wall.y)/length;
                opening.x=wall.x+tx*(length-opening.width)/2-ty*(side-0.01);
                opening.y=wall.y+ty*(length-opening.width)/2+tx*(side-0.01);
                opening.z=wall.z; opening.rotationDegrees=std::atan2(ty,tx)*180/std::acos(-1.0);
                dialog.setInitialRequest(opening);
                if(auto* combo=dialog.findChild<QComboBox*>(QStringLiteral("OpeningHostCombo")))
                    combo->setCurrentIndex(combo->findData(host->id()));
            }
        }
    }
    connectGeometryPreview(dialog);
    const int outcome = dialog.exec();
    m_occViewWidget->clearGeometryPreview();
    if (outcome != QDialog::Accepted) return;
    QString error;
    BuildingGeometryRequest request = dialog.request();
    if (BuildingGeometryService::isOpeningKind(request.kind) &&
        !dialog.hostObjectId().isEmpty()) {
        const auto host = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(dialog.hostObjectId()));
        if (host && host->geometryKind() == FcGeometryKind::Wall) {
            request = BuildingGeometryService::attachOpeningToWall(
                request, BuildingGeometryService::requestFromParameters(
                             host->geometryKind(), host->geometryParameters()));
        }
    }
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request, &error);
    if (shape.IsNull()) {
        QMessageBox::critical(this, u("Invalid Geometry"), error);
        return;
    }
    auto object = std::make_shared<FcGeometryObject>(dialog.geometryName(), shape);
    object->setGeometryKind(request.kind);
    object->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    object->setHostObjectId(dialog.hostObjectId());
    object->setControlObjectId(dialog.controlObjectId());
    object->setDefaultSurfaceId(dialog.surfaceObjectId());
    object->setFaceSurfaceIds(dialog.faceSurfaceIds());
    object->setDynamicOpening(dialog.dynamicOpening());
    if (!object->hostObjectId().isEmpty()) {
        const auto host=std::dynamic_pointer_cast<FcGeometryObject>(m_project->document()->findObject(object->hostObjectId()));
        const QString placement=host ? GeometryEditDependencyService::validateOpeningPlacement(request,
            BuildingGeometryService::requestFromParameters(host->geometryKind(),host->geometryParameters()))
            : u("The opening host no longer exists.");
        if(!placement.isEmpty()) {QMessageBox::warning(this,u("Invalid Geometry"),UiLanguageManager::text(placement));return;}
    }
    if (!dialog.groupObjectId().isEmpty()) {
        const auto chosen=m_project->document()->findObject(dialog.groupObjectId());
        if(chosen && !isLockedForModification(chosen.get())) insertionParent=chosen;
    }
    if (activeFloor) object->setFloorName(activeFloor->name());
    const auto applyPresence = [this, object, insertionParent](bool present) {
        if (present) {
            if (!object->parent() && insertionParent) insertionParent->addChild(object);
        } else {
            if (object->parent()) object->parent()->removeChild(object->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        if (present) {
            m_modelTreeWidget->selectObjectById(object->id());
            m_propertiesWidget->showObject(object.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Create Building Geometry"),
        [applyPresence]() { applyPresence(true); },
        [applyPresence]() { applyPresence(false); }));
    m_occViewWidget->fitAll();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Building geometry created: %1 [%2] (UUID %3)")
            .arg(object->name(), fcGeometryKindName(kind), object->id()));
}

void MainWindow::startInteractiveWallDrawing()
{
    OccViewWidget* sketchView =
        m_workspaceTabs && m_workspaceTabs->currentIndex() == 1
            ? m_planViewWidget : m_occViewWidget;
    if (!m_project || !m_project->document() || !sketchView ||
        !sketchView->isInitialized()) return;
    if (isLockedForModification(m_project->document()->geometryGroup().get())) {
        statusBar()->showMessage(u("Locked objects cannot be modified."),
                                 kStatusMessageDurationMs);
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("WallSketchSettingsDialog"));
    dialog.setWindowTitle(u("Draw Wall in View"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* instruction = new QLabel(
        u("Set the wall properties, then click wall endpoints in the top view. Hold Shift for an orthogonal segment; press Esc to finish or cancel."),
        &dialog);
    instruction->setWordWrap(true);
    layout->addWidget(instruction);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(
        QStringLiteral("Wall_%1").arg(m_project->document()->geometryGroup()->children().size() + 1,
                                       3, 10, QLatin1Char('0')),
        &dialog);
    const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
    std::shared_ptr<FcFloorObject> activeFloor;
    FcObject::Ptr selected = m_project->document()->findObject(
        m_modelTreeWidget ? m_modelTreeWidget->selectedObjectId() : QString{});
    for (FcObject* current = selected.get(); current; current = current->parent()) {
        const FcObject::Ptr candidate = m_project->document()->findObject(current->id());
        activeFloor = std::dynamic_pointer_cast<FcFloorObject>(candidate);
        if (activeFloor) break;
    }
    m_wallSketchFloorId = activeFloor ? activeFloor->id() : QString{};
    auto* elevation = createEngineeringSpinBox(
        &dialog, -1.0e9, 1.0e9,
        m_project->metersToDisplay(activeFloor ? activeFloor->baseElevation() : 0.0),
        unit);
    auto* thickness = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(0.2), unit);
    auto* height = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9,
        m_project->metersToDisplay(activeFloor ? activeFloor->defaultWallHeight() : 3.0),
        unit);
    auto* baseline = new QComboBox(&dialog);
    baseline->addItems({u("Left"), u("Center"), u("Right")});
    baseline->setCurrentIndex(1);
    auto* continuous = new QCheckBox(u("Continue drawing connected wall segments"), &dialog);
    auto* autoConnect = new QCheckBox(u("Automatically join nearby wall endpoints"), &dialog);
    auto* exactLengthEnabled = new QCheckBox(u("Use exact length"), &dialog);
    auto* exactLength = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(1.0), unit);
    auto* exactAngleEnabled = new QCheckBox(u("Use exact angle"), &dialog);
    auto* exactAngle = createEngineeringSpinBox(
        &dialog, -360.0, 360.0, 0.0, QStringLiteral("°"));
    continuous->setObjectName(QStringLiteral("WallSketchContinuousCheck"));
    autoConnect->setObjectName(QStringLiteral("WallSketchAutoConnectCheck"));
    exactLengthEnabled->setObjectName(QStringLiteral("WallSketchExactLengthCheck"));
    exactLength->setObjectName(QStringLiteral("WallSketchExactLengthSpin"));
    exactAngleEnabled->setObjectName(QStringLiteral("WallSketchExactAngleCheck"));
    exactAngle->setObjectName(QStringLiteral("WallSketchExactAngleSpin"));
    continuous->setChecked(false);
    autoConnect->setChecked(true);
    name->setObjectName(QStringLiteral("WallSketchNameEdit"));
    elevation->setObjectName(QStringLiteral("WallSketchElevationSpin"));
    thickness->setObjectName(QStringLiteral("WallSketchThicknessSpin"));
    height->setObjectName(QStringLiteral("WallSketchHeightSpin"));
    baseline->setObjectName(QStringLiteral("WallSketchBaselineCombo"));
    form->addRow(u("Name:"), name);
    form->addRow(u("Elevation:"), elevation);
    form->addRow(u("Thickness:"), thickness);
    form->addRow(u("Height:"), height);
    form->addRow(u("Wall baseline:"), baseline);
    form->addRow(exactLengthEnabled, exactLength);
    form->addRow(exactAngleEnabled, exactAngle);
    form->addRow(QString(), continuous);
    form->addRow(QString(), autoConnect);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    m_wallSketchName = name->text().trimmed().isEmpty()
                           ? QStringLiteral("Wall") : name->text().trimmed();
    m_wallSketchElevation = m_project->displayToMeters(elevation->value());
    m_wallSketchThickness = m_project->displayToMeters(thickness->value());
    m_wallSketchHeight = m_project->displayToMeters(height->value());
    m_wallSketchBaseline = baseline->currentIndex();
    m_wallSketchContinuous = continuous->isChecked();
    m_wallSketchAutoConnect = autoConnect->isChecked();
    m_wallSketchSegmentNumber = 0;
    const SnapSettings snap = m_snapManager ? m_snapManager->settings() : SnapSettings{};
    const double snapStep = snap.fdsGrid ? snap.fdsGridStep : snap.worldGridStep;
    QVector<std::shared_ptr<FcGeometryObject>> walls;
    collectGeometryObjects(m_project->document()->geometryGroup(), walls);
    struct WallLine { QPointF start; QPointF end; };
    QVector<WallLine> wallLines;
    QVector<QPointF> snapPoints;
    for (const auto& wall : walls) {
        if (!wall || wall->geometryKind() != FcGeometryKind::Wall || !wall->isVisible()) continue;
        const BuildingGeometryRequest request = BuildingGeometryService::requestFromParameters(
            wall->geometryKind(), wall->geometryParameters());
        const WallLine line{QPointF(request.x, request.y),
                            QPointF(request.endX, request.endY)};
        wallLines.append(line);
        if (snap.vertex) {
            snapPoints.append(line.start);
            snapPoints.append(line.end);
        }
        if (snap.edgeMidpoint) snapPoints.append((line.start + line.end) * 0.5);
    }
    if (snap.intersection) {
        for (int left = 0; left < wallLines.size(); ++left) {
            for (int right = left + 1; right < wallLines.size(); ++right) {
                const QPointF p = wallLines[left].start;
                const QPointF r = wallLines[left].end - p;
                const QPointF q = wallLines[right].start;
                const QPointF s = wallLines[right].end - q;
                const double cross = r.x() * s.y() - r.y() * s.x();
                if (std::abs(cross) <= 1.0e-12) continue;
                const QPointF qp = q - p;
                const double t = (qp.x() * s.y() - qp.y() * s.x()) / cross;
                const double uValue = (qp.x() * r.y() - qp.y() * r.x()) / cross;
                if (t >= -1.0e-9 && t <= 1.0 + 1.0e-9 &&
                    uValue >= -1.0e-9 && uValue <= 1.0 + 1.0e-9) {
                    snapPoints.append(p + r * t);
                }
            }
        }
    }
    m_wallSketchView = sketchView;
    m_transformGizmoAction->setChecked(false);
    sketchView->endDirectEditing();
    sketchView->setWallSketchSnapPoints(snapPoints);
    sketchView->setPerspective(false);
    sketchView->setOrientation(OccViewOrientation::Top);
    sketchView->beginWallSketch(m_wallSketchElevation,
                                m_wallSketchThickness,
                                m_wallSketchHeight,
                                m_wallSketchBaseline,
                                snap.enabled,
                                snapStep,
                                m_wallSketchContinuous,
                                snap.orthogonal,
                                exactLengthEnabled->isChecked()
                                    ? m_project->displayToMeters(exactLength->value()) : 0.0,
                                exactAngleEnabled->isChecked()
                                    ? exactAngle->value()
                                    : std::numeric_limits<double>::quiet_NaN());
    if (m_activeToolLabel) {
        m_activeToolLabel->setText(
            QStringLiteral("%1\n%2=%3 %4; %5=%6 %4")
                .arg(u("Active tool: Draw Wall"), u("Thickness"))
                .arg(m_project->metersToDisplay(m_wallSketchThickness))
                .arg(m_project->displayUnitSymbol(), u("Height"))
                .arg(m_project->metersToDisplay(m_wallSketchHeight)));
    }
    if (m_inspectorDock && m_inspectorTabs) {
        m_inspectorDock->show();
        m_inspectorDock->raise();
        m_inspectorTabs->setCurrentIndex(0);
    }
    statusBar()->showMessage(
        u("Wall drawing: click the start point and end point; hold Shift for orthogonal, Esc to cancel."));
}

void MainWindow::finishInteractiveWallDrawing(double startX, double startY,
                                               double endX, double endY)
{
    if (!m_project || !m_project->document()) return;
    QString startConnection;
    QString endConnection;
    if (m_wallSketchAutoConnect) {
        QVector<std::shared_ptr<FcGeometryObject>> existingWalls;
        collectGeometryObjects(m_project->document()->geometryGroup(), existingWalls);
        const SnapSettings snap = m_snapManager ? m_snapManager->settings() : SnapSettings{};
        const double tolerance = std::max(0.01,
            0.55 * (snap.fdsGrid ? snap.fdsGridStep : snap.worldGridStep));
        const QPointF snappedStart = BuildingGeometryService::snapWallEndpoint(
            QPointF(startX, startY), existingWalls, tolerance, &startConnection);
        const QPointF snappedEnd = BuildingGeometryService::snapWallEndpoint(
            QPointF(endX, endY), existingWalls, tolerance, &endConnection);
        startX = snappedStart.x(); startY = snappedStart.y();
        endX = snappedEnd.x(); endY = snappedEnd.y();
    }
    BuildingGeometryRequest request;
    request.kind = FcGeometryKind::Wall;
    request.x = startX;
    request.y = startY;
    request.z = m_wallSketchElevation;
    request.endX = endX;
    request.endY = endY;
    request.thickness = m_wallSketchThickness;
    request.height = m_wallSketchHeight;
    request.baseline = static_cast<FcWallBaseline>(m_wallSketchBaseline);
    QString error;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request, &error);
    if (shape.IsNull()) {
        QMessageBox::critical(this, u("Invalid Geometry"), error);
        return;
    }
    ++m_wallSketchSegmentNumber;
    const QString segmentName = m_wallSketchContinuous && m_wallSketchSegmentNumber > 1
                                    ? QStringLiteral("%1_%2")
                                          .arg(m_wallSketchName)
                                          .arg(m_wallSketchSegmentNumber, 3, 10,
                                               QLatin1Char('0'))
                                    : m_wallSketchName;
    auto object = std::make_shared<FcGeometryObject>(segmentName, shape);
    object->setGeometryKind(FcGeometryKind::Wall);
    QVariantMap wallParameters = BuildingGeometryService::requestToParameters(request);
    if (!startConnection.isEmpty()) {
        wallParameters.insert(QStringLiteral("connectedStartWallUuid"), startConnection);
    }
    if (!endConnection.isEmpty()) {
        wallParameters.insert(QStringLiteral("connectedEndWallUuid"), endConnection);
    }
    object->setGeometryParameters(wallParameters);
    const FcObject::Ptr insertionParent = m_wallSketchFloorId.isEmpty()
        ? std::static_pointer_cast<FcObject>(m_project->document()->geometryGroup())
        : m_project->document()->findObject(m_wallSketchFloorId);
    if (const auto floor = std::dynamic_pointer_cast<FcFloorObject>(insertionParent)) {
        object->setFloorName(floor->name());
    }
    const auto apply = [this, object, insertionParent](bool present) {
        if (present) {
            if (!object->parent() && insertionParent) insertionParent->addChild(object);
        } else {
            if (object->parent()) object->parent()->removeChild(object->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false);
        if (present) m_modelTreeWidget->selectObjectById(object->id());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Draw Wall"), [apply]() { apply(true); }, [apply]() { apply(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Wall drawn in view: %1, (%2,%3)-(%4,%5) m, UUID %6%7")
            .arg(object->name()).arg(startX, 0, 'f', 3).arg(startY, 0, 'f', 3)
            .arg(endX, 0, 'f', 3).arg(endY, 0, 'f', 3).arg(object->id())
            .arg(startConnection.isEmpty() && endConnection.isEmpty()
                     ? QString()
                     : QStringLiteral(" [auto-connected]")));
    if (!m_wallSketchView || !m_wallSketchView->isWallSketchActive()) {
        if (m_wallSketchView == m_occViewWidget) {
            m_occViewWidget->setOrientation(OccViewOrientation::Isometric);
        }
        m_wallSketchView = nullptr;
        statusBar()->showMessage(u("Wall created from two picked points."),
                                 kStatusMessageDurationMs);
    } else {
        statusBar()->showMessage(
            u("Wall segment created. Click the next endpoint; press Esc to finish."));
    }
}

void MainWindow::connectGeometryPreview(BuildingElementDialog& dialog)
{
    connect(&dialog, &BuildingElementDialog::geometryPreviewRequested,
            m_occViewWidget, &OccViewWidget::showGeometryPreview);
    connect(&dialog, &BuildingElementDialog::facePreviewRequested, this,
        [this, &dialog](const QString& key) {
            const TopoDS_Shape shape=BuildingGeometryService::createShape(dialog.request());
            const auto faces=BuildingGeometryService::faceInfos(shape);
            for (const auto& face:faces) {
                if (face.key!=key) continue;
                int index=0;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    if(++index==face.faceIndex) {m_occViewWidget->showGeometryPreview(it.Current());return;}
                }
            }
        });
}

void MainWindow::toggleDirectGeometryEditing(bool enabled)
{
    if (!enabled) {m_occViewWidget->endDirectEditing();return;}
    if (!m_project || !m_project->document()) return;
    const auto object=std::dynamic_pointer_cast<FcGeometryObject>(
        m_project->document()->findObject(m_modelTreeWidget->selectedObjectId()));
    if (!object || isLockedForModification(object.get())) return;
    QString consistencyError;
    if(!GeometryEditService::matchesShape(BuildingGeometryService::requestFromParameters(
        object->geometryKind(),object->geometryParameters()),object->shape(),1e-6,&consistencyError)) {
        const QSignalBlocker blocker(m_directEditAction);m_directEditAction->setChecked(false);
        QMessageBox::warning(this,u("Invalid Geometry"),UiLanguageManager::text(consistencyError));return;
    }
    m_transformGizmoAction->setChecked(false);
    const bool started=m_occViewWidget->beginDirectEditing(object->id(),
        BuildingGeometryService::requestFromParameters(object->geometryKind(),object->geometryParameters()),
        m_snapManager->settings().enabled ? (m_snapManager->settings().fdsGrid
            ? m_snapManager->settings().fdsGridStep : (m_snapManager->settings().worldGrid
                ? m_snapManager->settings().worldGridStep : 0.0)) : 0.0);
    const QSignalBlocker blocker(m_directEditAction);
    m_directEditAction->setChecked(started);
    statusBar()->showMessage(started
        ? u("Drag an orange handle; double-click for exact meters. Polygon points: Shift selects V, otherwise U. Esc cancels. Grid snap uses the current step.")
        : u("Direct editing supports boxes, straight walls, slabs and polygon extrusions only."));
}

void MainWindow::editSelectedBuildingGeometry()
{
    if (m_modelTreeWidget) editBuildingGeometry(m_modelTreeWidget->selectedObjectId());
}

void MainWindow::editBuildingGeometry(const QString& objectId)
{
    if (!m_project || !m_project->document() || objectId.isEmpty()) return;
    const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
        m_project->document()->findObject(objectId));
    if (!object || isLockedForModification(object.get())) return;
    if (object->geometryKind() == FcGeometryKind::Generic) {
        openObjectProperties(objectId); return;
    }
    if (object->geometryKind() == FcGeometryKind::BackgroundImage) {
        QDialog backgroundDialog(this);
        backgroundDialog.setObjectName(QStringLiteral("EditBackgroundImageDialog"));
        backgroundDialog.setWindowTitle(u("Edit Background Image"));
        auto* layout = new QVBoxLayout(&backgroundDialog);
        auto* form = new QFormLayout;
        auto* name = new QLineEdit(object->name(), &backgroundDialog);
        auto* plane = new QComboBox(&backgroundDialog);
        plane->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")});
        const QVariantMap beforeParameters = object->geometryParameters();
        plane->setCurrentText(beforeParameters.value(QStringLiteral("plane")).toString());
        const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
        auto* width = createEngineeringSpinBox(
            &backgroundDialog, 0.000001, 1.0e9,
            m_project->metersToDisplay(beforeParameters.value(QStringLiteral("width")).toDouble()), unit);
        auto* height = createEngineeringSpinBox(
            &backgroundDialog, 0.000001, 1.0e9,
            m_project->metersToDisplay(beforeParameters.value(QStringLiteral("height")).toDouble()), unit);
        auto* opacity = createEngineeringSpinBox(
            &backgroundDialog, 0.05, 1.0,
            beforeParameters.value(QStringLiteral("opacity"), 0.65).toDouble());
        form->addRow(u("Name:"), name); form->addRow(u("Plane:"), plane);
        form->addRow(u("Calibrated Width:"), width);
        form->addRow(u("Calibrated Height:"), height);
        form->addRow(u("Opacity:"), opacity); layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &backgroundDialog);
        connect(buttons, &QDialogButtonBox::accepted, &backgroundDialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &backgroundDialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (backgroundDialog.exec() != QDialog::Accepted) return;
        QVariantMap afterParameters = beforeParameters;
        afterParameters.insert(QStringLiteral("plane"), plane->currentText());
        afterParameters.insert(QStringLiteral("width"), m_project->displayToMeters(width->value()));
        afterParameters.insert(QStringLiteral("height"), m_project->displayToMeters(height->value()));
        afterParameters.insert(QStringLiteral("opacity"), opacity->value());
        const auto makeBackgroundShape = [](const QVariantMap& parameters) {
            const double x = parameters.value(QStringLiteral("x")).toDouble();
            const double y = parameters.value(QStringLiteral("y")).toDouble();
            const double z = parameters.value(QStringLiteral("z")).toDouble();
            const double w = parameters.value(QStringLiteral("width")).toDouble();
            const double h = parameters.value(QStringLiteral("height")).toDouble();
            constexpr double thickness = 1.0e-4;
            if (parameters.value(QStringLiteral("plane")).toString() == QStringLiteral("XZ")) {
                return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), w, thickness, h).Shape();
            }
            if (parameters.value(QStringLiteral("plane")).toString() == QStringLiteral("YZ")) {
                return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), thickness, w, h).Shape();
            }
            return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), w, h, thickness).Shape();
        };
        const QString beforeName = object->name();
        const QString afterName = name->text().trimmed().isEmpty() ? beforeName : name->text().trimmed();
        const TopoDS_Shape beforeShape = object->shape();
        const TopoDS_Shape afterShape = makeBackgroundShape(afterParameters);
        const auto applyBackground = [this, object](const QString& valueName,
                                                    const QVariantMap& parameters,
                                                    const TopoDS_Shape& shape) {
            object->setName(valueName); object->setGeometryParameters(parameters);
            object->setShape(shape);
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
                manager->displayObject(object);
            }
            m_project->setModified(true); m_modelTreeWidget->refresh();
            m_modelTreeWidget->selectObjectById(object->id()); updateWindowTitle();
        };
        m_undoStack->push(new FunctionalUndoCommand(
            u("Edit Background Image"),
            [=]() { applyBackground(afterName, afterParameters, afterShape); },
            [=]() { applyBackground(beforeName, beforeParameters, beforeShape); }));
        return;
    }
    BuildingElementDialog dialog(object->geometryKind(), m_project.get(), this);
    dialog.setObjectName(QStringLiteral("EditBuildingElementDialog"));
    dialog.setExistingObject(object);
    connectGeometryPreview(dialog);
    const int outcome = dialog.exec();
    m_occViewWidget->clearGeometryPreview();
    if (outcome != QDialog::Accepted) return;
    QString error;
    if (!applyBuildingGeometryEdit(objectId, dialog.request(), dialog.geometryName(),
        dialog.hostObjectId(), dialog.controlObjectId(), dialog.surfaceObjectId(),
        dialog.faceSurfaceIds(), dialog.dynamicOpening(), dialog.groupObjectId(), &error))
        QMessageBox::warning(this, u("Invalid Geometry"), UiLanguageManager::text(error));
}

bool MainWindow::commitParametricTransforms(const QStringList& ids,const QVector<gp_Trsf>& transforms)
{
    if(!m_project || ids.size()!=transforms.size())return false;
    QVector<QVariantMap> parameters;
    QStringList targets;
    for(int i=0;i<ids.size();++i) {
        const auto object=std::dynamic_pointer_cast<FcGeometryObject>(m_project->document()->findObject(ids[i]));
        if(!object)return false;
        // An attached opening follows its selected host in the host transaction.
        if(ids.contains(object->hostObjectId()))continue;
        BuildingGeometryRequest next;
        QString error;
        const auto before=BuildingGeometryService::requestFromParameters(object->geometryKind(),object->geometryParameters());
        if(!GeometryEditService::transformRequest(before,transforms[i],&next,&error) ||
           !commitGeometryParameters(ids[i],BuildingGeometryService::requestToParameters(next),&error,true)) {
            QMessageBox::warning(this,u("Invalid Geometry"),UiLanguageManager::text(error));return false;
        }
        parameters.append(BuildingGeometryService::requestToParameters(next));targets.append(ids[i]);
    }
    m_undoStack->beginMacro(u("Transform Geometry"));
    bool ok=true;
    for(int i=0;i<targets.size();++i) {
        QString error;
        if(!commitGeometryParameters(targets[i],parameters[i],&error)) {ok=false;break;}
    }
    m_undoStack->endMacro();
    if(!ok)m_undoStack->undo();
    updateMultiSelection(ids,true,true);
    return ok;
}

bool MainWindow::commitGeometryParameters(const QString& objectId, const QVariantMap& parameters, QString* error, bool validateOnly)
{
    if (!m_project || !m_project->document()) return false;
    const auto object=std::dynamic_pointer_cast<FcGeometryObject>(m_project->document()->findObject(objectId));
    if (!object) return false;
    return applyBuildingGeometryEdit(objectId,
        BuildingGeometryService::requestFromParameters(object->geometryKind(),parameters), object->name(),
        object->hostObjectId(),object->controlObjectId(),object->defaultSurfaceId(),object->faceSurfaceIds(),
        object->isDynamicOpening(),object->parent()?object->parent()->id():QString{},error,validateOnly);
}

bool MainWindow::applyBuildingGeometryEdit(const QString& objectId, BuildingGeometryRequest request,
    const QString& afterName, const QString& afterHost, const QString& afterControl,
    const QString& afterSurface, const QMap<QString,QString>& afterFaceSurfaces, bool afterDynamic,
    const QString& groupId, QString* errorMessage, bool validateOnly)
{
    const auto fail=[errorMessage](const QString& text) { if(errorMessage)*errorMessage=text; return false; };
    if (errorMessage) errorMessage->clear();
    if (!m_project || !m_project->document()) return false;
    const auto object=std::dynamic_pointer_cast<FcGeometryObject>(m_project->document()->findObject(objectId));
    if (!object || isLockedForModification(object.get())) return fail(u("Locked objects cannot be modified."));
    QString error;
    const BuildingGeometryRequest previousRequest =
        BuildingGeometryService::requestFromParameters(
            object->geometryKind(), object->geometryParameters());
    if(!GeometryEditService::handles(previousRequest).isEmpty() &&
       !GeometryEditService::matchesShape(previousRequest,object->shape(),1e-6,&error)) return fail(UiLanguageManager::text(error));
    if (BuildingGeometryService::isOpeningKind(request.kind) &&
        !afterHost.isEmpty()) {
        const auto host = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(afterHost));
        if (host && host->geometryKind() == FcGeometryKind::Wall) {
            request = BuildingGeometryService::attachOpeningToWall(
                request, BuildingGeometryService::requestFromParameters(
                             host->geometryKind(), host->geometryParameters()));
        }
        if(!host) return fail(u("The opening host no longer exists."));
        if(isLockedForModification(host.get())) return fail(u("Locked objects cannot be modified."));
        const QString placement=GeometryEditDependencyService::validateOpeningPlacement(request,
            BuildingGeometryService::requestFromParameters(host->geometryKind(),host->geometryParameters()));
        if(!placement.isEmpty()) return fail(UiLanguageManager::text(placement));
    }
    auto dependencyObject=std::make_shared<FcGeometryObject>(object->name(),object->shape());
    copyGeometrySemantics(*object,*dependencyObject);dependencyObject->restorePersistentId(objectId);
    dependencyObject->setHostObjectId(afterHost);
    const QStringList dependencyErrors=GeometryEditDependencyService::validate(*m_project->document(),*dependencyObject,request);
    if(!dependencyErrors.isEmpty()) {
        QStringList translated;for(const QString& message:dependencyErrors) translated.append(UiLanguageManager::text(message));
        return fail(translated.join(QLatin1Char('\n')));
    }
    const TopoDS_Shape afterShape = BuildingGeometryService::createShape(request, &error);
    if (afterShape.IsNull()) {
        return fail(error);
    }
    const QString beforeName = object->name();
    const TopoDS_Shape beforeShape = object->shape();
    const FcGeometryKind beforeKind = object->geometryKind();
    const QVariantMap beforeParameters = object->geometryParameters();
    const QString beforeHost = object->hostObjectId();
    const QString beforeControl = object->controlObjectId();
    const QString beforeSurface = object->defaultSurfaceId();
    const QMap<QString, QString> beforeFaceSurfaces = object->faceSurfaceIds();
    const bool beforeDynamic = object->isDynamicOpening();
    const QVariantMap afterParameters = BuildingGeometryService::requestToParameters(request);
    const auto beforeParent=m_project->document()->findObject(object->parent()->id());
    const auto afterParent=groupId.isEmpty()?beforeParent:m_project->document()->findObject(groupId);
    if (!afterParent || isLockedForModification(afterParent.get()) ||
        (afterParent->type()!=FcObjectType::Group && afterParent->type()!=FcObjectType::Folder &&
         afterParent->type()!=FcObjectType::Floor)) return fail(u("Invalid geometry group."));
    for(FcObject* ancestor=afterParent.get();ancestor;ancestor=ancestor->parent())
        if(ancestor==object.get()) return fail(u("Invalid geometry group."));
    QSet<QString> faceKeys;
    for(const auto& face:BuildingGeometryService::faceInfos(afterShape)) faceKeys.insert(face.key);
    for(auto it=afterFaceSurfaces.cbegin();it!=afterFaceSurfaces.cend();++it) {
        if(it.key().startsWith(QStringLiteral("TopoFace:")) && !faceKeys.contains(it.key()))
            return fail(u("Geometry changed a surface-assigned face. Reassign or clear the affected faces in Properties first."));
    }
    QVector<HostedGeometryState> beforeHosted;
    QVector<HostedGeometryState> afterHosted;
    if (beforeKind == FcGeometryKind::Wall && request.kind == FcGeometryKind::Wall &&
        GeometryEditDependencyService::geometryChanged(*object,request)) {
        QVector<std::shared_ptr<FcGeometryObject>> allGeometry;
        collectGeometryObjects(m_project->document()->geometryGroup(), allGeometry);
        for (const auto& hosted : allGeometry) {
            if (!hosted || hosted->hostObjectId() != object->id() ||
                !BuildingGeometryService::isOpeningKind(hosted->geometryKind())) continue;
            const BuildingGeometryRequest oldOpening =
                BuildingGeometryService::requestFromParameters(
                    hosted->geometryKind(), hosted->geometryParameters());
            QString hostedError;
            const BuildingGeometryRequest newOpening =
                GeometryEditDependencyService::updatedHostedOpening(
                    oldOpening, previousRequest, request, &hostedError);
            if(!hostedError.isEmpty()) return fail(UiLanguageManager::text(hostedError));
            const TopoDS_Shape hostedShape =
                BuildingGeometryService::createShape(newOpening, &hostedError);
            if (hostedShape.IsNull()) {
                return fail(QStringLiteral("%1: %2").arg(hosted->name(), hostedError));
            }
            beforeHosted.append({hosted, hosted->shape(), hosted->geometryParameters()});
            afterHosted.append({hosted, hostedShape,
                                BuildingGeometryService::requestToParameters(newOpening)});
        }
    }
    QVector<std::shared_ptr<FcFdsMesh>> meshes;
    std::function<void(const FcObject::Ptr&)> collectMeshes=[&](const FcObject::Ptr& item) {
        if(!item)return;
        if(auto mesh=meshForGeometryConversion(item)) meshes.append(mesh);
        for(const auto& child:item->children()) collectMeshes(child);
    };
    collectMeshes(m_project->document()->meshesGroup());
    auto nextObject=std::make_shared<FcGeometryObject>(afterName,afterShape);
    copyGeometrySemantics(*object,*nextObject); nextObject->restorePersistentId(objectId);
    nextObject->setGeometryKind(request.kind);nextObject->setGeometryParameters(afterParameters);
    nextObject->setHostObjectId(afterHost);nextObject->setControlObjectId(afterControl);
    nextObject->setDefaultSurfaceId(afterSurface);nextObject->setFaceSurfaceIds(afterFaceSurfaces);
    nextObject->setDynamicOpening(afterDynamic);
    const auto translatedErrors=[](const QStringList& errors) {
        QStringList translated;
        for(const QString& message:errors) translated.append(UiLanguageManager::text(message));
        return translated.join(QLatin1Char('\n'));
    };
    const auto surfaceErrors=FdsBlockConversionService::validateSurfaceAssignments(*nextObject);
    if(!surfaceErrors.isEmpty())return fail(translatedErrors(surfaceErrors));
    auto derived=GeometryDerivedUpdateService::plan(*m_project->document(),object,nextObject,meshes);
    if(!derived.success()) return fail(translatedErrors(derived.errors));
    for(const auto& hosted:afterHosted) {
        auto nextHosted=std::make_shared<FcGeometryObject>(hosted.object->name(),hosted.shape);
        copyGeometrySemantics(*hosted.object,*nextHosted);nextHosted->restorePersistentId(hosted.object->id());
        nextHosted->setGeometryParameters(hosted.parameters);
        const auto plan=GeometryDerivedUpdateService::plan(*m_project->document(),hosted.object,nextHosted,meshes);
        if(!plan.success()) return fail(translatedErrors(plan.errors));
        derived.updates+=plan.updates;
    }
    if(validateOnly)return true;
    const auto apply = [this, object, derived](const QString& name,
                                      const TopoDS_Shape& shape,
                                      FcGeometryKind kind,
                                      const QVariantMap& parameters,
                                      const QString& host,
                                      const QString& control,
                                      const QString& surface,
                                      const QMap<QString, QString>& faceSurfaces,
                                      bool dynamic,
                                      const QVector<HostedGeometryState>& hostedStates,
                                      const FcObject::Ptr& parent, bool forward) {
        m_occViewWidget->endDirectEditing();
        if (parent && object->parent()!=parent.get()) {
            if(object->parent()) object->parent()->removeChild(object->id());
            parent->addChild(object);
        }
        object->setName(name); object->setShape(shape); object->setGeometryKind(kind);
        object->setGeometryParameters(parameters); object->setHostObjectId(host);
        object->setControlObjectId(control); object->setDefaultSurfaceId(surface);
        object->setFaceSurfaceIds(faceSurfaces);
        object->setDynamicOpening(dynamic);
        for(const auto& change:derived.updates)
            change.target->setParameters(forward ? change.afterParameters : change.beforeParameters);
        if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
            manager->displayObject(object); manager->selectObject(object->id());
            for (const HostedGeometryState& state : hostedStates) {
                if (!state.object) continue;
                state.object->setShape(state.shape);
                state.object->setGeometryParameters(state.parameters);
                manager->displayObject(state.object);
            }
        } else {
            for (const HostedGeometryState& state : hostedStates) {
                if (!state.object) continue;
                state.object->setShape(state.shape);
                state.object->setGeometryParameters(state.parameters);
            }
        }
        rebuildFdsScene(false, object->id());
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh(); m_modelTreeWidget->selectObjectById(object->id());
        m_propertiesWidget->showObject(object.get()); updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Edit Building Geometry"),
        [=]() { apply(afterName, afterShape, request.kind, afterParameters,
                      afterHost, afterControl, afterSurface, afterFaceSurfaces,
                      afterDynamic, afterHosted, afterParent, true); },
        [=]() { apply(beforeName, beforeShape, beforeKind, beforeParameters,
                      beforeHost, beforeControl, beforeSurface, beforeFaceSurfaces,
                      beforeDynamic, beforeHosted, beforeParent, false); }));
    return true;
}

void MainWindow::previewFdsBlocks()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> geometry;
    QVector<std::shared_ptr<FcFdsMesh>> meshes;
    const QStringList selectedObjectIds = m_modelTreeWidget->selectedObjectIds();
    const QSet<QString> selectedIds(selectedObjectIds.cbegin(), selectedObjectIds.cend());
    bool restrictToSelection = false;
    for (const QString& id : selectedIds) {
        const FcObject::Ptr selected = m_project->document()->findObject(id);
        restrictToSelection = restrictToSelection ||
            static_cast<bool>(std::dynamic_pointer_cast<FcGeometryObject>(selected)) ||
            static_cast<bool>(std::dynamic_pointer_cast<FcIfcObject>(selected));
    }
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& root, bool selectedAncestor) {
        if (!root) return;
        const bool selected = selectedAncestor || selectedIds.contains(root->id());
        if (const auto item = std::dynamic_pointer_cast<FcGeometryObject>(root)) {
            if (!restrictToSelection || selected) geometry.append(item);
        }
        if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(root)) {
            if ((!restrictToSelection || selected) && ifc->hasShape())
                geometry.append(ifcConversionProxy(ifc));
        }
        if (const auto mesh = meshForGeometryConversion(root)) meshes.append(mesh);
        for (const FcObject::Ptr& child : root->children()) collect(child, selected);
    };
    collect(m_project->document()->geometryGroup(), false);
    collect(m_project->document()->meshesGroup(), false);
    const FdsBlockConversionResult result =
        FdsBlockConversionService::convert(geometry, meshes);
    GeometryDisplayManager* manager = m_occViewWidget->displayManager();
    if (manager) {
        for (const QString& previewId : m_fdsPreviewObjectIds) manager->removeObject(previewId);
        m_fdsPreviewObjectIds.clear();
        for (const FdsBlockPreview& preview : result.previews) {
            if (preview.lost) continue;
            TopoDS_Shape displayShape;
            if (preview.target == FdsBlockTarget::Geom) {
                const auto source = std::find_if(
                    geometry.cbegin(), geometry.cend(), [&preview](const auto& item) {
                        return item && item->id() == preview.sourceObjectId;
                    });
                if (source != geometry.cend()) displayShape = (*source)->shape();
            }
            FcFdsBounds bounds = preview.actual;
            const double minimumThickness = 0.01;
            if (std::abs(bounds.xMax - bounds.xMin) < minimumThickness) bounds.xMax = bounds.xMin + minimumThickness;
            if (std::abs(bounds.yMax - bounds.yMin) < minimumThickness) bounds.yMax = bounds.yMin + minimumThickness;
            if (std::abs(bounds.zMax - bounds.zMin) < minimumThickness) bounds.zMax = bounds.zMin + minimumThickness;
            try {
                if (displayShape.IsNull()) {
                    displayShape = BRepPrimAPI_MakeBox(
                        gp_Pnt(bounds.xMin, bounds.yMin, bounds.zMin),
                        bounds.xMax - bounds.xMin,
                        bounds.yMax - bounds.yMin,
                        bounds.zMax - bounds.zMin).Shape();
                }
                GeometryDisplayStyle style;
                style.red = preview.target == FdsBlockTarget::Hole ? 0.95 : 0.1;
                style.green = preview.target == FdsBlockTarget::Hole ? 0.2 : 0.8;
                style.blue = preview.target == FdsBlockTarget::Vent ? 0.2 : 0.95;
                style.transparency = 0.45;
                const QString previewId = QStringLiteral("FDS-PREVIEW:%1").arg(preview.sourceObjectId);
                manager->appendShape(previewId, displayShape, style);
                m_fdsPreviewObjectIds.append(previewId);
            } catch (const Standard_Failure&) {
                m_messageWidget->appendMessage(
                    QStringLiteral("[Warning] Could not display FDS preview for %1.")
                        .arg(preview.sourceName));
            }
        }
        manager->updateViewer();
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("FdsBlockPreviewDialog"));
    dialog.setWindowTitle(u("Preview FDS Blocks"));
    dialog.resize(980, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(
        u("Original building geometry remains unchanged. Cyan/red translucent blocks show the mesh-snapped FDS Actual result."),
        &dialog);
    summary->setWordWrap(true); layout->addWidget(summary);
    auto* table = new QTableWidget(result.previews.size(), 6, &dialog);
    table->setObjectName(QStringLiteral("FdsBlockPreviewTable"));
    table->setHorizontalHeaderLabels({u("Source"), u("Target"), u("Requested XB"),
                                      u("Actual / Snapped XB"), u("Volume Error"), u("Status")});
    const auto boundsText = [](const FcFdsBounds& bounds) {
        return QStringLiteral("%1, %2, %3, %4, %5, %6")
            .arg(bounds.xMin, 0, 'g', 6).arg(bounds.xMax, 0, 'g', 6)
            .arg(bounds.yMin, 0, 'g', 6).arg(bounds.yMax, 0, 'g', 6)
            .arg(bounds.zMin, 0, 'g', 6).arg(bounds.zMax, 0, 'g', 6);
    };
    for (int row = 0; row < result.previews.size(); ++row) {
        const FdsBlockPreview& preview = result.previews[row];
        table->setItem(row, 0, new QTableWidgetItem(preview.sourceName));
        table->item(row, 0)->setData(Qt::UserRole, preview.sourceObjectId);
        table->setItem(row, 1, new QTableWidgetItem(FdsBlockConversionService::targetName(preview.target)));
        table->setItem(row, 2, new QTableWidgetItem(boundsText(preview.requested)));
        table->setItem(row, 3, new QTableWidgetItem(boundsText(preview.actual)));
        table->setItem(row, 4, new QTableWidgetItem(
            QStringLiteral("%1 %").arg(preview.volumeErrorPercent, 0, 'f', 1)));
        table->setItem(row, 5, new QTableWidgetItem(
            preview.lost ? u("Lost") : preview.warnings.join(QStringLiteral(" "))));
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    connect(table, &QTableWidget::cellDoubleClicked, this, [this, table](int row, int) {
        const QString uuid = table->item(row, 0)->data(Qt::UserRole).toString();
        m_modelTreeWidget->selectObjectById(uuid, true);
    });
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    for (const QString& warning : result.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] FDS block preview: %1").arg(warning));
    }
    for (const QString& error : result.errors) {
        m_messageWidget->appendMessage(QStringLiteral("[Error] FDS block preview: %1").arg(error));
    }
    dialog.exec();
}

void MainWindow::convertBuildingGeometryToFds()
{
    if (!m_project || !m_project->document()) return;
    QVector<std::shared_ptr<FcGeometryObject>> geometry;
    QVector<std::shared_ptr<FcFdsMesh>> meshes;
    const QStringList selectedObjectIds = m_modelTreeWidget->selectedObjectIds();
    const QSet<QString> selectedIds(selectedObjectIds.cbegin(), selectedObjectIds.cend());
    bool restrictToSelection = false;
    for (const QString& id : selectedIds) {
        const FcObject::Ptr selected = m_project->document()->findObject(id);
        restrictToSelection = restrictToSelection ||
            static_cast<bool>(std::dynamic_pointer_cast<FcGeometryObject>(selected)) ||
            static_cast<bool>(std::dynamic_pointer_cast<FcIfcObject>(selected));
    }
    const std::function<void(const FcObject::Ptr&, bool)> collect =
        [&](const FcObject::Ptr& root, bool selectedAncestor) {
        if (!root) return;
        const bool selected = selectedAncestor || selectedIds.contains(root->id());
        if (const auto item = std::dynamic_pointer_cast<FcGeometryObject>(root)) {
            if (!restrictToSelection || selected) geometry.append(item);
        }
        if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(root)) {
            if ((!restrictToSelection || selected) && ifc->hasShape())
                geometry.append(ifcConversionProxy(ifc));
        }
        if (const auto mesh = meshForGeometryConversion(root)) meshes.append(mesh);
        for (const FcObject::Ptr& child : root->children()) collect(child, selected);
    };
    collect(m_project->document()->geometryGroup(), false);
    collect(m_project->document()->meshesGroup(), false);
    const FdsBlockConversionResult result = FdsBlockConversionService::convert(geometry, meshes);
    if (!result.success()) {
        QMessageBox::critical(this, u("FDS Block Conversion Failed"),
                              result.errors.join(QLatin1Char('\n')));
        return;
    }
    int generatedSequence = nextFdsSequenceIndex();
    for (const FcObject::Ptr& generated : result.fdsObjects) {
        if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(generated)) {
            namelist->setSequenceIndex(generatedSequence++);
        }
    }
    QVector<QPair<std::shared_ptr<FcObjectGroup>, FcObject::Ptr>> additions;
    for (const FcObject::Ptr& object : result.fdsObjects) {
        std::shared_ptr<FcObjectGroup> group;
        if (object->type() == FcObjectType::Vent) group = m_project->document()->ventsGroup();
        else group = m_project->document()->geometryGroup();
        additions.append({group, object});
    }
    QVector<QPair<std::shared_ptr<FcObjectGroup>, FcObject::Ptr>> replaced;
    for (const auto& group : {m_project->document()->geometryGroup(),
                              m_project->document()->ventsGroup()}) {
        if (!group) continue;
        for (const FcObject::Ptr& object : group->children()) {
            if (object && object->tags().contains(QStringLiteral("firecae:auto-converted"))) {
                replaced.append({group, object});
            }
        }
    }
    const auto apply = [this, additions, replaced](bool present) {
        for (const auto& addition : additions) {
            if (present) {
                if (!addition.second->parent()) addition.first->addChild(addition.second);
            } else if (addition.second->parent()) {
                addition.second->parent()->removeChild(addition.second->id());
            }
        }
        for (const auto& previous : replaced) {
            if (present) {
                if (previous.second->parent()) {
                    previous.second->parent()->removeChild(previous.second->id());
                }
            } else if (!previous.second->parent()) {
                previous.first->addChild(previous.second);
            }
        }
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh(); rebuildFdsScene(true); updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Generate FDS Blocks"), [apply]() { apply(true); }, [apply]() { apply(false); }));
    for (const QString& warning : result.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Generated %1 FDS objects from %2 building objects without modifying source geometry.")
            .arg(result.fdsObjects.size()).arg(result.previews.size()));
    statusBar()->showMessage(u("FDS blocks generated."), kStatusMessageDurationMs);
}

void MainWindow::healSelectedGeometry()
{
    if (!m_project || !m_project->document() || !m_modelTreeWidget) return;
    const auto object = std::dynamic_pointer_cast<FcGeometryObject>(
        m_project->document()->findObject(m_modelTreeWidget->selectedObjectId()));
    if (!object || isLockedForModification(object.get())) return;
    QString error;
    const TopoDS_Shape before = object->shape();
    const TopoDS_Shape after = BuildingGeometryService::heal(before, &error);
    if (after.IsNull()) {
        QMessageBox::warning(this, u("Geometry Repair Failed"), error); return;
    }
    const auto apply = [this, object](const TopoDS_Shape& shape) {
        object->setShape(shape);
        if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) manager->displayObject(object);
        m_project->setModified(true); m_currentFdsPath.clear(); updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Heal Geometry"), [=]() { apply(after); }, [=]() { apply(before); }));
}

void MainWindow::performBooleanOperation(int operation)
{
    if (!m_project || !m_project->document() || !m_modelTreeWidget) return;
    QVector<std::shared_ptr<FcGeometryObject>> selected;
    for (const QString& id : m_modelTreeWidget->selectedObjectIds()) {
        if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(
                m_project->document()->findObject(id))) selected.append(geometry);
    }
    if (selected.size() != 2) {
        QMessageBox::information(this, u("Geometry Operation"),
                                 u("Select exactly two geometry objects. The first is the target and the second is the tool."));
        return;
    }
    QString error;
    const FcBooleanOperation booleanOperation = static_cast<FcBooleanOperation>(operation);
    const TopoDS_Shape shape = BuildingGeometryService::booleanOperation(
        selected[0]->shape(), selected[1]->shape(), booleanOperation, &error);
    if (shape.IsNull()) {
        QMessageBox::warning(this, u("Geometry Operation Failed"), error); return;
    }
    const QString operationName = booleanOperation == FcBooleanOperation::Union ? QStringLiteral("Union")
        : booleanOperation == FcBooleanOperation::Intersection ? QStringLiteral("Intersection")
        : booleanOperation == FcBooleanOperation::Split ? QStringLiteral("Split")
        : QStringLiteral("Difference");
    auto result = std::make_shared<FcGeometryObject>(
        QStringLiteral("%1_%2_%3").arg(operationName, selected[0]->name(), selected[1]->name()), shape);
    result->setGeometryKind(FcGeometryKind::Generic);
    result->setTags({QStringLiteral("boolean:%1").arg(operationName.toLower()),
                     QStringLiteral("source-uuid:%1").arg(selected[0]->id()),
                     QStringLiteral("tool-uuid:%1").arg(selected[1]->id())});
    const auto apply = [this, result](bool present) {
        if (present) {
            if (!result->parent()) m_project->document()->geometryGroup()->addChild(result);
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) manager->displayObject(result);
        } else {
            if (result->parent()) result->parent()->removeChild(result->id());
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) manager->removeObject(result->id());
        }
        m_project->setModified(true); m_currentFdsPath.clear();
        m_modelTreeWidget->refresh(); if (present) m_modelTreeWidget->selectObjectById(result->id());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Boolean Geometry Operation"),
        [apply]() { apply(true); }, [apply]() { apply(false); }));
}

void MainWindow::importBackgroundImage()
{
    if (!m_project || !m_project->document()) return;
    const QString filePath = QFileDialog::getOpenFileName(
        this, u("Import Background Image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*.*)"));
    if (filePath.isEmpty()) return;
    QImageReader reader(filePath);
    const QSize imageSize = reader.size();
    const double aspect = imageSize.isValid() && imageSize.width() > 0
                              ? static_cast<double>(imageSize.height()) / imageSize.width()
                              : 0.75;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("BackgroundImageDialog"));
    dialog.setWindowTitle(u("Background Image Placement"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(QFileInfo(filePath).completeBaseName(), &dialog);
    auto* plane = new QComboBox(&dialog);
    plane->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")});
    const QString unit = QStringLiteral(" %1").arg(m_project->displayUnitSymbol());
    auto* x = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* y = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* z = createEngineeringSpinBox(&dialog, -1.0e9, 1.0e9, 0.0, unit);
    auto* width = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(10.0), unit);
    auto* height = createEngineeringSpinBox(
        &dialog, 0.000001, 1.0e9, m_project->metersToDisplay(10.0 * aspect), unit);
    auto* opacity = createEngineeringSpinBox(&dialog, 0.05, 1.0, 0.65);
    opacity->setSingleStep(0.05);
    auto* floor = new QLineEdit(&dialog);
    auto* locked = new QCheckBox(u("Lock background after import"), &dialog);
    locked->setChecked(true);
    auto* embed = new QCheckBox(u("Embed image data in the project"), &dialog);
    embed->setChecked(true);
    name->setObjectName(QStringLiteral("BackgroundNameEdit"));
    plane->setObjectName(QStringLiteral("BackgroundPlaneCombo"));
    width->setObjectName(QStringLiteral("BackgroundWidthSpin"));
    height->setObjectName(QStringLiteral("BackgroundHeightSpin"));
    opacity->setObjectName(QStringLiteral("BackgroundOpacitySpin"));
    form->addRow(u("Name:"), name); form->addRow(u("Plane:"), plane);
    form->addRow(QStringLiteral("X:"), x); form->addRow(QStringLiteral("Y:"), y);
    form->addRow(QStringLiteral("Z:"), z); form->addRow(u("Calibrated Width:"), width);
    form->addRow(u("Calibrated Height:"), height); form->addRow(u("Opacity:"), opacity);
    form->addRow(u("Floor:"), floor); form->addRow(QString(), locked);
    form->addRow(QString(), embed); layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const double startX = m_project->displayToMeters(x->value());
    const double startY = m_project->displayToMeters(y->value());
    const double startZ = m_project->displayToMeters(z->value());
    const double physicalWidth = m_project->displayToMeters(width->value());
    const double physicalHeight = m_project->displayToMeters(height->value());
    constexpr double planeThickness = 1.0e-4;
    TopoDS_Shape shape;
    if (plane->currentText() == QStringLiteral("XY")) {
        shape = BRepPrimAPI_MakeBox(gp_Pnt(startX, startY, startZ),
                                    physicalWidth, physicalHeight, planeThickness).Shape();
    } else if (plane->currentText() == QStringLiteral("XZ")) {
        shape = BRepPrimAPI_MakeBox(gp_Pnt(startX, startY, startZ),
                                    physicalWidth, planeThickness, physicalHeight).Shape();
    } else {
        shape = BRepPrimAPI_MakeBox(gp_Pnt(startX, startY, startZ),
                                    planeThickness, physicalWidth, physicalHeight).Shape();
    }
    auto object = std::make_shared<FcGeometryObject>(
        name->text().trimmed().isEmpty() ? QFileInfo(filePath).completeBaseName()
                                        : name->text().trimmed(),
        shape);
    object->setGeometryKind(FcGeometryKind::BackgroundImage);
    QVariantMap parameters{{QStringLiteral("resourcePath"), QFileInfo(filePath).absoluteFilePath()},
                           {QStringLiteral("extension"), QFileInfo(filePath).suffix().toLower()},
                           {QStringLiteral("plane"), plane->currentText()},
                           {QStringLiteral("x"), startX}, {QStringLiteral("y"), startY},
                           {QStringLiteral("z"), startZ},
                           {QStringLiteral("width"), physicalWidth},
                           {QStringLiteral("height"), physicalHeight},
                           {QStringLiteral("opacity"), opacity->value()}};
    if (embed->isChecked()) {
        QFile imageFile(filePath);
        if (imageFile.open(QIODevice::ReadOnly)) {
            parameters.insert(QStringLiteral("embeddedImageBase64"),
                              QString::fromLatin1(imageFile.readAll().toBase64()));
        }
    }
    object->setGeometryParameters(parameters);
    object->setFloorName(floor->text());
    object->setLocked(locked->isChecked());
    const auto apply = [this, object](bool present) {
        if (present) {
            if (!object->parent()) m_project->document()->geometryGroup()->addChild(object);
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
                manager->displayObject(object);
            }
        } else {
            if (object->parent()) object->parent()->removeChild(object->id());
            if (GeometryDisplayManager* manager = m_occViewWidget->displayManager()) {
                manager->removeObject(object->id());
            }
        }
        m_project->setModified(true); m_modelTreeWidget->refresh();
        if (present) m_modelTreeWidget->selectObjectById(object->id());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Import Background Image"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    m_occViewWidget->fitAll();
}

void MainWindow::importGeometry()
{
    GeometryImportWizard wizard({}, this);
    if (wizard.exec() != QDialog::Accepted) return;
    const QString filePath = wizard.filePath();
    if (GeometryImportService::detectFormat(filePath) == GeometryImportFormat::Ifc) {
        commitIfcImport(wizard.ifcImportResult());
        return;
    }
    commitGeometryImport(wizard.importResult());
}

bool MainWindow::importGeometryFile(const QString& filePath)
{
    if (GeometryImportService::detectFormat(filePath) == GeometryImportFormat::Ifc)
        return importIfcFile(filePath);
    return commitGeometryImport(GeometryImportService().importFile(filePath));
}

bool MainWindow::commitGeometryImport(const GeometryImportResult& result)
{
    if (!m_project || !m_project->document()) return false;
    if (!result.success()) {
        const QString message = result.errorMessage.isEmpty()
                                    ? u("Geometry import failed.") : result.errorMessage;
        m_messageWidget->appendMessage(QStringLiteral("[Error] Geometry import failed: %1")
                                           .arg(message));
        statusBar()->showMessage(u("Geometry import failed."), kStatusMessageDurationMs);
        return false;
    }
    const auto group = m_project->document()->geometryGroup();
    const auto object = result.object;
    if (!group || !object || isLockedForModification(group.get())) return false;
    GeometryDisplayManager* display = m_occViewWidget->displayManager();
    if (!display || !display->displayObject(object)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Imported geometry could not be displayed."));
        return false;
    }
    display->removeObject(object->id());
    const auto apply = [this, group, object](bool present) {
        GeometryDisplayManager* manager = m_occViewWidget->displayManager();
        if (present) {
            if (!object->parent()) group->addChild(object);
            if (manager) manager->displayObject(object);
        } else {
            if (manager) manager->removeObject(object->id());
            if (object->parent()) object->parent()->removeChild(object->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        if (present) {
            m_modelTreeWidget->selectObjectById(object->id());
            m_occViewWidget->fitAll();
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Import Geometry"), [apply]() { apply(true); },
        [apply]() { apply(false); }));
    for (const QString& warning : result.warnings)
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] %1 geometry imported: %2 triangles, %3 faces, %4 ms")
            .arg(GeometryImportService::formatName(result.format))
            .arg(result.quality.triangles)
            .arg(result.quality.faces)
            .arg(result.quality.elapsedMilliseconds));
    statusBar()->showMessage(u("Geometry imported."), kStatusMessageDurationMs);
    return true;
}

void MainWindow::runCurrentProject()
{
    if (!m_project) return;
    const FcScenario* scenario = m_project->activeScenario();
    const FdsRunMode initialMode =
        scenario && scenario->solverBackendId == QStringLiteral("fds.mpi.cpu")
            ? FdsRunMode::Mpi
            : scenario &&
                      scenario->solverBackendId == QStringLiteral("fds.openmp.cpu")
                  ? FdsRunMode::OpenMp
                  : FdsRunMode::Serial;
    runCurrentProjectWithInitialMode(initialMode);
}

void MainWindow::runCurrentProjectWithInitialMode(FdsRunMode initialMode)
{
    if (!m_project) return;
    const ApplicationSettings applicationSettings = ApplicationSettingsStore().load();
    if (applicationSettings.saveBeforeRun && m_project->isModified()) {
        if (!m_currentProjectPath.isEmpty()) {
            if (!saveProjectFile(m_currentProjectPath)) return;
        } else {
            performAutoSave(QStringLiteral("Pre-run recovery snapshot"));
        }
    }
    const FcScenario* scenario = m_project->activeScenario();
    const QString runChid = scenario && !scenario->chid.trimmed().isEmpty()
                                ? scenario->chid : m_project->chid();
    QString path = m_currentFdsPath;
    if (path.isEmpty() && scenario && !scenario->outputDirectory.trimmed().isEmpty()) {
        QString outputDirectory = scenario->outputDirectory;
        if (QDir::isRelativePath(outputDirectory) && !m_currentProjectPath.isEmpty()) {
            outputDirectory = QDir(QFileInfo(m_currentProjectPath).absolutePath())
                                  .filePath(outputDirectory);
        }
        if (QDir().mkpath(outputDirectory)) {
            path = QDir(outputDirectory).filePath(runChid + QStringLiteral(".fds"));
        }
    }
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this,
            u("Save and Run Current Project"),
            runChid + QStringLiteral(".fds"),
            QStringLiteral("FDS Input Files (*.fds);;All Files (*.*)"));
    }
    if (path.isEmpty()) return;
    if (!exportCurrentProjectToFds(path)) {
        QMessageBox::warning(this,
                             u("Model Validation Failed"),
                             u("See the Messages panel for details."));
        return;
    }
    QString executable = FdsRunner::configuredExecutable();
    if (!FdsRunner::validateExecutable(executable).isEmpty())
        executable = FdsRunner::detectExecutable();
    const int initialCount = initialMode == FdsRunMode::Serial
                                 ? 1
                                 : scenario ? qMax(1, scenario->processCount)
                                            : qMax(1, m_projectParallelProcessCount);
    SimulationRunDialog runDialog(path, executable, initialMode,
                                  initialCount, m_project->fdsVersion(), this);
    if (runDialog.exec() != QDialog::Accepted) return;
    const FdsRunRequest request = runDialog.request();
    if (!runFdsFile(path, request.mode, request.processCount,
                    request.threadCount)) {
        QMessageBox::warning(this,
                             u("FDS Could Not Start"),
                             u("See the Messages panel for details."));
    } else if (scenario) {
        FcScenario updatedScenario = *scenario;
        updatedScenario.outputDirectory = QFileInfo(path).absolutePath();
        m_project->updateScenario(updatedScenario);
        updateWindowTitle();
    }
}

void MainWindow::runCurrentProjectOpenMp()
{
    runCurrentProjectWithInitialMode(FdsRunMode::OpenMp);
}

void MainWindow::runCurrentProjectParallel()
{
    if (!m_project) return;
    const FdsModelStatistics statistics = FdsWriter::modelStatistics(*m_project);
    if (statistics.expandedMeshCount < 2) {
        QMessageBox::warning(
            this, u("FDS Parallel Processes"),
            u("Parallel FDS requires at least two expanded meshes in the current project."));
        return;
    }
    runCurrentProjectWithInitialMode(FdsRunMode::Mpi);
}

void MainWindow::chooseAndRunFdsCase()
{
    const QString inputFilePath = QFileDialog::getOpenFileName(
        this,
        u("Run FDS Case"),
        QString(),
        QStringLiteral("FDS Input Files (*.fds);;All Files (*.*)"));
    if (inputFilePath.isEmpty()) {
        return;
    }
    QString executable = FdsRunner::configuredExecutable();
    if (!FdsRunner::validateExecutable(executable).isEmpty())
        executable = FdsRunner::detectExecutable();
    SimulationRunDialog dialog(inputFilePath, executable,
                               FdsRunMode::Serial, 1, QString{}, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FdsRunRequest request = dialog.request();
    if (!runFdsFile(inputFilePath, request.mode, request.processCount,
                    request.threadCount)) {
        QMessageBox::warning(this,
                             u("FDS Could Not Start"),
                             u("See the Messages panel for details."));
    }
}

void MainWindow::chooseAndRunFdsCaseParallel()
{
    const QString inputFilePath = QFileDialog::getOpenFileName(
        this,
        u("Run FDS Parallel"),
        QString(),
        QStringLiteral("FDS Input Files (*.fds);;All Files (*.*)"));
    if (inputFilePath.isEmpty()) {
        return;
    }

    QString executable = FdsRunner::configuredExecutable();
    if (!FdsRunner::validateExecutable(executable).isEmpty())
        executable = FdsRunner::detectExecutable();
    SimulationRunDialog dialog(inputFilePath, executable,
                               FdsRunMode::Mpi,
                               qMax(2, m_projectParallelProcessCount),
                               QString{}, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const FdsRunRequest request = dialog.request();
    m_projectParallelProcessCount = request.processCount;
    if (!runFdsFile(inputFilePath, request.mode, request.processCount,
                    request.threadCount)) {
        QMessageBox::warning(this,
                             u("FDS Could Not Start"),
                             u("See the Messages panel for details."));
    }
}

bool MainWindow::runFdsFile(const QString& inputFilePath,
                            FdsRunMode mode,
                            int processCount,
                            int threadCount)
{
    if (!m_taskManager) {
        return false;
    }

    FdsRunRequest request;
    request.inputFilePath = inputFilePath;
    request.mode = mode;
    request.processCount = processCount;
    request.threadCount = threadCount;
    m_projectSolverExecutable = FdsRunner::configuredExecutable();
    if (mode == FdsRunMode::Mpi) {
        m_projectParallelProcessCount = qMax(2, processCount);
    }
    QString errorMessage;
    const QString taskId = m_taskManager->enqueue(
        request, m_project ? m_project->name() : QFileInfo(inputFilePath).completeBaseName(),
        m_project && m_project->activeScenario()
            ? m_project->activeScenario()->name : QStringLiteral("Default"),
        m_project ? m_project->endTime() : 0.0,
        &errorMessage);
    if (taskId.isEmpty()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] FDS calculation could not start: %1")
                .arg(errorMessage));
        m_messagesDock->show();
        m_messagesDock->raise();
        statusBar()->showMessage(u("FDS calculation could not start."),
                                 kStatusMessageDurationMs);
        return false;
    }
    return true;
}

void MainWindow::stopFdsCase()
{
    if (!m_taskManager || !m_taskManager->hasActiveTasks()) {
        return;
    }
    m_stopAction->setEnabled(false);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Stopping FDS calculation..."));
    statusBar()->showMessage(u("Stopping FDS calculation..."));
    const QString selectedTaskId = m_taskCenterWidget
                                       ? m_taskCenterWidget->selectedTaskId()
                                       : QString{};
    if (!selectedTaskId.isEmpty() && m_taskManager->cancel(selectedTaskId)) return;
    m_taskManager->cancel(m_taskManager->firstActiveTaskId());
}

void MainWindow::handleFdsRunFinished(const FdsRunSummary& summary)
{
    m_runAction->setEnabled(true);
    m_runOpenMpAction->setEnabled(true);
    m_runParallelAction->setEnabled(true);
    m_stopAction->setEnabled(m_taskManager && m_taskManager->hasActiveTasks());

    const double elapsedSeconds =
        static_cast<double>(summary.elapsedMilliseconds) / 1000.0;
    if (!summary.success) {
        const QString level = summary.cancelled
                                  ? QStringLiteral("Info")
                                  : QStringLiteral("Error");
        m_messageWidget->appendMessage(
            QStringLiteral("[%1] FDS calculation ended after %2 s: %3")
                .arg(level)
                .arg(elapsedSeconds, 0, 'f', 1)
                .arg(summary.errorMessage));
        statusBar()->showMessage(
            summary.cancelled ? u("FDS calculation stopped.")
                              : u("FDS calculation failed."),
            kStatusMessageDurationMs);
        return;
    }

    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS calculation completed in %1 s. Results: %2")
            .arg(elapsedSeconds, 0, 'f', 1)
            .arg(QDir::toNativeSeparators(summary.smvFilePath)));
    statusBar()->showMessage(u("FDS calculation completed."),
                             kStatusMessageDurationMs);

    CrashDiagnostics::recordOperation(QStringLiteral("FDS calculation completed"));
    if (!ApplicationSettingsStore().load().autoOpenResults) return;
    if (!openResultFile(summary.smvFilePath)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Warning] The calculation completed, but its results could not be added to the project automatically."));
    } else {
        openSelectedInSmokeview();
    }
}

void MainWindow::chooseAndOpenResults()
{
    const QString smvFilePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open FDS Results"),
        QString(),
        QStringLiteral("Smokeview Results (*.smv);;All Files (*.*)"));
    if (!smvFilePath.isEmpty()) {
        if (openResultFile(smvFilePath)) openSelectedInSmokeview();
    }
}

void MainWindow::compareFdsResults()
{
    const QString referenceSmvFile = QFileDialog::getOpenFileName(
        this,
        u("Select Reference FDS Results"),
        m_projectResultDirectory,
        u("Smokeview Results (*.smv);;All Files (*.*)"));
    if (referenceSmvFile.isEmpty()) {
        return;
    }
    const QString candidateSmvFile = QFileDialog::getOpenFileName(
        this,
        u("Select Candidate FDS Results"),
        QFileInfo(referenceSmvFile).absolutePath(),
        u("Smokeview Results (*.smv);;All Files (*.*)"));
    if (candidateSmvFile.isEmpty()) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const FdsResultComparison comparison = FdsResultComparator::compareSmvFiles(
        referenceSmvFile, candidateSmvFile);
    FdsInputComparison inputComparison;
    const bool hasInputComparison =
        QFileInfo::exists(comparison.referenceProvenance.fdsInputFile) &&
        QFileInfo::exists(comparison.candidateProvenance.fdsInputFile);
    if (hasInputComparison) {
        inputComparison = FdsInputComparator::compareFiles(
            comparison.referenceProvenance.fdsInputFile,
            comparison.candidateProvenance.fdsInputFile);
    }
    QApplication::restoreOverrideCursor();
    if (!comparison.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] FDS result comparison failed: %1")
                .arg(comparison.errorMessage));
        QMessageBox::warning(this, u("FDS Result Comparison"),
                             comparison.errorMessage);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(u("FDS Result Comparison"));
    dialog.resize(1120, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summaryLabel = new QLabel(
        u("%1: %2\nMatched CSV files: %3    Compared quantities: %4    "
          "Relative tolerance: %5%")
            .arg(comparison.passed() ? u("PASS") : u("FAIL"),
                 comparison.referenceCase + QStringLiteral("  /  ") +
                     comparison.candidateCase)
            .arg(comparison.matchedFileCount)
            .arg(static_cast<int>(comparison.quantities.size()))
            .arg(comparison.relativeTolerance * 100.0, 0, 'g', 4),
        &dialog);
    summaryLabel->setObjectName(QStringLiteral("ResultComparisonSummary"));
    layout->addWidget(summaryLabel);

    auto* pathLabel = new QLabel(
        u("Reference: %1\nCandidate: %2")
            .arg(QDir::toNativeSeparators(comparison.referenceSmvFile),
                 QDir::toNativeSeparators(comparison.candidateSmvFile)),
        &dialog);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(pathLabel);

    const auto provenanceText = [](const FdsResultProvenance& provenance) {
        return QStringLiteral("SHA-256=%1; revision=%2; end=%3 s; normal=%4")
            .arg(provenance.fdsInputSha256.isEmpty()
                     ? QStringLiteral("N/A") : provenance.fdsInputSha256,
                 provenance.solverRevision.isEmpty()
                     ? QStringLiteral("N/A") : provenance.solverRevision,
                 provenance.completedTime.isEmpty()
                     ? QStringLiteral("N/A") : provenance.completedTime,
                 provenance.normalTermination ? QStringLiteral("yes")
                                                : QStringLiteral("no"));
    };
    auto* provenanceLabel = new QLabel(
        u("Reference provenance: %1\nCandidate provenance: %2")
            .arg(provenanceText(comparison.referenceProvenance),
                 provenanceText(comparison.candidateProvenance)),
        &dialog);
    provenanceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    provenanceLabel->setWordWrap(true);
    layout->addWidget(provenanceLabel);

    auto* inputSummaryLabel = new QLabel(
        hasInputComparison && inputComparison.success()
            ? u("Input semantics (record order ignored): %1; records %2/%3; "
                "meshes %4/%5; cells %6/%7; review differences %8")
                  .arg(inputComparison.semanticallyEquivalent() ? u("PASS") : u("FAIL"))
                  .arg(inputComparison.referenceRecordCount)
                  .arg(inputComparison.candidateRecordCount)
                  .arg(inputComparison.referenceMeshCount)
                  .arg(inputComparison.candidateMeshCount)
                  .arg(inputComparison.referenceCellCount)
                  .arg(inputComparison.candidateCellCount)
                  .arg(inputComparison.unacceptableDifferenceCount())
            : u("Input semantic comparison is unavailable because one or both FDS input files are missing."),
        &dialog);
    inputSummaryLabel->setObjectName(QStringLiteral("InputComparisonSummary"));
    inputSummaryLabel->setWordWrap(true);
    layout->addWidget(inputSummaryLabel);

    auto* table = new QTableWidget(
        static_cast<int>(comparison.quantities.size()), 10, &dialog);
    table->setObjectName(QStringLiteral("ResultComparisonTable"));
    table->setHorizontalHeaderLabels(
        {u("CSV"), u("Quantity"), u("Samples"), u("Reference Final"),
         u("Candidate Final"), u("Reference Peak"), u("Candidate Peak"),
         u("RMSE"), u("Normalized RMSE"), u("Status")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    const auto numeric = [](double value) {
        return QString::number(value, 'g', 8);
    };
    int row = 0;
    for (const FdsCsvQuantityComparison& quantity : comparison.quantities) {
        const QStringList values = {
            quantity.referenceFile,
            quantity.quantity,
            QString::number(quantity.sampleCount),
            numeric(quantity.referenceFinal),
            numeric(quantity.candidateFinal),
            numeric(quantity.referencePeak),
            numeric(quantity.candidatePeak),
            numeric(quantity.rootMeanSquareError),
            numeric(quantity.normalizedRootMeanSquareError),
            quantity.withinTolerance ? u("PASS") : u("FAIL")};
        for (int column = 0; column < values.size(); ++column) {
            table->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        ++row;
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    QStringList notices;
    if (!comparison.missingCandidateFiles.isEmpty()) {
        notices.append(u("Missing candidate CSV files: %1")
                           .arg(comparison.missingCandidateFiles.join(
                               QStringLiteral(", "))));
    }
    if (!comparison.missingCandidateQuantities.isEmpty()) {
        notices.append(u("Missing candidate quantities: %1")
                           .arg(comparison.missingCandidateQuantities.join(
                               QStringLiteral(", "))));
    }
    if (!comparison.candidateOnlyFiles.isEmpty()) {
        notices.append(u("Candidate-only CSV files: %1")
                           .arg(comparison.candidateOnlyFiles.join(
                               QStringLiteral(", "))));
    }
    notices.append(comparison.warnings);
    if (!notices.isEmpty()) {
        auto* noticeLabel = new QLabel(notices.join(QLatin1Char('\n')), &dialog);
        noticeLabel->setWordWrap(true);
        noticeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(noticeLabel);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* saveButton =
        buttons->addButton(u("Save HTML/PDF/CSV Report..."), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, &dialog,
            [this, &comparison, &inputComparison, hasInputComparison]() {
        const QString reportDirectory = QFileDialog::getExistingDirectory(
            this, u("Choose Comparison Report Directory"),
            QFileInfo(comparison.candidateSmvFile).absolutePath());
        if (reportDirectory.isEmpty()) return;
        FdsComparisonReportOptions options;
        options.outputDirectory = reportDirectory;
        options.baseFileName = QStringLiteral("%1-vs-%2-comparison")
                                   .arg(comparison.referenceCase,
                                        comparison.candidateCase);
        options.title = QStringLiteral("FireCAE / FDS comparison — %1 vs %2")
                            .arg(comparison.referenceCase,
                                 comparison.candidateCase);
        options.fireCaeVersion = QCoreApplication::applicationVersion();
        options.fdsVersion = comparison.referenceProvenance.solverRevision ==
                                     comparison.candidateProvenance.solverRevision
                                 ? comparison.referenceProvenance.solverRevision
                                 : QStringLiteral("reference=%1; candidate=%2")
                                       .arg(comparison.referenceProvenance.solverRevision,
                                            comparison.candidateProvenance.solverRevision);
        options.runTimeSeconds = QStringLiteral("reference=%1; candidate=%2")
                                     .arg(comparison.referenceProvenance.completedTime,
                                          comparison.candidateProvenance.completedTime);
        options.reproduciblePaths = {
            comparison.referenceSmvFile,
            comparison.candidateSmvFile,
            comparison.referenceProvenance.fdsInputFile,
            comparison.candidateProvenance.fdsInputFile};
        const QVector<FdsUuidIdMapping> mappings = m_project
            ? FdsInputComparator::uuidToFdsIdMappings(*m_project)
            : QVector<FdsUuidIdMapping>{};
        const FdsComparisonReportArtifacts artifacts = FdsComparisonReport::write(
            hasInputComparison && inputComparison.success() ? &inputComparison : nullptr,
            comparison, mappings, options);
        if (!artifacts.success()) {
            QMessageBox::warning(this, u("Save Result Comparison"),
                                 artifacts.errorMessage);
            return;
        }
        QMessageBox::information(
            this, u("Save Result Comparison"),
            u("Comparison report written:\n%1\n%2\n%3")
                .arg(QDir::toNativeSeparators(artifacts.htmlFile),
                     QDir::toNativeSeparators(artifacts.pdfFile),
                     QDir::toNativeSeparators(artifacts.csvFile)));
    });
    layout->addWidget(buttons);

    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS result comparison %1: %2 vs %3 (%4 quantities)")
            .arg(comparison.passed() ? QStringLiteral("passed")
                                     : QStringLiteral("failed"),
                 comparison.referenceCase,
                 comparison.candidateCase)
            .arg(static_cast<int>(comparison.quantities.size())));
    dialog.exec();
}

bool MainWindow::openResultFile(const QString& smvFilePath)
{
    if (!m_project || !m_project->document()) {
        return false;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const FdsResultScanResult scan = FdsResultScanner().scanSmvFile(smvFilePath);
    QApplication::restoreOverrideCursor();
    if (!scan.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] FDS results could not be opened: %1")
                .arg(scan.errorMessage));
        statusBar()->showMessage(QStringLiteral("Failed to open FDS results."),
                                 kStatusMessageDurationMs);
        return false;
    }

    m_projectResultDirectory = scan.resultDirectory;

    auto resultCase = std::make_shared<FcResultCase>(scan.caseName);
    replaceResultCaseContents(*resultCase, scan);
    if (!m_project->document()->resultsGroup()->addChild(resultCase)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Result case could not be added to the project."));
        return false;
    }

    m_project->setModified(true);
    m_modelTreeWidget->refresh();
    m_modelTreeWidget->selectObjectById(resultCase->id());
    m_propertiesWidget->showObject(resultCase.get());
    m_deleteAction->setEnabled(true);
    m_reloadResultsAction->setEnabled(true);
    m_closeResultsAction->setEnabled(true);
    m_openSmokeviewAction->setEnabled(true);
    m_openNativeResultsAction->setEnabled(true);
    m_openSmokeAnimationAction->setEnabled(true);
    m_openSliceAnimationAction->setEnabled(true);
    m_openParticleAnimationAction->setEnabled(true);
    for (const QString& warning : scan.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS results opened: %1 (%2 files, status: %3)")
            .arg(scan.caseName)
            .arg(static_cast<int>(scan.files.size()))
            .arg(resultStatusName(scan.status)));
    statusBar()->showMessage(QStringLiteral("Opened FDS results: %1").arg(scan.caseName),
                             kStatusMessageDurationMs);
    refreshTutorialContext();
    return true;
}

bool MainWindow::reloadResultCase(const QString& objectId)
{
    if (!m_project || !m_project->document()) {
        return false;
    }
    const FcObject::Ptr selected = m_project->document()->findObject(objectId);
    const std::shared_ptr<FcResultCase> resultCase = resultCaseForObject(selected.get());
    if (!resultCase || isLockedForModification(resultCase.get())) {
        if (resultCase) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return false;
    }

    const FdsResultScanResult scan = FdsResultScanner().scanSmvFile(resultCase->smvFilePath());
    if (!scan.success()) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Results reload failed: %1").arg(scan.errorMessage));
        return false;
    }
    const QString caseId = resultCase->id();
    replaceResultCaseContents(*resultCase, scan);
    m_project->setModified(true);
    m_modelTreeWidget->refresh();
    m_modelTreeWidget->selectObjectById(caseId);
    m_propertiesWidget->showObject(resultCase.get());
    for (const QString& warning : scan.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS results reloaded: %1").arg(resultCase->name()));
    statusBar()->showMessage(QStringLiteral("Results reloaded."),
                             kStatusMessageDurationMs);
    return true;
}

bool MainWindow::closeResultCase(const QString& objectId)
{
    if (!m_project || !m_project->document()) {
        return false;
    }
    const FcObject::Ptr selected = m_project->document()->findObject(objectId);
    const std::shared_ptr<FcResultCase> resultCase = resultCaseForObject(selected.get());
    if (!resultCase || !resultCase->parent() ||
        isLockedForModification(resultCase.get())) {
        if (resultCase && isLockedForModification(resultCase.get())) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return false;
    }
    const QString caseName = resultCase->name();
    if (m_nativeResultViewerWidget && m_nativeResultViewerWidget->hasOpenCase()) {
        m_nativeResultViewerWidget->closeCase();
        if (m_workspaceTabs &&
            m_workspaceTabs->currentWidget() == m_nativeResultViewerWidget) {
            showModelWorkspace(0);
        }
    }
    if (m_smokeviewHostWidget && m_smokeviewHostWidget->processId() != 0) {
        m_smokeviewHostWidget->closeViewer();
        showModelWorkspace(0);
    }
    FcObject* parent = resultCase->parent();
    const auto& siblings = parent->children();
    const auto position = std::find_if(
        siblings.cbegin(), siblings.cend(), [&resultCase](const FcObject::Ptr& child) {
            return child && child->id() == resultCase->id();
        });
    if (position == siblings.cend()) return false;
    const std::size_t originalIndex = static_cast<std::size_t>(
        std::distance(siblings.cbegin(), position));
    const auto applyPresence = [this, parent, resultCase, originalIndex](bool present) {
        if (present) {
            if (!resultCase->parent()) parent->insertChild(resultCase, originalIndex);
        } else if (resultCase->parent()) {
            resultCase->parent()->removeChild(resultCase->id());
        }
        m_modelTreeWidget->clearSelection();
        m_propertiesWidget->clear();
        m_project->setModified(true);
        m_modelTreeWidget->refresh();
        m_deleteAction->setEnabled(false);
        m_reloadResultsAction->setEnabled(false);
        m_closeResultsAction->setEnabled(false);
        m_openSmokeviewAction->setEnabled(false);
        m_openNativeResultsAction->setEnabled(false);
        m_openSmokeAnimationAction->setEnabled(false);
        m_openSliceAnimationAction->setEnabled(false);
        m_openParticleAnimationAction->setEnabled(false);
        if (present) {
            m_modelTreeWidget->selectObjectById(resultCase->id());
            m_propertiesWidget->showObject(resultCase.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Close Results"),
        [applyPresence]() { applyPresence(false); },
        [applyPresence]() { applyPresence(true); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] FDS results closed: %1").arg(caseName));
    statusBar()->showMessage(QStringLiteral("Closed FDS results: %1").arg(caseName),
                             kStatusMessageDurationMs);
    return true;
}

bool MainWindow::openResultInNativeViewer(const QString& objectId)
{
    if (!m_project || !m_project->document() || !m_nativeResultViewerWidget)
        return false;
    const FcObject::Ptr selected = m_project->document()->findObject(objectId);
    const std::shared_ptr<FcResultCase> resultCase = resultCaseForObject(selected.get());
    if (!resultCase) return false;
    QString errorMessage;
    if (!m_nativeResultViewerWidget->openCase(resultCase->smvFilePath(), &errorMessage)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Native result viewer could not open the case: %1")
                .arg(errorMessage));
        statusBar()->showMessage(u("Native result viewer could not open the case."),
                                 kStatusMessageDurationMs);
        return false;
    }
    showModelWorkspace(3);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Native result viewer opened: %1")
            .arg(resultCase->name()));
    statusBar()->showMessage(u("Native result viewer opened."),
                             kStatusMessageDurationMs);
    return true;
}

bool MainWindow::launchResultInSmokeview(const QString& objectId)
{
    if (!m_project || !m_project->document()) {
        return false;
    }
    const FcObject::Ptr selected = m_project->document()->findObject(objectId);
    const std::shared_ptr<FcResultCase> resultCase = resultCaseForObject(selected.get());
    if (!resultCase) {
        return false;
    }
    const SmokeviewLaunchResult launch = SmokeviewLauncher::launch(resultCase->smvFilePath());
    if (!launch.success) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Smokeview launch failed: %1").arg(launch.errorMessage));
        statusBar()->showMessage(QStringLiteral("Smokeview launch failed."),
                                 kStatusMessageDurationMs);
        return false;
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Smokeview started for %1 (PID %2)")
            .arg(resultCase->name())
            .arg(launch.processId));
    statusBar()->showMessage(QStringLiteral("Smokeview started."),
                             kStatusMessageDurationMs);
    return true;
}

bool MainWindow::launchResultAnimation(const QString& objectId,
                                       SmokeviewLaunchMode mode)
{
    if (!m_project || !m_project->document()) {
        return false;
    }
    const FcObject::Ptr selected = m_project->document()->findObject(objectId);
    const std::shared_ptr<FcResultCase> resultCase = resultCaseForObject(selected.get());
    if (!resultCase) {
        return false;
    }

    m_centralStack->setCurrentWidget(m_smokeviewHostWidget);
    const SmokeviewLaunchResult launch =
        m_smokeviewHostWidget->openCase(resultCase->smvFilePath(), mode);
    if (!launch.success) {
        showModelWorkspace(0);
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] Smokeview animation launch failed: %1")
                .arg(launch.errorMessage));
        statusBar()->showMessage(u("Smokeview animation launch failed."),
                                 kStatusMessageDurationMs);
        return false;
    }
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Smokeview animation started for embedding: %1 (PID %2)")
            .arg(resultCase->name())
            .arg(launch.processId));
    statusBar()->showMessage(u("Smokeview animation is opening in the result workspace."),
                             kStatusMessageDurationMs);
    return true;
}

void MainWindow::reloadSelectedResults()
{
    reloadResultCase(m_modelTreeWidget->selectedObjectId());
}

void MainWindow::closeSelectedResults()
{
    closeResultCase(m_modelTreeWidget->selectedObjectId());
}

void MainWindow::openSelectedInSmokeview()
{
    const QString objectId = m_modelTreeWidget->selectedObjectId();
    if (launchResultAnimation(objectId, SmokeviewLaunchMode::Standard)) {
        return;
    }
    if (SmokeviewLauncher::detectExecutable().isEmpty()) {
        const QMessageBox::StandardButton configure = QMessageBox::question(
            this,
            QStringLiteral("Smokeview Not Configured"),
            QStringLiteral("Smokeview could not be found. Locate smokeview.exe now?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (configure == QMessageBox::Yes) {
            configureSmokeview();
            launchResultAnimation(objectId, SmokeviewLaunchMode::Standard);
        }
    }
}

void MainWindow::openSelectedInNativeViewer()
{
    openResultInNativeViewer(m_modelTreeWidget->selectedObjectId());
}

void MainWindow::openSelectedSmokeAnimation()
{
    launchResultAnimation(m_modelTreeWidget->selectedObjectId(),
                          SmokeviewLaunchMode::SmokeAndFire);
}

void MainWindow::openSelectedSliceAnimation()
{
    launchResultAnimation(m_modelTreeWidget->selectedObjectId(),
                          SmokeviewLaunchMode::Slice);
}

void MainWindow::openSelectedParticleAnimation()
{
    launchResultAnimation(m_modelTreeWidget->selectedObjectId(),
                          SmokeviewLaunchMode::Particles);
}

void MainWindow::configureSmokeview()
{
    const QString executablePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Locate Smokeview"),
        SmokeviewLauncher::configuredExecutable(),
        QStringLiteral("Smokeview (smokeview.exe);;Executables (*.exe)"));
    if (executablePath.isEmpty()) {
        return;
    }
    const QString error = SmokeviewLauncher::validateExecutable(executablePath);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Smokeview"), error);
        return;
    }
    SmokeviewLauncher::setConfiguredExecutable(executablePath);
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Smokeview configured: %1")
            .arg(QDir::toNativeSeparators(executablePath)));
    statusBar()->showMessage(QStringLiteral("Smokeview configured."),
                             kStatusMessageDurationMs);
}

bool MainWindow::importIfcFile(const QString& filePath)
{
    statusBar()->showMessage(QStringLiteral("Importing IFC model..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const IfcImportResult result = IfcImportService().importFile(filePath);
    QApplication::restoreOverrideCursor();
    return commitIfcImport(result);
}

bool MainWindow::commitIfcImport(const IfcImportResult& result)
{
    if (!m_project || !m_project->document()) {
        m_messageWidget->appendMessage(QStringLiteral("[Error] No active project."));
        return false;
    }

    if (!result.success()) {
        const QString message = result.errorMessage.isEmpty()
                                    ? QStringLiteral("Unknown IFC import error.")
                                    : result.errorMessage;
        m_messageWidget->appendMessage(QStringLiteral("[Error] IFC import failed: %1")
                                           .arg(message));
        statusBar()->showMessage(QStringLiteral("IFC import failed."),
                                 kStatusMessageDurationMs);
        QMessageBox::critical(this, QStringLiteral("IFC Import Failed"), message);
        return false;
    }

    const std::shared_ptr<FcIfcObject>& rootObject = result.rootObject;
    const auto geometryGroup = m_project->document()->geometryGroup();
    if (!geometryGroup || isLockedForModification(geometryGroup.get())) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] IFC model could not be added to the project."));
        statusBar()->showMessage(QStringLiteral("IFC import failed."),
                                 kStatusMessageDurationMs);
        return false;
    }

    GeometryDisplayManager* displayManager = m_occViewWidget->displayManager();
    if (!displayManager ||
        !displayManager->displayIfcModel(rootObject)) {
        m_messageWidget->appendMessage(
            QStringLiteral("[Error] IFC geometry could not be displayed."));
        statusBar()->showMessage(QStringLiteral("IFC import failed."),
                                 kStatusMessageDurationMs);
        return false;
    }
    displayManager->removeIfcModel(rootObject);
    const auto applyImport = [this, geometryGroup, rootObject](bool present) {
        GeometryDisplayManager* manager = m_occViewWidget->displayManager();
        if (present) {
            if (!rootObject->parent()) geometryGroup->addChild(rootObject);
            if (manager) {
                manager->displayIfcModel(rootObject);
                updateDisplayVisibilityRecursive(rootObject, manager);
            }
        } else {
            if (manager) manager->removeIfcModel(rootObject);
            if (rootObject->parent()) rootObject->parent()->removeChild(rootObject->id());
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        if (present) {
            m_modelTreeWidget->selectObjectById(rootObject->id());
            m_occViewWidget->fitAll();
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Import IFC"),
        [applyImport]() { applyImport(true); },
        [applyImport]() { applyImport(false); }));
    for (const QString& warning : result.warnings) {
        m_messageWidget->appendMessage(QStringLiteral("[Warning] %1").arg(warning));
    }

    const int objectCount = descendantCount(*rootObject);
    const QString fileName = QFileInfo(rootObject->sourceFile()).fileName();
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] IFC imported: %1 (%2 semantic objects, %3 geometry objects)")
            .arg(fileName)
            .arg(objectCount)
            .arg(result.geometryObjectCount));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] IFC import report: schema=%1, unit=%2, "
                       "storeys=%3, spaces=%4, materials=%5, property sets=%6, "
                       "filtered=%7, failed=%8.")
            .arg(result.preflight.schema, result.preflight.lengthUnit)
            .arg(result.preflight.storeyCount)
            .arg(result.preflight.spaceCount)
            .arg(result.preflight.materialRelationshipCount)
            .arg(result.preflight.propertySetCount)
            .arg(result.filteredObjectCount)
            .arg(result.failedComponents.size()));
    statusBar()->showMessage(
        QStringLiteral("Imported %1").arg(fileName), kStatusMessageDurationMs);
    return true;
}

void MainWindow::deleteSelectedObject()
{
    if (!m_modelTreeWidget || !m_project || !m_project->document()) {
        return;
    }
    const QStringList selectedIds = m_modelTreeWidget->selectedObjectIds();
    QVector<std::shared_ptr<FcGeometryObject>> geometryObjects;
    QVector<FcObject*> parents;
    QVector<std::size_t> indices;
    bool onlyGeometry = !selectedIds.isEmpty();
    for (const QString& id : selectedIds) {
        const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (!geometry || isLockedForModification(geometry.get()) || !geometry->parent()) {
            onlyGeometry = false;
            break;
        }
        FcObject* parent = geometry->parent();
        const auto& children = parent->children();
        const auto position = std::find_if(children.cbegin(), children.cend(),
            [&id](const FcObject::Ptr& child) { return child && child->id() == id; });
        if (position == children.cend()) { onlyGeometry = false; break; }
        geometryObjects.append(geometry); parents.append(parent);
        indices.append(static_cast<std::size_t>(std::distance(children.cbegin(), position)));
    }
    if (onlyGeometry) {
        if (!checkRemovalReferences(selectedIds, true)) return;
        if (QMessageBox::question(
                this, u("Delete Geometry"),
                QStringLiteral("Delete %1 selected geometry object(s)?")
                    .arg(geometryObjects.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QStringList ids;
        for (const auto& object : geometryObjects) ids.append(object->id());
        const auto applyDelete = [this, geometryObjects, parents, indices, ids](bool deleted) {
            if (deleted) {
                for (const auto& object : geometryObjects) {
                    if (object->parent()) object->parent()->removeChild(object->id());
                }
            } else {
                // Selection order is not sibling order. Restore each parent's
                // children in ascending original index so unselected siblings
                // retain their positions after a compound Undo.
                QVector<qsizetype> restoreOrder;
                for (qsizetype index = 0; index < geometryObjects.size(); ++index) {
                    restoreOrder.append(index);
                }
                std::stable_sort(restoreOrder.begin(), restoreOrder.end(),
                    [&indices](qsizetype left, qsizetype right) {
                        return indices[left] < indices[right];
                    });
                for (const qsizetype index : restoreOrder) {
                    if (!geometryObjects[index]->parent()) {
                        parents[index]->insertChild(geometryObjects[index], indices[index]);
                    }
                }
            }
            m_project->setModified(true);
            m_currentFdsPath.clear();
            m_modelTreeWidget->clearSelection();
            m_propertiesWidget->clear();
            m_modelTreeWidget->refresh();
            rebuildFdsScene(false);
            updateMultiSelection(deleted ? QStringList{} : ids, true, true);
            updateWindowTitle();
        };
        m_undoStack->push(new FunctionalUndoCommand(
            u("Delete Geometry"), [applyDelete]() { applyDelete(true); },
            [applyDelete]() { applyDelete(false); }));
        return;
    }
    if (selectedIds.size() > 1) {
        statusBar()->showMessage(
            u("Multiple deletion is available for geometry objects only."),
            kStatusMessageDurationMs);
        return;
    }
    const QString objectId = m_modelTreeWidget->selectedObjectId();
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (object && object->type() == FcObjectType::ResultCase) {
        closeResultCase(objectId);
    } else {
        removeObject(objectId);
    }
}

void MainWindow::duplicateSelectedObject()
{
    if (m_modelTreeWidget) {
        duplicateObject(m_modelTreeWidget->selectedObjectId());
    }
}

void MainWindow::moveSelectedObjectUp()
{
    if (m_modelTreeWidget) {
        moveObject(m_modelTreeWidget->selectedObjectId(), -1);
    }
}

void MainWindow::moveSelectedObjectDown()
{
    if (m_modelTreeWidget) {
        moveObject(m_modelTreeWidget->selectedObjectId(), 1);
    }
}

bool MainWindow::duplicateObject(const QString& objectId)
{
    if (!m_project || !m_project->document() || objectId.isEmpty()) return false;
    const auto source = std::dynamic_pointer_cast<FcFdsNamelist>(
        m_project->document()->findObject(objectId));
    if (!source || !source->parent() || isLockedForModification(source.get())) {
        if (source && isLockedForModification(source.get())) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return false;
    }

    FcObject* parent = source->parent();
    const auto& siblings = parent->children();
    const auto position = std::find_if(
        siblings.cbegin(), siblings.cend(), [&objectId](const FcObject::Ptr& child) {
            return child && child->id() == objectId;
        });
    if (position == siblings.cend()) return false;
    const std::size_t insertIndex =
        static_cast<std::size_t>(std::distance(siblings.cbegin(), position)) + 1;

    const QString copyFdsId = uniqueCopyFdsId(
        *m_project->document(), source->keyword(), source->fdsId());
    auto copy = std::make_shared<FcFdsNamelist>(
        source->name() + QStringLiteral(" Copy"), source->type(), source->keyword(),
        copyFdsId, source->sequenceIndex() + 1);
    copy->setParameters(source->parameters());
    copy->setVisible(source->isVisible());

    const int sourceSequence = source->sequenceIndex();
    const auto applyCopy = [this, parent, insertIndex, copy, sourceSequence](bool present) {
        if (present && !copy->parent()) {
            for (const auto& group : m_project->document()->groups()) {
                shiftSequenceAfter(group, sourceSequence, 1);
            }
            parent->insertChild(copy, insertIndex);
        } else if (!present && copy->parent()) {
            copy->parent()->removeChild(copy->id());
            for (const auto& group : m_project->document()->groups()) {
                shiftSequenceAfter(group, sourceSequence, -1);
            }
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? copy->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(copy->id());
            m_propertiesWidget->showObject(copy.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        u("Duplicate Object"),
        [applyCopy]() { applyCopy(true); },
        [applyCopy]() { applyCopy(false); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Object duplicated with new UUID: %1 (%2)")
            .arg(copy->name(), copy->id()));
    statusBar()->showMessage(u("Object duplicated."), kStatusMessageDurationMs);
    return true;
}

bool MainWindow::moveObject(const QString& objectId, int offset)
{
    if (!m_project || !m_project->document() || objectId.isEmpty() ||
        (offset != -1 && offset != 1)) {
        return false;
    }
    const auto object = std::dynamic_pointer_cast<FcFdsNamelist>(
        m_project->document()->findObject(objectId));
    if (!object || !object->parent() || isLockedForModification(object.get())) {
        if (object && isLockedForModification(object.get())) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return false;
    }
    FcObject* parent = object->parent();
    const auto& siblings = parent->children();
    const auto position = std::find_if(
        siblings.cbegin(), siblings.cend(), [&objectId](const FcObject::Ptr& child) {
            return child && child->id() == objectId;
        });
    if (position == siblings.cend()) return false;
    const int currentIndex = static_cast<int>(std::distance(siblings.cbegin(), position));
    const int targetIndex = currentIndex + offset;
    if (targetIndex < 0 || targetIndex >= static_cast<int>(siblings.size())) return false;
    const auto adjacent = std::dynamic_pointer_cast<FcFdsNamelist>(
        siblings[static_cast<std::size_t>(targetIndex)]);
    const int objectSequence = object->sequenceIndex();
    const int adjacentSequence = adjacent ? adjacent->sequenceIndex() : 0;
    const auto applyMove = [this, parent, object, adjacent, objectId,
                            currentIndex, targetIndex, objectSequence,
                            adjacentSequence](bool forward) {
        const int destination = forward ? targetIndex : currentIndex;
        if (!parent->moveChild(objectId, static_cast<std::size_t>(destination))) return;
        if (adjacent) {
            object->setSequenceIndex(forward ? adjacentSequence : objectSequence);
            adjacent->setSequenceIndex(forward ? objectSequence : adjacentSequence);
        }
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, objectId);
        m_modelTreeWidget->selectObjectById(objectId);
        m_propertiesWidget->showObject(object.get());
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        offset < 0 ? u("Move Object Up") : u("Move Object Down"),
        [applyMove]() { applyMove(true); },
        [applyMove]() { applyMove(false); }));
    statusBar()->showMessage(
        offset < 0 ? u("Object moved up.") : u("Object moved down."),
        kStatusMessageDurationMs);
    return true;
}

bool MainWindow::removeIfcObject(const QString& objectId, bool requestConfirmation)
{
    if (!m_project || !m_project->document() || objectId.isEmpty()) {
        return false;
    }
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (!object || object->type() != FcObjectType::IfcModel) {
        return false;
    }
    return removeObject(objectId, requestConfirmation);
}

bool MainWindow::checkRemovalReferences(const QStringList& objectIds,
                                        bool requestConfirmation)
{
    if (!m_project || !m_project->document()) return false;
    const QSet<QString> removalIds(objectIds.cbegin(), objectIds.cend());
    for (const QString& objectId : objectIds) {
        const FcObject::Ptr object = m_project->document()->findObject(objectId);
        if (!object) continue;
        QList<ObjectReferenceOwner> referenceOwners;
        for (const ObjectReferenceOwner& owner :
             collectObjectReferenceOwners(*m_project->document(), objectId)) {
            // References whose owners are being removed in the same atomic
            // action cannot become dangling. Every surviving owner blocks it.
            if (!removalIds.contains(owner.objectUuid)) referenceOwners.append(owner);
        }
        if (referenceOwners.isEmpty()) continue;
        m_messageWidget->appendMessage(
            u("Cannot delete \"%1\": it is used by %2 other object(s).")
                .arg(object->name()).arg(referenceOwners.size()));
        statusBar()->showMessage(u("Referenced objects cannot be deleted."),
                                 kStatusMessageDurationMs);
        if (requestConfirmation) {
            ObjectReferenceDialog dialog(*object, referenceOwners, this);
            if (dialog.exec() == QDialog::Accepted) {
                const QString ownerUuid = dialog.selectedOwnerUuid();
                const FcObject::Ptr owner = m_project->document()->findObject(ownerUuid);
                if (owner) {
                    // A filter must not leave the located row invisible. Clearing
                    // view filters changes neither the model nor object visibility.
                    if (auto* search = m_modelTreeWidget->findChild<QLineEdit*>(
                            QStringLiteral("ModelTreeSearchEdit"))) search->clear();
                    for (const QString& name : {QStringLiteral("ModelTreeTypeFilter"),
                                                QStringLiteral("ModelTreeFloorFilter")}) {
                        if (auto* filter = m_modelTreeWidget->findChild<QComboBox*>(name)) {
                            filter->setCurrentIndex(0);
                        }
                    }
                    if (m_modelTreeWidget->selectObjectById(ownerUuid, true)) {
                        m_propertiesWidget->showObject(owner.get());
                        statusBar()->showMessage(
                            u("Located \"%1\". Edit its reference before deleting the original object.")
                                .arg(owner->name()), kStatusMessageDurationMs);
                    }
                }
            }
        }
        return false;
    }
    return true;
}

bool MainWindow::removeObject(const QString& objectId, bool requestConfirmation)
{
    if (!m_project || !m_project->document() || objectId.isEmpty()) return false;
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (!object || object->type() == FcObjectType::Group || !object->parent() ||
        isLockedForModification(object.get())) {
        if (object && isLockedForModification(object.get())) {
            statusBar()->showMessage(u("Locked objects cannot be modified."),
                                     kStatusMessageDurationMs);
        }
        return false;
    }

    if (!checkRemovalReferences({objectId}, requestConfirmation)) return false;

    if (requestConfirmation) {
        const bool isIfc = object->type() == FcObjectType::IfcModel;
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            isIfc ? u("Delete Imported IFC") : u("Delete Object"),
            isIfc
                ? QStringLiteral("Remove imported IFC model \"%1\" from this project?\n\n"
                                 "The original IFC file will not be deleted.")
                      .arg(object->name())
                : u("Remove the selected object from this project?") +
                      QStringLiteral("\n\n") + object->name(),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }

    FcObject* parent = object->parent();
    const auto& siblings = parent->children();
    const auto position = std::find_if(
        siblings.cbegin(), siblings.cend(), [&objectId](const FcObject::Ptr& child) {
            return child && child->id() == objectId;
        });
    if (position == siblings.cend()) return false;
    const std::size_t originalIndex = static_cast<std::size_t>(
        std::distance(siblings.cbegin(), position));
    const QString objectName = object->name();
    const auto applyPresence = [this, object, parent, originalIndex](bool present) {
        GeometryDisplayManager* displayManager = m_occViewWidget->displayManager();
        if (present) {
            if (!object->parent()) parent->insertChild(object, originalIndex);
            restoreGeometryPresentationsRecursive(object, displayManager);
            updateDisplayVisibilityRecursive(object, displayManager);
        } else {
            if (displayManager) displayManager->clearSelection();
            removePresentationsRecursive(object, displayManager);
            if (object->parent()) object->parent()->removeChild(object->id());
        }
        m_modelTreeWidget->clearSelection();
        m_propertiesWidget->clear();
        m_deleteAction->setEnabled(false);
        m_editObjectAction->setEnabled(false);
        m_duplicateAction->setEnabled(false);
        m_moveUpAction->setEnabled(false);
        m_moveDownAction->setEnabled(false);
        m_project->setModified(true);
        m_currentFdsPath.clear();
        m_modelTreeWidget->refresh();
        rebuildFdsScene(false, present ? object->id() : QString{});
        if (present) {
            m_modelTreeWidget->selectObjectById(object->id());
            m_propertiesWidget->showObject(object.get());
        }
        updateWindowTitle();
    };
    m_undoStack->push(new FunctionalUndoCommand(
        object->type() == FcObjectType::IfcModel
            ? u("Delete Imported IFC") : u("Delete Object"),
        [applyPresence]() { applyPresence(false); },
        [applyPresence]() { applyPresence(true); }));
    m_messageWidget->appendMessage(
        QStringLiteral("[Info] Object removed from project: %1").arg(objectName));
    statusBar()->showMessage(
        QStringLiteral("Removed %1").arg(objectName), kStatusMessageDurationMs);
    return true;
}

void MainWindow::updateWindowTitle()
{
    QString applicationTitle = QStringLiteral("FireCAE");
#ifdef FIRECAE_BUILD_LABEL
    applicationTitle += QStringLiteral(" [%1]").arg(QStringLiteral(FIRECAE_BUILD_LABEL));
#endif
    if (!m_project) {
        setWindowTitle(applicationTitle);
        return;
    }
    setWindowTitle(QStringLiteral("%1 - %2%3")
                       .arg(applicationTitle, m_project->name(),
                            m_project->isModified() ? QStringLiteral(" *") : QString()));
}

void MainWindow::resetDefaultLayout()
{
    const unsigned int layoutRevision = ++m_layoutRevision;
    const QList<QDockWidget*> docks = {
        m_modelTreeDock, m_propertiesDock, m_inspectorDock,
        m_messagesDock, m_taskCenterDock, m_fdsOutputDock, m_resultStatusDock,
        m_tutorialGuideDock};
    for (QDockWidget* dock : docks) {
        dock->setFloating(false);
    }

    removeDockWidget(m_modelTreeDock);
    removeDockWidget(m_propertiesDock);
    removeDockWidget(m_messagesDock);
    removeDockWidget(m_taskCenterDock);
    removeDockWidget(m_inspectorDock);
    removeDockWidget(m_fdsOutputDock);
    removeDockWidget(m_resultStatusDock);
    removeDockWidget(m_tutorialGuideDock);

    addDockWidget(Qt::LeftDockWidgetArea, m_modelTreeDock);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    addDockWidget(Qt::RightDockWidgetArea, m_tutorialGuideDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_messagesDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_taskCenterDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_fdsOutputDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_resultStatusDock);
    tabifyDockWidget(m_propertiesDock, m_inspectorDock);
    tabifyDockWidget(m_inspectorDock, m_tutorialGuideDock);
    tabifyDockWidget(m_messagesDock, m_taskCenterDock);
    tabifyDockWidget(m_messagesDock, m_fdsOutputDock);
    tabifyDockWidget(m_messagesDock, m_resultStatusDock);

    for (QDockWidget* dock : docks) {
        dock->show();
    }
    if (!m_tutorialGuideWidget || m_tutorialGuideWidget->activeTutorialId().isEmpty())
        m_tutorialGuideDock->hide();
    // Modeling is the default workspace. Saved layouts are restored afterwards;
    // logs/diagnostics remain available from View and the compact task monitor.
    for (QDockWidget* dock : {m_messagesDock, m_taskCenterDock,
                             m_fdsOutputDock, m_resultStatusDock}) dock->hide();
    if (m_taskCenterWidget) m_taskCenterWidget->setDetailsExpanded(false);

    const int sideDockWidth = qBound(kLeftDockMinimumWidth,
                                     qRound(width() * 0.20),
                                     kLeftDockMaximumWidth);
    const int treeDockWidth = qBound(240, qRound(width() * 0.23), 360);
    const int messagesDockHeight = qBound(kMessagesDockMinimumHeight,
                                          qRound(height() * 0.20),
                                          kMessagesDockMaximumHeight);
    resizeDocks({m_messagesDock, m_taskCenterDock},
                {messagesDockHeight, messagesDockHeight}, Qt::Vertical);
    resizeDocks({m_modelTreeDock, m_propertiesDock, m_inspectorDock,
                 m_tutorialGuideDock},
                {treeDockWidth, sideDockWidth, sideDockWidth, sideDockWidth},
                Qt::Horizontal);
    QTimer::singleShot(0, this, [this, treeDockWidth, sideDockWidth, messagesDockHeight, layoutRevision]() {
        // Restoring a saved layout supersedes the initial default sizes.
        if (layoutRevision != m_layoutRevision) return;
        resizeDocks({m_modelTreeDock, m_propertiesDock, m_inspectorDock,
                     m_tutorialGuideDock},
                    {treeDockWidth, sideDockWidth, sideDockWidth, sideDockWidth},
                    Qt::Horizontal);
        resizeDocks({m_messagesDock, m_taskCenterDock, m_fdsOutputDock,
                     m_resultStatusDock},
                    {messagesDockHeight, messagesDockHeight,
                     messagesDockHeight, messagesDockHeight}, Qt::Vertical);
    });
    statusBar()->showMessage(QStringLiteral("Layout reset."), kStatusMessageDurationMs);
}

void MainWindow::ensureFloatingFeedbackDockGeometry(QDockWidget* dock)
{
    if (!dock || !dock->isFloating() ||
        (dock != m_messagesDock && dock != m_inspectorDock)) return;

    QScreen* targetScreen = nullptr;
    int largestIntersectionArea = 0;
    for (QScreen* screen : QGuiApplication::screens()) {
        const QRect intersection = screen->availableGeometry().intersected(dock->frameGeometry());
        const int area = intersection.width() * intersection.height();
        if (area > largestIntersectionArea) {
            largestIntersectionArea = area;
            targetScreen = screen;
        }
    }
    if (!targetScreen) targetScreen = screen();
    if (!targetScreen) return;

    const QRect available = targetScreen->availableGeometry().adjusted(8, 8, -8, -8);
    const QSize frameExtra = dock->frameGeometry().size() - dock->size();
    const QSize maximum(qMax(1, available.width() - qMax(0, frameExtra.width())),
                        qMax(1, available.height() - qMax(0, frameExtra.height())));
    const bool messages = dock == m_messagesDock;
    const QSize minimum = (messages ? QSize(480, 260) : QSize(420, 320)).boundedTo(maximum);
    const QSize preferred = (messages ? QSize(760, 360) : QSize(560, 440)).boundedTo(maximum);
    const bool unreadable = dock->width() < minimum.width() || dock->height() < minimum.height();
    const bool offScreen = !available.contains(dock->frameGeometry());

    // Set the minimum only while floating: a normal dock remains compact and
    // does not force the entire modeling workspace to grow.
    dock->setMinimumSize(minimum);
    if (!unreadable && !offScreen) return;

    dock->resize((unreadable ? dock->size().expandedTo(preferred) : dock->size()).boundedTo(maximum));
    QRect frame = dock->frameGeometry();
    if (unreadable || largestIntersectionArea == 0) frame.moveCenter(frameGeometry().center());
    frame.moveLeft(qBound(available.left(), frame.left(),
                          qMax(available.left(), available.right() - frame.width() + 1)));
    frame.moveTop(qBound(available.top(), frame.top(),
                         qMax(available.top(), available.bottom() - frame.height() + 1)));
    dock->move(frame.topLeft());
}

void MainWindow::restoreSavedLayout()
{
    const QString executableName =
        QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    if (executableName.contains(QStringLiteral("Tests"), Qt::CaseInsensitive) ||
        qEnvironmentVariableIsSet("FIRECAE_DISABLE_LAYOUT_RESTORE")) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("state")).toByteArray();
    const int workspaceTab = settings.value(QStringLiteral("workspaceTab"), 0).toInt();
    const QByteArray treeViewState = settings.value(QStringLiteral("modelTreeViewState")).toByteArray();
    const bool taskDetailsExpanded = settings.value(QStringLiteral("taskDetailsExpanded"), false).toBool();
    settings.endGroup();

    if (!geometry.isEmpty()) restoreGeometry(geometry);
    if (!state.isEmpty()) {
        if (restoreState(state, 1)) ++m_layoutRevision;
        else resetDefaultLayout();
    }
    if (m_modelTreeWidget && !treeViewState.isEmpty())
        m_modelTreeWidget->restoreViewState(treeViewState);
    if (m_taskCenterWidget) m_taskCenterWidget->setDetailsExpanded(taskDetailsExpanded);

    QScreen* targetScreen = nullptr;
    int largestIntersectionArea = 0;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (!screen) continue;
        const QRect intersection = screen->availableGeometry().intersected(frameGeometry());
        const int area = intersection.width() * intersection.height();
        if (area > largestIntersectionArea) {
            largestIntersectionArea = area;
            targetScreen = screen;
        }
    }
    if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen) {
        const QRect available = targetScreen->availableGeometry();
        const QSize safeSize(qMin(width(), available.width()),
                             qMin(height(), available.height()));
        if (largestIntersectionArea == 0) {
            setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                            safeSize, available));
        } else {
            const int safeX = qBound(available.left(), x(),
                                     available.right() - safeSize.width() + 1);
            const int safeY = qBound(available.top(), y(),
                                     available.bottom() - safeSize.height() + 1);
            setGeometry(QRect(QPoint(safeX, safeY), safeSize));
        }
    }
    if (m_workspaceTabs && workspaceTab >= 0 &&
        workspaceTab < m_workspaceTabs->count()) {
        m_workspaceTabs->setCurrentIndex(workspaceTab);
    }
}

void MainWindow::saveCurrentLayout()
{
    const QString executableName =
        QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    if (executableName.contains(QStringLiteral("Tests"), Qt::CaseInsensitive)) return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState(1));
    if (m_modelTreeWidget)
        settings.setValue(QStringLiteral("modelTreeViewState"), m_modelTreeWidget->saveViewState());
    if (m_taskCenterWidget)
        settings.setValue(QStringLiteral("taskDetailsExpanded"), m_taskCenterWidget->detailsExpanded());
    settings.setValue(QStringLiteral("workspaceTab"),
                      m_workspaceTabs ? m_workspaceTabs->currentIndex() : 0);
    settings.endGroup();
}

void MainWindow::showModelWorkspace(int tabIndex)
{
    if (!m_centralStack || !m_workspaceTabs) return;
    m_centralStack->setCurrentWidget(m_workspaceTabs);
    if (tabIndex >= 0 && tabIndex < m_workspaceTabs->count()) {
        m_workspaceTabs->setCurrentIndex(tabIndex);
    }
}

void MainWindow::refreshRecordView()
{
    if (!m_recordView) return;
    if (m_recordAdvancedMode && m_recordAdvancedMode->isChecked() &&
        !m_recordView->isReadOnly()) {
        return;
    }
    if (!m_project) {
        m_recordView->clear();
        m_recordView->setSourceMap({});
        return;
    }
    const FdsWriteResult result = FdsWriter::render(*m_project);
    m_recordView->setGeneratedContent(result.text, result.sourceMap);
    if (m_validationList) {
        m_validationList->clear();
        const auto attachSource = [](QListWidgetItem* item,
                                     const QString& message) {
            if (!item) return;
            static const QRegularExpression uuidExpression(
                QStringLiteral(R"(\[UUID\s+([^\]]+)\])"));
            static const QRegularExpression parameterExpression(
                QStringLiteral(
                    R"(\bparameter\s+([A-Z][A-Z0-9_]*(?:\([^)]*\))?))"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch uuidMatch =
                uuidExpression.match(message);
            if (uuidMatch.hasMatch()) {
                item->setData(Qt::UserRole, uuidMatch.captured(1).trimmed());
            }
            const QRegularExpressionMatch parameterMatch =
                parameterExpression.match(message);
            if (parameterMatch.hasMatch()) {
                item->setData(Qt::UserRole + 1,
                              parameterMatch.captured(1).trimmed().toUpper());
            }
        };
        for (const QString& error : result.errors) {
            auto* item = new QListWidgetItem(
                QStringLiteral("Error: %1").arg(error), m_validationList);
            item->setForeground(QColor(180, 35, 35));
            item->setToolTip(error);
            attachSource(item, error);
        }
        for (const QString& warning : result.warnings) {
            auto* item = new QListWidgetItem(
                QStringLiteral("Warning: %1").arg(warning), m_validationList);
            item->setForeground(QColor(170, 105, 0));
            item->setToolTip(warning);
            attachSource(item, warning);
        }
        if (result.errors.isEmpty() && result.warnings.isEmpty()) {
            auto* item = new QListWidgetItem(u("No validation issues."),
                                             m_validationList);
            item->setFlags(Qt::NoItemFlags);
        }
        if (m_inspectorTabs && m_inspectorTabs->count() > 2) {
            m_inspectorTabs->setTabText(
                2, QStringLiteral("%1 (%2)").arg(u("Validation Issues"))
                       .arg(result.errors.size() + result.warnings.size()));
        }
    }
    if (m_fdsOutputLog) {
        QStringList output;
        output.append(QStringLiteral("Generated %1 lines for CHID=%2.")
                          .arg(result.text.count(QLatin1Char('\n')) + 1)
                          .arg(m_project->chid()));
        output.append(QStringLiteral("Errors: %1; warnings: %2")
                          .arg(result.errors.size())
                          .arg(result.warnings.size()));
        for (const QString& error : result.errors) {
            output.append(QStringLiteral("[Error] %1").arg(error));
        }
        for (const QString& warning : result.warnings) {
            output.append(QStringLiteral("[Warning] %1").arg(warning));
        }
        m_fdsOutputLog->setPlainText(output.join(QLatin1Char('\n')));
    }
    if (m_resultStatusLabel && m_project->document()) {
        int resultCaseCount = 0;
        const std::function<void(const FcObject::Ptr&)> countResults =
            [&](const FcObject::Ptr& object) {
                if (!object) return;
                if (object->type() == FcObjectType::ResultCase) ++resultCaseCount;
                for (const FcObject::Ptr& child : object->children()) countResults(child);
            };
        for (const auto& group : m_project->document()->groups()) countResults(group);
        m_resultStatusLabel->setText(
            resultCaseCount == 0
                ? u("No result case is loaded.")
                : u("%1 result case(s) loaded. Select a case and open it in Smokeview.")
                      .arg(resultCaseCount));
    }
}

void MainWindow::saveAdvancedRecordDraft()
{
    if (!m_recordView || !m_recordAdvancedMode ||
        !m_recordAdvancedMode->isChecked()) {
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, u("Save Advanced FDS Draft"),
        QStringLiteral("fds-record-draft.fds"),
        QStringLiteral("FDS Input (*.fds);;All Files (*.*)"));
    if (filePath.isEmpty()) return;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
        file.write(m_recordView->toPlainText().toUtf8()) < 0 ||
        !file.commit()) {
        QMessageBox::critical(
            this, u("Advanced FDS Editing"),
            QStringLiteral("%1\n%2")
                .arg(u("Failed to save the advanced FDS draft."),
                     file.errorString()));
        return;
    }
    statusBar()->showMessage(u("Advanced FDS draft saved."),
                             kStatusMessageDurationMs);
}

void MainWindow::setInterfaceLanguage(UiLanguage language)
{
    UiLanguageManager::setCurrentLanguage(language);
    if (m_englishAction) {
        m_englishAction->setChecked(language == UiLanguage::English);
    }
    if (m_chineseAction) {
        m_chineseAction->setChecked(language == UiLanguage::ChineseSimplified);
    }
    retranslateUi();
    if (m_modelTreeWidget) {
        m_modelTreeWidget->retranslateUi();
        m_modelTreeWidget->refresh();
    }
    if (m_startPageWidget) m_startPageWidget->retranslateUi();
    refreshCurrentSelection();
    statusBar()->showMessage(
        language == UiLanguage::ChineseSimplified
            ? QStringLiteral("界面语言已切换为简体中文。")
            : QStringLiteral("Interface language changed to English."),
        kStatusMessageDurationMs);
}

void MainWindow::retranslateUi()
{
    const QList<QPair<QAction*, const char*>> actionTexts = {
        {m_newAction, "New"}, {m_openAction, "Open"}, {m_saveAction, "Save"},
        {m_saveAsAction, "Save As"}, {m_importFdsAction, "Import FDS Input..."},
        {m_exportFdsAction, "Export FDS Input..."},
        {m_exitAction, "Exit"}, {m_preferencesAction, "Preferences..."},
        {m_manageResourcesAction, "Project Resources..."},
        {m_packageProjectAction, "Package Project..."},
        {m_unpackProjectAction, "Unpack Project..."},
        {m_copyProjectAction, "Copy Project to Directory..."},
        {m_cleanResultsAction, "Clean Unused Results..."},
        {m_startPageAction, "Start Page"},
        {m_undoAction, "Undo"}, {m_redoAction, "Redo"},
        {m_editObjectAction, "Edit Selected Object..."},
        {m_duplicateAction, "Duplicate Object"},
        {m_moveUpAction, "Move Up"}, {m_moveDownAction, "Move Down"},
        {m_deleteAction, "Delete"},
        {m_selectAllAction, "Select All Objects"},
        {m_invertSelectionAction, "Invert Selection"},
        {m_selectByTypeAction, "Select by Type..."},
        {m_selectByPropertyAction, "Select by Property..."},
        {m_selectByFloorAction, "Select by Floor/Group..."},
        {m_snapEnabledAction, "Snap Enabled"},
        {m_snapSettingsAction, "Snap Settings..."},
        {m_transformAction, "Transform Selected..."},
        {m_transformGizmoAction, "Transform Gizmo"},
        {m_copyMoveAction, "Copy and Move..."}, {m_arrayAction, "Array Copy..."},
        {m_measureAction, "Measure Selection"},
        {m_mirrorAction, "Mirror Selected..."},
        {m_alignAction, "Align Selected..."},
        {m_copyToFloorAction, "Copy to Floor..."},
        {m_groupAction, "Group Selected"}, {m_hideSelectedAction, "Hide Selected"},
        {m_createFolderAction, "New Geometry Folder..."},
        {m_createFloorAction, "New Floor..."},
        {m_lockSelectedAction, "Lock/Unlock Selected"},
        {m_batchRenameAction, "Batch Rename..."},
        {m_assignSurfacesAction, "Assign Surfaces..."},
        {m_selectAction, "Select"}, {m_fitAllAction, "Fit All"},
        {m_restoreVisibilityAction, "Show All Objects"},
        {m_fitSelectionAction, "Fit Selection"},
        {m_perspectiveAction, "Perspective"},
        {m_orthographicAction, "Orthographic"},
        {m_saveViewAction, "Save View"}, {m_restoreViewAction, "Restore View"},
        {m_rotationCenterAction, "Set Rotation Center..."},
        {m_axesAction, "World Axes"}, {m_backgroundAction, "Background Color..."},
        {m_clippingPlaneAction, "Clipping Plane..."},
        {m_frontAction, "Front"}, {m_backAction, "Back"}, {m_leftAction, "Left"},
        {m_rightAction, "Right"}, {m_topAction, "Top"}, {m_bottomAction, "Bottom"},
        {m_isometricAction, "Isometric"}, {m_resetLayoutAction, "Reset Layout"},
        {m_createBoxAction, "Create Box"}, {m_createWallAction, "Create Wall"},
        {m_directEditAction, "Edit Dimensions in View"},
        {m_drawWallAction, "Draw Wall in View..."},
        {m_createRoomAction, "Create Room"}, {m_importGeometryAction, "Import Geometry"},
        {m_previewFdsBlocksAction, "Preview FDS Blocks..."},
        {m_convertGeometryToFdsAction, "Generate FDS Blocks"},
        {m_healGeometryAction, "Heal Selected Geometry"},
        {m_unionGeometryAction, "Boolean Union"},
        {m_cutGeometryAction, "Boolean Difference"},
        {m_intersectGeometryAction, "Boolean Intersection"},
        {m_splitGeometryAction, "Split with Selected Tool"},
        {m_backgroundImageAction, "Import Background Image..."},
        {m_projectSettingsAction, "Project Settings..."},
        {m_simulationParametersAction, "Simulation Parameters..."},
        {m_meshAction, "Mesh"}, {m_speciesAction, "Species"},
        {m_materialAction, "Material"},
        {m_surfaceAction, "Surface"}, {m_reactionAction, "Reaction"},
        {m_particleAction, "Particle"}, {m_ventAction, "Vent"},
        {m_deviceAction, "Device"}, {m_controlAction, "Control"},
        {m_hvacAction, "HVAC"}, {m_initialConditionAction, "Initial Condition"},
        {m_outputAction, "Output"}, {m_addFdsObjectAction, "Add FDS Object..."},
        {m_meshEngineeringAction, "Mesh Engineering Assistant..."},
        {m_fireSourceWizardAction, "Fire Source Wizard..."},
        {m_particleSprayWizardAction, "Particle and Sprinkler Wizard..."},
        {m_deviceControlWizardAction, "Device and Control Wizard..."},
        {m_outputWizardAction, "Output Wizard..."},
        {m_controlGraphAction, "Control Logic Graph..."},
        {m_hvacGraphAction, "HVAC Network Graph..."},
        {m_propertyLibraryAction, "FDS Property Library..."},
        {m_scenarioManagerAction, "Scenario Manager..."},
        {m_runAllScenariosAction, "Run All Scenarios..."},
        {m_simpleTestAction, "Create Simple Test Benchmark"},
        {m_activateVentsAction, "Open completed activate_vents example"},
        {m_bucketTest2Action, "Open completed bucket_test_2 example"},
        {m_couchAction, "Open completed couch example"},
        {m_couchSmoke12sAction, "Open completed couch_smoke_12s example"},
        {m_hvacAircoilAction, "Open completed HVAC_aircoil example"},
        {m_tunnelDemoAction, "Open completed tunnel_demo example"},
        {m_tunnelSmoke10sAction, "Open completed tunnel_smoke_10s example"},
        {m_runAction, "Run Current Project"},
        {m_runOpenMpAction, "Run Current Project with CPU/OpenMP..."},
        {m_runParallelAction, "Run Current Project with CPU/MPI..."},
        {m_validateAction, "Validate Model"}, {m_stopAction, "Stop FDS"},
        {m_openResultsAction, "Open FDS Results..."},
        {m_compareResultsAction, "Compare FDS Results..."},
        {m_reloadResultsAction, "Reload Results"},
        {m_closeResultsAction, "Close Results"},
        {m_openNativeResultsAction, "Open in Native Result Viewer"},
        {m_openSmokeviewAction, "Open in Smokeview"},
        {m_openSmokeAnimationAction, "Open Smoke/Fire Animation"},
        {m_openSliceAnimationAction, "Open Slice Animation"},
        {m_openParticleAnimationAction, "Open Particle Animation"},
        {m_configureSmokeviewAction, "Configure Smokeview..."},
        {m_englishAction, "English"}, {m_chineseAction, "Simplified Chinese"},
        {m_diagnosticsAction, "Diagnostics..."},
        {m_aboutAction, "About FireCAE"}};
    for (const auto& actionText : actionTexts) {
        if (actionText.first) {
            actionText.first->setText(u(actionText.second));
        }
    }
    if (m_restoreVisibilityAction && m_isolationActive) {
        m_restoreVisibilityAction->setText(u("Exit Isolation"));
    }
    for (QAction* action : m_buildingGeometryActions) {
        if (action) {
            action->setText(UiLanguageManager::text(
                action->property("firecaeEnglishText").toString()));
        }
    }

    const QList<QPair<QMenu*, const char*>> menuTexts = {
        {m_fileMenu, "File"}, {m_editMenu, "Edit"}, {m_viewMenu, "View"},
        {m_geometryMenu, "Geometry"}, {m_modelMenu, "Model"},
        {m_buildingElementsMenu, "Building Elements"},
        {m_profileGeometryMenu, "Profiles and Sweeps"},
        {m_openingsMenu, "Openings"},
        {m_geometryOperationsMenu, "Geometry Operations"},
        {m_modelAssistantsMenu, "Professional Assistants"},
        {m_tutorialMenu, "Tutorials"},
        {m_tutorialExamplesMenu, "Completed Examples (Reference)"},
        {m_simulationMenu, "Simulation"}, {m_resultsMenu, "Results"},
        {m_languageMenu, "Language"}, {m_helpMenu, "Help"}};
    for (const auto& menuText : menuTexts) {
        if (menuText.first) {
            menuText.first->setTitle(u(menuText.second));
        }
    }
    if (m_mainToolBar) m_mainToolBar->setWindowTitle(u("Main Toolbar"));
    if (m_modelingToolBar) {
        m_modelingToolBar->setWindowTitle(u("Modeling Tools"));
        for(QAction* action:m_modelingToolBar->actions()) {
            if(!action->isSeparator())action->setToolTip(action->text());
        }
    }
    if (m_modelTreeDock) m_modelTreeDock->setWindowTitle(u("Model Tree"));
    if (m_propertiesDock) m_propertiesDock->setWindowTitle(u("Properties"));
    if (m_inspectorDock) m_inspectorDock->setWindowTitle(u("Workspace Inspector"));
    if (m_messagesDock) m_messagesDock->setWindowTitle(u("Messages"));
    if (m_taskCenterDock) m_taskCenterDock->setWindowTitle(u("Simulation Task Center"));
    if (m_fdsOutputDock) m_fdsOutputDock->setWindowTitle(u("FDS Output"));
    if (m_resultStatusDock) m_resultStatusDock->setWindowTitle(u("Result Status"));
    if (m_tutorialGuideDock) m_tutorialGuideDock->setWindowTitle(u("Guided Tutorial"));
    if (m_inspectorTabs && m_inspectorTabs->count() >= 3) {
        m_inspectorTabs->setTabText(0, u("Drawing Tool"));
        m_inspectorTabs->setTabText(
            1, QStringLiteral("%1 (%2)").arg(u("Selection Set"))
                   .arg(m_selectionList ? m_selectionList->count() : 0));
        m_inspectorTabs->setTabText(
            2, QStringLiteral("%1 (%2)").arg(u("Validation Issues"))
                   .arg(m_validationList ? m_validationList->count() : 0));
    }
    if (m_activeToolLabel && m_selectAction && m_selectAction->isChecked()) {
        m_activeToolLabel->setText(u("Active tool: Selection"));
    }
    if (m_workspaceTabs && m_workspaceTabs->count() >= 4) {
        m_workspaceTabs->setTabText(0, u("Model 3D"));
        m_workspaceTabs->setTabText(1, u("Plan 2D"));
        m_workspaceTabs->setTabText(2, u("FDS Record"));
        m_workspaceTabs->setTabText(3, u("Results"));
    }
    if (m_recordView) {
        m_recordView->setPlaceholderText(
            u("The generated FDS input will appear here."));
    }
    if (m_taskCenterWidget) m_taskCenterWidget->retranslateUi();
    if (m_simulationStatusWidget) m_simulationStatusWidget->retranslateUi();
    if (m_nativeResultViewerWidget) m_nativeResultViewerWidget->retranslateUi();
    if (m_smokeviewHostWidget) m_smokeviewHostWidget->retranslateUi();
    updateSnapStatus();
}

void MainWindow::refreshCurrentSelection()
{
    if (!m_modelTreeWidget || !m_propertiesWidget || !m_project ||
        !m_project->document()) {
        return;
    }
    const QString objectId = m_modelTreeWidget->selectedObjectId();
    const FcObject::Ptr object = m_project->document()->findObject(objectId);
    if (object) {
        m_propertiesWidget->showObject(object.get());
    } else {
        m_propertiesWidget->clear();
    }
}
