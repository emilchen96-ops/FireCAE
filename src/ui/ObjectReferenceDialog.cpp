#include "ui/ObjectReferenceDialog.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcGeometryObject.h"
#include "ui/UiLanguage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QString t(const char* english)
{
    return UiLanguageManager::text(QString::fromUtf8(english));
}

QString typeName(FcObjectType type)
{
    switch (type) {
    case FcObjectType::Geometry: return t("Geometry");
    case FcObjectType::IfcModel: return t("IFC Model");
    case FcObjectType::IfcEntity: return t("IFC Entity");
    case FcObjectType::Mesh: return t("Mesh");
    case FcObjectType::MeshMultiplier: return t("Mesh Multiplier");
    case FcObjectType::SimulationParameter: return t("Simulation Parameter");
    case FcObjectType::Species: return t("Species");
    case FcObjectType::Obstruction: return t("Obstruction");
    case FcObjectType::Vent: return t("Vent");
    case FcObjectType::Material: return t("Material");
    case FcObjectType::Surface: return t("Surface");
    case FcObjectType::Reaction: return t("Reaction");
    case FcObjectType::Particle: return t("Particle");
    case FcObjectType::Property: return t("Property");
    case FcObjectType::Table: return t("Table");
    case FcObjectType::Ramp: return t("Ramp");
    case FcObjectType::Device: return t("Device");
    case FcObjectType::Control: return t("Control");
    case FcObjectType::HVAC: return t("HVAC");
    case FcObjectType::InitialCondition: return t("Initial Condition");
    case FcObjectType::Output: return t("Output");
    case FcObjectType::Result: return t("Result");
    case FcObjectType::ResultCase: return t("FDS Result Case");
    case FcObjectType::ResultFile: return t("FDS Result File");
    case FcObjectType::Folder: return t("Folder");
    case FcObjectType::Floor: return t("Floor");
    case FcObjectType::Group: return t("Group");
    default: return t("Object");
    }
}

QString friendlyRelation(const QString& field)
{
    const QString key = field.section(QLatin1Char('('), 0, 0).toUpper();
    if (key.startsWith(QStringLiteral("SURF_ID")) ||
        key == QStringLiteral("DEFAULT_SURFACE") ||
        key.startsWith(QStringLiteral("FACE_SURFACE:"))) return t("Surface assignment");
    if (key == QStringLiteral("MATL_ID")) return t("Material layer");
    if (key == QStringLiteral("CTRL_ID") || key == QStringLiteral("CONTROL")) return t("Control");
    if (key == QStringLiteral("HOST")) return t("Attached geometry");
    if (key.startsWith(QStringLiteral("RAMP_"))) return t("Time curve");
    if (key == QStringLiteral("DEVC_ID")) return t("Device");
    if (key == QStringLiteral("MULT_ID")) return t("Mesh Multiplier");
    if (key == QStringLiteral("PROP_ID")) return t("Property");
    if (key == QStringLiteral("PART_ID")) return t("Particle");
    if (key == QStringLiteral("REAC_ID")) return t("Reaction");
    if (key == QStringLiteral("SPEC_ID")) return t("Species");
    return t("Object reference");
}

QString objectPath(const FcObject& object)
{
    QStringList names;
    for (const FcObject* parent = object.parent(); parent; parent = parent->parent()) {
        // Group names are UI labels; names of user objects must not be translated.
        names.prepend(parent->type() == FcObjectType::Group
                          ? UiLanguageManager::text(parent->name()) : parent->name());
    }
    return names.join(QStringLiteral(" / "));
}

