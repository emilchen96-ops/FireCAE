#pragma once

#include "core/FcObject.h"

#include <TopoDS_Shape.hxx>
#include <QMap>

// Display information only: never a physical/FDS material assignment.
enum class FcIfcAppearanceOrigin { TypeFallback, ConvertedSource };

struct FcIfcAppearance
{
    // Linear RGB, as supplied by glTF/XCAF; alpha 1 is opaque.
    double red = 0.72;
    double green = 0.75;
    double blue = 0.78;
    double alpha = 1.0;
    FcIfcAppearanceOrigin origin = FcIfcAppearanceOrigin::TypeFallback;
    QString materialName;
};

class FcIfcObject final : public FcObject
{
public:
    FcIfcObject(const QString& name,
                const QString& ifcClass,
                const QString& globalId,
                bool modelRoot = false);

    const QString& ifcClass() const;
    const QString& globalId() const;
    const QString& description() const;
    void setDescription(const QString& description);

    const QString& sourceFile() const;
    void setSourceFile(const QString& sourceFile);

    const QString& schema() const;
    void setSchema(const QString& schema);

    const QString& fdsConversionRoute() const;
    void setFdsConversionRoute(const QString& route);

    const TopoDS_Shape& shape() const;
    void setShape(const TopoDS_Shape& shape);
    void clearShape();
    bool hasShape() const;

    const FcIfcAppearance& appearance() const;
    void setAppearance(const FcIfcAppearance& appearance);
    const QMap<int, FcIfcAppearance>& faceAppearances() const;
    bool setFaceAppearance(int faceIndex, const FcIfcAppearance& appearance);
    FcIfcAppearance appearanceForFace(int faceIndex) const;
    bool hasSourceAppearance() const;
    bool hasFallbackAppearance() const;
    static FcIfcAppearance typeFallbackAppearance(const QString& ifcClass);

private:
    QString m_ifcClass;
    QString m_globalId;
    QString m_description;
    QString m_sourceFile;
    QString m_schema;
    QString m_fdsConversionRoute = QStringLiteral("REFERENCE");
    TopoDS_Shape m_shape;
    int m_faceCount = 0;
    FcIfcAppearance m_appearance;
    // Indices refer to TopExp::MapShapes(shape, TopAbs_FACE), persisted with
    // that exact BREP. Replacing topology clears them; never guess a new face.
    QMap<int, FcIfcAppearance> m_faceAppearances;
};
