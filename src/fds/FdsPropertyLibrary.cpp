#include "fds/FdsPropertyLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <utility>

namespace
{
FcFdsParameter raw(const char* key, const char* value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::Raw,
            QString::fromLatin1(value), {}};
}

FcFdsParameter text(const char* key, const char* value)
{
    return {QString::fromLatin1(key), FcFdsParameterKind::String,
            QString::fromLatin1(value), {}};
}

QJsonObject entryToJson(const FdsLibraryEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("libraryId"), entry.libraryId);
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("category"), entry.category);
    object.insert(QStringLiteral("keyword"), entry.keyword);
    object.insert(QStringLiteral("fdsId"), entry.fdsId);
    object.insert(QStringLiteral("fdsVersion"), entry.fdsVersion);
    QJsonArray parameters;
    for (const FcFdsParameter& parameter : entry.parameters) {
        QJsonObject value;
        value.insert(QStringLiteral("key"), parameter.key);
        value.insert(QStringLiteral("kind"), static_cast<int>(parameter.kind));
        value.insert(QStringLiteral("value"), parameter.value);
        QJsonArray targets;
        for (const QString& target : parameter.targetObjectIds) targets.append(target);
        value.insert(QStringLiteral("targets"), targets);
        parameters.append(value);
    }
    object.insert(QStringLiteral("parameters"), parameters);
    return object;
}

bool entryFromJson(const QJsonObject& object, FdsLibraryEntry& entry)
{
    entry.libraryId = object.value(QStringLiteral("libraryId")).toString().trimmed();
    entry.name = object.value(QStringLiteral("name")).toString().trimmed();
    entry.category = object.value(QStringLiteral("category")).toString().trimmed();
    entry.keyword = object.value(QStringLiteral("keyword")).toString().trimmed().toUpper();
    entry.fdsId = object.value(QStringLiteral("fdsId")).toString().trimmed();
    entry.fdsVersion = object.value(QStringLiteral("fdsVersion")).toString(
        QStringLiteral("6.10"));
    entry.parameters.clear();
    for (const QJsonValue& parameterValue :
         object.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject parameterObject = parameterValue.toObject();
        FcFdsParameter parameter;
        parameter.key = parameterObject.value(QStringLiteral("key")).toString().trimmed().toUpper();
        const int kind = parameterObject.value(QStringLiteral("kind")).toInt();
        if (kind < static_cast<int>(FcFdsParameterKind::Raw) ||
            kind > static_cast<int>(FcFdsParameterKind::ObjectReferences)) return false;
        parameter.kind = static_cast<FcFdsParameterKind>(kind);
        parameter.value = parameterObject.value(QStringLiteral("value")).toString();
        for (const QJsonValue& target : parameterObject.value(QStringLiteral("targets")).toArray()) {
            parameter.targetObjectIds.append(target.toString());
        }
        if (parameter.key.isEmpty()) return false;
        entry.parameters.push_back(parameter);
    }
    return !entry.libraryId.isEmpty() && !entry.name.isEmpty() &&
           !entry.category.isEmpty() && !entry.keyword.isEmpty();
}
}

FdsPropertyLibrary::FdsPropertyLibrary(QString userFilePath)
    : m_userFilePath(std::move(userFilePath))
{
    if (m_userFilePath.isEmpty()) {
        m_userFilePath = QDir(QStandardPaths::writableLocation(
                                 QStandardPaths::AppConfigLocation))
                             .filePath(QStringLiteral("fds-property-library.json"));
    }
    reload();
}

const QVector<FdsLibraryEntry>& FdsPropertyLibrary::entries() const { return m_entries; }
const QString& FdsPropertyLibrary::userFilePath() const { return m_userFilePath; }

