#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include "fds/FdsPropertyLibrary.h"

#include <array>
#include <vector>

class FcProject;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

struct FireSourceWizardData
{
    QString name;
    QString surfaceFdsId;
    QString reactionFdsId;
    QString rampFdsId;
    QString hostObjectId;
    QString fuel;
    QString color;
    double hrrpua = 500.0;
    double totalHrr = 500.0;
    double burningArea = 1.0;
    double sootYield = 0.01;
    double coYield = 0.0;
    double radiativeFraction = 0.35;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double width = 1.0;
    double depth = 1.0;
    double startTime = 0.0;
    double peakTime = 10.0;
    double endTime = 60.0;
    bool useTotalHrr = false;
    bool createRamp = true;
};

class FireSourceWizardDialog final : public QDialog
{
public:
    explicit FireSourceWizardDialog(const FcProject* project,
                                    QWidget* parent = nullptr);

    FireSourceWizardData data() const;
    void retranslateUi();

protected:
    void accept() override;
    void changeEvent(QEvent* event) override;

private:
    void updatePreview();

    const FcProject* m_project = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_surfaceId = nullptr;
    QLineEdit* m_reactionId = nullptr;
    QLineEdit* m_rampId = nullptr;
    QComboBox* m_host = nullptr;
    QLabel* m_hostScope = nullptr;
    QLabel* m_hostSurface = nullptr;
    QComboBox* m_powerMode = nullptr;
    QDoubleSpinBox* m_hrrpua = nullptr;
    QDoubleSpinBox* m_totalHrr = nullptr;
    QDoubleSpinBox* m_area = nullptr;
    QLineEdit* m_fuel = nullptr;
    QDoubleSpinBox* m_sootYield = nullptr;
    QDoubleSpinBox* m_coYield = nullptr;
    QDoubleSpinBox* m_radiativeFraction = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_depth = nullptr;
    QDoubleSpinBox* m_startTime = nullptr;
    QDoubleSpinBox* m_peakTime = nullptr;
    QDoubleSpinBox* m_endTime = nullptr;
    QCheckBox* m_createRamp = nullptr;
    QLineEdit* m_color = nullptr;
    QWidget* m_preview = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_previewNote = nullptr;
};

struct SprayPatternRow
{
    double elevationMinimum = 30.0;
    double elevationMaximum = 31.0;
    double azimuthMinimum = 0.0;
    double azimuthMaximum = 1.0;
    double radius = 5.0;
    double weight = 1.0;
};

struct ParticleSprayWizardData
{
    QString name;
    QString speciesFdsId;
    QString speciesFormula;
    QString particleFdsId;
    double particleDiameter = 1750.0;
    double particleAge = 0.0;
    QString propertyFdsId;
    QString propertyQuantity;
    double flowRate = 60.0;
    double particleVelocity = 5.0;
    int particlesPerSecond = 10000;
    double activationTemperature = 74.0;
    double responseTimeIndex = 50.0;
    QString smokeviewId;
    QString tableFdsId;
    std::vector<SprayPatternRow> sprayPattern;
    QString deviceFdsId;
    double x = 0.0;
    double y = 0.0;
    double z = 3.0;
    bool activateAtTime = false;
    double activationTime = 5.0;
    bool createParticleOutput = true;
    double particleOutputInterval = 1.0;
};

class ParticleSprayWizardDialog final : public QDialog
{
public:
    explicit ParticleSprayWizardDialog(const FcProject* project,
                                       QWidget* parent = nullptr);

    ParticleSprayWizardData data() const;

protected:
    void accept() override;

private:
    void addPatternRow(const SprayPatternRow& row = {});
    void removeSelectedPatternRows();
    void updateControls();
    bool hasExistingFdsId(const QString& keyword, const QString& fdsId) const;

    const FcProject* m_project = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_systemType = nullptr;
    QLineEdit* m_speciesId = nullptr;
    QLineEdit* m_speciesFormula = nullptr;
    QLineEdit* m_particleId = nullptr;
    QDoubleSpinBox* m_particleDiameter = nullptr;
    QDoubleSpinBox* m_particleAge = nullptr;
    QLineEdit* m_propertyId = nullptr;
    QDoubleSpinBox* m_flowRate = nullptr;
    QDoubleSpinBox* m_particleVelocity = nullptr;
    QSpinBox* m_particlesPerSecond = nullptr;
    QDoubleSpinBox* m_activationTemperature = nullptr;
    QDoubleSpinBox* m_responseTimeIndex = nullptr;
    QLineEdit* m_smokeviewId = nullptr;
    QLineEdit* m_tableId = nullptr;
    QTableWidget* m_patternTable = nullptr;
    QLineEdit* m_deviceId = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QComboBox* m_activationMode = nullptr;
    QDoubleSpinBox* m_activationTime = nullptr;
    QCheckBox* m_createParticleOutput = nullptr;
    QDoubleSpinBox* m_particleOutputInterval = nullptr;
};

