#pragma once

#include "core/FcObject.h"

#include <TopoDS_Shape.hxx>

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

private:
    QString m_ifcClass;
    QString m_globalId;
    QString m_description;
    QString m_sourceFile;
    QString m_schema;
    QString m_fdsConversionRoute = QStringLiteral("REFERENCE");
    TopoDS_Shape m_shape;
};
