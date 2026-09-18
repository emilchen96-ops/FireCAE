#include "fds/FcProjectSerializer.h"

#include "core/FcDocument.h"
#include "core/FcFloorObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsSchema.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "results/FcResultCase.h"
#include "results/FcResultFile.h"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <array>
#include <algorithm>
#include <memory>
#include <sstream>

namespace
{
constexpr auto kFormatName = "FireCAEProject";

QByteArray serializeShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return {};
    std::ostringstream stream(std::ios::out | std::ios::binary);
    BRepTools::Write(shape, stream);
    const std::string data = stream.str();
    return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
}

bool restoreShape(const QByteArray& data, TopoDS_Shape& shape)
{
    if (data.isEmpty()) return false;
    std::istringstream stream(
        std::string(data.constData(), static_cast<std::size_t>(data.size())),
        std::ios::in | std::ios::binary);
    BRep_Builder builder;
    BRepTools::Read(shape, stream, builder);
    return !shape.IsNull();
}

QJsonArray boundsToJson(const FcFdsBounds& bounds)
{
    return {bounds.xMin, bounds.xMax, bounds.yMin,
            bounds.yMax, bounds.zMin, bounds.zMax};
}

bool boundsFromJson(const QJsonValue& value, FcFdsBounds& bounds)
{
    const QJsonArray values = value.toArray();
    if (values.size() != 6) return false;
    for (const QJsonValue& coordinate : values) {
        if (!coordinate.isDouble()) return false;
    }
    bounds = {values[0].toDouble(), values[1].toDouble(),
              values[2].toDouble(), values[3].toDouble(),
              values[4].toDouble(), values[5].toDouble()};
    return true;
}

template <std::size_t Size>
QJsonArray numericArrayToJson(const std::array<double, Size>& values)
{
    QJsonArray result;
    for (double value : values) result.append(value);
    return result;
}

template <std::size_t Size>
void numericArrayFromJson(const QJsonValue& value,
                          std::array<double, Size>& target)
{
    const QJsonArray values = value.toArray();
    if (values.size() != static_cast<int>(Size)) return;
    for (int index = 0; index < values.size(); ++index) {
        if (!values[index].isDouble()) return;
    }
    for (int index = 0; index < values.size(); ++index)
        target[static_cast<std::size_t>(index)] = values[index].toDouble();
}

QJsonObject simulationParametersToJson(const FcSimulationParameters& value)
{
    QJsonObject json;
    json.insert(QStringLiteral("timeConfigured"), value.timeConfigured);
    json.insert(QStringLiteral("startTime"), value.startTime);
    json.insert(QStringLiteral("initialTimeStep"), value.initialTimeStep);
    json.insert(QStringLiteral("environmentConfigured"), value.environmentConfigured);
    json.insert(QStringLiteral("ambientTemperature"), value.ambientTemperature);
    json.insert(QStringLiteral("ambientPressure"), value.ambientPressure);
    json.insert(QStringLiteral("gravity"), numericArrayToJson(value.gravity));
    json.insert(QStringLiteral("relativeHumidity"), value.relativeHumidity);
    json.insert(QStringLiteral("simulationMode"), value.simulationMode);
    json.insert(QStringLiteral("turbulenceModel"), value.turbulenceModel);
    json.insert(QStringLiteral("radiationConfigured"), value.radiationConfigured);
    json.insert(QStringLiteral("radiationEnabled"), value.radiationEnabled);
    json.insert(QStringLiteral("radiationAngles"), value.radiationAngles);
    json.insert(QStringLiteral("combustionConfigured"), value.combustionConfigured);
    json.insert(QStringLiteral("extinctionModel"), value.extinctionModel);
    json.insert(QStringLiteral("fixedMixTime"), value.fixedMixTime);
    json.insert(QStringLiteral("outputCadenceConfigured"),
                value.outputCadenceConfigured);
    json.insert(QStringLiteral("deviceOutputInterval"), value.deviceOutputInterval);
    json.insert(QStringLiteral("hrrOutputInterval"), value.hrrOutputInterval);
    json.insert(QStringLiteral("sliceOutputInterval"), value.sliceOutputInterval);
    json.insert(QStringLiteral("boundaryOutputInterval"),
                value.boundaryOutputInterval);
    json.insert(QStringLiteral("particleOutputInterval"),
                value.particleOutputInterval);
    json.insert(QStringLiteral("windConfigured"), value.windConfigured);
    json.insert(QStringLiteral("windSpeed"), value.windSpeed);
    json.insert(QStringLiteral("windDirection"), value.windDirection);
    json.insert(QStringLiteral("aerodynamicRoughness"),
                value.aerodynamicRoughness);
    json.insert(QStringLiteral("windReferenceHeight"),
                value.windReferenceHeight);
    json.insert(QStringLiteral("initializationConfigured"),
                value.initializationConfigured);
    json.insert(QStringLiteral("initializationBounds"),
                numericArrayToJson(value.initializationBounds));
    json.insert(QStringLiteral("initializationTemperature"),
                value.initializationTemperature);
    json.insert(QStringLiteral("restartEnabled"), value.restartEnabled);
    json.insert(QStringLiteral("restartChid"), value.restartChid);
    json.insert(QStringLiteral("restartInterval"), value.restartInterval);
    json.insert(QStringLiteral("numericsConfigured"), value.numericsConfigured);
    json.insert(QStringLiteral("maximumPressureIterations"),
                value.maximumPressureIterations);
    json.insert(QStringLiteral("velocityTolerance"), value.velocityTolerance);
    return json;
}

