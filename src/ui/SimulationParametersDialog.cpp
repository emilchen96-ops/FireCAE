#include "ui/SimulationParametersDialog.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcGeometryObject.h"
#include "ui/UiLanguage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <functional>

namespace
{
QString t(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

QDoubleSpinBox* realSpin(QWidget* parent, double minimum, double maximum,
                         int decimals, const QString& suffix = {})
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    return spin;
}

QWidget* scrollablePage(QWidget* contents, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(contents);
    return scroll;
}

int countKeyword(const FcObject::Ptr& object, const QString& keyword)
{
    if (!object) return 0;
    int result = 0;
    const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
    if (namelist && namelist->keyword().compare(keyword,
                                                Qt::CaseInsensitive) == 0) {
        ++result;
    }
    for (const FcObject::Ptr& child : object->children())
        result += countKeyword(child, keyword);
    return result;
}
}

SimulationParametersDialog::SimulationParametersDialog(
    const FcProject* project, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("SimulationParametersDialog"));
    setWindowTitle(t("Simulation Parameters"));
    resize(820, 700);
    const FcSimulationParameters current =
        project ? project->simulationParameters() : FcSimulationParameters{};

    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("SimulationParametersTabs"));

    auto* generalContents = new QWidget;
    auto* general = new QFormLayout(generalContents);
    m_projectName = new QLineEdit(project ? project->name() : QString{}, generalContents);
    m_projectName->setObjectName(QStringLiteral("SimulationTitleEdit"));
    m_chid = new QLineEdit(project ? project->chid() : QString{}, generalContents);
    m_chid->setObjectName(QStringLiteral("SimulationChidEdit"));
    m_timeConfigured = new QCheckBox(t("Use professional time-step settings"),
                                     generalContents);
    m_timeConfigured->setObjectName(QStringLiteral("SimulationTimeConfiguredCheck"));
    m_timeConfigured->setChecked(current.timeConfigured);
    m_startTime = realSpin(generalContents, -1.0e9, 1.0e9, 6,
                           QStringLiteral(" s"));
    m_startTime->setObjectName(QStringLiteral("SimulationStartTimeSpin"));
    m_startTime->setValue(current.startTime);
    m_endTime = realSpin(generalContents, 1.0e-6, 1.0e9, 6,
                         QStringLiteral(" s"));
    m_endTime->setObjectName(QStringLiteral("SimulationEndTimeSpin"));
    m_endTime->setValue(project ? project->endTime() : 60.0);
    m_initialTimeStep = realSpin(generalContents, 0.0, 1.0e6, 9,
                                 QStringLiteral(" s"));
    m_initialTimeStep->setObjectName(QStringLiteral("SimulationInitialTimeStepSpin"));
    m_initialTimeStep->setValue(current.initialTimeStep);
    general->addRow(t("Title:"), m_projectName);
    general->addRow(QStringLiteral("CHID:"), m_chid);
    general->addRow(m_timeConfigured);
    general->addRow(t("Start time:"), m_startTime);
    general->addRow(t("End time:"), m_endTime);
    general->addRow(t("Initial time step (0 = automatic):"), m_initialTimeStep);
    auto* timeNote = new QLabel(t(
        "FDS normally chooses a stable time step automatically. Set an initial value only when the numerical method requires it."),
        generalContents);
    timeNote->setWordWrap(true);
    general->addRow(timeNote);
    tabs->addTab(scrollablePage(generalContents, tabs), t("Time"));

    auto* environmentContents = new QWidget;
    auto* environment = new QFormLayout(environmentContents);
    m_environmentConfigured = new QCheckBox(
        t("Write environment and flow settings to FDS"), environmentContents);
    m_environmentConfigured->setObjectName(
        QStringLiteral("SimulationEnvironmentConfiguredCheck"));
    m_environmentConfigured->setChecked(current.environmentConfigured);
    m_ambientTemperature = realSpin(environmentContents, -273.15, 2000.0, 3,
                                    QStringLiteral(" °C"));
    m_ambientTemperature->setObjectName(
        QStringLiteral("SimulationAmbientTemperatureSpin"));
    m_ambientTemperature->setValue(current.ambientTemperature);
    m_ambientPressure = realSpin(environmentContents, 1.0, 1.0e7, 1,
                                 QStringLiteral(" Pa"));
    m_ambientPressure->setObjectName(
        QStringLiteral("SimulationAmbientPressureSpin"));
    m_ambientPressure->setValue(current.ambientPressure);
    for (int index = 0; index < 3; ++index) {
        m_gravity[index] = realSpin(environmentContents, -1000.0, 1000.0, 6,
                                    QStringLiteral(" m/s²"));
        m_gravity[index]->setObjectName(
            QStringLiteral("SimulationGravity%1Spin").arg(index));
        m_gravity[index]->setValue(current.gravity[static_cast<std::size_t>(index)]);
    }
    m_relativeHumidity = realSpin(environmentContents, 0.0, 100.0, 2,
                                  QStringLiteral(" %"));
    m_relativeHumidity->setObjectName(
        QStringLiteral("SimulationRelativeHumiditySpin"));
    m_relativeHumidity->setValue(current.relativeHumidity);
    m_simulationMode = new QComboBox(environmentContents);
    m_simulationMode->setObjectName(QStringLiteral("SimulationModeCombo"));
    m_simulationMode->addItems({QStringLiteral("VLES"), QStringLiteral("LES"),
                                QStringLiteral("DNS"), QStringLiteral("SVLES")});
    m_simulationMode->setCurrentText(current.simulationMode);
    m_turbulenceModel = new QComboBox(environmentContents);
    m_turbulenceModel->setObjectName(QStringLiteral("SimulationTurbulenceModelCombo"));
    m_turbulenceModel->addItems({QStringLiteral("DEARDORFF"),
        QStringLiteral("CONSTANT SMAGORINSKY"),
        QStringLiteral("DYNAMIC SMAGORINSKY"), QStringLiteral("VREMAN"),
        QStringLiteral("WALE")});
    m_turbulenceModel->setCurrentText(current.turbulenceModel);
    environment->addRow(m_environmentConfigured);
    environment->addRow(t("Ambient temperature:"), m_ambientTemperature);
    environment->addRow(t("Ambient pressure:"), m_ambientPressure);
    environment->addRow(t("Gravity X:"), m_gravity[0]);
    environment->addRow(t("Gravity Y:"), m_gravity[1]);
    environment->addRow(t("Gravity Z:"), m_gravity[2]);
    environment->addRow(t("Relative humidity:"), m_relativeHumidity);
    environment->addRow(t("Simulation mode:"), m_simulationMode);
    environment->addRow(t("Turbulence model:"), m_turbulenceModel);
    tabs->addTab(scrollablePage(environmentContents, tabs),
                 t("Environment and Flow"));

    auto* physicsContents = new QWidget;
    auto* physics = new QFormLayout(physicsContents);
    m_radiationConfigured = new QCheckBox(t("Write radiation settings to FDS"),
                                          physicsContents);
    m_radiationConfigured->setObjectName(
        QStringLiteral("SimulationRadiationConfiguredCheck"));
    m_radiationConfigured->setChecked(current.radiationConfigured);
    m_radiationEnabled = new QCheckBox(t("Enable radiation solver"), physicsContents);
    m_radiationEnabled->setObjectName(QStringLiteral("SimulationRadiationEnabledCheck"));
    m_radiationEnabled->setChecked(current.radiationEnabled);
    m_radiationAngles = new QSpinBox(physicsContents);
    m_radiationAngles->setObjectName(QStringLiteral("SimulationRadiationAnglesSpin"));
    m_radiationAngles->setRange(4, 10000);
    m_radiationAngles->setValue(current.radiationAngles);
    m_combustionConfigured = new QCheckBox(t("Write combustion settings to FDS"),
                                           physicsContents);
    m_combustionConfigured->setObjectName(
        QStringLiteral("SimulationCombustionConfiguredCheck"));
    m_combustionConfigured->setChecked(current.combustionConfigured);
    m_extinctionModel = new QComboBox(physicsContents);
    m_extinctionModel->setObjectName(QStringLiteral("SimulationExtinctionModelCombo"));
    m_extinctionModel->setEditable(true);
    m_extinctionModel->addItems({QString{}, QStringLiteral("EXTINCTION 1"),
                                 QStringLiteral("EXTINCTION 2")});
    m_extinctionModel->setCurrentText(current.extinctionModel);
    m_fixedMixTime = realSpin(physicsContents, 0.0, 1.0e6, 6,
                              QStringLiteral(" s"));
    m_fixedMixTime->setObjectName(QStringLiteral("SimulationFixedMixTimeSpin"));
    m_fixedMixTime->setValue(current.fixedMixTime);
    physics->addRow(m_radiationConfigured);
    physics->addRow(m_radiationEnabled);
    physics->addRow(t("Radiation angles:"), m_radiationAngles);
    physics->addRow(m_combustionConfigured);
    physics->addRow(t("Extinction model:"), m_extinctionModel);
    physics->addRow(t("Fixed mixing time (0 = automatic):"), m_fixedMixTime);
    tabs->addTab(scrollablePage(physicsContents, tabs),
                 t("Radiation and Combustion"));

    auto* outputContents = new QWidget;
    auto* output = new QFormLayout(outputContents);
    m_outputConfigured = new QCheckBox(t("Use custom output frequencies"),
                                       outputContents);
    m_outputConfigured->setObjectName(QStringLiteral("SimulationOutputConfiguredCheck"));
    m_outputConfigured->setChecked(current.outputCadenceConfigured);
    const std::array<double, 5> intervals{
        current.deviceOutputInterval, current.hrrOutputInterval,
        current.sliceOutputInterval, current.boundaryOutputInterval,
        current.particleOutputInterval};
    const std::array<QString, 5> names{
        QStringLiteral("Device"), QStringLiteral("HRR"), QStringLiteral("Slice"),
        QStringLiteral("Boundary"), QStringLiteral("Particle")};
    const std::array<QString, 5> labels{
        t("Device CSV interval:"), t("HRR CSV interval:"),
        t("Slice interval:"), t("Boundary interval:"),
        t("Particle interval:")};
    for (int index = 0; index < 5; ++index) {
        m_outputIntervals[index] = realSpin(outputContents, 1.0e-6, 1.0e9, 6,
                                            QStringLiteral(" s"));
        m_outputIntervals[index]->setObjectName(
            QStringLiteral("Simulation%1OutputIntervalSpin").arg(names[index]));
        m_outputIntervals[index]->setValue(intervals[static_cast<std::size_t>(index)]);
        output->addRow(labels[static_cast<std::size_t>(index)],
                       m_outputIntervals[index]);
    }
    output->insertRow(0, m_outputConfigured);
    m_restartEnabled = new QCheckBox(t("Restart from an existing FDS checkpoint"),
                                     outputContents);
    m_restartEnabled->setObjectName(QStringLiteral("SimulationRestartEnabledCheck"));
    m_restartEnabled->setChecked(current.restartEnabled);
    m_restartChid = new QLineEdit(current.restartChid, outputContents);
    m_restartChid->setObjectName(QStringLiteral("SimulationRestartChidEdit"));
    m_restartChid->setPlaceholderText(project ? project->chid() : QString{});
    m_restartInterval = realSpin(outputContents, 0.0, 1.0e9, 6,
                                 QStringLiteral(" s"));
    m_restartInterval->setObjectName(QStringLiteral("SimulationRestartIntervalSpin"));
    m_restartInterval->setValue(current.restartInterval);
    output->addRow(m_restartEnabled);
    output->addRow(t("Restart source CHID:"), m_restartChid);
    output->addRow(t("Checkpoint interval (0 = stop only):"), m_restartInterval);
    tabs->addTab(scrollablePage(outputContents, tabs), t("Output and Restart"));

    auto* domainContents = new QWidget;
    auto* domain = new QFormLayout(domainContents);
    m_windConfigured = new QCheckBox(t("Enable atmospheric wind"), domainContents);
    m_windConfigured->setObjectName(QStringLiteral("SimulationWindConfiguredCheck"));
    m_windConfigured->setChecked(current.windConfigured);
    m_windSpeed = realSpin(domainContents, 0.0, 500.0, 4,
                           QStringLiteral(" m/s"));
    m_windSpeed->setObjectName(QStringLiteral("SimulationWindSpeedSpin"));
    m_windSpeed->setValue(current.windSpeed);
    m_windDirection = realSpin(domainContents, -3600.0, 3600.0, 3,
                               QStringLiteral(" °"));
    m_windDirection->setObjectName(QStringLiteral("SimulationWindDirectionSpin"));
    m_windDirection->setValue(current.windDirection);
    m_roughness = realSpin(domainContents, 0.0, 1000.0, 6,
                           QStringLiteral(" m"));
    m_roughness->setObjectName(QStringLiteral("SimulationWindRoughnessSpin"));
    m_roughness->setValue(current.aerodynamicRoughness);
    m_referenceHeight = realSpin(domainContents, 1.0e-6, 1.0e6, 4,
                                 QStringLiteral(" m"));
    m_referenceHeight->setObjectName(QStringLiteral("SimulationWindReferenceHeightSpin"));
    m_referenceHeight->setValue(current.windReferenceHeight);
    domain->addRow(m_windConfigured);
    domain->addRow(t("Wind speed:"), m_windSpeed);
    domain->addRow(t("Direction:"), m_windDirection);
    domain->addRow(t("Aerodynamic roughness:"), m_roughness);
    domain->addRow(t("Reference height:"), m_referenceHeight);
    m_initializationConfigured = new QCheckBox(
        t("Create a uniform initialization region"), domainContents);
    m_initializationConfigured->setObjectName(
        QStringLiteral("SimulationInitializationConfiguredCheck"));
    m_initializationConfigured->setChecked(current.initializationConfigured);
    const std::array<QString, 6> boundNames{
        QStringLiteral("XMin"), QStringLiteral("XMax"),
        QStringLiteral("YMin"), QStringLiteral("YMax"),
        QStringLiteral("ZMin"), QStringLiteral("ZMax")};
    const std::array<QString, 6> boundLabels{
        t("Initial region X minimum:"), t("Initial region X maximum:"),
        t("Initial region Y minimum:"), t("Initial region Y maximum:"),
        t("Initial region Z minimum:"), t("Initial region Z maximum:")};
    domain->addRow(m_initializationConfigured);
    for (int index = 0; index < 6; ++index) {
        m_initializationBounds[index] = realSpin(domainContents, -1.0e9, 1.0e9,
                                                  6, QStringLiteral(" m"));
        m_initializationBounds[index]->setObjectName(
            QStringLiteral("SimulationInitial%1Spin").arg(boundNames[index]));
        m_initializationBounds[index]->setValue(
            current.initializationBounds[static_cast<std::size_t>(index)]);
        domain->addRow(boundLabels[static_cast<std::size_t>(index)],
                       m_initializationBounds[index]);
    }
    m_initializationTemperature = realSpin(domainContents, -273.15, 2000.0, 3,
                                            QStringLiteral(" °C"));
    m_initializationTemperature->setObjectName(
        QStringLiteral("SimulationInitializationTemperatureSpin"));
    m_initializationTemperature->setValue(current.initializationTemperature);
    domain->addRow(t("Initial temperature:"), m_initializationTemperature);
    int geomCount = 0;
    if (project && project->document()) {
        for (const auto& group : project->document()->groups())
            geomCount += countKeyword(group, QStringLiteral("GEOM"));
    }
    m_geomSummary = new QLabel(
        t("Native GEOM records are managed by Geometry > FDS Conversion. Current explicit GEOM records: %1")
            .arg(geomCount), domainContents);
    m_geomSummary->setObjectName(QStringLiteral("SimulationGeomSummaryLabel"));
    m_geomSummary->setWordWrap(true);
    domain->addRow(m_geomSummary);
    tabs->addTab(scrollablePage(domainContents, tabs), t("Wind, Initial, and GEOM"));

    auto* numericsContents = new QWidget;
    auto* numerics = new QFormLayout(numericsContents);
    m_numericsConfigured = new QCheckBox(t("Write advanced pressure solver settings"),
                                         numericsContents);
    m_numericsConfigured->setObjectName(QStringLiteral("SimulationNumericsConfiguredCheck"));
    m_numericsConfigured->setChecked(current.numericsConfigured);
    m_maximumPressureIterations = new QSpinBox(numericsContents);
    m_maximumPressureIterations->setObjectName(
        QStringLiteral("SimulationMaximumPressureIterationsSpin"));
    m_maximumPressureIterations->setRange(1, 10000000);
    m_maximumPressureIterations->setValue(current.maximumPressureIterations);
    m_velocityTolerance = realSpin(numericsContents, 0.0, 1.0e6, 9,
                                   QStringLiteral(" m/s"));
    m_velocityTolerance->setObjectName(QStringLiteral("SimulationVelocityToleranceSpin"));
    m_velocityTolerance->setValue(current.velocityTolerance);
    numerics->addRow(m_numericsConfigured);
    numerics->addRow(t("Maximum pressure iterations:"),
                     m_maximumPressureIterations);
    numerics->addRow(t("Velocity tolerance (0 = FDS default):"),
                     m_velocityTolerance);
    auto* advancedNote = new QLabel(t(
        "Only validated FDS fields are written here. Additional numerical keywords remain available in the Advanced FDS Object editor."),
        numericsContents);
    advancedNote->setWordWrap(true);
    numerics->addRow(advancedNote);
    tabs->addTab(scrollablePage(numericsContents, tabs), t("Numerics / Advanced"));

    root->addWidget(tabs, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         this);
    buttons->setObjectName(QStringLiteral("SimulationParametersButtons"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SimulationParametersDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto update = [this]() { updateEnabledStates(); };
    for (QCheckBox* check : {m_timeConfigured, m_environmentConfigured,
                             m_radiationConfigured, m_combustionConfigured,
                             m_outputConfigured, m_restartEnabled,
                             m_windConfigured, m_initializationConfigured,
                             m_numericsConfigured}) {
        connect(check, &QCheckBox::toggled, this, update);
    }
    updateEnabledStates();
}

QString SimulationParametersDialog::projectName() const
{
    return m_projectName->text().trimmed();
}

QString SimulationParametersDialog::chid() const
{
    return m_chid->text().trimmed();
}

double SimulationParametersDialog::endTime() const
{
    return m_endTime->value();
}

FcSimulationParameters SimulationParametersDialog::parameters() const
{
    FcSimulationParameters result;
    result.timeConfigured = m_timeConfigured->isChecked();
    result.startTime = m_startTime->value();
    result.initialTimeStep = m_initialTimeStep->value();
    result.environmentConfigured = m_environmentConfigured->isChecked();
    result.ambientTemperature = m_ambientTemperature->value();
    result.ambientPressure = m_ambientPressure->value();
    for (int index = 0; index < 3; ++index)
        result.gravity[static_cast<std::size_t>(index)] = m_gravity[index]->value();
    result.relativeHumidity = m_relativeHumidity->value();
    result.simulationMode = m_simulationMode->currentText();
    result.turbulenceModel = m_turbulenceModel->currentText();
    result.radiationConfigured = m_radiationConfigured->isChecked();
    result.radiationEnabled = m_radiationEnabled->isChecked();
    result.radiationAngles = m_radiationAngles->value();
    result.combustionConfigured = m_combustionConfigured->isChecked();
    result.extinctionModel = m_extinctionModel->currentText().trimmed();
    result.fixedMixTime = m_fixedMixTime->value();
    result.outputCadenceConfigured = m_outputConfigured->isChecked();
    result.deviceOutputInterval = m_outputIntervals[0]->value();
    result.hrrOutputInterval = m_outputIntervals[1]->value();
    result.sliceOutputInterval = m_outputIntervals[2]->value();
    result.boundaryOutputInterval = m_outputIntervals[3]->value();
    result.particleOutputInterval = m_outputIntervals[4]->value();
    result.restartEnabled = m_restartEnabled->isChecked();
    result.restartChid = m_restartChid->text().trimmed();
    result.restartInterval = m_restartInterval->value();
    result.windConfigured = m_windConfigured->isChecked();
    result.windSpeed = m_windSpeed->value();
    result.windDirection = m_windDirection->value();
    result.aerodynamicRoughness = m_roughness->value();
    result.windReferenceHeight = m_referenceHeight->value();
    result.initializationConfigured = m_initializationConfigured->isChecked();
    for (int index = 0; index < 6; ++index) {
        result.initializationBounds[static_cast<std::size_t>(index)] =
            m_initializationBounds[index]->value();
    }
    result.initializationTemperature = m_initializationTemperature->value();
    result.numericsConfigured = m_numericsConfigured->isChecked();
    result.maximumPressureIterations = m_maximumPressureIterations->value();
    result.velocityTolerance = m_velocityTolerance->value();
    return result;
}

void SimulationParametersDialog::validateAndAccept()
{
    QStringList errors;
    if (projectName().isEmpty()) errors.append(t("Title must not be empty."));
    if (chid().isEmpty()) errors.append(t("CHID must not be empty."));
    if (endTime() <= m_startTime->value())
        errors.append(t("End time must be greater than start time."));
    if (m_initializationConfigured->isChecked()) {
        if (m_initializationBounds[0]->value() >= m_initializationBounds[1]->value() ||
            m_initializationBounds[2]->value() >= m_initializationBounds[3]->value() ||
            m_initializationBounds[4]->value() >= m_initializationBounds[5]->value()) {
            errors.append(t("Initialization region minimum values must be below maximum values."));
        }
    }
    if (m_restartEnabled->isChecked() &&
        m_restartChid->text().trimmed().isEmpty() && chid().isEmpty()) {
        errors.append(t("Restart requires a source CHID."));
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, t("Invalid Simulation Parameters"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    accept();
}

void SimulationParametersDialog::updateEnabledStates()
{
    m_startTime->setEnabled(m_timeConfigured->isChecked());
    m_initialTimeStep->setEnabled(m_timeConfigured->isChecked());
    const std::array<QWidget*, 8> environmentWidgets{
        m_ambientTemperature, m_ambientPressure, m_gravity[0], m_gravity[1],
        m_gravity[2], m_relativeHumidity, m_simulationMode, m_turbulenceModel};
    for (QWidget* widget : environmentWidgets)
        widget->setEnabled(m_environmentConfigured->isChecked());
    m_radiationEnabled->setEnabled(m_radiationConfigured->isChecked());
    m_radiationAngles->setEnabled(m_radiationConfigured->isChecked());
    m_extinctionModel->setEnabled(m_combustionConfigured->isChecked());
    m_fixedMixTime->setEnabled(m_combustionConfigured->isChecked());
    for (QDoubleSpinBox* spin : m_outputIntervals)
        spin->setEnabled(m_outputConfigured->isChecked());
    m_restartChid->setEnabled(m_restartEnabled->isChecked());
    m_restartInterval->setEnabled(m_restartEnabled->isChecked() ||
                                  m_outputConfigured->isChecked());
    const std::array<QWidget*, 4> windWidgets{
        m_windSpeed, m_windDirection, m_roughness, m_referenceHeight};
    for (QWidget* widget : windWidgets)
        widget->setEnabled(m_windConfigured->isChecked());
    for (QDoubleSpinBox* spin : m_initializationBounds)
        spin->setEnabled(m_initializationConfigured->isChecked());
    m_initializationTemperature->setEnabled(
        m_initializationConfigured->isChecked());
    m_maximumPressureIterations->setEnabled(m_numericsConfigured->isChecked());
    m_velocityTolerance->setEnabled(m_numericsConfigured->isChecked());
}
