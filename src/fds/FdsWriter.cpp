#include "fds/FdsWriter.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "geometry/FcGeometryObject.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <memory>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace
{
QString number(double value)
{
    return QLocale::c().toString(value, 'g', 15);
}

QString quoted(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

QString bounds(const FcFdsBounds& value)
{
    return QStringLiteral("%1,%2,%3,%4,%5,%6")
        .arg(number(value.xMin),
             number(value.xMax),
             number(value.yMin),
             number(value.yMax),
             number(value.zMin),
             number(value.zMax));
}

QString parameterBase(const QString& key)
{
    const int arraySuffix = key.indexOf(QLatin1Char('('));
    return key.left(arraySuffix < 0 ? key.size() : arraySuffix).trimmed().toUpper();
}

QStringList expectedReferenceKeywords(const QString& key)
{
    const QString base = parameterBase(key);
    if (base == QStringLiteral("SURF_ID") ||
        base == QStringLiteral("SURF_ID6") ||
        base == QStringLiteral("SURF_IDS")) return {QStringLiteral("SURF")};
    if (base == QStringLiteral("FUEL") || base == QStringLiteral("SPEC_ID"))
        return {QStringLiteral("SPEC")};
    if (base == QStringLiteral("MATL_ID")) return {QStringLiteral("MATL")};
    if (base == QStringLiteral("PART_ID")) return {QStringLiteral("PART")};
    if (base == QStringLiteral("PROP_ID")) return {QStringLiteral("PROP")};
    if (base == QStringLiteral("RAMP_ID")) return {QStringLiteral("RAMP")};
    if (base == QStringLiteral("MULT_ID")) return {QStringLiteral("MULT")};
    if (base == QStringLiteral("OBST_ID")) return {QStringLiteral("OBST")};
    if (base == QStringLiteral("VENT_ID")) return {QStringLiteral("VENT")};
    if (base == QStringLiteral("DEVC_ID")) return {QStringLiteral("DEVC")};
    if (base == QStringLiteral("CTRL_ID")) return {QStringLiteral("CTRL")};
    if (base == QStringLiteral("INPUT_ID"))
        return {QStringLiteral("DEVC"), QStringLiteral("CTRL")};
    if (base == QStringLiteral("DUCT_ID") || base == QStringLiteral("NODE_ID") ||
        base == QStringLiteral("AIRCOIL_ID") || base == QStringLiteral("FAN_ID"))
        return {QStringLiteral("HVAC")};
    if (base == QStringLiteral("SPRAY_PATTERN_TABLE"))
        return {QStringLiteral("TABL")};
    return {};
}

QString fdsKeywordForObject(const FcObject& object)
{
    if (const auto* namelist = dynamic_cast<const FcFdsNamelist*>(&object)) {
        return namelist->keyword();
    }
    switch (object.type()) {
    case FcObjectType::Mesh: return QStringLiteral("MESH");
    case FcObjectType::MeshMultiplier: return QStringLiteral("MULT");
    case FcObjectType::Species: return QStringLiteral("SPEC");
    case FcObjectType::Material: return QStringLiteral("MATL");
    case FcObjectType::Surface: return QStringLiteral("SURF");
    case FcObjectType::Reaction: return QStringLiteral("REAC");
    case FcObjectType::Obstruction: return QStringLiteral("OBST");
    case FcObjectType::Vent: return QStringLiteral("VENT");
    case FcObjectType::Particle: return QStringLiteral("PART");
    case FcObjectType::Property: return QStringLiteral("PROP");
    case FcObjectType::Table: return QStringLiteral("TABL");
    case FcObjectType::Ramp: return QStringLiteral("RAMP");
    case FcObjectType::Device: return QStringLiteral("DEVC");
    case FcObjectType::Control: return QStringLiteral("CTRL");
    case FcObjectType::HVAC: return QStringLiteral("HVAC");
    default: return {};
    }
}

const FcFdsParameter* findParameter(const FcFdsNamelist& object,
                                    const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    for (const FcFdsParameter& parameter : object.parameters()) {
        if (parameterBase(parameter.key) == normalized) return &parameter;
    }
    return nullptr;
}

bool parseFiniteNumber(const QString& value, double& number)
{
    bool ok = false;
    number = QLocale::c().toDouble(value.trimmed(), &ok);
    return ok && std::isfinite(number);
}

bool parseFiniteNumberList(const QString& value,
                           int count,
                           std::vector<double>& numbers)
{
    numbers.clear();
    const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (parts.size() != count) return false;
    for (const QString& part : parts) {
        double number = 0.0;
        if (!parseFiniteNumber(part, number)) return false;
        numbers.push_back(number);
    }
    return true;
}

bool parseFiniteNumberVector(const QString& value, std::vector<double>& numbers)
{
    numbers.clear();
    const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    for (const QString& part : parts) {
        double parsed = 0.0;
        if (!parseFiniteNumber(part, parsed)) return false;
        numbers.push_back(parsed);
    }
    return true;
}

QString hvacSubtype(const FcFdsNamelist& object)
{
    return object.parameterValue(QStringLiteral("TYPE_ID")).trimmed().toUpper();
}

struct MeshExtent
{
    std::array<double, 6> bounds{};
    std::array<int, 3> cells{};
    QString sourceUuid;
    QString sourceName;
};

bool containsCoordinate(const MeshExtent& mesh, int axis, double coordinate)
{
    constexpr double tolerance = 1.0e-9;
    return coordinate >= mesh.bounds[static_cast<std::size_t>(axis * 2)] - tolerance &&
           coordinate <= mesh.bounds[static_cast<std::size_t>(axis * 2 + 1)] + tolerance;
}

bool volumeOverlap(const MeshExtent& left, const MeshExtent& right)
{
    constexpr double tolerance = 1.0e-9;
    for (int axis = 0; axis < 3; ++axis) {
        const double intersection =
            qMin(left.bounds[static_cast<std::size_t>(axis * 2 + 1)],
                 right.bounds[static_cast<std::size_t>(axis * 2 + 1)]) -
            qMax(left.bounds[static_cast<std::size_t>(axis * 2)],
                 right.bounds[static_cast<std::size_t>(axis * 2)]);
        if (intersection <= tolerance) return false;
    }
    return true;
}

int sharedFaceAxis(const MeshExtent& left, const MeshExtent& right)
{
    constexpr double tolerance = 1.0e-9;
    for (int axis = 0; axis < 3; ++axis) {
        const std::size_t minimum = static_cast<std::size_t>(axis * 2);
        const std::size_t maximum = minimum + 1;
        const bool touches =
            std::abs(left.bounds[maximum] - right.bounds[minimum]) <= tolerance ||
            std::abs(right.bounds[maximum] - left.bounds[minimum]) <= tolerance;
        if (!touches) continue;
        bool positiveFaceArea = true;
        for (int other = 0; other < 3; ++other) {
            if (other == axis) continue;
            const std::size_t otherMinimum = static_cast<std::size_t>(other * 2);
            const std::size_t otherMaximum = otherMinimum + 1;
            const double intersection =
                qMin(left.bounds[otherMaximum], right.bounds[otherMaximum]) -
                qMax(left.bounds[otherMinimum], right.bounds[otherMinimum]);
            if (intersection <= tolerance) {
                positiveFaceArea = false;
                break;
            }
        }
        if (positiveFaceArea) return axis;
    }
    return -1;
}

double meshCellSize(const MeshExtent& mesh, int axis)
{
    const int count = mesh.cells[static_cast<std::size_t>(axis)];
    if (count <= 0) return 0.0;
    return (mesh.bounds[static_cast<std::size_t>(axis * 2 + 1)] -
            mesh.bounds[static_cast<std::size_t>(axis * 2)]) /
           count;
}

bool nearlyInteger(double value)
{
    return std::abs(value - std::round(value)) <=
           1.0e-7 * std::max(1.0, std::abs(value));
}

bool boundaryGridAligned(const MeshExtent& mesh,
                         int axis,
                         double overlapMinimum,
                         double overlapMaximum)
{
    const double cellSize = meshCellSize(mesh, axis);
    if (cellSize <= 0.0) return true;
    const double origin = mesh.bounds[static_cast<std::size_t>(axis * 2)];
    return nearlyInteger((overlapMinimum - origin) / cellSize) &&
           nearlyInteger((overlapMaximum - origin) / cellSize);
}

int integerParameter(const FcFdsNamelist& object, const QString& key, int fallback)
{
    bool ok = false;
    const int value = object.parameterValue(key).toInt(&ok);
    return ok ? value : fallback;
}

double numberParameter(const FcFdsNamelist& object,
                       const QString& key,
                       double fallback)
{
    double value = 0.0;
    return parseFiniteNumber(object.parameterValue(key), value) ? value : fallback;
}

bool disabledInActiveScenario(const FcProject& project, const QString& objectId)
{
    const FcScenario* scenario = project.activeScenario();
    return scenario && scenario->disabledObjectIds.contains(objectId);
}

std::shared_ptr<FcFdsObject> cloneTypedFdsObject(const FcFdsObject& source)
{
    if (const auto* value = dynamic_cast<const FcFdsMesh*>(&source))
        return std::make_shared<FcFdsMesh>(value->name(), value->fdsId(), value->cells(), value->bounds());
    if (const auto* value = dynamic_cast<const FcFdsSurface*>(&source))
        return std::make_shared<FcFdsSurface>(value->name(), value->fdsId(), value->heatReleaseRatePerArea(), value->color());
    if (const auto* value = dynamic_cast<const FcFdsReaction*>(&source))
        return std::make_shared<FcFdsReaction>(value->name(), value->fdsId(), value->fuel(), value->sootYield());
    if (const auto* value = dynamic_cast<const FcFdsObstruction*>(&source))
        return std::make_shared<FcFdsObstruction>(value->name(), value->fdsId(), value->bounds(), value->surfaceId());
    if (const auto* value = dynamic_cast<const FcFdsVent*>(&source))
        return std::make_shared<FcFdsVent>(value->name(), value->fdsId(), value->bounds(), value->surfaceId());
    if (const auto* value = dynamic_cast<const FcFdsOutput*>(&source)) {
        return value->kind() == FcFdsOutputKind::Boundary
            ? FcFdsOutput::boundary(value->name(), value->fdsId(), value->quantity())
            : FcFdsOutput::slice(value->name(), value->fdsId(), value->planeAxis(),
                                 value->planeValue(), value->quantity(), value->vectorOutput());
    }
    return {};
}

bool applyTypedScenarioOverride(std::shared_ptr<FcFdsObject>& object,
                                const FcScenarioParameterOverride& entry,
                                const FcProject& project)
{
    const QString key = entry.parameterKey.trimmed().toUpper();
    QString value = entry.value.trimmed();
    if (entry.reference) {
        if ((key != QStringLiteral("SURF_ID") && key != QStringLiteral("FUEL")) ||
            entry.targetObjectIds.size() != 1) return false;
        const QString targetId = entry.targetObjectIds.front();
        const auto target = std::dynamic_pointer_cast<FcFdsObject>(project.document()->findObject(targetId));
        if (!target || disabledInActiveScenario(project, targetId) ||
            !expectedReferenceKeywords(key).contains(fdsKeywordForObject(*target))) return false;
        value = target->fdsId();
    }
    const auto stringValue = [&value]() {
        if (value.size() >= 2 && (value.front() == QLatin1Char('\'') || value.front() == QLatin1Char('"')) &&
            value.back() == value.front()) {
            const QChar quote = value.front();
            QString text = value.mid(1, value.size() - 2);
            text.replace(QString(2, quote), QString(1, quote));
            return text;
        }
        return value;
    };
    std::vector<double> numbers;
    if (key == QStringLiteral("XB")) {
        if (!parseFiniteNumberList(value, 6, numbers)) return false;
        const FcFdsBounds box{numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]};
        if (auto* mesh = dynamic_cast<FcFdsMesh*>(object.get())) mesh->setBounds(box);
        else if (auto* obstruction = dynamic_cast<FcFdsObstruction*>(object.get())) obstruction->setBounds(box);
        else if (auto* vent = dynamic_cast<FcFdsVent*>(object.get())) vent->setBounds(box);
        else return false;
        return true;
    }
    if (auto* mesh = dynamic_cast<FcFdsMesh*>(object.get())) {
        if (key != QStringLiteral("IJK") || !parseFiniteNumberList(value, 3, numbers)) return false;
        std::array<int, 3> cells{};
        for (std::size_t axis = 0; axis < cells.size(); ++axis) {
            const double count = numbers[axis];
            if (count <= 0.0 || count > std::numeric_limits<int>::max() || std::floor(count) != count) return false;
            cells[axis] = static_cast<int>(count);
        }
        mesh->setCells(cells);
        return true;
    }
    if (auto* surface = dynamic_cast<FcFdsSurface*>(object.get())) {
        if (key == QStringLiteral("COLOR")) { surface->setColor(stringValue()); return true; }
        double rate = 0.0;
        if (key != QStringLiteral("HRRPUA") || !parseFiniteNumber(value, rate)) return false;
        surface->setHeatReleaseRatePerArea(rate);
        return true;
    }
    if (auto* reaction = dynamic_cast<FcFdsReaction*>(object.get())) {
        if (key == QStringLiteral("FUEL")) { reaction->setFuel(stringValue()); return true; }
        double yield = 0.0;
        if (key != QStringLiteral("SOOT_YIELD") || !parseFiniteNumber(value, yield)) return false;
        reaction->setSootYield(yield);
        return true;
    }
    if (auto* obstruction = dynamic_cast<FcFdsObstruction*>(object.get())) {
        if (key != QStringLiteral("SURF_ID")) return false;
        obstruction->setSurfaceId(stringValue());
        return true;
    }
    if (auto* vent = dynamic_cast<FcFdsVent*>(object.get())) {
        if (key != QStringLiteral("SURF_ID")) return false;
        vent->setSurfaceId(stringValue());
        return true;
    }
    if (const auto* output = dynamic_cast<const FcFdsOutput*>(object.get())) {
        QString quantity = output->quantity();
        FcFdsPlaneAxis axis = output->planeAxis();
        double plane = output->planeValue();
        bool vector = output->vectorOutput();
        if (key == QStringLiteral("QUANTITY")) quantity = stringValue();
        else if (output->kind() == FcFdsOutputKind::Slice &&
                 (key == QStringLiteral("PBX") || key == QStringLiteral("PBY") || key == QStringLiteral("PBZ"))) {
            if (!parseFiniteNumber(value, plane)) return false;
            axis = key == QStringLiteral("PBX") ? FcFdsPlaneAxis::X :
                   key == QStringLiteral("PBY") ? FcFdsPlaneAxis::Y : FcFdsPlaneAxis::Z;
        } else if (output->kind() == FcFdsOutputKind::Slice && key == QStringLiteral("VECTOR")) {
            const QString logical = value.toUpper();
            if (logical != QStringLiteral(".TRUE.") && logical != QStringLiteral(".FALSE.")) return false;
            vector = logical == QStringLiteral(".TRUE.");
        } else return false;
        object = output->kind() == FcFdsOutputKind::Boundary
            ? FcFdsOutput::boundary(output->name(), output->fdsId(), quantity)
            : FcFdsOutput::slice(output->name(), output->fdsId(), axis, plane, quantity, vector);
        return true;
    }
    return false;
}

template<typename T>
std::vector<std::shared_ptr<T>> effectiveTypedObjects(
    const FcProject& project, std::vector<std::shared_ptr<T>> objects, QStringList& errors)
{
    const FcScenario* scenario = project.activeScenario();
    if (!scenario) return objects;
    for (auto& source : objects) {
        std::shared_ptr<FcFdsObject> effective;
        for (const auto& entry : scenario->parameterOverrides) {
            if (entry.objectId != source->id()) continue;
            if (!effective) effective = cloneTypedFdsObject(*source);
            if (!effective || !applyTypedScenarioOverride(effective, entry, project)) {
                errors.append(QStringLiteral("Scenario '%1': object '%2' [UUID %3] has an unsupported or invalid override %4=%5.")
                    .arg(scenario->name, source->name(), source->id(), entry.parameterKey, entry.value));
            }
        }
        if (effective) {
            effective->restorePersistentId(source->id());
            source = std::dynamic_pointer_cast<T>(effective);
        }
    }
    return objects;
}

std::shared_ptr<FcFdsNamelist> scenarioNamelist(
    const FcProject& project, const std::shared_ptr<FcFdsNamelist>& source)
{
    const FcScenario* scenario = project.activeScenario();
    if (!scenario || !source) return source;
    bool hasOverride = false;
    for (const FcScenarioParameterOverride& entry : scenario->parameterOverrides) {
        if (entry.objectId == source->id()) { hasOverride = true; break; }
    }
    if (!hasOverride) return source;
    auto effective = std::make_shared<FcFdsNamelist>(
        source->name(), source->type(), source->keyword(), source->fdsId(),
        source->sequenceIndex());
    effective->restorePersistentId(source->id());
    effective->setVisible(source->isVisible());
    effective->setLocked(source->isLocked());
    effective->setFloorName(source->floorName());
    effective->setTags(source->tags());
    std::vector<FcFdsParameter> parameters = source->parameters();
    for (const FcScenarioParameterOverride& entry : scenario->parameterOverrides) {
        if (entry.objectId != source->id()) continue;
        auto existing = std::find_if(
            parameters.begin(), parameters.end(), [&entry](const FcFdsParameter& parameter) {
                return parameter.key.compare(entry.parameterKey,
                                             Qt::CaseInsensitive) == 0;
            });
        FcFdsParameter value;
        value.key = entry.parameterKey.trimmed().toUpper();
        value.kind = entry.reference ? FcFdsParameterKind::ObjectReferences
                                     : FcFdsParameterKind::Raw;
        value.value = entry.value;
        value.targetObjectIds = entry.targetObjectIds;
        if (existing == parameters.end()) parameters.push_back(value);
        else *existing = value;
    }
    effective->setParameters(parameters);
    return effective;
}

template<typename T>
void collectObject(const FcObject::Ptr& object, std::vector<std::shared_ptr<T>>& result)
{
    if (!object) {
        return;
    }
    if (const auto typed = std::dynamic_pointer_cast<T>(object)) {
        result.push_back(typed);
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectObject<T>(child, result);
    }
}

template<typename T>
std::vector<std::shared_ptr<T>> collect(const std::shared_ptr<FcObjectGroup>& group)
{
    std::vector<std::shared_ptr<T>> result;
    if (group) {
        for (const FcObject::Ptr& child : group->children()) {
            collectObject<T>(child, result);
        }
    }
    return result;
}

template<typename T>
void appendValidation(const std::vector<std::shared_ptr<T>>& objects,
                      const QString& kind,
                      QStringList& errors)
{
    QSet<QString> identifiers;
    for (const std::shared_ptr<T>& object : objects) {
        const QString prefix = kind + QStringLiteral(" '") + object->name() +
                               QStringLiteral("' [UUID ") + object->id() +
                               QStringLiteral("]: ");
        for (const QString& error : object->validate()) {
            errors.append(prefix + error);
        }
        const QString normalizedId = object->fdsId().trimmed().toUpper();
        if (!normalizedId.isEmpty() && identifiers.contains(normalizedId)) {
            errors.append(prefix + QStringLiteral("duplicate FDS ID '") +
                          object->fdsId() + QStringLiteral("'."));
        }
        identifiers.insert(normalizedId);
    }
}

QString renderGenericValue(const FcFdsParameter& parameter,
                           const FcDocument& document,
                           QStringList& errors,
                           const QString& owner)
{
    if (parameter.kind == FcFdsParameterKind::Raw) {
        return parameter.value;
    }
    if (parameter.kind == FcFdsParameterKind::String) {
        return quoted(parameter.value);
    }
    QStringList identifiers;
    const QStringList expectedKeywords = expectedReferenceKeywords(parameter.key);
    for (const QString& uuid : parameter.targetObjectIds) {
        const FcObject::Ptr target = document.findObject(uuid);
        const auto fdsTarget = std::dynamic_pointer_cast<FcFdsObject>(target);
        if (!fdsTarget) {
            errors.append(owner + QStringLiteral(" parameter ") + parameter.key +
                          QStringLiteral(" references missing object UUID '") + uuid +
                          QStringLiteral("'."));
            continue;
        }
        const QString actualKeyword = fdsKeywordForObject(*target);
        if (!expectedKeywords.isEmpty() &&
            !expectedKeywords.contains(actualKeyword, Qt::CaseInsensitive)) {
            errors.append(owner + QStringLiteral(" parameter ") + parameter.key +
                          QStringLiteral(" references UUID '") + uuid +
                          QStringLiteral("' of FDS type ") + actualKeyword +
                          QStringLiteral("; expected ") +
                          expectedKeywords.join(QStringLiteral(" or ")) +
                          QLatin1Char('.'));
            continue;
        }
        if (fdsTarget->fdsId().isEmpty()) {
            errors.append(owner + QStringLiteral(" parameter ") + parameter.key +
                          QStringLiteral(" references UUID '") + uuid +
                          QStringLiteral("' whose FDS ID is empty."));
            continue;
        }
        identifiers.append(quoted(fdsTarget->fdsId()));
    }
    return identifiers.isEmpty() ? parameter.value : identifiers.join(QLatin1Char(','));
}
// Character IDs are case-sensitive in FDS. Decode a single literal, without
// removing internal quotes or conflating a custom "Open" surface with OPEN.
bool controlScalarId(const FcFdsParameter& parameter, QString& value)
{
    value = parameter.value.trimmed();
    if (parameter.kind == FcFdsParameterKind::String) return true;
    if (value.isEmpty()) return true;
    const QChar quote = value.front();
    if (quote != QLatin1Char('\'') && quote != QLatin1Char('"')) {
        return !value.contains(QLatin1Char(',')) &&
               !value.contains(QLatin1Char('\'')) && !value.contains(QLatin1Char('"'));
    }
    if (value.size() < 2 || value.back() != quote) return false;
    QString decoded;
    for (qsizetype i = 1; i < value.size() - 1; ++i) {
        if (value.at(i) == quote) {
            if (i + 1 >= value.size() - 1 || value.at(i + 1) != quote) return false;
            ++i;
        }
        decoded += value.at(i);
    }
    value = decoded.trimmed();
    return true;
}

bool hasControlBinding(const FcFdsNamelist& target)
{
    for (const FcFdsParameter& parameter : target.parameters()) {
        const QString key = parameterBase(parameter.key);
        if (key != QStringLiteral("DEVC_ID") && key != QStringLiteral("CTRL_ID")) continue;
        if (parameter.kind == FcFdsParameterKind::ObjectReferences &&
            !parameter.targetObjectIds.isEmpty()) return true;
        QString id;
        if (!controlScalarId(parameter, id) ||
            (!id.isEmpty() && id != QStringLiteral("null"))) return true;
    }
    return false;
}

QStringList controlledVentErrors(const FcProject& project,
                                 const FcFdsNamelist& effectiveTarget)
{
    QStringList errors;
    const FcDocument* document = project.document();
    const FcFdsParameter* surface = findParameter(effectiveTarget, QStringLiteral("SURF_ID"));
    if (!surface || !document) return errors; // Other model validation covers missing SURF_ID.
    const auto checkSurfaceIdentity = [&](const std::shared_ptr<FcFdsObject>& object) {
        if (disabledInActiveScenario(project, object->id())) {
            errors.append(QStringLiteral("Controlled VENT SURF_ID references surface '%1' [UUID %2] disabled by the active scenario.")
                              .arg(object->fdsId(), object->id()));
        }
        if (const auto generic = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
            const auto effective = scenarioNamelist(project, generic);
            // The writer emits the object's canonical ID before raw parameters;
            // an ID override must not silently change a UUID binding's meaning.
            const FcFdsParameter* rawId = findParameter(*effective, QStringLiteral("ID"));
            if (rawId && !rawId->value.trimmed().isEmpty()) {
                QString id;
                if (!controlScalarId(*rawId, id) ||
                    (!id.isEmpty() && id != effective->fdsId())) {
                    errors.append(QStringLiteral("Controlled VENT surface '%1' [UUID %2] has a conflicting raw ID parameter in the active scenario; resolve the surface identity before binding control.")
                                      .arg(effective->fdsId(), effective->id()));
                }
            }
        }
    };
    QString surfaceId;
    if (surface->kind == FcFdsParameterKind::ObjectReferences &&
        !surface->targetObjectIds.isEmpty()) {
        if (surface->targetObjectIds.size() != 1) {
            errors.append(QStringLiteral("Controlled VENT SURF_ID requires exactly one surface UUID."));
            return errors;
        }
        const QString uuid = surface->targetObjectIds.front();
        const auto resolved = std::dynamic_pointer_cast<FcFdsObject>(document->findObject(uuid));
        if (!resolved || fdsKeywordForObject(*resolved) != QStringLiteral("SURF") ||
            resolved->fdsId().trimmed().isEmpty()) {
            errors.append(QStringLiteral("Controlled VENT SURF_ID references a missing or invalid SURF [UUID %1].").arg(uuid));
            return errors;
        }
        checkSurfaceIdentity(resolved);
        surfaceId = resolved->fdsId().trimmed(); // Same identity used by renderGenericValue.
    } else {
        // ObjectReferences can carry a raw fallback, as renderGenericValue does.
        if (!controlScalarId(*surface, surfaceId)) {
            errors.append(QStringLiteral("Controlled VENT SURF_ID must contain one surface identifier."));
            return errors;
        }
        for (const auto& group : document->groups()) {
            for (const auto& object : collect<FcFdsObject>(group)) {
                if (fdsKeywordForObject(*object) == QStringLiteral("SURF") &&
                    object->fdsId() == surfaceId) checkSurfaceIdentity(object);
            }
        }
    }
    // FDS 6.11.1 read.f90 maps PERIODIC FLOW ONLY to PERIODIC_BOUNDARY too.
    if (surfaceId == QStringLiteral("OPEN") || surfaceId == QStringLiteral("MIRROR") ||
        surfaceId == QStringLiteral("PERIODIC") || surfaceId == QStringLiteral("PERIODIC FLOW ONLY")) {
        errors.append(QStringLiteral("VENT with SURF_ID '%1' cannot be controlled by DEVC_ID or CTRL_ID.").arg(surfaceId));
    }
    return errors;
}

}

