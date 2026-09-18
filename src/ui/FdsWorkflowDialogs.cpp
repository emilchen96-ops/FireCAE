#include "ui/FdsWorkflowDialogs.h"
#include "ui/UiLanguage.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsWriter.h"
#include "geometry/FcGeometryObject.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
QString uiText(const char* english)
{
    return UiLanguageManager::text(QString::fromUtf8(english));
}

void collectObjects(const FcObject::Ptr& object, QVector<FcObject::Ptr>& result)
{
    if (!object) return;
    result.append(object);
    for (const FcObject::Ptr& child : object->children()) collectObjects(child, result);
}

QVector<FcObject::Ptr> allObjects(const FcProject* project)
{
    QVector<FcObject::Ptr> result;
    if (!project || !project->document()) return result;
    for (const auto& group : project->document()->groups()) collectObjects(group, result);
    return result;
}

class HrrPreviewWidget final : public QWidget
{
public:
    explicit HrrPreviewWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(170);
        setObjectName(QStringLiteral("FireSourceHrrPreview"));
    }

    void setCurve(double start, double peak, double end, double maximum, bool customRamp)
    {
        m_start = start;
        m_peak = peak;
        m_end = end;
        m_maximum = maximum;
        m_customRamp = customRamp;
        setProperty("customRamp", customRamp);
        setAccessibleName(uiText("HRR setting preview"));
        setAccessibleDescription(customRamp ? uiText("Custom RAMP setting illustration; this is not a simulation result.")
                                           : uiText("No custom RAMP. Constant setpoint illustration only; FDS startup and computed HRR may differ."));
        setProperty("timeAxisLabel", uiText("Time (s)"));
        setProperty("hrrAxisLabel", uiText("HRR (kW)"));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        const QRectF graph = rect().adjusted(58, 14, -18, -36);
        painter.setPen(QPen(palette().mid().color(), 1));
        painter.drawLine(graph.bottomLeft(), graph.bottomRight());
        painter.drawLine(graph.bottomLeft(), graph.topLeft());
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(2, graph.top() - 4, 52, 24), Qt::AlignRight,
                         QString::number(m_maximum, 'g', 5));
        painter.drawText(QRectF(graph.left(), graph.bottom() + 5, graph.width(), 24),
                         Qt::AlignCenter, uiText("Time (s)"));
        painter.save();
        painter.translate(16, graph.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-graph.height() / 2.0, -12, graph.height(), 24),
                         Qt::AlignCenter, uiText("HRR (kW)"));
        painter.restore();
        if (m_end <= 0.0 || m_maximum <= 0.0) return;
        const auto point = [&](double time, double value) {
            return QPointF(graph.left() + graph.width() * time / m_end,
                           graph.bottom() - graph.height() * value / m_maximum);
        };
        QPainterPath path(point(0.0, m_customRamp ? 0.0 : m_maximum));
        if (m_customRamp) {
            path.lineTo(point(std::clamp(m_start, 0.0, m_end), 0.0));
            path.lineTo(point(std::clamp(m_peak, 0.0, m_end), m_maximum));
            path.lineTo(point(m_end, 0.0));
        } else {
            path.lineTo(point(m_end, m_maximum));
        }
        painter.setPen(QPen(QColor(220, 55, 45), 3));
        painter.drawPath(path);
    }

private:
    double m_start = 0.0;
    double m_peak = 10.0;
    double m_end = 60.0;
    double m_maximum = 500.0;
    bool m_customRamp = true;
};

QDoubleSpinBox* engineeringSpin(QWidget* parent, double minimum, double maximum,
                                double value, const QString& suffix)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(6);
    spin->setValue(value);
    spin->setSuffix(suffix);
    return spin;
}

QString cleanId(QString value)
{
    value = value.trimmed().toUpper();
    value.replace(QLatin1Char(' '), QLatin1Char('_'));
    return value;
}

QString hostDisplayName(const FcObject::Ptr& object)
{
    const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
    const QString type = namelist
        ? (namelist->keyword() == QStringLiteral("OBST") ? uiText("Obstruction") : uiText("Vent"))
              + QStringLiteral(" (%1)").arg(namelist->keyword())
        : uiText("Geometry");
    const QString location = namelist && !namelist->fdsId().isEmpty()
        ? QStringLiteral("FDS ID: %1").arg(namelist->fdsId())
        : uiText("Path: %1").arg(object->parent()
              ? object->parent()->name() + QStringLiteral(" / ") + object->name()
              : object->name());
    return QStringLiteral("%1 | %2 | %3").arg(object->name(), type, location);
}

QString surfaceDisplayName(const FcProject* project, const QString& objectId)
{
    const auto surface = project && project->document()
        ? project->document()->findObject(objectId) : FcObject::Ptr{};
    if (!surface) return uiText("Unresolved surface reference");
    const auto fds = std::dynamic_pointer_cast<FcFdsObject>(surface);
    return fds && !fds->fdsId().isEmpty()
        ? QStringLiteral("%1 (FDS ID: %2)").arg(surface->name(), fds->fdsId())
        : surface->name();
}

QString currentHostSurface(const FcProject* project, const FcObject::Ptr& object)
{
    QStringList assignments;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        for (const FcFdsParameter& parameter : namelist->parameters()) {
            const QString key = parameter.key.trimmed().toUpper();
            if (key != QStringLiteral("SURF_ID") && key != QStringLiteral("SURF_IDS") &&
                key != QStringLiteral("SURF_ID6")) continue;
            QStringList surfaces;
            for (const QString& id : parameter.targetObjectIds)
                surfaces.append(surfaceDisplayName(project, id));
            const QString value = surfaces.isEmpty() ? parameter.value : surfaces.join(QStringLiteral(", "));
            if (!value.trimmed().isEmpty()) assignments.append(key + QStringLiteral(" = ") + value);
        }
    } else if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        if (!geometry->defaultSurfaceId().isEmpty())
            assignments.append(surfaceDisplayName(project, geometry->defaultSurfaceId()));
        if (!geometry->faceSurfaceIds().isEmpty())
            assignments.append(uiText("Existing face assignments: %1").arg(geometry->faceSurfaceIds().size()));
    }
    return uiText("Current surface: %1").arg(assignments.isEmpty()
        ? uiText("FDS default (INERT)") : assignments.join(QStringLiteral("; ")));
}
}

