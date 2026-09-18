#include "fds/FdsImporter.h"

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsSchema.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <vector>

namespace
{
struct ParsedParameter
{
    QString key;
    QString value;
};

struct ParsedRecord
{
    QString keyword;
    std::vector<ParsedParameter> parameters;
    int sequenceIndex = 0;
};

bool isQuote(QChar character)
{
    return character == QLatin1Char('\'') || character == QLatin1Char('"');
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && isQuote(value.front()) && value.back() == value.front()) {
        const QChar quote = value.front();
        value = value.mid(1, value.size() - 2);
        value.replace(QString(2, quote), QString(quote));
    }
    return value;
}

QString withoutComments(const QString& text)
{
    QString cleaned;
    cleaned.reserve(text.size());
    QChar quote;
    bool comment = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (comment) {
            if (character == QLatin1Char('\n')) {
                comment = false;
                cleaned.append(character);
            }
            continue;
        }
        if (!quote.isNull()) {
            cleaned.append(character);
            if (character == quote) {
                if (i + 1 < text.size() && text.at(i + 1) == quote) {
                    cleaned.append(text.at(++i));
                } else {
                    quote = {};
                }
            }
            continue;
        }
        if (isQuote(character)) {
            quote = character;
            cleaned.append(character);
        } else if (character == QLatin1Char('!')) {
            comment = true;
        } else {
            cleaned.append(character);
        }
    }
    return cleaned;
}

std::vector<QString> recordTexts(const QString& source)
{
    std::vector<QString> records;
    const QString text = withoutComments(source);
    for (int start = 0; start < text.size();) {
        start = text.indexOf(QLatin1Char('&'), start);
        if (start < 0) {
            break;
        }
        QChar quote;
        int end = start + 1;
        for (; end < text.size(); ++end) {
            const QChar character = text.at(end);
            if (!quote.isNull()) {
                if (character == quote) {
                    if (end + 1 < text.size() && text.at(end + 1) == quote) {
                        ++end;
                    } else {
                        quote = {};
                    }
                }
            } else if (isQuote(character)) {
                quote = character;
            } else if (character == QLatin1Char('/')) {
                records.push_back(text.mid(start + 1, end - start - 1).trimmed());
                ++end;
                break;
            }
        }
        start = std::max(end, start + 1);
    }
    return records;
}

bool assignmentAt(const QString& body, int index, int* equalsIndex)
{
    if (index < 0 || index >= body.size() || !body.at(index).isLetter()) {
        return false;
    }
    if (index > 0) {
        const QChar previous = body.at(index - 1);
        if (!previous.isSpace() && previous != QLatin1Char(',')) {
            return false;
        }
    }
    int cursor = index;
    int parentheses = 0;
    while (cursor < body.size()) {
        const QChar character = body.at(cursor);
        if (character == QLatin1Char('(')) {
            ++parentheses;
        } else if (character == QLatin1Char(')') && parentheses > 0) {
            --parentheses;
        } else if (parentheses == 0 && (character.isLetterOrNumber() ||
                                        character == QLatin1Char('_'))) {
            // Continue through a normal keyword.
        } else if (parentheses > 0 && (character.isLetterOrNumber() ||
                                       character == QLatin1Char('_') ||
                                       character == QLatin1Char(':') ||
                                       character == QLatin1Char(','))) {
            // Continue through an indexed keyword, e.g. MATL_ID(1:2,1).
        } else {
            break;
        }
        ++cursor;
    }
    while (cursor < body.size() && body.at(cursor).isSpace()) {
        ++cursor;
    }
    if (cursor < body.size() && body.at(cursor) == QLatin1Char('=')) {
        *equalsIndex = cursor;
        return true;
    }
    return false;
}

