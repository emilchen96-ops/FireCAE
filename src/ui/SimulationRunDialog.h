#pragma once

#include "simulation/FdsRunner.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;

class SimulationRunDialog final : public QDialog
{
public:
    explicit SimulationRunDialog(const QString& inputFilePath,
                                 const QString& fdsExecutablePath,
                                 FdsRunMode initialMode = FdsRunMode::Serial,
                                 int initialCount = 1,
                                 const QString& expectedSchemaVersion = QString{},
                                 QWidget* parent = nullptr);

    FdsRunRequest request() const;
    SolverLaunchPlan launchPlan() const;

private:
    void updatePreview();
    SolverBackendKind selectedBackendKind() const;

    QString m_inputFilePath;
    QString m_fdsExecutablePath;
    QString m_expectedSchemaVersion;
    QString m_probedExecutable;
    QString m_detectedSolverVersion;
    QString m_versionProbeError;
    QComboBox* m_backend = nullptr;
    QLabel* m_countLabel = nullptr;
    QSpinBox* m_count = nullptr;
    QLabel* m_workingDirectory = nullptr;
    QLabel* m_environment = nullptr;
    QLabel* m_solverVersion = nullptr;
    QPlainTextEdit* m_command = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