FcSimulationParameters simulationParametersFromJson(const QJsonObject& json)
{
    FcSimulationParameters value;
    if (json.isEmpty()) return value;
    value.timeConfigured = json.value(QStringLiteral("timeConfigured")).toBool();
    value.startTime = json.value(QStringLiteral("startTime")).toDouble(value.startTime);
    value.initialTimeStep =
        json.value(QStringLiteral("initialTimeStep")).toDouble(value.initialTimeStep);
    value.environmentConfigured =
        json.value(QStringLiteral("environmentConfigured")).toBool();
    value.ambientTemperature = json.value(QStringLiteral("ambientTemperature"))
                                   .toDouble(value.ambientTemperature);
    value.ambientPressure = json.value(QStringLiteral("ambientPressure"))
                                .toDouble(value.ambientPressure);
    numericArrayFromJson(json.value(QStringLiteral("gravity")), value.gravity);
    value.relativeHumidity = json.value(QStringLiteral("relativeHumidity"))
                                 .toDouble(value.relativeHumidity);
    value.simulationMode = json.value(QStringLiteral("simulationMode"))
                               .toString(value.simulationMode);
    value.turbulenceModel = json.value(QStringLiteral("turbulenceModel"))
                                .toString(value.turbulenceModel);
    value.radiationConfigured =
        json.value(QStringLiteral("radiationConfigured")).toBool();
    value.radiationEnabled =
        json.value(QStringLiteral("radiationEnabled")).toBool(value.radiationEnabled);
    value.radiationAngles =
        json.value(QStringLiteral("radiationAngles")).toInt(value.radiationAngles);
    value.combustionConfigured =
        json.value(QStringLiteral("combustionConfigured")).toBool();
    value.extinctionModel =
        json.value(QStringLiteral("extinctionModel")).toString();
    value.fixedMixTime =
        json.value(QStringLiteral("fixedMixTime")).toDouble(value.fixedMixTime);
    value.outputCadenceConfigured =
        json.value(QStringLiteral("outputCadenceConfigured")).toBool();
    value.deviceOutputInterval = json.value(QStringLiteral("deviceOutputInterval"))
                                     .toDouble(value.deviceOutputInterval);
    value.hrrOutputInterval = json.value(QStringLiteral("hrrOutputInterval"))
                                  .toDouble(value.hrrOutputInterval);
    value.sliceOutputInterval = json.value(QStringLiteral("sliceOutputInterval"))
                                    .toDouble(value.sliceOutputInterval);
    value.boundaryOutputInterval =
        json.value(QStringLiteral("boundaryOutputInterval"))
            .toDouble(value.boundaryOutputInterval);
    value.particleOutputInterval =
        json.value(QStringLiteral("particleOutputInterval"))
            .toDouble(value.particleOutputInterval);
    value.windConfigured = json.value(QStringLiteral("windConfigured")).toBool();
    value.windSpeed = json.value(QStringLiteral("windSpeed")).toDouble(value.windSpeed);
    value.windDirection =
        json.value(QStringLiteral("windDirection")).toDouble(value.windDirection);
    value.aerodynamicRoughness =
        json.value(QStringLiteral("aerodynamicRoughness"))
            .toDouble(value.aerodynamicRoughness);
    value.windReferenceHeight =
        json.value(QStringLiteral("windReferenceHeight"))
            .toDouble(value.windReferenceHeight);
    value.initializationConfigured =
        json.value(QStringLiteral("initializationConfigured")).toBool();
    numericArrayFromJson(json.value(QStringLiteral("initializationBounds")),
                         value.initializationBounds);
    value.initializationTemperature =
        json.value(QStringLiteral("initializationTemperature"))
            .toDouble(value.initializationTemperature);
    value.restartEnabled = json.value(QStringLiteral("restartEnabled")).toBool();
    value.restartChid = json.value(QStringLiteral("restartChid")).toString();
    value.restartInterval = json.value(QStringLiteral("restartInterval"))
                                .toDouble(value.restartInterval);
    value.numericsConfigured =
        json.value(QStringLiteral("numericsConfigured")).toBool();
    value.maximumPressureIterations =
        json.value(QStringLiteral("maximumPressureIterations"))
            .toInt(value.maximumPressureIterations);
    value.velocityTolerance = json.value(QStringLiteral("velocityTolerance"))
                                  .toDouble(value.velocityTolerance);
    return value;
}

QJsonObject scenarioToJson(const FcScenario& scenario)
{
    QJsonObject json;
    json.insert(QStringLiteral("uuid"), scenario.id);
    json.insert(QStringLiteral("name"), scenario.name);
    json.insert(QStringLiteral("chid"), scenario.chid);
    json.insert(QStringLiteral("outputDirectory"),
                QDir::fromNativeSeparators(scenario.outputDirectory));
    json.insert(QStringLiteral("solverBackendId"), scenario.solverBackendId);
    json.insert(QStringLiteral("processCount"), qBound(1, scenario.processCount, 1024));
    QStringList disabled(scenario.disabledObjectIds.cbegin(),
                         scenario.disabledObjectIds.cend());
    std::sort(disabled.begin(), disabled.end());
    QJsonArray disabledJson;
    for (const QString& id : disabled) disabledJson.append(id);
    json.insert(QStringLiteral("disabledObjectUuids"), disabledJson);
    QJsonArray overrides;
    for (const FcScenarioParameterOverride& entry : scenario.parameterOverrides) {
        QJsonObject item;
        item.insert(QStringLiteral("objectUuid"), entry.objectId);
        item.insert(QStringLiteral("parameter"), entry.parameterKey);
        item.insert(QStringLiteral("value"), entry.value);
        item.insert(QStringLiteral("reference"), entry.reference);
        QJsonArray targets;
        for (const QString& target : entry.targetObjectIds) targets.append(target);
        item.insert(QStringLiteral("targetUuids"), targets);
        overrides.append(item);
    }
    json.insert(QStringLiteral("parameterOverrides"), overrides);
    return json;
}

