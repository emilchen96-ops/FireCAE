#include "geometry/FcIfcObject.h"

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <algorithm>
#include <cmath>

namespace {
FcIfcAppearance normalized(FcIfcAppearance value)
{
    const auto channel = [](double x, double fallback) {
        return std::isfinite(x) ? std::clamp(x, 0.0, 1.0) : fallback;
    };
    value.red = channel(value.red, 0.72);
    value.green = channel(value.green, 0.75);
    value.blue = channel(value.blue, 0.78);
    value.alpha = channel(value.alpha, 1.0);
    return value;
}
}

FcIfcObject::FcIfcObject(const QString& name,
                         const QString& ifcClass,
                         const QString& globalId,
                         bool modelRoot)
    : FcObject(name, modelRoot ? FcObjectType::IfcModel : FcObjectType::IfcEntity)
    , m_ifcClass(ifcClass)
    , m_globalId(globalId)
    , m_appearance(typeFallbackAppearance(ifcClass))
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
    if (m_shape.IsNull() || shape.IsNull() || !m_shape.IsSame(shape))
        m_faceAppearances.clear();
    m_shape = shape;
    TopTools_IndexedMapOfShape faces;
    if (!shape.IsNull()) TopExp::MapShapes(shape, TopAbs_FACE, faces);
    m_faceCount = faces.Extent();
}

void FcIfcObject::clearShape()
{
    m_shape.Nullify();
    m_faceCount = 0;
    m_faceAppearances.clear();
}

bool FcIfcObject::hasShape() const
{
    return !m_shape.IsNull();
}

const FcIfcAppearance& FcIfcObject::appearance() const { return m_appearance; }
void FcIfcObject::setAppearance(const FcIfcAppearance& value)
{
    m_appearance = normalized(value);
}
const QMap<int, FcIfcAppearance>& FcIfcObject::faceAppearances() const
{
    return m_faceAppearances;
}
bool FcIfcObject::setFaceAppearance(int index, const FcIfcAppearance& value)
{
    if (m_shape.IsNull() || index < 1) return false;
    if (index > m_faceCount) return false;
    m_faceAppearances.insert(index, normalized(value));
    return true;
}
FcIfcAppearance FcIfcObject::appearanceForFace(int index) const
{
    return m_faceAppearances.value(index, m_appearance);
}
bool FcIfcObject::hasSourceAppearance() const
{
    if (m_appearance.origin == FcIfcAppearanceOrigin::ConvertedSource) return true;
    return std::any_of(m_faceAppearances.cbegin(), m_faceAppearances.cend(),
                      [](const FcIfcAppearance& a) {
        return a.origin == FcIfcAppearanceOrigin::ConvertedSource;
    });
}
bool FcIfcObject::hasFallbackAppearance() const
{
    if (std::any_of(m_faceAppearances.cbegin(), m_faceAppearances.cend(),
                    [](const FcIfcAppearance& a) {
        return a.origin == FcIfcAppearanceOrigin::TypeFallback;
    })) return true;
    if (m_appearance.origin != FcIfcAppearanceOrigin::TypeFallback) return false;
    return m_faceCount == 0 || m_faceAppearances.size() < m_faceCount;
}
FcIfcAppearance FcIfcObject::typeFallbackAppearance(const QString& ifcClass)
{
    const QString type = ifcClass.trimmed().toUpper();
    FcIfcAppearance value;
    if (type.contains(QStringLiteral("WINDOW"))) {
        value.red = 0.40; value.green = 0.68; value.blue = 0.82;
    } else if (type.contains(QStringLiteral("DOOR"))) {
        value.red = 0.56; value.green = 0.38; value.blue = 0.23;
    } else if (type.contains(QStringLiteral("WALL"))) {
        value.red = 0.80; value.green = 0.76; value.blue = 0.66;
    } else if (type.contains(QStringLiteral("SLAB")) || type.contains(QStringLiteral("ROOF"))) {
        value.red = 0.58; value.green = 0.63; value.blue = 0.68;
    } else if (type.contains(QStringLiteral("COLUMN")) || type.contains(QStringLiteral("BEAM"))) {
        value.red = 0.64; value.green = 0.69; value.blue = 0.75;
    } else if (type.contains(QStringLiteral("SPACE"))) {
        value.red = 0.58; value.green = 0.78; value.blue = 0.60;
    }
    return value;
}