std::vector<ParsedParameter> parseParameters(const QString& body)
{
    struct Assignment { int keyStart; int equals; };
    std::vector<Assignment> assignments;
    QChar quote;
    for (int i = 0; i < body.size(); ++i) {
        const QChar character = body.at(i);
        if (!quote.isNull()) {
            if (character == quote) {
                if (i + 1 < body.size() && body.at(i + 1) == quote) {
                    ++i;
                } else {
                    quote = {};
                }
            }
            continue;
        }
        if (isQuote(character)) {
            quote = character;
            continue;
        }
        int equals = -1;
        if (assignmentAt(body, i, &equals)) {
            assignments.push_back({i, equals});
            i = equals;
        }
    }

    std::vector<ParsedParameter> parameters;
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const Assignment& current = assignments[i];
        const int valueEnd = i + 1 < assignments.size()
                                 ? assignments[i + 1].keyStart
                                 : body.size();
        QString value = body.mid(current.equals + 1, valueEnd - current.equals - 1)
                            .trimmed();
        while (value.endsWith(QLatin1Char(','))) {
            value.chop(1);
            value = value.trimmed();
        }
        parameters.push_back({body.mid(current.keyStart,
                                       current.equals - current.keyStart)
                                  .trimmed().toUpper(),
                              value});
    }
    return parameters;
}

QString valueOf(const ParsedRecord& record, const QString& key)
{
    for (const ParsedParameter& parameter : record.parameters) {
        if (parameter.key == key) {
            return unquote(parameter.value);
        }
    }
    return {};
}

FcObjectType objectTypeFor(const QString& keyword)
{
    if (keyword == QStringLiteral("MESH")) return FcObjectType::Mesh;
    if (keyword == QStringLiteral("MULT")) return FcObjectType::MeshMultiplier;
    if (keyword == QStringLiteral("SPEC")) return FcObjectType::Species;
    if (keyword == QStringLiteral("MATL")) return FcObjectType::Material;
    if (keyword == QStringLiteral("SURF")) return FcObjectType::Surface;
    if (keyword == QStringLiteral("REAC")) return FcObjectType::Reaction;
    if (keyword == QStringLiteral("OBST")) return FcObjectType::Obstruction;
    if (keyword == QStringLiteral("VENT")) return FcObjectType::Vent;
    if (keyword == QStringLiteral("PART")) return FcObjectType::Particle;
    if (keyword == QStringLiteral("PROP")) return FcObjectType::Property;
    if (keyword == QStringLiteral("TABL")) return FcObjectType::Table;
    if (keyword == QStringLiteral("RAMP")) return FcObjectType::Ramp;
    if (keyword == QStringLiteral("DEVC")) return FcObjectType::Device;
    if (keyword == QStringLiteral("CTRL")) return FcObjectType::Control;
    if (keyword == QStringLiteral("HVAC")) return FcObjectType::HVAC;
    if (keyword == QStringLiteral("INIT")) return FcObjectType::InitialCondition;
    if (keyword == QStringLiteral("SLCF") || keyword == QStringLiteral("BNDF") ||
        keyword == QStringLiteral("ISOF") || keyword == QStringLiteral("PROF") ||
        keyword == QStringLiteral("PL3D") || keyword == QStringLiteral("SM3D") ||
        keyword == QStringLiteral("DUMP")) return FcObjectType::Output;
    return FcObjectType::SimulationParameter;
}

std::shared_ptr<FcObjectGroup> groupFor(FcDocument* document,
                                        FcObjectType type)
{
    switch (type) {
    case FcObjectType::Mesh:
    case FcObjectType::MeshMultiplier: return document->meshesGroup();
    case FcObjectType::Species: return document->speciesGroup();
    case FcObjectType::Material: return document->materialsGroup();
    case FcObjectType::Surface: return document->surfacesGroup();
    case FcObjectType::Reaction: return document->reactionsGroup();
    case FcObjectType::Obstruction: return document->geometryGroup();
    case FcObjectType::Vent: return document->ventsGroup();
    case FcObjectType::Particle:
    case FcObjectType::Property: return document->particlesGroup();
    case FcObjectType::Table:
    case FcObjectType::Ramp:
    case FcObjectType::Control: return document->controlsGroup();
    case FcObjectType::Device: return document->devicesGroup();
    case FcObjectType::HVAC: return document->hvacGroup();
    case FcObjectType::InitialCondition: return document->initialConditionsGroup();
    case FcObjectType::Output: return document->outputsGroup();
    default: return document->configurationGroup();
    }
}

