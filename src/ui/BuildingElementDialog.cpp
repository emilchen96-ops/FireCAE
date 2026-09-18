#include "ui/BuildingElementDialog.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcGeometryObject.h"
#include "ui/UiLanguage.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

namespace
{
QString t(const char* text) { return UiLanguageManager::text(QString::fromLatin1(text)); }

QDoubleSpinBox* lengthSpin(QWidget* parent, const QString& suffix,
                           double minimum = -1.0e9)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, 1.0e9);
    spin->setDecimals(6);
    spin->setSingleStep(0.1);
    spin->setSuffix(QStringLiteral(" ") + suffix);
    return spin;
}

void addObjects(QComboBox* combo, const FcObject::Ptr& root,
                const std::function<bool(const FcObject::Ptr&)>& predicate)
{
    if (!combo || !root) return;
    for (const FcObject::Ptr& child : root->children()) {
        if (!child) continue;
        if (predicate(child)) combo->addItem(child->name(), child->id());
        addObjects(combo, child, predicate);
    }
}
}

BuildingElementDialog::BuildingElementDialog(FcGeometryKind initialKind,
                                             FcProject* project,
                                             QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("BuildingElementDialog"));
    setWindowTitle(t("Create Building Geometry"));
    resize(620, 720);
    auto* layout = new QVBoxLayout(this);
    auto* formContainer = new QWidget(this);
    auto* form = new QFormLayout(formContainer);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("BuildingNameEdit"));
    m_kindCombo = new QComboBox(this);
    m_kindCombo->setObjectName(QStringLiteral("BuildingKindCombo"));
    const QList<FcGeometryKind> kinds = {
        FcGeometryKind::Box, FcGeometryKind::Wall, FcGeometryKind::Slab,
        FcGeometryKind::Roof, FcGeometryKind::Column, FcGeometryKind::Beam,
        FcGeometryKind::PolygonPrism, FcGeometryKind::PolylineSweep,
        FcGeometryKind::Cylinder, FcGeometryKind::RectangleProfile,
        FcGeometryKind::ProfileExtrusion, FcGeometryKind::PathSweep,
        FcGeometryKind::Stair, FcGeometryKind::Ramp, FcGeometryKind::Room,
        FcGeometryKind::RectangularOpening, FcGeometryKind::PolygonalOpening,
        FcGeometryKind::Door, FcGeometryKind::Window,
        FcGeometryKind::SlabOpening, FcGeometryKind::WallVent};
    for (FcGeometryKind kind : kinds) {
        m_kindCombo->addItem(UiLanguageManager::text(fcGeometryKindName(kind)),
                             static_cast<int>(kind));
    }
    const int initialIndex = m_kindCombo->findData(static_cast<int>(initialKind));
    m_kindCombo->setCurrentIndex(std::max(0, initialIndex));
    const QString suffix = project ? project->displayUnitSymbol() : QStringLiteral("m");
    m_x = lengthSpin(this, suffix);
    m_y = lengthSpin(this, suffix);
    m_z = lengthSpin(this, suffix);
    m_endX = lengthSpin(this, suffix);
    m_endY = lengthSpin(this, suffix);
    m_width = lengthSpin(this, suffix, 0.000001);
    m_depth = lengthSpin(this, suffix, 0.000001);
    m_height = lengthSpin(this, suffix, 0.000001);
    m_thickness = lengthSpin(this, suffix, 0.000001);
    m_radius = lengthSpin(this, suffix, 0.0);
    m_rise = lengthSpin(this, suffix, 0.000001);
    m_x->setObjectName(QStringLiteral("BuildingStartXSpin"));
    m_y->setObjectName(QStringLiteral("BuildingStartYSpin"));
    m_z->setObjectName(QStringLiteral("BuildingBaseZSpin"));
    m_endX->setObjectName(QStringLiteral("BuildingEndXSpin"));
    m_endY->setObjectName(QStringLiteral("BuildingEndYSpin"));
    m_width->setObjectName(QStringLiteral("BuildingWidthSpin"));
    m_depth->setObjectName(QStringLiteral("BuildingDepthSpin"));
    m_height->setObjectName(QStringLiteral("BuildingHeightSpin"));
    m_thickness->setObjectName(QStringLiteral("BuildingThicknessSpin"));
    m_radius->setObjectName(QStringLiteral("BuildingRadiusSpin"));
    m_rise->setObjectName(QStringLiteral("BuildingRiseSpin"));
    m_steps = new QSpinBox(this);
    m_steps->setObjectName(QStringLiteral("BuildingStepCountSpin"));
    m_steps->setRange(1, 10000);
    m_baseline = new QComboBox(this);
    m_baseline->setObjectName(QStringLiteral("WallBaselineCombo"));
    m_baseline->addItem(t("Left"), static_cast<int>(FcWallBaseline::Left));
    m_baseline->addItem(t("Center"), static_cast<int>(FcWallBaseline::Center));
    m_baseline->addItem(t("Right"), static_cast<int>(FcWallBaseline::Right));
    m_fdsConversionRoute = new QComboBox(this);
    m_fdsConversionRoute->setObjectName(QStringLiteral("FdsGeometryConversionRouteCombo"));
    m_fdsConversionRoute->addItem(t("Automatic"), QStringLiteral("Auto"));
    m_fdsConversionRoute->addItem(t("Native FDS GEOM (triangulated)"),
                                  QStringLiteral("GEOM"));
    m_fdsConversionRoute->addItem(t("Rasterized FDS OBST"),
                                  QStringLiteral("OBST"));
    m_fdsConversionRoute->addItem(t("Ignore during FDS conversion"),
                                  QStringLiteral("IGNORE"));
    m_fdsConversionRoute->addItem(t("Drawing reference only"),
                                  QStringLiteral("REFERENCE"));
    m_profile = new QLineEdit(this);
    m_profile->setObjectName(QStringLiteral("BuildingProfileEdit"));
    m_profile->setPlaceholderText(QStringLiteral("0,0; 4,0; 4,3; 0,3"));
    m_path = new QLineEdit(this);
    m_path->setObjectName(QStringLiteral("BuildingPathEdit"));
    m_path->setPlaceholderText(QStringLiteral("0,0; 4,0; 4,3"));
    m_hostCombo = new QComboBox(this);
    m_controlCombo = new QComboBox(this);
    m_surfaceCombo = new QComboBox(this);
    m_hostCombo->setObjectName(QStringLiteral("OpeningHostCombo"));
    m_controlCombo->setObjectName(QStringLiteral("OpeningControlCombo"));
    m_surfaceCombo->setObjectName(QStringLiteral("GeometrySurfaceCombo"));
    m_dynamic = new QCheckBox(t("Controlled dynamic opening"), this);
    m_dynamic->setObjectName(QStringLiteral("DynamicOpeningCheck"));
    m_previewSummary = new QLabel(this);
    m_previewSummary->setWordWrap(true);
    m_previewSummary->setObjectName(QStringLiteral("GeometryPreviewSummary"));

    form->addRow(t("Name:"), m_nameEdit);
    form->addRow(t("Geometry type:"), m_kindCombo);
    form->addRow(t("Start X:"), m_x);
    form->addRow(t("Start Y:"), m_y);
    form->addRow(t("Base Z:"), m_z);
    form->addRow(t("End X:"), m_endX);
    form->addRow(t("End Y:"), m_endY);
    form->addRow(t("Width:"), m_width);
    form->addRow(t("Depth / run:"), m_depth);
    form->addRow(t("Height:"), m_height);
    form->addRow(t("Thickness:"), m_thickness);
    form->addRow(t("Radius (0 = rectangular column):"), m_radius);
    form->addRow(t("Rise:"), m_rise);
    form->addRow(t("Step count:"), m_steps);
    form->addRow(t("Wall baseline:"), m_baseline);
    form->addRow(t("FDS conversion route:"), m_fdsConversionRoute);
    form->addRow(t("2D profile (x,y; ...):"), m_profile);
    form->addRow(t("2D path (x,y; ...):"), m_path);
    form->addRow(t("Opening host (UUID):"), m_hostCombo);
    form->addRow(t("Control / device (UUID):"), m_controlCombo);
    form->addRow(t("Default FDS surface (UUID):"), m_surfaceCombo);
    for (const auto& face : QList<QPair<QString, QString>>{
             {QStringLiteral("X-"), t("Surface X-:")},
             {QStringLiteral("X+"), t("Surface X+:")},
             {QStringLiteral("Y-"), t("Surface Y-:")},
             {QStringLiteral("Y+"), t("Surface Y+:")},
             {QStringLiteral("Z-"), t("Surface Z-:")},
             {QStringLiteral("Z+"), t("Surface Z+:")}}) {
        auto* combo = new QComboBox(this);
        combo->setObjectName(QStringLiteral("GeometryFaceSurface%1Combo").arg(face.first));
        m_faceSurfaceCombos.insert(face.first, combo);
        form->addRow(face.second, combo);
    }
    m_topologyFaceGroup = new QGroupBox(t("Topological face overrides"), this);
    m_topologyFaceGroup->setObjectName(QStringLiteral("TopologyFaceSurfaceGroup"));
    m_topologyFaceForm = new QFormLayout(m_topologyFaceGroup);
    auto* topologyHelp = new QLabel(
        t("Each entry identifies a real OpenCascade face by a stable geometric signature. These overrides are used by native FDS GEOM triangulation."),
        m_topologyFaceGroup);
    topologyHelp->setWordWrap(true);
    m_topologyFaceForm->addRow(topologyHelp);
    form->addRow(m_topologyFaceGroup);
    form->addRow(QString(), m_dynamic);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("BuildingElementScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(formContainer);
    layout->addWidget(scrollArea, 1);
    auto* previewButton = new QPushButton(t("Validate and Preview"), this);
    previewButton->setObjectName(QStringLiteral("ValidateGeometryPreviewButton"));
    layout->addWidget(previewButton);
    layout->addWidget(m_previewSummary);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BuildingElementDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &BuildingElementDialog::reject);
    connect(previewButton, &QPushButton::clicked, this,
            [this]() { updatePreviewSummary(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this,
            [this]() { updateFieldAvailability(); updatePreviewSummary(); });
    connect(m_baseline, &QComboBox::currentIndexChanged, this,
            [this]() { handleBaselineChanged(); });
    connect(m_dynamic, &QCheckBox::toggled, this,
            [this]() { updateFieldAvailability(); });
    layout->addWidget(buttons);

    m_endX->setValue(toDisplay(4.0));
    m_width->setValue(toDisplay(4.0));
    m_depth->setValue(toDisplay(3.0));
    m_height->setValue(toDisplay(3.0));
    m_thickness->setValue(toDisplay(0.2));
    m_radius->setValue(toDisplay(0.25));
    m_rise->setValue(toDisplay(3.0));
    m_steps->setValue(12);
    m_baseline->setCurrentIndex(1);
    m_lastBaseline = FcWallBaseline::Center;
    m_profile->setText(QStringLiteral("0,0; 4,0; 4,3; 0,3"));
    populateReferences();
    updateFieldAvailability();
    updatePreviewSummary();
}

void BuildingElementDialog::setExistingObject(
    const std::shared_ptr<FcGeometryObject>& object)
{
    if (!object) return;
    setWindowTitle(t("Edit Building Geometry"));
    m_nameEdit->setText(object->name());
    const BuildingGeometryRequest value = BuildingGeometryService::requestFromParameters(
        object->geometryKind(), object->geometryParameters());
    m_kindCombo->setCurrentIndex(m_kindCombo->findData(static_cast<int>(value.kind)));
    m_x->setValue(toDisplay(value.x)); m_y->setValue(toDisplay(value.y));
    m_z->setValue(toDisplay(value.z)); m_endX->setValue(toDisplay(value.endX));
    m_endY->setValue(toDisplay(value.endY)); m_width->setValue(toDisplay(value.width));
    m_depth->setValue(toDisplay(value.depth)); m_height->setValue(toDisplay(value.height));
    m_thickness->setValue(toDisplay(value.thickness));
    m_radius->setValue(toDisplay(value.radius)); m_rise->setValue(toDisplay(value.rise));
    m_steps->setValue(value.stepCount);
    m_baseline->setCurrentIndex(m_baseline->findData(static_cast<int>(value.baseline)));
    m_fdsConversionRoute->setCurrentIndex(
        m_fdsConversionRoute->findData(value.fdsConversionRoute));
    m_lastBaseline = value.baseline;
    m_editingWall = value.kind == FcGeometryKind::Wall;
    m_profile->setText(formatPoints(value.profile));
    m_path->setText(formatPoints(value.path));
    m_hostCombo->setCurrentIndex(m_hostCombo->findData(object->hostObjectId()));
    m_controlCombo->setCurrentIndex(m_controlCombo->findData(object->controlObjectId()));
    m_surfaceCombo->setCurrentIndex(m_surfaceCombo->findData(object->defaultSurfaceId()));
    for (auto iterator = m_faceSurfaceCombos.begin();
         iterator != m_faceSurfaceCombos.end(); ++iterator) {
        iterator.value()->setCurrentIndex(
            iterator.value()->findData(object->faceSurfaceIds().value(iterator.key())));
    }
    m_dynamic->setChecked(object->isDynamicOpening());
    updateTopologyFaceEditors(object->shape(), object->faceSurfaceIds());
    updateFieldAvailability();
    updatePreviewSummary();
}

void BuildingElementDialog::setInitialRequest(const BuildingGeometryRequest& value)
{
    const int kindIndex = m_kindCombo->findData(static_cast<int>(value.kind));
    if (kindIndex >= 0) m_kindCombo->setCurrentIndex(kindIndex);
    m_x->setValue(toDisplay(value.x));
    m_y->setValue(toDisplay(value.y));
    m_z->setValue(toDisplay(value.z));
    m_endX->setValue(toDisplay(value.endX));
    m_endY->setValue(toDisplay(value.endY));
    m_width->setValue(toDisplay(value.width));
    m_depth->setValue(toDisplay(value.depth));
    m_height->setValue(toDisplay(value.height));
    m_thickness->setValue(toDisplay(value.thickness));
    m_radius->setValue(toDisplay(value.radius));
    m_rise->setValue(toDisplay(value.rise));
    m_steps->setValue(value.stepCount);
    m_baseline->setCurrentIndex(static_cast<int>(value.baseline));
    const int routeIndex = m_fdsConversionRoute->findData(value.fdsConversionRoute);
    if (routeIndex >= 0) m_fdsConversionRoute->setCurrentIndex(routeIndex);
    m_profile->setText(formatPoints(value.profile));
    m_path->setText(formatPoints(value.path));
    updateFieldAvailability();
    updatePreviewSummary();
}

QString BuildingElementDialog::geometryName() const { return m_nameEdit->text().trimmed(); }

BuildingGeometryRequest BuildingElementDialog::request() const
{
    BuildingGeometryRequest value;
    value.kind = static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt());
    value.x = fromDisplay(m_x->value()); value.y = fromDisplay(m_y->value());
    value.z = fromDisplay(m_z->value()); value.endX = fromDisplay(m_endX->value());
    value.endY = fromDisplay(m_endY->value()); value.width = fromDisplay(m_width->value());
    value.depth = fromDisplay(m_depth->value()); value.height = fromDisplay(m_height->value());
    value.thickness = fromDisplay(m_thickness->value());
    value.radius = fromDisplay(m_radius->value()); value.rise = fromDisplay(m_rise->value());
    value.stepCount = m_steps->value();
    value.baseline = static_cast<FcWallBaseline>(m_baseline->currentData().toInt());
    value.fdsConversionRoute = m_fdsConversionRoute->currentData().toString();
    value.profile = parsePoints(m_profile->text());
    value.path = parsePoints(m_path->text());
    return value;
}