QStringList FdsWriter::validateControlledTarget(const FcProject& project,
                                                const QString& objectId)
{
    const FcDocument* document = project.document();
    const auto target = document ? std::dynamic_pointer_cast<FcFdsNamelist>(
                                       document->findObject(objectId)) : nullptr;
    if (!target || (target->keyword() != QStringLiteral("OBST") &&
                    target->keyword() != QStringLiteral("VENT"))) {
        return {QStringLiteral("Select an existing controlled VENT or OBST [UUID %1].").arg(objectId)};
    }
    if (disabledInActiveScenario(project, objectId)) {
        return {QStringLiteral("Controlled target '%1' [UUID %2] is disabled by the active scenario.")
                    .arg(target->name(), objectId)};
    }
    if (target->keyword() != QStringLiteral("VENT")) return {};
    return controlledVentErrors(project, *scenarioNamelist(project, target));
}

FdsModelStatistics FdsWriter::modelStatistics(const FcProject& project)
{
    FdsModelStatistics statistics;
    const FcDocument* document = project.document();
    if (!document) {
        statistics.warnings.append(QStringLiteral("Project has no document."));
        return statistics;
    }
    const auto typedMeshes = effectiveTypedObjects(
        project, collect<FcFdsMesh>(document->meshesGroup()), statistics.warnings);
    for (const auto& mesh : typedMeshes) {
        if (disabledInActiveScenario(project, mesh->id())) continue;
        const auto& cells = mesh->cells();
        const qint64 cellCount = static_cast<qint64>(cells[0]) * cells[1] * cells[2];
        statistics.meshes.push_back(
            {mesh->id(), mesh->name(), mesh->fdsId(), 1, cellCount, cellCount});
        ++statistics.expandedMeshCount;
        statistics.totalCellCount += cellCount;
    }

    std::vector<std::shared_ptr<FcFdsNamelist>> namelists;
    for (const auto& group : document->groups()) {
        const auto objects = collect<FcFdsNamelist>(group);
        for (const auto& object : objects) {
            if (!disabledInActiveScenario(project, object->id()))
                namelists.push_back(scenarioNamelist(project, object));
        }
    }
    for (const auto& mesh : namelists) {
        if (mesh->keyword() != QStringLiteral("MESH")) continue;
        std::vector<double> parsedCells;
        if (!parseFiniteNumberList(mesh->parameterValue(QStringLiteral("IJK")), 3,
                                   parsedCells)) {
            statistics.warnings.append(
                QStringLiteral("Mesh '%1' [UUID %2] has invalid IJK.")
                    .arg(mesh->name(), mesh->id()));
            continue;
        }
        qint64 cellsPerInstance = 1;
        bool cellCountsValid = true;
        for (double value : parsedCells) {
            const qint64 integer = qRound64(value);
            if (integer <= 0 || std::abs(value - static_cast<double>(integer)) > 1.0e-9) {
                cellCountsValid = false;
                break;
            }
            cellsPerInstance *= integer;
        }
        if (!cellCountsValid) {
            statistics.warnings.append(
                QStringLiteral("Mesh '%1' [UUID %2] IJK must use positive integers.")
                    .arg(mesh->name(), mesh->id()));
            continue;
        }
        std::shared_ptr<FcFdsNamelist> multiplier;
        if (const FcFdsParameter* parameter =
                findParameter(*mesh, QStringLiteral("MULT_ID"));
            parameter && parameter->kind == FcFdsParameterKind::ObjectReferences &&
            !parameter->targetObjectIds.isEmpty()) {
            multiplier = scenarioNamelist(project, std::dynamic_pointer_cast<FcFdsNamelist>(
                document->findObject(parameter->targetObjectIds.constFirst())));
        }
        const int iLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("I_LOWER"), 0)
                               : 0;
        const int iUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("I_UPPER"), 0)
                               : 0;
        const int jLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("J_LOWER"), 0)
                               : 0;
        const int jUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("J_UPPER"), 0)
                               : 0;
        const int kLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("K_LOWER"), 0)
                               : 0;
        const int kUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("K_UPPER"), 0)
                               : 0;
        if (iUpper < iLower || jUpper < jLower || kUpper < kLower) {
            statistics.warnings.append(
                QStringLiteral("Mesh '%1' [UUID %2] has invalid MULT bounds.")
                    .arg(mesh->name(), mesh->id()));
            continue;
        }
        const qint64 instances =
            static_cast<qint64>(iUpper - iLower + 1) *
            static_cast<qint64>(jUpper - jLower + 1) *
            static_cast<qint64>(kUpper - kLower + 1);
        const qint64 totalCells = cellsPerInstance * instances;
        statistics.meshes.push_back({mesh->id(), mesh->name(), mesh->fdsId(),
                                     instances, cellsPerInstance, totalCells});
        statistics.expandedMeshCount += instances;
        statistics.totalCellCount += totalCells;
    }
    return statistics;
}

