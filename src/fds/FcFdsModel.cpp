#include "fds/FcFdsModel.h"
#include "fds/FdsSchema.h"

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace
{
QStringList validateId(const QString& id, const QString& objectKind)
{
    if (id.trimmed().isEmpty()) {
        return {objectKind + QStringLiteral(" must have a non-empty FDS ID.")};
    }
    return {};
}

bool finiteBounds(const FcFdsBounds& bounds)
{
    return std::isfinite(bounds.xMin) && std::isfinite(bounds.xMax) &&
           std::isfinite(bounds.yMin) && std::isfinite(bounds.yMax) &&
           std::isfinite(bounds.zMin) && std::isfinite(bounds.zMax);
}

QStringList validateOrderedBounds(const FcFdsBounds& bounds,
                                  bool volumeRequired,
                                  const QString& objectKind)
{
    if (!finiteBounds(bounds)) {
        return {objectKind + QStringLiteral(" bounds must be finite.")};
    }
    if (bounds.xMin > bounds.xMax || bounds.yMin > bounds.yMax ||
        bounds.zMin > bounds.zMax) {
        return {objectKind + QStringLiteral(" bounds must be ordered min-to-max.")};
    }
    if (volumeRequired && !bounds.isVolume()) {
        return {objectKind + QStringLiteral(" bounds must define a non-zero volume.")};
    }
    if (!volumeRequired && !bounds.isPlanar()) {
        return {objectKind + QStringLiteral(" bounds must define exactly one plane.")};
    }
    return {};
}

bool hasParameter(const std::vector<FcFdsParameter>& parameters,
                  const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    for (const FcFdsParameter& parameter : parameters) {
        if (parameter.key == normalized) return true;
    }
    return false;
}

bool parseNumberList(const QString& value, int expectedCount,
                     std::vector<double>& numbers)
{
    numbers.clear();
    const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (parts.size() != expectedCount) return false;
    for (const QString& part : parts) {
        bool ok = false;
        const double number = QLocale::c().toDouble(part.trimmed(), &ok);
        if (!ok || !std::isfinite(number)) return false;
        numbers.push_back(number);
    }
    return true;
}

bool parseNumber(const QString& value, double& number)
{
    bool ok = false;
    number = QLocale::c().toDouble(value.trimmed(), &ok);
    return ok && std::isfinite(number);
}
}

bool FcFdsBounds::isVolume() const
{
    return xMax > xMin && yMax > yMin && zMax > zMin;
}

bool FcFdsBounds::isPlanar() const
{
    const int zeroDimensions = static_cast<int>(xMax == xMin) +
                               static_cast<int>(yMax == yMin) +
                               static_cast<int>(zMax == zMin);
    return xMax >= xMin && yMax >= yMin && zMax >= zMin && zeroDimensions == 1;
}

FcFdsObject::FcFdsObject(const QString& name, FcObjectType type, const QString& fdsId)
    : FcObject(name, type)
    , m_fdsId(fdsId.trimmed())
{
}

const QString& FcFdsObject::fdsId() const { return m_fdsId; }
void FcFdsObject::setFdsId(const QString& id) { m_fdsId = id.trimmed(); }

FcFdsNamelist::FcFdsNamelist(const QString& name,
                             FcObjectType type,
                             const QString& keyword,
                             const QString& fdsId,
                             int sequenceIndex)
    : FcFdsObject(name, type, fdsId)
    , m_keyword(keyword.trimmed().toUpper())
    , m_sequenceIndex(sequenceIndex)
{
    if (m_keyword == QStringLiteral("SLCF")) setVisible(false);
}

const QString& FcFdsNamelist::keyword() const { return m_keyword; }
int FcFdsNamelist::sequenceIndex() const { return m_sequenceIndex; }
void FcFdsNamelist::setSequenceIndex(int index) { m_sequenceIndex = index; }
const std::vector<FcFdsParameter>& FcFdsNamelist::parameters() const
{
    return m_parameters;
}

