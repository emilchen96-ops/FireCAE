#include "ui/SurfaceAssignmentDialog.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcGeometryObject.h"
#include "ui/UiLanguage.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QVBoxLayout>

#include <functional>

namespace
{
QString t(const char* text) { return UiLanguageManager::text(QString::fromLatin1(text)); }

void visitObjects(const FcObject::Ptr& root,
                  const std::function<void(const FcObject::Ptr&)>& visitor)
{
    if (!root) return;
    for (const FcObject::Ptr& child : root->children()) {
        if (!child) continue;
        visitor(child);
        visitObjects(child, visitor);
    }
}

QColor surfaceColor(const FcObject::Ptr& object)
{
    QString text;
    if (const auto surface = std::dynamic_pointer_cast<FcFdsSurface>(object)) {
        text = surface->color();
    } else if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        text = namelist->parameterValue(QStringLiteral("COLOR"));
        if (text.isEmpty()) {
            const QStringList rgb = namelist->parameterValue(QStringLiteral("RGB"))
                                        .split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (rgb.size() == 3) {
                return QColor(rgb[0].trimmed().toInt(), rgb[1].trimmed().toInt(),
                              rgb[2].trimmed().toInt());
            }
        }
    }
    text.remove(QLatin1Char('\''));
    const QColor color(text.trimmed());
    return color.isValid() ? color : QColor(180, 180, 180);
}
}

SurfaceAssignmentDialog::SurfaceAssignmentDialog(FcProject* project, int targetCount,
                                                 QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("SurfaceAssignmentDialog"));
    setWindowTitle(t("Assign Building Surfaces"));
    resize(620, 460);
    auto* layout = new QVBoxLayout(this);
    auto* summary = new QLabel(
        t("Assign default or local face surfaces to the selected geometry. Empty face overrides inherit the default surface by UUID."),
        this);
    summary->setWordWrap(true);
    summary->setText(summary->text() + QStringLiteral("\n") +
                     t("Selected geometry count:") + QStringLiteral(" ") +
                     QString::number(targetCount));
    layout->addWidget(summary);

    auto* form = new QFormLayout;
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("SurfaceAssignmentModeCombo"));
    m_mode->addItem(t("Assign to selected objects"), QStringLiteral("assign"));
    m_mode->addItem(t("Copy from another geometry object"), QStringLiteral("copy"));
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("SurfaceCopySourceCombo"));
    m_defaultSurface = new QComboBox(this);
    m_defaultSurface->setObjectName(QStringLiteral("BatchDefaultSurfaceCombo"));
    m_face = new QComboBox(this);
    m_face->setObjectName(QStringLiteral("BatchFaceChoiceCombo"));
    m_faceSurface = new QComboBox(this);
    m_faceSurface->setObjectName(QStringLiteral("BatchFaceSurfaceCombo"));
    m_replaceOverrides = new QCheckBox(t("Clear existing local overrides before assignment"), this);
    m_replaceOverrides->setObjectName(QStringLiteral("ReplaceSurfaceOverridesCheck"));
    m_copyTopology = new QCheckBox(t("Copy matching topological-face overrides"), this);
    m_copyTopology->setObjectName(QStringLiteral("CopyTopologyOverridesCheck"));
    m_copyTopology->setChecked(true);
    form->addRow(t("Operation:"), m_mode);
    form->addRow(t("Copy source (UUID):"), m_source);
    form->addRow(t("Default surface:"), m_defaultSurface);
    form->addRow(t("Local face override:"), m_face);
    form->addRow(t("Face surface:"), m_faceSurface);
    form->addRow(QString(), m_replaceOverrides);
    form->addRow(QString(), m_copyTopology);
    layout->addLayout(form);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("SurfaceColorTexturePreview"));
    m_preview->setMinimumHeight(92);
    m_preview->setWordWrap(true);
    layout->addWidget(m_preview);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SurfaceAssignmentDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    populateObjects();
    connect(m_mode, &QComboBox::currentIndexChanged, this,
            [this]() { updateMode(); });
    connect(m_defaultSurface, &QComboBox::currentIndexChanged, this,
            [this]() { updateSurfacePreview(); });
    connect(m_faceSurface, &QComboBox::currentIndexChanged, this,
            [this]() { updateSurfacePreview(); });
    updateMode();
    updateSurfacePreview();
}