FdsWriteResult FdsWriter::render(const FcProject& project)
{
    FdsWriteResult result;
    const FcScenario* activeScenario = project.activeScenario();
    const QString effectiveChid = activeScenario && !activeScenario->chid.trimmed().isEmpty()
                                      ? activeScenario->chid.trimmed() : project.chid();
    if (effectiveChid.trimmed().isEmpty()) {
        result.errors.append(QStringLiteral("Project CHID must not be empty."));
    }
    if (project.endTime() <= 0.0) {
        result.errors.append(QStringLiteral("Project end time must be positive."));
    }

    const FcDocument* document = project.document();
    if (!document) {
        result.errors.append(QStringLiteral("Project has no document."));
        return result;
    }

    const auto withoutDisabled = [&project](auto values) {
        values.erase(std::remove_if(values.begin(), values.end(), [&project](const auto& value) {
            return disabledInActiveScenario(project, value->id());
        }), values.end());
        return values;
    };
    if (activeScenario) {
        for (const auto& entry : activeScenario->parameterOverrides) {
            if (!std::dynamic_pointer_cast<FcFdsObject>(document->findObject(entry.objectId))) {
                result.errors.append(QStringLiteral("Scenario '%1': override %2 targets a missing or unsupported FDS object [UUID %3].")
                    .arg(activeScenario->name, entry.parameterKey, entry.objectId));
            }
        }
    }
    const auto meshes = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsMesh>(document->meshesGroup())), result.errors);
    const auto reactions = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsReaction>(document->reactionsGroup())), result.errors);
    const auto surfaces = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsSurface>(document->surfacesGroup())), result.errors);
    const auto obstructions = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsObstruction>(document->geometryGroup())), result.errors);
    const auto vents = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsVent>(document->ventsGroup())), result.errors);
    const auto outputs = effectiveTypedObjects(project, withoutDisabled(collect<FcFdsOutput>(document->outputsGroup())), result.errors);
    std::vector<std::shared_ptr<FcFdsNamelist>> namelists;
    for (const auto& group : document->groups()) {
        const auto groupObjects = collect<FcFdsNamelist>(group);
        for (const auto& object : groupObjects) {
            if (!disabledInActiveScenario(project, object->id())) {
                namelists.push_back(scenarioNamelist(project, object));
            }
        }
    }
    std::stable_sort(namelists.begin(), namelists.end(),
                     [](const auto& left, const auto& right) {
                         return left->sequenceIndex() < right->sequenceIndex();
                     });

    const bool hasGenericMesh = std::any_of(namelists.cbegin(), namelists.cend(),
                                            [](const auto& object) {
                                                return object->keyword() ==
                                                       QStringLiteral("MESH");
                                            });
    if (meshes.empty() && !hasGenericMesh) {
        result.errors.append(QStringLiteral("At least one mesh is required."));
    }
    appendValidation(meshes, QStringLiteral("Mesh"), result.errors);
    appendValidation(reactions, QStringLiteral("Reaction"), result.errors);
    appendValidation(surfaces, QStringLiteral("Surface"), result.errors);
    appendValidation(obstructions, QStringLiteral("Obstruction"), result.errors);
    appendValidation(vents, QStringLiteral("Vent"), result.errors);
    appendValidation(outputs, QStringLiteral("Output"), result.errors);
    QSet<QString> genericIdentifiers;
    QHash<QString, std::shared_ptr<FcFdsNamelist>> controlsByUuid;
    QHash<QString, QStringList> controlGraph;
    QHash<QString, std::shared_ptr<FcFdsNamelist>> hvacByUuid;
    QHash<QString, QStringList> hvacDuctNodes;
    QHash<QString, QStringList> hvacNodeDucts;
    QSet<QString> referencedHvacComponents;
    QSet<QString> fireSurfaceUuids;
    QSet<QString> attachedSurfaceUuids;
    for (const auto& object : namelists) {
        const QString prefix = QStringLiteral("Namelist '") + object->name() +
                               QStringLiteral("' [UUID ") + object->id() +
                               QStringLiteral("]: ");
        for (const QString& error : object->validate()) {
            result.errors.append(prefix + error);
        }
        if (object->keyword() == QStringLiteral("VENT") && hasControlBinding(*object)) {
            for (const QString& error : controlledVentErrors(project, *object)) {
                result.errors.append(prefix + error);
            }
        }
        if (!object->fdsId().isEmpty()) {
            const QString key = object->keyword() + QLatin1Char(':') +
                                object->fdsId().trimmed().toUpper();
            const bool repeatedSeriesRecord =
                object->keyword() == QStringLiteral("RAMP") ||
                object->keyword() == QStringLiteral("TABL");
            if (!repeatedSeriesRecord && genericIdentifiers.contains(key)) {
                result.errors.append(prefix + QStringLiteral("duplicate FDS ID '") +
                                     object->fdsId() + QStringLiteral("'."));
            }
            genericIdentifiers.insert(key);
        }
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (parameter.kind == FcFdsParameterKind::ObjectReferences) {
                (void)renderGenericValue(parameter, *document, result.errors, prefix);
                for (const QString& uuid : parameter.targetObjectIds) {
                    if (disabledInActiveScenario(project, uuid)) {
                        result.errors.append(
                            prefix + QStringLiteral("parameter ") + parameter.key +
                            QStringLiteral(" references object UUID '") + uuid +
                            QStringLiteral("' disabled by active scenario '") +
                            (activeScenario ? activeScenario->name : QString{}) +
                            QStringLiteral("'."));
                    }
                }
                if ((object->keyword() == QStringLiteral("OBST") ||
                     object->keyword() == QStringLiteral("VENT")) &&
                    (parameterBase(parameter.key) == QStringLiteral("SURF_ID") ||
                     parameterBase(parameter.key) == QStringLiteral("SURF_ID6") ||
                     parameterBase(parameter.key) == QStringLiteral("SURF_IDS"))) {
                    for (const QString& uuid : parameter.targetObjectIds) {
                        attachedSurfaceUuids.insert(uuid);
                    }
                }
            }
        }
        if (object->keyword() == QStringLiteral("CTRL")) {
            controlsByUuid.insert(object->id(), object);
            const FcFdsParameter* input = findParameter(*object, QStringLiteral("INPUT_ID"));
            if (input && input->kind == FcFdsParameterKind::ObjectReferences) {
                for (const QString& targetUuid : input->targetObjectIds) {
                    const auto target = std::dynamic_pointer_cast<FcFdsNamelist>(
                        document->findObject(targetUuid));
                    if (target && target->keyword() == QStringLiteral("CTRL")) {
                        controlGraph[object->id()].append(targetUuid);
                    }
                }
            }
        }
        if (object->keyword() == QStringLiteral("SURF")) {
            const FcFdsParameter* hrrpua = findParameter(*object, QStringLiteral("HRRPUA"));
            const FcFdsParameter* mlrpua = findParameter(*object, QStringLiteral("MLRPUA"));
            double fireRate = 0.0;
            const bool hasFireRate =
                (hrrpua && parseFiniteNumber(hrrpua->value, fireRate) && fireRate > 0.0) ||
                (mlrpua && parseFiniteNumber(mlrpua->value, fireRate) && fireRate > 0.0);
            if (hasFireRate) fireSurfaceUuids.insert(object->id());
            const FcFdsParameter* materials =
                findParameter(*object, QStringLiteral("MATL_ID"));
            const FcFdsParameter* thickness =
                findParameter(*object, QStringLiteral("THICKNESS"));
            if (materials && materials->kind == FcFdsParameterKind::ObjectReferences) {
                if (!thickness) {
                    result.errors.append(
                        prefix + QStringLiteral(
                            "SURF parameter THICKNESS is required when MATL_ID is set."));
                } else {
                    std::vector<double> values;
                    if (!parseFiniteNumberVector(thickness->value, values)) {
                        result.errors.append(
                            prefix + QStringLiteral(
                                "SURF parameter THICKNESS must contain finite numbers."));
                    } else {
                        if (values.size() != static_cast<std::size_t>(
                                                 materials->targetObjectIds.size())) {
                            result.errors.append(
                                prefix + QStringLiteral(
                                    "SURF MATL_ID layer count must equal THICKNESS count."));
                        }
                        if (std::any_of(values.cbegin(), values.cend(),
                                        [](double value) { return value <= 0.0; })) {
                            result.errors.append(
                                prefix + QStringLiteral(
                                    "SURF THICKNESS values must all be positive."));
                        }
                    }
                }
            }
        }
        if (object->keyword() == QStringLiteral("MATL")) {
            const QStringList positiveParameters = {
                QStringLiteral("DENSITY"), QStringLiteral("CONDUCTIVITY"),
                QStringLiteral("SPECIFIC_HEAT")};
            for (const QString& key : positiveParameters) {
                const FcFdsParameter* parameter = findParameter(*object, key);
                if (!parameter) continue;
                double value = 0.0;
                if (!parseFiniteNumber(parameter->value, value) || value <= 0.0) {
                    result.errors.append(prefix + QStringLiteral("MATL parameter ") + key +
                                         QStringLiteral(" must be positive and finite."));
                }
            }
        }
        if (object->keyword() == QStringLiteral("PART")) {
            const FcFdsParameter* diameter =
                findParameter(*object, QStringLiteral("DIAMETER"));
            if (diameter) {
                double value = 0.0;
                if (!parseFiniteNumber(diameter->value, value) || value <= 0.0) {
                    result.errors.append(
                        prefix + QStringLiteral(
                            "PART parameter DIAMETER must be positive and finite."));
                }
            }
        }
        if (object->keyword() == QStringLiteral("PROP")) {
            const QStringList positiveParameters = {
                QStringLiteral("FLOW_RATE"), QStringLiteral("PARTICLE_VELOCITY"),
                QStringLiteral("PARTICLES_PER_SECOND")};
            for (const QString& key : positiveParameters) {
                const FcFdsParameter* parameter = findParameter(*object, key);
                if (!parameter) continue;
                double value = 0.0;
                if (!parseFiniteNumber(parameter->value, value) || value <= 0.0) {
                    result.errors.append(prefix + QStringLiteral("PROP parameter ") + key +
                                         QStringLiteral(" must be positive and finite."));
                }
            }
        }
        if (object->keyword() == QStringLiteral("TABL")) {
            const FcFdsParameter* table =
                findParameter(*object, QStringLiteral("TABLE_DATA"));
            std::vector<double> values;
            if (table && parseFiniteNumberList(table->value, 6, values)) {
                if (values[0] < 0.0 || values[1] > 180.0 ||
                    values[0] > values[1] || values[2] < 0.0 ||
                    values[3] > 360.0 || values[2] > values[3] ||
                    values[4] <= 0.0 || values[5] < 0.0) {
                    result.errors.append(
                        prefix + QStringLiteral(
                            "TABL spray angles, radius, and weighting must be ordered and non-negative."));
                }
            }
        }
        if (object->keyword() == QStringLiteral("HVAC")) {
            hvacByUuid.insert(object->id(), object);
            const QString subtype = hvacSubtype(*object);
            if (subtype.isEmpty()) {
                result.errors.append(prefix +
                                     QStringLiteral("HVAC parameter TYPE_ID is required."));
            }
            if (subtype == QStringLiteral("NODE")) {
                const FcFdsParameter* ducts =
                    findParameter(*object, QStringLiteral("DUCT_ID"));
                if (ducts && ducts->kind == FcFdsParameterKind::ObjectReferences) {
                    hvacNodeDucts.insert(object->id(), ducts->targetObjectIds);
                }
            }
            if (subtype == QStringLiteral("DUCT")) {
            const FcFdsParameter* nodes = findParameter(*object, QStringLiteral("NODE_ID"));
            if (!nodes ||
                (nodes->kind == FcFdsParameterKind::ObjectReferences &&
                 nodes->targetObjectIds.size() != 2)) {
                result.errors.append(
                    prefix + QStringLiteral(
                        "HVAC DUCT parameter NODE_ID must reference exactly two HVAC nodes."));
            }
                if (nodes && nodes->kind == FcFdsParameterKind::ObjectReferences) {
                    hvacDuctNodes.insert(object->id(), nodes->targetObjectIds);
                }
                const QStringList positiveParameters = {
                    QStringLiteral("LENGTH"), QStringLiteral("AREA"),
                    QStringLiteral("DIAMETER")};
                for (const QString& key : positiveParameters) {
                    const FcFdsParameter* parameter = findParameter(*object, key);
                    if (!parameter) continue;
                    double value = 0.0;
                    if (!parseFiniteNumber(parameter->value, value) || value <= 0.0) {
                        result.errors.append(
                            prefix + QStringLiteral("HVAC DUCT parameter ") + key +
                            QStringLiteral(" must be positive and finite."));
                    }
                }
                if (const FcFdsParameter* flow =
                        findParameter(*object, QStringLiteral("VOLUME_FLOW"))) {
                    double value = 0.0;
                    if (!parseFiniteNumber(flow->value, value) || value < 0.0) {
                        result.errors.append(
                            prefix + QStringLiteral(
                                "HVAC DUCT parameter VOLUME_FLOW must be non-negative and finite."));
                    }
                }
                for (const QString& componentKey :
                     {QStringLiteral("AIRCOIL_ID"), QStringLiteral("FAN_ID")}) {
                    const FcFdsParameter* component = findParameter(*object, componentKey);
                    if (component &&
                        component->kind == FcFdsParameterKind::ObjectReferences) {
                        for (const QString& uuid : component->targetObjectIds) {
                            referencedHvacComponents.insert(uuid);
                        }
                    }
                }
            }
        }
    }

    for (const auto& geometry : collect<FcGeometryObject>(document->geometryGroup())) {
        if (disabledInActiveScenario(project, geometry->id())) continue;
        if (!geometry->defaultSurfaceId().isEmpty()) {
            attachedSurfaceUuids.insert(geometry->defaultSurfaceId());
        }
        for (auto iterator = geometry->faceSurfaceIds().cbegin();
             iterator != geometry->faceSurfaceIds().cend(); ++iterator) {
            if (!iterator.value().isEmpty()) attachedSurfaceUuids.insert(iterator.value());
        }
    }
    for (const QString& fireSurfaceUuid : fireSurfaceUuids) {
        if (attachedSurfaceUuids.contains(fireSurfaceUuid)) continue;
        const FcObject::Ptr surface = document->findObject(fireSurfaceUuid);
        result.errors.append(
            QStringLiteral("Namelist '") +
            (surface ? surface->name() : QStringLiteral("SURF")) +
            QStringLiteral("' [UUID ") + fireSurfaceUuid +
            QStringLiteral("]: fire surface is not attached to any geometry, OBST, or VENT."));
    }

    QHash<QString, int> controlVisitState;
    QSet<QString> reportedControlCycles;
    const std::function<void(const QString&)> visitControl =
        [&](const QString& uuid) {
            controlVisitState[uuid] = 1;
            for (const QString& targetUuid : controlGraph.value(uuid)) {
                if (controlVisitState.value(targetUuid) == 1) {
                    const QString cycleKey = uuid + QLatin1Char('>') + targetUuid;
                    if (!reportedControlCycles.contains(cycleKey)) {
                        reportedControlCycles.insert(cycleKey);
                        const auto owner = controlsByUuid.value(uuid);
                        result.errors.append(
                            QStringLiteral("Namelist '") +
                            (owner ? owner->name() : QStringLiteral("CTRL")) +
                            QStringLiteral("' [UUID ") + uuid +
                            QStringLiteral("]: CTRL INPUT_ID reference cycle reaches UUID '") +
                            targetUuid + QStringLiteral("'."));
                    }
                } else if (controlVisitState.value(targetUuid) == 0) {
                    visitControl(targetUuid);
                }
            }
            controlVisitState[uuid] = 2;
        };
    for (auto iterator = controlsByUuid.cbegin(); iterator != controlsByUuid.cend();
         ++iterator) {
        if (controlVisitState.value(iterator.key()) == 0) visitControl(iterator.key());
    }

    QHash<QString, QStringList> hvacNodeGraph;
    QHash<QString, QString> hvacConnectionOwners;
    bool structuredHvacTopology = !hvacDuctNodes.isEmpty();
    for (auto ductIterator = hvacDuctNodes.cbegin();
         ductIterator != hvacDuctNodes.cend(); ++ductIterator) {
        const auto duct = hvacByUuid.value(ductIterator.key());
        const QString ductPrefix = QStringLiteral("Namelist '") +
                                   (duct ? duct->name() : QStringLiteral("HVAC DUCT")) +
                                   QStringLiteral("' [UUID ") + ductIterator.key() +
                                   QStringLiteral("]: ");
        const QStringList nodes = ductIterator.value();
        if (nodes.size() != 2) {
            structuredHvacTopology = false;
            continue;
        }
        if (nodes[0] == nodes[1]) {
            result.errors.append(
                ductPrefix + QStringLiteral(
                    "HVAC DUCT NODE_ID endpoints must be two distinct nodes."));
            continue;
        }
        bool endpointsValid = true;
        for (const QString& nodeUuid : nodes) {
            const auto node = hvacByUuid.value(nodeUuid);
            if (!node || hvacSubtype(*node) != QStringLiteral("NODE")) {
                result.errors.append(
                    ductPrefix + QStringLiteral("HVAC DUCT NODE_ID UUID '") + nodeUuid +
                    QStringLiteral("' must target an HVAC NODE."));
                endpointsValid = false;
                continue;
            }
            if (!hvacNodeDucts.value(nodeUuid).contains(ductIterator.key())) {
                result.errors.append(
                    QStringLiteral("Namelist '") + node->name() +
                    QStringLiteral("' [UUID ") + nodeUuid +
                    QStringLiteral("]: HVAC NODE DUCT_ID does not reference connected duct UUID '") +
                    ductIterator.key() + QStringLiteral("'."));
            }
        }
        if (!endpointsValid) continue;
        const QString connectionKey = nodes[0] < nodes[1]
                                          ? nodes[0] + QLatin1Char('>') + nodes[1]
                                          : nodes[1] + QLatin1Char('>') + nodes[0];
        if (hvacConnectionOwners.contains(connectionKey)) {
            result.errors.append(
                ductPrefix + QStringLiteral(
                    "duplicate HVAC duct connection; it repeats duct UUID '") +
                hvacConnectionOwners.value(connectionKey) + QStringLiteral("'."));
        } else {
            hvacConnectionOwners.insert(connectionKey, ductIterator.key());
        }
        hvacNodeGraph[nodes[0]].append(nodes[1]);
        hvacNodeGraph[nodes[1]].append(nodes[0]);
    }
    for (auto nodeIterator = hvacNodeDucts.cbegin();
         nodeIterator != hvacNodeDucts.cend(); ++nodeIterator) {
        const auto node = hvacByUuid.value(nodeIterator.key());
        const QString nodePrefix = QStringLiteral("Namelist '") +
                                   (node ? node->name() : QStringLiteral("HVAC NODE")) +
                                   QStringLiteral("' [UUID ") + nodeIterator.key() +
                                   QStringLiteral("]: ");
        if (nodeIterator.value().isEmpty()) {
            result.errors.append(
                nodePrefix + QStringLiteral("HVAC NODE is disconnected from every duct."));
        }
        for (const QString& ductUuid : nodeIterator.value()) {
            const auto duct = hvacByUuid.value(ductUuid);
            if (!duct || hvacSubtype(*duct) != QStringLiteral("DUCT")) {
                result.errors.append(
                    nodePrefix + QStringLiteral("HVAC NODE DUCT_ID UUID '") + ductUuid +
                    QStringLiteral("' must target an HVAC DUCT."));
            } else if (!hvacDuctNodes.value(ductUuid).contains(nodeIterator.key())) {
                result.errors.append(
                    nodePrefix + QStringLiteral("HVAC NODE DUCT_ID UUID '") + ductUuid +
                    QStringLiteral("' does not connect back to this node."));
            }
        }
    }
    if (structuredHvacTopology && !hvacNodeGraph.isEmpty()) {
        QSet<QString> visited;
        bool cycleReported = false;
        const std::function<void(const QString&, const QString&)> visitHvacNode =
            [&](const QString& uuid, const QString& parent) {
                visited.insert(uuid);
                for (const QString& adjacent : hvacNodeGraph.value(uuid)) {
                    if (adjacent == parent) continue;
                    if (visited.contains(adjacent)) {
                        if (!cycleReported) {
                            cycleReported = true;
                            const auto node = hvacByUuid.value(uuid);
                            result.errors.append(
                                QStringLiteral("Namelist '") +
                                (node ? node->name() : QStringLiteral("HVAC NODE")) +
                                QStringLiteral("' [UUID ") + uuid +
                                QStringLiteral("]: HVAC topology contains a duct cycle reaching UUID '") +
                                adjacent + QStringLiteral("'."));
                        }
                    } else {
                        visitHvacNode(adjacent, uuid);
                    }
                }
            };
        const QString firstNode = hvacNodeGraph.cbegin().key();
        visitHvacNode(firstNode, QString{});
        if (visited.size() != hvacNodeGraph.size()) {
            for (auto iterator = hvacNodeGraph.cbegin(); iterator != hvacNodeGraph.cend();
                 ++iterator) {
                if (visited.contains(iterator.key())) continue;
                const auto node = hvacByUuid.value(iterator.key());
                result.errors.append(
                    QStringLiteral("Namelist '") +
                    (node ? node->name() : QStringLiteral("HVAC NODE")) +
                    QStringLiteral("' [UUID ") + iterator.key() +
                    QStringLiteral("]: HVAC network is disconnected from UUID '") +
                    firstNode + QStringLiteral("'."));
                break;
            }
        }
    }
    for (auto iterator = hvacByUuid.cbegin(); iterator != hvacByUuid.cend();
         ++iterator) {
        const QString subtype = hvacSubtype(*iterator.value());
        if (structuredHvacTopology &&
            (subtype == QStringLiteral("AIRCOIL") || subtype == QStringLiteral("FAN")) &&
            !referencedHvacComponents.contains(iterator.key())) {
            result.errors.append(
                QStringLiteral("Namelist '") + iterator.value()->name() +
                QStringLiteral("' [UUID ") + iterator.key() +
                QStringLiteral("]: HVAC ") + subtype +
                QStringLiteral(" is not connected to any duct."));
        }
    }

    std::vector<MeshExtent> meshExtents;
    meshExtents.reserve(meshes.size() + namelists.size());
    for (const auto& mesh : meshes) {
        const FcFdsBounds& value = mesh->bounds();
        meshExtents.push_back({{value.xMin, value.xMax, value.yMin, value.yMax,
                                value.zMin, value.zMax},
                               mesh->cells(),
                               mesh->id(), mesh->name()});
    }
    for (const auto& mesh : namelists) {
        if (mesh->keyword() != QStringLiteral("MESH")) continue;
        std::vector<double> parsedBounds;
        if (!parseFiniteNumberList(mesh->parameterValue(QStringLiteral("XB")), 6,
                                   parsedBounds)) {
            continue;
        }
        std::array<int, 3> meshCells{};
        std::vector<double> parsedCells;
        if (parseFiniteNumberList(mesh->parameterValue(QStringLiteral("IJK")), 3,
                                  parsedCells)) {
            for (int axis = 0; axis < 3; ++axis) {
                const double value = parsedCells[static_cast<std::size_t>(axis)];
                if (value > 0.0 && nearlyInteger(value)) {
                    meshCells[static_cast<std::size_t>(axis)] =
                        static_cast<int>(std::llround(value));
                }
            }
        }
        std::shared_ptr<FcFdsNamelist> multiplier;
        if (const FcFdsParameter* parameter =
                findParameter(*mesh, QStringLiteral("MULT_ID"));
            parameter && parameter->kind == FcFdsParameterKind::ObjectReferences &&
            !parameter->targetObjectIds.isEmpty()) {
            multiplier = scenarioNamelist(project, std::dynamic_pointer_cast<FcFdsNamelist>(
                document->findObject(parameter->targetObjectIds.constFirst())));
        }
        const int iLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("I_LOWER"), 0)
                               : 0;
        const int iUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("I_UPPER"), 0)
                               : 0;
        const int jLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("J_LOWER"), 0)
                               : 0;
        const int jUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("J_UPPER"), 0)
                               : 0;
        const int kLower = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("K_LOWER"), 0)
                               : 0;
        const int kUpper = multiplier
                               ? integerParameter(*multiplier, QStringLiteral("K_UPPER"), 0)
                               : 0;
        const double dx = multiplier
                              ? numberParameter(*multiplier, QStringLiteral("DX"), 0.0)
                              : 0.0;
        const double dy = multiplier
                              ? numberParameter(*multiplier, QStringLiteral("DY"), 0.0)
                              : 0.0;
        const double dz = multiplier
                              ? numberParameter(*multiplier, QStringLiteral("DZ"), 0.0)
                              : 0.0;
        if (iUpper < iLower || jUpper < jLower || kUpper < kLower) {
            result.errors.append(
                QStringLiteral("Namelist '") + mesh->name() +
                QStringLiteral("' [UUID ") + mesh->id() +
                QStringLiteral("]: MULT index bounds must be ordered lower-to-upper."));
            continue;
        }
        const qint64 instanceCount =
            static_cast<qint64>(iUpper - iLower + 1) *
            static_cast<qint64>(jUpper - jLower + 1) *
            static_cast<qint64>(kUpper - kLower + 1);
        if (instanceCount > 10000) {
            result.errors.append(
                QStringLiteral("Namelist '") + mesh->name() +
                QStringLiteral("' [UUID ") + mesh->id() +
                QStringLiteral("]: MULT expands to more than 10000 mesh instances."));
            continue;
        }
        for (int i = iLower; i <= iUpper; ++i) {
            for (int j = jLower; j <= jUpper; ++j) {
                for (int k = kLower; k <= kUpper; ++k) {
                    meshExtents.push_back(
                        {{parsedBounds[0] + i * dx, parsedBounds[1] + i * dx,
                           parsedBounds[2] + j * dy, parsedBounds[3] + j * dy,
                           parsedBounds[4] + k * dz, parsedBounds[5] + k * dz},
                         meshCells,
                         mesh->id(), mesh->name()});
                }
            }
        }
    }
    QSet<QString> reportedOverlaps;
    std::vector<std::vector<std::size_t>> meshAdjacency(meshExtents.size());
    QSet<QString> reportedBoundaryWarnings;
    for (std::size_t left = 0; left < meshExtents.size(); ++left) {
        for (std::size_t right = left + 1; right < meshExtents.size(); ++right) {
            const QString pairKey = QStringLiteral("%1:%2")
                                        .arg(qMin(left, right))
                                        .arg(qMax(left, right));
            if (volumeOverlap(meshExtents[left], meshExtents[right])) {
                meshAdjacency[left].push_back(right);
                meshAdjacency[right].push_back(left);
                const QString sourcePairKey = meshExtents[left].sourceUuid <
                                                      meshExtents[right].sourceUuid
                                                  ? meshExtents[left].sourceUuid +
                                                        meshExtents[right].sourceUuid
                                                  : meshExtents[right].sourceUuid +
                                                        meshExtents[left].sourceUuid;
                if (!reportedOverlaps.contains(sourcePairKey)) {
                    reportedOverlaps.insert(sourcePairKey);
                    result.errors.append(
                        QStringLiteral("Mesh '") + meshExtents[left].sourceName +
                        QStringLiteral("' [UUID ") + meshExtents[left].sourceUuid +
                        QStringLiteral("] overlaps mesh '") +
                        meshExtents[right].sourceName + QStringLiteral("' [UUID ") +
                        meshExtents[right].sourceUuid +
                        QStringLiteral("] with a non-zero volume."));
                }
                continue;
            }

            const int faceAxis = sharedFaceAxis(meshExtents[left], meshExtents[right]);
            if (faceAxis < 0) continue;
            meshAdjacency[left].push_back(right);
            meshAdjacency[right].push_back(left);

            bool aligned = true;
            for (int axis = 0; axis < 3; ++axis) {
                if (axis == faceAxis) continue;
                const std::size_t minimum = static_cast<std::size_t>(axis * 2);
                const std::size_t maximum = minimum + 1;
                const double overlapMinimum =
                    qMax(meshExtents[left].bounds[minimum],
                         meshExtents[right].bounds[minimum]);
                const double overlapMaximum =
                    qMin(meshExtents[left].bounds[maximum],
                         meshExtents[right].bounds[maximum]);
                const double leftCell = meshCellSize(meshExtents[left], axis);
                const double rightCell = meshCellSize(meshExtents[right], axis);
                if (leftCell <= 0.0 || rightCell <= 0.0) continue;
                const double ratio = qMax(leftCell, rightCell) /
                                     qMin(leftCell, rightCell);
                if (!nearlyInteger(ratio) ||
                    !boundaryGridAligned(meshExtents[left], axis,
                                         overlapMinimum, overlapMaximum) ||
                    !boundaryGridAligned(meshExtents[right], axis,
                                         overlapMinimum, overlapMaximum)) {
                    aligned = false;
                    break;
                }
            }
            if (!aligned && !reportedBoundaryWarnings.contains(pairKey)) {
                reportedBoundaryWarnings.insert(pairKey);
                result.warnings.append(
                    QStringLiteral("Mesh '") + meshExtents[left].sourceName +
                    QStringLiteral("' [UUID ") + meshExtents[left].sourceUuid +
                    QStringLiteral("] and mesh '") +
                    meshExtents[right].sourceName + QStringLiteral("' [UUID ") +
                    meshExtents[right].sourceUuid +
                    QStringLiteral("] share a face, but their boundary cell lines "
                                   "or tangential cell-size ratios are discontinuous."));
            }
        }
    }
    if (meshExtents.size() > 1) {
        std::vector<bool> connected(meshExtents.size(), false);
        std::vector<std::size_t> pending = {0};
        connected.front() = true;
        while (!pending.empty()) {
            const std::size_t current = pending.back();
            pending.pop_back();
            for (const std::size_t adjacent : meshAdjacency[current]) {
                if (!connected[adjacent]) {
                    connected[adjacent] = true;
                    pending.push_back(adjacent);
                }
            }
        }
        QSet<QString> reportedGapSources;
        for (std::size_t index = 1; index < meshExtents.size(); ++index) {
            if (connected[index] ||
                reportedGapSources.contains(meshExtents[index].sourceUuid)) {
                continue;
            }
            reportedGapSources.insert(meshExtents[index].sourceUuid);
            result.warnings.append(
                QStringLiteral("Mesh '") + meshExtents[index].sourceName +
                QStringLiteral("' [UUID ") + meshExtents[index].sourceUuid +
                QStringLiteral("] is disconnected from the first mesh group; "
                               "check for an unintended mesh gap."));
        }
    }
    for (const auto& object : namelists) {
        if (object->keyword() != QStringLiteral("SLCF") || meshExtents.empty()) continue;
        const QStringList planeKeys = {QStringLiteral("PBX"), QStringLiteral("PBY"),
                                       QStringLiteral("PBZ")};
        for (int axis = 0; axis < planeKeys.size(); ++axis) {
            const FcFdsParameter* plane = findParameter(*object, planeKeys.at(axis));
            if (!plane) continue;
            double coordinate = 0.0;
            if (!parseFiniteNumber(plane->value, coordinate)) continue;
            const bool inside = std::any_of(
                meshExtents.cbegin(), meshExtents.cend(),
                [axis, coordinate](const MeshExtent& mesh) {
                    return containsCoordinate(mesh, axis, coordinate);
                });
            if (!inside) {
                result.errors.append(
                    QStringLiteral("Namelist '") + object->name() +
                    QStringLiteral("' [UUID ") + object->id() +
                    QStringLiteral("]: SLCF parameter ") + planeKeys.at(axis) +
                    QStringLiteral("=") + plane->value +
                    QStringLiteral(" lies outside every expanded mesh extent."));
            }
        }
    }

    QSet<QString> surfaceIds;
    for (const auto& surface : surfaces) {
        surfaceIds.insert(surface->fdsId().trimmed().toUpper());
    }
    const auto validateSurfaceReference = [&surfaceIds, &result](const QString& owner,
                                                                 const QString& id) {
        const QString normalized = id.trimmed().toUpper();
        if (!normalized.isEmpty() && normalized != QStringLiteral("OPEN") &&
            !surfaceIds.contains(normalized)) {
            result.errors.append(owner + QStringLiteral(" references unknown surface '") +
                                 id + QStringLiteral("'."));
        }
    };
    for (const auto& obstruction : obstructions) {
        validateSurfaceReference(QStringLiteral("Obstruction '") + obstruction->name() +
                                     QStringLiteral("'"),
                                 obstruction->surfaceId());
    }
    for (const auto& vent : vents) {
        validateSurfaceReference(QStringLiteral("Vent '") + vent->name() +
                                     QStringLiteral("'"),
                                 vent->surfaceId());
    }

    if (!result.success()) {
        return result;
    }

    const auto appendLine = [&result](QString line, const QString& objectId,
                                      const std::vector<FdsSourceMapEntry>& fields = {}) {
        const int lineNumber = result.text.count(QLatin1Char('\n')) + 1;
        if (!objectId.isEmpty()) {
            result.sourceMap.push_back(
                {lineNumber, 0, static_cast<int>(line.size()), objectId, QString{}});
        }
        for (FdsSourceMapEntry field : fields) {
            field.line = lineNumber;
            result.sourceMap.push_back(std::move(field));
        }
        result.text += line;
        result.text += QLatin1Char('\n');
    };
    const auto appendMappedLine = [&appendLine](
                                      const QString& line, const QString& objectId,
                                      const QStringList& parameterKeys) {
        std::vector<FdsSourceMapEntry> fields;
        for (const QString& key : parameterKeys) {
            const int start = line.indexOf(key + QLatin1Char('='));
            if (start < 0) continue;
            int end = line.indexOf(QStringLiteral(", "), start);
            const int slash = line.indexOf(QStringLiteral(" /"), start);
            if (end < 0 || (slash >= 0 && slash < end)) end = slash;
            if (end < 0) end = line.size();
            fields.push_back({0, start, end, objectId, key});
        }
        appendLine(line, objectId, fields);
    };

    const FcSimulationParameters& simulation = project.simulationParameters();
    const auto appendSimulationTime = [&simulation](QString& line) {
        if (!simulation.timeConfigured) return;
        line += QStringLiteral(", T_BEGIN=") + number(simulation.startTime);
        if (simulation.initialTimeStep > 0.0) {
            line += QStringLiteral(", DT=") + number(simulation.initialTimeStep);
        }
    };
    const auto appendSimulationRecords = [&]() {
        if (simulation.environmentConfigured || simulation.restartEnabled) {
            QStringList values;
            if (simulation.environmentConfigured) {
                values << QStringLiteral("TMPA=%1").arg(
                              number(simulation.ambientTemperature))
                       << QStringLiteral("P_INF=%1").arg(
                              number(simulation.ambientPressure))
                       << QStringLiteral("GVEC=%1,%2,%3")
                              .arg(number(simulation.gravity[0]),
                                   number(simulation.gravity[1]),
                                   number(simulation.gravity[2]))
                       << QStringLiteral("HUMIDITY=%1").arg(
                              number(simulation.relativeHumidity))
                       << QStringLiteral("SIMULATION_MODE=%1").arg(
                              quoted(simulation.simulationMode))
                       << QStringLiteral("TURBULENCE_MODEL=%1").arg(
                              quoted(simulation.turbulenceModel));
            }
            if (simulation.restartEnabled) {
                values << QStringLiteral("RESTART=.TRUE.");
                if (!simulation.restartChid.trimmed().isEmpty()) {
                    values << QStringLiteral("RESTART_CHID=%1").arg(
                                  quoted(simulation.restartChid.trimmed()));
                }
            }
            appendLine(QStringLiteral("&MISC %1 /").arg(
                           values.join(QStringLiteral(", "))), {});
        }
        if (simulation.radiationConfigured) {
            appendLine(
                QStringLiteral("&RADI RADIATION=%1, NUMBER_RADIATION_ANGLES=%2 /")
                    .arg(simulation.radiationEnabled
                             ? QStringLiteral(".TRUE.")
                             : QStringLiteral(".FALSE."))
                    .arg(simulation.radiationAngles), {});
        }
        if (simulation.combustionConfigured) {
            QStringList values;
            if (!simulation.extinctionModel.trimmed().isEmpty()) {
                values << QStringLiteral("EXTINCTION_MODEL=%1").arg(
                              quoted(simulation.extinctionModel.trimmed()));
            }
            if (simulation.fixedMixTime > 0.0) {
                values << QStringLiteral("FIXED_MIX_TIME=%1").arg(
                              number(simulation.fixedMixTime));
            }
            if (!values.isEmpty()) {
                appendLine(QStringLiteral("&COMB %1 /").arg(
                               values.join(QStringLiteral(", "))), {});
            }
        }
        if (simulation.outputCadenceConfigured ||
            simulation.restartInterval > 0.0) {
            QStringList values;
            if (simulation.outputCadenceConfigured) {
                values << QStringLiteral("DT_DEVC=%1").arg(
                              number(simulation.deviceOutputInterval))
                       << QStringLiteral("DT_HRR=%1").arg(
                              number(simulation.hrrOutputInterval))
                       << QStringLiteral("DT_SLCF=%1").arg(
                              number(simulation.sliceOutputInterval))
                       << QStringLiteral("DT_BNDF=%1").arg(
                              number(simulation.boundaryOutputInterval))
                       << QStringLiteral("DT_PART=%1").arg(
                              number(simulation.particleOutputInterval));
            }
            if (simulation.restartInterval > 0.0) {
                values << QStringLiteral("DT_RESTART=%1").arg(
                              number(simulation.restartInterval));
            }
            appendLine(QStringLiteral("&DUMP %1 /").arg(
                           values.join(QStringLiteral(", "))), {});
        }
        if (simulation.windConfigured) {
            appendLine(
                QStringLiteral("&WIND SPEED=%1, DIRECTION=%2, Z_0=%3, Z_REF=%4 /")
                    .arg(number(simulation.windSpeed),
                         number(simulation.windDirection),
                         number(simulation.aerodynamicRoughness),
                         number(simulation.windReferenceHeight)), {});
        }
        if (simulation.initializationConfigured) {
            QStringList coordinates;
            for (double coordinate : simulation.initializationBounds)
                coordinates.append(number(coordinate));
            appendLine(
                QStringLiteral("&INIT XB=%1, TEMPERATURE=%2 /")
                    .arg(coordinates.join(QLatin1Char(',')),
                         number(simulation.initializationTemperature)), {});
        }
        if (simulation.numericsConfigured) {
            QString line = QStringLiteral("&PRES MAX_PRESSURE_ITERATIONS=%1")
                               .arg(simulation.maximumPressureIterations);
            if (simulation.velocityTolerance > 0.0) {
                line += QStringLiteral(", VELOCITY_TOLERANCE=") +
                        number(simulation.velocityTolerance);
            }
            line += QStringLiteral(" /");
            appendLine(line, {});
        }
    };

    appendLine(QStringLiteral("&HEAD CHID=%1, TITLE=%2 /")
                   .arg(quoted(effectiveChid), quoted(project.name())), {});

    const auto appendTypedMeshes = [&]() {
        for (const auto& mesh : meshes) {
            const auto& cells = mesh->cells();
            appendMappedLine(
                QStringLiteral("&MESH ID=%1, IJK=%2,%3,%4, XB=%5 /")
                    .arg(quoted(mesh->fdsId())).arg(cells[0]).arg(cells[1])
                    .arg(cells[2]).arg(bounds(mesh->bounds())),
                mesh->id(), {QStringLiteral("ID"), QStringLiteral("IJK"),
                             QStringLiteral("XB")});
        }
    };
    const auto appendTypedPhysicsAndOutputs = [&]() {
        for (const auto& reaction : reactions) {
            appendMappedLine(
                QStringLiteral("&REAC ID=%1, FUEL=%2, SOOT_YIELD=%3 /")
                    .arg(quoted(reaction->fdsId()), quoted(reaction->fuel()),
                         number(reaction->sootYield())),
                reaction->id(), {QStringLiteral("ID"), QStringLiteral("FUEL"),
                                 QStringLiteral("SOOT_YIELD")});
        }
        for (const auto& surface : surfaces) {
            QString line = QStringLiteral("&SURF ID=%1, HRRPUA=%2")
                               .arg(quoted(surface->fdsId()),
                                    number(surface->heatReleaseRatePerArea()));
            if (!surface->color().isEmpty()) {
                line += QStringLiteral(", COLOR=") + quoted(surface->color());
            }
            line += QStringLiteral(" /");
            appendMappedLine(line, surface->id(),
                             {QStringLiteral("ID"), QStringLiteral("HRRPUA"),
                              QStringLiteral("COLOR")});
        }
        for (const auto& obstruction : obstructions) {
            QString line = QStringLiteral("&OBST ID=%1, XB=%2")
                               .arg(quoted(obstruction->fdsId()),
                                    bounds(obstruction->bounds()));
            if (!obstruction->surfaceId().isEmpty()) {
                line += QStringLiteral(", SURF_ID=") +
                        quoted(obstruction->surfaceId());
            }
            line += QStringLiteral(" /");
            appendMappedLine(line, obstruction->id(),
                             {QStringLiteral("ID"), QStringLiteral("XB"),
                              QStringLiteral("SURF_ID")});
        }
        for (const auto& vent : vents) {
            appendMappedLine(
                QStringLiteral("&VENT ID=%1, XB=%2, SURF_ID=%3 /")
                    .arg(quoted(vent->fdsId()), bounds(vent->bounds()),
                         quoted(vent->surfaceId())),
                vent->id(), {QStringLiteral("ID"), QStringLiteral("XB"),
                             QStringLiteral("SURF_ID")});
        }
        for (const auto& output : outputs) {
            if (output->kind() == FcFdsOutputKind::Boundary) {
                appendMappedLine(
                    QStringLiteral("&BNDF QUANTITY=%1 /")
                        .arg(quoted(output->quantity())),
                    output->id(), {QStringLiteral("QUANTITY")});
                continue;
            }
            const char axis = output->planeAxis() == FcFdsPlaneAxis::X
                                  ? 'X'
                                  : output->planeAxis() == FcFdsPlaneAxis::Y ? 'Y' : 'Z';
            const QString planeKey = QStringLiteral("PB") + QChar::fromLatin1(axis);
            QString line = QStringLiteral("&SLCF %1=%2, QUANTITY=%3")
                               .arg(planeKey, number(output->planeValue()),
                                    quoted(output->quantity()));
            if (output->vectorOutput()) line += QStringLiteral(", VECTOR=.TRUE.");
            line += QStringLiteral(" /");
            appendMappedLine(line, output->id(),
                             {planeKey, QStringLiteral("QUANTITY"),
                              QStringLiteral("VECTOR")});
        }
    };

    // Imported projects use generic namelist objects.  They are emitted in
    // source order so round-tripping an official FDS case preserves semantics.
    // HEAD/TIME ownership remains on FcProject and is emitted canonically.
    if (!namelists.empty()) {
        // Professional assistants create generic records even in projects that
        // still contain the original strongly typed objects.  Preserve both
        // object families; entering generic mode must never discard the mesh,
        // fire source, geometry, vents, or outputs already in the project.
        appendTypedMeshes();
        appendTypedPhysicsAndOutputs();
        for (const auto& object : namelists) {
            const QString keyword = object->keyword();
            if (keyword == QStringLiteral("HEAD") || keyword == QStringLiteral("TIME") ||
                keyword == QStringLiteral("TAIL")) {
                continue;
            }
            QString line = QLatin1Char('&') + keyword;
            std::vector<FdsSourceMapEntry> fields;
            if (!object->fdsId().isEmpty()) {
                const int start = line.size() + 1;
                line += QStringLiteral(" ID=") + quoted(object->fdsId());
                fields.push_back({0, start, static_cast<int>(line.size()), object->id(),
                                  QStringLiteral("ID")});
            }
            bool hasParameter = !object->fdsId().isEmpty();
            for (const FcFdsParameter& parameter : object->parameters()) {
                line += hasParameter ? QStringLiteral(", ") : QStringLiteral(" ");
                const int start = line.size();
                line += parameter.key + QLatin1Char('=') +
                        renderGenericValue(parameter, *document, result.errors,
                                           object->name());
                fields.push_back({0, start, static_cast<int>(line.size()), object->id(),
                                  parameter.key});
                hasParameter = true;
            }
            line += QStringLiteral(" /");
            appendLine(line, object->id(), fields);
        }
        appendSimulationRecords();
        QString timeLine = QStringLiteral("&TIME T_END=") + number(project.endTime());
        std::vector<FdsSourceMapEntry> timeFields;
        for (const auto& object : namelists) {
            if (object->keyword() != QStringLiteral("TIME")) continue;
            for (const FcFdsParameter& parameter : object->parameters()) {
                timeLine += QStringLiteral(", ");
                const int start = timeLine.size();
                timeLine += parameter.key + QLatin1Char('=') +
                            renderGenericValue(parameter, *document, result.errors,
                                               object->name());
                timeFields.push_back({0, start, static_cast<int>(timeLine.size()), object->id(),
                                      parameter.key});
            }
        }
        // Professional project settings are appended last so they explicitly
        // override an imported TIME value while preserving unknown fields.
        appendSimulationTime(timeLine);
        timeLine += QStringLiteral(" /");
        appendLine(timeLine, {}, timeFields);
        appendLine(QStringLiteral("&TAIL /"), {});
        return result;
    }

    appendTypedMeshes();
    QString timeLine = QStringLiteral("&TIME T_END=%1").arg(number(project.endTime()));
    appendSimulationTime(timeLine);
    timeLine += QStringLiteral(" /");
    appendLine(timeLine, {});
    appendSimulationRecords();
    appendTypedPhysicsAndOutputs();
    appendLine(QStringLiteral("&TAIL /"), {});
    return result;
}

bool FdsWriter::writeFile(const FcProject& project,
                          const QString& filePath,
                          QString* errorMessage)
{
    const FdsWriteResult result = render(project);
    if (!result.success()) {
        if (errorMessage) {
            *errorMessage = result.errors.join(QLatin1Char('\n'));
        }
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create output directory: ") +
                            fileInfo.absolutePath();
        }
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    if (file.write(result.text.toUtf8()) < 0 || !file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}
