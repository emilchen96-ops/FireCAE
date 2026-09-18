#pragma once

#include "settings/ApplicationSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class ApplicationSettingsDialog final : public QDialog
{
public:
    explicit ApplicationSettingsDialog(const ApplicationSettings& settings,
                                       QWidget* parent = nullptr);

    ApplicationSettings settings() const;

private:
    QComboBox* m_language = nullptr;
    QComboBox* m_unit = nullptr;
    QComboBox* m_theme = nullptr;
    QLineEdit* m_backgroundColor = nullptr;
    QCheckBox* m_autoSaveEnabled = nullptr;
    QSpinBox* m_autoSaveInterval = nullptr;
    QSpinBox* m_autoSaveMaximum = nullptr;
    QLineEdit* m_fdsExecutable = nullptr;
    QLineEdit* m_mpiExecutable = nullptr;
    QLineEdit* m_smokeviewExecutable = nullptr;
    QSpinBox* m_mpiProcessCount = nullptr;
    QCheckBox* m_autoOpenResults = nullptr;
    QCheckBox* m_backupBeforeOpen = nullptr;
    QCheckBox* m_saveBeforeRun = nullptr;
    QLineEdit* m_defaultWorkingDirectory = nullptr;
    QDoubleSpinBox* m_defaultMeshCellSize = nullptr;
    QLineEdit* m_defaultMaterial = nullptr;
    QComboBox* m_defaultColorScheme = nullptr;
    QCheckBox* m_highDpiEnabled = nullptr;
    QSpinBox* m_largeFileWarning = nullptr;
    QComboBox* m_logLevel = nullptr;
    QComboBox* m_renderQuality = nullptr;
};