QString BuildingElementDialog::hostObjectId() const { return m_hostCombo->currentData().toString(); }
QString BuildingElementDialog::controlObjectId() const { return m_controlCombo->currentData().toString(); }
QString BuildingElementDialog::surfaceObjectId() const { return m_surfaceCombo->currentData().toString(); }
QMap<QString, QString> BuildingElementDialog::faceSurfaceIds() const
{
    QMap<QString, QString> assignments;
    for (auto iterator = m_faceSurfaceCombos.cbegin();
         iterator != m_faceSurfaceCombos.cend(); ++iterator) {
        const QString uuid = iterator.value()->currentData().toString();
        if (!uuid.isEmpty()) assignments.insert(iterator.key(), uuid);
    }
    for (auto iterator = m_topologyFaceSurfaceCombos.cbegin();
         iterator != m_topologyFaceSurfaceCombos.cend(); ++iterator) {
        const QString uuid = iterator.value()->currentData().toString();
        if (!uuid.isEmpty()) assignments.insert(iterator.key(), uuid);
    }
    return assignments;
}
bool BuildingElementDialog::dynamicOpening() const { return m_dynamic->isChecked(); }

void BuildingElementDialog::accept()
{
    if (geometryName().isEmpty()) {
        QMessageBox::warning(this, t("Invalid Geometry"), t("Enter a geometry name."));
        return;
    }
    QString error;
    if (BuildingGeometryService::createShape(request(), &error).IsNull()) {
        QMessageBox::warning(this, t("Invalid Geometry"), error);
        return;
    }
    if (BuildingGeometryService::isOpeningKind(request().kind) && hostObjectId().isEmpty()) {
        QMessageBox::warning(this, t("Invalid Geometry"),
                             t("Choose a host wall, slab, or roof for the opening."));
        return;
    }
    if (dynamicOpening() && controlObjectId().isEmpty()) {
        QMessageBox::warning(this, t("Invalid Geometry"),
                             t("Choose a CTRL or DEVC object for a dynamic opening."));
        return;
    }
    QDialog::accept();
}