bool isQuoted(const QString& value)
{
    const QString trimmed = value.trimmed();
    return trimmed.size() >= 2 && isQuote(trimmed.front()) &&
           trimmed.back() == trimmed.front();
}

bool isReferenceKey(const QString& key)
{
    const QString base = key.left(key.indexOf(QLatin1Char('(')) < 0
                                      ? key.size()
                                      : key.indexOf(QLatin1Char('(')));
    return base.endsWith(QStringLiteral("_ID")) ||
           base == QStringLiteral("FUEL") ||
           base == QStringLiteral("SPRAY_PATTERN_TABLE");
}

QString referenceKeyword(const QString& key)
{
    const QString base = key.left(key.indexOf(QLatin1Char('(')) < 0
                                      ? key.size()
                                      : key.indexOf(QLatin1Char('(')));
    if (base == QStringLiteral("SURF_ID")) return QStringLiteral("SURF");
    if (base == QStringLiteral("FUEL")) return QStringLiteral("SPEC");
    if (base == QStringLiteral("MATL_ID")) return QStringLiteral("MATL");
    if (base == QStringLiteral("SPEC_ID")) return QStringLiteral("SPEC");
    if (base == QStringLiteral("PART_ID")) return QStringLiteral("PART");
    if (base == QStringLiteral("PROP_ID")) return QStringLiteral("PROP");
    if (base == QStringLiteral("DEVC_ID")) return QStringLiteral("DEVC");
    if (base == QStringLiteral("CTRL_ID")) return QStringLiteral("CTRL");
    if (base == QStringLiteral("RAMP_ID")) return QStringLiteral("RAMP");
    if (base == QStringLiteral("MULT_ID")) return QStringLiteral("MULT");
    if (base == QStringLiteral("OBST_ID")) return QStringLiteral("OBST");
    if (base == QStringLiteral("VENT_ID")) return QStringLiteral("VENT");
    if (base == QStringLiteral("DUCT_ID") || base == QStringLiteral("NODE_ID") ||
        base == QStringLiteral("AIRCOIL_ID") || base == QStringLiteral("FAN_ID"))
        return QStringLiteral("HVAC");
    if (base == QStringLiteral("SPRAY_PATTERN_TABLE")) return QStringLiteral("TABL");
    return {};
}

QStringList quotedValues(const QString& raw)
{
    QStringList values;
    QChar quote;
    QString current;
    for (int i = 0; i < raw.size(); ++i) {
        const QChar character = raw.at(i);
        if (quote.isNull()) {
            if (isQuote(character)) {
                quote = character;
                current.clear();
            }
        } else if (character == quote) {
            if (i + 1 < raw.size() && raw.at(i + 1) == quote) {
                current.append(character);
                ++i;
            } else {
                values.append(current);
                quote = {};
            }
        } else {
            current.append(character);
        }
    }
    return values;
}
}

FdsImportResult FdsImporter::importFile(const QString& filePath) const
{
    FdsImportResult result;
    result.sourceFilePath = QFileInfo(filePath).absoluteFilePath();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = file.errorString();
        return result;
    }
    result = importText(QString::fromUtf8(file.readAll()), filePath);
    result.sourceFilePath = QFileInfo(filePath).absoluteFilePath();
    return result;
}