void collectOwners(const FcObject::Ptr& object,
                   const QString& targetUuid,
                   const QString& targetSurfaceFdsId,
                   QList<ObjectReferenceOwner>& owners)
{
    if (!object) return;
    QStringList keys;
    const auto addKey = [&keys](const QString& key) {
        if (!keys.contains(key)) keys.append(key);
    };
    QString keyword;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        keyword = namelist->keyword();
        for (const FcFdsParameter& parameter : namelist->parameters()) {
            if (parameter.kind == FcFdsParameterKind::ObjectReferences &&
                parameter.targetObjectIds.contains(targetUuid)) addKey(parameter.key);
        }
    }
    if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        if (geometry->defaultSurfaceId() == targetUuid) addKey(QStringLiteral("DEFAULT_SURFACE"));
        for (auto assignment = geometry->faceSurfaceIds().cbegin();
             assignment != geometry->faceSurfaceIds().cend(); ++assignment) {
            if (assignment.value() == targetUuid) {
                addKey(QStringLiteral("FACE_SURFACE:%1").arg(assignment.key()));
            }
        }
        if (geometry->hostObjectId() == targetUuid) addKey(QStringLiteral("HOST"));
        if (geometry->controlObjectId() == targetUuid) addKey(QStringLiteral("CONTROL"));
    }
    // Legacy strongly typed records store SURF_ID as an FDS identifier, not as a
    // display name. Resolve that semantic reference to the target, but continue
    // storing/navigating the owner's UUID in the dialog.
    if (!targetSurfaceFdsId.isEmpty()) {
        if (const auto obstruction = std::dynamic_pointer_cast<FcFdsObstruction>(object)) {
            keyword = QStringLiteral("OBST");
            if (obstruction->surfaceId() == targetSurfaceFdsId) addKey(QStringLiteral("SURF_ID"));
        }
        if (const auto vent = std::dynamic_pointer_cast<FcFdsVent>(object)) {
            keyword = QStringLiteral("VENT");
            if (vent->surfaceId() == targetSurfaceFdsId) addKey(QStringLiteral("SURF_ID"));
        }
    }
    if (!keys.isEmpty()) {
        const auto fdsObject = std::dynamic_pointer_cast<FcFdsObject>(object);
        owners.append({object->id(), object->name(), object->type(), keyword,
                       fdsObject ? fdsObject->fdsId() : QString{}, objectPath(*object), keys});
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectOwners(child, targetUuid, targetSurfaceFdsId, owners);
    }
}
}

QList<ObjectReferenceOwner> collectObjectReferenceOwners(
    const FcDocument& document, const QString& targetUuid)
{
    QList<ObjectReferenceOwner> owners;
    if (targetUuid.isEmpty()) return owners;
    const FcObject::Ptr target = document.findObject(targetUuid);
    if (!target) return owners;
    const auto surface = std::dynamic_pointer_cast<FcFdsObject>(target);
    const QString surfaceFdsId = surface && surface->type() == FcObjectType::Surface
                                    ? surface->fdsId() : QString{};
    for (const auto& group : document.groups()) {
        collectOwners(group, targetUuid, surfaceFdsId, owners);
    }
    return owners;
}