void BuildingElementDialog::populateReferences()
{
    m_hostCombo->clear(); m_controlCombo->clear(); m_surfaceCombo->clear();
    m_hostCombo->addItem(t("None"), QString());
    m_controlCombo->addItem(t("None"), QString());
    m_surfaceCombo->addItem(t("None"), QString());
    for (QComboBox* combo : m_faceSurfaceCombos) {
        populateSurfaceCombo(combo, true);
    }
    if (!m_project || !m_project->document()) return;
    addObjects(m_hostCombo, m_project->document()->geometryGroup(), [](const FcObject::Ptr& object) {
        const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object);
        return geometry && !BuildingGeometryService::isOpeningKind(geometry->geometryKind());
    });
    addObjects(m_controlCombo, m_project->document()->controlsGroup(), [](const FcObject::Ptr& object) {
        return object->type() == FcObjectType::Control || object->type() == FcObjectType::Device;
    });
    addObjects(m_controlCombo, m_project->document()->devicesGroup(), [](const FcObject::Ptr& object) {
        return object->type() == FcObjectType::Control || object->type() == FcObjectType::Device;
    });
    addObjects(m_surfaceCombo, m_project->document()->surfacesGroup(), [](const FcObject::Ptr& object) {
        return object->type() == FcObjectType::Surface;
    });
    for (QComboBox* combo : m_faceSurfaceCombos) populateSurfaceCombo(combo, true);
    for (QComboBox* combo : m_topologyFaceSurfaceCombos) populateSurfaceCombo(combo, true);
}

