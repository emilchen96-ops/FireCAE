#include "ui/FloorEditorDialog.h"

#include "core/FcProject.h"
#include "ui/UiLanguage.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
QString t(const char* value)
{
    return UiLanguageManager::text(QString::fromUtf8(value));
}
}

FloorEditorDialog::FloorEditorDialog(const FcProject* project, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("FloorEditorDialog"));
    setWindowTitle(t("Floor Properties"));
    resize(620, 620);
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        t("Define the architectural level used by 2D drawing, object placement and display clipping."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    const QString suffix = QStringLiteral(" %1")
                               .arg(project ? project->displayUnitSymbol()
                                            : QStringLiteral("m"));
    const auto makeLength = [this, &suffix](const QString& objectName,
                                            double minimum,
                                            double maximum) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(objectName);
        spin->setRange(minimum, maximum);
        spin->setDecimals(4);
        spin->setSuffix(suffix);
        return spin;
    };
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("FloorNameEdit"));
    m_baseElevation = makeLength(QStringLiteral("FloorBaseElevationSpin"), -1.0e9, 1.0e9);
    m_storeyHeight = makeLength(QStringLiteral("FloorStoreyHeightSpin"), 0.001, 1.0e9);
    m_slabThickness = makeLength(QStringLiteral("FloorSlabThicknessSpin"), 0.001, 1.0e9);
    m_wallHeight = makeLength(QStringLiteral("FloorWallHeightSpin"), 0.001, 1.0e9);
    m_storeyHeight->setValue(project ? project->metersToDisplay(3.0) : 3.0);
    m_slabThickness->setValue(project ? project->metersToDisplay(0.2) : 0.2);
    m_wallHeight->setValue(project ? project->metersToDisplay(3.0) : 3.0);

    auto* form = new QFormLayout;
    form->addRow(t("Name:"), m_nameEdit);
    form->addRow(t("Base Elevation:"), m_baseElevation);
    form->addRow(t("Default Storey Height:"), m_storeyHeight);
    form->addRow(t("Default Slab Thickness:"), m_slabThickness);
    form->addRow(t("Default Wall Height:"), m_wallHeight);
    root->addLayout(form);

    auto* backgroundGroup = new QGroupBox(t("2D Background Image"), this);
    auto* backgroundLayout = new QHBoxLayout(backgroundGroup);
    m_backgroundImage = new QLineEdit(backgroundGroup);
    m_backgroundImage->setObjectName(QStringLiteral("FloorBackgroundImageEdit"));
    auto* browse = new QPushButton(t("Browse..."), backgroundGroup);
    browse->setObjectName(QStringLiteral("FloorBackgroundBrowseButton"));
    backgroundLayout->addWidget(m_backgroundImage, 1);
    backgroundLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this, t("Select 2D Background Image"), {},
            t("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
        if (!filePath.isEmpty()) m_backgroundImage->setText(filePath);
    });
    root->addWidget(backgroundGroup);

    auto* clippingGroup = new QGroupBox(t("Display Clipping Range"), this);
    auto* clippingForm = new QFormLayout(clippingGroup);
    m_clippingEnabled = new QCheckBox(t("Enable clipping for this floor"), clippingGroup);
    m_clippingEnabled->setObjectName(QStringLiteral("FloorClippingEnabledCheck"));
    m_clipXMin = makeLength(QStringLiteral("FloorClipXMinSpin"), -1.0e9, 1.0e9);
    m_clipXMax = makeLength(QStringLiteral("FloorClipXMaxSpin"), -1.0e9, 1.0e9);
    m_clipYMin = makeLength(QStringLiteral("FloorClipYMinSpin"), -1.0e9, 1.0e9);
    m_clipYMax = makeLength(QStringLiteral("FloorClipYMaxSpin"), -1.0e9, 1.0e9);
    m_clipZMin = makeLength(QStringLiteral("FloorClipZMinSpin"), -1.0e9, 1.0e9);
    m_clipZMax = makeLength(QStringLiteral("FloorClipZMaxSpin"), -1.0e9, 1.0e9);
    m_clipXMin->setValue(project ? project->metersToDisplay(-50.0) : -50.0);
    m_clipXMax->setValue(project ? project->metersToDisplay(50.0) : 50.0);
    m_clipYMin->setValue(project ? project->metersToDisplay(-50.0) : -50.0);
    m_clipYMax->setValue(project ? project->metersToDisplay(50.0) : 50.0);
    m_clipZMin->setValue(0.0);
    m_clipZMax->setValue(project ? project->metersToDisplay(3.0) : 3.0);
    clippingForm->addRow(QString(), m_clippingEnabled);
    clippingForm->addRow(t("X Minimum:"), m_clipXMin);
    clippingForm->addRow(t("X Maximum:"), m_clipXMax);
    clippingForm->addRow(t("Y Minimum:"), m_clipYMin);
    clippingForm->addRow(t("Y Maximum:"), m_clipYMax);
    clippingForm->addRow(t("Z Minimum:"), m_clipZMin);
    clippingForm->addRow(t("Z Maximum:"), m_clipZMax);
    const auto updateClippingEnabled = [this]() {
        const bool enabled = m_clippingEnabled->isChecked();
        for (QDoubleSpinBox* spin : {m_clipXMin, m_clipXMax, m_clipYMin,
                                     m_clipYMax, m_clipZMin, m_clipZMax}) {
            spin->setEnabled(enabled);
        }
    };
    connect(m_clippingEnabled, &QCheckBox::toggled, this,
            [updateClippingEnabled]() { updateClippingEnabled(); });
    updateClippingEnabled();
    root->addWidget(clippingGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        const FloorEditorData values = data();
        if (values.name.trimmed().isEmpty()) {
            QMessageBox::warning(this, t("Floor Validation"),
                                 t("Floor name cannot be empty."));
            return;
        }
        if (!values.clippingRange.isValid()) {
            QMessageBox::warning(this, t("Floor Validation"),
                                 t("Each clipping minimum must be smaller than its maximum."));
            return;
        }
        accept();
    });
    root->addWidget(buttons);
}