struct OutputWizardData
{
    QString name;
    QString keyword;
    QString fdsId;
    QString quantity;
    QString axis;
    double coordinate = 0.0;
    bool vector = false;
    QString targetObjectId;
    double x = 0.0;
    double y = 0.0;
    double z = 1.5;
    double interval = 1.0;
};

class OutputWizardDialog final : public QDialog
{
public:
    explicit OutputWizardDialog(const FcProject* project,
                                QWidget* parent = nullptr);

    OutputWizardData data() const;

protected:
    void accept() override;

private:
    void updateControls();

    QLineEdit* m_name = nullptr;
    QComboBox* m_kind = nullptr;
    QLineEdit* m_fdsId = nullptr;
    QComboBox* m_quantity = nullptr;
    QComboBox* m_axis = nullptr;
    QDoubleSpinBox* m_coordinate = nullptr;
    QCheckBox* m_vector = nullptr;
    QComboBox* m_target = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QDoubleSpinBox* m_interval = nullptr;
};

struct DeviceControlWizardData
{
    QString name;
    QString deviceFdsId;
    QString quantity;
    QString propertyObjectId;
    double x = 0.0;
    double y = 0.0;
    double z = 1.5;
    double setpoint = 0.0;
    int tripDirection = 1;
    QString controlledObjectId;
    bool createControl = false;
    QString controlFdsId;
    double delay = 0.0;
    bool activateTarget = true;
};

class DeviceControlWizardDialog final : public QDialog
{
public:
    explicit DeviceControlWizardDialog(const FcProject* project,
                                       QWidget* parent = nullptr);

    DeviceControlWizardData data() const;

protected:
    void accept() override;

private:
    void updateControls();

    QLineEdit* m_name = nullptr;
    const FcProject* m_project = nullptr;
    QLineEdit* m_deviceId = nullptr;
    QComboBox* m_type = nullptr;
    QComboBox* m_property = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QDoubleSpinBox* m_setpoint = nullptr;
    QComboBox* m_tripDirection = nullptr;
    QComboBox* m_controlledObject = nullptr;
    QCheckBox* m_createControl = nullptr;
    QLineEdit* m_controlId = nullptr;
    QDoubleSpinBox* m_delay = nullptr;
    QComboBox* m_action = nullptr;
};

enum class FdsNetworkGraphKind
{
    Controls,
    Hvac
};

class FdsNetworkGraphDialog final : public QDialog
{
public:
    explicit FdsNetworkGraphDialog(const FcProject* project,
                                   FdsNetworkGraphKind kind,
                                   QWidget* parent = nullptr);

private:
    void rebuild();

    const FcProject* m_project = nullptr;
    FdsNetworkGraphKind m_kind = FdsNetworkGraphKind::Controls;
    QGraphicsView* m_view = nullptr;
    QLabel* m_summary = nullptr;
};

class FdsPropertyLibraryDialog final : public QDialog
{
public:
    explicit FdsPropertyLibraryDialog(const QString& userLibraryPath = {},
                                      QWidget* parent = nullptr);

    FdsLibraryEntry selectedEntry() const;

protected:
    void accept() override;

private:
    void rebuildTable();
    QString selectedLibraryId() const;
    void importEntries();
    void exportEntries();
    void duplicateEntry();
    void removeEntry();

    FdsPropertyLibrary m_library;
    QLineEdit* m_search = nullptr;
    QComboBox* m_category = nullptr;
    QTableWidget* m_table = nullptr;
};

struct MeshEngineeringBlock
{
    QString name;
    QString fdsId;
    std::array<int, 3> cells{};
    std::array<double, 6> bounds{};
};

class MeshEngineeringDialog final : public QDialog
{
public:
    explicit MeshEngineeringDialog(const FcProject* project,
                                   const QStringList& selectedObjectIds,
                                   QWidget* parent = nullptr);

    std::vector<MeshEngineeringBlock> blocks() const;

protected:
    void accept() override;

private:
    void updatePreview();
    void fitSelectedGeometry();

    const FcProject* m_project = nullptr;
    QStringList m_selectedObjectIds;
    std::array<QDoubleSpinBox*, 3> m_minimum{};
    std::array<QDoubleSpinBox*, 3> m_maximum{};
    std::array<QDoubleSpinBox*, 3> m_targetSize{};
    std::array<QSpinBox*, 3> m_totalCells{};
    std::array<QSpinBox*, 3> m_splits{};
    QComboBox* m_resolutionMode = nullptr;
    QDoubleSpinBox* m_margin = nullptr;
    QDoubleSpinBox* m_designFireHrr = nullptr;
    QSpinBox* m_cellsAcrossFireDiameter = nullptr;
    QLabel* m_fireDiameterLabel = nullptr;
    QCheckBox* m_autoAdjustBounds = nullptr;
    QCheckBox* m_multipleOfFour = nullptr;
    QLineEdit* m_namePrefix = nullptr;
    QLineEdit* m_idPrefix = nullptr;
    QLabel* m_meshSummary = nullptr;
};