FcScenario scenarioFromJson(const QJsonObject& json)
{
    FcScenario scenario;
    scenario.id = json.value(QStringLiteral("uuid")).toString();
    scenario.name = json.value(QStringLiteral("name")).toString();
    scenario.chid = json.value(QStringLiteral("chid")).toString();
    scenario.outputDirectory = QDir::toNativeSeparators(
        json.value(QStringLiteral("outputDirectory")).toString());
    scenario.solverBackendId = json.value(QStringLiteral("solverBackendId"))
                                   .toString(QStringLiteral("fds.serial.cpu"));
    scenario.processCount = qBound(
        1, json.value(QStringLiteral("processCount")).toInt(1), 1024);
    for (const QJsonValue& value :
         json.value(QStringLiteral("disabledObjectUuids")).toArray()) {
        if (value.isString()) scenario.disabledObjectIds.insert(value.toString());
    }
    for (const QJsonValue& value :
         json.value(QStringLiteral("parameterOverrides")).toArray()) {
        const QJsonObject item = value.toObject();
        FcScenarioParameterOverride entry;
        entry.objectId = item.value(QStringLiteral("objectUuid")).toString();
        entry.parameterKey = item.value(QStringLiteral("parameter")).toString();
        entry.value = item.value(QStringLiteral("value")).toString();
        entry.reference = item.value(QStringLiteral("reference")).toBool(false);
        for (const QJsonValue& target :
             item.value(QStringLiteral("targetUuids")).toArray()) {
            if (target.isString()) entry.targetObjectIds.append(target.toString());
        }
        if (!entry.objectId.isEmpty() && !entry.parameterKey.trimmed().isEmpty()) {
            scenario.parameterOverrides.append(entry);
        }
    }
    return scenario;
}