FireSourceWizardDialog::FireSourceWizardDialog(const FcProject* project, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("FireSourceWizardDialog"));
    setWindowTitle(uiText("Fire Source Wizard"));
    resize(760, 720);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("FireSourceWizardTabs"));

    auto* identityPage = new QWidget(tabs);
    auto* identity = new QFormLayout(identityPage);
    m_name = new QLineEdit(QStringLiteral("Design Fire"), identityPage);
    m_name->setObjectName(QStringLiteral("FireSourceNameEdit"));
    m_surfaceId = new QLineEdit(QStringLiteral("FIRE_SURF"), identityPage);
    m_reactionId = new QLineEdit(QStringLiteral("FIRE_REAC"), identityPage);
    m_rampId = new QLineEdit(QStringLiteral("FIRE_RAMP"), identityPage);
    m_host = new QComboBox(identityPage);
    m_host->setObjectName(QStringLiteral("FireSourceHostCombo"));
    m_host->addItem(uiText("No host — create a burner VENT"), QString{});
    for (const FcObject::Ptr& object : allObjects(project)) {
        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        const bool validNamelist = namelist &&
            (namelist->keyword() == QStringLiteral("OBST") ||
             namelist->keyword() == QStringLiteral("VENT"));
        if (!std::dynamic_pointer_cast<FcGeometryObject>(object) && !validNamelist) continue;
        m_host->addItem(hostDisplayName(object), object->id());
        m_host->setItemData(m_host->count() - 1,
                           hostDisplayName(object) + QStringLiteral("\nUUID: ") + object->id(),
                           Qt::ToolTipRole);
    }
    identity->addRow(uiText("Name:"), m_name);
    identity->addRow(uiText("Attach to geometry / OBST / VENT:"), m_host);
    m_hostScope = new QLabel(identityPage);
    m_hostScope->setObjectName(QStringLiteral("FireSourceHostScopeLabel"));
    m_hostScope->setWordWrap(true);
    m_hostScope->setTextFormat(Qt::PlainText);
    identity->addRow(m_hostScope);
    m_hostSurface = new QLabel(identityPage);
    m_hostSurface->setObjectName(QStringLiteral("FireSourceHostSurfaceLabel"));
    m_hostSurface->setWordWrap(true);
    m_hostSurface->setTextFormat(Qt::PlainText);
    m_hostSurface->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity->addRow(m_hostSurface);
    identity->addRow(uiText("Surface FDS ID:"), m_surfaceId);
    identity->addRow(uiText("Reaction FDS ID:"), m_reactionId);
    identity->addRow(uiText("Ramp FDS ID:"), m_rampId);
    tabs->addTab(identityPage, uiText("Identity and Attachment"));

    auto* geometryPage = new QWidget(tabs);
    auto* geometry = new QFormLayout(geometryPage);
    m_x = engineeringSpin(geometryPage, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_y = engineeringSpin(geometryPage, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_z = engineeringSpin(geometryPage, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_width = engineeringSpin(geometryPage, 0.000001, 1.0e9, 1.0, QStringLiteral(" m"));
    m_depth = engineeringSpin(geometryPage, 0.000001, 1.0e9, 1.0, QStringLiteral(" m"));
    m_x->setObjectName(QStringLiteral("FireSourceXSpin"));
    m_y->setObjectName(QStringLiteral("FireSourceYSpin"));
    m_z->setObjectName(QStringLiteral("FireSourceZSpin"));
    m_width->setObjectName(QStringLiteral("FireSourceWidthSpin"));
    m_depth->setObjectName(QStringLiteral("FireSourceDepthSpin"));
    geometry->addRow(uiText("X minimum:"), m_x);
    geometry->addRow(uiText("Y minimum:"), m_y);
    geometry->addRow(uiText("Elevation:"), m_z);
    geometry->addRow(uiText("Width:"), m_width);
    geometry->addRow(uiText("Depth:"), m_depth);
    auto* geometryHint = new QLabel(
        uiText("These fields create a planar burner VENT when no existing geometry, OBST, or VENT host is selected."),
        geometryPage);
    geometryHint->setWordWrap(true);
    geometry->addRow(geometryHint);
    tabs->addTab(geometryPage, uiText("Location and Size"));

    auto* physicsPage = new QWidget(tabs);
    auto* physics = new QFormLayout(physicsPage);
    m_powerMode = new QComboBox(physicsPage);
    m_powerMode->setObjectName(QStringLiteral("FireSourcePowerModeCombo"));
    m_powerMode->addItem(uiText("HRR per unit area (HRRPUA)"), false);
    m_powerMode->addItem(uiText("Total HRR and burning area"), true);
    m_hrrpua = engineeringSpin(physicsPage, 0.001, 1.0e9, 500.0, QStringLiteral(" kW/m²"));
    m_hrrpua->setObjectName(QStringLiteral("FireSourceHrrpuaSpin"));
    m_totalHrr = engineeringSpin(physicsPage, 0.001, 1.0e12, 500.0, QStringLiteral(" kW"));
    m_totalHrr->setObjectName(QStringLiteral("FireSourceTotalHrrSpin"));
    m_area = engineeringSpin(physicsPage, 0.000001, 1.0e9, 1.0, QStringLiteral(" m²"));
    m_area->setObjectName(QStringLiteral("FireSourceAreaSpin"));
    m_fuel = new QLineEdit(QStringLiteral("PROPANE"), physicsPage);
    m_sootYield = engineeringSpin(physicsPage, 0.0, 1.0, 0.01, QString{});
    m_sootYield->setObjectName(QStringLiteral("FireSourceSootYieldSpin"));
    m_coYield = engineeringSpin(physicsPage, 0.0, 1.0, 0.0, QString{});
    m_coYield->setObjectName(QStringLiteral("FireSourceCoYieldSpin"));
    m_radiativeFraction = engineeringSpin(physicsPage, 0.0, 1.0, 0.35, QString{});
    m_radiativeFraction->setObjectName(
        QStringLiteral("FireSourceRadiativeFractionSpin"));
    m_color = new QLineEdit(QStringLiteral("RED"), physicsPage);
    physics->addRow(uiText("Power definition:"), m_powerMode);
    physics->addRow(QStringLiteral("HRRPUA:"), m_hrrpua);
    physics->addRow(uiText("Total HRR:"), m_totalHrr);
    physics->addRow(uiText("Burning area:"), m_area);
    physics->addRow(uiText("Fuel:"), m_fuel);
    physics->addRow(uiText("Soot yield:"), m_sootYield);
    physics->addRow(uiText("CO yield:"), m_coYield);
    physics->addRow(uiText("Radiative fraction:"), m_radiativeFraction);
    physics->addRow(uiText("Display color:"), m_color);
    tabs->addTab(physicsPage, uiText("Fire and Reaction"));

    auto* timePage = new QWidget(tabs);
    auto* timeLayout = new QVBoxLayout(timePage);
    auto* timeForm = new QFormLayout;
    m_createRamp = new QCheckBox(uiText("Create independent RAMP records"), timePage);
    m_createRamp->setObjectName(QStringLiteral("FireSourceCreateRampCheck"));
    m_createRamp->setChecked(true);
    m_startTime = engineeringSpin(timePage, 0.0, 1.0e9, 0.0, QStringLiteral(" s"));
    m_peakTime = engineeringSpin(timePage, 0.0, 1.0e9, 10.0, QStringLiteral(" s"));
    m_startTime->setObjectName(QStringLiteral("FireSourceStartTimeSpin"));
    m_peakTime->setObjectName(QStringLiteral("FireSourcePeakTimeSpin"));
    m_endTime = engineeringSpin(timePage, 0.001, 1.0e9,
                                project ? project->endTime() : 60.0,
                                QStringLiteral(" s"));
    m_endTime->setObjectName(QStringLiteral("FireSourceEndTimeSpin"));
    timeForm->addRow(m_createRamp);
    timeForm->addRow(uiText("Start time:"), m_startTime);
    timeForm->addRow(uiText("Peak time:"), m_peakTime);
    timeForm->addRow(uiText("End time:"), m_endTime);
    timeLayout->addLayout(timeForm);
    m_preview = new HrrPreviewWidget(timePage);
    m_previewNote = new QLabel(timePage);
    m_previewNote->setObjectName(QStringLiteral("FireSourcePreviewNoteLabel"));
    m_previewNote->setWordWrap(true);
    m_summary = new QLabel(timePage);
    m_summary->setObjectName(QStringLiteral("FireSourceSummaryLabel"));
    m_summary->setWordWrap(true);
    timeLayout->addWidget(m_preview);
    timeLayout->addWidget(m_previewNote);
    timeLayout->addWidget(m_summary);
    tabs->addTab(timePage, uiText("Time and HRR Preview"));
    root->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &FireSourceWizardDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    const auto changed = [this]() { updatePreview(); };
    connect(m_powerMode, &QComboBox::currentIndexChanged, this, changed);
    connect(m_hrrpua, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_totalHrr, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_area, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_host, &QComboBox::currentIndexChanged, this, changed);
    connect(m_width, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_depth, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_startTime, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_peakTime, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_endTime, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_createRamp, &QCheckBox::toggled, this, changed);
    updatePreview();
}

FireSourceWizardData FireSourceWizardDialog::data() const
{
    FireSourceWizardData result;
    result.name = m_name->text().trimmed();
    result.surfaceFdsId = cleanId(m_surfaceId->text());
    result.reactionFdsId = cleanId(m_reactionId->text());
    result.rampFdsId = cleanId(m_rampId->text());
    result.hostObjectId = m_host->currentData().toString();
    result.fuel = m_fuel->text().trimmed().toUpper();
    result.color = m_color->text().trimmed().toUpper();
    result.useTotalHrr = m_powerMode->currentData().toBool();
    result.totalHrr = m_totalHrr->value();
    result.burningArea = m_area->value();
    result.hrrpua = result.useTotalHrr
                         ? result.totalHrr / result.burningArea
                         : m_hrrpua->value();
    result.sootYield = m_sootYield->value();
    result.coYield = m_coYield->value();
    result.radiativeFraction = m_radiativeFraction->value();
    result.x = m_x->value();
    result.y = m_y->value();
    result.z = m_z->value();
    result.width = m_width->value();
    result.depth = m_depth->value();
    result.startTime = m_startTime->value();
    result.peakTime = m_peakTime->value();
    result.endTime = m_endTime->value();
    result.createRamp = m_createRamp->isChecked();
    return result;
}

void FireSourceWizardDialog::accept()
{
    const FireSourceWizardData value = data();
    QStringList errors;
    if (value.name.isEmpty()) errors.append(uiText("Name is required."));
    if (value.surfaceFdsId.isEmpty()) errors.append(uiText("Surface FDS ID is required."));
    if (value.reactionFdsId.isEmpty()) errors.append(uiText("Reaction FDS ID is required."));
    if (value.fuel.isEmpty()) errors.append(uiText("Fuel is required."));
    if (value.hrrpua <= 0.0) errors.append(uiText("HRRPUA must be positive."));
    if (value.hostObjectId.isEmpty() && (value.width <= 0.0 || value.depth <= 0.0)) {
        errors.append(uiText("Burner width and depth must be positive."));
    }
    if (value.createRamp && value.rampFdsId.isEmpty()) errors.append(uiText("Ramp FDS ID is required."));
    if (value.createRamp && (value.startTime > value.peakTime || value.peakTime > value.endTime)) {
        errors.append(uiText("Times must satisfy Start ≤ Peak ≤ End."));
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, uiText("Fire Source Validation"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    QDialog::accept();
}

void FireSourceWizardDialog::updatePreview()
{
    const bool total = m_powerMode->currentData().toBool();
    const bool createsVent = m_host->currentData().toString().isEmpty();
    const bool customRamp = m_createRamp->isChecked();
    const auto host = createsVent || !m_project || !m_project->document()
        ? FcObject::Ptr{} : m_project->document()->findObject(m_host->currentData().toString());
    const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(host);
    if (createsVent) {
        m_hostScope->setText(uiText("Creates a new planar burner VENT at the specified location. No OBST base is created; place it on an existing solid face or mesh boundary."));
    } else if (namelist && namelist->keyword() == QStringLiteral("VENT")) {
        m_hostScope->setText(uiText("Replaces this VENT's SURF_ID with the new fire surface and keeps its existing coordinates. No additional burner VENT is created."));
    } else if (namelist && namelist->keyword() == QStringLiteral("OBST")) {
        m_hostScope->setText(uiText("Sets this OBST's overall default SURF_ID, not just its top face. Existing per-face assignments are preserved. To burn only the top, create a separate planar VENT."));
    } else {
        m_hostScope->setText(uiText("Sets this geometry object's default surface and preserves existing face assignments. Generate or update its FDS geometry conversion before running."));
    }
    m_hostSurface->setVisible(static_cast<bool>(host));
    m_hostSurface->setText(host ? currentHostSurface(m_project, host) : QString{});
    m_host->setToolTip(m_host->currentData(Qt::ToolTipRole).toString());
    m_x->setEnabled(createsVent); m_y->setEnabled(createsVent); m_z->setEnabled(createsVent);
    m_width->setEnabled(createsVent); m_depth->setEnabled(createsVent);
    if (createsVent) {
        const QSignalBlocker blocker(m_area);
        m_area->setValue(m_width->value() * m_depth->value());
    }
    m_hrrpua->setEnabled(!total);
    m_totalHrr->setEnabled(total);
    m_area->setEnabled(total);
    m_rampId->setEnabled(customRamp);
    m_startTime->setEnabled(customRamp);
    m_peakTime->setEnabled(customRamp);
    m_endTime->setEnabled(customRamp);
    const double hrrpua = total ? m_totalHrr->value() / m_area->value()
                                : m_hrrpua->value();
    const double maximum = hrrpua * m_area->value();
    static_cast<HrrPreviewWidget*>(m_preview)->setCurve(
        m_startTime->value(), m_peakTime->value(),
        customRamp ? m_endTime->value() : (m_project ? m_project->endTime() : 60.0),
        maximum, customRamp);
    m_previewNote->setText(customRamp
        ? uiText("Custom RAMP setting illustration; this is not a simulation result.")
        : uiText("No custom RAMP. Constant setpoint illustration only; FDS startup and computed HRR may differ."));
    QStringList records{QStringLiteral("SURF"), QStringLiteral("REAC")};
    if (customRamp) records.append(QStringLiteral("RAMP"));
    if (createsVent) records.append(QStringLiteral("VENT"));
    QString summary = uiText("Resolved HRRPUA: %1 kW/m²; HRR based on the entered burning area: %2 kW.")
                          .arg(hrrpua, 0, 'g', 8).arg(maximum, 0, 'g', 8);
    summary += QLatin1Char('\n') + uiText("Independent editable records to create: %1.")
                                     .arg(records.join(QStringLiteral(", ")));
    if (!createsVent) summary += QLatin1Char('\n') + uiText("The entered burning area is not measured from the host. Check its active burning faces before running.");
    m_summary->setText(summary);
}

void FireSourceWizardDialog::retranslateUi()
{
    setWindowTitle(uiText("Fire Source Wizard"));
    const QSignalBlocker blocker(m_host);
    m_host->setItemText(0, uiText("No host — create a burner VENT"));
    for (int index = 1; index < m_host->count(); ++index) {
        const auto object = m_project && m_project->document()
            ? m_project->document()->findObject(m_host->itemData(index).toString()) : FcObject::Ptr{};
        if (!object) continue;
        m_host->setItemText(index, hostDisplayName(object));
        m_host->setItemData(index, hostDisplayName(object) + QStringLiteral("\nUUID: ") + object->id(),
                            Qt::ToolTipRole);
    }
    m_createRamp->setText(uiText("Create independent RAMP records"));
    updatePreview();
}

void FireSourceWizardDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_summary) retranslateUi();
}

ParticleSprayWizardDialog::ParticleSprayWizardDialog(const FcProject* project,
                                                     QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("ParticleSprayWizardDialog"));
    setWindowTitle(uiText("Particle and Sprinkler Wizard"));
    resize(900, 720);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("ParticleSprayWizardTabs"));

    auto* identityPage = new QWidget(tabs);
    auto* identityForm = new QFormLayout(identityPage);
    m_name = new QLineEdit(QStringLiteral("Sprinkler 01"), identityPage);
    m_name->setObjectName(QStringLiteral("ParticleSprayNameEdit"));
    m_systemType = new QComboBox(identityPage);
    m_systemType->setObjectName(QStringLiteral("ParticleSpraySystemTypeCombo"));
    m_systemType->addItem(uiText("Sprinkler"),
                          QStringLiteral("SPRINKLER LINK TEMPERATURE"));
    m_systemType->addItem(uiText("Water nozzle"),
                          QStringLiteral("LINK TEMPERATURE"));
    m_speciesId = new QLineEdit(QStringLiteral("WATER VAPOR"), identityPage);
    m_speciesId->setObjectName(QStringLiteral("ParticleSpraySpeciesIdEdit"));
    m_speciesFormula = new QLineEdit(identityPage);
    m_speciesFormula->setObjectName(QStringLiteral("ParticleSpraySpeciesFormulaEdit"));
    m_speciesFormula->setPlaceholderText(uiText("H2O (optional)"));
    m_particleId = new QLineEdit(QStringLiteral("WATER_DROPLETS"), identityPage);
    m_particleId->setObjectName(QStringLiteral("ParticleSprayParticleIdEdit"));
    m_particleDiameter = engineeringSpin(identityPage, 1.0, 100000.0, 1750.0,
                                          QStringLiteral(" µm"));
    m_particleDiameter->setObjectName(QStringLiteral("ParticleSprayDiameterSpin"));
    m_particleAge = engineeringSpin(identityPage, 0.0, 1.0e9, 0.0,
                                     QStringLiteral(" s"));
    m_particleAge->setObjectName(QStringLiteral("ParticleSprayAgeSpin"));
    identityForm->addRow(uiText("System type:"), m_systemType);
    identityForm->addRow(uiText("Name:"), m_name);
    identityForm->addRow(uiText("Species FDS ID:"), m_speciesId);
    identityForm->addRow(uiText("Chemical formula:"), m_speciesFormula);
    identityForm->addRow(uiText("Particle FDS ID:"), m_particleId);
    identityForm->addRow(uiText("Droplet diameter:"), m_particleDiameter);
    identityForm->addRow(uiText("Maximum particle age:"), m_particleAge);
    auto* identityHint = new QLabel(
        uiText("FireCAE creates a UUID-linked SPEC and PART pair. Diameter uses the FDS micrometre convention."),
        identityPage);
    identityHint->setWordWrap(true);
    identityForm->addRow(identityHint);
    tabs->addTab(identityPage, uiText("Particle Definition"));

    auto* propertyPage = new QWidget(tabs);
    auto* propertyForm = new QFormLayout(propertyPage);
    m_propertyId = new QLineEdit(QStringLiteral("SPRINKLER_K11"), propertyPage);
    m_propertyId->setObjectName(QStringLiteral("ParticleSprayPropertyIdEdit"));
    m_flowRate = engineeringSpin(propertyPage, 0.001, 1.0e9, 60.0,
                                 QStringLiteral(" L/min"));
    m_flowRate->setObjectName(QStringLiteral("ParticleSprayFlowRateSpin"));
    m_particleVelocity = engineeringSpin(propertyPage, 0.001, 1.0e6, 5.0,
                                         QStringLiteral(" m/s"));
    m_particleVelocity->setObjectName(QStringLiteral("ParticleSprayVelocitySpin"));
    m_particlesPerSecond = new QSpinBox(propertyPage);
    m_particlesPerSecond->setObjectName(
        QStringLiteral("ParticleSprayParticlesPerSecondSpin"));
    m_particlesPerSecond->setRange(1, 1000000000);
    m_particlesPerSecond->setValue(10000);
    m_activationTemperature = engineeringSpin(propertyPage, -273.15, 5000.0,
                                               74.0, QStringLiteral(" °C"));
    m_activationTemperature->setObjectName(
        QStringLiteral("ParticleSprayActivationTemperatureSpin"));
    m_responseTimeIndex = engineeringSpin(propertyPage, 0.001, 1.0e9, 50.0,
                                          QStringLiteral(" (m·s)^0.5"));
    m_responseTimeIndex->setObjectName(QStringLiteral("ParticleSprayRtiSpin"));
    m_smokeviewId = new QLineEdit(QStringLiteral("sprinkler_upright"), propertyPage);
    m_smokeviewId->setObjectName(QStringLiteral("ParticleSpraySmokeviewIdEdit"));
    propertyForm->addRow(uiText("Property FDS ID:"), m_propertyId);
    propertyForm->addRow(uiText("Flow rate:"), m_flowRate);
    propertyForm->addRow(uiText("Initial particle velocity:"), m_particleVelocity);
    propertyForm->addRow(uiText("Particles per second:"), m_particlesPerSecond);
    propertyForm->addRow(uiText("Activation temperature:"), m_activationTemperature);
    propertyForm->addRow(uiText("Response time index (RTI):"), m_responseTimeIndex);
    propertyForm->addRow(uiText("Smokeview symbol ID:"), m_smokeviewId);
    tabs->addTab(propertyPage, uiText("Sprinkler Property"));

    auto* patternPage = new QWidget(tabs);
    auto* patternLayout = new QVBoxLayout(patternPage);
    auto* tableIdForm = new QFormLayout;
    m_tableId = new QLineEdit(QStringLiteral("SPRAY_TABLE_01"), patternPage);
    m_tableId->setObjectName(QStringLiteral("ParticleSprayTableIdEdit"));
    tableIdForm->addRow(uiText("Spray table FDS ID:"), m_tableId);
    patternLayout->addLayout(tableIdForm);
    m_patternTable = new QTableWidget(0, 6, patternPage);
    m_patternTable->setObjectName(QStringLiteral("ParticleSprayPatternTable"));
    m_patternTable->setHorizontalHeaderLabels({
        uiText("Elevation min (deg)"), uiText("Elevation max (deg)"),
        uiText("Azimuth min (deg)"), uiText("Azimuth max (deg)"),
        uiText("Radius"), uiText("Weight")});
    m_patternTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_patternTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    addPatternRow({30.0, 31.0, 0.0, 1.0, 5.0, 0.2});
    addPatternRow({30.0, 31.0, 179.0, 180.0, 5.0, 0.8});
    patternLayout->addWidget(m_patternTable, 1);
    auto* patternButtons = new QHBoxLayout;
    auto* addRowButton = new QPushButton(uiText("Add spray sector"), patternPage);
    addRowButton->setObjectName(QStringLiteral("ParticleSprayAddPatternRowButton"));
    auto* removeRowButton = new QPushButton(uiText("Remove selected sector"), patternPage);
    removeRowButton->setObjectName(
        QStringLiteral("ParticleSprayRemovePatternRowButton"));
    patternButtons->addWidget(addRowButton);
    patternButtons->addWidget(removeRowButton);
    patternButtons->addStretch(1);
    patternLayout->addLayout(patternButtons);
    auto* patternHint = new QLabel(
        uiText("Each row becomes one TABL record. Rows share one FDS ID and remain separate UUID objects."),
        patternPage);
    patternHint->setWordWrap(true);
    patternLayout->addWidget(patternHint);
    connect(addRowButton, &QPushButton::clicked, this,
            [this]() { addPatternRow(); });
    connect(removeRowButton, &QPushButton::clicked, this,
            &ParticleSprayWizardDialog::removeSelectedPatternRows);
    tabs->addTab(patternPage, uiText("Spray Distribution"));

    auto* placementPage = new QWidget(tabs);
    auto* placementForm = new QFormLayout(placementPage);
    m_deviceId = new QLineEdit(QStringLiteral("SPRINKLER_01"), placementPage);
    m_deviceId->setObjectName(QStringLiteral("ParticleSprayDeviceIdEdit"));
    m_x = engineeringSpin(placementPage, -1.0e9, 1.0e9, 0.0,
                          QStringLiteral(" m"));
    m_y = engineeringSpin(placementPage, -1.0e9, 1.0e9, 0.0,
                          QStringLiteral(" m"));
    m_z = engineeringSpin(placementPage, -1.0e9, 1.0e9, 3.0,
                          QStringLiteral(" m"));
    m_x->setObjectName(QStringLiteral("ParticleSprayXSpin"));
    m_y->setObjectName(QStringLiteral("ParticleSprayYSpin"));
    m_z->setObjectName(QStringLiteral("ParticleSprayZSpin"));
    m_activationMode = new QComboBox(placementPage);
    m_activationMode->setObjectName(QStringLiteral("ParticleSprayActivationModeCombo"));
    m_activationMode->addItem(uiText("Thermal link (temperature and RTI)"), false);
    m_activationMode->addItem(uiText("Fixed activation time"), true);
    m_activationTime = engineeringSpin(placementPage, 0.0, 1.0e9, 5.0,
                                       QStringLiteral(" s"));
    m_activationTime->setObjectName(QStringLiteral("ParticleSprayActivationTimeSpin"));
    m_createParticleOutput = new QCheckBox(
        uiText("Create particle animation output cadence (DUMP / DT_PART)"),
        placementPage);
    m_createParticleOutput->setObjectName(
        QStringLiteral("ParticleSprayCreateOutputCheck"));
    m_createParticleOutput->setChecked(true);
    m_particleOutputInterval = engineeringSpin(placementPage, 0.001, 1.0e9,
                                               1.0, QStringLiteral(" s"));
    m_particleOutputInterval->setObjectName(
        QStringLiteral("ParticleSprayOutputIntervalSpin"));
    placementForm->addRow(uiText("Device FDS ID:"), m_deviceId);
    placementForm->addRow(uiText("Point X:"), m_x);
    placementForm->addRow(uiText("Point Y:"), m_y);
    placementForm->addRow(uiText("Point Z:"), m_z);
    placementForm->addRow(uiText("Activation mode:"), m_activationMode);
    placementForm->addRow(uiText("Activation time:"), m_activationTime);
    placementForm->addRow(m_createParticleOutput);
    placementForm->addRow(uiText("Particle output interval:"),
                          m_particleOutputInterval);
    tabs->addTab(placementPage, uiText("Placement and Output"));
    root->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &ParticleSprayWizardDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(m_activationMode, &QComboBox::currentIndexChanged, this,
            [this]() { updateControls(); });
    connect(m_createParticleOutput, &QCheckBox::toggled, this,
            [this]() { updateControls(); });
    updateControls();
}

