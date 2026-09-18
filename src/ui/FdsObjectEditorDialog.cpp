#include "ui/FdsObjectEditorDialog.h"

#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FdsSchema.h"
#include "ui/FdsSchemaEditorWidget.h"
#include "ui/UiLanguage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <utility>

namespace
{
constexpr int ReferenceIdsRole = Qt::UserRole;

class MeshPreviewWidget final : public QWidget
{
public:
    explicit MeshPreviewWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("MeshPreviewWidget"));
        setMinimumSize(260, 250);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(52, 60, 73));

        const double lx = qMax(1.0e-9, property("meshLengthX").toDouble());
        const double ly = qMax(1.0e-9, property("meshLengthY").toDouble());
        const double lz = qMax(1.0e-9, property("meshLengthZ").toDouble());
        const double horizontalMaximum = qMax(lx, ly);
        const double verticalScale = lz / qMax(horizontalMaximum, lz);
        const double xScale = lx / horizontalMaximum;
        const double yScale = ly / horizontalMaximum;

        const QPointF origin(width() * 0.22, height() * 0.76);
        const QPointF vx(width() * 0.48 * xScale, height() * 0.11 * xScale);
        const QPointF vy(width() * 0.24 * yScale, -height() * 0.18 * yScale);
        const QPointF vz(0.0, -height() * 0.52 * qMax(0.22, verticalScale));
        const std::array<QPointF, 8> points = {
            origin, origin + vx, origin + vy, origin + vx + vy,
            origin + vz, origin + vx + vz, origin + vy + vz,
            origin + vx + vy + vz};
        const std::array<std::pair<int, int>, 12> edges = {
            std::pair{0, 1}, std::pair{0, 2}, std::pair{1, 3}, std::pair{2, 3},
            std::pair{4, 5}, std::pair{4, 6}, std::pair{5, 7}, std::pair{6, 7},
            std::pair{0, 4}, std::pair{1, 5}, std::pair{2, 6}, std::pair{3, 7}};

        painter.setPen(QPen(QColor(112, 174, 255), 2.0));
        for (const auto& edge : edges) painter.drawLine(points[edge.first], points[edge.second]);
        painter.setPen(QColor(225, 231, 241));
        painter.drawText(QRectF(8, 8, width() - 16, 36), Qt::AlignLeft | Qt::AlignTop,
                         UiLanguageManager::text(QStringLiteral("Live mesh preview")));
        painter.setPen(QColor(200, 208, 220));
        painter.drawText(QRectF(8, height() - 42, width() - 16, 34),
                         Qt::AlignLeft | Qt::AlignBottom,
                         QStringLiteral("%1 × %2 × %3 m")
                             .arg(lx, 0, 'g', 6)
                             .arg(ly, 0, 'g', 6)
                             .arg(lz, 0, 'g', 6));
    }
};

QString t(const char* english)
{
    return UiLanguageManager::text(QString::fromUtf8(english));
}

QString compactNumber(double value)
{
    if (qAbs(value) < 1.0e-12) value = 0.0;
    return QString::number(value, 'g', 12);
}

bool parseNumberList(const QString& value, int count, std::vector<double>& numbers)
{
    numbers.clear();
    const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (parts.size() != count) return false;
    for (const QString& part : parts) {
        bool ok = false;
        const double number = part.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(number)) return false;
        numbers.push_back(number);
    }
    return true;
}

FcDocumentGroup defaultGroupForKeyword(const QString& value)
{
    const QString keyword = value.trimmed().toUpper();
    if (keyword == QStringLiteral("MESH") || keyword == QStringLiteral("MULT"))
        return FcDocumentGroup::Meshes;
    if (keyword == QStringLiteral("SPEC")) return FcDocumentGroup::Species;
    if (keyword == QStringLiteral("MATL")) return FcDocumentGroup::Materials;
    if (keyword == QStringLiteral("SURF")) return FcDocumentGroup::Surfaces;
    if (keyword == QStringLiteral("REAC")) return FcDocumentGroup::Reactions;
    if (keyword == QStringLiteral("OBST")) return FcDocumentGroup::Geometry;
    if (keyword == QStringLiteral("VENT")) return FcDocumentGroup::Vents;
    if (keyword == QStringLiteral("PART") || keyword == QStringLiteral("PROP"))
        return FcDocumentGroup::Particles;
    if (keyword == QStringLiteral("DEVC")) return FcDocumentGroup::Devices;
    if (keyword == QStringLiteral("CTRL") || keyword == QStringLiteral("RAMP") ||
        keyword == QStringLiteral("TABL")) return FcDocumentGroup::Controls;
    if (keyword == QStringLiteral("HVAC")) return FcDocumentGroup::HVAC;
    if (keyword == QStringLiteral("INIT")) return FcDocumentGroup::InitialConditions;
    if (keyword == QStringLiteral("SLCF") || keyword == QStringLiteral("BNDF") ||
        keyword == QStringLiteral("ISOF") || keyword == QStringLiteral("PROF") ||
        keyword == QStringLiteral("PL3D") || keyword == QStringLiteral("SM3D") ||
        keyword == QStringLiteral("DUMP")) return FcDocumentGroup::Outputs;
    return FcDocumentGroup::Configuration;
}

QString editorHintForKeyword(const QString& value)
{
    const QString keyword = value.trimmed().toUpper();
    if (keyword == QStringLiteral("MESH"))
        return t("Mesh assistant: use Mesh Settings for normal editing; this advanced page exposes the generated IJK/XB and optional MULT reference.");
    if (keyword == QStringLiteral("MATL"))
        return t("Material assistant: thermal properties must be positive; add reaction parameters in FDS layer order.");
    if (keyword == QStringLiteral("SURF"))
        return t("Surface assistant: MATL_ID stores UUIDs and its layer count must match the THICKNESS array.");
    if (keyword == QStringLiteral("PROP") || keyword == QStringLiteral("TABL"))
        return t("Spray assistant: use UUID references for PART/TABL and keep each TABLE_DATA row as six ordered values.");
    if (keyword == QStringLiteral("HVAC"))
        return t("HVAC assistant: TYPE_ID selects NODE, DUCT, FAN, or AIRCOIL; topology links store UUIDs.");
    if (keyword == QStringLiteral("CTRL"))
        return t("Control assistant: INPUT_ID may select multiple DEVC/CTRL UUIDs; cycles are rejected.");
    return t("Choose a recommended parameter or use the generic table for any supported FDS key.");
}

QString groupName(FcDocumentGroup group)
{
    switch (group) {
    case FcDocumentGroup::Geometry: return t("Geometry");
    case FcDocumentGroup::Meshes: return t("Meshes");
    case FcDocumentGroup::Configuration: return t("Configuration");
    case FcDocumentGroup::Species: return t("Species");
    case FcDocumentGroup::Materials: return t("Materials");
    case FcDocumentGroup::Surfaces: return t("Surfaces");
    case FcDocumentGroup::Reactions: return t("Reactions");
    case FcDocumentGroup::Particles: return t("Particles");
    case FcDocumentGroup::Vents: return t("Vents");
    case FcDocumentGroup::Devices: return t("Devices");
    case FcDocumentGroup::Controls: return t("Controls");
    case FcDocumentGroup::HVAC: return t("HVAC");
    case FcDocumentGroup::InitialConditions: return t("Initial Conditions");
    case FcDocumentGroup::Outputs: return t("Outputs");
    case FcDocumentGroup::Results:
    case FcDocumentGroup::Count: return t("Results");
    }
    return {};
}

