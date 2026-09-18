#pragma once

#include "core/FcProject.h"

#include <QDialog>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class SimulationParametersDialog final : public QDialog
{
public:
    explicit SimulationParametersDialog(const FcProject* project,
                                        QWidget* parent = nullptr);

    QString projectName() const;
    QString chid() const;
    double endTime() const;
    FcSimulationParameters parameters() const;

private:
    void validateAndAccept();
    void updateEnabledStates();

    QLineEdit* m_projectName = nullptr;
    QLineEdit* m_chid = nullptr;
    QCheckBox* m_timeConfigured = nullptr;
    QDoubleSpinBox* m_startTime = nullptr;
    QDoubleSpinBox* m_endTime = nullptr;
    QDoubleSpinBox* m_initialTimeStep = nullptr;

    QCheckBox* m_environmentConfigured = nullptr;
    QDoubleSpinBox* m_ambientTemperature = nullptr;
    QDoubleSpinBox* m_ambientPressure = nullptr;
    std::array<QDoubleSpinBox*, 3> m_gravity{};
    QDoubleSpinBox* m_relativeHumidity = nullptr;
    QComboBox* m_simulationMode = nullptr;
    QComboBox* m_turbulenceModel = nullptr;

    QCheckBox* m_radiationConfigured = nullptr;
    QCheckBox* m_radiationEnabled = nullptr;
    QSpinBox* m_radiationAngles = nullptr;
    QCheckBox* m_combustionConfigured = nullptr;
    QComboBox* m_extinctionModel = nullptr;
    QDoubleSpinBox* m_fixedMixTime = nullptr;

    QCheckBox* m_outputConfigured = nullptr;
    std::array<QDoubleSpinBox*, 5> m_outputIntervals{};
    QCheckBox* m_restartEnabled = nullptr;
    QLineEdit* m_restartChid = nullptr;
    QDoubleSpinBox* m_restartInterval = nullptr;

    QCheckBox* m_windConfigured = nullptr;
    QDoubleSpinBox* m_windSpeed = nullptr;
    QDoubleSpinBox* m_windDirection = nullptr;
    QDoubleSpinBox* m_roughness = nullptr;
    QDoubleSpinBox* m_referenceHeight = nullptr;
    QCheckBox* m_initializationConfigured = nullptr;
    std::array<QDoubleSpinBox*, 6> m_initializationBounds{};
    QDoubleSpinBox* m_initializationTemperature = nullptr;
    QLabel* m_geomSummary = nullptr;

    QCheckBox* m_numericsConfigured = nullptr;
    QSpinBox* m_maximumPressureIterations = nullptr;
    QDoubleSpinBox* m_velocityTolerance = nullptr;
};
