#include "core/FcProject.h"

#include "core/FcDocument.h"

#include <cmath>

namespace
{
bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) <= 1.0e-12;
}
}

bool FcSimulationParameters::operator==(
    const FcSimulationParameters& other) const
{
    return timeConfigured == other.timeConfigured &&
           nearlyEqual(startTime, other.startTime) &&
           nearlyEqual(initialTimeStep, other.initialTimeStep) &&
           environmentConfigured == other.environmentConfigured &&
           nearlyEqual(ambientTemperature, other.ambientTemperature) &&
           nearlyEqual(ambientPressure, other.ambientPressure) &&
           gravity == other.gravity &&
           nearlyEqual(relativeHumidity, other.relativeHumidity) &&
           simulationMode == other.simulationMode &&
           turbulenceModel == other.turbulenceModel &&
           radiationConfigured == other.radiationConfigured &&
           radiationEnabled == other.radiationEnabled &&
           radiationAngles == other.radiationAngles &&
           combustionConfigured == other.combustionConfigured &&
           extinctionModel == other.extinctionModel &&
           nearlyEqual(fixedMixTime, other.fixedMixTime) &&
           outputCadenceConfigured == other.outputCadenceConfigured &&
           nearlyEqual(deviceOutputInterval, other.deviceOutputInterval) &&
           nearlyEqual(hrrOutputInterval, other.hrrOutputInterval) &&
           nearlyEqual(sliceOutputInterval, other.sliceOutputInterval) &&
           nearlyEqual(boundaryOutputInterval, other.boundaryOutputInterval) &&
           nearlyEqual(particleOutputInterval, other.particleOutputInterval) &&
           windConfigured == other.windConfigured &&
           nearlyEqual(windSpeed, other.windSpeed) &&
           nearlyEqual(windDirection, other.windDirection) &&
           nearlyEqual(aerodynamicRoughness, other.aerodynamicRoughness) &&
           nearlyEqual(windReferenceHeight, other.windReferenceHeight) &&
           initializationConfigured == other.initializationConfigured &&
           initializationBounds == other.initializationBounds &&
           nearlyEqual(initializationTemperature,
                       other.initializationTemperature) &&
           restartEnabled == other.restartEnabled &&
           restartChid == other.restartChid &&
           nearlyEqual(restartInterval, other.restartInterval) &&
           numericsConfigured == other.numericsConfigured &&
           maximumPressureIterations == other.maximumPressureIterations &&
           nearlyEqual(velocityTolerance, other.velocityTolerance);
}

FcProject::FcProject(const QString& name)
    : m_name(name)
    , m_chid(makeChid(name))
    , m_document(std::make_unique<FcDocument>())
{
    FcScenario base = FcScenario::create(QStringLiteral("Default"), m_chid);
    m_activeScenarioId = base.id;
    m_defaultScenarioId = base.id;
    m_scenarios.append(base);
}

FcProject::~FcProject() = default;

const QString& FcProject::name() const { return m_name; }

void FcProject::setName(const QString& name)
{
    m_name = name;
    m_modified = true;
}

const QString& FcProject::chid() const { return m_chid; }

void FcProject::setChid(const QString& chid)
{
    const QString previous = m_chid;
    m_chid = chid.trimmed();
    if (m_scenarios.size() == 1 ||
        (!m_scenarios.isEmpty() &&
         (m_scenarios.front().chid.isEmpty() || m_scenarios.front().chid == previous))) {
        m_scenarios.front().chid = m_chid;
    }
    m_modified = true;
}

double FcProject::endTime() const { return m_endTime; }

void FcProject::setEndTime(double seconds)
{
    m_endTime = seconds;
    m_modified = true;
}

const QString& FcProject::fdsVersion() const { return m_fdsVersion; }

void FcProject::setFdsVersion(const QString& version)
{
    const QString normalized = version.trimmed();
    m_fdsVersion = normalized.isEmpty() ? QStringLiteral("6.11.1") : normalized;
    m_modified = true;
}

FcDisplayUnit FcProject::displayUnit() const { return m_displayUnit; }

void FcProject::setDisplayUnit(FcDisplayUnit unit)
{
    m_displayUnit = unit;
    m_modified = true;
}

double FcProject::metersToDisplay(double meters) const
{
    switch (m_displayUnit) {
    case FcDisplayUnit::Centimeters: return meters * 100.0;
    case FcDisplayUnit::Millimeters: return meters * 1000.0;
    case FcDisplayUnit::Feet: return meters / 0.3048;
    case FcDisplayUnit::Inches: return meters / 0.0254;
    case FcDisplayUnit::Meters:
    default: return meters;
    }
}

double FcProject::displayToMeters(double value) const
{
    switch (m_displayUnit) {
    case FcDisplayUnit::Centimeters: return value / 100.0;
    case FcDisplayUnit::Millimeters: return value / 1000.0;
    case FcDisplayUnit::Feet: return value * 0.3048;
    case FcDisplayUnit::Inches: return value * 0.0254;
    case FcDisplayUnit::Meters:
    default: return value;
    }
}

QString FcProject::displayUnitSymbol() const
{
    switch (m_displayUnit) {
    case FcDisplayUnit::Centimeters: return QStringLiteral("cm");
    case FcDisplayUnit::Millimeters: return QStringLiteral("mm");
    case FcDisplayUnit::Feet: return QStringLiteral("ft");
    case FcDisplayUnit::Inches: return QStringLiteral("in");
    case FcDisplayUnit::Meters:
    default: return QStringLiteral("m");
    }
}

const FcSimulationParameters& FcProject::simulationParameters() const
{
    return m_simulationParameters;
}