void FcFdsNamelist::setParameters(const std::vector<FcFdsParameter>& parameters)
{
    m_parameters = parameters;
    for (FcFdsParameter& parameter : m_parameters) {
        parameter.key = parameter.key.trimmed().toUpper();
        parameter.value = parameter.value.trimmed();
    }
}

void FcFdsNamelist::clearParameters()
{
    m_parameters.clear();
}

void FcFdsNamelist::addRawParameter(const QString& key, const QString& value)
{
    m_parameters.push_back({key.trimmed().toUpper(), FcFdsParameterKind::Raw,
                            value.trimmed(), {}});
}

void FcFdsNamelist::addStringParameter(const QString& key, const QString& value)
{
    m_parameters.push_back({key.trimmed().toUpper(), FcFdsParameterKind::String,
                            value, {}});
}

void FcFdsNamelist::addReferenceParameter(const QString& key,
                                          const QStringList& targetObjectIds,
                                          const QString& fallbackValue)
{
    m_parameters.push_back({key.trimmed().toUpper(),
                            FcFdsParameterKind::ObjectReferences,
                            fallbackValue.trimmed(), targetObjectIds});
}

bool FcFdsNamelist::setReferenceTargets(const QString& key,
                                        const QStringList& targetObjectIds)
{
    const QString normalized = key.trimmed().toUpper();
    for (FcFdsParameter& parameter : m_parameters) {
        if (parameter.key == normalized) {
            parameter.kind = FcFdsParameterKind::ObjectReferences;
            parameter.targetObjectIds = targetObjectIds;
            return true;
        }
    }
    return false;
}

QString FcFdsNamelist::parameterValue(const QString& key) const
{
    const QString normalized = key.trimmed().toUpper();
    for (const FcFdsParameter& parameter : m_parameters) {
        if (parameter.key == normalized) {
            return parameter.value;
        }
    }
    return {};
}

