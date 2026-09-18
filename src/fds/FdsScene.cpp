#include "fds/FdsScene.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"

#include <QLocale>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
using ObjectList = std::vector<FcObject::Ptr>;

void collectObjects(const FcObject::Ptr& object, ObjectList& objects)
{
    if (!object) return;
    objects.push_back(object);
    for (const FcObject::Ptr& child : object->children()) {
        collectObjects(child, objects);
    }
}

const FcFdsParameter* parameter(const FcFdsNamelist& object,
                                const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    for (const FcFdsParameter& candidate : object.parameters()) {
        if (candidate.key == normalized) return &candidate;
    }
    return nullptr;
}

QString value(const FcFdsNamelist& object, const QString& key)
{
    const FcFdsParameter* candidate = parameter(object, key);
    return candidate ? candidate->value.trimmed() : QString{};
}

bool number(const QString& text, double& result)
{
    bool ok = false;
    result = QLocale::c().toDouble(text.trimmed(), &ok);
    return ok && std::isfinite(result);
}

std::vector<double> numbers(const QString& text, int expected)
{
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() != expected) return {};
    std::vector<double> result;
    result.reserve(static_cast<std::size_t>(expected));
    for (const QString& part : parts) {
        double parsed = 0.0;
        if (!number(part, parsed)) return {};
        result.push_back(parsed);
    }
    return result;
}

bool boundsFromText(const QString& text, FcFdsBounds& bounds)
{
    const std::vector<double> values = numbers(text, 6);
    if (values.size() != 6) return false;
    bounds = {values[0], values[1], values[2], values[3], values[4], values[5]};
    return bounds.xMin <= bounds.xMax && bounds.yMin <= bounds.yMax &&
           bounds.zMin <= bounds.zMax;
}

FdsScenePoint center(const FcFdsBounds& bounds)
{
    return {(bounds.xMin + bounds.xMax) * 0.5,
            (bounds.yMin + bounds.yMax) * 0.5,
            (bounds.zMin + bounds.zMax) * 0.5};
}

FdsSceneColor namedColor(const QString& name, bool* recognized = nullptr)
{
    static const QHash<QString, FdsSceneColor> colors = {
        {QStringLiteral("BLACK"), {0.08, 0.08, 0.08, 1.0}},
        {QStringLiteral("WHITE"), {0.95, 0.95, 0.95, 1.0}},
        {QStringLiteral("RED"), {0.90, 0.12, 0.08, 1.0}},
        {QStringLiteral("GREEN"), {0.10, 0.72, 0.22, 1.0}},
        {QStringLiteral("BLUE"), {0.12, 0.32, 0.92, 1.0}},
        {QStringLiteral("CYAN"), {0.05, 0.78, 0.82, 1.0}},
        {QStringLiteral("YELLOW"), {0.95, 0.82, 0.10, 1.0}},
        {QStringLiteral("ORANGE"), {0.95, 0.43, 0.06, 1.0}},
        {QStringLiteral("PURPLE"), {0.58, 0.18, 0.78, 1.0}},
        {QStringLiteral("GRAY"), {0.52, 0.54, 0.58, 1.0}},
        {QStringLiteral("GREY"), {0.52, 0.54, 0.58, 1.0}}
    };
    const auto iterator = colors.constFind(name.trimmed().toUpper());
    if (recognized) *recognized = iterator != colors.cend();
    return iterator == colors.cend() ? FdsSceneColor{} : iterator.value();
}

FdsSceneColor typeColor(const QString& keyword)
{
    const QString normalized = keyword.toUpper();
    if (normalized == QStringLiteral("MESH")) return {0.40, 0.56, 0.82, 1.0};
    if (normalized == QStringLiteral("OBST")) return {0.68, 0.70, 0.73, 1.0};
    if (normalized == QStringLiteral("VENT")) return {0.20, 0.72, 0.34, 0.90};
    if (normalized == QStringLiteral("DEVC")) return {0.94, 0.76, 0.10, 1.0};
    if (normalized == QStringLiteral("HVAC")) return {0.10, 0.72, 0.84, 1.0};
    if (normalized == QStringLiteral("INIT")) return {0.76, 0.25, 0.82, 0.65};
    if (normalized == QStringLiteral("SLCF")) return {0.25, 0.72, 0.95, 0.25};
    return {};
}

