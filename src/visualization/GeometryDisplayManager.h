#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector>

#include <memory>

class FcGeometryObject;
class FcIfcObject;

struct GeometryDisplayStyle
{
    double red = 0.82;
    double green = 0.84;
    double blue = 0.88;
    double transparency = 0.0;
    bool wireframe = false;
    bool visible = true;
};

class GeometryDisplayManager final
{
public:
    explicit GeometryDisplayManager(const Handle(AIS_InteractiveContext)& context);
    // Detaches transient OCCT selection owners before the viewer is destroyed.
    ~GeometryDisplayManager();

    bool displayObject(const std::shared_ptr<FcGeometryObject>& object);
    bool displayShape(const QString& objectId, const TopoDS_Shape& shape);
    bool appendShape(const QString& objectId,
                     const TopoDS_Shape& shape,
                     const GeometryDisplayStyle& style = {});
    bool displayIfcModel(const std::shared_ptr<FcIfcObject>& rootObject);
    bool hideObject(const QString& objectId);
    bool showObject(const QString& objectId);
    bool selectObject(const QString& objectId);
    bool selectObjects(const QStringList& objectIds);
    QString selectAt(int x, int y, const Handle(V3d_View)& view,
                     bool toggle = false);
    QStringList selectRectangle(int x1, int y1, int x2, int y2,
                                const Handle(V3d_View)& view,
                                bool toggle = false);
    void clearSelection();
    QString selectedObjectId() const;
    QStringList selectedObjectIds() const;
    Handle(AIS_InteractiveObject) presentationForObject(
        const QString& objectId) const;
    QVector<Handle(AIS_InteractiveObject)> presentationsForObject(
        const QString& objectId) const;
    QString objectIdForPresentation(
        const Handle(AIS_InteractiveObject)& presentation) const;
    bool removeObject(const QString& objectId);
    int removeIfcModel(const std::shared_ptr<FcIfcObject>& rootObject);
    void clear(bool updateViewer = true);

    int displayedObjectCount() const;
    int reverseMappingCount() const;
    bool contains(const QString& objectId) const;
    bool isObjectVisible(const QString& objectId) const;
    bool fitSelection(const Handle(V3d_View)& view) const;
    bool attachManipulator(const QStringList& objectIds);
    void detachManipulator(bool updateViewer = true);
    bool updateManipulatorDetection(int x, int y, const Handle(V3d_View)& view);
    bool beginManipulatorDrag(int x, int y, const Handle(V3d_View)& view);
    bool updateManipulatorDrag(int x, int y, const Handle(V3d_View)& view);
    QStringList finishManipulatorDrag(bool apply);
    bool isManipulatorAttached() const;
    gp_Trsf objectLocalTransformation(const QString& objectId) const;
    void updateViewer();

private:
    bool displayShapeWithoutUpdate(const QString& objectId,
                                   const TopoDS_Shape& shape,
                                   const GeometryDisplayStyle& style,
                                   bool replaceExisting);
    bool removeObjectWithoutUpdate(const QString& objectId);

    Handle(AIS_InteractiveContext) m_context;
    QHash<QString, QVector<Handle(AIS_Shape)>> m_presentations;
    QHash<const AIS_InteractiveObject*, QString> m_objectIdsByPresentation;
    QSet<QString> m_selectedObjectIds;
    Handle(AIS_Manipulator) m_manipulator;
    QStringList m_manipulatorObjectIds;
};
