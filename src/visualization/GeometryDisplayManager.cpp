#include "visualization/GeometryDisplayManager.h"
#include "modeling/BuildingGeometryService.h"
#include <TopoDS_Face.hxx>

#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"

#include <AIS_TexturedShape.hxx>
#include <AIS_ColoredShape.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_Group.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QColor>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace {
// The lines belong to the same presentation/selection owner as the solid.
// Highlighting is an OCCT overlay and never overwrites domain appearance.
class IfcColoredPresentation final : public AIS_ColoredShape
{
public:
    explicit IfcColoredPresentation(const TopoDS_Shape& shape)
        : AIS_ColoredShape(shape), m_edges(GeometryDisplayManager::ifcFeatureEdges(shape)) {}
protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer mode) override
    {
        AIS_ColoredShape::Compute(manager, presentation, mode);
        if (mode != 1 || m_edges.isEmpty()) return;
        Handle(Graphic3d_ArrayOfSegments) segments = new Graphic3d_ArrayOfSegments(
            static_cast<Standard_Integer>(m_edges.size() * 2));
        for (const auto& edge : m_edges) {
            segments->AddVertex(edge[0]);
            segments->AddVertex(edge[1]);
        }
        const Handle(Graphic3d_Group) group = presentation->NewGroup();
        group->SetPrimitivesAspect(Attributes()->FaceBoundaryAspect()->Aspect());
        group->AddPrimitiveArray(segments);
    }
private:
    QVector<std::array<gp_Pnt, 2>> m_edges;
};
}