QVector<FdsLibraryEntry> FdsPropertyLibrary::builtInEntries()
{
    return {
        {QStringLiteral("builtin.material.concrete"), QStringLiteral("Concrete"),
         QStringLiteral("Materials"), QStringLiteral("MATL"), QStringLiteral("CONCRETE"),
         QStringLiteral("6.10"), {raw("DENSITY", "2280"), raw("CONDUCTIVITY", "1.8"),
                                   raw("SPECIFIC_HEAT", "1.04")}, true},
        {QStringLiteral("builtin.material.gypsum"), QStringLiteral("Gypsum Board"),
         QStringLiteral("Materials"), QStringLiteral("MATL"), QStringLiteral("GYPSUM"),
         QStringLiteral("6.10"), {raw("DENSITY", "800"), raw("CONDUCTIVITY", "0.17"),
                                   raw("SPECIFIC_HEAT", "1.09")}, true},
        {QStringLiteral("builtin.surface.wall"), QStringLiteral("12 mm Gypsum Wall"),
         QStringLiteral("Surfaces"), QStringLiteral("SURF"), QStringLiteral("GYPSUM_WALL"),
         QStringLiteral("6.10"), {raw("THICKNESS", "0.012"), text("BACKING", "INSULATED"),
                                   text("COLOR", "GRAY")}, true},
        {QStringLiteral("builtin.reaction.propane"), QStringLiteral("Propane Reaction"),
         QStringLiteral("Reactions"), QStringLiteral("REAC"), QStringLiteral("PROPANE_REAC"),
         QStringLiteral("6.10"), {text("FUEL", "PROPANE"), raw("SOOT_YIELD", "0.01"),
                                   raw("CO_YIELD", "0.0")}, true},
        {QStringLiteral("builtin.species.oxygen"), QStringLiteral("Oxygen Species"),
         QStringLiteral("Species"), QStringLiteral("SPEC"), QStringLiteral("OXYGEN"),
         QStringLiteral("6.10"), {text("FORMULA", "O2"), raw("BACKGROUND", ".TRUE.")}, true},
        {QStringLiteral("builtin.particle.water"), QStringLiteral("Water Droplet Particle"),
         QStringLiteral("Particles"), QStringLiteral("PART"), QStringLiteral("WATER_DROPLETS"),
         QStringLiteral("6.10"), {text("QUANTITIES", "DIAMETER"), raw("SAMPLING_FACTOR", "1")}, true},
        {QStringLiteral("builtin.fire.500"), QStringLiteral("500 kW/m² Design Fire"),
         QStringLiteral("Fire Sources"), QStringLiteral("SURF"), QStringLiteral("FIRE_500"),
         QStringLiteral("6.10"), {raw("HRRPUA", "500"), text("COLOR", "RED")}, true},
        {QStringLiteral("builtin.firecurve.t_squared"), QStringLiteral("Medium T-Squared Fire Curve"),
         QStringLiteral("Fire Source Curves"), QStringLiteral("RAMP"), QStringLiteral("MEDIUM_FIRE"),
         QStringLiteral("6.10"), {raw("T", "0"), raw("F", "0"),
                                   raw("T", "300"), raw("F", "1")}, true},
        {QStringLiteral("builtin.detector.thermocouple"), QStringLiteral("Thermocouple"),
         QStringLiteral("Device Templates"), QStringLiteral("DEVC"), QStringLiteral("TC"),
         QStringLiteral("6.10"), {text("QUANTITY", "THERMOCOUPLE"), raw("XYZ", "0,0,1.5")}, true},
        {QStringLiteral("builtin.detector.smoke"), QStringLiteral("Smoke Detector"),
         QStringLiteral("Device Templates"), QStringLiteral("DEVC"), QStringLiteral("SMOKE_DETECTOR"),
         QStringLiteral("6.10"), {text("QUANTITY", "CHAMBER OBSCURATION"), raw("XYZ", "0,0,2.4")}, true},
        {QStringLiteral("builtin.sprinkler.quick"), QStringLiteral("Quick Response Sprinkler"),
         QStringLiteral("Sprinkler Templates"), QStringLiteral("PROP"), QStringLiteral("QR_SPRINKLER"),
         QStringLiteral("6.10"), {text("QUANTITY", "SPRINKLER LINK TEMPERATURE"),
                                   raw("ACTIVATION_TEMPERATURE", "68"), raw("RTI", "50"),
                                   raw("FLOW_RATE", "60")}, true},
        {QStringLiteral("builtin.hvac.fan"), QStringLiteral("Constant Flow Fan"),
         QStringLiteral("HVAC"), QStringLiteral("HVAC"), QStringLiteral("FAN"),
         QStringLiteral("6.10"), {text("TYPE_ID", "FAN"), raw("VOLUME_FLOW", "1.0")}, true},
        {QStringLiteral("builtin.hvac.aircoil"), QStringLiteral("Air Cooling Coil"),
         QStringLiteral("HVAC"), QStringLiteral("HVAC"), QStringLiteral("AIRCOIL"),
         QStringLiteral("6.10"), {text("TYPE_ID", "AIRCOIL"), raw("COOLANT_TEMPERATURE", "10")}, true}
    };
}

bool FdsPropertyLibrary::reload(QString* errorMessage)
{
    m_entries = builtInEntries();
    if (!QFileInfo::exists(m_userFilePath)) return true;
    QVector<FdsLibraryEntry> userEntries;
    if (!readEntries(m_userFilePath, userEntries, errorMessage)) return false;
    for (FdsLibraryEntry& entry : userEntries) {
        entry.builtIn = false;
        if (find(entry.libraryId)) entry.libraryId = uniqueId(entry.libraryId);
        m_entries.append(entry);
    }
    return true;
}