void BuildingElementDialog::populateSurfaceCombo(QComboBox* combo,
                                                  bool inheritDefault) const
{
    if (!combo) return;
    const QString selected = combo->currentData().toString();
    combo->clear();
    combo->addItem(inheritDefault ? t("Inherit default") : t("None"), QString());
    if (m_project && m_project->document()) {
        addObjects(combo, m_project->document()->surfacesGroup(),
                   [](const FcObject::Ptr& object) {
            return object->type() == FcObjectType::Surface;
        });
    }
    const int index = combo->findData(selected);
    if (index >= 0) combo->setCurrentIndex(index);
}

void BuildingElementDialog::updateTopologyFaceEditors(
    const TopoDS_Shape& shape, const QMap<QString, QString>& assignments)
{
    if (!m_topologyFaceForm || !m_topologyFaceGroup) return;
    QMap<QString, QString> selected = assignments;
    if (selected.isEmpty()) {
        for (auto iterator = m_topologyFaceSurfaceCombos.cbegin();
             iterator != m_topologyFaceSurfaceCombos.cend(); ++iterator) {
            const QString uuid = iterator.value()->currentData().toString();
            if (!uuid.isEmpty()) selected.insert(iterator.key(), uuid);
        }
    }
    const QVector<GeometryFaceInfo> faces = BuildingGeometryService::faceInfos(shape);
    QStringList newKeys;
    for (const GeometryFaceInfo& face : faces) newKeys.append(face.key);
    newKeys.sort();
    if (newKeys == m_topologyFaceSurfaceCombos.keys()) {
        for (auto iterator = selected.cbegin(); iterator != selected.cend(); ++iterator) {
            QComboBox* combo = m_topologyFaceSurfaceCombos.value(iterator.key());
            if (combo) combo->setCurrentIndex(combo->findData(iterator.value()));
        }
        return;
    }
    while (m_topologyFaceForm->count() > 1) {
        QLayoutItem* item = m_topologyFaceForm->takeAt(1);
        if (item && item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_topologyFaceSurfaceCombos.clear();
    for (const GeometryFaceInfo& face : faces) {
        auto* combo = new QComboBox(m_topologyFaceGroup);
        combo->setObjectName(QStringLiteral("TopologyFaceSurface_%1")
                                 .arg(face.key.mid(QStringLiteral("TopoFace:").size())));
        populateSurfaceCombo(combo, true);
        const int index = combo->findData(selected.value(face.key));
        if (index >= 0) combo->setCurrentIndex(index);
        combo->setToolTip(face.key);
        m_topologyFaceSurfaceCombos.insert(face.key, combo);
        m_topologyFaceForm->addRow(face.label, combo);
    }
    m_topologyFaceGroup->setVisible(!faces.isEmpty());
}

void BuildingElementDialog::handleBaselineChanged()
{
    if (m_adjustingBaseline || !m_editingWall ||
        static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt()) !=
            FcGeometryKind::Wall) return;
    const FcWallBaseline next = static_cast<FcWallBaseline>(
        m_baseline->currentData().toInt());
    if (next == m_lastBaseline) return;
    BuildingGeometryRequest current = request();
    current.baseline = m_lastBaseline;
    const BuildingGeometryRequest rebased =
        BuildingGeometryService::rebaseWall(current, next, true);
    m_adjustingBaseline = true;
    m_x->setValue(toDisplay(rebased.x));
    m_y->setValue(toDisplay(rebased.y));
    m_endX->setValue(toDisplay(rebased.endX));
    m_endY->setValue(toDisplay(rebased.endY));
    if (!rebased.path.isEmpty()) m_path->setText(formatPoints(rebased.path));
    m_adjustingBaseline = false;
    m_lastBaseline = next;
    updatePreviewSummary();
}

void BuildingElementDialog::updateFieldAvailability()
{
    const FcGeometryKind kind = static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt());
    const bool lineBased = kind == FcGeometryKind::Wall || kind == FcGeometryKind::Beam ||
                           kind == FcGeometryKind::PolylineSweep || kind == FcGeometryKind::PathSweep;
    const bool polygon = kind == FcGeometryKind::PolygonPrism ||
                         kind == FcGeometryKind::PolygonalOpening ||
                         kind == FcGeometryKind::ProfileExtrusion;
    const bool opening = BuildingGeometryService::isOpeningKind(kind);
    m_endX->setEnabled(lineBased); m_endY->setEnabled(lineBased);
    m_baseline->setEnabled(kind == FcGeometryKind::Wall);
    m_profile->setEnabled(polygon);
    m_path->setEnabled(kind == FcGeometryKind::PathSweep ||
                       kind == FcGeometryKind::PolylineSweep ||
                       kind == FcGeometryKind::Wall);
    m_steps->setEnabled(kind == FcGeometryKind::Stair);
    m_rise->setEnabled(kind == FcGeometryKind::Stair || kind == FcGeometryKind::Ramp);
    m_hostCombo->setEnabled(opening);
    m_dynamic->setEnabled(opening);
    m_controlCombo->setEnabled(opening && m_dynamic->isChecked());
}