void FcProject::setSimulationParameters(
    const FcSimulationParameters& parameters)
{
    m_simulationParameters = parameters;
    m_modified = true;
}

FcDocument* FcProject::document() { return m_document.get(); }
const FcDocument* FcProject::document() const { return m_document.get(); }

bool FcProject::isModified() const { return m_modified; }
void FcProject::setModified(bool modified) { m_modified = modified; }

const QVector<FcScenario>& FcProject::scenarios() const { return m_scenarios; }

const FcScenario* FcProject::scenario(const QString& id) const
{
    for (const FcScenario& value : m_scenarios) if (value.id == id) return &value;
    return nullptr;
}

const FcScenario* FcProject::activeScenario() const
{
    return scenario(m_activeScenarioId);
}

QString FcProject::addScenario(const QString& name)
{
    FcScenario value = FcScenario::create(name, m_chid + QStringLiteral("_scenario"));
    m_scenarios.append(value);
    m_modified = true;
    return value.id;
}

QString FcProject::duplicateScenario(const QString& id, const QString& name)
{
    const FcScenario* source = scenario(id);
    if (!source) return {};
    FcScenario copy = *source;
    copy.id = FcScenario::create(QStringLiteral("Temporary")).id;
    copy.name = name.trimmed().isEmpty()
                    ? source->name + QStringLiteral(" Copy") : name.trimmed();
    copy.chid = source->chid + QStringLiteral("_copy");
    m_scenarios.append(copy);
    m_modified = true;
    return copy.id;
}

bool FcProject::renameScenario(const QString& id, const QString& name)
{
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) return false;
    for (FcScenario& value : m_scenarios) {
        if (value.id != id) continue;
        value.name = normalized;
        m_modified = true;
        return true;
    }
    return false;
}

bool FcProject::removeScenario(const QString& id)
{
    if (m_scenarios.size() <= 1) return false;
    for (qsizetype index = 0; index < m_scenarios.size(); ++index) {
        if (m_scenarios[index].id != id) continue;
        m_scenarios.removeAt(index);
        if (m_activeScenarioId == id) m_activeScenarioId = m_scenarios.front().id;
        if (m_defaultScenarioId == id) m_defaultScenarioId = m_scenarios.front().id;
        m_modified = true;
        return true;
    }
    return false;
}

bool FcProject::setActiveScenario(const QString& id)
{
    if (!scenario(id)) return false;
    m_activeScenarioId = id;
    m_modified = true;
    return true;
}

bool FcProject::setDefaultScenario(const QString& id)
{
    if (!scenario(id)) return false;
    m_defaultScenarioId = id;
    m_modified = true;
    return true;
}

const QString& FcProject::activeScenarioId() const { return m_activeScenarioId; }
const QString& FcProject::defaultScenarioId() const { return m_defaultScenarioId; }

bool FcProject::setScenarioObjectEnabled(const QString& scenarioId,
                                         const QString& objectId, bool enabled)
{
    for (FcScenario& value : m_scenarios) {
        if (value.id != scenarioId) continue;
        if (enabled) value.disabledObjectIds.remove(objectId);
        else value.disabledObjectIds.insert(objectId);
        m_modified = true;
        return true;
    }
    return false;
}

bool FcProject::setScenarioOverride(
    const QString& scenarioId, const FcScenarioParameterOverride& overrideValue)
{
    if (overrideValue.objectId.isEmpty() || overrideValue.parameterKey.trimmed().isEmpty()) {
        return false;
    }
    for (FcScenario& value : m_scenarios) {
        if (value.id != scenarioId) continue;
        for (FcScenarioParameterOverride& existing : value.parameterOverrides) {
            if (existing.objectId == overrideValue.objectId &&
                existing.parameterKey.compare(overrideValue.parameterKey,
                                              Qt::CaseInsensitive) == 0) {
                existing = overrideValue;
                m_modified = true;
                return true;
            }
        }
        value.parameterOverrides.append(overrideValue);
        m_modified = true;
        return true;
    }
    return false;
}

bool FcProject::updateScenario(const FcScenario& scenarioValue)
{
    if (!scenarioValue.isValid()) return false;
    for (FcScenario& value : m_scenarios) {
        if (value.id != scenarioValue.id) continue;
        value = scenarioValue;
        value.processCount = qBound(1, value.processCount, 1024);
        m_modified = true;
        return true;
    }
    return false;
}

void FcProject::setScenarios(const QVector<FcScenario>& scenarios,
                             const QString& activeId,
                             const QString& defaultId)
{
    m_scenarios.clear();
    for (const FcScenario& value : scenarios) {
        if (!value.isValid()) continue;
        bool duplicate = false;
        for (const FcScenario& existing : m_scenarios) {
            if (existing.id == value.id) { duplicate = true; break; }
        }
        if (!duplicate) m_scenarios.append(value);
    }
    if (m_scenarios.isEmpty()) {
        m_scenarios.append(FcScenario::create(QStringLiteral("Default"), m_chid));
    }
    m_activeScenarioId = scenario(activeId) ? activeId : m_scenarios.front().id;
    m_defaultScenarioId = scenario(defaultId) ? defaultId : m_scenarios.front().id;
    m_modified = true;
}

QString FcProject::makeChid(const QString& name)
{
    QString chid;
    for (const QChar character : name.trimmed()) {
        if (character.isLetterOrNumber()) {
            chid.append(character.toLower());
        } else if (!chid.endsWith(QLatin1Char('_'))) {
            chid.append(QLatin1Char('_'));
        }
    }
    while (chid.endsWith(QLatin1Char('_'))) {
        chid.chop(1);
    }
    return chid.isEmpty() ? QStringLiteral("firecae_case") : chid;
}
