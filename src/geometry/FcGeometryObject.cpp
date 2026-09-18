#include "geometry/FcGeometryObject.h"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <sstream>

QString fcGeometryKindName(FcGeometryKind kind)
{
    switch (kind) {
    case FcGeometryKind::Box: return QStringLiteral("Box");
    case FcGeometryKind::Wall: return QStringLiteral("Wall");
    case FcGeometryKind::Slab: return QStringLiteral("Slab");
    case FcGeometryKind::Roof: return QStringLiteral("Roof");
    case FcGeometryKind::Column: return QStringLiteral("Column");
    case FcGeometryKind::Beam: return QStringLiteral("Beam");
    case FcGeometryKind::PolygonPrism: return QStringLiteral("PolygonPrism");
    case FcGeometryKind::PolylineSweep: return QStringLiteral("PolylineSweep");
    case FcGeometryKind::Cylinder: return QStringLiteral("Cylinder");
    case FcGeometryKind::RectangleProfile: return QStringLiteral("RectangleProfile");
    case FcGeometryKind::ProfileExtrusion: return QStringLiteral("ProfileExtrusion");
    case FcGeometryKind::PathSweep: return QStringLiteral("PathSweep");
    case FcGeometryKind::Stair: return QStringLiteral("Stair");
    case FcGeometryKind::Ramp: return QStringLiteral("Ramp");
    case FcGeometryKind::Room: return QStringLiteral("Room");
    case FcGeometryKind::RectangularOpening: return QStringLiteral("RectangularOpening");
    case FcGeometryKind::PolygonalOpening: return QStringLiteral("PolygonalOpening");
    case FcGeometryKind::Door: return QStringLiteral("Door");
    case FcGeometryKind::Window: return QStringLiteral("Window");
    case FcGeometryKind::SlabOpening: return QStringLiteral("SlabOpening");
    case FcGeometryKind::WallVent: return QStringLiteral("WallVent");
    case FcGeometryKind::BackgroundImage: return QStringLiteral("BackgroundImage");
    case FcGeometryKind::Generic:
    default: return QStringLiteral("Generic");
    }
}

FcGeometryKind fcGeometryKindFromName(const QString& name)
{
    for (int value = static_cast<int>(FcGeometryKind::Generic);
         value <= static_cast<int>(FcGeometryKind::BackgroundImage); ++value) {
        const auto kind = static_cast<FcGeometryKind>(value);
        if (fcGeometryKindName(kind).compare(name, Qt::CaseInsensitive) == 0) {
            return kind;
        }
    }
    return FcGeometryKind::Generic;
}

FcGeometryObject::FcGeometryObject(const QString& name)
    : FcObject(name, FcObjectType::Geometry)
{
}

FcGeometryObject::FcGeometryObject(const QString& name, const TopoDS_Shape& shape)
    : FcObject(name, FcObjectType::Geometry)
    , m_shape(shape)
{
}

const TopoDS_Shape& FcGeometryObject::shape() const { return m_shape; }
void FcGeometryObject::setShape(const TopoDS_Shape& shape) { m_shape = shape; }
bool FcGeometryObject::hasShape() const { return !m_shape.IsNull(); }

QByteArray FcGeometryObject::shapeData() const
{
    if (m_shape.IsNull()) return {};
    std::ostringstream stream(std::ios::out | std::ios::binary);
    BRepTools::Write(m_shape, stream);
    const std::string text = stream.str();
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}

bool FcGeometryObject::restoreShapeData(const QByteArray& data)
{
    if (data.isEmpty()) return false;
    std::istringstream stream(
        std::string(data.constData(), static_cast<std::size_t>(data.size())),
        std::ios::in | std::ios::binary);
    TopoDS_Shape restored;
    BRep_Builder builder;
    BRepTools::Read(restored, stream, builder);
    if (restored.IsNull()) return false;
    m_shape = restored;
    return true;
}

FcGeometryKind FcGeometryObject::geometryKind() const { return m_geometryKind; }
void FcGeometryObject::setGeometryKind(FcGeometryKind kind) { m_geometryKind = kind; }
const QVariantMap& FcGeometryObject::geometryParameters() const
{
    return m_geometryParameters;
}
void FcGeometryObject::setGeometryParameters(const QVariantMap& parameters)
{
    m_geometryParameters = parameters;
}
const QString& FcGeometryObject::hostObjectId() const { return m_hostObjectId; }
void FcGeometryObject::setHostObjectId(const QString& objectId)
{
    m_hostObjectId = objectId;
}
const QString& FcGeometryObject::controlObjectId() const { return m_controlObjectId; }
void FcGeometryObject::setControlObjectId(const QString& objectId)
{
    m_controlObjectId = objectId;
}
bool FcGeometryObject::isDynamicOpening() const { return m_dynamicOpening; }
void FcGeometryObject::setDynamicOpening(bool dynamic) { m_dynamicOpening = dynamic; }
const QString& FcGeometryObject::defaultSurfaceId() const
{
    return m_defaultSurfaceId;
}
void FcGeometryObject::setDefaultSurfaceId(const QString& objectId)
{
    m_defaultSurfaceId = objectId;
}
const QMap<QString, QString>& FcGeometryObject::faceSurfaceIds() const
{
    return m_faceSurfaceIds;
}
void FcGeometryObject::setFaceSurfaceIds(const QMap<QString, QString>& assignments)
{
    m_faceSurfaceIds = assignments;
}