FdsSceneColor colorFor(const FcFdsNamelist& object,
                       const FcDocument& document,
                       const QString& keyword)
{
    const std::vector<double> rgb = numbers(value(object, QStringLiteral("RGB")), 3);
    if (rgb.size() == 3) {
        return {std::clamp(rgb[0] / 255.0, 0.0, 1.0),
                std::clamp(rgb[1] / 255.0, 0.0, 1.0),
                std::clamp(rgb[2] / 255.0, 0.0, 1.0), 1.0};
    }
    bool recognized = false;
    const FdsSceneColor direct = namedColor(value(object, QStringLiteral("COLOR")),
                                            &recognized);
    if (recognized) return direct;

    const FcFdsParameter* surface = parameter(object, QStringLiteral("SURF_ID"));
    if (surface) {
        for (const QString& targetId : surface->targetObjectIds) {
            const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
                document.findObject(targetId));
            if (target) return colorFor(*target, document, target->keyword());
        }
    }
    return typeColor(keyword);
}

void includeBounds(FdsScene& scene, const FcFdsBounds& bounds)
{
    if (!scene.hasDomainBounds) {
        scene.domainBounds = bounds;
        scene.hasDomainBounds = true;
        return;
    }
    scene.domainBounds.xMin = std::min(scene.domainBounds.xMin, bounds.xMin);
    scene.domainBounds.xMax = std::max(scene.domainBounds.xMax, bounds.xMax);
    scene.domainBounds.yMin = std::min(scene.domainBounds.yMin, bounds.yMin);
    scene.domainBounds.yMax = std::max(scene.domainBounds.yMax, bounds.yMax);
    scene.domainBounds.zMin = std::min(scene.domainBounds.zMin, bounds.zMin);
    scene.domainBounds.zMax = std::max(scene.domainBounds.zMax, bounds.zMax);
}

bool inheritedVisibility(const FcObject& object)
{
    for (const FcObject* current = &object; current; current = current->parent()) {
        if (!current->isVisible()) return false;
    }
    return true;
}

const FcFdsNamelist* referencedNamelist(const FcFdsNamelist& object,
                                        const QString& key,
                                        const FcDocument& document)
{
    const FcFdsParameter* reference = parameter(object, key);
    if (!reference) return nullptr;
    for (const QString& targetId : reference->targetObjectIds) {
        const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
            document.findObject(targetId));
        if (target) return target.get();
    }
    return nullptr;
}

int integerValue(const FcFdsNamelist& object, const char* key, int fallback)
{
    double parsed = 0.0;
    return number(value(object, QString::fromLatin1(key)), parsed)
               ? static_cast<int>(std::llround(parsed))
               : fallback;
}

double realValue(const FcFdsNamelist& object, const char* key, double fallback)
{
    double parsed = 0.0;
    return number(value(object, QString::fromLatin1(key)), parsed) ? parsed : fallback;
}

