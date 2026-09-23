#include "ui/BuildingElementDialog.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsBlockConversionService.h"
#include "geometry/FcGeometryObject.h"
#include "ui/UiLanguage.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

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
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QSet>
#include <cmath>
#include <limits>
#include <QHBoxLayout>

#include <functional>

namespace
{
QString t(const char* text) { return UiLanguageManager::text(QString::fromUtf8(text)); }
constexpr int missingReferenceRole = Qt::UserRole + 1;

void selectReference(QComboBox* combo, const QString& id)
{
    if (!combo) return;
    int index = combo->findData(id);
    if (index < 0 && !id.isEmpty()) {
        const QString shortId = id.size() > 12 ? id.left(12) + QStringLiteral("…") : id;
        combo->addItem(t("Missing reference: %1").arg(shortId), id);
        index = combo->count() - 1;
        combo->setItemData(index, true, missingReferenceRole);
        combo->setItemData(index, id, Qt::ToolTipRole);
    }
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

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
    resize(760, 720);
    auto* layout = new QVBoxLayout(this);
    m_readOnlyReason = new QLabel(this);
    m_readOnlyReason->setWordWrap(true);
    m_readOnlyReason->hide();
    layout->addWidget(m_readOnlyReason);
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("GeometryPropertiesTabs"));
    layout->addWidget(m_tabs, 1);
    const auto makePage = [this](const QString& title, const QString& name) {
        auto* scroll = new QScrollArea(m_tabs);
        scroll->setObjectName(name);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget(scroll);
        scroll->setWidget(content);
        m_tabs->addTab(scroll, title);
        return content;
    };
    auto* generalPage = makePage(t("General"), QStringLiteral("GeometryGeneralPage"));
    auto* general = new QFormLayout(generalPage);
    general->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* formContainer = new QWidget(this);
    auto* form = new QFormLayout(formContainer);
    m_geometryForm = form;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
        // FDS RAMP means a time curve; the geometric ramp has a distinct label.
        m_kindCombo->addItem(kind == FcGeometryKind::Ramp ? t("Ramp geometry")
                            : UiLanguageManager::text(fcGeometryKindName(kind)),
                             static_cast<int>(kind));
    }
    const int initialIndex = m_kindCombo->findData(static_cast<int>(initialKind));
    m_kindCombo->setCurrentIndex(std::max(0, initialIndex));
    m_description = new QLineEdit(this);
    m_description->setObjectName(QStringLiteral("GeometryDescriptionEdit"));
    m_groupCombo = new QComboBox(this);
    m_groupCombo->setObjectName(QStringLiteral("GeometryGroupCombo"));
    m_customColor = new QCheckBox(t("Use custom display color"), this);
    m_customColor->setObjectName(QStringLiteral("GeometryCustomColorCheck"));
    m_colorButton = new QPushButton(this);
    m_colorButton->setObjectName(QStringLiteral("GeometryColorButton"));
    m_outline = new QCheckBox(t("Show feature outlines (display only)"), this);
    m_outline->setObjectName(QStringLiteral("GeometryOutlineCheck"));
    m_bounds = new QLabel(this);
    m_bounds->setObjectName(QStringLiteral("GeometryBoundsSummary"));
    m_bounds->setWordWrap(true);
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
    m_rotation = lengthSpin(this, QStringLiteral("°"));
    m_rotation->setObjectName(QStringLiteral("GeometryRotationSpin"));
    m_extrusionDistance = lengthSpin(this, suffix, 0.000001);
    m_extrusionDistance->setObjectName(QStringLiteral("GeometryExtrusionDistanceSpin"));
    m_extrusionMode = new QComboBox(this);
    m_extrusionMode->setObjectName(QStringLiteral("GeometryExtrusionModeCombo"));
    m_extrusionMode->addItem(t("Normal to profile plane"), true);
    m_extrusionMode->addItem(t("Custom direction (unitless vector)"), false);
    for (int axis = 0; axis < 3; ++axis) {
        m_direction[axis] = lengthSpin(this, QString());
        m_direction[axis]->setObjectName(QStringLiteral("GeometryExtrusionDirection%1Spin").arg(axis));
        m_direction[axis]->setValue(axis == 2 ? 1.0 : 0.0);
    }
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

    general->addRow(t("Name:"), m_nameEdit);
    general->addRow(t("Description:"), m_description);
    general->addRow(t("Group:"), m_groupCombo);
    general->addRow(m_customColor, m_colorButton);
    general->addRow(m_outline);
    auto* textureNote = new QLabel(t("Texture coordinates are not edited here: the current viewer does not render texture maps."), this);
    textureNote->setWordWrap(true);
    general->addRow(textureNote);
    general->addRow(t("Calculated bounds (m):"), m_bounds);
    auto* physics = new QGroupBox(t("OBST solver options"), this);
    auto* physicsForm = new QFormLayout(physics);
    for (const auto& item : QList<QPair<QString, QString>>{
             {QStringLiteral("THICKEN"), t("Thicken grid-thin obstructions")},
             {QStringLiteral("BNDF_OBST"), t("Record boundary output")},
             {QStringLiteral("PERMIT_HOLE"), t("Permit holes")},
             {QStringLiteral("ALLOW_VENT"), t("Allow vents")},
             {QStringLiteral("REMOVABLE"), t("Allow FDS to remove obstruction")}}) {
        auto* check = new QCheckBox(item.second, this);
        check->setObjectName(QStringLiteral("GeometryFds_%1").arg(item.first));
        check->setTristate(true);
        check->setCheckState(Qt::PartiallyChecked);
        check->setToolTip(t("Partially checked: use the FDS default; checked: true; unchecked: false."));
        m_physicsChecks.insert(item.first, check);
        physicsForm->addRow(check);
    }
    m_densityEnabled = new QCheckBox(t("Bulk density:"), this);
    m_densityEnabled->setObjectName(QStringLiteral("GeometryBulkDensityEnabled"));
    m_density = lengthSpin(this, QStringLiteral("kg/m³"), 0.000001);
    m_density->setObjectName(QStringLiteral("GeometryBulkDensitySpin"));
    m_density->setValue(1.0);
    m_density->setEnabled(false);
    physicsForm->addRow(m_densityEnabled, m_density);
    connect(m_densityEnabled, &QCheckBox::toggled, m_density, &QWidget::setEnabled);
    general->addRow(physics);
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
    form->addRow(t("Rotation about Z:"), m_rotation);
    form->addRow(t("FDS conversion route:"), m_fdsConversionRoute);
    form->addRow(t("2D profile (x,y; ...):"), m_profile);
    form->addRow(t("2D path (x,y; ...):"), m_path);
    m_profileGroup = new QGroupBox(t("Profile vertices (world coordinates)"), this);
    auto* profileLayout = new QVBoxLayout(m_profileGroup);
    m_profileTable = new QTableWidget(0, 3, m_profileGroup);
    m_profileTable->setObjectName(QStringLiteral("GeometryProfilePointsTable"));
    m_profileTable->setHorizontalHeaderLabels({QStringLiteral("X (%1)").arg(suffix), QStringLiteral("Y (%1)").arg(suffix), QStringLiteral("Z (%1)").arg(suffix)});
    m_profileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_profileTable->setMinimumHeight(170);
    profileLayout->addWidget(m_profileTable);
    addTableTools(m_profileTable, profileLayout, true);
    auto* extrusionForm = new QFormLayout;
    extrusionForm->addRow(t("Extrusion:"), m_extrusionMode);
    extrusionForm->addRow(t("Extrusion distance:"), m_extrusionDistance);
    extrusionForm->addRow(t("Direction X:"), m_direction[0]);
    extrusionForm->addRow(t("Direction Y:"), m_direction[1]);
    extrusionForm->addRow(t("Direction Z:"), m_direction[2]);
    profileLayout->addLayout(extrusionForm);
    form->addRow(m_profileGroup);
    form->addRow(t("Opening host:"), m_hostCombo);
    general->addRow(t("Activation control / device:"), m_controlCombo);
    general->addRow(QString(), m_dynamic);
    auto* surfacePage = makePage(t("Surfaces"), QStringLiteral("GeometrySurfacesPage"));
    auto* surfaces = new QFormLayout(surfacePage);
    m_surfaceMode = new QComboBox(this);
    m_surfaceMode->setObjectName(QStringLiteral("GeometrySurfaceModeCombo"));
    m_surfaceMode->addItem(t("One surface for all faces"), false);
    m_surfaceMode->addItem(t("Individual face overrides"), true);
    surfaces->addRow(t("Assignment:"), m_surfaceMode);
    surfaces->addRow(t("Default FDS surface:"), m_surfaceCombo);
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
        surfaces->addRow(face.second, combo);
    }
    m_topologyFaceGroup = new QGroupBox(t("Topological face overrides"), this);
    m_topologyFaceGroup->setObjectName(QStringLiteral("TopologyFaceSurfaceGroup"));
    m_topologyFaceForm = new QFormLayout(m_topologyFaceGroup);
    auto* topologyHelp = new QLabel(
        t("Each entry identifies a real OpenCascade face by a stable geometric signature. These overrides are used by native FDS GEOM triangulation."),
        m_topologyFaceGroup);
    topologyHelp->setWordWrap(true);
    m_topologyFaceForm->addRow(topologyHelp);
    surfaces->addRow(m_topologyFaceGroup);
    auto* clearFaces = new QPushButton(t("Clear obsolete face assignments"), this);
    clearFaces->setObjectName(QStringLiteral("ClearObsoleteFaceAssignmentsButton"));
    surfaces->addRow(clearFaces);
    connect(clearFaces, &QPushButton::clicked, this, [this]() {
        updatePreviewSummary();
        m_unresolvedFaces.clear();
        updatePreviewSummary();
    });
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("BuildingElementScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(formContainer);
    m_tabs->insertTab(1, scrollArea, t("Geometry"));
    auto* advancedPage = makePage(t("Advanced"), QStringLiteral("GeometryAdvancedPage"));
    auto* advanced = new QVBoxLayout(advancedPage);
    auto* advancedHelp = new QLabel(t("Additional OBST fields apply only to the OBST conversion route. Geometry, surfaces and references are managed by their dedicated controls.") +
        QStringLiteral("\nBNDF_OBST, THICKEN, PERMIT_HOLE, ALLOW_VENT, REMOVABLE: .TRUE. / .FALSE.\nBULK_DENSITY: kg/m³ > 0"), this);
    advancedHelp->setWordWrap(true);
    advanced->addWidget(advancedHelp);
    m_advancedTable = new QTableWidget(0, 2, this);
    m_advancedTable->setObjectName(QStringLiteral("GeometryAdvancedFieldsTable"));
    m_advancedTable->setHorizontalHeaderLabels({t("Field"), t("Value")});
    m_advancedTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    advanced->addWidget(m_advancedTable);
    addTableTools(m_advancedTable, advanced, false);
    auto* previewButton = new QPushButton(t("Validate and Preview"), this);
    previewButton->setObjectName(QStringLiteral("ValidateGeometryPreviewButton"));
    layout->addWidget(previewButton);
    layout->addWidget(m_previewSummary);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons = buttons;
    buttons->button(QDialogButtonBox::Ok)->setText(t("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(t("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &BuildingElementDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &BuildingElementDialog::reject);
    connect(previewButton, &QPushButton::clicked, this,
            [this]() { updatePreviewSummary(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this,
            [this]() {
                const auto value = request();
                if (!m_spatialProfile && (value.kind == FcGeometryKind::PolygonPrism || value.kind == FcGeometryKind::ProfileExtrusion))
                    populateProfileTable(value);
                updateFieldAvailability(); updatePreviewSummary();
            });
    connect(m_baseline, &QComboBox::currentIndexChanged, this,
            [this]() { handleBaselineChanged(); });
    connect(m_dynamic, &QCheckBox::toggled, this,
            [this]() { updateFieldAvailability(); });
    connect(m_surfaceMode, &QComboBox::currentIndexChanged, this,
            [this]() { updateFieldAvailability(); });
    connect(m_extrusionMode, &QComboBox::currentIndexChanged, this,
            [this]() { if (!m_updatingProfile) m_spatialProfile = true; updateFieldAvailability(); });
    connect(m_extrusionDistance, &QDoubleSpinBox::valueChanged, this,
            [this]() { if (!m_updatingProfile) { m_spatialProfile = true; updateFieldAvailability(); } });
    for (QDoubleSpinBox* direction : m_direction)
        connect(direction, &QDoubleSpinBox::valueChanged, this,
                [this]() { if (!m_updatingProfile) { m_spatialProfile = true; updateFieldAvailability(); } });
    connect(m_profileTable, &QTableWidget::itemChanged, this, [this]() {
        if (!m_updatingProfile) { m_spatialProfile = true; updateFieldAvailability(); }
    });
    connect(m_profileTable->model(), &QAbstractItemModel::rowsRemoved, this, [this]() {
        if (!m_updatingProfile) { m_spatialProfile = true; updateFieldAvailability(); }
    });
    connect(m_profileTable->model(), &QAbstractItemModel::rowsInserted, this, [this]() {
        if (!m_updatingProfile) { m_spatialProfile = true; updateFieldAvailability(); }
    });
    connect(m_profile, &QLineEdit::textEdited, this, [this]() {
        if (m_updatingProfile) return;
        m_spatialProfile = false;
        BuildingGeometryRequest value = request();
        value.profile3d.clear();
        populateProfileTable(value);
    });
    connect(m_customColor, &QCheckBox::toggled, this, [this]() { updateColorButton(); });
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(QColor(m_color), this, t("Display color"));
        if (color.isValid()) { m_color = color.name(); updateColorButton(); }
    });
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
    m_extrusionDistance->setValue(toDisplay(3.0));
    m_spatialProfile = false;
    populateProfileTable(request());
    updateColorButton();
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
    m_rotation->setValue(value.rotationDegrees);
    populateMetadata(value.extraParameters);
    populateProfileTable(value);
    if (object->parent()) selectReference(m_groupCombo, object->parent()->id());
    m_baseline->setCurrentIndex(m_baseline->findData(static_cast<int>(value.baseline)));
    m_fdsConversionRoute->setCurrentIndex(
        m_fdsConversionRoute->findData(value.fdsConversionRoute));
    m_lastBaseline = value.baseline;
    m_editingWall = value.kind == FcGeometryKind::Wall;
    m_profile->setText(formatPoints(value.profile));
    m_path->setText(formatPoints(value.path));
    selectReference(m_hostCombo, object->hostObjectId());
    selectReference(m_controlCombo, object->controlObjectId());
    selectReference(m_surfaceCombo, object->defaultSurfaceId());
    for (auto iterator = m_faceSurfaceCombos.begin();
         iterator != m_faceSurfaceCombos.end(); ++iterator) {
        selectReference(iterator.value(), object->faceSurfaceIds().value(iterator.key()));
    }
    m_dynamic->setChecked(object->isDynamicOpening());
    m_surfaceMode->setCurrentIndex(object->faceSurfaceIds().isEmpty() ? 0 : 1);
    updateTopologyFaceEditors(object->shape(), object->faceSurfaceIds());
    updateFieldAvailability();
    updatePreviewSummary();
    if (object->isLocked()) setReadOnly(t("This object is locked. Its properties are read-only."));
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
    m_rotation->setValue(value.rotationDegrees);
    populateMetadata(value.extraParameters);
    populateProfileTable(value);
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
    value.extraParameters = m_extraParameters;
    value.extraParameters.insert(QStringLiteral("description"), description());
    value.extraParameters.insert(QStringLiteral("displayOutline"), m_outline->isChecked());
    if (m_customColor->isChecked()) value.extraParameters.insert(QStringLiteral("displayColor"), m_color);
    else value.extraParameters.remove(QStringLiteral("displayColor"));
    value.extraParameters.insert(QStringLiteral("fdsAdditionalFields"), additionalFields());
    if (m_project && m_project->document()) {
        const auto control = m_project->document()->findObject(controlObjectId());
        value.extraParameters.insert(QStringLiteral("activationReferenceKind"),
            control && control->type() == FcObjectType::Device ? QStringLiteral("DEVC_ID") : QStringLiteral("CTRL_ID"));
    }
    value.kind = static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt());
    value.x = fromDisplay(m_x->value()); value.y = fromDisplay(m_y->value());
    value.z = fromDisplay(m_z->value()); value.endX = fromDisplay(m_endX->value());
    value.endY = fromDisplay(m_endY->value()); value.width = fromDisplay(m_width->value());
    value.depth = fromDisplay(m_depth->value()); value.height = fromDisplay(m_height->value());
    value.thickness = fromDisplay(m_thickness->value());
    value.radius = fromDisplay(m_radius->value()); value.rise = fromDisplay(m_rise->value());
    value.stepCount = m_steps->value();
    value.rotationDegrees = m_rotation->value();
    value.baseline = static_cast<FcWallBaseline>(m_baseline->currentData().toInt());
    value.fdsConversionRoute = m_fdsConversionRoute->currentData().toString();
    value.profile = parsePoints(m_profile->text());
    value.path = parsePoints(m_path->text());
    if (m_spatialProfile && (value.kind == FcGeometryKind::PolygonPrism || value.kind == FcGeometryKind::ProfileExtrusion)) {
        for (int row = 0; row < m_profileTable->rowCount(); ++row) {
            std::array<double, 3> point{};
            for (int column = 0; column < 3; ++column) {
                bool ok = false;
                const auto* item = m_profileTable->item(row, column);
                const double coordinate = item ? item->text().toDouble(&ok) : 0.0;
                point[column] = ok ? fromDisplay(coordinate) : std::numeric_limits<double>::quiet_NaN();
            }
            value.profile3d.append(point);
        }
    }
    value.extrusionNormal = m_extrusionMode->currentData().toBool();
    value.extrusionDistance = fromDisplay(m_extrusionDistance->value());
    for (int axis = 0; axis < 3; ++axis) value.extrusionDirection[axis] = m_direction[axis]->value();
    return value;
}

