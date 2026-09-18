#include "visualization/GeometryDisplayManager.h"

#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"

#include <AIS_TexturedShape.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Vec2.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

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
    if (parameters.contains(QStringLiteral("displayColorRed")) &&
        parameters.contains(QStringLiteral("displayColorGreen")) &&
        parameters.contains(QStringLiteral("displayColorBlue"))) {
        GeometryDisplayStyle style;
        style.red = std::clamp(parameters.value(QStringLiteral("displayColorRed")).toDouble(),
                               0.0, 1.0);
        style.green = std::clamp(parameters.value(QStringLiteral("displayColorGreen")).toDouble(),
                                 0.0, 1.0);
        style.blue = std::clamp(parameters.value(QStringLiteral("displayColorBlue")).toDouble(),
                                0.0, 1.0);
        style.transparency = 1.0 - std::clamp(
            parameters.value(QStringLiteral("displayOpacity"), 1.0).toDouble(), 0.0, 1.0);
        const bool displayed = displayShapeWithoutUpdate(object->id(), object->shape(),
                                                         style, true);
        if (displayed && !m_context.IsNull()) m_context->UpdateCurrentViewer();
        return displayed;
    }
    return displayShape(object->id(), object->shape());
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
                if (!displayShapeWithoutUpdate(
                        object->id(), object->shape(), GeometryDisplayStyle{}, true)) {
                    failed = true;
                    return;
                }
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