std::vector<std::array<double, 3>> multiplierOffsets(
    const FcFdsNamelist& object, const FcDocument& document, FdsScene& scene)
{
    const FcFdsNamelist* multiplier = referencedNamelist(
        object, QStringLiteral("MULT_ID"), document);
    if (!multiplier) return {{{0.0, 0.0, 0.0}}};

    const int iLower = integerValue(*multiplier, "I_LOWER", 0);
    const int iUpper = integerValue(*multiplier, "I_UPPER", 0);
    const int jLower = integerValue(*multiplier, "J_LOWER", 0);
    const int jUpper = integerValue(*multiplier, "J_UPPER", 0);
    const int kLower = integerValue(*multiplier, "K_LOWER", 0);
    const int kUpper = integerValue(*multiplier, "K_UPPER", 0);
    if (iUpper < iLower || jUpper < jLower || kUpper < kLower) {
        scene.warnings.append(QStringLiteral("MULT %1 has reversed index limits.")
                                  .arg(multiplier->name()));
        return {{{0.0, 0.0, 0.0}}};
    }
    const qint64 count = static_cast<qint64>(iUpper - iLower + 1) *
                         static_cast<qint64>(jUpper - jLower + 1) *
                         static_cast<qint64>(kUpper - kLower + 1);
    constexpr qint64 maximumInstances = 4096;
    if (count > maximumInstances) {
        scene.warnings.append(QStringLiteral("MULT %1 expands to %2 instances; capped at %3.")
                                  .arg(multiplier->name()).arg(count).arg(maximumInstances));
    }
    const double dx = realValue(*multiplier, "DX", 0.0);
    const double dy = realValue(*multiplier, "DY", 0.0);
    const double dz = realValue(*multiplier, "DZ", 0.0);
    std::vector<std::array<double, 3>> offsets;
    offsets.reserve(static_cast<std::size_t>(std::min(count, maximumInstances)));
    for (int k = kLower; k <= kUpper; ++k) {
        for (int j = jLower; j <= jUpper; ++j) {
            for (int i = iLower; i <= iUpper; ++i) {
                if (static_cast<qint64>(offsets.size()) >= maximumInstances) return offsets;
                offsets.push_back({i * dx, j * dy, k * dz});
            }
        }
    }
    return offsets;
}

FcFdsBounds translated(const FcFdsBounds& source,
                       const std::array<double, 3>& offset)
{
    return {source.xMin + offset[0], source.xMax + offset[0],
            source.yMin + offset[1], source.yMax + offset[1],
            source.zMin + offset[2], source.zMax + offset[2]};
}

bool planeBounds(const FcFdsNamelist& object,
                 const FcFdsBounds& domain,
                 bool hasDomain,
                 FcFdsBounds& result)
{
    if (boundsFromText(value(object, QStringLiteral("XB")), result)) return true;
    if (!hasDomain) return false;

    const QString mb = value(object, QStringLiteral("MB")).toUpper();
    result = domain;
    if (mb == QStringLiteral("XMIN")) result.xMax = result.xMin;
    else if (mb == QStringLiteral("XMAX")) result.xMin = result.xMax;
    else if (mb == QStringLiteral("YMIN")) result.yMax = result.yMin;
    else if (mb == QStringLiteral("YMAX")) result.yMin = result.yMax;
    else if (mb == QStringLiteral("ZMIN")) result.zMax = result.zMin;
    else if (mb == QStringLiteral("ZMAX")) result.zMin = result.zMax;
    else {
        double plane = 0.0;
        if (number(value(object, QStringLiteral("PBX")), plane)) {
            result.xMin = result.xMax = plane;
        } else if (number(value(object, QStringLiteral("PBY")), plane)) {
            result.yMin = result.yMax = plane;
        } else if (number(value(object, QStringLiteral("PBZ")), plane)) {
            result.zMin = result.zMax = plane;
        } else {
            return false;
        }
    }
    return true;
}

void appendBoxInstances(const FcFdsNamelist& object,
                        const QString& keyword,
                        const FcFdsBounds& baseBounds,
                        const FcDocument& document,
                        FdsScene& scene,
                        bool wireframe)
{
    const auto offsets = multiplierOffsets(object, document, scene);
    int index = 0;
    for (const auto& offset : offsets) {
        const FcFdsBounds instanceBounds = translated(baseBounds, offset);
        FdsScenePrimitive primitive;
        primitive.kind = FdsScenePrimitiveKind::Box;
        primitive.objectId = object.id();
        primitive.instanceKey = QStringLiteral("%1:%2").arg(object.id()).arg(index++);
        primitive.keyword = keyword;
        primitive.name = object.name();
        primitive.bounds = instanceBounds;
        primitive.color = colorFor(object, document, keyword);
        primitive.wireframe = wireframe;
        primitive.visible = inheritedVisibility(object);
        scene.primitives.push_back(primitive);
        if (keyword == QStringLiteral("MESH")) includeBounds(scene, instanceBounds);
    }
}

QStringList referenceIds(const FcFdsNamelist& object, const QString& key)
{
    const FcFdsParameter* candidate = parameter(object, key);
    return candidate ? candidate->targetObjectIds : QStringList{};
}
}