QString BuildingElementDialog::hostObjectId() const
{
    return BuildingGeometryService::isOpeningKind(static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt()))
        ? m_hostCombo->currentData().toString() : QString();
}
QString BuildingElementDialog::controlObjectId() const
{
    const bool opening = BuildingGeometryService::isOpeningKind(static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt()));
    return opening && !m_dynamic->isChecked() ? QString() : m_controlCombo->currentData().toString();
}
QString BuildingElementDialog::surfaceObjectId() const { return m_surfaceCombo->currentData().toString(); }
QMap<QString, QString> BuildingElementDialog::faceSurfaceIds() const
{
    QMap<QString, QString> assignments;
    if (m_surfaceMode->currentIndex() == 0) return assignments;
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
bool BuildingElementDialog::dynamicOpening() const
{
    return BuildingGeometryService::isOpeningKind(static_cast<FcGeometryKind>(m_kindCombo->currentData().toInt())) && m_dynamic->isChecked();
}
QString BuildingElementDialog::groupObjectId() const { return m_groupCombo->currentData().toString(); }
QString BuildingElementDialog::description() const { return m_description->text(); }

void BuildingElementDialog::accept()
{
    if (m_readOnly) { reject(); return; }
    QString error;
    updatePreviewSummary();
    if (!validateInput(&error)) {
        QMessageBox::warning(this, t("Invalid Geometry"), error);
        return;
    }
    QDialog::accept();
}

bool BuildingElementDialog::validateInput(QString* error) const
{
    const auto fail = [error](const QString& text) { if (error) *error = text; return false; };
    if (geometryName().isEmpty()) return fail(t("Enter a geometry name."));
    for (const QComboBox* combo : {m_surfaceCombo, m_hostCombo, m_controlCombo, m_groupCombo})
        if (combo->currentData(missingReferenceRole).toBool())
            return fail(t("A referenced object is missing or incompatible. Choose a valid replacement or explicitly select None before applying."));
    for (const QLineEdit* edit : {m_path, m_profile}) {
        if (!edit->isEnabled() || edit->text().trimmed().isEmpty()) continue;
        for (const QString& token : edit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QStringList parts = token.split(QLatin1Char(','));
            bool valid = parts.size() == 2;
            for (const QString& part : parts) {
                bool ok = false;
                const double number = part.trimmed().toDouble(&ok);
                valid = valid && ok && std::isfinite(number);
            }
            if (!valid) return fail(t("Each profile or path point must contain two valid numeric coordinates."));
        }
    }
    QString geometryError;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request(), &geometryError);
    if (shape.IsNull())
        return fail(UiLanguageManager::text(geometryError));
    if (BuildingGeometryService::isOpeningKind(request().kind) && hostObjectId().isEmpty()) {
        return fail(t("Choose a host wall, slab, or roof for the opening."));
    }
    if (dynamicOpening() && controlObjectId().isEmpty()) {
        return fail(t("Choose a CTRL or DEVC object for a dynamic opening."));
    }
    QString fieldError;
    const QVariantMap fields = additionalFields(&fieldError);
    if (!fieldError.isEmpty()) return fail(fieldError);
    const QString route = request().fdsConversionRoute.toUpper();
    if (!fields.isEmpty() && route != QStringLiteral("OBST"))
        return fail(t("Select the explicit OBST conversion route to use additional OBST fields."));
    if (m_surfaceMode->currentIndex() != 0 && !m_unresolvedFaces.isEmpty())
        return fail(t("Geometry changes invalidated face assignments. Clear obsolete assignments and assign the new faces before applying."));
    if (m_surfaceMode->currentIndex() != 0) {
        QSet<QString> keys;
        for (const auto& face : BuildingGeometryService::faceInfos(shape)) keys.insert(face.key);
        const auto assignments = faceSurfaceIds();
        for (auto item = assignments.cbegin(); item != assignments.cend(); ++item)
            if (item.key().startsWith(QStringLiteral("TopoFace:")) && !keys.contains(item.key()))
                return fail(t("Geometry changes invalidated face assignments. Clear obsolete assignments and assign the new faces before applying."));
    }
    if (m_project && m_project->document()) {
        const auto document = m_project->document();
        const QString defaultSurface = m_surfaceCombo->currentData().toString();
        if (!defaultSurface.isEmpty()) {
            const auto surface = document->findObject(defaultSurface);
            if (!surface || surface->type() != FcObjectType::Surface)
                return fail(t("A selected surface no longer exists."));
        }
        const QString selectedHost = m_hostCombo->currentData().toString();
        if (!selectedHost.isEmpty()) {
            const auto host = std::dynamic_pointer_cast<FcGeometryObject>(document->findObject(selectedHost));
            if (!host || BuildingGeometryService::isOpeningKind(host->geometryKind()))
                return fail(t("A referenced object is missing or incompatible. Choose a valid replacement or explicitly select None before applying."));
        }
        const QString selectedGroup = m_groupCombo->currentData().toString();
        if (!selectedGroup.isEmpty()) {
            const auto group = document->findObject(selectedGroup);
            if (!group || (group->type() != FcObjectType::Group && group->type() != FcObjectType::Folder && group->type() != FcObjectType::Floor))
                return fail(t("A referenced object is missing or incompatible. Choose a valid replacement or explicitly select None before applying."));
        }
        if (!controlObjectId().isEmpty()) {
            const auto control = document->findObject(controlObjectId());
            if (!control || (control->type() != FcObjectType::Control && control->type() != FcObjectType::Device))
                return fail(t("The selected activation control or device no longer exists."));
        }
        for (const QString& id : faceSurfaceIds())
            if (!document->findObject(id) || document->findObject(id)->type() != FcObjectType::Surface)
                return fail(t("A selected surface no longer exists."));
    }
    FcGeometryObject surfaceCandidate(geometryName(), shape);
    const auto candidateRequest = request();
    surfaceCandidate.setGeometryKind(candidateRequest.kind);
    surfaceCandidate.setGeometryParameters(BuildingGeometryService::requestToParameters(candidateRequest));
    surfaceCandidate.setDefaultSurfaceId(surfaceObjectId());
    surfaceCandidate.setFaceSurfaceIds(faceSurfaceIds());
    QStringList surfaceErrors = FdsBlockConversionService::validateSurfaceAssignments(surfaceCandidate);
    if (!surfaceErrors.isEmpty()) {
        for (QString& message : surfaceErrors) message = UiLanguageManager::text(message);
        return fail(surfaceErrors.join(QLatin1Char('\n')));
    }
    if (error) error->clear();
    return true;
}