void FloorEditorDialog::setFloor(const FcFloorObject& floor)
{
    const auto display = [this](double value) {
        return m_project ? m_project->metersToDisplay(value) : value;
    };
    m_nameEdit->setText(floor.name());
    m_baseElevation->setValue(display(floor.baseElevation()));
    m_storeyHeight->setValue(display(floor.defaultStoreyHeight()));
    m_slabThickness->setValue(display(floor.defaultSlabThickness()));
    m_wallHeight->setValue(display(floor.defaultWallHeight()));
    m_backgroundImage->setText(floor.backgroundImagePath());
    m_clippingEnabled->setChecked(floor.clippingEnabled());
    const FcFloorClipRange& clip = floor.clippingRange();
    m_clipXMin->setValue(display(clip.xMin));
    m_clipXMax->setValue(display(clip.xMax));
    m_clipYMin->setValue(display(clip.yMin));
    m_clipYMax->setValue(display(clip.yMax));
    m_clipZMin->setValue(display(clip.zMin));
    m_clipZMax->setValue(display(clip.zMax));
}

FloorEditorData FloorEditorDialog::data() const
{
    const auto meters = [this](double value) {
        return m_project ? m_project->displayToMeters(value) : value;
    };
    FloorEditorData result;
    result.name = m_nameEdit->text().trimmed();
    result.baseElevation = meters(m_baseElevation->value());
    result.storeyHeight = meters(m_storeyHeight->value());
    result.slabThickness = meters(m_slabThickness->value());
    result.wallHeight = meters(m_wallHeight->value());
    result.backgroundImagePath = m_backgroundImage->text().trimmed();
    result.clippingEnabled = m_clippingEnabled->isChecked();
    result.clippingRange = {meters(m_clipXMin->value()), meters(m_clipXMax->value()),
                            meters(m_clipYMin->value()), meters(m_clipYMax->value()),
                            meters(m_clipZMin->value()), meters(m_clipZMax->value())};
    return result;
}