FcDocumentGroup groupForParentName(const QString& name, const QString& keyword)
{
    if (name == QStringLiteral("Geometry")) return FcDocumentGroup::Geometry;
    if (name == QStringLiteral("Meshes")) return FcDocumentGroup::Meshes;
    if (name == QStringLiteral("Configuration")) return FcDocumentGroup::Configuration;
    if (name == QStringLiteral("Species")) return FcDocumentGroup::Species;
    if (name == QStringLiteral("Materials")) return FcDocumentGroup::Materials;
    if (name == QStringLiteral("Surfaces")) return FcDocumentGroup::Surfaces;
    if (name == QStringLiteral("Reactions")) return FcDocumentGroup::Reactions;
    if (name == QStringLiteral("Particles")) return FcDocumentGroup::Particles;
    if (name == QStringLiteral("Vents")) return FcDocumentGroup::Vents;
    if (name == QStringLiteral("Devices")) return FcDocumentGroup::Devices;
    if (name == QStringLiteral("Controls")) return FcDocumentGroup::Controls;
    if (name == QStringLiteral("HVAC")) return FcDocumentGroup::HVAC;
    if (name == QStringLiteral("Initial Conditions"))
        return FcDocumentGroup::InitialConditions;
    if (name == QStringLiteral("Outputs")) return FcDocumentGroup::Outputs;
    return defaultGroupForKeyword(keyword);
}

void collectFdsObjects(const FcObject::Ptr& root,
                       std::vector<std::shared_ptr<FcFdsObject>>& objects)
{
    if (!root) return;
    if (const auto fdsObject = std::dynamic_pointer_cast<FcFdsObject>(root)) {
        objects.push_back(fdsObject);
    }
    for (const FcObject::Ptr& child : root->children()) {
        collectFdsObjects(child, objects);
    }
}

QString parameterBase(const QString& key)
{
    const int arraySuffix = key.indexOf(QLatin1Char('('));
    return key.left(arraySuffix < 0 ? key.size() : arraySuffix).trimmed().toUpper();
}

QStringList allowedReferenceKeywords(const QString& keyword, const QString& key)
{
    return FdsSchemaRegistry::allowedReferenceKeywords(keyword, key);
}

QString requiredHvacSubtype(const QString& key)
{
    const QString base = parameterBase(key);
    if (base == QStringLiteral("DUCT_ID")) return QStringLiteral("DUCT");
    if (base == QStringLiteral("NODE_ID")) return QStringLiteral("NODE");
    if (base == QStringLiteral("AIRCOIL_ID")) return QStringLiteral("AIRCOIL");
    if (base == QStringLiteral("FAN_ID")) return QStringLiteral("FAN");
    return {};
}

QString fdsKeywordForObject(const FcFdsObject& object)
{
    if (const auto* namelist = dynamic_cast<const FcFdsNamelist*>(&object)) {
        return namelist->keyword();
    }
    switch (object.type()) {
    case FcObjectType::Mesh: return QStringLiteral("MESH");
    case FcObjectType::MeshMultiplier: return QStringLiteral("MULT");
    case FcObjectType::Species: return QStringLiteral("SPEC");
    case FcObjectType::Material: return QStringLiteral("MATL");
    case FcObjectType::Surface: return QStringLiteral("SURF");
    case FcObjectType::Reaction: return QStringLiteral("REAC");
    case FcObjectType::Obstruction: return QStringLiteral("OBST");
    case FcObjectType::Vent: return QStringLiteral("VENT");
    case FcObjectType::Particle: return QStringLiteral("PART");
    case FcObjectType::Property: return QStringLiteral("PROP");
    case FcObjectType::Table: return QStringLiteral("TABL");
    case FcObjectType::Ramp: return QStringLiteral("RAMP");
    case FcObjectType::Device: return QStringLiteral("DEVC");
    case FcObjectType::Control: return QStringLiteral("CTRL");
    case FcObjectType::HVAC: return QStringLiteral("HVAC");
    default: return {};
    }
}
}

