#pragma once

#include "fds/FcFdsModel.h"

#include <QString>
#include <QStringList>

#include <array>
#include <vector>

class FcProject;

enum class FdsScenePrimitiveKind
{
    Box,
    Plane,
    Point,
    Line
};

struct FdsSceneColor
{
    double red = 0.75;
    double green = 0.78;
    double blue = 0.82;
    double alpha = 1.0;
};

struct FdsScenePoint
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct FdsScenePrimitive
{
    FdsScenePrimitiveKind kind = FdsScenePrimitiveKind::Box;
    QString objectId;
    QString instanceKey;
    QString keyword;
    QString name;
    FcFdsBounds bounds;
    FdsScenePoint start;
    FdsScenePoint end;
    FdsSceneColor color;
    bool wireframe = false;
    bool visible = true;
    bool selectable = true;
};

struct FdsScene
{
    std::vector<FdsScenePrimitive> primitives;
    QStringList warnings;
    FcFdsBounds domainBounds;
    bool hasDomainBounds = false;

    int primitiveCount(const QString& keyword) const;
    int primitiveCountForObject(const QString& objectId) const;
};

class FdsSceneBuilder final
{
public:
    static FdsScene build(const FcProject& project);
};