QJsonObject serializeObject(const FcObject::Ptr& object)
{
    QJsonObject json;
    json.insert(QStringLiteral("uuid"), object->id());
    json.insert(QStringLiteral("name"), object->name());
    json.insert(QStringLiteral("objectType"), static_cast<int>(object->type()));
    json.insert(QStringLiteral("visible"), object->isVisible());
    json.insert(QStringLiteral("locked"), object->isLocked());
    json.insert(QStringLiteral("floor"), object->floorName());
    QJsonArray tags;
    for (const QString& tag : object->tags()) tags.append(tag);
    json.insert(QStringLiteral("tags"), tags);

    if (const auto floor = std::dynamic_pointer_cast<FcFloorObject>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Floor"));
        json.insert(QStringLiteral("baseElevation"), floor->baseElevation());
        json.insert(QStringLiteral("defaultStoreyHeight"), floor->defaultStoreyHeight());
        json.insert(QStringLiteral("defaultSlabThickness"), floor->defaultSlabThickness());
        json.insert(QStringLiteral("defaultWallHeight"), floor->defaultWallHeight());
        json.insert(QStringLiteral("backgroundImage"),
                    QDir::fromNativeSeparators(floor->backgroundImagePath()));
        json.insert(QStringLiteral("clippingEnabled"), floor->clippingEnabled());
        const FcFloorClipRange& clip = floor->clippingRange();
        json.insert(QStringLiteral("clippingRange"),
                    QJsonArray{clip.xMin, clip.xMax, clip.yMin, clip.yMax,
                               clip.zMin, clip.zMax});
    } else if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Ifc"));
        json.insert(QStringLiteral("ifcClass"), ifc->ifcClass());
        json.insert(QStringLiteral("globalId"), ifc->globalId());
        json.insert(QStringLiteral("description"), ifc->description());
        json.insert(QStringLiteral("sourceFile"),
                    QDir::fromNativeSeparators(ifc->sourceFile()));
        json.insert(QStringLiteral("schema"), ifc->schema());
        json.insert(QStringLiteral("fdsConversionRoute"),
                    ifc->fdsConversionRoute());
        if (ifc->hasShape()) {
            json.insert(QStringLiteral("shapeBrepBase64"),
                        QString::fromLatin1(serializeShape(ifc->shape()).toBase64()));
        }
    } else if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Geometry"));
        if (geometry->hasShape()) {
            json.insert(QStringLiteral("shapeBrepBase64"),
                        QString::fromLatin1(geometry->shapeData().toBase64()));
        }
        json.insert(QStringLiteral("geometryKind"),
                    fcGeometryKindName(geometry->geometryKind()));
        json.insert(QStringLiteral("geometryParameters"),
                    QJsonObject::fromVariantMap(geometry->geometryParameters()));
        json.insert(QStringLiteral("hostObjectUuid"), geometry->hostObjectId());
        json.insert(QStringLiteral("controlObjectUuid"), geometry->controlObjectId());
        json.insert(QStringLiteral("dynamicOpening"), geometry->isDynamicOpening());
        json.insert(QStringLiteral("defaultSurfaceUuid"), geometry->defaultSurfaceId());
        QJsonObject faceSurfaces;
        for (auto iterator = geometry->faceSurfaceIds().cbegin();
             iterator != geometry->faceSurfaceIds().cend(); ++iterator) {
            faceSurfaces.insert(iterator.key(), iterator.value());
        }
        json.insert(QStringLiteral("faceSurfaceUuids"), faceSurfaces);
    } else if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("FdsNamelist"));
        json.insert(QStringLiteral("keyword"), namelist->keyword());
        json.insert(QStringLiteral("fdsId"), namelist->fdsId());
        json.insert(QStringLiteral("sequenceIndex"), namelist->sequenceIndex());
        QJsonArray parameters;
        for (const FcFdsParameter& parameter : namelist->parameters()) {
            QJsonObject parameterJson;
            parameterJson.insert(QStringLiteral("key"), parameter.key);
            parameterJson.insert(QStringLiteral("kind"), static_cast<int>(parameter.kind));
            parameterJson.insert(QStringLiteral("value"), parameter.value);
            QJsonArray targets;
            for (const QString& target : parameter.targetObjectIds) {
                targets.append(target);
            }
            parameterJson.insert(QStringLiteral("targets"), targets);
            parameters.append(parameterJson);
        }
        json.insert(QStringLiteral("parameters"), parameters);
    } else if (const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Mesh"));
        json.insert(QStringLiteral("fdsId"), mesh->fdsId());
        json.insert(QStringLiteral("cells"),
                    QJsonArray{mesh->cells()[0], mesh->cells()[1], mesh->cells()[2]});
        json.insert(QStringLiteral("bounds"), boundsToJson(mesh->bounds()));
    } else if (const auto reaction = std::dynamic_pointer_cast<FcFdsReaction>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Reaction"));
        json.insert(QStringLiteral("fdsId"), reaction->fdsId());
        json.insert(QStringLiteral("fuel"), reaction->fuel());
        json.insert(QStringLiteral("sootYield"), reaction->sootYield());
    } else if (const auto surface = std::dynamic_pointer_cast<FcFdsSurface>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Surface"));
        json.insert(QStringLiteral("fdsId"), surface->fdsId());
        json.insert(QStringLiteral("hrrpua"), surface->heatReleaseRatePerArea());
        json.insert(QStringLiteral("color"), surface->color());
    } else if (const auto obstruction =
                   std::dynamic_pointer_cast<FcFdsObstruction>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Obstruction"));
        json.insert(QStringLiteral("fdsId"), obstruction->fdsId());
        json.insert(QStringLiteral("bounds"), boundsToJson(obstruction->bounds()));
        json.insert(QStringLiteral("surfaceId"), obstruction->surfaceId());
    } else if (const auto vent = std::dynamic_pointer_cast<FcFdsVent>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Vent"));
        json.insert(QStringLiteral("fdsId"), vent->fdsId());
        json.insert(QStringLiteral("bounds"), boundsToJson(vent->bounds()));
        json.insert(QStringLiteral("surfaceId"), vent->surfaceId());
    } else if (const auto output = std::dynamic_pointer_cast<FcFdsOutput>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Output"));
        json.insert(QStringLiteral("fdsId"), output->fdsId());
        json.insert(QStringLiteral("outputKind"), static_cast<int>(output->kind()));
        json.insert(QStringLiteral("quantity"), output->quantity());
        json.insert(QStringLiteral("planeAxis"), static_cast<int>(output->planeAxis()));
        json.insert(QStringLiteral("planeValue"), output->planeValue());
        json.insert(QStringLiteral("vectorOutput"), output->vectorOutput());
    } else if (const auto result = std::dynamic_pointer_cast<FcResultCase>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("ResultCase"));
        json.insert(QStringLiteral("resultDirectory"), QDir::fromNativeSeparators(result->resultDirectory()));
        json.insert(QStringLiteral("smvFilePath"), QDir::fromNativeSeparators(result->smvFilePath()));
        json.insert(QStringLiteral("fdsInputFilePath"), QDir::fromNativeSeparators(result->fdsInputFilePath()));
        json.insert(QStringLiteral("status"), static_cast<int>(result->status()));
        json.insert(QStringLiteral("resultFileCount"), result->resultFileCount());
        json.insert(QStringLiteral("startTime"), result->startTime());
        json.insert(QStringLiteral("endTime"), result->endTime());
        json.insert(QStringLiteral("lastScanTime"), result->lastScanTime().toString(Qt::ISODateWithMs));
        json.insert(QStringLiteral("warningCount"), result->warningCount());
    } else if (const auto file = std::dynamic_pointer_cast<FcResultFile>(object)) {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("ResultFile"));
        json.insert(QStringLiteral("fileType"), static_cast<int>(file->fileType()));
        json.insert(QStringLiteral("filePath"), QDir::fromNativeSeparators(file->filePath()));
        // Decimal strings preserve all qint64 values beyond JSON's exact integer range.
        json.insert(QStringLiteral("fileSize"), QString::number(file->fileSize()));
        json.insert(QStringLiteral("dataRowCount"), QString::number(file->dataRowCount()));
        json.insert(QStringLiteral("exists"), file->exists());
        json.insert(QStringLiteral("lastModified"), file->lastModified().toString(Qt::ISODateWithMs));
        json.insert(QStringLiteral("columnCount"), file->columnCount());
        json.insert(QStringLiteral("startTime"), file->startTime());
        json.insert(QStringLiteral("endTime"), file->endTime());
        json.insert(QStringLiteral("columnNames"), QJsonArray::fromStringList(file->columnNames()));
    } else {
        json.insert(QStringLiteral("storageClass"), QStringLiteral("Generic"));
    }

    QJsonArray children;
    for (const FcObject::Ptr& child : object->children()) {
        if (child) children.append(serializeObject(child));
    }
    json.insert(QStringLiteral("children"), children);
    return json;
}