QStringList FcFdsNamelist::validate() const
{
    QStringList errors;
    if (m_keyword.isEmpty()) {
        errors.append(QStringLiteral("FDS namelist keyword must not be empty."));
    }
    for (const FcFdsParameter& parameter : m_parameters) {
        if (parameter.key.isEmpty()) {
            errors.append(QStringLiteral("FDS namelist parameter key must not be empty."));
        }
    }

    const auto requireParameter = [this, &errors](const char* key) {
        const QString parameter = QString::fromLatin1(key);
        if (!hasParameter(m_parameters, parameter)) {
            errors.append(m_keyword + QStringLiteral(" requires parameter ") +
                          parameter + QLatin1Char('.'));
        }
    };
    const QStringList idRequiredKeywords = {
        QStringLiteral("SPEC"), QStringLiteral("MATL"), QStringLiteral("SURF"),
        QStringLiteral("PART"), QStringLiteral("PROP"), QStringLiteral("MULT"),
        QStringLiteral("RAMP"), QStringLiteral("TABL"), QStringLiteral("DEVC"),
        QStringLiteral("CTRL"), QStringLiteral("HVAC")};
    if (idRequiredKeywords.contains(m_keyword) && fdsId().isEmpty()) {
        errors.append(m_keyword + QStringLiteral(" must have a non-empty FDS ID."));
    }

    if (m_keyword == QStringLiteral("MESH")) {
        requireParameter("IJK");
        requireParameter("XB");
        std::vector<double> cells;
        if (hasParameter(m_parameters, QStringLiteral("IJK")) &&
            (!parseNumberList(parameterValue(QStringLiteral("IJK")), 3, cells) ||
             cells[0] <= 0.0 || cells[1] <= 0.0 || cells[2] <= 0.0)) {
            errors.append(QStringLiteral("MESH IJK must contain three positive cell counts."));
        }
        std::vector<double> boundsValues;
        if (hasParameter(m_parameters, QStringLiteral("XB")) &&
            (!parseNumberList(parameterValue(QStringLiteral("XB")), 6, boundsValues) ||
             boundsValues[1] <= boundsValues[0] ||
             boundsValues[3] <= boundsValues[2] ||
             boundsValues[5] <= boundsValues[4])) {
            errors.append(QStringLiteral("MESH XB must define an ordered non-zero volume."));
        }
    } else if (m_keyword == QStringLiteral("VENT")) {
        const QStringList planeKeys = {
            QStringLiteral("PBX"), QStringLiteral("PBY"), QStringLiteral("PBZ")};
        const bool hasPlaneBoundary = std::any_of(
            planeKeys.cbegin(), planeKeys.cend(),
            [this](const QString& key) { return hasParameter(m_parameters, key); });
        if (!hasParameter(m_parameters, QStringLiteral("XB")) &&
            !hasParameter(m_parameters, QStringLiteral("MB")) &&
            !hasPlaneBoundary) {
            errors.append(QStringLiteral("VENT requires XB, MB, PBX, PBY, or PBZ."));
        }
        requireParameter("SURF_ID");
        if (hasParameter(m_parameters, QStringLiteral("XB"))) {
            std::vector<double> values;
            if (!parseNumberList(parameterValue(QStringLiteral("XB")), 6, values)) {
                errors.append(QStringLiteral("VENT XB must contain six finite coordinates."));
            } else {
                const int planarDimensions = static_cast<int>(values[0] == values[1]) +
                                             static_cast<int>(values[2] == values[3]) +
                                             static_cast<int>(values[4] == values[5]);
                if (values[1] < values[0] || values[3] < values[2] ||
                    values[5] < values[4] || planarDimensions != 1) {
                    errors.append(QStringLiteral("VENT XB must define exactly one plane."));
                }
            }
        }
        for (const QString& planeKey : planeKeys) {
            if (!hasParameter(m_parameters, planeKey)) continue;
            double coordinate = 0.0;
            if (!parseNumber(parameterValue(planeKey), coordinate)) {
                errors.append(QStringLiteral("VENT ") + planeKey +
                              QStringLiteral(" must be a finite coordinate."));
            }
        }
    } else if (m_keyword == QStringLiteral("DEVC")) {
        if (!hasParameter(m_parameters, QStringLiteral("QUANTITY")) &&
            !hasParameter(m_parameters, QStringLiteral("PROP_ID"))) {
            errors.append(QStringLiteral("DEVC requires QUANTITY or PROP_ID."));
        }
        if (hasParameter(m_parameters, QStringLiteral("XYZ"))) {
            std::vector<double> values;
            if (!parseNumberList(parameterValue(QStringLiteral("XYZ")), 3, values)) {
                errors.append(QStringLiteral("DEVC XYZ must contain three finite coordinates."));
            }
        }
    } else if (m_keyword == QStringLiteral("CTRL")) {
        requireParameter("FUNCTION_TYPE");
        requireParameter("INPUT_ID");
    } else if (m_keyword == QStringLiteral("RAMP")) {
        requireParameter("T");
        requireParameter("F");
        double value = 0.0;
        if (hasParameter(m_parameters, QStringLiteral("T")) &&
            !parseNumber(parameterValue(QStringLiteral("T")), value)) {
            errors.append(QStringLiteral("RAMP T must be a finite number."));
        }
        if (hasParameter(m_parameters, QStringLiteral("F")) &&
            !parseNumber(parameterValue(QStringLiteral("F")), value)) {
            errors.append(QStringLiteral("RAMP F must be a finite number."));
        }
    } else if (m_keyword == QStringLiteral("TABL")) {
        requireParameter("TABLE_DATA");
        if (hasParameter(m_parameters, QStringLiteral("TABLE_DATA"))) {
            std::vector<double> values;
            if (!parseNumberList(parameterValue(QStringLiteral("TABLE_DATA")), 6,
                                 values)) {
                errors.append(QStringLiteral(
                    "TABL TABLE_DATA must contain six finite numbers."));
            }
        }
    } else if (m_keyword == QStringLiteral("TIME") &&
               hasParameter(m_parameters, QStringLiteral("DT"))) {
        double timeStep = 0.0;
        if (!parseNumber(parameterValue(QStringLiteral("DT")), timeStep) ||
            timeStep <= 0.0) {
            errors.append(QStringLiteral("TIME DT must be a positive finite number."));
        }
    }
    for (const QString& schemaError : FdsSchemaRegistry::validate(
             m_keyword, fdsId(), m_parameters)) {
        if (!errors.contains(schemaError)) errors.append(schemaError);
    }
    return errors;
}