bool FdsPropertyLibrary::save(QString* errorMessage) const
{
    QVector<FdsLibraryEntry> userEntries;
    for (const FdsLibraryEntry& entry : m_entries) {
        if (!entry.builtIn) userEntries.append(entry);
    }
    return writeEntries(m_userFilePath, userEntries, errorMessage);
}

bool FdsPropertyLibrary::importFile(const QString& filePath,
                                    FdsLibraryConflictPolicy policy,
                                    QString* errorMessage)
{
    QVector<FdsLibraryEntry> imported;
    if (!readEntries(filePath, imported, errorMessage)) return false;
    for (FdsLibraryEntry entry : imported) {
        entry.builtIn = false;
        const FdsLibraryEntry* existing = find(entry.libraryId);
        if (existing) {
            if (policy == FdsLibraryConflictPolicy::Skip) continue;
            if (policy == FdsLibraryConflictPolicy::Rename || existing->builtIn) {
                entry.libraryId = uniqueId(entry.libraryId);
                entry.name += QStringLiteral(" (Imported)");
            } else {
                for (int index = 0; index < m_entries.size(); ++index) {
                    if (m_entries[index].libraryId == entry.libraryId) {
                        m_entries[index] = entry;
                        existing = nullptr;
                        break;
                    }
                }
                if (!existing) continue;
            }
        }
        m_entries.append(entry);
    }
    return save(errorMessage);
}

bool FdsPropertyLibrary::exportFile(const QString& filePath,
                                    const QStringList& libraryIds,
                                    QString* errorMessage) const
{
    QVector<FdsLibraryEntry> selected;
    for (const QString& id : libraryIds) {
        if (const FdsLibraryEntry* entry = find(id)) selected.append(*entry);
    }
    return writeEntries(filePath, selected, errorMessage);
}

QString FdsPropertyLibrary::duplicateAsUser(const QString& libraryId,
                                            QString* errorMessage)
{
    const FdsLibraryEntry* source = find(libraryId);
    if (!source) {
        if (errorMessage) *errorMessage = QStringLiteral("Library entry was not found.");
        return {};
    }
    FdsLibraryEntry copy = *source;
    copy.builtIn = false;
    copy.libraryId = uniqueId(source->libraryId + QStringLiteral(".copy"));
    copy.name += QStringLiteral(" Copy");
    m_entries.append(copy);
    if (!save(errorMessage)) {
        m_entries.removeLast();
        return {};
    }
    return copy.libraryId;
}

bool FdsPropertyLibrary::removeUserEntry(const QString& libraryId,
                                         QString* errorMessage)
{
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries[index].libraryId != libraryId) continue;
        if (m_entries[index].builtIn) {
            if (errorMessage) *errorMessage = QStringLiteral("Built-in entries cannot be removed.");
            return false;
        }
        const FdsLibraryEntry removed = m_entries.takeAt(index);
        if (!save(errorMessage)) {
            m_entries.insert(index, removed);
            return false;
        }
        return true;
    }
    if (errorMessage) *errorMessage = QStringLiteral("Library entry was not found.");
    return false;
}

const FdsLibraryEntry* FdsPropertyLibrary::find(const QString& libraryId) const
{
    for (const FdsLibraryEntry& entry : m_entries) {
        if (entry.libraryId == libraryId) return &entry;
    }
    return nullptr;
}

bool FdsPropertyLibrary::readEntries(const QString& filePath,
                                     QVector<FdsLibraryEntry>& entries,
                                     QString* errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (errorMessage) *errorMessage = parseError.errorString();
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("formatVersion")).toInt() != 1) {
        if (errorMessage) *errorMessage = QStringLiteral("Unsupported library format version.");
        return false;
    }
    entries.clear();
    for (const QJsonValue& value : root.value(QStringLiteral("entries")).toArray()) {
        FdsLibraryEntry entry;
        if (!entryFromJson(value.toObject(), entry)) {
            if (errorMessage) *errorMessage = QStringLiteral("Library contains an invalid entry.");
            return false;
        }
        entries.append(entry);
    }
    return true;
}

bool FdsPropertyLibrary::writeEntries(const QString& filePath,
                                      const QVector<FdsLibraryEntry>& entries,
                                      QString* errorMessage) const
{
    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create library directory.");
        return false;
    }
    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), 1);
    QJsonArray array;
    for (const FdsLibraryEntry& entry : entries) array.append(entryToJson(entry));
    root.insert(QStringLiteral("entries"), array);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

QString FdsPropertyLibrary::uniqueId(QString preferred) const
{
    preferred = preferred.trimmed();
    if (preferred.isEmpty()) preferred = QStringLiteral("user.entry");
    QString candidate = preferred;
    int suffix = 2;
    while (find(candidate)) candidate = preferred + QLatin1Char('.') + QString::number(suffix++);
    return candidate;
}