bool requireString(const QJsonObject& json, const QString& key,
                   QString& value, QString& error)
{
    if (!json.value(key).isString()) {
        error = QStringLiteral("Missing or invalid string field '%1'.").arg(key);
        return false;
    }
    value = json.value(key).toString();
    return true;
}

FcObject::Ptr deserializeObject(const QJsonObject& json,
                                QSet<QString>& restoredIds,
                                QString& error)
{
    QString uuid;
    QString name;
    QString storageClass;
    if (!requireString(json, QStringLiteral("uuid"), uuid, error) ||
        !requireString(json, QStringLiteral("name"), name, error) ||
        !requireString(json, QStringLiteral("storageClass"), storageClass, error) ||
        !json.value(QStringLiteral("objectType")).isDouble()) {
        if (error.isEmpty()) error = QStringLiteral("Object type is missing or invalid.");
        return {};
    }
    const FcObjectType type = static_cast<FcObjectType>(
        json.value(QStringLiteral("objectType")).toInt());
    const QString fdsId = json.value(QStringLiteral("fdsId")).toString();
    FcObject::Ptr object;

    if (storageClass == QStringLiteral("Floor")) {
        auto floor = std::make_shared<FcFloorObject>(name);
        floor->setBaseElevation(
            json.value(QStringLiteral("baseElevation")).toDouble(0.0));
        floor->setDefaultStoreyHeight(
            json.value(QStringLiteral("defaultStoreyHeight")).toDouble(3.0));
        floor->setDefaultSlabThickness(
            json.value(QStringLiteral("defaultSlabThickness")).toDouble(0.2));
        floor->setDefaultWallHeight(
            json.value(QStringLiteral("defaultWallHeight")).toDouble(3.0));
        floor->setBackgroundImagePath(QDir::toNativeSeparators(
            json.value(QStringLiteral("backgroundImage")).toString()));
        floor->setClippingEnabled(
            json.value(QStringLiteral("clippingEnabled")).toBool(false));
        const QJsonArray clip = json.value(QStringLiteral("clippingRange")).toArray();
        if (clip.size() == 6) {
            floor->setClippingRange({clip[0].toDouble(), clip[1].toDouble(),
                                     clip[2].toDouble(), clip[3].toDouble(),
                                     clip[4].toDouble(), clip[5].toDouble()});
        }
        object = floor;
    } else if (storageClass == QStringLiteral("Ifc")) {
        auto ifc = std::make_shared<FcIfcObject>(
            name,
            json.value(QStringLiteral("ifcClass")).toString(),
            json.value(QStringLiteral("globalId")).toString(),
            type == FcObjectType::IfcModel);
        ifc->setDescription(json.value(QStringLiteral("description")).toString());
        ifc->setSourceFile(QDir::toNativeSeparators(
            json.value(QStringLiteral("sourceFile")).toString()));
        ifc->setSchema(json.value(QStringLiteral("schema")).toString());
        ifc->setFdsConversionRoute(
            json.value(QStringLiteral("fdsConversionRoute"))
                .toString(QStringLiteral("REFERENCE")));
        const QByteArray data = QByteArray::fromBase64(
            json.value(QStringLiteral("shapeBrepBase64")).toString().toLatin1());
        if (!data.isEmpty()) {
            TopoDS_Shape shape;
            if (!restoreShape(data, shape)) {
                error = QStringLiteral("IFC object '%1' contains invalid BREP data.").arg(name);
                return {};
            }
            ifc->setShape(shape);
        }
        object = ifc;
    } else if (storageClass == QStringLiteral("Geometry")) {
        auto geometry = std::make_shared<FcGeometryObject>(name);
        const QByteArray data = QByteArray::fromBase64(
            json.value(QStringLiteral("shapeBrepBase64")).toString().toLatin1());
        if (!data.isEmpty() && !geometry->restoreShapeData(data)) {
            error = QStringLiteral("Geometry '%1' contains invalid BREP data.").arg(name);
            return {};
        }
        geometry->setGeometryKind(fcGeometryKindFromName(
            json.value(QStringLiteral("geometryKind")).toString()));
        geometry->setGeometryParameters(
            json.value(QStringLiteral("geometryParameters")).toObject().toVariantMap());
        geometry->setHostObjectId(
            json.value(QStringLiteral("hostObjectUuid")).toString());
        geometry->setControlObjectId(
            json.value(QStringLiteral("controlObjectUuid")).toString());
        geometry->setDynamicOpening(
            json.value(QStringLiteral("dynamicOpening")).toBool(false));
        geometry->setDefaultSurfaceId(
            json.value(QStringLiteral("defaultSurfaceUuid")).toString());
        QMap<QString, QString> faceSurfaces;
        const QJsonObject assignments =
            json.value(QStringLiteral("faceSurfaceUuids")).toObject();
        for (auto iterator = assignments.constBegin();
             iterator != assignments.constEnd(); ++iterator) {
            faceSurfaces.insert(iterator.key(), iterator.value().toString());
        }
        geometry->setFaceSurfaceIds(faceSurfaces);
        object = geometry;
    } else if (storageClass == QStringLiteral("FdsNamelist")) {
        QString keyword;
        if (!requireString(json, QStringLiteral("keyword"), keyword, error)) return {};
        auto namelist = std::make_shared<FcFdsNamelist>(
            name, type, keyword, fdsId,
            json.value(QStringLiteral("sequenceIndex")).toInt());
        std::vector<FcFdsParameter> parameters;
        for (const QJsonValue& parameterValue :
             json.value(QStringLiteral("parameters")).toArray()) {
            const QJsonObject parameterJson = parameterValue.toObject();
            FcFdsParameter parameter;
            parameter.key = parameterJson.value(QStringLiteral("key")).toString();
            parameter.kind = static_cast<FcFdsParameterKind>(
                parameterJson.value(QStringLiteral("kind")).toInt());
            parameter.value = parameterJson.value(QStringLiteral("value")).toString();
            for (const QJsonValue& target :
                 parameterJson.value(QStringLiteral("targets")).toArray()) {
                parameter.targetObjectIds.append(target.toString());
            }
            parameters.push_back(parameter);
        }
        namelist->setParameters(parameters);
        object = namelist;
    } else if (storageClass == QStringLiteral("Mesh")) {
        const QJsonArray cells = json.value(QStringLiteral("cells")).toArray();
        FcFdsBounds bounds;
        if (cells.size() != 3 || !boundsFromJson(json.value(QStringLiteral("bounds")), bounds)) {
            error = QStringLiteral("Mesh cells or bounds are invalid.");
            return {};
        }
        object = std::make_shared<FcFdsMesh>(
            name, fdsId,
            std::array<int, 3>{cells[0].toInt(), cells[1].toInt(), cells[2].toInt()},
            bounds);
    } else if (storageClass == QStringLiteral("Reaction")) {
        object = std::make_shared<FcFdsReaction>(
            name, fdsId, json.value(QStringLiteral("fuel")).toString(),
            json.value(QStringLiteral("sootYield")).toDouble());
    } else if (storageClass == QStringLiteral("Surface")) {
        object = std::make_shared<FcFdsSurface>(
            name, fdsId, json.value(QStringLiteral("hrrpua")).toDouble(),
            json.value(QStringLiteral("color")).toString());
    } else if (storageClass == QStringLiteral("Obstruction")) {
        FcFdsBounds bounds;
        if (!boundsFromJson(json.value(QStringLiteral("bounds")), bounds)) {
            error = QStringLiteral("Obstruction bounds are invalid.");
            return {};
        }
        object = std::make_shared<FcFdsObstruction>(
            name, fdsId, bounds, json.value(QStringLiteral("surfaceId")).toString());
    } else if (storageClass == QStringLiteral("Vent")) {
        FcFdsBounds bounds;
        if (!boundsFromJson(json.value(QStringLiteral("bounds")), bounds)) {
            error = QStringLiteral("Vent bounds are invalid.");
            return {};
        }
        object = std::make_shared<FcFdsVent>(
            name, fdsId, bounds, json.value(QStringLiteral("surfaceId")).toString());
    } else if (storageClass == QStringLiteral("Output")) {
        const auto kind = static_cast<FcFdsOutputKind>(
            json.value(QStringLiteral("outputKind")).toInt());
        if (kind == FcFdsOutputKind::Boundary) {
            object = FcFdsOutput::boundary(
                name, fdsId, json.value(QStringLiteral("quantity")).toString());
        } else {
            object = FcFdsOutput::slice(
                name, fdsId,
                static_cast<FcFdsPlaneAxis>(json.value(QStringLiteral("planeAxis")).toInt()),
                json.value(QStringLiteral("planeValue")).toDouble(),
                json.value(QStringLiteral("quantity")).toString(),
                json.value(QStringLiteral("vectorOutput")).toBool());
        }
    } else if (storageClass == QStringLiteral("ResultCase")) {
        auto result = std::make_shared<FcResultCase>(name);
        result->updateMetadata(
            QDir::toNativeSeparators(json.value(QStringLiteral("resultDirectory")).toString()),
            QDir::toNativeSeparators(json.value(QStringLiteral("smvFilePath")).toString()),
            QDir::toNativeSeparators(json.value(QStringLiteral("fdsInputFilePath")).toString()),
            static_cast<FcResultStatus>(qBound(0, json.value(QStringLiteral("status")).toInt(),
                                               static_cast<int>(FcResultStatus::MissingFiles))),
            json.value(QStringLiteral("resultFileCount")).toInt(),
            json.value(QStringLiteral("startTime")).toString(),
            json.value(QStringLiteral("endTime")).toString(),
            QDateTime::fromString(json.value(QStringLiteral("lastScanTime")).toString(), Qt::ISODateWithMs),
            json.value(QStringLiteral("warningCount")).toInt());
        object = result;
    } else if (storageClass == QStringLiteral("ResultFile")) {
        QStringList columnNames;
        for (const QJsonValue& column : json.value(QStringLiteral("columnNames")).toArray()) {
            columnNames.append(column.toString());
        }
        object = std::make_shared<FcResultFile>(
            name,
            static_cast<FcResultFileType>(qBound(0, json.value(QStringLiteral("fileType")).toInt(),
                                                 static_cast<int>(FcResultFileType::Other))),
            QDir::toNativeSeparators(json.value(QStringLiteral("filePath")).toString()),
            json.value(QStringLiteral("fileSize")).toString().toLongLong(),
            json.value(QStringLiteral("exists")).toBool(),
            QDateTime::fromString(json.value(QStringLiteral("lastModified")).toString(), Qt::ISODateWithMs),
            json.value(QStringLiteral("columnCount")).toInt(),
            json.value(QStringLiteral("dataRowCount")).toString().toLongLong(),
            json.value(QStringLiteral("startTime")).toString(),
            json.value(QStringLiteral("endTime")).toString(), columnNames);
    } else if (storageClass == QStringLiteral("Generic")) {
        object = type == FcObjectType::Floor
                     ? std::static_pointer_cast<FcObject>(
                           std::make_shared<FcFloorObject>(name))
                     : std::make_shared<FcObject>(name, type);
    } else {
        error = QStringLiteral("Unsupported storage class '%1'.").arg(storageClass);
        return {};
    }

    if (!object->restorePersistentId(uuid)) {
        error = QStringLiteral("Object '%1' has an invalid UUID '%2'.").arg(name, uuid);
        return {};
    }
    if (restoredIds.contains(object->id())) {
        error = QStringLiteral("Duplicate object UUID '%1'.").arg(object->id());
        return {};
    }
    restoredIds.insert(object->id());
    object->setVisible(json.value(QStringLiteral("visible")).toBool(true));
    object->setLocked(json.value(QStringLiteral("locked")).toBool(false));
    object->setFloorName(json.value(QStringLiteral("floor")).toString());
    QStringList tags;
    for (const QJsonValue& tag : json.value(QStringLiteral("tags")).toArray()) {
        if (tag.isString()) tags.append(tag.toString());
    }
    object->setTags(tags);

    for (const QJsonValue& childValue :
         json.value(QStringLiteral("children")).toArray()) {
        if (!childValue.isObject()) {
            error = QStringLiteral("Object '%1' contains an invalid child record.").arg(name);
            return {};
        }
        FcObject::Ptr child = deserializeObject(childValue.toObject(), restoredIds, error);
        if (!child || !object->addChild(child)) {
            if (error.isEmpty()) {
                error = QStringLiteral("Could not restore a child of '%1'.").arg(name);
            }
            return {};
        }
    }
    return object;
}
}