QVector<std::array<gp_Pnt, 2>> GeometryDisplayManager::ifcFeatureEdges(
    const TopoDS_Shape& shape, double creaseAngleDegrees)
{
    QVector<std::array<gp_Pnt, 2>> result;
    if (shape.IsNull()) return result;
    struct PointHash {
        std::size_t operator()(const std::array<std::int64_t, 3>& p) const {
            std::size_t seed = 0;
            for (auto value : p) seed ^= std::hash<std::int64_t>{}(value) +
                0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
    struct Edge { int first; int second; gp_Vec normal; int count = 1; bool crease = false; };
    std::unordered_map<std::array<std::int64_t, 3>, int, PointHash> vertices;
    std::unordered_map<std::uint64_t, Edge> edges;
    QVector<gp_Pnt> points;
    const double cosine = std::cos(std::clamp(creaseAngleDegrees, 0.1, 89.9) * std::acos(-1.0) / 180.0);
    // glTF often duplicates vertices at normals/material seams. Weld for display
    // adjacency only; the actual imported geometry remains byte-for-byte intact.
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double tolerance = 1.0e-7;
    gp_Pnt origin;
    if (!bounds.IsVoid()) {
        origin = bounds.CornerMin();
        tolerance = std::max(tolerance, origin.Distance(bounds.CornerMax()) * 1.0e-9);
    }
    const auto vertexIndex = [&](const gp_Pnt& point) {
        const std::array<std::int64_t, 3> key{
            std::llround((point.X() - origin.X()) / tolerance),
            std::llround((point.Y() - origin.Y()) / tolerance),
            std::llround((point.Z() - origin.Z()) / tolerance)};
        const auto found = vertices.find(key);
        if (found != vertices.end()) return found->second;
        const int index = static_cast<int>(points.size());
        vertices.emplace(key, index);
        points.append(point);
        return index;
    };
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int f = 1; f <= faces.Extent(); ++f) {
        const TopoDS_Face face = TopoDS::Face(faces(f));
        if (!BRep_Tool::Surface(face).IsNull()) continue; // true BREP uses OCCT feature boundaries
        TopLoc_Location location;
        const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(face, location);
        if (mesh.IsNull()) continue;
        QVector<int> indices(mesh->NbNodes() + 1);
        for (int i = 1; i <= mesh->NbNodes(); ++i)
            indices[i] = vertexIndex(mesh->Node(i).Transformed(location.Transformation()));
        for (int i = 1; i <= mesh->NbTriangles(); ++i) {
            int a, b, c;
            mesh->Triangle(i).Get(a, b, c);
            const int ids[] = {indices[a], indices[b], indices[c]};
            gp_Vec normal = gp_Vec(points[ids[0]], points[ids[1]]).Crossed(
                gp_Vec(points[ids[0]], points[ids[2]]));
            if (normal.SquareMagnitude() < 1.0e-24) continue;
            normal.Normalize();
            for (int side = 0; side < 3; ++side) {
                const int low = std::min(ids[side], ids[(side + 1) % 3]);
                const int high = std::max(ids[side], ids[(side + 1) % 3]);
                const auto key = (static_cast<std::uint64_t>(low) << 32) | static_cast<std::uint32_t>(high);
                auto found = edges.find(key);
                if (found == edges.end()) edges.emplace(key, Edge{low, high, normal});
                else {
                    ++found->second.count;
                    found->second.crease |= std::abs(found->second.normal.Dot(normal)) < cosine;
                }
            }
        }
    }
    for (const auto& item : edges) {
        const Edge& edge = item.second;
        if (edge.count != 2 || edge.crease)
            result.append({points[edge.first], points[edge.second]});
    }
    return result;
}

Handle(AIS_Shape) GeometryDisplayManager::createIfcPresentation(const FcIfcObject& object)
{
    if (!object.hasShape()) return {};
    Handle(AIS_ColoredShape) presentation = new IfcColoredPresentation(object.shape());
    const FcIfcAppearance& base = object.appearance();
    presentation->SetColor(Quantity_Color(base.red, base.green, base.blue, Quantity_TOC_RGB));
    presentation->SetTransparency(1.0 - base.alpha);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(object.shape(), TopAbs_FACE, faces);
    bool hasAnalyticFaces = false;
    for (int i = 1; i <= faces.Extent(); ++i)
        hasAnalyticFaces |= !BRep_Tool::Surface(TopoDS::Face(faces(i))).IsNull();
    for (auto it = object.faceAppearances().cbegin(); it != object.faceAppearances().cend(); ++it) {
        const FcIfcAppearance& appearance = it.value();
        presentation->SetCustomColor(faces(it.key()), Quantity_Color(
            appearance.red, appearance.green, appearance.blue, Quantity_TOC_RGB));
        presentation->SetCustomTransparency(faces(it.key()), 1.0 - appearance.alpha);
    }
    presentation->Attributes()->SetFaceBoundaryDraw(hasAnalyticFaces);
    presentation->Attributes()->SetFaceBoundaryUpperContinuity(GeomAbs_C0);
    presentation->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(
        Quantity_Color(0.12, 0.14, 0.17, Quantity_TOC_RGB), Aspect_TOL_SOLID, 1.1));
    presentation->Attributes()->SetIsoOnTriangulation(false);
    presentation->SetDisplayMode(1);
    return presentation;
}

GeometryDisplayManager::GeometryDisplayManager(
    const Handle(AIS_InteractiveContext)& context)
    : m_context(context)
{
}

GeometryDisplayManager::~GeometryDisplayManager()
{
    // The interactive context keeps its own handle to a displayed
    // AIS_Manipulator.  Detach it while the OCCT view/context are still alive;
    // otherwise their selection owners may outlive this manager during Win32
    // widget teardown. The window may already be closed, so do not redraw.
    detachManipulator(false);
}

