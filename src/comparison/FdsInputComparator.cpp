#include "comparison/FdsInputComparator.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FdsImporter.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
struct SemanticRecord
{
    QString keyword;
    QString identity;
    QMap<QString, QStringList> parameters;

    QString signature() const
    {
        QStringList fields{keyword};
        for (auto iterator = parameters.cbegin(); iterator != parameters.cend(); ++iterator) {
            fields.append(iterator.key() + QLatin1Char('=') +
                          iterator.value().join(QChar(0x1e)));
        }
        return fields.join(QChar(0x1f));
    }
};

QString fileSha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

QString escapeCsv(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString escapeHtml(QString value)
{
    return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString normalizeScalar(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 &&
        ((value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')) ||
         (value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')))) {
        value = value.mid(1, value.size() - 2);
        return QStringLiteral("S:") + value;
    }
    const QString upper = value.toUpper();
    if (upper == QStringLiteral(".TRUE.") || upper == QStringLiteral("T"))
        return QStringLiteral("B:TRUE");
    if (upper == QStringLiteral(".FALSE.") || upper == QStringLiteral("F"))
        return QStringLiteral("B:FALSE");
    bool numeric = false;
    const double number = QLocale::c().toDouble(value, &numeric);
    if (numeric && std::isfinite(number)) {
        return QStringLiteral("N:") + QLocale::c().toString(number, 'g', 15);
    }
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return QStringLiteral("R:") + value;
}

QString normalizeValue(const QString& value)
{
    QStringList parts;
    QString current;
    QChar quote;
    int parentheses = 0;
    for (int index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (!quote.isNull()) {
            current.append(character);
            if (character == quote) {
                if (index + 1 < value.size() && value.at(index + 1) == quote) {
                    current.append(value.at(++index));
                } else {
                    quote = {};
                }
            }
        } else if (character == QLatin1Char('\'') || character == QLatin1Char('"')) {
            quote = character;
            current.append(character);
        } else if (character == QLatin1Char('(')) {
            ++parentheses;
            current.append(character);
        } else if (character == QLatin1Char(')')) {
            parentheses = std::max(0, parentheses - 1);
            current.append(character);
        } else if (character == QLatin1Char(',') && parentheses == 0) {
            parts.append(normalizeScalar(current));
            current.clear();
        } else {
            current.append(character);
        }
    }
    parts.append(normalizeScalar(current));
    return parts.join(QLatin1Char(','));
}

QString displayNormalized(QString value)
{
    value.replace(QStringLiteral("S:"), QString{});
    value.replace(QStringLiteral("N:"), QString{});
    value.replace(QStringLiteral("B:"), QString{});
    value.replace(QStringLiteral("R:"), QString{});
    return value;
}

QString keywordForType(FcObjectType type)
{
    switch (type) {
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
    case FcObjectType::InitialCondition: return QStringLiteral("INIT");
    case FcObjectType::Output: return QStringLiteral("OUTPUT");
    default: return QStringLiteral("FDS");
    }
}

void collectNamelists(const FcObject::Ptr& object,
                      QVector<std::shared_ptr<FcFdsNamelist>>& records)
{
    if (!object) return;
    if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
        records.append(namelist);
    }
    for (const FcObject::Ptr& child : object->children()) {
        collectNamelists(child, records);
    }
}

QVector<SemanticRecord> semanticRecords(const FcProject& project,
                                        FdsInputComparison& summary,
                                        bool reference)
{
    QVector<std::shared_ptr<FcFdsNamelist>> objects;
    if (project.document()) {
        for (const auto& group : project.document()->groups()) {
            collectNamelists(group, objects);
        }
    }
    std::sort(objects.begin(), objects.end(),
              [](const auto& left, const auto& right) {
                  return left->sequenceIndex() < right->sequenceIndex();
              });
    QVector<SemanticRecord> records;
    const QStringList anchors = {
        QStringLiteral("XB"), QStringLiteral("XYZ"), QStringLiteral("QUANTITY"),
        QStringLiteral("SURF_ID"), QStringLiteral("DEVC_ID"),
        QStringLiteral("CTRL_ID"), QStringLiteral("NODE_ID"),
        QStringLiteral("DUCT_ID"), QStringLiteral("PBX"),
        QStringLiteral("PBY"), QStringLiteral("PBZ")};
    for (const auto& object : objects) {
        SemanticRecord record;
        record.keyword = object->keyword().trimmed().toUpper();
        for (const FcFdsParameter& parameter : object->parameters()) {
            record.parameters[parameter.key.trimmed().toUpper()].append(
                normalizeValue(parameter.value));
        }
        QString identity = record.keyword;
        if (!object->fdsId().trimmed().isEmpty()) {
            identity += QStringLiteral(" ID=") + object->fdsId().trimmed().toUpper();
        } else {
            for (const QString& anchor : anchors) {
                if (!record.parameters.contains(anchor)) continue;
                identity += QStringLiteral(" %1=%2").arg(
                    anchor, record.parameters.value(anchor).join(QStringLiteral(";")));
                break;
            }
        }
        record.identity = identity;
        records.append(record);

        QMap<QString, int>& counts = reference ? summary.referenceKeywordCounts
                                               : summary.candidateKeywordCounts;
        ++counts[record.keyword];
        if (record.keyword == QStringLiteral("MESH")) {
            if (reference) ++summary.referenceMeshCount;
            else ++summary.candidateMeshCount;
            const QString ijk = record.parameters.value(QStringLiteral("IJK")).value(0);
            const QStringList parts = displayNormalized(ijk).split(QLatin1Char(','));
            if (parts.size() == 3) {
                qint64 cells = 1;
                bool valid = true;
                for (const QString& part : parts) {
                    bool ok = false;
                    const qint64 count = part.toLongLong(&ok);
                    valid = valid && ok && count > 0 &&
                            cells <= std::numeric_limits<qint64>::max() / count;
                    if (valid) cells *= count;
                }
                if (valid) {
                    if (reference) summary.referenceCellCount += cells;
                    else summary.candidateCellCount += cells;
                }
            }
        }
    }
    // Duplicate namelists without IDs (for example OBST records) are ranked by
    // canonical content.  This makes a pure record-order change disappear from
    // the semantic comparison while still keeping every duplicate addressable.
    QMap<QString, QVector<int>> duplicateGroups;
    for (int index = 0; index < records.size(); ++index) {
        duplicateGroups[records.at(index).identity].append(index);
    }
    for (auto iterator = duplicateGroups.cbegin(); iterator != duplicateGroups.cend();
         ++iterator) {
        QVector<int> indices = iterator.value();
        if (indices.size() < 2) continue;
        std::sort(indices.begin(), indices.end(), [&records](int left, int right) {
            return records.at(left).signature() < records.at(right).signature();
        });
        for (int rank = 0; rank < indices.size(); ++rank) {
            records[indices.at(rank)].identity += QStringLiteral(" #%1").arg(rank + 1);
        }
    }
    if (reference) summary.referenceRecordCount = records.size();
    else summary.candidateRecordCount = records.size();
    return records;
}

QString rawDiff(const QString& referenceText, const QString& candidateText)
{
    if (referenceText == candidateText) return QStringLiteral("No raw text differences.\n");
    const QStringList left = referenceText.split(QLatin1Char('\n'));
    const QStringList right = candidateText.split(QLatin1Char('\n'));
    const int maximum = std::max(left.size(), right.size());
    QString output = QStringLiteral("--- reference\n+++ candidate\n");
    int emitted = 0;
    for (int index = 0; index < maximum; ++index) {
        const QString leftLine = index < left.size() ? left.at(index) : QString{};
        const QString rightLine = index < right.size() ? right.at(index) : QString{};
        if (leftLine == rightLine) continue;
        output += QStringLiteral("@@ line %1 @@\n").arg(index + 1);
        if (index < left.size()) output += QStringLiteral("- %1\n").arg(leftLine);
        if (index < right.size()) output += QStringLiteral("+ %1\n").arg(rightLine);
        if (++emitted >= 500) {
            output += QStringLiteral("... raw diff truncated after 500 changed line positions ...\n");
            break;
        }
    }
    return output;
}

QVector<QPair<QString, QString>> rawNamelists(const QString& text)
{
    QVector<QPair<QString, QString>> result;
    for (int start = 0; start < text.size();) {
        start = text.indexOf(QLatin1Char('&'), start);
        if (start < 0) break;
        QChar quote;
        int end = start + 1;
        for (; end < text.size(); ++end) {
            const QChar character = text.at(end);
            if (!quote.isNull()) {
                if (character == quote) {
                    if (end + 1 < text.size() && text.at(end + 1) == quote) ++end;
                    else quote = {};
                }
            } else if (character == QLatin1Char('\'') || character == QLatin1Char('"')) {
                quote = character;
            } else if (character == QLatin1Char('/')) {
                break;
            }
        }
        const QString body = text.mid(start + 1, end - start - 1).trimmed();
        const QRegularExpressionMatch keyword =
            QRegularExpression(QStringLiteral("^([A-Za-z0-9_]+)\\b")).match(body);
        if (keyword.hasMatch()) {
            result.append({keyword.captured(1).toUpper(), body});
        }
        start = std::max(end + 1, start + 1);
    }
    return result;
}

QString rawParameter(const QString& body, const QString& key)
{
    const QRegularExpression expression(
        QStringLiteral("(?:^|[,\\s])%1\\s*=\\s*(('(?:''|[^'])*')|"
                       "(\"(?:\"\"|[^\"])*\")|([^,\\s/]+))")
            .arg(QRegularExpression::escape(key)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(body);
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}

void augmentSpecialRecords(QVector<SemanticRecord>& records,
                           const QString& sourceText,
                           FdsInputComparison& summary,
                           bool reference)
{
    QMap<QString, int>& counts = reference ? summary.referenceKeywordCounts
                                           : summary.candidateKeywordCounts;
    const auto addOrMerge = [&](const QString& keyword,
                                const QMap<QString, QStringList>& parameters) {
        auto existing = std::find_if(records.begin(), records.end(),
                                     [&keyword](const SemanticRecord& record) {
                                         return record.keyword == keyword;
                                     });
        if (existing == records.end()) {
            records.append({keyword, keyword, parameters});
            ++counts[keyword];
        } else {
            for (auto iterator = parameters.cbegin(); iterator != parameters.cend();
                 ++iterator) existing->parameters[iterator.key()] = iterator.value();
        }
    };
    for (const auto& raw : rawNamelists(sourceText)) {
        if (raw.first == QStringLiteral("HEAD")) {
            QMap<QString, QStringList> parameters;
            for (const QString& key : {QStringLiteral("CHID"), QStringLiteral("TITLE")}) {
                const QString value = rawParameter(raw.second, key);
                if (!value.isEmpty()) parameters[key].append(normalizeValue(value));
            }
            addOrMerge(raw.first, parameters);
        } else if (raw.first == QStringLiteral("TIME")) {
            QMap<QString, QStringList> parameters;
            const QString value = rawParameter(raw.second, QStringLiteral("T_END"));
            if (!value.isEmpty()) parameters[QStringLiteral("T_END")].append(
                normalizeValue(value));
            addOrMerge(raw.first, parameters);
        } else if (raw.first == QStringLiteral("TAIL")) {
            addOrMerge(raw.first, {});
        }
    }
    if (reference) summary.referenceRecordCount = records.size();
    else summary.candidateRecordCount = records.size();
}

bool acceptableDifference(const QString& recordKey, const QString& parameter,
                          QString& explanation)
{
    if (recordKey.startsWith(QStringLiteral("HEAD")) &&
        (parameter == QStringLiteral("CHID") || parameter == QStringLiteral("TITLE"))) {
        explanation = QStringLiteral(
            "HEAD CHID/TITLE may differ to keep independently generated result directories separate.");
        return true;
    }
    return false;
}

void appendParameterDifferences(const SemanticRecord& reference,
                                const SemanticRecord& candidate,
                                FdsInputComparison& result)
{
    QStringList keys = reference.parameters.keys();
    for (const QString& key : candidate.parameters.keys()) {
        if (!keys.contains(key)) keys.append(key);
    }
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        const QString referenceValue =
            reference.parameters.value(key).join(QStringLiteral("; "));
        const QString candidateValue =
            candidate.parameters.value(key).join(QStringLiteral("; "));
        if (referenceValue == candidateValue) continue;
        FdsInputDifference difference;
        difference.category = QStringLiteral("parameter");
        difference.recordKey = reference.identity;
        difference.parameter = key;
        difference.referenceValue = displayNormalized(referenceValue);
        difference.candidateValue = displayNormalized(candidateValue);
        difference.acceptable = acceptableDifference(
            reference.identity, key, difference.explanation);
        result.differences.append(difference);
    }
}

void collectMappings(const FcObject::Ptr& object, QVector<FdsUuidIdMapping>& mappings)
{
    if (!object) return;
    if (const auto fdsObject = std::dynamic_pointer_cast<FcFdsObject>(object)) {
        QString keyword;
        if (const auto namelist = std::dynamic_pointer_cast<FcFdsNamelist>(object)) {
            keyword = namelist->keyword();
        } else {
            keyword = keywordForType(object->type());
        }
        mappings.append({object->id(), keyword, fdsObject->fdsId(), object->name()});
    }
    for (const FcObject::Ptr& child : object->children()) collectMappings(child, mappings);
}
}

bool FdsInputComparison::semanticallyEquivalent() const
{
    return success() && unacceptableDifferenceCount() == 0;
}

int FdsInputComparison::unacceptableDifferenceCount() const
{
    return static_cast<int>(std::count_if(
        differences.cbegin(), differences.cend(),
        [](const FdsInputDifference& difference) { return !difference.acceptable; }));
}

QString FdsInputComparison::csvReport() const
{
    QString csv = QStringLiteral("record,key,reference,candidate,status,explanation\n");
    csv += QStringLiteral("provenance,sha256,%1,%2,%3,\n")
               .arg(escapeCsv(referenceSha256), escapeCsv(candidateSha256),
                    referenceSha256 == candidateSha256 ? QStringLiteral("MATCH")
                                                       : QStringLiteral("DIFFERENT"));
    csv += QStringLiteral("summary,record_count,%1,%2,INFO,\n")
               .arg(referenceRecordCount).arg(candidateRecordCount);
    csv += QStringLiteral("summary,mesh_count,%1,%2,INFO,"
                          "MESH definitions before MULT expansion\n")
               .arg(referenceMeshCount).arg(candidateMeshCount);
    csv += QStringLiteral("summary,total_cells,%1,%2,INFO,"
                          "Cells in MESH definitions before MULT expansion\n")
               .arg(referenceCellCount).arg(candidateCellCount);
    for (const FdsInputDifference& difference : differences) {
        csv += QStringLiteral("%1,%2,%3,%4,%5,%6\n")
                   .arg(escapeCsv(difference.category),
                        escapeCsv(difference.recordKey +
                                  (difference.parameter.isEmpty()
                                       ? QString{}
                                       : QStringLiteral("/") + difference.parameter)),
                        escapeCsv(difference.referenceValue),
                        escapeCsv(difference.candidateValue),
                        difference.acceptable ? QStringLiteral("ACCEPTABLE")
                                              : QStringLiteral("DIFFERENT"),
                        escapeCsv(difference.explanation));
    }
    return csv;
}

QString FdsInputComparison::htmlReport() const
{
    QString html = QStringLiteral(
        "<h2>FDS input comparison</h2>"
        "<p><b>Status:</b> %1<br><b>Reference:</b> %2<br><b>Candidate:</b> %3</p>"
        "<table><tr><th>Metric</th><th>Reference</th><th>Candidate</th></tr>"
        "<tr><td>SHA-256</td><td><code>%4</code></td><td><code>%5</code></td></tr>"
        "<tr><td>Namelists</td><td>%6</td><td>%7</td></tr>"
        "<tr><td>MESH definitions</td><td>%8</td><td>%9</td></tr>"
        "<tr><td>Cells before MULT expansion</td><td>%10</td><td>%11</td></tr></table>")
        .arg(semanticallyEquivalent() ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             escapeHtml(referenceFile), escapeHtml(candidateFile), referenceSha256,
             candidateSha256)
        .arg(referenceRecordCount).arg(candidateRecordCount)
        .arg(referenceMeshCount).arg(candidateMeshCount)
        .arg(referenceCellCount).arg(candidateCellCount);
    html += QStringLiteral(
        "<h3>Semantic differences (record order ignored)</h3>"
        "<table><tr><th>Record</th><th>Parameter</th><th>Reference</th>"
        "<th>Candidate</th><th>Status / explanation</th></tr>");
    if (differences.isEmpty()) {
        html += QStringLiteral("<tr><td colspan='5'>No semantic differences.</td></tr>");
    }
    for (const FdsInputDifference& difference : differences) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td>"
                               "<td class='%5'>%6%7</td></tr>")
                    .arg(escapeHtml(difference.recordKey),
                         escapeHtml(difference.parameter),
                         escapeHtml(difference.referenceValue),
                         escapeHtml(difference.candidateValue),
                         difference.acceptable ? QStringLiteral("acceptable")
                                               : QStringLiteral("different"),
                         difference.acceptable ? QStringLiteral("ACCEPTABLE")
                                               : QStringLiteral("DIFFERENT"),
                         difference.explanation.isEmpty()
                             ? QString{}
                             : QStringLiteral(" — ") + escapeHtml(difference.explanation));
    }
    html += QStringLiteral("</table><h3>Raw text differences</h3><pre>%1</pre>")
                .arg(escapeHtml(rawTextDiff));
    return html;
}

FdsInputComparison FdsInputComparator::compareFiles(const QString& referenceFile,
                                                     const QString& candidateFile)
{
    FdsInputComparison result;
    result.referenceFile = QFileInfo(referenceFile).absoluteFilePath();
    result.candidateFile = QFileInfo(candidateFile).absoluteFilePath();
    QFile reference(result.referenceFile);
    QFile candidate(result.candidateFile);
    if (!reference.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Reference FDS input cannot be read: %1")
                                  .arg(result.referenceFile);
        return result;
    }
    if (!candidate.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Candidate FDS input cannot be read: %1")
                                  .arg(result.candidateFile);
        return result;
    }
    const QString referenceText = QString::fromUtf8(reference.readAll());
    const QString candidateText = QString::fromUtf8(candidate.readAll());
    result.referenceSha256 = fileSha256(result.referenceFile);
    result.candidateSha256 = fileSha256(result.candidateFile);
    result.rawTextDiff = rawDiff(referenceText, candidateText);

    const FdsImportResult referenceImport = FdsImporter().importText(
        referenceText, result.referenceFile);
    const FdsImportResult candidateImport = FdsImporter().importText(
        candidateText, result.candidateFile);
    if (!referenceImport.success()) {
        result.errorMessage = QStringLiteral("Reference FDS input cannot be parsed: %1")
                                  .arg(referenceImport.errorMessage);
        return result;
    }
    if (!candidateImport.success()) {
        result.errorMessage = QStringLiteral("Candidate FDS input cannot be parsed: %1")
                                  .arg(candidateImport.errorMessage);
        return result;
    }
    result.warnings.append(referenceImport.warnings);
    result.warnings.append(candidateImport.warnings);
    QVector<SemanticRecord> referenceRecords =
        semanticRecords(*referenceImport.project, result, true);
    QVector<SemanticRecord> candidateRecords =
        semanticRecords(*candidateImport.project, result, false);
    augmentSpecialRecords(referenceRecords, referenceText, result, true);
    augmentSpecialRecords(candidateRecords, candidateText, result, false);

    QMap<QString, SemanticRecord> referenceByKey;
    QMap<QString, SemanticRecord> candidateByKey;
    for (const SemanticRecord& record : referenceRecords)
        referenceByKey.insert(record.identity, record);
    for (const SemanticRecord& record : candidateRecords)
        candidateByKey.insert(record.identity, record);
    QStringList keys = referenceByKey.keys();
    for (const QString& key : candidateByKey.keys()) if (!keys.contains(key)) keys.append(key);
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        if (!candidateByKey.contains(key)) {
            result.differences.append({QStringLiteral("missing-record"), key, {},
                                       QStringLiteral("present"), QStringLiteral("missing"),
                                       false, QStringLiteral("Record is missing from the candidate input.")});
        } else if (!referenceByKey.contains(key)) {
            result.differences.append({QStringLiteral("candidate-only-record"), key, {},
                                       QStringLiteral("missing"), QStringLiteral("present"),
                                       false, QStringLiteral("Candidate contains an additional record.")});
        } else {
            appendParameterDifferences(referenceByKey.value(key),
                                       candidateByKey.value(key), result);
        }
    }
    return result;
}

QVector<FdsUuidIdMapping> FdsInputComparator::uuidToFdsIdMappings(
    const FcProject& project)
{
    QVector<FdsUuidIdMapping> mappings;
    if (project.document()) {
        for (const auto& group : project.document()->groups()) collectMappings(group, mappings);
    }
    std::sort(mappings.begin(), mappings.end(),
              [](const FdsUuidIdMapping& left, const FdsUuidIdMapping& right) {
                  if (left.keyword != right.keyword) return left.keyword < right.keyword;
                  if (left.fdsId != right.fdsId) return left.fdsId < right.fdsId;
                  return left.uuid < right.uuid;
              });
    return mappings;
}
