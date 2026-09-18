#pragma once

#include "core/FcObject.h"

#include <QStringList>

#include <array>
#include <vector>

struct FcFdsBounds
{
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    double zMin = 0.0;
    double zMax = 0.0;

    bool isVolume() const;
    bool isPlanar() const;
};

class FcFdsObject : public FcObject
{
public:
    const QString& fdsId() const;
    void setFdsId(const QString& id);

protected:
    FcFdsObject(const QString& name, FcObjectType type, const QString& fdsId);

private:
    QString m_fdsId;
};

enum class FcFdsParameterKind
{
    Raw,
    String,
    ObjectReferences
};

struct FcFdsParameter
{
    QString key;
    FcFdsParameterKind kind = FcFdsParameterKind::Raw;
    QString value;
    QStringList targetObjectIds;
};

// A lossless, structured representation of any FDS namelist record that does
// not yet have a dedicated strongly typed FireCAE class.  It allows official
// FDS inputs to participate in the UUID object tree immediately, while common
// records can continue to gain dedicated editors incrementally.
class FcFdsNamelist final : public FcFdsObject
{
public:
    FcFdsNamelist(const QString& name,
                  FcObjectType type,
                  const QString& keyword,
                  const QString& fdsId = {},
                  int sequenceIndex = 0);

    const QString& keyword() const;
    int sequenceIndex() const;
    void setSequenceIndex(int index);
    const std::vector<FcFdsParameter>& parameters() const;
    void setParameters(const std::vector<FcFdsParameter>& parameters);
    void clearParameters();
    void addRawParameter(const QString& key, const QString& value);
    void addStringParameter(const QString& key, const QString& value);
    void addReferenceParameter(const QString& key,
                               const QStringList& targetObjectIds,
                               const QString& fallbackValue = {});
    bool setReferenceTargets(const QString& key,
                             const QStringList& targetObjectIds);
    QString parameterValue(const QString& key) const;
    QStringList validate() const;

private:
    QString m_keyword;
    int m_sequenceIndex = 0;
    std::vector<FcFdsParameter> m_parameters;
};

class FcFdsMesh final : public FcFdsObject
{
public:
    FcFdsMesh(const QString& name,
              const QString& fdsId,
              const std::array<int, 3>& cells,
              const FcFdsBounds& bounds);

    const std::array<int, 3>& cells() const;
    void setCells(const std::array<int, 3>& cells);
    const FcFdsBounds& bounds() const;
    void setBounds(const FcFdsBounds& bounds);
    QStringList validate() const;

private:
    std::array<int, 3> m_cells;
    FcFdsBounds m_bounds;
};

class FcFdsReaction final : public FcFdsObject
{
public:
    FcFdsReaction(const QString& name,
                  const QString& fdsId,
                  const QString& fuel,
                  double sootYield);

    const QString& fuel() const;
    void setFuel(const QString& fuel);
    double sootYield() const;
    void setSootYield(double value);
    QStringList validate() const;

private:
    QString m_fuel;
    double m_sootYield = 0.0;
};

class FcFdsSurface final : public FcFdsObject
{
public:
    FcFdsSurface(const QString& name,
                 const QString& fdsId,
                 double heatReleaseRatePerArea,
                 const QString& color = {});

    double heatReleaseRatePerArea() const;
    void setHeatReleaseRatePerArea(double value);
    const QString& color() const;
    void setColor(const QString& color);
    QStringList validate() const;

private:
    double m_heatReleaseRatePerArea = 0.0;
    QString m_color;
};

class FcFdsObstruction final : public FcFdsObject
{
public:
    FcFdsObstruction(const QString& name,
                     const QString& fdsId,
                     const FcFdsBounds& bounds,
                     const QString& surfaceId = {});

    const FcFdsBounds& bounds() const;
    void setBounds(const FcFdsBounds& bounds);
    const QString& surfaceId() const;
    void setSurfaceId(const QString& id);
    QStringList validate() const;

private:
    FcFdsBounds m_bounds;
    QString m_surfaceId;
};

class FcFdsVent final : public FcFdsObject
{
public:
    FcFdsVent(const QString& name,
              const QString& fdsId,
              const FcFdsBounds& bounds,
              const QString& surfaceId);

    const FcFdsBounds& bounds() const;
    void setBounds(const FcFdsBounds& bounds);
    const QString& surfaceId() const;
    void setSurfaceId(const QString& id);
    QStringList validate() const;

private:
    FcFdsBounds m_bounds;
    QString m_surfaceId;
};

enum class FcFdsOutputKind
{
    Boundary,
    Slice
};

enum class FcFdsPlaneAxis
{
    X,
    Y,
    Z
};

class FcFdsOutput final : public FcFdsObject
{
public:
    static std::shared_ptr<FcFdsOutput> boundary(const QString& name,
                                                 const QString& fdsId,
                                                 const QString& quantity);
    static std::shared_ptr<FcFdsOutput> slice(const QString& name,
                                              const QString& fdsId,
                                              FcFdsPlaneAxis axis,
                                              double planeValue,
                                              const QString& quantity,
                                              bool vectorOutput = false);

    FcFdsOutputKind kind() const;
    const QString& quantity() const;
    FcFdsPlaneAxis planeAxis() const;
    double planeValue() const;
    bool vectorOutput() const;
    QStringList validate() const;

private:
    FcFdsOutput(const QString& name,
                const QString& fdsId,
                FcFdsOutputKind kind,
                const QString& quantity,
                FcFdsPlaneAxis axis,
                double planeValue,
                bool vectorOutput);

    FcFdsOutputKind m_kind = FcFdsOutputKind::Boundary;
    QString m_quantity;
    FcFdsPlaneAxis m_axis = FcFdsPlaneAxis::X;
    double m_planeValue = 0.0;
    bool m_vectorOutput = false;
};
