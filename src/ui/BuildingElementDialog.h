#pragma once

#include "modeling/BuildingGeometryService.h"

#include <QDialog>
#include <QMap>

#include <memory>

class FcGeometryObject;
class FcProject;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QDialogButtonBox;
class QPushButton;
class QVBoxLayout;

class BuildingElementDialog final : public QDialog
{
    Q_OBJECT
public:
    BuildingElementDialog(FcGeometryKind initialKind,
                          FcProject* project,
                          QWidget* parent = nullptr);

    void setExistingObject(const std::shared_ptr<FcGeometryObject>& object);
    void setInitialRequest(const BuildingGeometryRequest& request);
    QString geometryName() const;
    BuildingGeometryRequest request() const;
    QString hostObjectId() const;
    QString controlObjectId() const;
    QString surfaceObjectId() const;
    QMap<QString, QString> faceSurfaceIds() const;
    bool dynamicOpening() const;
    QString groupObjectId() const;
    QString description() const;
    void setReadOnly(const QString& reason);
    bool validateInput(QString* error = nullptr) const;

signals:
    void facePreviewRequested(const QString& faceKey);
    void geometryPreviewRequested(const TopoDS_Shape& shape);

protected:
    void accept() override;

private:
    void populateReferences();
    void updateFieldAvailability();
    void updatePreviewSummary();
    void updateTopologyFaceEditors(const TopoDS_Shape& shape,
                                   const QMap<QString, QString>& assignments = {});
    void populateSurfaceCombo(QComboBox* combo, bool inheritDefault) const;
    void handleBaselineChanged();
    void populateProfileTable(const BuildingGeometryRequest& request);
    void populateMetadata(const QVariantMap& parameters);
    QVariantMap additionalFields(QString* error = nullptr) const;
    void updateColorButton();
    void addTableTools(QTableWidget* table, QVBoxLayout* layout, bool orderable);
    QVector<QPointF> parsePoints(const QString& text) const;
    QString formatPoints(const QVector<QPointF>& points) const;
    double fromDisplay(double value) const;
    double toDisplay(double value) const;

    FcProject* m_project = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QFormLayout* m_geometryForm = nullptr;
    QLineEdit* m_description = nullptr;
    QComboBox* m_groupCombo = nullptr;
    QTabWidget* m_tabs = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QLabel* m_readOnlyReason = nullptr;
    QLabel* m_bounds = nullptr;
    QCheckBox* m_customColor = nullptr;
    QPushButton* m_colorButton = nullptr;
    QCheckBox* m_outline = nullptr;
    QString m_color = QStringLiteral("#d1d6e0");
    QTableWidget* m_profileTable = nullptr;
    QTableWidget* m_advancedTable = nullptr;
    QComboBox* m_extrusionMode = nullptr;
    QDoubleSpinBox* m_extrusionDistance = nullptr;
    QDoubleSpinBox* m_direction[3] = {nullptr, nullptr, nullptr};
    QDoubleSpinBox* m_rotation = nullptr;
    QGroupBox* m_profileGroup = nullptr;
    QComboBox* m_surfaceMode = nullptr;
    QMap<QString, QCheckBox*> m_physicsChecks;
    QCheckBox* m_densityEnabled = nullptr;
    QDoubleSpinBox* m_density = nullptr;
    QVariantMap m_extraParameters;
    QMap<QString, QString> m_unresolvedFaces;
    bool m_updatingProfile = false;
    bool m_spatialProfile = false;
    bool m_readOnly = false;
    QComboBox* m_kindCombo = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QDoubleSpinBox* m_endX = nullptr;
    QDoubleSpinBox* m_endY = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_depth = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QDoubleSpinBox* m_thickness = nullptr;
    QDoubleSpinBox* m_radius = nullptr;
    QDoubleSpinBox* m_rise = nullptr;
    QSpinBox* m_steps = nullptr;
    QComboBox* m_baseline = nullptr;
    QComboBox* m_fdsConversionRoute = nullptr;
    QLineEdit* m_profile = nullptr;
    QLineEdit* m_path = nullptr;
    QComboBox* m_hostCombo = nullptr;
    QComboBox* m_controlCombo = nullptr;
    QComboBox* m_surfaceCombo = nullptr;
    QMap<QString, QComboBox*> m_faceSurfaceCombos;
    QMap<QString, QComboBox*> m_topologyFaceSurfaceCombos;
    QFormLayout* m_topologyFaceForm = nullptr;
    QGroupBox* m_topologyFaceGroup = nullptr;
    QCheckBox* m_dynamic = nullptr;
    QLabel* m_previewSummary = nullptr;
    bool m_editingWall = false;
    bool m_adjustingBaseline = false;
    FcWallBaseline m_lastBaseline = FcWallBaseline::Center;
};