bool GeometryDisplayManager::displayObject(
    const std::shared_ptr<FcGeometryObject>& object)
{
    if (!object) {
        return false;
    }

    if (object->geometryKind() == FcGeometryKind::BackgroundImage) {
        QString texturePath = object->geometryParameters()
                                  .value(QStringLiteral("resourcePath")).toString();
        if (!QFileInfo::exists(texturePath)) {
            const QByteArray embedded = QByteArray::fromBase64(
                object->geometryParameters().value(
                    QStringLiteral("embeddedImageBase64")).toString().toLatin1());
            if (!embedded.isEmpty()) {
                const QString cacheDirectory = QDir(
                    QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                    .filePath(QStringLiteral("background-images"));
                QDir().mkpath(cacheDirectory);
                const QString extension = object->geometryParameters()
                                              .value(QStringLiteral("extension"),
                                                     QStringLiteral("png")).toString();
                QString cacheName = object->id();
                cacheName.remove(QLatin1Char('{')).remove(QLatin1Char('}'));
                texturePath = QDir(cacheDirectory).filePath(
                    cacheName + QLatin1Char('.') + extension);
                QFile cacheFile(texturePath);
                if (cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    cacheFile.write(embedded);
                    cacheFile.close();
                }
            }
        }
        if (!texturePath.isEmpty() && QFileInfo::exists(texturePath) &&
            !m_context.IsNull() && !object->shape().IsNull()) {
            removeObjectWithoutUpdate(object->id());
            Handle(AIS_TexturedShape) textured = new AIS_TexturedShape(object->shape());
            textured->SetTextureFileName(
                TCollection_AsciiString(texturePath.toUtf8().constData()));
            textured->SetTextureMapOn();
            textured->SetTextureRepeat(false);
            textured->DisableTextureModulate();
            Handle(AIS_Shape) presentation = textured;
            const double transparency = object->geometryParameters()
                                            .value(QStringLiteral("opacity"), 0.65)
                                            .toDouble();
            m_context->SetTransparency(presentation,
                                       std::clamp(1.0 - transparency, 0.0, 0.95),
                                       Standard_False);
            m_context->SetDisplayMode(presentation, 3, Standard_False);
            m_context->Display(presentation, Standard_False);
            m_presentations[object->id()].push_back(presentation);
            m_objectIdsByPresentation.insert(presentation.get(), object->id());
            m_context->UpdateCurrentViewer();
            return true;
        }
    }
    const QVariantMap& parameters = object->geometryParameters();
    GeometryDisplayStyle style;
    // An opening is a cutter, not a solid filling the hole in its host.
    if (BuildingGeometryService::isOpeningKind(object->geometryKind())) {
        style.wireframe = true;
        style.red = 0.9; style.green = 0.35; style.blue = 0.1;
    }
    if (parameters.contains(QStringLiteral("displayColorRed")) &&
        parameters.contains(QStringLiteral("displayColorGreen")) &&
        parameters.contains(QStringLiteral("displayColorBlue"))) {
        style.red = std::clamp(parameters.value(QStringLiteral("displayColorRed")).toDouble(),
                               0.0, 1.0);
        style.green = std::clamp(parameters.value(QStringLiteral("displayColorGreen")).toDouble(),
                                 0.0, 1.0);
        style.blue = std::clamp(parameters.value(QStringLiteral("displayColorBlue")).toDouble(),
                                0.0, 1.0);
        style.transparency = 1.0 - std::clamp(
            parameters.value(QStringLiteral("displayOpacity"), 1.0).toDouble(), 0.0, 1.0);
    }
    const QColor overrideColor(parameters.value(QStringLiteral("displayColor")).toString());
    if (overrideColor.isValid()) {
        const Quantity_Color color(overrideColor.redF(), overrideColor.greenF(),
                                   overrideColor.blueF(), Quantity_TOC_sRGB);
        style.red = color.Red(); style.green = color.Green(); style.blue = color.Blue();
    }
    style.outline = parameters.value(QStringLiteral("displayOutline"), false).toBool();
    const bool displayed = displayShapeWithoutUpdate(object->id(), object->shape(), style, true);
    if (displayed && !m_context.IsNull()) m_context->UpdateCurrentViewer();
    return displayed;
}

bool GeometryDisplayManager::displayShape(const QString& objectId,
                                          const TopoDS_Shape& shape)
{
    const bool displayed = displayShapeWithoutUpdate(
        objectId, shape, GeometryDisplayStyle{}, true);
    if (displayed && !m_context.IsNull()) {
        m_context->UpdateCurrentViewer();
    }
    return displayed;
}

bool GeometryDisplayManager::appendShape(const QString& objectId,
                                         const TopoDS_Shape& shape,
                                         const GeometryDisplayStyle& style)
{
    return displayShapeWithoutUpdate(objectId, shape, style, false);
}

bool GeometryDisplayManager::displayIfcModel(
    const std::shared_ptr<FcIfcObject>& rootObject)
{
    if (m_context.IsNull() || !rootObject) {
        return false;
    }

    QStringList displayedIds;
    bool failed = false;
    const std::function<void(const std::shared_ptr<FcIfcObject>&)> displayObjectTree =
        [&](const std::shared_ptr<FcIfcObject>& object) {
            if (!object || failed) {
                return;
            }
            if (object->hasShape()) {
                const Handle(AIS_Shape) presentation = createIfcPresentation(*object);
                if (presentation.IsNull()) {
                    failed = true;
                    return;
                }
                removeObjectWithoutUpdate(object->id());
                m_context->Display(presentation, Standard_False);
                m_presentations[object->id()].append(presentation);
                m_objectIdsByPresentation.insert(presentation.get(), object->id());
                displayedIds.append(object->id());
            }
            for (const FcObject::Ptr& child : object->children()) {
                displayObjectTree(std::dynamic_pointer_cast<FcIfcObject>(child));
            }
        };
    displayObjectTree(rootObject);

    if (failed || displayedIds.isEmpty()) {
        for (const QString& objectId : displayedIds) {
            removeObjectWithoutUpdate(objectId);
        }
        m_context->UpdateCurrentViewer();
        return false;
    }

    m_context->UpdateCurrentViewer();
    return true;
}

bool GeometryDisplayManager::hideObject(const QString& objectId)
{
    const auto iterator = m_presentations.constFind(objectId);
    if (m_context.IsNull() || iterator == m_presentations.cend()) {
        return false;
    }
    for (const Handle(AIS_Shape)& presentation : iterator.value()) {
        m_context->Erase(presentation, Standard_False);
    }
    m_context->UpdateCurrentViewer();
    return true;
}

bool GeometryDisplayManager::showObject(const QString& objectId)
{
    const auto iterator = m_presentations.constFind(objectId);
    if (m_context.IsNull() || iterator == m_presentations.cend()) {
        return false;
    }
    for (const Handle(AIS_Shape)& presentation : iterator.value()) {
        m_context->Display(presentation, Standard_False);
    }
    m_context->UpdateCurrentViewer();
    return true;
}

bool GeometryDisplayManager::selectObject(const QString& objectId)
{
    return selectObjects({objectId});
}

bool GeometryDisplayManager::selectObjects(const QStringList& objectIds)
{
    if (m_context.IsNull()) return false;
    m_context->ClearSelected(Standard_False);
    m_selectedObjectIds.clear();
    for (const QString& objectId : objectIds) {
        const auto iterator = m_presentations.constFind(objectId);
        if (iterator == m_presentations.cend()) continue;
        bool selectedAny = false;
        for (const Handle(AIS_Shape)& presentation : iterator.value()) {
            if (m_context->IsDisplayed(presentation)) {
                m_context->AddOrRemoveSelected(presentation, Standard_False);
                selectedAny = true;
            }
        }
        if (selectedAny) m_selectedObjectIds.insert(objectId);
    }
    m_context->UpdateCurrentViewer();
    return !m_selectedObjectIds.isEmpty();
}

QString GeometryDisplayManager::selectAt(int x,
                                         int y,
                                         const Handle(V3d_View)& view,
                                         bool toggle)
{
    if (m_context.IsNull() || view.IsNull()) {
        clearSelection();
        return {};
    }

    m_context->MoveTo(x, y, view, Standard_False);
    if (!m_context->HasDetected()) {
        if (!toggle) clearSelection();
        return {};
    }

    const QString objectId = objectIdForPresentation(m_context->DetectedInteractive());
    if (objectId.isEmpty()) {
        if (!toggle) clearSelection();
        return {};
    }
    QStringList selection = selectedObjectIds();
    if (toggle) {
        if (selection.contains(objectId)) selection.removeAll(objectId);
        else selection.append(objectId);
    } else {
        selection = {objectId};
    }
    selectObjects(selection);
    return objectId;
}

QStringList GeometryDisplayManager::selectRectangle(
    int x1, int y1, int x2, int y2, const Handle(V3d_View)& view, bool toggle)
{
    if (m_context.IsNull() || view.IsNull()) return {};
    const Graphic3d_Vec2i minimum(std::min(x1, x2), std::min(y1, y2));
    const Graphic3d_Vec2i maximum(std::max(x1, x2), std::max(y1, y2));
    m_context->SelectRectangle(
        minimum, maximum, view,
        toggle ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
    m_selectedObjectIds.clear();
    for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
        const QString objectId = objectIdForPresentation(m_context->SelectedInteractive());
        if (!objectId.isEmpty()) m_selectedObjectIds.insert(objectId);
    }
    m_context->UpdateCurrentViewer();
    return selectedObjectIds();
}

void GeometryDisplayManager::clearSelection()
{
    m_selectedObjectIds.clear();
    if (!m_context.IsNull()) {
        m_context->ClearSelected(Standard_True);
    }
}

QString GeometryDisplayManager::selectedObjectId() const
{
    return m_selectedObjectIds.size() == 1
               ? *m_selectedObjectIds.cbegin()
               : QString{};
}

QStringList GeometryDisplayManager::selectedObjectIds() const
{
    QStringList result = m_selectedObjectIds.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

Handle(AIS_InteractiveObject) GeometryDisplayManager::presentationForObject(
    const QString& objectId) const
{
    const auto iterator = m_presentations.constFind(objectId);
    if (iterator == m_presentations.cend()) {
        return {};
    }
    return iterator.value().isEmpty()
               ? Handle(AIS_InteractiveObject){}
               : Handle(AIS_InteractiveObject)(iterator.value().constFirst());
}

QVector<Handle(AIS_InteractiveObject)>
GeometryDisplayManager::presentationsForObject(const QString& objectId) const
{
    QVector<Handle(AIS_InteractiveObject)> result;
    const auto iterator = m_presentations.constFind(objectId);
    if (iterator == m_presentations.cend()) return result;
    result.reserve(iterator.value().size());
    for (const Handle(AIS_Shape)& presentation : iterator.value()) {
        result.push_back(presentation);
    }
    return result;
}

QString GeometryDisplayManager::objectIdForPresentation(
    const Handle(AIS_InteractiveObject)& presentation) const
{
    if (presentation.IsNull()) {
        return {};
    }
    return m_objectIdsByPresentation.value(presentation.get());
}

bool GeometryDisplayManager::removeObject(const QString& objectId)
{
    const bool removed = removeObjectWithoutUpdate(objectId);
    if (removed && !m_context.IsNull()) {
        m_context->UpdateCurrentViewer();
    }
    return removed;
}

int GeometryDisplayManager::removeIfcModel(
    const std::shared_ptr<FcIfcObject>& rootObject)
{
    if (!rootObject) {
        return 0;
    }

    int removedCount = 0;
    const std::function<void(const std::shared_ptr<FcIfcObject>&)> removeObjectTree =
        [&](const std::shared_ptr<FcIfcObject>& object) {
            if (!object) {
                return;
            }
            if (removeObjectWithoutUpdate(object->id())) {
                ++removedCount;
            }
            for (const FcObject::Ptr& child : object->children()) {
                removeObjectTree(std::dynamic_pointer_cast<FcIfcObject>(child));
            }
        };
    removeObjectTree(rootObject);
    if (removedCount > 0 && !m_context.IsNull()) {
        m_context->UpdateCurrentViewer();
    }
    return removedCount;
}

bool GeometryDisplayManager::displayShapeWithoutUpdate(
    const QString& objectId,
    const TopoDS_Shape& shape,
    const GeometryDisplayStyle& style,
    bool replaceExisting)
{
    if (m_context.IsNull() || objectId.isEmpty() || shape.IsNull()) {
        return false;
    }

    if (replaceExisting) removeObjectWithoutUpdate(objectId);
    Handle(AIS_Shape) presentation = new AIS_Shape(shape);
    presentation->Attributes()->SetFaceBoundaryDraw(style.outline);
    presentation->Attributes()->SetFaceBoundaryUpperContinuity(GeomAbs_C0);
    presentation->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(
        Quantity_Color(0.12, 0.14, 0.17, Quantity_TOC_RGB), Aspect_TOL_SOLID, 1.1));
    m_context->SetColor(
        presentation,
        Quantity_Color(style.red, style.green, style.blue, Quantity_TOC_RGB),
        Standard_False);
    if (style.transparency > 0.0) {
        m_context->SetTransparency(
            presentation, std::clamp(style.transparency, 0.0, 0.95), Standard_False);
    }
    m_context->SetDisplayMode(presentation, style.wireframe ? 0 : 1, Standard_False);
    if (style.visible) m_context->Display(presentation, Standard_False);
    m_presentations[objectId].push_back(presentation);
    m_objectIdsByPresentation.insert(presentation.get(), objectId);
    return true;
}

bool GeometryDisplayManager::removeObjectWithoutUpdate(const QString& objectId)
{
    const auto iterator = m_presentations.find(objectId);
    if (iterator == m_presentations.end()) {
        return false;
    }
    if (m_manipulatorObjectIds.contains(objectId)) detachManipulator();
    if (!m_context.IsNull()) {
        for (const Handle(AIS_Shape)& presentation : iterator.value()) {
            m_context->Remove(presentation, Standard_False);
        }
    }
    m_selectedObjectIds.remove(objectId);
    for (const Handle(AIS_Shape)& presentation : iterator.value()) {
        m_objectIdsByPresentation.remove(presentation.get());
    }
    m_presentations.erase(iterator);
    return true;
}

void GeometryDisplayManager::clear(bool updateViewer)
{
    detachManipulator(false);
    m_selectedObjectIds.clear();
    if (!m_context.IsNull()) {
        m_context->ClearSelected(Standard_False);
        m_context->RemoveAll(updateViewer ? Standard_True : Standard_False);
    }
    m_objectIdsByPresentation.clear();
    m_presentations.clear();
}

int GeometryDisplayManager::displayedObjectCount() const
{
    int count = 0;
    for (auto iterator = m_presentations.cbegin(); iterator != m_presentations.cend();
         ++iterator) {
        count += iterator.value().size();
    }
    return count;
}

int GeometryDisplayManager::reverseMappingCount() const
{
    return m_objectIdsByPresentation.size();
}

bool GeometryDisplayManager::contains(const QString& objectId) const
{
    const auto iterator = m_presentations.constFind(objectId);
    return iterator != m_presentations.cend() && !iterator.value().isEmpty();
}

bool GeometryDisplayManager::isObjectVisible(const QString& objectId) const
{
    const auto iterator = m_presentations.constFind(objectId);
    if (m_context.IsNull() || iterator == m_presentations.cend() ||
        iterator.value().isEmpty()) return false;
    return std::all_of(iterator.value().cbegin(), iterator.value().cend(),
                       [this](const Handle(AIS_Shape)& presentation) {
                           return m_context->IsDisplayed(presentation);
                       });
}

bool GeometryDisplayManager::fitSelection(const Handle(V3d_View)& view) const
{
    if (view.IsNull() || m_selectedObjectIds.isEmpty()) return false;
    Bnd_Box bounds;
    for (const QString& objectId : m_selectedObjectIds) {
        const auto iterator = m_presentations.constFind(objectId);
        if (iterator == m_presentations.cend()) continue;
        for (const Handle(AIS_Shape)& presentation : iterator.value()) {
            if (!presentation.IsNull() && m_context->IsDisplayed(presentation)) {
                bounds.Add(presentation->BoundingBox());
            }
        }
    }
    if (bounds.IsVoid()) return false;
    view->FitAll(bounds, 0.08, Standard_True);
    view->ZFitAll();
    view->Redraw();
    return true;
}

bool GeometryDisplayManager::attachManipulator(const QStringList& objectIds)
{
    detachManipulator();
    if (m_context.IsNull()) return false;
    Handle(AIS_ManipulatorObjectSequence) objects = new AIS_ManipulatorObjectSequence();
    QStringList attachedIds;
    for (const QString& objectId : objectIds) {
        const auto iterator = m_presentations.constFind(objectId);
        if (iterator == m_presentations.cend() || iterator.value().size() != 1) continue;
        const Handle(AIS_Shape)& presentation = iterator.value().constFirst();
        if (presentation.IsNull() || !m_context->IsDisplayed(presentation)) continue;
        objects->Append(presentation);
        attachedIds.append(objectId);
    }
    if (objects->IsEmpty()) return false;
    try {
        m_manipulator = new AIS_Manipulator();
        m_manipulator->SetModeActivationOnDetection(Standard_True);
        AIS_Manipulator::OptionsForAttach options;
        options.SetAdjustPosition(Standard_True)
               .SetAdjustSize(Standard_False)
               // Attach() enables every manipulator mode when this is true.
               // Enable only the three supported modes once below; duplicate
               // activation creates duplicate sensitive owners in OCCT.
               .SetEnableModes(Standard_False);
        m_manipulator->Attach(objects, options);
        m_manipulator->EnableMode(AIS_MM_Translation);
        m_manipulator->EnableMode(AIS_MM_Rotation);
        m_manipulator->EnableMode(AIS_MM_Scaling);
        m_manipulatorObjectIds = attachedIds;
        m_context->UpdateCurrentViewer();
        return true;
    } catch (const Standard_Failure&) {
        detachManipulator();
        return false;
    }
}

void GeometryDisplayManager::detachManipulator(bool updateViewer)
{
    if (!m_manipulator.IsNull()) {
        if (m_manipulator->HasActiveTransformation()) {
            m_manipulator->StopTransform(Standard_False);
        }
        // AIS_Manipulator owns transient selection entities.  Clear detection
        // and selection before detaching so OCCT never releases their owners
        // later during view/context teardown (which is unsafe on Win32).
        if (!m_context.IsNull()) {
            m_context->ClearDetected(Standard_False);
            m_context->ClearSelected(Standard_False);
        }
        m_manipulator->DeactivateCurrentMode();
        m_manipulator->Detach();
        m_manipulator.Nullify();
        if (updateViewer && !m_context.IsNull()) m_context->UpdateCurrentViewer();
    }
    m_manipulatorObjectIds.clear();
}

bool GeometryDisplayManager::updateManipulatorDetection(
    int x, int y, const Handle(V3d_View)& view)
{
    if (m_context.IsNull() || m_manipulator.IsNull() || view.IsNull()) return false;
    m_context->MoveTo(x, y, view, Standard_True);
    return m_manipulator->HasActiveMode();
}

bool GeometryDisplayManager::beginManipulatorDrag(
    int x, int y, const Handle(V3d_View)& view)
{
    if (!updateManipulatorDetection(x, y, view) || !m_manipulator->HasActiveMode()) {
        return false;
    }
    m_manipulator->StartTransform(x, y, view);
    return m_manipulator->HasActiveTransformation();
}

bool GeometryDisplayManager::updateManipulatorDrag(
    int x, int y, const Handle(V3d_View)& view)
{
    if (m_manipulator.IsNull() || !m_manipulator->HasActiveTransformation() ||
        view.IsNull()) return false;
    m_manipulator->Transform(x, y, view);
    view->Redraw();
    return true;
}

QStringList GeometryDisplayManager::finishManipulatorDrag(bool apply)
{
    if (m_manipulator.IsNull() || !m_manipulator->HasActiveTransformation()) return {};
    m_manipulator->StopTransform(apply ? Standard_True : Standard_False);
    if (!m_context.IsNull()) m_context->UpdateCurrentViewer();
    return apply ? m_manipulatorObjectIds : QStringList{};
}

bool GeometryDisplayManager::isManipulatorAttached() const
{
    return !m_manipulator.IsNull() && m_manipulator->IsAttached();
}

gp_Trsf GeometryDisplayManager::objectLocalTransformation(const QString& objectId) const
{
    const auto iterator = m_presentations.constFind(objectId);
    if (iterator == m_presentations.cend() || iterator.value().isEmpty() ||
        iterator.value().constFirst().IsNull()) return gp_Trsf();
    return iterator.value().constFirst()->LocalTransformation();
}

void GeometryDisplayManager::updateViewer()
{
    if (!m_context.IsNull()) m_context->UpdateCurrentViewer();
}