bool FcProjectSerializer::save(const FcProject& project,
                               const QString& filePath,
                               QString* errorMessage)
{
    return save(project, filePath, FcProjectRuntimeSettings{}, errorMessage);
}

bool FcProjectSerializer::save(const FcProject& project,
                               const QString& filePath,
                               const FcProjectRuntimeSettings& runtimeSettings,
                               QString* errorMessage)
{
    const FcDocument* document = project.document();
    if (!document || filePath.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project or file path is invalid.");
        return false;
    }

    QJsonObject projectJson;
    projectJson.insert(QStringLiteral("name"), project.name());
    projectJson.insert(QStringLiteral("chid"), project.chid());
    projectJson.insert(QStringLiteral("endTime"), project.endTime());
    projectJson.insert(QStringLiteral("fdsVersion"), project.fdsVersion());
    projectJson.insert(QStringLiteral("displayUnit"),
                       static_cast<int>(project.displayUnit()));
    projectJson.insert(QStringLiteral("simulationParameters"),
                       simulationParametersToJson(project.simulationParameters()));
    projectJson.insert(QStringLiteral("resultDirectory"),
                       QDir::fromNativeSeparators(runtimeSettings.resultDirectory));
    projectJson.insert(QStringLiteral("solverExecutable"),
                       QDir::fromNativeSeparators(runtimeSettings.solverExecutable));
    projectJson.insert(QStringLiteral("parallelProcessCount"),
                       qBound(1, runtimeSettings.parallelProcessCount, 1024));
    QJsonArray scenarios;
    for (const FcScenario& scenario : project.scenarios()) {
        scenarios.append(scenarioToJson(scenario));
    }
    projectJson.insert(QStringLiteral("scenarios"), scenarios);
    projectJson.insert(QStringLiteral("activeScenarioUuid"),
                       project.activeScenarioId());
    projectJson.insert(QStringLiteral("defaultScenarioUuid"),
                       project.defaultScenarioId());

    QJsonArray groups;
    for (std::size_t index = 0; index < document->groups().size(); ++index) {
        const auto& group = document->groups()[index];
        QJsonObject groupJson = serializeObject(group);
        groupJson.insert(QStringLiteral("groupIndex"), static_cast<int>(index));
        groups.append(groupJson);
    }
    projectJson.insert(QStringLiteral("groups"), groups);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kFormatName));
    root.insert(QStringLiteral("version"), CurrentFormatVersion);
    root.insert(QStringLiteral("project"), projectJson);

    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create project directory '%1'.")
                                .arg(fileInfo.absolutePath());
        }
        return false;
    }
    QSaveFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