FdsObjectEditorDialog::FdsObjectEditorDialog(const FcProject* project,
                                             const QString& initialKeyword,
                                             const FcFdsNamelist* object,
                                             QWidget* parent)
    : QDialog(parent)
    , m_project(project)
    , m_object(object)
{
    setObjectName(QStringLiteral("FdsObjectEditorDialog"));
    setWindowTitle(object ? t("Edit FDS Object") : t("Create FDS Object"));
    resize(900, 680);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("FdsObjectNameEdit"));
    m_keywordCombo = new QComboBox(this);
    m_keywordCombo->setObjectName(QStringLiteral("FdsKeywordCombo"));
    m_keywordCombo->setEditable(true);
    m_keywordCombo->addItems(FdsSchemaRegistry::keywords(
        project ? project->fdsVersion() : FdsSchemaRegistry::defaultVersion()));
    m_fdsIdEdit = new QLineEdit(this);
    m_fdsIdEdit->setObjectName(QStringLiteral("FdsIdEdit"));
    m_groupCombo = new QComboBox(this);
    m_groupCombo->setObjectName(QStringLiteral("FdsGroupCombo"));
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(FcDocumentGroup::Results); ++index) {
        const auto group = static_cast<FcDocumentGroup>(index);
        m_groupCombo->addItem(groupName(group), static_cast<int>(group));
    }
    form->addRow(t("Name:"), m_nameEdit);
    form->addRow(t("Namelist:"), m_keywordCombo);
    form->addRow(t("FDS ID:"), m_fdsIdEdit);
    form->addRow(t("Category:"), m_groupCombo);
    root->addLayout(form);

    m_editorTabs = new QTabWidget(this);
    m_editorTabs->setObjectName(QStringLiteral("FdsEditorTabs"));

    m_meshEditorPage = new QWidget(m_editorTabs);
    m_meshEditorPage->setObjectName(QStringLiteral("MeshSettingsPage"));
    auto* meshPageLayout = new QHBoxLayout(m_meshEditorPage);
    auto* meshControlsLayout = new QVBoxLayout;

    auto* locationGroup = new QGroupBox(t("Mesh Location and Dimensions"),
                                         m_meshEditorPage);
    locationGroup->setObjectName(QStringLiteral("MeshLocationGroup"));
    auto* locationGrid = new QGridLayout(locationGroup);
    locationGrid->addWidget(new QLabel(t("Axis"), locationGroup), 0, 0);
    locationGrid->addWidget(new QLabel(t("Start coordinate"), locationGroup), 0, 1);
    locationGrid->addWidget(new QLabel(t("Length"), locationGroup), 0, 2);
    locationGrid->addWidget(new QLabel(t("End coordinate"), locationGroup), 0, 3);
    const std::array<QString, 3> axes = {QStringLiteral("X"), QStringLiteral("Y"),
                                         QStringLiteral("Z")};
    const std::array<double, 3> defaultLengths = {10.0, 10.0, 3.0};
    for (int axis = 0; axis < 3; ++axis) {
        locationGrid->addWidget(new QLabel(axes[axis], locationGroup), axis + 1, 0);
        auto* origin = new QDoubleSpinBox(locationGroup);
        origin->setObjectName(QStringLiteral("MeshOrigin%1Spin").arg(axes[axis]));
        origin->setRange(-1.0e6, 1.0e6);
        origin->setDecimals(6);
        origin->setSingleStep(0.1);
        origin->setSuffix(QStringLiteral(" m"));
        origin->setKeyboardTracking(false);
        m_meshOriginSpins[axis] = origin;
        locationGrid->addWidget(origin, axis + 1, 1);

        auto* size = new QDoubleSpinBox(locationGroup);
        size->setObjectName(QStringLiteral("MeshLength%1Spin").arg(axes[axis]));
        size->setRange(0.000001, 1.0e6);
        size->setDecimals(6);
        size->setSingleStep(0.1);
        size->setSuffix(QStringLiteral(" m"));
        size->setKeyboardTracking(false);
        size->setValue(defaultLengths[axis]);
        m_meshSizeSpins[axis] = size;
        locationGrid->addWidget(size, axis + 1, 2);

        auto* end = new QLabel(locationGroup);
        end->setObjectName(QStringLiteral("MeshEnd%1Label").arg(axes[axis]));
        end->setMinimumWidth(90);
        m_meshEndLabels[axis] = end;
        locationGrid->addWidget(end, axis + 1, 3);
    }
    meshControlsLayout->addWidget(locationGroup);

    auto* resolutionGroup = new QGroupBox(t("Mesh Resolution"), m_meshEditorPage);
    resolutionGroup->setObjectName(QStringLiteral("MeshResolutionGroup"));
    auto* resolutionLayout = new QVBoxLayout(resolutionGroup);
    auto* modeLayout = new QFormLayout;
    m_meshResolutionMode = new QComboBox(resolutionGroup);
    m_meshResolutionMode->setObjectName(QStringLiteral("MeshResolutionModeCombo"));
    m_meshResolutionMode->addItem(t("Number of cells"), 0);
    m_meshResolutionMode->addItem(t("Target cell size"), 1);
    modeLayout->addRow(t("Input method:"), m_meshResolutionMode);
    resolutionLayout->addLayout(modeLayout);

    m_meshResolutionStack = new QStackedWidget(resolutionGroup);
    auto* cellCountPage = new QWidget(m_meshResolutionStack);
    auto* cellCountGrid = new QGridLayout(cellCountPage);
    auto* targetSizePage = new QWidget(m_meshResolutionStack);
    auto* targetSizeGrid = new QGridLayout(targetSizePage);
    const std::array<int, 3> defaultCells = {20, 20, 12};
    const std::array<double, 3> defaultCellSizes = {0.5, 0.5, 0.25};
    for (int axis = 0; axis < 3; ++axis) {
        cellCountGrid->addWidget(
            new QLabel(t("Cells along %1:").arg(axes[axis]), cellCountPage), axis, 0);
        auto* cells = new QSpinBox(cellCountPage);
        cells->setObjectName(QStringLiteral("MeshCellCount%1Spin").arg(axes[axis]));
        cells->setRange(1, 1000000);
        cells->setValue(defaultCells[axis]);
        cells->setKeyboardTracking(false);
        m_meshCellSpins[axis] = cells;
        cellCountGrid->addWidget(cells, axis, 1);

        targetSizeGrid->addWidget(
            new QLabel(t("Target size along %1:").arg(axes[axis]), targetSizePage),
            axis, 0);
        auto* targetSize = new QDoubleSpinBox(targetSizePage);
        targetSize->setObjectName(
            QStringLiteral("MeshTargetCellSize%1Spin").arg(axes[axis]));
        targetSize->setRange(0.000001, 1.0e6);
        targetSize->setDecimals(6);
        targetSize->setSingleStep(0.05);
        targetSize->setSuffix(QStringLiteral(" m"));
        targetSize->setKeyboardTracking(false);
        targetSize->setValue(defaultCellSizes[axis]);
        m_meshTargetCellSizeSpins[axis] = targetSize;
        targetSizeGrid->addWidget(targetSize, axis, 1);
    }
    m_meshResolutionStack->addWidget(cellCountPage);
    m_meshResolutionStack->addWidget(targetSizePage);
    resolutionLayout->addWidget(m_meshResolutionStack);
    meshControlsLayout->addWidget(resolutionGroup);

    m_meshSummaryLabel = new QLabel(m_meshEditorPage);
    m_meshSummaryLabel->setObjectName(QStringLiteral("MeshSummaryLabel"));
    m_meshSummaryLabel->setWordWrap(true);
    m_meshSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshSummaryLabel->setStyleSheet(
        QStringLiteral("QLabel { background: palette(alternate-base); border: 1px solid "
                       "palette(mid); border-radius: 4px; padding: 8px; }"));
    meshControlsLayout->addWidget(m_meshSummaryLabel);
    meshControlsLayout->addStretch();
    meshPageLayout->addLayout(meshControlsLayout, 3);

    m_meshPreview = new MeshPreviewWidget(m_meshEditorPage);
    meshPageLayout->addWidget(m_meshPreview, 2);
    m_editorTabs->addTab(m_meshEditorPage, t("Mesh Settings"));

    m_basicSchemaEditor = new FdsSchemaEditorWidget(
        project, object, FdsSchemaEditorMode::Basic, m_editorTabs);
    m_editorTabs->addTab(m_basicSchemaEditor, t("Basic Parameters"));
    m_schemaEditor = new FdsSchemaEditorWidget(
        project, object, FdsSchemaEditorMode::Professional, m_editorTabs);
    m_editorTabs->addTab(m_schemaEditor, t("Professional Parameters"));

    auto* advancedPage = new QWidget(m_editorTabs);
    advancedPage->setObjectName(QStringLiteral("AdvancedFdsParametersPage"));
    auto* advancedLayout = new QVBoxLayout(advancedPage);
    auto* hint = new QLabel(
        t("Raw values are written exactly as entered; Text values are quoted; References store target UUIDs."),
        advancedPage);
    hint->setWordWrap(true);
    advancedLayout->addWidget(hint);

    m_specializedHint = new QLabel(advancedPage);
    m_specializedHint->setObjectName(QStringLiteral("FdsSpecializedEditorHint"));
    m_specializedHint->setWordWrap(true);
    advancedLayout->addWidget(m_specializedHint);

    m_parameterTable = new QTableWidget(0, 3, advancedPage);
    m_parameterTable->setObjectName(QStringLiteral("FdsParameterTable"));
    m_parameterTable->setHorizontalHeaderLabels(
        {t("Parameter"), t("Value Type"), t("Value / Reference")});
    m_parameterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_parameterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_parameterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_parameterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_parameterTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_parameterTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    advancedLayout->addWidget(m_parameterTable, 1);

    auto* rowButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(t("Add Parameter"), advancedPage);
    addButton->setObjectName(QStringLiteral("AddFdsParameterButton"));
    auto* removeButton = new QPushButton(t("Remove Parameter"), advancedPage);
    removeButton->setObjectName(QStringLiteral("RemoveFdsParameterButton"));
    auto* moveUpButton = new QPushButton(t("Move Parameter Up"), advancedPage);
    moveUpButton->setObjectName(QStringLiteral("MoveFdsParameterUpButton"));
    auto* moveDownButton = new QPushButton(t("Move Parameter Down"), advancedPage);
    moveDownButton->setObjectName(QStringLiteral("MoveFdsParameterDownButton"));
    m_chooseReferenceButton = new QPushButton(t("Choose References..."), advancedPage);
    m_chooseReferenceButton->setObjectName(QStringLiteral("ChooseFdsReferenceButton"));
    rowButtons->addWidget(addButton);
    rowButtons->addWidget(removeButton);
    rowButtons->addWidget(moveUpButton);
    rowButtons->addWidget(moveDownButton);
    rowButtons->addWidget(m_chooseReferenceButton);
    rowButtons->addStretch();
    advancedLayout->addLayout(rowButtons);

    auto* presetButtons = new QHBoxLayout;
    m_parameterPresetCombo = new QComboBox(advancedPage);
    m_parameterPresetCombo->setObjectName(QStringLiteral("FdsRecommendedParameterCombo"));
    auto* addRecommendedButton =
        new QPushButton(t("Add Recommended Parameter"), advancedPage);
    addRecommendedButton->setObjectName(
        QStringLiteral("AddRecommendedFdsParameterButton"));
    presetButtons->addWidget(m_parameterPresetCombo, 1);
    presetButtons->addWidget(addRecommendedButton);
    advancedLayout->addLayout(presetButtons);
    m_editorTabs->addTab(advancedPage, t("Advanced FDS Parameters"));
    root->addWidget(m_editorTabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         this);
    root->addWidget(buttons);
    connect(addButton, &QPushButton::clicked, this,
            [this]() { addParameterRow(); });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_parameterTable->currentRow();
        if (row >= 0) m_parameterTable->removeRow(row);
        updateReferenceButton();
    });
    connect(moveUpButton, &QPushButton::clicked, this,
            [this]() { moveCurrentParameterRow(-1); });
    connect(moveDownButton, &QPushButton::clicked, this,
            [this]() { moveCurrentParameterRow(1); });
    connect(addRecommendedButton, &QPushButton::clicked,
            this, &FdsObjectEditorDialog::addRecommendedParameter);
    connect(m_chooseReferenceButton, &QPushButton::clicked,
            this, &FdsObjectEditorDialog::chooseReferencesForCurrentRow);
    connect(m_parameterTable, &QTableWidget::currentCellChanged,
            this, [this]() { updateReferenceButton(); });
    connect(m_parameterTable, &QTableWidget::cellChanged, this,
            [this](int, int) {
                if (!m_updatingMeshEditor &&
                    m_keywordCombo->currentText().trimmed().compare(
                        QStringLiteral("MESH"), Qt::CaseInsensitive) == 0) {
                    syncMeshControlsFromParameters();
                }
            });
    const auto applySchemaEdit =
            [this](const QString& key, int kindValue, const QString& value,
                   const QStringList& targetIds) {
                const auto kind = static_cast<FcFdsParameterKind>(kindValue);
                int row = parameterRow(key);
                if (row < 0) {
                    addParameterRow({key, kind, value, targetIds});
                    row = parameterRow(key);
                }
                if (row < 0) return;
                const QSignalBlocker tableBlocker(m_parameterTable);
                if (auto* kindCombo = qobject_cast<QComboBox*>(
                        m_parameterTable->cellWidget(row, 1))) {
                    const QSignalBlocker kindBlocker(kindCombo);
                    const int index = kindCombo->findData(kindValue);
                    if (index >= 0) kindCombo->setCurrentIndex(index);
                }
                if (QTableWidgetItem* item = m_parameterTable->item(row, 2)) {
                    item->setData(ReferenceIdsRole, targetIds);
                    item->setText(kind == FcFdsParameterKind::ObjectReferences
                                      ? targetIds.join(QStringLiteral(", ")) : value);
                }
            };
    connect(m_basicSchemaEditor, &FdsSchemaEditorWidget::parameterEdited,
            this, applySchemaEdit);
    connect(m_schemaEditor, &FdsSchemaEditorWidget::parameterEdited,
            this, applySchemaEdit);
    connect(m_editorTabs, &QTabWidget::currentChanged, this, [this](int index) {
        FdsSchemaEditorWidget* editor = nullptr;
        if (m_editorTabs->widget(index) == m_basicSchemaEditor) {
            editor = m_basicSchemaEditor;
        } else if (m_editorTabs->widget(index) == m_schemaEditor) {
            editor = m_schemaEditor;
        }
        if (editor) {
            editor->setNamelist(
                m_keywordCombo->currentText(), editorData().parameters,
                m_project ? m_project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
        }
    });
    connect(m_meshResolutionMode, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updatingMeshEditor) return;
                m_meshResolutionStack->setCurrentIndex(index);
                if (index == 1) {
                    for (int axis = 0; axis < 3; ++axis) {
                        const double actual = m_meshSizeSpins[axis]->value() /
                                              m_meshCellSpins[axis]->value();
                        const QSignalBlocker blocker(m_meshTargetCellSizeSpins[axis]);
                        m_meshTargetCellSizeSpins[axis]->setValue(actual);
                    }
                }
                syncMeshParametersFromControls();
            });
    for (int axis = 0; axis < 3; ++axis) {
        connect(m_meshOriginSpins[axis], &QDoubleSpinBox::valueChanged,
                this, [this](double) { syncMeshParametersFromControls(); });
        connect(m_meshSizeSpins[axis], &QDoubleSpinBox::valueChanged,
                this, [this](double) { syncMeshParametersFromControls(); });
        connect(m_meshCellSpins[axis], &QSpinBox::valueChanged,
                this, [this](int) { syncMeshParametersFromControls(); });
        connect(m_meshTargetCellSizeSpins[axis], &QDoubleSpinBox::valueChanged,
                this, [this](double) { syncMeshParametersFromControls(); });
    }
    connect(buttons, &QDialogButtonBox::accepted,
            this, &FdsObjectEditorDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_keywordCombo, &QComboBox::currentTextChanged, this,
            [this](const QString keyword) {
                switchParameterDraft(keyword);
                updateEditorAssist(keyword);
                if (!m_object) {
                    // Only update the untouched generated name. Never rename a
                    // user-specified object when changing its record type.
                    if (!m_defaultObjectName.isEmpty() &&
                        m_nameEdit->text() == m_defaultObjectName) {
                        m_defaultObjectName = keyword + QStringLiteral(" 001");
                        m_nameEdit->setText(m_defaultObjectName);
                    }
                    const int group = static_cast<int>(defaultGroupForKeyword(keyword));
                    const int index = m_groupCombo->findData(group);
                    if (index >= 0) m_groupCombo->setCurrentIndex(index);
                }
            });

    const QString keyword = object ? object->keyword()
                                   : (initialKeyword.isEmpty()
                                          ? QStringLiteral("MESH")
                                          : initialKeyword.trimmed().toUpper());
    m_keywordCombo->setCurrentText(keyword);
    if (object) {
        m_nameEdit->setText(object->name());
        m_fdsIdEdit->setText(object->fdsId());
        m_keywordCombo->setEnabled(false);
        m_groupCombo->setEnabled(false);
        const int groupIndex = m_groupCombo->findData(static_cast<int>(
            groupForParentName(object->parent() ? object->parent()->name() : QString{},
                               object->keyword())));
        if (groupIndex >= 0) m_groupCombo->setCurrentIndex(groupIndex);
        for (const FcFdsParameter& parameter : object->parameters()) {
            addParameterRow(parameter);
        }
    } else {
        m_defaultObjectName = keyword + QStringLiteral(" 001");
        m_nameEdit->setText(m_defaultObjectName);
        const int groupIndex = m_groupCombo->findData(
            static_cast<int>(defaultGroupForKeyword(keyword)));
        if (groupIndex >= 0) m_groupCombo->setCurrentIndex(groupIndex);
        const auto defaults = FdsSchemaRegistry::defaultParameters(
            keyword, project ? project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
        if (defaults.empty()) {
            addParameterRow();
        } else {
            for (const FcFdsParameter& parameter : defaults) {
                addParameterRow(parameter);
            }
        }
    }
    updateEditorAssist(keyword);
    m_basicSchemaEditor->setNamelist(
        keyword, editorData().parameters,
        project ? project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
    m_schemaEditor->setNamelist(
        keyword, editorData().parameters,
        project ? project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
    syncMeshControlsFromParameters();
    updateReferenceButton();
    m_parameterKeyword = keyword.trimmed().toUpper();
}

void FdsObjectEditorDialog::switchParameterDraft(const QString& keyword)
{
    const QString normalized = keyword.trimmed().toUpper();
    // Existing objects keep their record type. During construction the initial
    // rows have not been loaded yet; an empty edit is not a new record type.
    if (m_object || m_parameterKeyword.isEmpty()) return;

    // A type change can also be requested while a Basic/Advanced editor still
    // owns focus. Commit that editor before caching the old type's table. The
    // combo already contains the new type at this point; use the previous type
    // while committing so deferred MESH spin edits update the MESH draft.
    QWidget* activeEditor = QApplication::focusWidget();
    if (activeEditor && m_editorTabs && m_editorTabs->isAncestorOf(activeEditor)) {
        const QString requestedKeyword = m_keywordCombo->currentText();
        const QSignalBlocker keywordBlocker(m_keywordCombo);
        m_keywordCombo->setCurrentText(m_parameterKeyword);
        activeEditor->clearFocus();
        m_keywordCombo->setCurrentText(requestedKeyword);
    }
    // Empty/case-only edits still rebuild helper pages in updateEditorAssist;
    // commit their focused values above before deciding no draft swap is needed.
    if (normalized.isEmpty() || normalized == m_parameterKeyword) return;
    m_parameterDrafts.insert(m_parameterKeyword, editorData().parameters);
    const auto draft = m_parameterDrafts.constFind(normalized);
    const bool hasDraft = draft != m_parameterDrafts.cend();
    const auto parameters = !hasDraft
        ? FdsSchemaRegistry::defaultParameters(
              normalized, m_project ? m_project->fdsVersion()
                                    : FdsSchemaRegistry::defaultVersion())
        : draft.value();
    const QSignalBlocker tableBlocker(m_parameterTable);
    const bool wasUpdating = m_updatingMeshEditor;
    m_updatingMeshEditor = true;
    m_parameterTable->setRowCount(0);
    if (!hasDraft && parameters.empty()) {
        addParameterRow();
    } else {
        for (const FcFdsParameter& parameter : parameters) addParameterRow(parameter);
    }
    m_updatingMeshEditor = wasUpdating;
    m_parameterKeyword = normalized;
    updateReferenceButton();
}

int FdsObjectEditorDialog::parameterRow(const QString& key) const
{
    if (!m_parameterTable) return -1;
    const QString normalized = key.trimmed().toUpper();
    for (int row = 0; row < m_parameterTable->rowCount(); ++row) {
        const QTableWidgetItem* item = m_parameterTable->item(row, 0);
        if (item && item->text().trimmed().compare(normalized,
                                                   Qt::CaseInsensitive) == 0) {
            return row;
        }
    }
    return -1;
}

void FdsObjectEditorDialog::setRawParameterValue(const QString& key,
                                                  const QString& value)
{
    int row = parameterRow(key);
    if (row < 0) {
        addParameterRow({key, FcFdsParameterKind::Raw, value, {}});
        row = parameterRow(key);
    }
    if (row < 0) return;
    if (QTableWidgetItem* valueItem = m_parameterTable->item(row, 2)) {
        valueItem->setText(value);
        valueItem->setData(ReferenceIdsRole, QStringList{});
    }
    if (auto* kind = qobject_cast<QComboBox*>(m_parameterTable->cellWidget(row, 1))) {
        const int rawIndex = kind->findData(static_cast<int>(FcFdsParameterKind::Raw));
        if (rawIndex >= 0) kind->setCurrentIndex(rawIndex);
    }
}

void FdsObjectEditorDialog::syncMeshControlsFromParameters()
{
    if (m_updatingMeshEditor || !m_parameterTable ||
        m_keywordCombo->currentText().trimmed().compare(
            QStringLiteral("MESH"), Qt::CaseInsensitive) != 0) {
        return;
    }
    m_updatingMeshEditor = true;

    std::vector<double> values;
    const int ijkRow = parameterRow(QStringLiteral("IJK"));
    if (ijkRow >= 0 && m_parameterTable->item(ijkRow, 2) &&
        parseNumberList(m_parameterTable->item(ijkRow, 2)->text(), 3, values)) {
        for (int axis = 0; axis < 3; ++axis) {
            const double rounded = std::round(values[static_cast<std::size_t>(axis)]);
            if (rounded >= 1.0 && rounded <= 1000000.0 &&
                qAbs(values[static_cast<std::size_t>(axis)] - rounded) < 1.0e-9) {
                const QSignalBlocker blocker(m_meshCellSpins[axis]);
                m_meshCellSpins[axis]->setValue(static_cast<int>(rounded));
            }
        }
    }

    const int xbRow = parameterRow(QStringLiteral("XB"));
    if (xbRow >= 0 && m_parameterTable->item(xbRow, 2) &&
        parseNumberList(m_parameterTable->item(xbRow, 2)->text(), 6, values)) {
        for (int axis = 0; axis < 3; ++axis) {
            const double minimum = values[static_cast<std::size_t>(axis * 2)];
            const double maximum = values[static_cast<std::size_t>(axis * 2 + 1)];
            if (maximum <= minimum) continue;
            const QSignalBlocker originBlocker(m_meshOriginSpins[axis]);
            const QSignalBlocker sizeBlocker(m_meshSizeSpins[axis]);
            m_meshOriginSpins[axis]->setValue(minimum);
            m_meshSizeSpins[axis]->setValue(maximum - minimum);
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        const double actual = m_meshSizeSpins[axis]->value() /
                              m_meshCellSpins[axis]->value();
        const QSignalBlocker blocker(m_meshTargetCellSizeSpins[axis]);
        m_meshTargetCellSizeSpins[axis]->setValue(actual);
    }
    m_meshResolutionStack->setCurrentIndex(m_meshResolutionMode->currentIndex());
    updateMeshSummary();
    m_updatingMeshEditor = false;
}

void FdsObjectEditorDialog::syncMeshParametersFromControls()
{
    if (m_updatingMeshEditor ||
        m_keywordCombo->currentText().trimmed().compare(
            QStringLiteral("MESH"), Qt::CaseInsensitive) != 0) {
        return;
    }
    m_updatingMeshEditor = true;

    if (m_meshResolutionMode->currentIndex() == 1) {
        for (int axis = 0; axis < 3; ++axis) {
            const double length = m_meshSizeSpins[axis]->value();
            const double target = m_meshTargetCellSizeSpins[axis]->value();
            const double requestedCells = std::ceil(length / target);
            const int cells = requestedCells >= 1000000.0
                                  ? 1000000
                                  : qMax(1, static_cast<int>(requestedCells));
            const QSignalBlocker blocker(m_meshCellSpins[axis]);
            m_meshCellSpins[axis]->setValue(cells);
        }
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            const double actual = m_meshSizeSpins[axis]->value() /
                                  m_meshCellSpins[axis]->value();
            const QSignalBlocker blocker(m_meshTargetCellSizeSpins[axis]);
            m_meshTargetCellSizeSpins[axis]->setValue(actual);
        }
    }

    const QString ijk = QStringLiteral("%1,%2,%3")
                            .arg(m_meshCellSpins[0]->value())
                            .arg(m_meshCellSpins[1]->value())
                            .arg(m_meshCellSpins[2]->value());
    QStringList bounds;
    for (int axis = 0; axis < 3; ++axis) {
        const double minimum = m_meshOriginSpins[axis]->value();
        bounds.append(compactNumber(minimum));
        bounds.append(compactNumber(minimum + m_meshSizeSpins[axis]->value()));
    }
    setRawParameterValue(QStringLiteral("IJK"), ijk);
    setRawParameterValue(QStringLiteral("XB"), bounds.join(QLatin1Char(',')));
    updateMeshSummary();
    m_updatingMeshEditor = false;
}

void FdsObjectEditorDialog::updateMeshSummary()
{
    if (!m_meshSummaryLabel) return;
    QStringList bounds;
    std::array<double, 3> cellSizes{};
    for (int axis = 0; axis < 3; ++axis) {
        const double minimum = m_meshOriginSpins[axis]->value();
        const double maximum = minimum + m_meshSizeSpins[axis]->value();
        bounds.append(compactNumber(minimum));
        bounds.append(compactNumber(maximum));
        m_meshEndLabels[axis]->setText(QStringLiteral("%1 m").arg(compactNumber(maximum)));
        cellSizes[axis] = m_meshSizeSpins[axis]->value() /
                          m_meshCellSpins[axis]->value();
        m_meshPreview->setProperty(
            axis == 0 ? "meshLengthX" : axis == 1 ? "meshLengthY" : "meshLengthZ",
            m_meshSizeSpins[axis]->value());
    }
    m_meshPreview->update();

    const qint64 totalCells = static_cast<qint64>(m_meshCellSpins[0]->value()) *
                              m_meshCellSpins[1]->value() *
                              m_meshCellSpins[2]->value();
    const double smallestCell = qMin(cellSizes[0], qMin(cellSizes[1], cellSizes[2]));
    const double largestCell = qMax(cellSizes[0], qMax(cellSizes[1], cellSizes[2]));
    const double aspectRatio = largestCell / smallestCell;
    const double baseMemoryMiB = static_cast<double>(totalCells) / 1024.0;
    const QString memoryText = baseMemoryMiB >= 1024.0
                                   ? QStringLiteral("%1 GiB").arg(baseMemoryMiB / 1024.0,
                                                                  0, 'f', 2)
                                   : QStringLiteral("%1 MiB").arg(baseMemoryMiB, 0, 'f', 1);
    const QString ijk = QStringLiteral("%1,%2,%3")
                            .arg(m_meshCellSpins[0]->value())
                            .arg(m_meshCellSpins[1]->value())
                            .arg(m_meshCellSpins[2]->value());
    QString summary =
        t("FDS output: IJK=%1   XB=%2").arg(ijk, bounds.join(QLatin1Char(',')));
    summary += QLatin1Char('\n') +
               t("Actual cell size: Δx=%1 m, Δy=%2 m, Δz=%3 m")
                   .arg(compactNumber(cellSizes[0]), compactNumber(cellSizes[1]),
                        compactNumber(cellSizes[2]));
    summary += QLatin1Char('\n') +
               t("Total cells: %1   Estimated base memory: %2")
                   .arg(QString::number(totalCells), memoryText);
    summary += QLatin1Char('\n') +
               t("Cell aspect ratio: %1 (recommended ≤ 2.0)")
                   .arg(aspectRatio, 0, 'f', 2);
    if (aspectRatio > 2.0) {
        summary += QLatin1Char('\n') +
                   t("Warning: strongly stretched cells may reduce solution quality.");
    }
    summary += QLatin1Char('\n') +
               t("Memory is a rough baseline estimate and excludes result/output buffers.");
    m_meshSummaryLabel->setText(summary);
    const QString color = aspectRatio > 2.0 ? QStringLiteral("#b45309")
                                             : QStringLiteral("palette(text)");
    m_meshSummaryLabel->setStyleSheet(
        QStringLiteral("QLabel { background: palette(alternate-base); border: 1px solid "
                       "palette(mid); border-radius: 4px; padding: 8px; color: %1; }")
            .arg(color));
}

void FdsObjectEditorDialog::addParameterRow(const FcFdsParameter& parameter)
{
    const int row = m_parameterTable->rowCount();
    m_parameterTable->insertRow(row);
    m_parameterTable->setItem(row, 0, new QTableWidgetItem(parameter.key));
    auto* kind = new QComboBox(m_parameterTable);
    kind->addItem(t("Raw"), static_cast<int>(FcFdsParameterKind::Raw));
    kind->addItem(t("Text"), static_cast<int>(FcFdsParameterKind::String));
    kind->addItem(t("Reference"), static_cast<int>(FcFdsParameterKind::ObjectReferences));
    const int kindIndex = kind->findData(static_cast<int>(parameter.kind));
    kind->setCurrentIndex(qMax(0, kindIndex));
    m_parameterTable->setCellWidget(row, 1, kind);
    auto* value = new QTableWidgetItem(parameter.value);
    value->setData(ReferenceIdsRole, parameter.targetObjectIds);
    if (parameter.kind == FcFdsParameterKind::ObjectReferences &&
        !parameter.targetObjectIds.isEmpty()) {
        value->setText(parameter.targetObjectIds.join(QStringLiteral(", ")));
    }
    m_parameterTable->setItem(row, 2, value);
    connect(kind, &QComboBox::currentIndexChanged, this,
            [this]() { updateReferenceButton(); });
    m_parameterTable->setCurrentCell(row, 0);
}

void FdsObjectEditorDialog::addRecommendedParameter()
{
    const auto recommended = FdsSchemaRegistry::recommendedParameters(
        m_keywordCombo->currentText(),
        m_project ? m_project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
    const int index = m_parameterPresetCombo->currentIndex();
    if (index < 0 || index >= static_cast<int>(recommended.size())) return;
    addParameterRow(recommended[static_cast<std::size_t>(index)]);
}

void FdsObjectEditorDialog::moveCurrentParameterRow(int offset)
{
    const int row = m_parameterTable->currentRow();
    const int target = row + offset;
    if (row < 0 || target < 0 || target >= m_parameterTable->rowCount()) return;
    std::vector<FcFdsParameter> parameters = editorData().parameters;
    std::swap(parameters[static_cast<std::size_t>(row)],
              parameters[static_cast<std::size_t>(target)]);
    m_parameterTable->setRowCount(0);
    for (const FcFdsParameter& parameter : parameters) addParameterRow(parameter);
    m_parameterTable->setCurrentCell(target, 0);
}

void FdsObjectEditorDialog::updateEditorAssist(const QString& keyword)
{
    if (m_specializedHint) m_specializedHint->setText(editorHintForKeyword(keyword));
    const bool isMesh = keyword.trimmed().compare(QStringLiteral("MESH"),
                                                   Qt::CaseInsensitive) == 0;
    if (m_editorTabs && m_meshEditorPage) {
        m_editorTabs->setTabVisible(0, isMesh);
        m_editorTabs->setTabVisible(1, !isMesh);
        m_editorTabs->setCurrentIndex(isMesh ? 0 : 1);
        if (isMesh) syncMeshControlsFromParameters();
    }
    if (m_schemaEditor && m_parameterTable) {
        m_basicSchemaEditor->setNamelist(
            keyword, editorData().parameters,
            m_project ? m_project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
        m_schemaEditor->setNamelist(
            keyword, editorData().parameters,
            m_project ? m_project->fdsVersion() : FdsSchemaRegistry::defaultVersion());
    }
    if (!m_parameterPresetCombo) return;
    m_parameterPresetCombo->clear();
    for (const FcFdsParameter& parameter : FdsSchemaRegistry::recommendedParameters(
             keyword, m_project ? m_project->fdsVersion()
                                : FdsSchemaRegistry::defaultVersion())) {
        QString kind = t("Raw");
        if (parameter.kind == FcFdsParameterKind::String) kind = t("Text");
        if (parameter.kind == FcFdsParameterKind::ObjectReferences) kind = t("Reference");
        m_parameterPresetCombo->addItem(
            QStringLiteral("%1 — %2").arg(parameter.key, kind), parameter.key);
    }
}

void FdsObjectEditorDialog::chooseReferencesForCurrentRow()
{
    const int row = m_parameterTable->currentRow();
    if (row < 0 || !m_project || !m_project->document()) return;
    auto* kind = qobject_cast<QComboBox*>(m_parameterTable->cellWidget(row, 1));
    if (!kind || static_cast<FcFdsParameterKind>(kind->currentData().toInt()) !=
                     FcFdsParameterKind::ObjectReferences) return;

    QDialog dialog(this);
    dialog.setWindowTitle(t("Choose Referenced Objects"));
    dialog.resize(520, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("FdsReferenceList"));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const QStringList selectedIds = m_parameterTable->item(row, 2)
                                        ->data(ReferenceIdsRole).toStringList();
    const QString parameterKey = m_parameterTable->item(row, 0)
                                     ? m_parameterTable->item(row, 0)->text()
                                     : QString{};
    const QStringList allowedKeywords = allowedReferenceKeywords(
        m_keywordCombo->currentText(), parameterKey);
    const QString hvacSubtype = requiredHvacSubtype(parameterKey);
    std::vector<std::shared_ptr<FcFdsObject>> objects;
    for (const auto& group : m_project->document()->groups()) {
        collectFdsObjects(group, objects);
    }
    for (const auto& object : objects) {
        if (!object || object.get() == m_object) continue;
        const QString keyword = fdsKeywordForObject(*object);
        if (!allowedKeywords.isEmpty() &&
            !allowedKeywords.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        if (!hvacSubtype.isEmpty()) {
            const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
            if (!namelist || namelist->parameterValue(QStringLiteral("TYPE_ID")).compare(
                                hvacSubtype, Qt::CaseInsensitive) != 0) {
                continue;
            }
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 | %2 | %3 | UUID %4")
                .arg(keyword, object->fdsId(), object->name(), object->id()),
            list);
        item->setData(Qt::UserRole, object->id());
        item->setSelected(selectedIds.contains(object->id()));
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QStringList ids;
    QStringList labels;
    for (QListWidgetItem* item : list->selectedItems()) {
        ids.append(item->data(Qt::UserRole).toString());
        labels.append(item->text());
    }
    QTableWidgetItem* value = m_parameterTable->item(row, 2);
    value->setData(ReferenceIdsRole, ids);
    value->setText(labels.join(QStringLiteral("; ")));
}

void FdsObjectEditorDialog::updateReferenceButton()
{
    const int row = m_parameterTable->currentRow();
    auto* kind = row >= 0
                     ? qobject_cast<QComboBox*>(m_parameterTable->cellWidget(row, 1))
                     : nullptr;
    m_chooseReferenceButton->setEnabled(
        kind && static_cast<FcFdsParameterKind>(kind->currentData().toInt()) ==
                    FcFdsParameterKind::ObjectReferences);
}

FdsObjectEditorData FdsObjectEditorDialog::editorData() const
{
    FdsObjectEditorData data;
    data.name = m_nameEdit->text().trimmed();
    data.keyword = m_keywordCombo->currentText().trimmed().toUpper();
    data.fdsId = m_fdsIdEdit->text().trimmed();
    data.group = static_cast<FcDocumentGroup>(m_groupCombo->currentData().toInt());
    for (int row = 0; row < m_parameterTable->rowCount(); ++row) {
        const QTableWidgetItem* keyItem = m_parameterTable->item(row, 0);
        const QTableWidgetItem* valueItem = m_parameterTable->item(row, 2);
        auto* kindCombo = qobject_cast<QComboBox*>(m_parameterTable->cellWidget(row, 1));
        FcFdsParameter parameter;
        parameter.key = keyItem ? keyItem->text().trimmed().toUpper() : QString{};
        parameter.kind = kindCombo
                             ? static_cast<FcFdsParameterKind>(kindCombo->currentData().toInt())
                             : FcFdsParameterKind::Raw;
        parameter.value = valueItem ? valueItem->text().trimmed() : QString{};
        if (parameter.kind == FcFdsParameterKind::ObjectReferences && valueItem) {
            parameter.targetObjectIds = valueItem->data(ReferenceIdsRole).toStringList();
        }
        data.parameters.push_back(std::move(parameter));
    }
    return data;
}

void FdsObjectEditorDialog::setEditorData(
    const FdsObjectEditorData& editorDataValue)
{
    // An explicit load supersedes uncommitted text from the old pages. Tab
    // visibility/current-index changes can emit editingFinished before the new
    // setNamelist generation is installed, so suppress both editors throughout
    // the replacement. The generation guards cover later retired-page signals.
    const QSignalBlocker basicEditorBlocker(m_basicSchemaEditor);
    const QSignalBlocker professionalEditorBlocker(m_schemaEditor);
    m_nameEdit->setText(editorDataValue.name);
    {
        // An explicit full load owns its parameters and must not select a draft.
        const QSignalBlocker keywordBlocker(m_keywordCombo);
        m_keywordCombo->setCurrentText(editorDataValue.keyword.trimmed().toUpper());
    }
    m_parameterDrafts.clear();
    m_parameterKeyword = editorDataValue.keyword.trimmed().toUpper();
    m_fdsIdEdit->setText(editorDataValue.fdsId);
    const int groupIndex = m_groupCombo->findData(
        static_cast<int>(editorDataValue.group));
    if (groupIndex >= 0) m_groupCombo->setCurrentIndex(groupIndex);

    m_updatingMeshEditor = true;
    m_parameterTable->setRowCount(0);
    for (const FcFdsParameter& parameter : editorDataValue.parameters) {
        addParameterRow(parameter);
    }
    m_updatingMeshEditor = false;

    updateEditorAssist(editorDataValue.keyword);
    if (m_basicSchemaEditor) {
        m_basicSchemaEditor->setNamelist(
            editorDataValue.keyword, editorDataValue.parameters,
            m_project ? m_project->fdsVersion()
                      : FdsSchemaRegistry::defaultVersion());
    }
    if (m_schemaEditor) {
        m_schemaEditor->setNamelist(
            editorDataValue.keyword, editorDataValue.parameters,
            m_project ? m_project->fdsVersion()
                      : FdsSchemaRegistry::defaultVersion());
    }
    syncMeshControlsFromParameters();
    updateReferenceButton();
}

void FdsObjectEditorDialog::validateAndAccept()
{
    const FdsObjectEditorData data = editorData();
    QStringList errors;
    if (data.name.isEmpty()) errors.append(t("Object name must not be empty."));
    if (data.keyword.isEmpty()) errors.append(t("Namelist keyword must not be empty."));
    for (const FcFdsParameter& parameter : data.parameters) {
        if (parameter.key.isEmpty()) {
            errors.append(t("Parameter names must not be empty."));
            break;
        }
        if (parameter.kind == FcFdsParameterKind::ObjectReferences &&
            parameter.targetObjectIds.isEmpty()) {
            errors.append(t("Reference parameters must select at least one object."));
            break;
        }
    }
    FcFdsNamelist candidate(data.name, FcObjectType::Unknown, data.keyword,
                            data.fdsId);
    candidate.setParameters(data.parameters);
    for (const QString& error : candidate.validate()) {
        if (!errors.contains(error)) errors.append(error);
    }
    for (const QString& error : FdsSchemaRegistry::validate(
             data.keyword, data.fdsId, data.parameters,
             m_project ? m_project->fdsVersion() : FdsSchemaRegistry::defaultVersion())) {
        if (!errors.contains(error)) errors.append(error);
    }
    if (m_project && m_project->document() && !data.fdsId.isEmpty() &&
        data.keyword != QStringLiteral("RAMP") &&
        data.keyword != QStringLiteral("TABL")) {
        std::vector<std::shared_ptr<FcFdsObject>> objects;
        for (const auto& group : m_project->document()->groups()) {
            collectFdsObjects(group, objects);
        }
        for (const auto& object : objects) {
            if (!object || object.get() == m_object) continue;
            if (fdsKeywordForObject(*object) == data.keyword &&
                object->fdsId().compare(data.fdsId, Qt::CaseInsensitive) == 0) {
                errors.append(t("Another object already uses this FDS ID."));
                break;
            }
        }
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, t("Invalid FDS Object"), errors.join(QLatin1Char('\n')));
        return;
    }
    accept();
}

FdsProjectDialog::FdsProjectDialog(const FcProject* project, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("FdsProjectDialog"));
    setWindowTitle(t("Project Settings"));
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_nameEdit = new QLineEdit(project ? project->name() : QString{}, this);
    m_nameEdit->setObjectName(QStringLiteral("ProjectNameEdit"));
    m_chidEdit = new QLineEdit(project ? project->chid() : QString{}, this);
    m_chidEdit->setObjectName(QStringLiteral("ProjectChidEdit"));
    m_endTimeSpin = new QDoubleSpinBox(this);
    m_endTimeSpin->setObjectName(QStringLiteral("ProjectEndTimeSpin"));
    m_endTimeSpin->setRange(0.000001, 1.0e9);
    m_endTimeSpin->setDecimals(6);
    m_endTimeSpin->setValue(project ? project->endTime() : 60.0);
    m_endTimeSpin->setSuffix(QStringLiteral(" s"));
    m_fdsVersionCombo = new QComboBox(this);
    m_fdsVersionCombo->setObjectName(QStringLiteral("ProjectFdsVersionCombo"));
    m_fdsVersionCombo->addItems(FdsSchemaRegistry::supportedVersions());
    m_fdsVersionCombo->setCurrentText(project ? project->fdsVersion()
                                                : FdsSchemaRegistry::defaultVersion());
    form->addRow(t("Name:"), m_nameEdit);
    form->addRow(QStringLiteral("CHID:"), m_chidEdit);
    form->addRow(t("End Time:"), m_endTimeSpin);
    form->addRow(t("FDS Version / Schema:"), m_fdsVersionCombo);
    m_displayUnitCombo = new QComboBox(this);
    m_displayUnitCombo->setObjectName(QStringLiteral("ProjectDisplayUnitCombo"));
    m_displayUnitCombo->addItem(QStringLiteral("m"), static_cast<int>(FcDisplayUnit::Meters));
    m_displayUnitCombo->addItem(QStringLiteral("cm"), static_cast<int>(FcDisplayUnit::Centimeters));
    m_displayUnitCombo->addItem(QStringLiteral("mm"), static_cast<int>(FcDisplayUnit::Millimeters));
    m_displayUnitCombo->addItem(QStringLiteral("ft"), static_cast<int>(FcDisplayUnit::Feet));
    m_displayUnitCombo->addItem(QStringLiteral("in"), static_cast<int>(FcDisplayUnit::Inches));
    const int unitIndex = m_displayUnitCombo->findData(static_cast<int>(
        project ? project->displayUnit() : FcDisplayUnit::Meters));
    m_displayUnitCombo->setCurrentIndex(qMax(0, unitIndex));
    form->addRow(t("Display Unit:"), m_displayUnitCombo);
    root->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &FdsProjectDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString FdsProjectDialog::projectName() const { return m_nameEdit->text().trimmed(); }
QString FdsProjectDialog::chid() const { return m_chidEdit->text().trimmed(); }
double FdsProjectDialog::endTime() const { return m_endTimeSpin->value(); }
QString FdsProjectDialog::fdsVersion() const { return m_fdsVersionCombo->currentText(); }
FcDisplayUnit FdsProjectDialog::displayUnit() const
{
    return static_cast<FcDisplayUnit>(m_displayUnitCombo->currentData().toInt());
}

void FdsProjectDialog::validateAndAccept()
{
    if (projectName().isEmpty() || chid().isEmpty()) {
        QMessageBox::warning(this,
                             t("Invalid Project Settings"),
                             t("Project name and CHID must not be empty."));
        return;
    }
    accept();
}