FcFdsMesh::FcFdsMesh(const QString& name,
                     const QString& fdsId,
                     const std::array<int, 3>& cells,
                     const FcFdsBounds& bounds)
    : FcFdsObject(name, FcObjectType::Mesh, fdsId)
    , m_cells(cells)
    , m_bounds(bounds)
{
}

const std::array<int, 3>& FcFdsMesh::cells() const { return m_cells; }
void FcFdsMesh::setCells(const std::array<int, 3>& cells) { m_cells = cells; }
const FcFdsBounds& FcFdsMesh::bounds() const { return m_bounds; }
void FcFdsMesh::setBounds(const FcFdsBounds& bounds) { m_bounds = bounds; }

QStringList FcFdsMesh::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Mesh"));
    if (m_cells[0] <= 0 || m_cells[1] <= 0 || m_cells[2] <= 0) {
        errors.append(QStringLiteral("Mesh cell counts must all be positive."));
    }
    errors.append(validateOrderedBounds(m_bounds, true, QStringLiteral("Mesh")));
    return errors;
}

FcFdsReaction::FcFdsReaction(const QString& name,
                             const QString& fdsId,
                             const QString& fuel,
                             double sootYield)
    : FcFdsObject(name, FcObjectType::Reaction, fdsId)
    , m_fuel(fuel.trimmed())
    , m_sootYield(sootYield)
{
}

const QString& FcFdsReaction::fuel() const { return m_fuel; }
void FcFdsReaction::setFuel(const QString& fuel) { m_fuel = fuel.trimmed(); }
double FcFdsReaction::sootYield() const { return m_sootYield; }
void FcFdsReaction::setSootYield(double value) { m_sootYield = value; }

QStringList FcFdsReaction::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Reaction"));
    if (m_fuel.isEmpty()) {
        errors.append(QStringLiteral("Reaction fuel must not be empty."));
    }
    if (!std::isfinite(m_sootYield) || m_sootYield < 0.0) {
        errors.append(QStringLiteral("Reaction soot yield must be finite and non-negative."));
    }
    return errors;
}

FcFdsSurface::FcFdsSurface(const QString& name,
                           const QString& fdsId,
                           double heatReleaseRatePerArea,
                           const QString& color)
    : FcFdsObject(name, FcObjectType::Surface, fdsId)
    , m_heatReleaseRatePerArea(heatReleaseRatePerArea)
    , m_color(color.trimmed())
{
}

double FcFdsSurface::heatReleaseRatePerArea() const { return m_heatReleaseRatePerArea; }
void FcFdsSurface::setHeatReleaseRatePerArea(double value)
{
    m_heatReleaseRatePerArea = value;
}
const QString& FcFdsSurface::color() const { return m_color; }
void FcFdsSurface::setColor(const QString& color) { m_color = color.trimmed(); }

QStringList FcFdsSurface::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Surface"));
    if (!std::isfinite(m_heatReleaseRatePerArea) || m_heatReleaseRatePerArea < 0.0) {
        errors.append(QStringLiteral("Surface HRRPUA must be finite and non-negative."));
    }
    return errors;
}

FcFdsObstruction::FcFdsObstruction(const QString& name,
                                   const QString& fdsId,
                                   const FcFdsBounds& bounds,
                                   const QString& surfaceId)
    : FcFdsObject(name, FcObjectType::Obstruction, fdsId)
    , m_bounds(bounds)
    , m_surfaceId(surfaceId.trimmed())
{
}