FcProjectLoadResult FcProjectSerializer::load(const QString& filePath)
{
    FcProjectLoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorMessage = QStringLiteral("Invalid project JSON: %1")
                                  .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() !=
            QString::fromLatin1(kFormatName) ||
        root.value(QStringLiteral("version")).toInt() < 1 ||
        root.value(QStringLiteral("version")).toInt() > CurrentFormatVersion) {
        result.errorMessage = QStringLiteral("Unsupported FireCAE project format or version.");
        return result;
    }
    const QJsonObject projectJson = root.value(QStringLiteral("project")).toObject();
    if (projectJson.isEmpty()) {
        result.errorMessage = QStringLiteral("Project record is missing.");
        return result;
    }
    auto project = std::make_unique<FcProject>(
        projectJson.value(QStringLiteral("name")).toString(QStringLiteral("Untitled")));
    project->setChid(projectJson.value(QStringLiteral("chid")).toString());
    project->setEndTime(projectJson.value(QStringLiteral("endTime")).toDouble(60.0));
    const QJsonValue versionValue = projectJson.value(QStringLiteral("fdsVersion"));
    QString projectFdsVersion = versionValue.toString().trimmed();
    if (projectFdsVersion.isEmpty()) {
        projectFdsVersion = QStringLiteral("6.10");
        result.warnings.append(
            QStringLiteral("Legacy project has no FDS Schema version; "
                           "it was opened with the compatibility Schema 6.10."));
    } else if (!FdsSchemaRegistry::isSupportedVersion(projectFdsVersion)) {
        const QString compatible =
            FdsSchemaRegistry::compatibleVersionForRevision(projectFdsVersion);
        if (!compatible.isEmpty()) {
            result.warnings.append(
                QStringLiteral("Project FDS Schema %1 was mapped to the compatible "
                               "FireCAE Schema %2.")
                    .arg(projectFdsVersion, compatible));
            projectFdsVersion = compatible;
        } else {
            result.warnings.append(
                QStringLiteral("Project FDS Schema %1 is not validated by this FireCAE "
                               "build; review the generated FDS file before solving.")
                    .arg(projectFdsVersion));
        }
    }
    project->setFdsVersion(projectFdsVersion);
    const int unitValue = projectJson.value(QStringLiteral("displayUnit")).toInt(
        static_cast<int>(FcDisplayUnit::Meters));
    project->setDisplayUnit(static_cast<FcDisplayUnit>(
        qBound(static_cast<int>(FcDisplayUnit::Meters), unitValue,
               static_cast<int>(FcDisplayUnit::Inches))));
    project->setSimulationParameters(simulationParametersFromJson(
        projectJson.value(QStringLiteral("simulationParameters")).toObject()));
    result.runtimeSettings.resultDirectory = QDir::toNativeSeparators(
        projectJson.value(QStringLiteral("resultDirectory")).toString());
    result.runtimeSettings.solverExecutable = QDir::toNativeSeparators(
        projectJson.value(QStringLiteral("solverExecutable")).toString());
    result.runtimeSettings.parallelProcessCount = qBound(
        1, projectJson.value(QStringLiteral("parallelProcessCount")).toInt(1), 1024);
    QVector<FcScenario> scenarios;
    for (const QJsonValue& value : projectJson.value(QStringLiteral("scenarios")).toArray()) {
        if (!value.isObject()) continue;
        const FcScenario scenario = scenarioFromJson(value.toObject());
        if (scenario.isValid()) scenarios.append(scenario);
    }
    if (!scenarios.isEmpty()) {
        project->setScenarios(
            scenarios,
            projectJson.value(QStringLiteral("activeScenarioUuid")).toString(),
            projectJson.value(QStringLiteral("defaultScenarioUuid")).toString());
    }

    const QJsonArray groups = projectJson.value(QStringLiteral("groups")).toArray();
    if (groups.size() != static_cast<int>(FcDocument::StandardGroupCount)) {
        result.errorMessage = QStringLiteral("Project must contain exactly %1 standard groups.")
                                  .arg(FcDocument::StandardGroupCount);
        return result;
    }
    QSet<QString> restoredIds;
    FcDocument* targetDocument = project->document();
    for (int index = 0; index < groups.size(); ++index) {
        const QJsonObject groupJson = groups[index].toObject();
        if (groupJson.value(QStringLiteral("groupIndex")).toInt(-1) != index) {
            result.errorMessage = QStringLiteral("Project group order is invalid at index %1.")
                                      .arg(index);
            return result;
        }
        const auto group = targetDocument->groups()[static_cast<std::size_t>(index)];
        const QString groupId = groupJson.value(QStringLiteral("uuid")).toString();
        if (!group->restorePersistentId(groupId) || restoredIds.contains(group->id())) {
            result.errorMessage = QStringLiteral("Invalid or duplicate group UUID '%1'.")
                                      .arg(groupId);
            return result;
        }
        restoredIds.insert(group->id());
        group->setName(groupJson.value(QStringLiteral("name")).toString(group->name()));
        group->setVisible(groupJson.value(QStringLiteral("visible")).toBool(true));
        group->setLocked(groupJson.value(QStringLiteral("locked")).toBool(false));
        group->setFloorName(groupJson.value(QStringLiteral("floor")).toString());
        QStringList groupTags;
        for (const QJsonValue& tag : groupJson.value(QStringLiteral("tags")).toArray()) {
            if (tag.isString()) groupTags.append(tag.toString());
        }
        group->setTags(groupTags);
        group->clearChildren();
        for (const QJsonValue& childValue :
             groupJson.value(QStringLiteral("children")).toArray()) {
            QString error;
            FcObject::Ptr child = deserializeObject(childValue.toObject(), restoredIds, error);
            if (!child || !group->addChild(child)) {
                result.errorMessage = error.isEmpty()
                                          ? QStringLiteral("Could not restore group child.")
                                          : error;
                return result;
            }
        }
    }

    project->setModified(false);
    result.project = std::move(project);
    return result;
}
