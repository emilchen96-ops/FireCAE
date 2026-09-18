#pragma once

#include "core/FcObject.h"

#include <TopoDS_Shape.hxx>
#include <QByteArray>
#include <QMap>
#include <QVariantMap>

enum class FcGeometryKind
{
    Generic,
    Box,
    Wall,
    Slab,
    Roof,
    Column,
    Beam,
    PolygonPrism,
    PolylineSweep,
    Cylinder,
    RectangleProfile,
    ProfileExtrusion,
    PathSweep,
    Stair,
    Ramp,
    Room,
    RectangularOpening,
    PolygonalOpening,
    Door,
    Window,
    SlabOpening,
    WallVent,
    BackgroundImage
};

QString fcGeometryKindName(FcGeometryKind kind);
FcGeometryKind fcGeometryKindFromName(const QString& name);

class FcGeometryObject final : public FcObject
{
public:
    explicit FcGeometryObject(const QString& name);
    FcGeometryObject(const QString& name, const TopoDS_Shape& shape);

    const TopoDS_Shape& shape() const;
    void setShape(const TopoDS_Shape& shape);
    bool hasShape() const;

    QByteArray shapeData() const;
    bool restoreShapeData(const QByteArray& data);

    FcGeometryKind geometryKind() const;
    void setGeometryKind(FcGeometryKind kind);
    const QVariantMap& geometryParameters() const;
    void setGeometryParameters(const QVariantMap& parameters);
    const QString& hostObjectId() const;
    void setHostObjectId(const QString& objectId);
    const QString& controlObjectId() const;
    void setControlObjectId(const QString& objectId);
    bool isDynamicOpening() const;
    void setDynamicOpening(bool dynamic);
    const QString& defaultSurfaceId() const;
    void setDefaultSurfaceId(const QString& objectId);
    const QMap<QString, QString>& faceSurfaceIds() const;
    void setFaceSurfaceIds(const QMap<QString, QString>& assignments);

private:
    TopoDS_Shape m_shape;
    FcGeometryKind m_geometryKind = FcGeometryKind::Generic;
    QVariantMap m_geometryParameters;
    QString m_hostObjectId;
    QString m_controlObjectId;
    bool m_dynamicOpening = false;
    QString m_defaultSurfaceId;
    QMap<QString, QString> m_faceSurfaceIds;
};
