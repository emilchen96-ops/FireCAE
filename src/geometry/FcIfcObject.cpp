#include "geometry/FcIfcObject.h"

FcIfcObject::FcIfcObject(const QString& name,
                         const QString& ifcClass,
                         const QString& globalId,
                         bool modelRoot)
    : FcObject(name, modelRoot ? FcObjectType::IfcModel : FcObjectType::IfcEntity)
    , m_ifcClass(ifcClass)
    , m_globalId(globalId)
{
}

const QString& FcIfcObject::ifcClass() const
{
    return m_ifcClass;
}

const QString& FcIfcObject::globalId() const
{
    return m_globalId;
}

const QString& FcIfcObject::description() const
{
    return m_description;
}

void FcIfcObject::setDescription(const QString& description)
{
    m_description = description;
}

const QString& FcIfcObject::sourceFile() const
{
    return m_sourceFile;
}

void FcIfcObject::setSourceFile(const QString& sourceFile)
{
    m_sourceFile = sourceFile;
}

const QString& FcIfcObject::schema() const
{
    return m_schema;
}

void FcIfcObject::setSchema(const QString& schema)
{
    m_schema = schema;
}

const QString& FcIfcObject::fdsConversionRoute() const
{
    return m_fdsConversionRoute;
}

void FcIfcObject::setFdsConversionRoute(const QString& route)
{
    const QString normalized = route.trimmed().toUpper();
    m_fdsConversionRoute = normalized.isEmpty()
                               ? QStringLiteral("REFERENCE") : normalized;
}

const TopoDS_Shape& FcIfcObject::shape() const
{
    return m_shape;
}

void FcIfcObject::setShape(const TopoDS_Shape& shape)
{
    m_shape = shape;
}

void FcIfcObject::clearShape()
{
    m_shape.Nullify();
}

bool FcIfcObject::hasShape() const
{
    return !m_shape.IsNull();
}
