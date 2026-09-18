#pragma once

#include <QString>
#include <QVector>

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

enum class SnapTarget
{
    None,
    WorldGrid,
    FdsGrid,
    Vertex,
    EdgeMidpoint,
    Intersection,
    Edge,
    Face,
    ObjectCenter,
    Orthogonal,
    Angle
};

struct SnapSettings
{
    bool enabled = true;
    bool worldGrid = true;
    bool fdsGrid = true;
    bool vertex = true;
    bool edgeMidpoint = true;
    bool intersection = true;
    bool edge = true;
    bool face = true;
    bool objectCenter = true;
    bool orthogonal = true;
    bool angle = true;
    double worldGridStep = 0.1;
    double fdsGridStep = 0.1;
    double angleStepDegrees = 15.0;
};

struct SnapResult
{
    gp_Pnt point;
    SnapTarget target = SnapTarget::None;
    bool snapped = false;
    double distance = 0.0;
};

class SnapManager final
{
public:
    const SnapSettings& settings() const;
    void setSettings(const SnapSettings& settings);

    void setTemporarilyDisabled(bool disabled);
    bool isTemporarilyDisabled() const;

    SnapResult snapPoint(const gp_Pnt& rawPoint,
                         const QVector<TopoDS_Shape>& candidates = {},
                         double geometricTolerance = 0.15) const;
    gp_Vec constrainTranslation(const gp_Vec& translation) const;
    double snapAngle(double degrees) const;

    static QString targetName(SnapTarget target);

private:
    bool isEnabled() const;

    SnapSettings m_settings;
    bool m_temporarilyDisabled = false;
};
