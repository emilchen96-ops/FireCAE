#pragma once

#include "core/FcObject.h"

#include <QString>

struct FcFloorClipRange
{
    double xMin = -50.0;
    double xMax = 50.0;
    double yMin = -50.0;
    double yMax = 50.0;
    double zMin = 0.0;
    double zMax = 3.0;

    bool isValid() const;
};

class FcFloorObject final : public FcObject
{
public:
    explicit FcFloorObject(const QString& name);

    double baseElevation() const;
    void setBaseElevation(double value);

    double defaultStoreyHeight() const;
    void setDefaultStoreyHeight(double value);

    double defaultSlabThickness() const;
    void setDefaultSlabThickness(double value);

    double defaultWallHeight() const;
    void setDefaultWallHeight(double value);

    const QString& backgroundImagePath() const;
    void setBackgroundImagePath(const QString& value);

    bool clippingEnabled() const;
    void setClippingEnabled(bool enabled);

    const FcFloorClipRange& clippingRange() const;
    bool setClippingRange(const FcFloorClipRange& range);

private:
    double m_baseElevation = 0.0;
    double m_defaultStoreyHeight = 3.0;
    double m_defaultSlabThickness = 0.2;
    double m_defaultWallHeight = 3.0;
    QString m_backgroundImagePath;
    bool m_clippingEnabled = false;
    FcFloorClipRange m_clippingRange;
};