void BuildingElementDialog::populateReferences()
{
    m_hostCombo->clear(); m_controlCombo->clear(); m_surfaceCombo->clear();
    m_hostCombo->addItem(t("None"), QString());
    m_controlCombo->addItem(t("None"), QString());
    m_surfaceCombo->addItem(t("None"), QString());
    m_groupCombo->clear();
    m_groupCombo->addItem(t("Keep current group"), QString());
    for (QComboBox* combo : m_faceSurfaceCombos) {
        populateSurfaceCombo(combo, true);
    }
    if (!m_project || !m_project->document()) return;
    const auto geometryRoot = m_project->document()->geometryGroup();
    m_groupCombo->addItem(geometryRoot->name(), geometryRoot->id());
    addObjects(m_groupCombo, geometryRoot, [](const FcObject::Ptr& object) {
        return object->type() == FcObjectType::Group || object->type() == FcObjectType::Folder || object->type() == FcObjectType::Floor;
    });
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
    selectReference(combo, selected);
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
    for (auto assignment = selected.cbegin(); assignment != selected.cend(); ++assignment)
        if (assignment.key().startsWith(QStringLiteral("TopoFace:")) && !newKeys.contains(assignment.key()))
            m_unresolvedFaces.insert(assignment.key(), assignment.value());
    newKeys.sort();
    if (newKeys == m_topologyFaceSurfaceCombos.keys()) {
        for (auto iterator = selected.cbegin(); iterator != selected.cend(); ++iterator) {
            QComboBox* combo = m_topologyFaceSurfaceCombos.value(iterator.key());
            if (combo) selectReference(combo, iterator.value());
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
        selectReference(combo, selected.value(face.key));
        combo->setToolTip(face.key);
        m_topologyFaceSurfaceCombos.insert(face.key, combo);
        auto* locate = new QPushButton(t("Face %1 · %2 m²").arg(face.faceIndex).arg(face.area, 0, 'g', 5), m_topologyFaceGroup);
        locate->setObjectName(QStringLiteral("GeometryFacePreviewButton"));
        locate->setToolTip(t("Highlight this face in the model view"));
        connect(locate, &QPushButton::clicked, this, [this, key = face.key]() { emit facePreviewRequested(key); });
        connect(combo, &QComboBox::activated, this, [this, key = face.key]() { emit facePreviewRequested(key); });
        m_topologyFaceForm->addRow(locate, combo);
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
    const bool spatial = kind == FcGeometryKind::PolygonPrism || kind == FcGeometryKind::ProfileExtrusion;
    const bool sweep = kind == FcGeometryKind::PolylineSweep || kind == FcGeometryKind::PathSweep;
    const bool round = kind == FcGeometryKind::Cylinder || kind == FcGeometryKind::Column;
    const bool box = kind == FcGeometryKind::Box || kind == FcGeometryKind::Slab ||
        kind == FcGeometryKind::Roof || kind == FcGeometryKind::RectangleProfile ||
        (opening && kind != FcGeometryKind::PolygonalOpening);
    const auto row = [this](QWidget* field, bool visible) { m_geometryForm->setRowVisible(field, visible); };
    row(m_x, !polygon && !sweep); row(m_y, !polygon && !sweep);
    row(m_z, !spatial || !m_spatialProfile);
    row(m_endX, lineBased && !sweep); row(m_endY, lineBased && !sweep);
    row(m_width, box || kind == FcGeometryKind::Column || kind == FcGeometryKind::Beam ||
        kind == FcGeometryKind::Stair || kind == FcGeometryKind::Ramp || kind == FcGeometryKind::Room);
    row(m_depth, box || kind == FcGeometryKind::Column || kind == FcGeometryKind::Stair ||
        kind == FcGeometryKind::Ramp || kind == FcGeometryKind::Room);
    row(m_height, box || round || kind == FcGeometryKind::Wall || sweep ||
        kind == FcGeometryKind::Room || kind == FcGeometryKind::PolygonalOpening || (spatial && !m_spatialProfile));
    if (auto* label = qobject_cast<QLabel*>(m_geometryForm->labelForField(m_height)))
        label->setText(kind == FcGeometryKind::Slab || kind == FcGeometryKind::Roof || kind == FcGeometryKind::RectangleProfile ? t("Thickness:") : t("Height:"));
    row(m_thickness, kind == FcGeometryKind::Wall || kind == FcGeometryKind::Beam || sweep || kind == FcGeometryKind::Room);
    row(m_radius, round); row(m_rise, kind == FcGeometryKind::Stair || kind == FcGeometryKind::Ramp);
    row(m_steps, kind == FcGeometryKind::Stair); row(m_baseline, kind == FcGeometryKind::Wall);
    row(m_rotation, box); row(m_profile, kind == FcGeometryKind::PolygonalOpening);
    row(m_path, kind == FcGeometryKind::Wall || sweep);
    row(m_hostCombo, opening || m_hostCombo->currentData(missingReferenceRole).toBool());
    m_endX->setEnabled(lineBased); m_endY->setEnabled(lineBased);
    m_baseline->setEnabled(kind == FcGeometryKind::Wall);
    m_profile->setEnabled(kind == FcGeometryKind::PolygonalOpening);
    m_profileGroup->setVisible(kind == FcGeometryKind::PolygonPrism || kind == FcGeometryKind::ProfileExtrusion);
    for (QDoubleSpinBox* direction : m_direction)
        direction->setEnabled(!m_extrusionMode->currentData().toBool());
    m_path->setEnabled(kind == FcGeometryKind::PathSweep ||
                       kind == FcGeometryKind::PolylineSweep ||
                       kind == FcGeometryKind::Wall);
    m_steps->setEnabled(kind == FcGeometryKind::Stair);
    m_rise->setEnabled(kind == FcGeometryKind::Stair || kind == FcGeometryKind::Ramp);
    m_hostCombo->setEnabled(opening || m_hostCombo->currentData(missingReferenceRole).toBool());
    m_dynamic->setEnabled(opening);
    m_controlCombo->setEnabled(!opening || m_dynamic->isChecked() || m_controlCombo->currentData(missingReferenceRole).toBool());
    const bool overrides = m_surfaceMode->currentIndex() != 0;
    for (QComboBox* combo : m_faceSurfaceCombos) combo->setEnabled(overrides);
    if (m_topologyFaceGroup) m_topologyFaceGroup->setEnabled(overrides);
    m_height->setEnabled(!polygon || kind == FcGeometryKind::PolygonalOpening || !m_spatialProfile);
    if (m_readOnly) setReadOnly(m_readOnlyReason->text());
}

void BuildingElementDialog::updatePreviewSummary()
{
    QString error;
    const TopoDS_Shape shape = BuildingGeometryService::createShape(request(), &error);
    if (shape.IsNull()) {
        m_previewSummary->setText(t("Preview unavailable: ") + UiLanguageManager::text(error));
        return;
    }
    updateTopologyFaceEditors(shape);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    const GeometryValidationResult quality = BuildingGeometryService::validate(shape);
    QStringList details;
    details.append((quality.valid && quality.closed
                        ? t("Valid closed geometry. Estimated volume: %1 m³. Faces: %2. Vertices: %3.")
                        : t("Geometry requires attention. Estimated volume: %1 m³. Faces: %2. Vertices: %3."))
                       .arg(properties.Mass(), 0, 'g', 8)
                       .arg(quality.faceCount)
                       .arg(quality.vertexCount));
    const auto translatedDetail = [&quality](const QString& message) {
        const QString translated = UiLanguageManager::text(message);
        if (translated != message) return translated;
        // The service formats counts before returning them. Restore only its
        // known templates instead of trying to translate arbitrary substrings.
        const QString vertices = QStringLiteral("Geometry topology contains %1 coincident vertex occurrence(s); repair can merge them.");
        if (message == vertices.arg(quality.duplicateVertexCount))
            return UiLanguageManager::text(vertices).arg(quality.duplicateVertexCount);
        const QString faces = QStringLiteral("Geometry contains %1 duplicate topological face(s).");
        if (message == faces.arg(quality.duplicateFaceCount))
            return UiLanguageManager::text(faces).arg(quality.duplicateFaceCount);
        const QString prefix = QStringLiteral("Geometry validation failed: ");
        if (message.startsWith(prefix))
            return t("Geometry validation failed: %1").arg(message.mid(prefix.size()));
        return message;
    };
    for (const QString& warning : quality.warnings) details.append(translatedDetail(warning));
    for (const QString& errorDetail : quality.errors) details.append(translatedDetail(errorDetail));
    if (!m_unresolvedFaces.isEmpty() && m_surfaceMode->currentIndex() != 0)
        details.append(t("Geometry changes invalidated face assignments. Clear obsolete assignments and assign the new faces before applying."));
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (!box.IsVoid()) {
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        m_bounds->setText(QStringLiteral("X: %1 … %2\nY: %3 … %4\nZ: %5 … %6")
            .arg(xmin, 0, 'g', 8).arg(xmax, 0, 'g', 8).arg(ymin, 0, 'g', 8)
            .arg(ymax, 0, 'g', 8).arg(zmin, 0, 'g', 8).arg(zmax, 0, 'g', 8));
    }
    m_previewSummary->setText(
        details.join(QLatin1Char('\n')));
    emit geometryPreviewRequested(shape);
}

void BuildingElementDialog::setReadOnly(const QString& reason)
{
    m_readOnly = true;
    m_readOnlyReason->setText(reason);
    m_readOnlyReason->show();
    // Read-only means no mutation, not an inert notebook: tabs, scrolling,
    // text selection and face inspection must remain available.
    m_topologyFaceGroup->setEnabled(true);
    for (QLineEdit* edit : m_tabs->findChildren<QLineEdit*>()) edit->setReadOnly(true);
    for (QAbstractSpinBox* spin : m_tabs->findChildren<QAbstractSpinBox*>()) spin->setReadOnly(true);
    for (QComboBox* combo : m_tabs->findChildren<QComboBox*>()) combo->setEnabled(false);
    for (QCheckBox* check : m_tabs->findChildren<QCheckBox*>()) check->setEnabled(false);
    for (QTableWidget* table : m_tabs->findChildren<QTableWidget*>()) table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (QPushButton* button : m_tabs->findChildren<QPushButton*>())
        button->setEnabled(button->objectName() == QStringLiteral("GeometryFacePreviewButton"));
    m_buttons->button(QDialogButtonBox::Ok)->hide();
    m_buttons->button(QDialogButtonBox::Cancel)->setText(t("Close"));
}

void BuildingElementDialog::updateColorButton()
{
    m_colorButton->setText(m_color);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(
        m_color, QColor(m_color).lightness() > 128 ? QStringLiteral("black") : QStringLiteral("white")));
    m_colorButton->setEnabled(m_customColor->isChecked());
}

void BuildingElementDialog::populateProfileTable(const BuildingGeometryRequest& value)
{
    m_updatingProfile = true;
    const QSignalBlocker block(m_profileTable);
    QVector<std::array<double, 3>> points = value.profile3d;
    if (points.isEmpty()) for (const QPointF& point : value.profile)
        points.append({point.x(), point.y(), value.z});
    m_profileTable->setRowCount(static_cast<int>(points.size()));
    for (int row = 0; row < points.size(); ++row)
        for (int column = 0; column < 3; ++column)
            m_profileTable->setItem(row, column, new QTableWidgetItem(QString::number(toDisplay(points[row][column]), 'g', 12)));
    const bool hasSpatialData = !value.profile3d.isEmpty();
    m_spatialProfile = (value.kind == FcGeometryKind::PolygonPrism || value.kind == FcGeometryKind::ProfileExtrusion) && !points.isEmpty();
    // Legacy profiles always extruded in +Z, independent of point winding.
    // Promote them to the XYZ table without silently changing that direction.
    m_extrusionMode->setCurrentIndex(hasSpatialData && value.extrusionNormal ? 0 : 1);
    m_extrusionDistance->setValue(toDisplay(hasSpatialData ? value.extrusionDistance : value.height));
    for (int axis = 0; axis < 3; ++axis)
        m_direction[axis]->setValue(hasSpatialData ? value.extrusionDirection[axis] : (axis == 2 ? 1.0 : 0.0));
    m_updatingProfile = false;
}

void BuildingElementDialog::populateMetadata(const QVariantMap& parameters)
{
    m_extraParameters = parameters;
    m_description->setText(parameters.value(QStringLiteral("description")).toString());
    const QString color = parameters.value(QStringLiteral("displayColor")).toString();
    m_customColor->setChecked(QColor::isValidColorName(color));
    if (m_customColor->isChecked()) m_color = color;
    m_outline->setChecked(parameters.value(QStringLiteral("displayOutline"), false).toBool());
    updateColorButton();
    QVariantMap fields = parameters.value(QStringLiteral("fdsAdditionalFields")).toMap();
    for (auto item = m_physicsChecks.begin(); item != m_physicsChecks.end(); ++item) {
        const QString raw = fields.take(item.key()).toString().trimmed().toUpper();
        if (raw == QStringLiteral(".TRUE.") || raw == QStringLiteral("TRUE")) item.value()->setCheckState(Qt::Checked);
        else if (raw == QStringLiteral(".FALSE.") || raw == QStringLiteral("FALSE")) item.value()->setCheckState(Qt::Unchecked);
        else {
            item.value()->setCheckState(Qt::PartiallyChecked);
            if (!raw.isEmpty()) fields.insert(item.key(), raw);
        }
    }
    const QVariant density = fields.take(QStringLiteral("BULK_DENSITY"));
    bool densityOk = false;
    const double densityValue = density.toDouble(&densityOk);
    densityOk = densityOk && std::isfinite(densityValue) && densityValue > 0.0;
    m_densityEnabled->setChecked(density.isValid() && densityOk);
    if (density.isValid() && densityOk) m_density->setValue(densityValue);
    else if (density.isValid()) fields.insert(QStringLiteral("BULK_DENSITY"), density);
    m_advancedTable->setRowCount(static_cast<int>(fields.size()));
    int row = 0;
    for (auto field = fields.cbegin(); field != fields.cend(); ++field, ++row) {
        m_advancedTable->setItem(row, 0, new QTableWidgetItem(field.key()));
        m_advancedTable->setItem(row, 1, new QTableWidgetItem(field.value().toString()));
    }
}

QVariantMap BuildingElementDialog::additionalFields(QString* error) const
{
    if (error) error->clear();
    QVariantMap fields;
    for (auto item = m_physicsChecks.cbegin(); item != m_physicsChecks.cend(); ++item)
        if (item.value()->checkState() != Qt::PartiallyChecked)
            fields.insert(item.key(), item.value()->isChecked() ? QStringLiteral(".TRUE.") : QStringLiteral(".FALSE."));
    if (m_densityEnabled && m_densityEnabled->isChecked())
        fields.insert(QStringLiteral("BULK_DENSITY"), QString::number(m_density->value(), 'g', 12));
    if (!m_advancedTable) return fields;
    for (int row = 0; row < m_advancedTable->rowCount(); ++row) {
        const QString key = m_advancedTable->item(row, 0) ? m_advancedTable->item(row, 0)->text().trimmed().toUpper() : QString();
        QString raw = m_advancedTable->item(row, 1) ? m_advancedTable->item(row, 1)->text().trimmed() : QString();
        if (key.isEmpty() && raw.isEmpty()) continue;
        QString reason;
        if (fields.contains(key)) reason = t("Duplicate field or conflict with the General page.");
        else if (m_physicsChecks.contains(key)) {
            raw = raw.toUpper();
            if (raw == QStringLiteral("TRUE")) raw = QStringLiteral(".TRUE.");
            if (raw == QStringLiteral("FALSE")) raw = QStringLiteral(".FALSE.");
            if (raw != QStringLiteral(".TRUE.") && raw != QStringLiteral(".FALSE.")) reason = t("Expected .TRUE. or .FALSE.");
        } else if (key == QStringLiteral("BULK_DENSITY")) {
            bool ok = false;
            const double density = raw.toDouble(&ok);
            if (!ok || !std::isfinite(density) || density <= 0.0) reason = t("Expected a positive density in kg/m³.");
        } else reason = t("Unsupported field for this geometry editor. Use the dedicated FDS object editor for other fields.");
        if (!reason.isEmpty()) {
            if (error) *error = t("Advanced row %1: %2").arg(row + 1).arg(reason);
            return fields;
        }
        fields.insert(key, raw);
    }
    return fields;
}

void BuildingElementDialog::addTableTools(QTableWidget* table, QVBoxLayout* layout, bool orderable)
{
    auto* tools = new QHBoxLayout;
    const auto add = [this, tools](const char* label, const std::function<void()>& action) {
        auto* button = new QPushButton(t(label), this);
        tools->addWidget(button);
        connect(button, &QPushButton::clicked, this, action);
        return button;
    };
    add("Insert Row", [table]() {
        const int row = table->currentRow() < 0 ? table->rowCount() : table->currentRow() + 1;
        table->insertRow(row);
        for (int column = 0; column < table->columnCount(); ++column)
            table->setItem(row, column, new QTableWidgetItem(QStringLiteral("0")));
        table->setCurrentCell(row, 0);
    });
    const auto selectedRows = [table]() {
        QSet<int> selected;
        for (auto* item : table->selectedItems()) selected.insert(item->row());
        QList<int> rows = selected.values();
        std::sort(rows.begin(), rows.end());
        return rows;
    };
    const auto remove = [table, selectedRows]() {
        QList<int> rows = selectedRows();
        for (auto row = rows.crbegin(); row != rows.crend(); ++row) table->removeRow(*row);
    };
    add("Remove Row", remove);
    if (orderable) for (const auto& direction : QList<QPair<const char*, int>>{{"Move Up", -1}, {"Move Down", 1}})
        add(direction.first, [table, delta = direction.second]() {
            const int row = table->currentRow(), other = row + delta;
            if (row < 0 || other < 0 || other >= table->rowCount()) return;
            for (int column = 0; column < table->columnCount(); ++column) {
                auto* first = table->takeItem(row, column);
                auto* second = table->takeItem(other, column);
                table->setItem(row, column, second);
                table->setItem(other, column, first);
            }
            table->setCurrentCell(other, 0);
        });
    const auto copy = [table, selectedRows]() {
        QStringList lines;
        for (int row : selectedRows()) {
            QStringList values;
            for (int column = 0; column < table->columnCount(); ++column)
                values.append(table->item(row, column) ? table->item(row, column)->text() : QString());
            lines.append(values.join(QLatin1Char('\t')));
        }
        if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    };
    add("Copy", copy);
    add("Cut", [copy, remove]() { copy(); remove(); });
    add("Paste", [this, table]() {
        const QStringList lines = QApplication::clipboard()->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QList<QStringList> rows;
        for (const QString& line : lines) {
            const QStringList cells = line.trimmed().split(QLatin1Char('\t'));
            if (cells.size() != table->columnCount()) {
                m_previewSummary->setText(t("Paste requires tab-separated columns matching the table."));
                return;
            }
            rows.append(cells);
        }
        int row = table->currentRow() < 0 ? table->rowCount() : table->currentRow();
        for (const QStringList& cells : rows) {
            table->insertRow(row);
            for (int column = 0; column < cells.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(cells[column]));
            ++row;
        }
    });
    layout->addLayout(tools);
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
