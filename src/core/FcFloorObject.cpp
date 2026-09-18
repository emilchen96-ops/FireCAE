#include "core/FcFloorObject.h"

#include <QtGlobal>

bool FcFloorClipRange::isValid() const
{
    return qIsFinite(xMin) && qIsFinite(xMax) && xMin < xMax &&
           qIsFinite(yMin) && qIsFinite(yMax) && yMin < yMax &&
           qIsFinite(zMin) && qIsFinite(zMax) && zMin < zMax;
}

FcFloorObject::FcFloorObject(const QString& name)
    : FcObject(name, FcObjectType::Floor)
{
}

double FcFloorObject::baseElevation() const { return m_baseElevation; }
void FcFloorObject::setBaseElevation(double value)
{
    if (!qIsFinite(value)) return;
    m_baseElevation = value;
    m_clippingRange.zMin = value;
    m_clippingRange.zMax = value + m_defaultStoreyHeight;
}

double FcFloorObject::defaultStoreyHeight() const { return m_defaultStoreyHeight; }
void FcFloorObject::setDefaultStoreyHeight(double value)
{
    if (!qIsFinite(value) || value <= 0.0) return;
    m_defaultStoreyHeight = value;
    m_clippingRange.zMax = m_baseElevation + value;
}

double FcFloorObject::defaultSlabThickness() const { return m_defaultSlabThickness; }
void FcFloorObject::setDefaultSlabThickness(double value)
{
    if (qIsFinite(value) && value > 0.0) m_defaultSlabThickness = value;
}

double FcFloorObject::defaultWallHeight() const { return m_defaultWallHeight; }
void FcFloorObject::setDefaultWallHeight(double value)
{
    if (qIsFinite(value) && value > 0.0) m_defaultWallHeight = value;
}

const QString& FcFloorObject::backgroundImagePath() const
{
    return m_backgroundImagePath;
}

void FcFloorObject::setBackgroundImagePath(const QString& value)
{
    m_backgroundImagePath = value.trimmed();
}

bool FcFloorObject::clippingEnabled() const { return m_clippingEnabled; }
void FcFloorObject::setClippingEnabled(bool enabled) { m_clippingEnabled = enabled; }

const FcFloorClipRange& FcFloorObject::clippingRange() const
{
    return m_clippingRange;
}

bool FcFloorObject::setClippingRange(const FcFloorClipRange& range)
{
    if (!range.isValid()) return false;
    m_clippingRange = range;
    return true;
}