FdsImportResult FdsImporter::importText(const QString& text,
                                        const QString& sourceName) const
{
    FdsImportResult result;
    result.sourceFilePath = sourceName;
    std::vector<ParsedRecord> records;
    int sequence = 0;
    for (const QString& recordText : recordTexts(text)) {
        int split = 0;
        while (split < recordText.size() && !recordText.at(split).isSpace()) {
            ++split;
        }
        ParsedRecord record;
        record.keyword = recordText.left(split).trimmed().toUpper();
        record.parameters = parseParameters(recordText.mid(split));
        record.sequenceIndex = sequence++;
        if (!record.keyword.isEmpty()) {
            records.push_back(std::move(record));
        }
    }
    if (records.empty()) {
        result.errorMessage = QStringLiteral("No FDS namelist records were found.");
        return result;
    }

    QString projectName = QFileInfo(sourceName).completeBaseName();
    if (projectName.isEmpty()) projectName = QStringLiteral("Imported FDS Case");
    for (const ParsedRecord& record : records) {
        if (record.keyword == QStringLiteral("HEAD")) {
            const QString title = valueOf(record, QStringLiteral("TITLE"));
            if (!title.isEmpty()) projectName = title;
            break;
        }
    }
    auto project = std::make_unique<FcProject>(projectName);
    FcDocument* document = project->document();
    QHash<QString, QString> uuidByKeywordAndFdsId;
    QHash<QString, QStringList> uuidsByFdsId;
    std::vector<std::shared_ptr<FcFdsNamelist>> objects;
    QHash<QString, int> unnamedCounters;
    std::shared_ptr<FcObjectGroup> additionalRecords;
    QSet<QString> unsupportedRecordNames;
    QSet<QString> unsupportedParameterNames;

    for (const ParsedRecord& record : records) {
        const FdsNamelistSchema* recordSchema =
            FdsSchemaRegistry::namelist(record.keyword);
        if (!recordSchema && record.keyword != QStringLiteral("TAIL")) {
            ++result.unsupportedRecordCount;
            unsupportedRecordNames.insert(record.keyword);
        }
        if (recordSchema) {
            for (const ParsedParameter& parameter : record.parameters) {
                if (parameter.key == QStringLiteral("ID")) continue;
                if (!FdsSchemaRegistry::parameter(record.keyword, parameter.key)) {
                    ++result.unsupportedParameterCount;
                    unsupportedParameterNames.insert(
                        record.keyword + QLatin1Char('.') + parameter.key);
                }
            }
        }
        if (record.keyword == QStringLiteral("TAIL")) continue;
        if (record.keyword == QStringLiteral("HEAD")) {
            const QString chid = valueOf(record, QStringLiteral("CHID"));
            if (!chid.isEmpty()) project->setChid(chid);
        }
        if (record.keyword == QStringLiteral("TIME")) {
            bool ok = false;
            const double endTime = valueOf(record, QStringLiteral("T_END"))
                                       .toDouble(&ok);
            if (ok && endTime > 0.0) project->setEndTime(endTime);
        }

        const QString fdsId = valueOf(record, QStringLiteral("ID"));
        const bool special = record.keyword == QStringLiteral("HEAD") ||
                             record.keyword == QStringLiteral("TIME");
        int storedParameterCount = 0;
        for (const ParsedParameter& parameter : record.parameters) {
            if (parameter.key == QStringLiteral("ID") ||
                (record.keyword == QStringLiteral("HEAD") &&
                 (parameter.key == QStringLiteral("CHID") ||
                  parameter.key == QStringLiteral("TITLE"))) ||
                (record.keyword == QStringLiteral("TIME") &&
                 parameter.key == QStringLiteral("T_END"))) {
                continue;
            }
            ++storedParameterCount;
        }
        if (special && storedParameterCount == 0) continue;

        const int ordinal = ++unnamedCounters[record.keyword];
        const QString name = !fdsId.isEmpty()
                                 ? fdsId
                                 : QStringLiteral("%1 %2").arg(record.keyword)
                                       .arg(ordinal, 3, 10, QLatin1Char('0'));
        const FcObjectType objectType = objectTypeFor(record.keyword);
        auto object = std::make_shared<FcFdsNamelist>(name,
                                                       objectType,
                                                       record.keyword,
                                                       fdsId,
                                                       record.sequenceIndex);
        for (const ParsedParameter& parameter : record.parameters) {
            if (parameter.key == QStringLiteral("ID") ||
                (record.keyword == QStringLiteral("HEAD") &&
                 (parameter.key == QStringLiteral("CHID") ||
                  parameter.key == QStringLiteral("TITLE"))) ||
                (record.keyword == QStringLiteral("TIME") &&
                 parameter.key == QStringLiteral("T_END"))) continue;
            if (isQuoted(parameter.value) && !isReferenceKey(parameter.key)) {
                object->addStringParameter(parameter.key, unquote(parameter.value));
            } else {
                object->addRawParameter(parameter.key, parameter.value);
            }
        }
        if (objectType == FcObjectType::SimulationParameter &&
            !FdsSchemaRegistry::namelist(record.keyword)) {
            if (!additionalRecords) {
                additionalRecords = std::make_shared<FcObjectGroup>(
                    QStringLiteral("Additional Records"));
                document->configurationGroup()->addChild(additionalRecords);
            }
            additionalRecords->addChild(object);
        } else {
            groupFor(document, object->type())->addChild(object);
        }
        objects.push_back(object);
        ++result.objectCount;
        if (!fdsId.isEmpty()) {
            const QString normalized = fdsId.trimmed().toUpper();
            const QString typedKey = record.keyword + QLatin1Char(':') + normalized;
            uuidsByFdsId[normalized].append(object->id());
            if (!uuidByKeywordAndFdsId.contains(typedKey)) {
                uuidByKeywordAndFdsId.insert(typedKey, object->id());
            } else if (record.keyword != QStringLiteral("RAMP") &&
                       record.keyword != QStringLiteral("TABL")) {
                result.warnings.append(QStringLiteral("Duplicate FDS ID: ") + fdsId);
            }
        }
    }

    for (const std::shared_ptr<FcFdsNamelist>& object : objects) {
        for (const FcFdsParameter& parameter : object->parameters()) {
            if (!isReferenceKey(parameter.key)) continue;
            const QStringList values = quotedValues(parameter.value);
            if (values.isEmpty()) continue;
            QStringList targets;
            bool allResolved = true;
            for (const QString& value : values) {
                const QString normalized = value.trimmed().toUpper();
                QString uuid;
                const QString targetKeyword = referenceKeyword(parameter.key);
                if (!targetKeyword.isEmpty()) {
                    uuid = uuidByKeywordAndFdsId.value(targetKeyword +
                                                       QLatin1Char(':') + normalized);
                }
                if (uuid.isEmpty() && uuidsByFdsId.value(normalized).size() == 1) {
                    uuid = uuidsByFdsId.value(normalized).constFirst();
                }
                if (uuid.isEmpty()) {
                    allResolved = false;
                    break;
                }
                targets.append(uuid);
            }
            if (allResolved) object->setReferenceTargets(parameter.key, targets);
            if (allResolved) {
                ++result.resolvedReferenceCount;
            } else {
                ++result.unresolvedReferenceCount;
                result.unresolvedReferences.append(
                    QStringLiteral("%1 [UUID %2] parameter %3=%4")
                        .arg(object->name(), object->id(), parameter.key,
                             parameter.value));
            }
        }
    }

    result.unsupportedRecords = QStringList(unsupportedRecordNames.cbegin(),
                                            unsupportedRecordNames.cend());
    result.unsupportedParameters = QStringList(unsupportedParameterNames.cbegin(),
                                               unsupportedParameterNames.cend());
    result.unsupportedRecords.sort(Qt::CaseInsensitive);
    result.unsupportedParameters.sort(Qt::CaseInsensitive);
    if (!result.unsupportedRecords.isEmpty()) {
        result.warnings.append(
            QStringLiteral("Unsupported FDS records preserved under Additional Records: %1")
                .arg(result.unsupportedRecords.join(QStringLiteral(", "))));
    }
    if (!result.unsupportedParameters.isEmpty()) {
        result.warnings.append(
            QStringLiteral("Unsupported FDS fields preserved for advanced editing: %1")
                .arg(result.unsupportedParameters.join(QStringLiteral(", "))));
    }
    for (const QString& unresolved : result.unresolvedReferences) {
        result.warnings.append(
            QStringLiteral("Unresolved FDS reference preserved as raw text: %1")
                .arg(unresolved));
    }

    const bool hasMesh = std::any_of(objects.cbegin(), objects.cend(),
                                     [](const auto& object) {
                                         return object->keyword() == QStringLiteral("MESH");
                                     });
    if (!hasMesh) {
        result.warnings.append(QStringLiteral("Imported input contains no MESH record."));
    }
    project->setModified(false);
    result.project = std::move(project);
    return result;
}