ObjectReferenceDialog::ObjectReferenceDialog(
    const FcObject& target, const QList<ObjectReferenceOwner>& owners, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ObjectReferenceDialog"));
    setWindowTitle(t("Cannot delete this object"));
    resize(760, 420);
    setMinimumSize(580, 320);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        t("Cannot delete \"%1\" because other objects use it.\nSelect an object below to locate it, then change or remove its reference before trying again.")
            .arg(target.name()), this);
    explanation->setObjectName(QStringLiteral("ObjectReferenceExplanation"));
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    m_ownerTable = new QTableWidget(owners.size(), 5, this);
    m_ownerTable->setObjectName(QStringLiteral("ObjectReferenceOwnersTable"));
    m_ownerTable->setHorizontalHeaderLabels({t("Object"), t("Type"), t("FDS ID"), t("Location"), t("Usage")});
    m_ownerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ownerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ownerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ownerTable->setWordWrap(false);
    m_ownerTable->verticalHeader()->hide();
    m_ownerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_ownerTable->horizontalHeader()->setStretchLastSection(true);
    m_ownerTable->setColumnWidth(0, 180);
    m_ownerTable->setColumnWidth(1, 80);
    m_ownerTable->setColumnWidth(2, 110);
    m_ownerTable->setColumnWidth(3, 160);
    QStringList technicalLines{
        t("Target object: %1\nUUID: %2").arg(target.name(), target.id())};
    for (int row = 0; row < owners.size(); ++row) {
        const ObjectReferenceOwner& owner = owners.at(row);
        QStringList usages;
        for (const QString& field : owner.fieldKeys) {
            const QString usage = friendlyRelation(field);
            if (!usages.contains(usage)) usages.append(usage);
        }
        const QStringList labels{owner.objectName, typeName(owner.objectType), owner.fdsId,
                                 owner.objectPath, usages.join(t(", "))};
        for (int column = 0; column < labels.size(); ++column) {
            auto* item = new QTableWidgetItem(labels.at(column));
            item->setData(Qt::UserRole, owner.objectUuid);
            item->setToolTip(labels.at(column));
            m_ownerTable->setItem(row, column, item);
        }
        technicalLines.append(
            t("Referencing object: %1\nUUID: %2\nFDS record: %3\nFDS ID: %4\nReference fields: %5")
                .arg(owner.objectName, owner.objectUuid, owner.keyword, owner.fdsId,
                     owner.fieldKeys.join(QStringLiteral(", "))));
    }
    layout->addWidget(m_ownerTable, 1);

    auto* detailsToggle = new QToolButton(this);
    detailsToggle->setObjectName(QStringLiteral("ObjectReferenceDetailsToggle"));
    detailsToggle->setText(t("Technical details"));
    detailsToggle->setCheckable(true);
    detailsToggle->setArrowType(Qt::RightArrow);
    detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(detailsToggle);
    auto* details = new QPlainTextEdit(technicalLines.join(QStringLiteral("\n\n")), this);
    details->setObjectName(QStringLiteral("ObjectReferenceTechnicalDetails"));
    details->setReadOnly(true);
    details->setLineWrapMode(QPlainTextEdit::NoWrap);
    details->setMaximumHeight(160);
    details->hide();
    layout->addWidget(details);

    auto* buttons = new QDialogButtonBox(this);
    auto* copyButton = buttons->addButton(t("Copy technical details"), QDialogButtonBox::ActionRole);
    copyButton->setObjectName(QStringLiteral("ObjectReferenceCopyDetailsButton"));
    copyButton->hide();
    auto* locateButton = buttons->addButton(t("Locate referencing object"), QDialogButtonBox::AcceptRole);
    locateButton->setObjectName(QStringLiteral("ObjectReferenceLocateButton"));
    auto* closeButton = buttons->addButton(t("Close"), QDialogButtonBox::RejectRole);
    closeButton->setObjectName(QStringLiteral("ObjectReferenceCloseButton"));
    closeButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!selectedOwnerUuid().isEmpty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(detailsToggle, &QToolButton::toggled, this, [detailsToggle, details, copyButton](bool checked) {
        detailsToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        details->setVisible(checked);
        copyButton->setVisible(checked);
    });
    connect(copyButton, &QPushButton::clicked, this, [details]() {
        QApplication::clipboard()->setText(details->toPlainText());
    });
    connect(m_ownerTable, &QTableWidget::itemSelectionChanged, this, [this, locateButton]() {
        locateButton->setEnabled(!selectedOwnerUuid().isEmpty());
    });
    locateButton->setEnabled(false);
    if (!owners.isEmpty()) m_ownerTable->selectRow(0);
}

QString ObjectReferenceDialog::selectedOwnerUuid() const
{
    const auto selected = m_ownerTable->selectedItems();
    return selected.isEmpty() ? QString{} : selected.constFirst()->data(Qt::UserRole).toString();
}