const FcFdsBounds& FcFdsObstruction::bounds() const { return m_bounds; }
void FcFdsObstruction::setBounds(const FcFdsBounds& bounds) { m_bounds = bounds; }
const QString& FcFdsObstruction::surfaceId() const { return m_surfaceId; }
void FcFdsObstruction::setSurfaceId(const QString& id) { m_surfaceId = id.trimmed(); }

QStringList FcFdsObstruction::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Obstruction"));
    errors.append(validateOrderedBounds(m_bounds, true, QStringLiteral("Obstruction")));
    return errors;
}

FcFdsVent::FcFdsVent(const QString& name,
                     const QString& fdsId,
                     const FcFdsBounds& bounds,
                     const QString& surfaceId)
    : FcFdsObject(name, FcObjectType::Vent, fdsId)
    , m_bounds(bounds)
    , m_surfaceId(surfaceId.trimmed())
{
}

const FcFdsBounds& FcFdsVent::bounds() const { return m_bounds; }
void FcFdsVent::setBounds(const FcFdsBounds& bounds) { m_bounds = bounds; }
const QString& FcFdsVent::surfaceId() const { return m_surfaceId; }
void FcFdsVent::setSurfaceId(const QString& id) { m_surfaceId = id.trimmed(); }

QStringList FcFdsVent::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Vent"));
    errors.append(validateOrderedBounds(m_bounds, false, QStringLiteral("Vent")));
    if (m_surfaceId.isEmpty()) {
        errors.append(QStringLiteral("Vent must reference a surface."));
    }
    return errors;
}

std::shared_ptr<FcFdsOutput> FcFdsOutput::boundary(const QString& name,
                                                   const QString& fdsId,
                                                   const QString& quantity)
{
    return std::shared_ptr<FcFdsOutput>(new FcFdsOutput(name,
                                                        fdsId,
                                                        FcFdsOutputKind::Boundary,
                                                        quantity,
                                                        FcFdsPlaneAxis::X,
                                                        0.0,
                                                        false));
}

std::shared_ptr<FcFdsOutput> FcFdsOutput::slice(const QString& name,
                                                const QString& fdsId,
                                                FcFdsPlaneAxis axis,
                                                double planeValue,
                                                const QString& quantity,
                                                bool vectorOutput)
{
    return std::shared_ptr<FcFdsOutput>(new FcFdsOutput(name,
                                                        fdsId,
                                                        FcFdsOutputKind::Slice,
                                                        quantity,
                                                        axis,
                                                        planeValue,
                                                        vectorOutput));
}

FcFdsOutput::FcFdsOutput(const QString& name,
                         const QString& fdsId,
                         FcFdsOutputKind kind,
                         const QString& quantity,
                         FcFdsPlaneAxis axis,
                         double planeValue,
                         bool vectorOutput)
    : FcFdsObject(name, FcObjectType::Output, fdsId)
    , m_kind(kind)
    , m_quantity(quantity.trimmed())
    , m_axis(axis)
    , m_planeValue(planeValue)
    , m_vectorOutput(vectorOutput)
{
    if (kind == FcFdsOutputKind::Slice) setVisible(false);
}

FcFdsOutputKind FcFdsOutput::kind() const { return m_kind; }
const QString& FcFdsOutput::quantity() const { return m_quantity; }
FcFdsPlaneAxis FcFdsOutput::planeAxis() const { return m_axis; }
double FcFdsOutput::planeValue() const { return m_planeValue; }
bool FcFdsOutput::vectorOutput() const { return m_vectorOutput; }

QStringList FcFdsOutput::validate() const
{
    QStringList errors = validateId(fdsId(), QStringLiteral("Output"));
    if (m_quantity.isEmpty()) {
        errors.append(QStringLiteral("Output quantity must not be empty."));
    }
    if (m_kind == FcFdsOutputKind::Slice && !std::isfinite(m_planeValue)) {
        errors.append(QStringLiteral("Slice plane value must be finite."));
    }
    return errors;
}