QString SurfaceAssignmentDialog::keepValue() { return QStringLiteral("__KEEP__"); }
QString SurfaceAssignmentDialog::clearAllFacesValue() { return QStringLiteral("__CLEAR_ALL__"); }
QString SurfaceAssignmentDialog::allTopologyFacesValue() { return QStringLiteral("__ALL_TOPOLOGY__"); }
bool SurfaceAssignmentDialog::copyMode() const
{
    return m_mode->currentData().toString() == QStringLiteral("copy");
}
QString SurfaceAssignmentDialog::sourceGeometryId() const { return m_source->currentData().toString(); }
QString SurfaceAssignmentDialog::defaultChoice() const { return m_defaultSurface->currentData().toString(); }
QString SurfaceAssignmentDialog::faceChoice() const { return m_face->currentData().toString(); }
QString SurfaceAssignmentDialog::faceSurfaceId() const { return m_faceSurface->currentData().toString(); }
bool SurfaceAssignmentDialog::replaceOverrides() const { return m_replaceOverrides->isChecked(); }
bool SurfaceAssignmentDialog::copyTopologyOverrides() const { return m_copyTopology->isChecked(); }

void SurfaceAssignmentDialog::populateObjects()
{
    m_source->addItem(t("Choose source geometry"), QString());
    m_defaultSurface->addItem(t("Keep current default"), keepValue());
    m_defaultSurface->addItem(t("None"), QString());
    m_faceSurface->addItem(t("Inherit default"), QString());
    m_face->addItem(t("Do not change local overrides"), keepValue());
    m_face->addItem(t("Clear all local overrides"), clearAllFacesValue());
    m_face->addItem(t("All topological faces"), allTopologyFacesValue());
    for (const QString& direction : {QStringLiteral("X-"), QStringLiteral("X+"),
                                     QStringLiteral("Y-"), QStringLiteral("Y+"),
                                     QStringLiteral("Z-"), QStringLiteral("Z+")}) {
        m_face->addItem(direction, direction);
    }
    if (!m_project || !m_project->document()) return;
    visitObjects(m_project->document()->geometryGroup(), [this](const FcObject::Ptr& object) {
        if (std::dynamic_pointer_cast<FcGeometryObject>(object)) {
            m_source->addItem(object->name(), object->id());
        }
    });
    visitObjects(m_project->document()->surfacesGroup(), [this](const FcObject::Ptr& object) {
        if (object->type() != FcObjectType::Surface) return;
        const QString label = QStringLiteral("%1  [%2]").arg(object->name(), object->id());
        m_defaultSurface->addItem(label, object->id());
        m_faceSurface->addItem(label, object->id());
    });
}

void SurfaceAssignmentDialog::updateMode()
{
    const bool copy = copyMode();
    m_source->setEnabled(copy);
    m_copyTopology->setEnabled(copy);
    m_defaultSurface->setEnabled(!copy);
    m_face->setEnabled(!copy);
    m_faceSurface->setEnabled(!copy);
    m_replaceOverrides->setEnabled(!copy);
}

void SurfaceAssignmentDialog::updateSurfacePreview()
{
    QString uuid = m_faceSurface->currentData().toString();
    if (uuid.isEmpty()) uuid = m_defaultSurface->currentData().toString();
    if (!m_project || !m_project->document() || uuid.isEmpty() || uuid == keepValue()) {
        m_preview->setStyleSheet(QStringLiteral("QLabel { border: 1px solid #888; padding: 8px; }") );
        m_preview->setText(t("Surface preview: inherited, unassigned, or unchanged."));
        m_preview->setPixmap(QPixmap());
        return;
    }
    const FcObject::Ptr object = m_project->document()->findObject(uuid);
    if (!object) return;
    QString texture;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        texture = namelist->parameterValue(QStringLiteral("TEXTURE_MAP"));
        texture.remove(QLatin1Char('\''));
    }
    const QColor color = surfaceColor(object);
    m_preview->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid #555; padding: 8px; }")
                                 .arg(color.name(), color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
    m_preview->setText(QStringLiteral("%1\nUUID: %2\n%3: %4")
                           .arg(object->name(), uuid, t("Texture"),
                                texture.isEmpty() ? t("None") : texture));
    if (!texture.isEmpty() && QFileInfo::exists(texture)) {
        QPixmap pixmap(texture);
        if (!pixmap.isNull()) m_preview->setPixmap(pixmap.scaledToHeight(72, Qt::SmoothTransformation));
    }
}

void SurfaceAssignmentDialog::accept()
{
    if (copyMode() && sourceGeometryId().isEmpty()) {
        QMessageBox::warning(this, t("Surface Assignment"),
                             t("Choose a source geometry object."));
        return;
    }
    QDialog::accept();
}