int FdsScene::primitiveCount(const QString& keyword) const
{
    return static_cast<int>(std::count_if(
        primitives.cbegin(), primitives.cend(), [&keyword](const auto& primitive) {
            return primitive.keyword.compare(keyword, Qt::CaseInsensitive) == 0;
        }));
}

int FdsScene::primitiveCountForObject(const QString& objectId) const
{
    return static_cast<int>(std::count_if(
        primitives.cbegin(), primitives.cend(), [&objectId](const auto& primitive) {
            return primitive.objectId == objectId;
        }));
}

FdsScene FdsSceneBuilder::build(const FcProject& project)
{
    FdsScene scene;
    const FcDocument* document = project.document();
    if (!document) return scene;

    ObjectList objects;
    for (const auto& group : document->groups()) {
        collectObjects(std::static_pointer_cast<FcObject>(group), objects);
    }
    if (const FcScenario* scenario = project.activeScenario()) {
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [scenario](const FcObject::Ptr& object) {
            return object && scenario->disabledObjectIds.contains(object->id());
        }), objects.end());
    }

    // MESH is built first because MB/PB planes need the complete domain.
    for (const FcObject::Ptr& object : objects) {
        if (const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(object)) {
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Box;
            primitive.objectId = mesh->id();
            primitive.instanceKey = mesh->id() + QStringLiteral(":0");
            primitive.keyword = QStringLiteral("MESH");
            primitive.name = mesh->name();
            primitive.bounds = mesh->bounds();
            primitive.color = typeColor(primitive.keyword);
            primitive.wireframe = true;
            primitive.visible = inheritedVisibility(*mesh);
            scene.primitives.push_back(primitive);
            includeBounds(scene, primitive.bounds);
            continue;
        }
        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!namelist || namelist->keyword() != QStringLiteral("MESH")) continue;
        FcFdsBounds bounds;
        if (!boundsFromText(value(*namelist, QStringLiteral("XB")), bounds) ||
            !bounds.isVolume()) {
            scene.warnings.append(QStringLiteral("MESH %1 has no displayable XB.")
                                      .arg(namelist->name()));
            continue;
        }
        appendBoxInstances(*namelist, QStringLiteral("MESH"), bounds,
                           *document, scene, true);
    }

    QHash<QString, FdsScenePoint> hvacNodePositions;
    QHash<QString, FdsScenePoint> hvacDuctMidpoints;

    for (const FcObject::Ptr& object : objects) {
        if (const auto obstruction = std::dynamic_pointer_cast<FcFdsObstruction>(object)) {
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Box;
            primitive.objectId = obstruction->id();
            primitive.instanceKey = obstruction->id() + QStringLiteral(":0");
            primitive.keyword = QStringLiteral("OBST");
            primitive.name = obstruction->name();
            primitive.bounds = obstruction->bounds();
            primitive.color = typeColor(primitive.keyword);
            primitive.visible = inheritedVisibility(*obstruction);
            scene.primitives.push_back(primitive);
            continue;
        }
        if (const auto vent = std::dynamic_pointer_cast<FcFdsVent>(object)) {
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Plane;
            primitive.objectId = vent->id();
            primitive.instanceKey = vent->id() + QStringLiteral(":0");
            primitive.keyword = QStringLiteral("VENT");
            primitive.name = vent->name();
            primitive.bounds = vent->bounds();
            primitive.color = vent->surfaceId().compare(QStringLiteral("OPEN"), Qt::CaseInsensitive) == 0
                                  ? FdsSceneColor{0.18, 0.72, 0.94, 0.45}
                                  : typeColor(primitive.keyword);
            primitive.visible = inheritedVisibility(*vent);
            scene.primitives.push_back(primitive);
            continue;
        }

        const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!namelist || namelist->keyword() == QStringLiteral("MESH")) continue;
        const QString keyword = namelist->keyword();
        if (keyword == QStringLiteral("OBST")) {
            FcFdsBounds bounds;
            if (boundsFromText(value(*namelist, QStringLiteral("XB")), bounds) &&
                bounds.isVolume()) {
                appendBoxInstances(*namelist, keyword, bounds, *document, scene, false);
            } else {
                scene.warnings.append(QStringLiteral("OBST %1 has no displayable XB.")
                                          .arg(namelist->name()));
            }
        } else if (keyword == QStringLiteral("VENT")) {
            FcFdsBounds bounds;
            if (!planeBounds(*namelist, scene.domainBounds, scene.hasDomainBounds, bounds)) {
                scene.warnings.append(QStringLiteral("VENT %1 cannot be placed without XB or a mesh domain.")
                                          .arg(namelist->name()));
                continue;
            }
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Plane;
            primitive.objectId = namelist->id();
            primitive.instanceKey = namelist->id() + QStringLiteral(":0");
            primitive.keyword = keyword;
            primitive.name = namelist->name();
            primitive.bounds = bounds;
            primitive.color = colorFor(*namelist, *document, keyword);
            if (value(*namelist, QStringLiteral("SURF_ID")).compare(
                    QStringLiteral("OPEN"), Qt::CaseInsensitive) == 0) {
                primitive.color = {0.18, 0.72, 0.94, 0.42};
            }
            primitive.visible = inheritedVisibility(*namelist);
            scene.primitives.push_back(primitive);
        } else if (keyword == QStringLiteral("DEVC")) {
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Point;
            primitive.objectId = namelist->id();
            primitive.instanceKey = namelist->id() + QStringLiteral(":0");
            primitive.keyword = keyword;
            primitive.name = namelist->name();
            primitive.color = colorFor(*namelist, *document, keyword);
            primitive.visible = inheritedVisibility(*namelist);
            const std::vector<double> xyz = numbers(value(*namelist, QStringLiteral("XYZ")), 3);
            FcFdsBounds region;
            if (xyz.size() == 3) primitive.start = primitive.end = {xyz[0], xyz[1], xyz[2]};
            else if (boundsFromText(value(*namelist, QStringLiteral("XB")), region)) {
                primitive.start = primitive.end = center(region);
            } else {
                const QStringList nodes = referenceIds(*namelist, QStringLiteral("NODE_ID"));
                const QStringList ducts = referenceIds(*namelist, QStringLiteral("DUCT_ID"));
                if (!nodes.isEmpty() && hvacNodePositions.contains(nodes.constFirst())) {
                    primitive.start = primitive.end = hvacNodePositions.value(nodes.constFirst());
                } else if (!ducts.isEmpty() && hvacDuctMidpoints.contains(ducts.constFirst())) {
                    primitive.start = primitive.end = hvacDuctMidpoints.value(ducts.constFirst());
                } else if (nodes.isEmpty() && ducts.isEmpty()) {
                    scene.warnings.append(QStringLiteral("DEVC %1 has no resolvable position.")
                                              .arg(namelist->name()));
                }
                continue;
            }
            scene.primitives.push_back(primitive);
        } else if (keyword == QStringLiteral("INIT")) {
            FcFdsBounds bounds;
            const std::vector<double> xyz = numbers(value(*namelist, QStringLiteral("XYZ")), 3);
            FdsScenePrimitive primitive;
            primitive.objectId = namelist->id();
            primitive.instanceKey = namelist->id() + QStringLiteral(":0");
            primitive.keyword = keyword;
            primitive.name = namelist->name();
            primitive.color = colorFor(*namelist, *document, keyword);
            primitive.visible = inheritedVisibility(*namelist);
            if (boundsFromText(value(*namelist, QStringLiteral("XB")), bounds)) {
                primitive.kind = bounds.isVolume() ? FdsScenePrimitiveKind::Box
                                                   : FdsScenePrimitiveKind::Plane;
                primitive.bounds = bounds;
            } else if (xyz.size() == 3) {
                primitive.kind = FdsScenePrimitiveKind::Point;
                primitive.start = primitive.end = {xyz[0], xyz[1], xyz[2]};
            } else continue;
            scene.primitives.push_back(primitive);
        }
    }

    // Resolve HVAC nodes from explicit XYZ or their referenced vents.
    for (const FcObject::Ptr& object : objects) {
        const auto hvac = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!hvac || hvac->keyword() != QStringLiteral("HVAC") ||
            value(*hvac, QStringLiteral("TYPE_ID")).compare(
                QStringLiteral("NODE"), Qt::CaseInsensitive) != 0) continue;
        const std::vector<double> xyz = numbers(value(*hvac, QStringLiteral("XYZ")), 3);
        if (xyz.size() == 3) {
            hvacNodePositions.insert(hvac->id(), {xyz[0], xyz[1], xyz[2]});
            continue;
        }
        const QStringList vents = referenceIds(*hvac, QStringLiteral("VENT_ID"));
        if (vents.isEmpty()) continue;
        const auto match = std::find_if(scene.primitives.cbegin(), scene.primitives.cend(),
            [&vents](const FdsScenePrimitive& primitive) {
                return primitive.objectId == vents.constFirst();
            });
        if (match != scene.primitives.cend()) hvacNodePositions.insert(hvac->id(), center(match->bounds));
    }

    // Ducts are deliberately built after all nodes, independent of object order.
    for (const FcObject::Ptr& object : objects) {
        const auto hvac = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!hvac || hvac->keyword() != QStringLiteral("HVAC") ||
            value(*hvac, QStringLiteral("TYPE_ID")).compare(
                QStringLiteral("DUCT"), Qt::CaseInsensitive) != 0) continue;
        const QStringList nodes = referenceIds(*hvac, QStringLiteral("NODE_ID"));
        if (nodes.size() < 2 || !hvacNodePositions.contains(nodes[0]) ||
            !hvacNodePositions.contains(nodes[1])) {
            scene.warnings.append(QStringLiteral("HVAC duct %1 has unresolved nodes.")
                                      .arg(hvac->name()));
            continue;
        }
        FdsScenePrimitive primitive;
        primitive.kind = FdsScenePrimitiveKind::Line;
        primitive.objectId = hvac->id();
        primitive.instanceKey = hvac->id() + QStringLiteral(":0");
        primitive.keyword = QStringLiteral("HVAC");
        primitive.name = hvac->name();
        primitive.start = hvacNodePositions.value(nodes[0]);
        primitive.end = hvacNodePositions.value(nodes[1]);
        primitive.color = typeColor(QStringLiteral("HVAC"));
        primitive.visible = inheritedVisibility(*hvac);
        scene.primitives.push_back(primitive);
        hvacDuctMidpoints.insert(hvac->id(),
            {(primitive.start.x + primitive.end.x) * 0.5,
             (primitive.start.y + primitive.end.y) * 0.5,
             (primitive.start.z + primitive.end.z) * 0.5});
    }

    // HVAC nodes and components are markers; components attach to their duct midpoint.
    for (const FcObject::Ptr& object : objects) {
        const auto hvac = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!hvac || hvac->keyword() != QStringLiteral("HVAC")) continue;
        const QString type = value(*hvac, QStringLiteral("TYPE_ID")).toUpper();
        FdsScenePoint position;
        bool hasPosition = false;
        if (type == QStringLiteral("NODE") && hvacNodePositions.contains(hvac->id())) {
            position = hvacNodePositions.value(hvac->id());
            hasPosition = true;
        } else if (type != QStringLiteral("DUCT")) {
            for (const FcObject::Ptr& candidate : objects) {
                const auto duct = std::dynamic_pointer_cast<FcFdsNamelist>(candidate);
                if (!duct || duct->keyword() != QStringLiteral("HVAC")) continue;
                const QStringList components = referenceIds(*duct, QStringLiteral("AIRCOIL_ID")) +
                                               referenceIds(*duct, QStringLiteral("FAN_ID"));
                if (components.contains(hvac->id()) && hvacDuctMidpoints.contains(duct->id())) {
                    position = hvacDuctMidpoints.value(duct->id());
                    hasPosition = true;
                    break;
                }
            }
        }
        if (!hasPosition) continue;
        FdsScenePrimitive primitive;
        primitive.kind = FdsScenePrimitiveKind::Point;
        primitive.objectId = hvac->id();
        primitive.instanceKey = hvac->id() + QStringLiteral(":marker");
        primitive.keyword = QStringLiteral("HVAC");
        primitive.name = hvac->name();
        primitive.start = primitive.end = position;
        primitive.color = typeColor(QStringLiteral("HVAC"));
        primitive.visible = inheritedVisibility(*hvac);
        scene.primitives.push_back(primitive);
    }

    // Devices attached to HVAC records can only be placed after the network is resolved.
    for (const FcObject::Ptr& object : objects) {
        const auto device = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!device || device->keyword() != QStringLiteral("DEVC") ||
            !value(*device, QStringLiteral("XYZ")).isEmpty() ||
            !value(*device, QStringLiteral("XB")).isEmpty()) continue;
        FdsScenePoint position;
        bool hasPosition = false;
        const QStringList nodes = referenceIds(*device, QStringLiteral("NODE_ID"));
        const QStringList ducts = referenceIds(*device, QStringLiteral("DUCT_ID"));
        if (!nodes.isEmpty() && hvacNodePositions.contains(nodes.constFirst())) {
            position = hvacNodePositions.value(nodes.constFirst());
            hasPosition = true;
        } else if (!ducts.isEmpty() && hvacDuctMidpoints.contains(ducts.constFirst())) {
            position = hvacDuctMidpoints.value(ducts.constFirst());
            hasPosition = true;
        }
        if (!hasPosition) continue;
        FdsScenePrimitive primitive;
        primitive.kind = FdsScenePrimitiveKind::Point;
        primitive.objectId = device->id();
        primitive.instanceKey = device->id() + QStringLiteral(":0");
        primitive.keyword = QStringLiteral("DEVC");
        primitive.name = device->name();
        primitive.start = primitive.end = position;
        primitive.color = typeColor(QStringLiteral("DEVC"));
        primitive.visible = inheritedVisibility(*device);
        scene.primitives.push_back(primitive);
    }

    // SLCF planes default to hidden to avoid obscuring the model.
    for (const FcObject::Ptr& object : objects) {
        if (const auto output = std::dynamic_pointer_cast<FcFdsOutput>(object)) {
            if (output->kind() != FcFdsOutputKind::Slice || !scene.hasDomainBounds) continue;
            FdsScenePrimitive primitive;
            primitive.kind = FdsScenePrimitiveKind::Plane;
            primitive.objectId = output->id();
            primitive.instanceKey = output->id() + QStringLiteral(":0");
            primitive.keyword = QStringLiteral("SLCF");
            primitive.name = output->name();
            primitive.bounds = scene.domainBounds;
            if (output->planeAxis() == FcFdsPlaneAxis::X) primitive.bounds.xMin = primitive.bounds.xMax = output->planeValue();
            else if (output->planeAxis() == FcFdsPlaneAxis::Y) primitive.bounds.yMin = primitive.bounds.yMax = output->planeValue();
            else primitive.bounds.zMin = primitive.bounds.zMax = output->planeValue();
            primitive.color = typeColor(primitive.keyword);
            primitive.visible = inheritedVisibility(*output);
            scene.primitives.push_back(primitive);
            continue;
        }
        const auto output = std::dynamic_pointer_cast<FcFdsNamelist>(object);
        if (!output || output->keyword() != QStringLiteral("SLCF")) continue;
        FcFdsBounds bounds;
        if (!planeBounds(*output, scene.domainBounds, scene.hasDomainBounds, bounds)) {
            scene.warnings.append(QStringLiteral("SLCF %1 cannot be placed without a mesh domain.")
                                      .arg(output->name()));
            continue;
        }
        FdsScenePrimitive primitive;
        primitive.kind = FdsScenePrimitiveKind::Plane;
        primitive.objectId = output->id();
        primitive.instanceKey = output->id() + QStringLiteral(":0");
        primitive.keyword = QStringLiteral("SLCF");
        primitive.name = output->name();
        primitive.bounds = bounds;
        primitive.color = typeColor(primitive.keyword);
        primitive.visible = inheritedVisibility(*output);
        scene.primitives.push_back(primitive);
    }

    return scene;
}
