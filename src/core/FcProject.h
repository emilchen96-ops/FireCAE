#pragma once

#include <QString>
#include <QVector>

#include "core/FcScenario.h"

#include <array>
#include <memory>

class FcDocument;

enum class FcDisplayUnit
{
    Meters,
    Centimeters,
    Millimeters,
    Feet,
    Inches
};

struct FcSimulationParameters
{
    bool timeConfigured = false;
    double startTime = 0.0;
    double initialTimeStep = 0.0;

    bool environmentConfigured = false;
    double ambientTemperature = 20.0;
    double ambientPressure = 101325.0;
    std::array<double, 3> gravity{0.0, 0.0, -9.81};
    double relativeHumidity = 40.0;
    QString simulationMode = QStringLiteral("VLES");
    QString turbulenceModel = QStringLiteral("DEARDORFF");

    bool radiationConfigured = false;
    bool radiationEnabled = true;
    int radiationAngles = 100;

    bool combustionConfigured = false;
    QString extinctionModel;
    double fixedMixTime = 0.0;

    bool outputCadenceConfigured = false;
    double deviceOutputInterval = 1.0;
    double hrrOutputInterval = 1.0;
    double sliceOutputInterval = 1.0;
    double boundaryOutputInterval = 1.0;
    double particleOutputInterval = 1.0;

    bool windConfigured = false;
    double windSpeed = 0.0;
    double windDirection = 270.0;
    double aerodynamicRoughness = 0.03;
    double windReferenceHeight = 2.0;

    bool initializationConfigured = false;
    std::array<double, 6> initializationBounds{0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    double initializationTemperature = 20.0;

    bool restartEnabled = false;
    QString restartChid;
    double restartInterval = 0.0;

    bool numericsConfigured = false;
    int maximumPressureIterations = 1000;
    double velocityTolerance = 0.0;

    bool operator==(const FcSimulationParameters& other) const;
    bool operator!=(const FcSimulationParameters& other) const
    {
        return !(*this == other);
    }
};

class FcProject
{
public:
    explicit FcProject(const QString& name);
    ~FcProject();

    FcProject(const FcProject&) = delete;
    FcProject& operator=(const FcProject&) = delete;

    const QString& name() const;
    void setName(const QString& name);

    const QString& chid() const;
    void setChid(const QString& chid);

    double endTime() const;
    void setEndTime(double seconds);

    const QString& fdsVersion() const;
    void setFdsVersion(const QString& version);

    FcDisplayUnit displayUnit() const;
    void setDisplayUnit(FcDisplayUnit unit);
    double metersToDisplay(double meters) const;
    double displayToMeters(double value) const;
    QString displayUnitSymbol() const;

    const FcSimulationParameters& simulationParameters() const;
    void setSimulationParameters(const FcSimulationParameters& parameters);

    FcDocument* document();
    const FcDocument* document() const;

    bool isModified() const;
    void setModified(bool modified);

    const QVector<FcScenario>& scenarios() const;
    const FcScenario* scenario(const QString& id) const;
    const FcScenario* activeScenario() const;
    QString addScenario(const QString& name);
    QString duplicateScenario(const QString& id, const QString& name = {});
    bool renameScenario(const QString& id, const QString& name);
    bool removeScenario(const QString& id);
    bool setActiveScenario(const QString& id);
    bool setDefaultScenario(const QString& id);
    const QString& activeScenarioId() const;
    const QString& defaultScenarioId() const;
    bool setScenarioObjectEnabled(const QString& scenarioId,
                                  const QString& objectId, bool enabled);
    bool setScenarioOverride(const QString& scenarioId,
                             const FcScenarioParameterOverride& overrideValue);
    bool updateScenario(const FcScenario& scenario);
    void setScenarios(const QVector<FcScenario>& scenarios,
                      const QString& activeId,
                      const QString& defaultId);

private:
    static QString makeChid(const QString& name);

    QString m_name;
    QString m_chid;
    double m_endTime = 60.0;
    QString m_fdsVersion = QStringLiteral("6.11.1");
    FcDisplayUnit m_displayUnit = FcDisplayUnit::Meters;
    FcSimulationParameters m_simulationParameters;
    std::unique_ptr<FcDocument> m_document;
    bool m_modified = false;
    QVector<FcScenario> m_scenarios;
    QString m_activeScenarioId;
    QString m_defaultScenarioId;
};