void BuildingElementDialog::updatePreviewSummary()
{
    QString error;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request(), &error);
    if (shape.IsNull()) {
        m_previewSummary->setText(t("Preview unavailable: ") + error);
        return;
    }
    updateTopologyFaceEditors(shape);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    const GeometryValidationResult quality = BuildingGeometryService::validate(shape);
    QStringList details;
    details.append(t("Valid closed geometry. Estimated volume: %1 m³. Faces: %2. Vertices: %3.")
                       .arg(properties.Mass(), 0, 'g', 8)
                       .arg(quality.faceCount)
                       .arg(quality.vertexCount));
    details.append(quality.warnings);
    details.append(quality.errors);
    details.append(t("The object will keep its source parameters, UUID, and topological-face surface identities when saved."));
    m_previewSummary->setText(
        details.join(QLatin1Char('\n')));
}

QVector<QPointF> BuildingElementDialog::parsePoints(const QString& text) const
{
    QVector<QPointF> points;
    for (const QString& token : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList coordinates = token.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (coordinates.size() != 2) continue;
        bool xOk = false, yOk = false;
        const double x = coordinates[0].trimmed().toDouble(&xOk);
        const double y = coordinates[1].trimmed().toDouble(&yOk);
        if (xOk && yOk) points.append(QPointF(fromDisplay(x), fromDisplay(y)));
    }
    return points;
}

QString BuildingElementDialog::formatPoints(const QVector<QPointF>& points) const
{
    QStringList values;
    for (const QPointF& point : points) {
        values.append(QStringLiteral("%1,%2").arg(toDisplay(point.x()), 0, 'g', 10)
                          .arg(toDisplay(point.y()), 0, 'g', 10));
    }
    return values.join(QStringLiteral("; "));
}

double BuildingElementDialog::fromDisplay(double value) const
{
    return m_project ? m_project->displayToMeters(value) : value;
}

double BuildingElementDialog::toDisplay(double value) const
{
    return m_project ? m_project->metersToDisplay(value) : value;
}