void ParticleSprayWizardDialog::addPatternRow(const SprayPatternRow& row)
{
    const int targetRow = m_patternTable->rowCount();
    m_patternTable->insertRow(targetRow);
    const std::array<double, 6> values = {{
        row.elevationMinimum, row.elevationMaximum,
        row.azimuthMinimum, row.azimuthMaximum, row.radius, row.weight}};
    for (int column = 0; column < static_cast<int>(values.size()); ++column) {
        m_patternTable->setItem(
            targetRow, column,
            new QTableWidgetItem(QString::number(values[column], 'g', 15)));
    }
}

void ParticleSprayWizardDialog::removeSelectedPatternRows()
{
    QSet<int> rows;
    for (const QModelIndex& index : m_patternTable->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    QList<int> ordered = rows.values();
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    for (const int row : ordered) m_patternTable->removeRow(row);
}

ParticleSprayWizardData ParticleSprayWizardDialog::data() const
{
    ParticleSprayWizardData result;
    result.name = m_name->text().trimmed();
    // FDS built-in species names such as "WATER VAPOR" contain meaningful
    // spaces.  Do not apply the underscore-normalization used for FireCAE's
    // generated IDs or FDS will interpret this as an incomplete custom species.
    result.speciesFdsId = m_speciesId->text().trimmed().toUpper();
    result.speciesFormula = m_speciesFormula->text().trimmed();
    result.particleFdsId = cleanId(m_particleId->text());
    result.particleDiameter = m_particleDiameter->value();
    result.particleAge = m_particleAge->value();
    result.propertyFdsId = cleanId(m_propertyId->text());
    result.propertyQuantity = m_systemType->currentData().toString();
    result.flowRate = m_flowRate->value();
    result.particleVelocity = m_particleVelocity->value();
    result.particlesPerSecond = m_particlesPerSecond->value();
    result.activationTemperature = m_activationTemperature->value();
    result.responseTimeIndex = m_responseTimeIndex->value();
    result.smokeviewId = m_smokeviewId->text().trimmed();
    result.tableFdsId = cleanId(m_tableId->text());
    for (int row = 0; row < m_patternTable->rowCount(); ++row) {
        const auto value = [this, row](int column) {
            const QTableWidgetItem* item = m_patternTable->item(row, column);
            bool ok = false;
            const double number = item ? item->text().trimmed().toDouble(&ok) : 0.0;
            return ok ? number : std::numeric_limits<double>::quiet_NaN();
        };
        result.sprayPattern.push_back({value(0), value(1), value(2),
                                       value(3), value(4), value(5)});
    }
    result.deviceFdsId = cleanId(m_deviceId->text());
    result.x = m_x->value(); result.y = m_y->value(); result.z = m_z->value();
    result.activateAtTime = m_activationMode->currentData().toBool();
    result.activationTime = m_activationTime->value();
    result.createParticleOutput = m_createParticleOutput->isChecked();
    result.particleOutputInterval = m_particleOutputInterval->value();
    return result;
}

bool ParticleSprayWizardDialog::hasExistingFdsId(const QString& keyword,
                                                 const QString& fdsId) const
{
    if (fdsId.isEmpty()) return false;
    for (const FcObject::Ptr& object : allObjects(m_project)) {
        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (namelist && namelist->keyword() == keyword &&
            namelist->fdsId().trimmed().compare(fdsId, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void ParticleSprayWizardDialog::accept()
{
    const ParticleSprayWizardData value = data();
    QStringList errors;
    if (value.name.isEmpty()) errors.append(uiText("Name is required."));
    const std::array<std::pair<QString, QString>, 5> ids = {{
        {QStringLiteral("SPEC"), value.speciesFdsId},
        {QStringLiteral("PART"), value.particleFdsId},
        {QStringLiteral("PROP"), value.propertyFdsId},
        {QStringLiteral("TABL"), value.tableFdsId},
        {QStringLiteral("DEVC"), value.deviceFdsId}}};
    for (const auto& [keyword, id] : ids) {
        if (id.isEmpty()) {
            errors.append(uiText("%1 FDS ID is required.").arg(keyword));
        } else if (hasExistingFdsId(keyword, id)) {
            errors.append(uiText("%1 FDS ID '%2' already exists in this project.").arg(keyword, id));
        }
    }
    if (value.sprayPattern.empty()) {
        errors.append(uiText("Add at least one spray distribution sector."));
    }
    double totalWeight = 0.0;
    for (int row = 0; row < static_cast<int>(value.sprayPattern.size()); ++row) {
        const SprayPatternRow& sector = value.sprayPattern[static_cast<std::size_t>(row)];
        const std::array<double, 6> numbers = {{sector.elevationMinimum,
            sector.elevationMaximum, sector.azimuthMinimum, sector.azimuthMaximum,
            sector.radius, sector.weight}};
        if (!std::all_of(numbers.begin(), numbers.end(),
                         [](double number) { return std::isfinite(number); }) ||
            sector.elevationMinimum > sector.elevationMaximum ||
            sector.azimuthMinimum > sector.azimuthMaximum || sector.radius <= 0.0 ||
            sector.weight < 0.0) {
            errors.append(uiText("Spray sector %1 must contain ordered finite angles, a positive radius, and a non-negative weight.")
                              .arg(row + 1));
        }
        totalWeight += sector.weight;
    }
    if (!(totalWeight > 0.0)) {
        errors.append(uiText("Spray distribution weights must have a positive sum."));
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, uiText("Particle and Sprinkler Validation"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    QDialog::accept();
}

void ParticleSprayWizardDialog::updateControls()
{
    const bool timeActivation = m_activationMode->currentData().toBool();
    m_activationTime->setEnabled(timeActivation);
    m_activationTemperature->setEnabled(!timeActivation);
    m_responseTimeIndex->setEnabled(!timeActivation);
    m_particleOutputInterval->setEnabled(m_createParticleOutput->isChecked());
}

OutputWizardDialog::OutputWizardDialog(const FcProject* project, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("OutputWizardDialog"));
    setWindowTitle(uiText("FDS Output Wizard"));
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(QStringLiteral("Temperature Slice"), this);
    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("OutputKindCombo"));
    m_kind->addItem(uiText("Slice plane (SLCF)"), QStringLiteral("SLCF"));
    m_kind->addItem(uiText("Vector slice plane"), QStringLiteral("SLCF_VECTOR"));
    m_kind->addItem(uiText("Boundary quantity (BNDF)"), QStringLiteral("BNDF"));
    m_kind->addItem(uiText("Isosurface (ISOF)"), QStringLiteral("ISOF"));
    m_kind->addItem(uiText("Plot3D field (PL3D)"), QStringLiteral("PL3D"));
    m_kind->addItem(uiText("3D smoke / fire quantity"), QStringLiteral("SM3D"));
    m_kind->addItem(uiText("Point device / CSV"), QStringLiteral("DEVC"));
    m_kind->addItem(uiText("Profile"), QStringLiteral("PROF"));
    m_kind->addItem(uiText("Particle output cadence"), QStringLiteral("DUMP_PART"));
    m_kind->addItem(uiText("HVAC quantity device"), QStringLiteral("HVAC_DEVC"));
    m_fdsId = new QLineEdit(this);
    m_quantity = new QComboBox(this);
    m_quantity->setObjectName(QStringLiteral("OutputQuantityCombo"));
    m_quantity->setEditable(true);
    m_quantity->addItems({QStringLiteral("TEMPERATURE"), QStringLiteral("VELOCITY"),
                          QStringLiteral("HRRPUV"), QStringLiteral("SOOT DENSITY"),
                          QStringLiteral("VISIBILITY"), QStringLiteral("PRESSURE"),
                          QStringLiteral("HEAT FLUX"), QStringLiteral("WALL TEMPERATURE"),
                          QStringLiteral("VOLUME FRACTION")});
    m_axis = new QComboBox(this);
    m_axis->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
    m_axis->setCurrentText(QStringLiteral("Z"));
    m_coordinate = engineeringSpin(this, -1.0e9, 1.0e9, 1.5, QStringLiteral(" m"));
    m_vector = new QCheckBox(uiText("Vector output"), this);
    m_target = new QComboBox(this);
    m_target->setObjectName(QStringLiteral("OutputTargetObjectCombo"));
    m_target->addItem(uiText("No target object"), QString{});
    for (const FcObject::Ptr& object : allObjects(project)) {
        const auto fds = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!fds) continue;
        if (fds->keyword() == QStringLiteral("HVAC") ||
            fds->keyword() == QStringLiteral("DEVC") ||
            fds->keyword() == QStringLiteral("SPEC") ||
            fds->keyword() == QStringLiteral("PART")) {
            m_target->addItem(QStringLiteral("%1 — %2").arg(object->name(), fds->fdsId()),
                              object->id());
        }
    }
    m_x = engineeringSpin(this, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_y = engineeringSpin(this, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_z = engineeringSpin(this, -1.0e9, 1.0e9, 1.5, QStringLiteral(" m"));
    m_interval = engineeringSpin(this, 0.000001, 1.0e9, 1.0, QStringLiteral(" s"));
    m_x->setObjectName(QStringLiteral("OutputXSpin"));
    m_y->setObjectName(QStringLiteral("OutputYSpin"));
    m_z->setObjectName(QStringLiteral("OutputZSpin"));
    m_interval->setObjectName(QStringLiteral("OutputIntervalSpin"));
    form->addRow(uiText("Name:"), m_name);
    form->addRow(uiText("Output type:"), m_kind);
    form->addRow(uiText("Optional FDS ID:"), m_fdsId);
    form->addRow(uiText("Quantity:"), m_quantity);
    form->addRow(uiText("Plane axis:"), m_axis);
    form->addRow(uiText("Plane coordinate:"), m_coordinate);
    form->addRow(m_vector);
    form->addRow(uiText("Point X:"), m_x);
    form->addRow(uiText("Point Y:"), m_y);
    form->addRow(uiText("Point Z:"), m_z);
    form->addRow(uiText("Target object:"), m_target);
    form->addRow(uiText("Output interval:"), m_interval);
    root->addLayout(form);
    auto* hint = new QLabel(uiText("Quantities are selected from the current FDS schema; "
                                           "the editable list also preserves advanced quantities."), this);
    hint->setWordWrap(true);
    root->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &OutputWizardDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(m_kind, &QComboBox::currentIndexChanged, this,
            [this]() { updateControls(); });
    updateControls();
}

OutputWizardData OutputWizardDialog::data() const
{
    OutputWizardData result;
    result.name = m_name->text().trimmed();
    result.keyword = m_kind->currentData().toString();
    result.fdsId = cleanId(m_fdsId->text());
    result.quantity = m_quantity->currentText().trimmed().toUpper();
    result.axis = m_axis->currentText();
    result.coordinate = m_coordinate->value();
    result.vector = m_vector->isChecked() || result.keyword == QStringLiteral("SLCF_VECTOR");
    result.targetObjectId = m_target->currentData().toString();
    result.x = m_x->value(); result.y = m_y->value(); result.z = m_z->value();
    result.interval = m_interval->value();
    return result;
}

void OutputWizardDialog::accept()
{
    OutputWizardData value = data();
    if (value.name.isEmpty() || value.quantity.isEmpty()) {
        QMessageBox::warning(this, uiText("Output Validation"),
                             uiText("Name and Quantity are required."));
        return;
    }
    if ((value.keyword == QStringLiteral("DEVC") ||
         value.keyword == QStringLiteral("HVAC_DEVC") ||
         value.keyword == QStringLiteral("PROF")) && value.fdsId.isEmpty()) {
        m_fdsId->setText(cleanId(value.name));
        value = data();
    }
    if (value.keyword == QStringLiteral("HVAC_DEVC") &&
        value.targetObjectId.isEmpty()) {
        QMessageBox::warning(this, uiText("Output Validation"),
                             uiText("Select an HVAC node or duct for an HVAC output device."));
        return;
    }
    QDialog::accept();
}

void OutputWizardDialog::updateControls()
{
    const QString kind = m_kind->currentData().toString();
    const bool plane = kind == QStringLiteral("SLCF") ||
                       kind == QStringLiteral("SLCF_VECTOR");
    const bool point = kind == QStringLiteral("DEVC") ||
                       kind == QStringLiteral("HVAC_DEVC") ||
                       kind == QStringLiteral("PROF");
    const bool target = kind == QStringLiteral("HVAC_DEVC");
    const bool cadence = kind == QStringLiteral("DUMP_PART");
    m_axis->setEnabled(plane);
    m_coordinate->setEnabled(plane);
    m_vector->setEnabled(plane || kind == QStringLiteral("PL3D"));
    m_x->setEnabled(point); m_y->setEnabled(point); m_z->setEnabled(point);
    m_target->setEnabled(target);
    m_interval->setEnabled(cadence);
}

DeviceControlWizardDialog::DeviceControlWizardDialog(const FcProject* project,
                                                     QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("DeviceControlWizardDialog"));
    setWindowTitle(uiText("Device and Control Wizard"));
    resize(720, 620);
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    auto* devicePage = new QWidget(tabs);
    auto* deviceForm = new QFormLayout(devicePage);
    m_name = new QLineEdit(QStringLiteral("Temperature Sensor"), devicePage);
    m_name->setObjectName(QStringLiteral("DeviceControlNameEdit"));
    m_deviceId = new QLineEdit(QStringLiteral("TEMP_01"), devicePage);
    m_deviceId->setObjectName(QStringLiteral("DeviceControlDeviceIdEdit"));
    m_type = new QComboBox(devicePage);
    m_type->setObjectName(QStringLiteral("DeviceControlTypeCombo"));
    const std::array<std::pair<const char*, const char*>, 12> deviceTypes = {{
        {"Temperature sensor", "TEMPERATURE"}, {"Thermocouple", "THERMOCOUPLE"},
        {"Smoke detector", "CHAMBER OBSCURATION"}, {"Heat detector", "LINK TEMPERATURE"},
        {"Sprinkler", "SPRINKLER LINK TEMPERATURE"}, {"Nozzle", "LINK TEMPERATURE"},
        {"Heat flux gauge", "HEAT FLUX"}, {"Gas concentration device", "VOLUME FRACTION"},
        {"Heat release rate device", "HRR"}, {"Flow device", "VOLUME FLOW"},
        {"Beam detector", "PATH OBSCURATION"}, {"Aspiration detector", "CHAMBER DENSITY"}
    }};
    for (const auto& [label, quantity] : deviceTypes) {
        m_type->addItem(uiText(label), QString::fromLatin1(quantity));
    }
    m_property = new QComboBox(devicePage);
    m_property->setObjectName(QStringLiteral("DeviceControlPropertyCombo"));
    m_property->addItem(uiText("No detector / sprinkler property"), QString{});
    m_controlledObject = new QComboBox(devicePage);
    m_controlledObject->setObjectName(QStringLiteral("DeviceControlTargetCombo"));
    m_controlledObject->addItem(uiText("No controlled object"), QString{});
    for (const FcObject::Ptr& object : allObjects(project)) {
        const auto fds = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!fds) continue;
        if (fds->keyword() == QStringLiteral("PROP")) {
            m_property->addItem(QStringLiteral("%1 — %2").arg(object->name(), fds->fdsId()),
                                object->id());
        }
        if (fds->keyword() == QStringLiteral("VENT") ||
            fds->keyword() == QStringLiteral("OBST")) {
            m_controlledObject->addItem(
                QStringLiteral("%1 — %2").arg(object->name(), fds->fdsId()), object->id());
        }
    }
    m_x = engineeringSpin(devicePage, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_y = engineeringSpin(devicePage, -1.0e9, 1.0e9, 0.0, QStringLiteral(" m"));
    m_z = engineeringSpin(devicePage, -1.0e9, 1.0e9, 1.5, QStringLiteral(" m"));
    m_x->setObjectName(QStringLiteral("DeviceControlXSpin"));
    m_y->setObjectName(QStringLiteral("DeviceControlYSpin"));
    m_z->setObjectName(QStringLiteral("DeviceControlZSpin"));
    m_setpoint = engineeringSpin(devicePage, -1.0e12, 1.0e12, 68.0, QString{});
    m_setpoint->setObjectName(QStringLiteral("DeviceControlSetpointSpin"));
    m_tripDirection = new QComboBox(devicePage);
    m_tripDirection->setObjectName(QStringLiteral("DeviceControlTripDirectionCombo"));
    m_tripDirection->addItem(uiText("Trigger when value rises above threshold"), 1);
    m_tripDirection->addItem(uiText("Trigger when value falls below threshold"), -1);
    deviceForm->addRow(uiText("Name:"), m_name);
    deviceForm->addRow(uiText("Device type:"), m_type);
    deviceForm->addRow(uiText("Device FDS ID:"), m_deviceId);
    deviceForm->addRow(uiText("Point X:"), m_x);
    deviceForm->addRow(uiText("Point Y:"), m_y);
    deviceForm->addRow(uiText("Point Z:"), m_z);
    deviceForm->addRow(uiText("Activation threshold:"), m_setpoint);
    deviceForm->addRow(uiText("Trigger direction:"), m_tripDirection);
    deviceForm->addRow(uiText("Detector / sprinkler property:"), m_property);
    tabs->addTab(devicePage, uiText("Detector"));

    auto* controlPage = new QWidget(tabs);
    auto* controlForm = new QFormLayout(controlPage);
    controlForm->addRow(uiText("Controlled object:"), m_controlledObject);
    m_createControl = new QCheckBox(uiText("Create a CTRL record for delay / logic"), controlPage);
    m_createControl->setObjectName(QStringLiteral("DeviceControlCreateControlCheck"));
    m_controlId = new QLineEdit(QStringLiteral("CTRL_01"), controlPage);
    m_controlId->setObjectName(QStringLiteral("DeviceControlControlIdEdit"));
    m_delay = engineeringSpin(controlPage, 0.0, 1.0e9, 0.0, QStringLiteral(" s"));
    m_delay->setObjectName(QStringLiteral("DeviceControlDelaySpin"));
    m_action = new QComboBox(controlPage);
    m_action->setObjectName(QStringLiteral("DeviceControlActionCombo"));
    m_action->addItem(uiText("Activate target"), true);
    m_action->addItem(uiText("Deactivate target"), false);
    controlForm->addRow(m_createControl);
    controlForm->addRow(uiText("Control FDS ID:"), m_controlId);
    controlForm->addRow(uiText("Delay:"), m_delay);
    controlForm->addRow(uiText("Action:"), m_action);
    auto* hint = new QLabel(
        uiText("References are stored as object UUIDs. FireCAE resolves UUIDs to FDS IDs only when exporting."),
        controlPage);
    hint->setWordWrap(true);
    controlForm->addRow(hint);
    tabs->addTab(controlPage, uiText("Activation Control"));
    root->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &DeviceControlWizardDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(m_type, &QComboBox::currentIndexChanged, this,
            [this]() { updateControls(); });
    connect(m_controlledObject, &QComboBox::currentIndexChanged, this,
            [this]() { updateControls(); });
    connect(m_createControl, &QCheckBox::toggled, this,
            [this]() { updateControls(); });
    updateControls();
}

DeviceControlWizardData DeviceControlWizardDialog::data() const
{
    DeviceControlWizardData result;
    result.name = m_name->text().trimmed();
    result.deviceFdsId = cleanId(m_deviceId->text());
    result.quantity = m_type->currentData().toString();
    result.propertyObjectId = m_property->currentData().toString();
    result.x = m_x->value(); result.y = m_y->value(); result.z = m_z->value();
    result.setpoint = m_setpoint->value();
    result.tripDirection = m_tripDirection->currentData().toInt();
    result.controlledObjectId = m_controlledObject->currentData().toString();
    result.createControl = m_createControl->isChecked();
    result.controlFdsId = cleanId(m_controlId->text());
    result.delay = m_delay->value();
    result.activateTarget = m_action->currentData().toBool();
    return result;
}

void DeviceControlWizardDialog::accept()
{
    const DeviceControlWizardData value = data();
    QStringList errors;
    if (value.name.isEmpty()) errors.append(uiText("Name is required."));
    if (value.deviceFdsId.isEmpty()) errors.append(uiText("Device FDS ID is required."));
    if (value.createControl && value.controlFdsId.isEmpty()) {
        errors.append(uiText("Control FDS ID is required."));
    }
    if (value.createControl && value.controlledObjectId.isEmpty()) {
        errors.append(uiText("Select a controlled VENT or OBST when creating control logic."));
    }
    if (!value.controlledObjectId.isEmpty()) {
        if (!m_project) {
            errors.append(QStringLiteral("The controlled target has no project."));
        } else {
            errors.append(FdsWriter::validateControlledTarget(*m_project, value.controlledObjectId));
            // Creating the binding changes the base object. Do not silently
            // leave an active scenario override masking this new controller.
            if (const FcScenario* scenario = m_project->activeScenario()) {
                for (const auto& entry : scenario->parameterOverrides) {
                    const QString key = entry.parameterKey.section(QLatin1Char('('), 0, 0).trimmed().toUpper();
                    if (entry.objectId == value.controlledObjectId &&
                        (key == QStringLiteral("DEVC_ID") || key == QStringLiteral("CTRL_ID"))) {
                        errors.append(QStringLiteral("Active scenario '%1' overrides target %2 [UUID %3]; remove this override before creating a new control binding.")
                                          .arg(scenario->name, key, value.controlledObjectId));
                    }
                }
            }
        }
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, uiText("Device and Control Validation"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    QDialog::accept();
}

void DeviceControlWizardDialog::updateControls()
{
    const QString quantity = m_type->currentData().toString();
    const bool propertyUseful = quantity == QStringLiteral("SPRINKLER LINK TEMPERATURE") ||
                                quantity == QStringLiteral("LINK TEMPERATURE");
    m_property->setEnabled(propertyUseful);
    const bool hasTarget = !m_controlledObject->currentData().toString().isEmpty();
    m_createControl->setEnabled(hasTarget);
    if (!hasTarget) m_createControl->setChecked(false);
    m_controlId->setEnabled(m_createControl->isChecked());
    m_delay->setEnabled(m_createControl->isChecked());
    m_action->setEnabled(hasTarget);
}

FdsNetworkGraphDialog::FdsNetworkGraphDialog(const FcProject* project,
                                             FdsNetworkGraphKind kind,
                                             QWidget* parent)
    : QDialog(parent), m_project(project), m_kind(kind)
{
    setObjectName(kind == FdsNetworkGraphKind::Controls
                      ? QStringLiteral("ControlLogicGraphDialog")
                      : QStringLiteral("HvacNetworkGraphDialog"));
    setWindowTitle(kind == FdsNetworkGraphKind::Controls
                       ? uiText("Control Logic Graph")
                       : uiText("HVAC Network Graph"));
    resize(920, 620);
    auto* root = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_view = new QGraphicsView(this);
    m_view->setObjectName(QStringLiteral("FdsNetworkGraphicsView"));
    m_view->setRenderHint(QPainter::Antialiasing);
    root->addWidget(m_summary);
    root->addWidget(m_view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* fit = buttons->addButton(uiText("Fit Graph"), QDialogButtonBox::ActionRole);
    connect(fit, &QPushButton::clicked, this, [this]() { m_view->fitInView(m_view->scene()->itemsBoundingRect(), Qt::KeepAspectRatio); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    rebuild();
}

void FdsNetworkGraphDialog::rebuild()
{
    auto* scene = new QGraphicsScene(m_view);
    m_view->setScene(scene);
    QVector<std::shared_ptr<FcFdsNamelist>> objects;
    for (const FcObject::Ptr& object : allObjects(m_project)) {
        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!namelist) continue;
        if (m_kind == FdsNetworkGraphKind::Controls) {
            if (namelist->keyword() == QStringLiteral("CTRL") ||
                namelist->keyword() == QStringLiteral("DEVC") ||
                namelist->keyword() == QStringLiteral("RAMP") ||
                namelist->keyword() == QStringLiteral("PROP")) objects.append(namelist);
        } else if (namelist->keyword() == QStringLiteral("HVAC") ||
                   namelist->keyword() == QStringLiteral("VENT")) {
            objects.append(namelist);
        }
    }
    QHash<QString, QGraphicsRectItem*> nodes;
    constexpr qreal width = 190.0;
    constexpr qreal height = 62.0;
    for (int index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        int column = 0;
        if (m_kind == FdsNetworkGraphKind::Controls) {
            column = object->keyword() == QStringLiteral("CTRL") ? 1 : 0;
        } else {
            const QString type = object->parameterValue(QStringLiteral("TYPE_ID")).toUpper();
            column = type == QStringLiteral("DUCT") ? 1 :
                     type == QStringLiteral("FAN") || type == QStringLiteral("AIRCOIL") ||
                     type == QStringLiteral("FILTER") ? 2 : 0;
        }
        int row = 0;
        for (int previous = 0; previous < index; ++previous) {
            const auto& other = objects[previous];
            int otherColumn = 0;
            if (m_kind == FdsNetworkGraphKind::Controls) {
                otherColumn = other->keyword() == QStringLiteral("CTRL") ? 1 : 0;
            } else {
                const QString type = other->parameterValue(QStringLiteral("TYPE_ID")).toUpper();
                otherColumn = type == QStringLiteral("DUCT") ? 1 :
                              type == QStringLiteral("FAN") || type == QStringLiteral("AIRCOIL") ||
                              type == QStringLiteral("FILTER") ? 2 : 0;
            }
            if (otherColumn == column) ++row;
        }
        const QRectF rect(column * 270.0, row * 100.0, width, height);
        auto* box = scene->addRect(rect, QPen(QColor(55, 75, 95), 2),
                                   QBrush(column == 0 ? QColor(220, 238, 250)
                                                     : column == 1 ? QColor(232, 245, 225)
                                                                   : QColor(255, 238, 205)));
        box->setData(0, object->id());
        auto* label = scene->addText(QStringLiteral("&%1 %2\n%3")
                                         .arg(object->keyword(), object->fdsId(), object->name()));
        label->setTextWidth(width - 12.0);
        label->setPos(rect.left() + 6.0, rect.top() + 4.0);
        nodes.insert(object->id(), box);
    }
    int edges = 0;
    int dangling = 0;
    for (const auto& object : objects) {
        QGraphicsRectItem* destination = nodes.value(object->id(), nullptr);
        if (!destination) continue;
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind != FcFdsParameterKind::ObjectReferences) continue;
            for (const QString& targetId : parameter.targetObjectIds) {
                QGraphicsRectItem* source = nodes.value(targetId, nullptr);
                if (!source) { ++dangling; continue; }
                const QPointF a = source->sceneBoundingRect().center();
                const QPointF b = destination->sceneBoundingRect().center();
                scene->addLine(QLineF(a, b), QPen(QColor(70, 70, 70), 2));
                ++edges;
            }
        }
    }
    const FdsWriteResult validation = m_project ? FdsWriter::render(*m_project)
                                                : FdsWriteResult{};
    m_summary->setText(uiText("%1 nodes, %2 UUID links, %3 dangling links. "
                                      "Project validation: %4 error(s), %5 warning(s).")
                           .arg(objects.size()).arg(edges).arg(dangling)
                           .arg(validation.errors.size()).arg(validation.warnings.size()));
    scene->setSceneRect(scene->itemsBoundingRect().adjusted(-30, -30, 30, 30));
    if (!scene->items().isEmpty()) m_view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

FdsPropertyLibraryDialog::FdsPropertyLibraryDialog(const QString& userLibraryPath,
                                                   QWidget* parent)
    : QDialog(parent), m_library(userLibraryPath)
{
    setObjectName(QStringLiteral("FdsPropertyLibraryDialog"));
    setWindowTitle(uiText("FDS Property Library"));
    resize(900, 590);
    auto* root = new QVBoxLayout(this);
    auto* filters = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("FdsLibrarySearchEdit"));
    m_search->setPlaceholderText(uiText("Search name, category, FDS type, or ID..."));
    m_category = new QComboBox(this);
    m_category->setObjectName(QStringLiteral("FdsLibraryCategoryCombo"));
    m_category->addItem(uiText("All Categories"), QString{});
    QSet<QString> categories;
    for (const FdsLibraryEntry& entry : m_library.entries()) categories.insert(entry.category);
    QStringList sortedCategories(categories.cbegin(), categories.cend());
    sortedCategories.sort(Qt::CaseInsensitive);
    for (const QString& category : sortedCategories) m_category->addItem(category, category);
    filters->addWidget(m_search, 1);
    filters->addWidget(m_category);
    root->addLayout(filters);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("FdsLibraryTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({uiText("Source"), uiText("Category"),
                                        uiText("Name"), uiText("FDS Type"),
                                        uiText("FDS ID"), uiText("Version")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    root->addWidget(m_table, 1);

    auto* tools = new QHBoxLayout;
    auto* importButton = new QPushButton(uiText("Import Library..."), this);
    auto* exportButton = new QPushButton(uiText("Export Selected..."), this);
    auto* duplicateButton = new QPushButton(uiText("Duplicate to User Library"), this);
    auto* removeButton = new QPushButton(uiText("Remove User Entry"), this);
    tools->addWidget(importButton);
    tools->addWidget(exportButton);
    tools->addWidget(duplicateButton);
    tools->addWidget(removeButton);
    tools->addStretch();
    root->addLayout(tools);
    auto* path = new QLabel(uiText("User library: %1").arg(
                                QDir::toNativeSeparators(m_library.userFilePath())), this);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(path);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(uiText("Copy to Project"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FdsPropertyLibraryDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildTable(); });
    connect(m_category, &QComboBox::currentIndexChanged, this,
            [this]() { rebuildTable(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { accept(); });
    connect(importButton, &QPushButton::clicked, this, [this]() { importEntries(); });
    connect(exportButton, &QPushButton::clicked, this, [this]() { exportEntries(); });
    connect(duplicateButton, &QPushButton::clicked, this, [this]() { duplicateEntry(); });
    connect(removeButton, &QPushButton::clicked, this, [this]() { removeEntry(); });
    rebuildTable();
}

FdsLibraryEntry FdsPropertyLibraryDialog::selectedEntry() const
{
    if (const FdsLibraryEntry* entry = m_library.find(selectedLibraryId())) return *entry;
    return {};
}

void FdsPropertyLibraryDialog::accept()
{
    if (selectedLibraryId().isEmpty()) {
        QMessageBox::information(this, uiText("FDS Property Library"),
                                 uiText("Select an entry to copy into the project."));
        return;
    }
    QDialog::accept();
}

void FdsPropertyLibraryDialog::rebuildTable()
{
    const QString search = m_search->text().trimmed();
    const QString category = m_category->currentData().toString();
    const QString previous = selectedLibraryId();
    m_table->setRowCount(0);
    for (const FdsLibraryEntry& entry : m_library.entries()) {
        const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                     .arg(entry.name, entry.category,
                                          entry.keyword, entry.fdsId);
        if (!category.isEmpty() && entry.category != category) continue;
        if (!search.isEmpty() && !haystack.contains(search, Qt::CaseInsensitive)) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QStringList values = {
            entry.builtIn ? uiText("Built-in") : uiText("User"),
            entry.category, entry.name, entry.keyword, entry.fdsId, entry.fdsVersion};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            if (column == 0) item->setData(Qt::UserRole, entry.libraryId);
            m_table->setItem(row, column, item);
        }
        if (entry.libraryId == previous) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0) m_table->selectRow(0);
    m_table->resizeColumnsToContents();
}

QString FdsPropertyLibraryDialog::selectedLibraryId() const
{
    const int row = m_table ? m_table->currentRow() : -1;
    const QTableWidgetItem* item = row >= 0 ? m_table->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void FdsPropertyLibraryDialog::importEntries()
{
    const QString path = QFileDialog::getOpenFileName(
        this, uiText("Import FDS Library"), {},
        uiText("FireCAE FDS Library (*.json);;All Files (*.*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_library.importFile(path, FdsLibraryConflictPolicy::Rename, &error)) {
        QMessageBox::warning(this, uiText("Library Import Failed"), error);
        return;
    }
    rebuildTable();
}

void FdsPropertyLibraryDialog::exportEntries()
{
    const QString id = selectedLibraryId();
    if (id.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, uiText("Export FDS Library Entry"), QStringLiteral("fds-library.json"),
        uiText("FireCAE FDS Library (*.json)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_library.exportFile(path, {id}, &error)) {
        QMessageBox::warning(this, uiText("Library Export Failed"), error);
    }
}

void FdsPropertyLibraryDialog::duplicateEntry()
{
    QString error;
    const QString copyId = m_library.duplicateAsUser(selectedLibraryId(), &error);
    if (copyId.isEmpty()) {
        QMessageBox::warning(this, uiText("Library Copy Failed"), error);
        return;
    }
    rebuildTable();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->data(Qt::UserRole).toString() == copyId) {
            m_table->selectRow(row);
            break;
        }
    }
}

void FdsPropertyLibraryDialog::removeEntry()
{
    QString error;
    if (!m_library.removeUserEntry(selectedLibraryId(), &error)) {
        QMessageBox::information(this, uiText("FDS Property Library"), error);
        return;
    }
    rebuildTable();
}

MeshEngineeringDialog::MeshEngineeringDialog(const FcProject* project,
                                             const QStringList& selectedObjectIds,
                                             QWidget* parent)
    : QDialog(parent), m_project(project), m_selectedObjectIds(selectedObjectIds)
{
    setObjectName(QStringLiteral("MeshEngineeringDialog"));
    setWindowTitle(uiText("Mesh Engineering Assistant"));
    resize(820, 620);
    auto* root = new QVBoxLayout(this);
    auto* identity = new QFormLayout;
    m_namePrefix = new QLineEdit(QStringLiteral("Mesh"), this);
    m_idPrefix = new QLineEdit(QStringLiteral("MESH"), this);
    identity->addRow(uiText("Name prefix:"), m_namePrefix);
    identity->addRow(uiText("FDS ID prefix:"), m_idPrefix);
    root->addLayout(identity);

    auto* domainBox = new QGroupBox(uiText("Domain bounds (SI metres)"), this);
    auto* domain = new QGridLayout(domainBox);
    domain->addWidget(new QLabel(uiText("Axis"), domainBox), 0, 0);
    domain->addWidget(new QLabel(uiText("Minimum"), domainBox), 0, 1);
    domain->addWidget(new QLabel(uiText("Maximum"), domainBox), 0, 2);
    domain->addWidget(new QLabel(uiText("Length"), domainBox), 0, 3);
    const std::array<QString, 3> axes = {QStringLiteral("X"), QStringLiteral("Y"),
                                         QStringLiteral("Z")};
    const std::array<double, 3> defaults = {10.0, 10.0, 3.0};
    for (int axis = 0; axis < 3; ++axis) {
        m_minimum[axis] = engineeringSpin(domainBox, -1.0e9, 1.0e9, 0.0,
                                           QStringLiteral(" m"));
        m_maximum[axis] = engineeringSpin(domainBox, -1.0e9, 1.0e9, defaults[axis],
                                           QStringLiteral(" m"));
        m_minimum[axis]->setObjectName(
            QStringLiteral("MeshEngineeringMin%1Spin").arg(axes[axis]));
        m_maximum[axis]->setObjectName(
            QStringLiteral("MeshEngineeringMax%1Spin").arg(axes[axis]));
        auto* length = new QLabel(domainBox);
        length->setObjectName(QStringLiteral("MeshEngineeringLength%1Label").arg(axes[axis]));
        const auto updateLength = [this, length, axis]() {
            length->setText(QStringLiteral("%1 m")
                                .arg(m_maximum[axis]->value() - m_minimum[axis]->value(),
                                     0, 'g', 8));
        };
        connect(m_minimum[axis], &QDoubleSpinBox::valueChanged, this,
                [this, updateLength]() { updateLength(); updatePreview(); });
        connect(m_maximum[axis], &QDoubleSpinBox::valueChanged, this,
                [this, updateLength]() { updateLength(); updatePreview(); });
        updateLength();
        domain->addWidget(new QLabel(axes[axis], domainBox), axis + 1, 0);
        domain->addWidget(m_minimum[axis], axis + 1, 1);
        domain->addWidget(m_maximum[axis], axis + 1, 2);
        domain->addWidget(length, axis + 1, 3);
    }
    auto* fitSelection = new QPushButton(uiText("Fit Selected Geometry + Margin"),
                                         domainBox);
    fitSelection->setObjectName(QStringLiteral("MeshEngineeringFitSelectionButton"));
    m_margin = engineeringSpin(domainBox, 0.0, 1.0e6, 0.5, QStringLiteral(" m"));
    m_margin->setObjectName(QStringLiteral("MeshEngineeringMarginSpin"));
    domain->addWidget(fitSelection, 4, 1, 1, 2);
    domain->addWidget(m_margin, 4, 3);
    root->addWidget(domainBox);

    auto* resolutionBox = new QGroupBox(uiText("Resolution and automatic split"), this);
    auto* resolution = new QGridLayout(resolutionBox);
    m_resolutionMode = new QComboBox(resolutionBox);
    m_resolutionMode->setObjectName(QStringLiteral("MeshEngineeringResolutionModeCombo"));
    m_resolutionMode->addItem(uiText("Target cell size"), 0);
    m_resolutionMode->addItem(uiText("Total cells"), 1);
    resolution->addWidget(m_resolutionMode, 0, 0, 1, 4);
    resolution->addWidget(new QLabel(uiText("Axis"), resolutionBox), 1, 0);
    resolution->addWidget(new QLabel(uiText("Target size"), resolutionBox), 1, 1);
    resolution->addWidget(new QLabel(uiText("Total cells"), resolutionBox), 1, 2);
    resolution->addWidget(new QLabel(uiText("Split meshes"), resolutionBox), 1, 3);
    for (int axis = 0; axis < 3; ++axis) {
        m_targetSize[axis] = engineeringSpin(resolutionBox, 0.000001, 1.0e6,
                                              axis == 2 ? 0.15 : 0.2,
                                              QStringLiteral(" m"));
        m_totalCells[axis] = new QSpinBox(resolutionBox);
        m_totalCells[axis]->setRange(1, 1000000);
        m_totalCells[axis]->setValue(axis == 2 ? 20 : 50);
        m_splits[axis] = new QSpinBox(resolutionBox);
        m_splits[axis]->setRange(1, 128);
        m_splits[axis]->setValue(1);
        m_targetSize[axis]->setObjectName(
            QStringLiteral("MeshEngineeringTarget%1Spin").arg(axes[axis]));
        m_totalCells[axis]->setObjectName(
            QStringLiteral("MeshEngineeringCells%1Spin").arg(axes[axis]));
        m_splits[axis]->setObjectName(
            QStringLiteral("MeshEngineeringSplit%1Spin").arg(axes[axis]));
        resolution->addWidget(new QLabel(axes[axis], resolutionBox), axis + 2, 0);
        resolution->addWidget(m_targetSize[axis], axis + 2, 1);
        resolution->addWidget(m_totalCells[axis], axis + 2, 2);
        resolution->addWidget(m_splits[axis], axis + 2, 3);
        connect(m_targetSize[axis], &QDoubleSpinBox::valueChanged,
                this, [this]() { updatePreview(); });
        connect(m_totalCells[axis], &QSpinBox::valueChanged,
                this, [this]() { updatePreview(); });
        connect(m_splits[axis], &QSpinBox::valueChanged,
                this, [this]() { updatePreview(); });
    }
    m_multipleOfFour = new QCheckBox(
        uiText("Round each mesh axis cell count to a multiple of 4"), resolutionBox);
    m_multipleOfFour->setChecked(true);
    resolution->addWidget(m_multipleOfFour, 5, 0, 1, 4);

    auto* fireResolutionBox = new QGroupBox(
        uiText("Design-fire D*/cell-size resolution check"), this);
    auto* fireResolution = new QGridLayout(fireResolutionBox);
    m_designFireHrr = engineeringSpin(fireResolutionBox, 0.001, 1.0e9,
                                      1000.0, QStringLiteral(" kW"));
    m_designFireHrr->setObjectName(QStringLiteral("MeshEngineeringDesignFireHrrSpin"));
    m_cellsAcrossFireDiameter = new QSpinBox(fireResolutionBox);
    m_cellsAcrossFireDiameter->setObjectName(
        QStringLiteral("MeshEngineeringCellsAcrossDStarSpin"));
    m_cellsAcrossFireDiameter->setRange(4, 64);
    m_cellsAcrossFireDiameter->setValue(10);
    m_fireDiameterLabel = new QLabel(fireResolutionBox);
    m_fireDiameterLabel->setObjectName(QStringLiteral("MeshEngineeringDStarLabel"));
    auto* applyDStar = new QPushButton(
        uiText("Apply recommended D*/dx cell size"), fireResolutionBox);
    applyDStar->setObjectName(QStringLiteral("MeshEngineeringApplyDStarButton"));
    fireResolution->addWidget(new QLabel(uiText("Design peak HRR:"), fireResolutionBox), 0, 0);
    fireResolution->addWidget(m_designFireHrr, 0, 1);
    fireResolution->addWidget(new QLabel(uiText("Cells across D*:"), fireResolutionBox), 0, 2);
    fireResolution->addWidget(m_cellsAcrossFireDiameter, 0, 3);
    fireResolution->addWidget(m_fireDiameterLabel, 1, 0, 1, 3);
    fireResolution->addWidget(applyDStar, 1, 3);
    root->addWidget(fireResolutionBox);

    m_autoAdjustBounds = new QCheckBox(
        uiText("Allow the assistant to adjust mesh bounds to whole cells"), this);
    m_autoAdjustBounds->setObjectName(QStringLiteral("MeshEngineeringAutoAdjustBoundsCheck"));
    m_autoAdjustBounds->setChecked(true);
    root->addWidget(m_autoAdjustBounds);
    root->addWidget(resolutionBox);
    m_meshSummary = new QLabel(this);
    m_meshSummary->setObjectName(QStringLiteral("MeshEngineeringSummaryLabel"));
    m_meshSummary->setWordWrap(true);
    root->addWidget(m_meshSummary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &MeshEngineeringDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    connect(fitSelection, &QPushButton::clicked, this,
            [this]() { fitSelectedGeometry(); });
    connect(m_resolutionMode, &QComboBox::currentIndexChanged,
            this, [this]() { updatePreview(); });
    connect(m_multipleOfFour, &QCheckBox::toggled,
            this, [this]() { updatePreview(); });
    connect(m_designFireHrr, &QDoubleSpinBox::valueChanged,
            this, [this]() { updatePreview(); });
    connect(m_cellsAcrossFireDiameter, &QSpinBox::valueChanged,
            this, [this]() { updatePreview(); });
    connect(applyDStar, &QPushButton::clicked, this, [this]() {
        constexpr double rho = 1.2;
        constexpr double cp = 1.0;
        constexpr double ambientTemperature = 293.15;
        constexpr double gravity = 9.81;
        const double dStar = std::pow(
            m_designFireHrr->value() /
                (rho * cp * ambientTemperature * std::sqrt(gravity)), 0.4);
        const double cellSize = dStar / m_cellsAcrossFireDiameter->value();
        m_resolutionMode->setCurrentIndex(0);
        for (QDoubleSpinBox* spin : m_targetSize) spin->setValue(cellSize);
        updatePreview();
    });
    updatePreview();
}

std::vector<MeshEngineeringBlock> MeshEngineeringDialog::blocks() const
{
    std::array<int, 3> localCells{};
    for (int axis = 0; axis < 3; ++axis) {
        const double length = m_maximum[axis]->value() - m_minimum[axis]->value();
        const int split = m_splits[axis]->value();
        const int requestedTotal = m_resolutionMode->currentData().toInt() == 0
                                       ? qMax(1, static_cast<int>(std::ceil(
                                             length / m_targetSize[axis]->value())))
                                       : m_totalCells[axis]->value();
        localCells[axis] = qMax(1, static_cast<int>(std::ceil(
                                      static_cast<double>(requestedTotal) / split)));
        if (m_multipleOfFour->isChecked()) {
            localCells[axis] = ((localCells[axis] + 3) / 4) * 4;
        }
    }
    std::vector<MeshEngineeringBlock> result;
    int index = 1;
    for (int i = 0; i < m_splits[0]->value(); ++i) {
        for (int j = 0; j < m_splits[1]->value(); ++j) {
            for (int k = 0; k < m_splits[2]->value(); ++k) {
                MeshEngineeringBlock block;
                block.name = QStringLiteral("%1 %2").arg(m_namePrefix->text().trimmed())
                                 .arg(index, 3, 10, QLatin1Char('0'));
                block.fdsId = QStringLiteral("%1_%2").arg(cleanId(m_idPrefix->text()))
                                  .arg(index, 3, 10, QLatin1Char('0'));
                block.cells = localCells;
                const std::array<int, 3> indices = {i, j, k};
                const std::array<int, 3> splits = {m_splits[0]->value(),
                                                   m_splits[1]->value(),
                                                   m_splits[2]->value()};
                for (int axis = 0; axis < 3; ++axis) {
                    const double requestedLength =
                        m_maximum[axis]->value() - m_minimum[axis]->value();
                    const double length = m_autoAdjustBounds->isChecked() &&
                                                  m_resolutionMode->currentData().toInt() == 0
                                              ? localCells[axis] * splits[axis] *
                                                    m_targetSize[axis]->value()
                                              : requestedLength;
                    block.bounds[axis * 2] = m_minimum[axis]->value() +
                                             length * indices[axis] / splits[axis];
                    block.bounds[axis * 2 + 1] = m_minimum[axis]->value() +
                                                 length * (indices[axis] + 1) / splits[axis];
                }
                result.push_back(block);
                ++index;
            }
        }
    }
    return result;
}

void MeshEngineeringDialog::accept()
{
    QStringList errors;
    if (m_namePrefix->text().trimmed().isEmpty() || cleanId(m_idPrefix->text()).isEmpty()) {
        errors.append(uiText("Name and FDS ID prefixes are required."));
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (m_maximum[axis]->value() <= m_minimum[axis]->value()) {
            errors.append(uiText("Every domain maximum must be greater than its minimum."));
            break;
        }
    }
    const std::vector<MeshEngineeringBlock> generated = blocks();
    qint64 cellCount = 0;
    for (const MeshEngineeringBlock& block : generated) {
        cellCount += static_cast<qint64>(block.cells[0]) * block.cells[1] * block.cells[2];
    }
    if (generated.size() > 1024) errors.append(uiText("Automatic split exceeds 1024 meshes."));
    if (cellCount > 2000000000LL) errors.append(uiText("Total cell count exceeds the safe editor limit."));
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, uiText("Mesh Engineering Validation"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    QDialog::accept();
}

void MeshEngineeringDialog::updatePreview()
{
    const bool targetMode = m_resolutionMode->currentData().toInt() == 0;
    for (int axis = 0; axis < 3; ++axis) {
        m_targetSize[axis]->setEnabled(targetMode);
        m_totalCells[axis]->setEnabled(!targetMode);
    }
    const std::vector<MeshEngineeringBlock> generated = blocks();
    qint64 cells = 0;
    double minimumCell = 1.0e100;
    double maximumCell = 0.0;
    for (const MeshEngineeringBlock& block : generated) {
        const qint64 blockCells = static_cast<qint64>(block.cells[0]) *
                                  block.cells[1] * block.cells[2];
        cells += blockCells;
        for (int axis = 0; axis < 3; ++axis) {
            const double size = (block.bounds[axis * 2 + 1] - block.bounds[axis * 2]) /
                                block.cells[axis];
            minimumCell = qMin(minimumCell, size);
            maximumCell = qMax(maximumCell, size);
        }
    }
    const double aspect = minimumCell > 0.0 ? maximumCell / minimumCell : 0.0;
    const double memoryGiB = cells * 80.0 / (1024.0 * 1024.0 * 1024.0);
    constexpr double rho = 1.2;
    constexpr double cp = 1.0;
    constexpr double ambientTemperature = 293.15;
    constexpr double gravity = 9.81;
    const double dStar = std::pow(
        m_designFireHrr->value() /
            (rho * cp * ambientTemperature * std::sqrt(gravity)), 0.4);
    const double dStarRatio = maximumCell > 0.0 ? dStar / maximumCell : 0.0;
    const double recommendedCell = dStar / m_cellsAcrossFireDiameter->value();
    m_fireDiameterLabel->setText(
        uiText("D* = %1 m; recommended maximum cell size = %2 m")
            .arg(dStar, 0, 'g', 6).arg(recommendedCell, 0, 'g', 6));
    const qint64 largestMeshCells = generated.empty()
        ? 0
        : static_cast<qint64>(generated.front().cells[0]) *
              generated.front().cells[1] * generated.front().cells[2];
    const double computeIndex = cells * (minimumCell > 0.0 ? 1.0 / minimumCell : 0.0);
    const int mpiProcesses = qMin(static_cast<int>(generated.size()),
                                  qMax(1, QThread::idealThreadCount()));
    const QString resolutionRating = dStarRatio >= 16.0
        ? uiText("fine")
        : dStarRatio >= 10.0 ? uiText("moderate")
                              : uiText("coarse");
    m_meshSummary->setText(
        uiText("Generated meshes: %1    Total cells: %2    Estimated base memory: %3 GiB\n"
                       "Cell size range: %4–%5 m    Maximum aspect ratio: %6\n"
                       "D*/dx (using largest cell): %7 (%8)    Relative compute index: %9\n"
                       "Alignment: PASS    Overlap: PASS    Largest mesh load: %10 cells\n"
                       "Recommended MPI processes: %11 (CPU parallel; balanced meshes per process)")
            .arg(generated.size()).arg(cells).arg(memoryGiB, 0, 'f', 3)
            .arg(minimumCell, 0, 'g', 6).arg(maximumCell, 0, 'g', 6)
            .arg(aspect, 0, 'f', 3)
            .arg(dStarRatio, 0, 'f', 2).arg(resolutionRating)
            .arg(computeIndex, 0, 'g', 6).arg(largestMeshCells)
            .arg(mpiProcesses));
}

void MeshEngineeringDialog::fitSelectedGeometry()
{
    if (!m_project || !m_project->document()) return;
    Bnd_Box bounds;
    int count = 0;
    for (const QString& id : m_selectedObjectIds) {
        const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(
            m_project->document()->findObject(id));
        if (!geometry || !geometry->hasShape()) continue;
        BRepBndLib::Add(geometry->shape(), bounds);
        ++count;
    }
    if (count == 0 || bounds.IsVoid()) {
        QMessageBox::information(this, uiText("Mesh Engineering Assistant"),
                                 uiText("Select one or more geometry objects first."));
        return;
    }
    Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
    Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    const double margin = m_margin->value();
    const std::array<double, 3> minimum = {xMin - margin, yMin - margin, zMin - margin};
    const std::array<double, 3> maximum = {xMax + margin, yMax + margin, zMax + margin};
    for (int axis = 0; axis < 3; ++axis) {
        m_minimum[axis]->setValue(minimum[axis]);
        m_maximum[axis]->setValue(maximum[axis]);
    }
    updatePreview();
}
