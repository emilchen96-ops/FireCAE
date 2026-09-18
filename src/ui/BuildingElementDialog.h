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

class BuildingElementDialog final : public QDialog
{
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
    QVector<QPointF> parsePoints(const QString& text) const;
    QString formatPoints(const QVector<QPointF>& points) const;
    double fromDisplay(double value) const;
    double toDisplay(double value) const;

    FcProject* m_project = nullptr;
    QLineEdit* m_nameEdit = nullptr;
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
