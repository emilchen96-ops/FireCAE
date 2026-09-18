#pragma once

#include "core/FcFloorObject.h"

#include <QDialog>

class FcProject;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;

struct FloorEditorData
{
    QString name;
    double baseElevation = 0.0;
    double storeyHeight = 3.0;
    double slabThickness = 0.2;
    double wallHeight = 3.0;
    QString backgroundImagePath;
    bool clippingEnabled = false;
    FcFloorClipRange clippingRange;
};

class FloorEditorDialog final : public QDialog
{
public:
    explicit FloorEditorDialog(const FcProject* project, QWidget* parent = nullptr);

    void setFloor(const FcFloorObject& floor);
    FloorEditorData data() const;

private:
    const FcProject* m_project = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QDoubleSpinBox* m_baseElevation = nullptr;
    QDoubleSpinBox* m_storeyHeight = nullptr;
    QDoubleSpinBox* m_slabThickness = nullptr;
    QDoubleSpinBox* m_wallHeight = nullptr;
    QLineEdit* m_backgroundImage = nullptr;
    QCheckBox* m_clippingEnabled = nullptr;
    QDoubleSpinBox* m_clipXMin = nullptr;
    QDoubleSpinBox* m_clipXMax = nullptr;
    QDoubleSpinBox* m_clipYMin = nullptr;
    QDoubleSpinBox* m_clipYMax = nullptr;
    QDoubleSpinBox* m_clipZMin = nullptr;
    QDoubleSpinBox* m_clipZMax = nullptr;
};
