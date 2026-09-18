#include "comparison/ThreeWayComparisonEvidence.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace
{
QString sha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString resolvePath(const QDir& baseDirectory, const QString& value)
{
    if (value.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(value);
    return QDir::cleanPath(info.isAbsolute()
                               ? info.absoluteFilePath()
                               : QFileInfo(baseDirectory.filePath(value)).absoluteFilePath());
}

QString canonicalOrAbsolute(const QString& value)
{
    const QFileInfo info(value);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString markdownCell(QString value)
{
    value.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    value.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return value;
}

QString jsonStatus(bool value)
{
    return value ? QStringLiteral("PASS") : QStringLiteral("FAIL");
}

FdsEvidenceValidation parseSource(const QJsonObject& object,
                                  FdsEvidenceSourceKind kind,
                                  const QDir& baseDirectory)
{
    FdsEvidenceValidation validation;
    validation.source.kind = kind;
    validation.source.label = object.value(QStringLiteral("label")).toString(
        ThreeWayComparisonEvidence::sourceDisplayName(kind));
    validation.source.producer = object.value(QStringLiteral("producer")).toString().trimmed();
    validation.source.smvFile = resolvePath(
        baseDirectory, object.value(QStringLiteral("smv")).toString());
    validation.source.fdsFile = resolvePath(
        baseDirectory, object.value(QStringLiteral("fds")).toString());
    validation.source.projectFile = resolvePath(
        baseDirectory, object.value(QStringLiteral("project")).toString());
    validation.source.exportedAt = QDateTime::fromString(
        object.value(QStringLiteral("exportedAt")).toString(), Qt::ISODate);

    const auto requireFile = [&](const QString& path, const QString& description,
                                 const QString& extension) {
        if (path.isEmpty()) {
            validation.pendingIssues.append(
                QStringLiteral("%1 path is not supplied.").arg(description));
            return;
        }
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            validation.pendingIssues.append(
                QStringLiteral("%1 does not exist: %2").arg(description, path));
            return;
        }
        if (!extension.isEmpty() &&
            info.suffix().compare(extension, Qt::CaseInsensitive) != 0) {
            validation.invalidIssues.append(
                QStringLiteral("%1 must use the .%2 extension: %3")
                    .arg(description, extension, path));
        }
        if (info.size() <= 0) {
            validation.invalidIssues.append(
                QStringLiteral("%1 is empty: %2").arg(description, path));
        }
    };

    requireFile(validation.source.smvFile, QStringLiteral("Smokeview result"),
                QStringLiteral("smv"));
    requireFile(validation.source.fdsFile, QStringLiteral("FDS input"),
                QStringLiteral("fds"));

    const QString expectedProducer =
        kind == FdsEvidenceSourceKind::NativeFds
            ? QStringLiteral("FDS")
            : (kind == FdsEvidenceSourceKind::FireCae
                   ? QStringLiteral("FireCAE")
                   : QStringLiteral("PyroSim"));
    if (validation.source.producer.isEmpty()) {
        validation.pendingIssues.append(QStringLiteral("Producer is not supplied."));
    } else if (!validation.source.producer.contains(expectedProducer,
                                                    Qt::CaseInsensitive)) {
        validation.invalidIssues.append(
            QStringLiteral("Producer '%1' does not identify %2.")
                .arg(validation.source.producer, expectedProducer));
    }

    if (kind == FdsEvidenceSourceKind::FireCae) {
        requireFile(validation.source.projectFile,
                    QStringLiteral("FireCAE project evidence"),
                    QStringLiteral("firecae"));
    } else if (kind == FdsEvidenceSourceKind::PyroSim) {
        requireFile(validation.source.projectFile,
                    QStringLiteral("PyroSim project evidence"),
                    QStringLiteral("psm"));
        if (!validation.source.exportedAt.isValid()) {
            validation.pendingIssues.append(
                QStringLiteral("A valid ISO-8601 PyroSim export timestamp is required."));
        }
    }

    validation.smvSha256 = sha256(validation.source.smvFile);
    validation.fdsSha256 = sha256(validation.source.fdsFile);
    validation.projectSha256 = sha256(validation.source.projectFile);
    return validation;
}

QString pairName(const FdsThreeWayPairComparison& pair)
{
    return QStringLiteral("%1 vs %2")
        .arg(ThreeWayComparisonEvidence::sourceDisplayName(pair.reference),
             ThreeWayComparisonEvidence::sourceDisplayName(pair.candidate));
}

QJsonObject evidenceJson(const FdsEvidenceValidation& validation)
{
    QJsonObject object;
    object.insert(QStringLiteral("source"),
                  ThreeWayComparisonEvidence::sourceKey(validation.source.kind));
    object.insert(QStringLiteral("label"), validation.source.label);
    object.insert(QStringLiteral("producer"), validation.source.producer);
    object.insert(QStringLiteral("smv"), validation.source.smvFile);
    object.insert(QStringLiteral("fds"), validation.source.fdsFile);
    object.insert(QStringLiteral("project"), validation.source.projectFile);
    object.insert(QStringLiteral("exportedAt"),
                  validation.source.exportedAt.isValid()
                      ? validation.source.exportedAt.toString(Qt::ISODate)
                      : QString());
    object.insert(QStringLiteral("smvSha256"), validation.smvSha256);
    object.insert(QStringLiteral("fdsSha256"), validation.fdsSha256);
    object.insert(QStringLiteral("projectSha256"), validation.projectSha256);
    object.insert(QStringLiteral("ready"), validation.ready());
    object.insert(QStringLiteral("pendingIssues"),
                  QJsonArray::fromStringList(validation.pendingIssues));
    object.insert(QStringLiteral("invalidIssues"),
                  QJsonArray::fromStringList(validation.invalidIssues));
    object.insert(QStringLiteral("warnings"),
                  QJsonArray::fromStringList(validation.warnings));
    return object;
}
}

bool FdsThreeWayComparisonResult::evidenceReady() const
{
    return errorMessage.isEmpty() && evidence.size() == 3 &&
           std::all_of(evidence.cbegin(), evidence.cend(),
                       [](const FdsEvidenceValidation& item) {
                           return item.ready();
                       });
}

bool FdsThreeWayComparisonResult::comparisonsPassed() const
{
    return evidenceReady() && pairs.size() == 3 &&
           std::all_of(pairs.cbegin(), pairs.cend(),
                       [](const FdsThreeWayPairComparison& pair) {
                           return pair.passed();
                       });
}

QString FdsThreeWayComparisonResult::statusCode() const
{
    if (!errorMessage.isEmpty()) {
        return QStringLiteral("INVALID_MANIFEST");
    }
    for (const FdsEvidenceValidation& item : evidence) {
        if (!item.invalidIssues.isEmpty()) {
            return QStringLiteral("INVALID_EVIDENCE");
        }
    }
    if (!evidenceReady()) {
        return QStringLiteral("PENDING_EVIDENCE");
    }
    return comparisonsPassed() ? QStringLiteral("PASS")
                               : QStringLiteral("COMPARISON_FAILED");
}

QString FdsThreeWayComparisonResult::markdownReport() const
{
    QString report;
    QTextStream stream(&report);
    stream << "# FireCAE three-source FDS comparison evidence\n\n";
    stream << "- Case: `" << caseId << "`\n";
    stream << "- Status: **" << statusCode() << "**\n";
    stream << "- Manifest: `" << QDir::toNativeSeparators(manifestFile) << "`\n\n";
    if (!errorMessage.isEmpty()) {
        stream << "## Manifest error\n\n" << errorMessage << "\n";
        return report;
    }
    stream << "## Source evidence\n\n";
    stream << "| Source | Producer | SMV SHA-256 | FDS SHA-256 | Project SHA-256 | Status |\n";
    stream << "|---|---|---|---|---|---|\n";
    for (const FdsEvidenceValidation& item : evidence) {
        stream << "| " << ThreeWayComparisonEvidence::sourceDisplayName(item.source.kind) << " | "
               << markdownCell(item.source.producer) << " | `"
               << item.smvSha256 << "` | `" << item.fdsSha256 << "` | `"
               << item.projectSha256 << "` | "
               << (item.ready() ? "READY" : "NOT READY") << " |\n";
    }
    stream << "\n## Evidence findings\n\n";
    bool hasFindings = false;
    for (const FdsEvidenceValidation& item : evidence) {
        for (const QString& issue : item.pendingIssues) {
            stream << "- [PENDING] " << ThreeWayComparisonEvidence::sourceDisplayName(item.source.kind)
                   << ": " << issue << "\n";
            hasFindings = true;
        }
        for (const QString& issue : item.invalidIssues) {
            stream << "- [INVALID] " << ThreeWayComparisonEvidence::sourceDisplayName(item.source.kind)
                   << ": " << issue << "\n";
            hasFindings = true;
        }
        for (const QString& warning : item.warnings) {
            stream << "- [INFO] " << ThreeWayComparisonEvidence::sourceDisplayName(item.source.kind)
                   << ": " << warning << "\n";
            hasFindings = true;
        }
    }
    for (const QString& warning : warnings) {
        stream << "- [INFO] " << warning << "\n";
        hasFindings = true;
    }
    if (!hasFindings) {
        stream << "- No evidence issues.\n";
    }
    stream << "\n## Pair comparisons\n\n";
    stream << "| Pair | FDS input | Result quantities | Overall |\n";
    stream << "|---|---|---|---|\n";
    for (const FdsThreeWayPairComparison& pair : pairs) {
        stream << "| " << pairName(pair) << " | "
               << (pair.inputComparison.success() &&
                           pair.inputComparison.semanticallyEquivalent()
                       ? "PASS" : "FAIL")
               << " | "
               << (pair.resultComparison.success() && pair.resultComparison.passed()
                       ? "PASS" : "FAIL")
               << " | " << (pair.passed() ? "PASS" : "FAIL") << " |\n";
    }
    if (pairs.isEmpty()) {
        stream << "| Not run | PENDING | PENDING | PENDING |\n";
    }
    stream << "\n> A source label alone is not accepted as provenance. PyroSim evidence "
              "requires an independent `.psm` project, a result set, producer metadata, "
              "and an export timestamp. File hashes provide traceability; they do not "
              "cryptographically prove which desktop application performed an export.\n";
    return report;
}

QString FdsThreeWayComparisonResult::jsonReport() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("caseId"), caseId);
    root.insert(QStringLiteral("status"), statusCode());
    root.insert(QStringLiteral("manifest"), manifestFile);
    root.insert(QStringLiteral("error"), errorMessage);
    root.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
    QJsonArray sources;
    for (const FdsEvidenceValidation& item : evidence) {
        sources.append(evidenceJson(item));
    }
    root.insert(QStringLiteral("sources"), sources);
    QJsonArray comparisons;
    for (const FdsThreeWayPairComparison& pair : pairs) {
        QJsonObject object;
        object.insert(QStringLiteral("reference"),
                      ThreeWayComparisonEvidence::sourceKey(pair.reference));
        object.insert(QStringLiteral("candidate"),
                      ThreeWayComparisonEvidence::sourceKey(pair.candidate));
        object.insert(QStringLiteral("inputStatus"),
                      jsonStatus(pair.inputComparison.success() &&
                                 pair.inputComparison.semanticallyEquivalent()));
        object.insert(QStringLiteral("resultStatus"),
                      jsonStatus(pair.resultComparison.success() &&
                                 pair.resultComparison.passed()));
        object.insert(QStringLiteral("status"), jsonStatus(pair.passed()));
        object.insert(QStringLiteral("inputReferenceSha256"),
                      pair.inputComparison.referenceSha256);
        object.insert(QStringLiteral("inputCandidateSha256"),
                      pair.inputComparison.candidateSha256);
        object.insert(QStringLiteral("quantityCount"),
                      static_cast<int>(pair.resultComparison.quantities.size()));
        comparisons.append(object);
    }
    root.insert(QStringLiteral("comparisons"), comparisons);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

FdsThreeWayComparisonResult ThreeWayComparisonEvidence::loadAndCompare(
    const QString& manifestFile)
{
    FdsThreeWayComparisonResult result;
    result.manifestFile = QFileInfo(manifestFile).absoluteFilePath();
    QFile file(result.manifestFile);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("Comparison manifest cannot be read: %1")
                                  .arg(result.manifestFile);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorMessage = QStringLiteral("Invalid comparison manifest JSON: %1")
                                  .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        result.errorMessage = QStringLiteral("Unsupported comparison manifest schemaVersion.");
        return result;
    }
    result.caseId = root.value(QStringLiteral("caseId")).toString().trimmed();
    if (result.caseId.isEmpty()) {
        result.errorMessage = QStringLiteral("Comparison manifest caseId is required.");
        return result;
    }
    const QJsonObject sources = root.value(QStringLiteral("sources")).toObject();
    const QDir baseDirectory = QFileInfo(result.manifestFile).absoluteDir();
    const QVector<FdsEvidenceSourceKind> kinds = {
        FdsEvidenceSourceKind::NativeFds,
        FdsEvidenceSourceKind::FireCae,
        FdsEvidenceSourceKind::PyroSim};
    for (FdsEvidenceSourceKind kind : kinds) {
        result.evidence.append(parseSource(
            sources.value(sourceKey(kind)).toObject(), kind, baseDirectory));
    }

    for (int left = 0; left < result.evidence.size(); ++left) {
        for (int right = left + 1; right < result.evidence.size(); ++right) {
            FdsEvidenceValidation& first = result.evidence[left];
            FdsEvidenceValidation& second = result.evidence[right];
            const auto rejectSharedPath = [&](const QString& firstPath,
                                              const QString& secondPath,
                                              const QString& description) {
                if (firstPath.isEmpty() || secondPath.isEmpty()) {
                    return;
                }
                if (canonicalOrAbsolute(firstPath).compare(
                        canonicalOrAbsolute(secondPath), Qt::CaseInsensitive) == 0) {
                    const QString issue = QStringLiteral(
                        "%1 shares the same physical path with %2 (%3).")
                        .arg(sourceDisplayName(first.source.kind),
                             sourceDisplayName(second.source.kind), description);
                    first.invalidIssues.append(issue);
                    second.invalidIssues.append(issue);
                }
            };
            rejectSharedPath(first.source.smvFile, second.source.smvFile,
                             QStringLiteral("SMV"));
            rejectSharedPath(first.source.fdsFile, second.source.fdsFile,
                             QStringLiteral("FDS"));
            if (!first.fdsSha256.isEmpty() &&
                first.fdsSha256 == second.fdsSha256) {
                result.warnings.append(
                    QStringLiteral("%1 and %2 FDS inputs have identical content hashes; "
                                   "their independent paths are retained as provenance.")
                        .arg(sourceDisplayName(first.source.kind),
                             sourceDisplayName(second.source.kind)));
            }
        }
    }

    if (!result.evidenceReady()) {
        return result;
    }
    const QVector<QPair<int, int>> pairIndexes = {{0, 1}, {0, 2}, {1, 2}};
    for (const QPair<int, int>& indexes : pairIndexes) {
        const FdsEvidenceValidation& reference = result.evidence.at(indexes.first);
        const FdsEvidenceValidation& candidate = result.evidence.at(indexes.second);
        FdsThreeWayPairComparison pair;
        pair.reference = reference.source.kind;
        pair.candidate = candidate.source.kind;
        pair.inputComparison = FdsInputComparator::compareFiles(
            reference.source.fdsFile, candidate.source.fdsFile);
        pair.resultComparison = FdsResultComparator::compareSmvFiles(
            reference.source.smvFile, candidate.source.smvFile);
        pair.attempted = true;
        result.pairs.append(std::move(pair));
    }
    return result;
}

FdsThreeWayReportArtifacts ThreeWayComparisonEvidence::writeReports(
    const FdsThreeWayComparisonResult& result,
    const QString& outputDirectory,
    const QString& baseFileName)
{
    FdsThreeWayReportArtifacts artifacts;
    QDir directory(outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        artifacts.errorMessage = QStringLiteral("Cannot create report directory: %1")
                                     .arg(outputDirectory);
        return artifacts;
    }
    const QString safeBase = baseFileName.trimmed().isEmpty()
        ? QStringLiteral("three-way-comparison") : baseFileName.trimmed();
    artifacts.markdownFile = directory.filePath(safeBase + QStringLiteral(".md"));
    artifacts.jsonFile = directory.filePath(safeBase + QStringLiteral(".json"));
    const auto write = [&](const QString& path, const QString& contents) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            artifacts.errorMessage = QStringLiteral("Cannot write report: %1").arg(path);
            return false;
        }
        file.write(contents.toUtf8());
        if (!file.commit()) {
            artifacts.errorMessage = QStringLiteral("Cannot commit report: %1").arg(path);
            return false;
        }
        return true;
    };
    if (!write(artifacts.markdownFile, result.markdownReport()) ||
        !write(artifacts.jsonFile, result.jsonReport())) {
        return artifacts;
    }
    return artifacts;
}

QString ThreeWayComparisonEvidence::sourceKey(FdsEvidenceSourceKind kind)
{
    switch (kind) {
    case FdsEvidenceSourceKind::NativeFds:
        return QStringLiteral("nativeFds");
    case FdsEvidenceSourceKind::FireCae:
        return QStringLiteral("fireCae");
    case FdsEvidenceSourceKind::PyroSim:
        return QStringLiteral("pyroSim");
    }
    return {};
}

QString ThreeWayComparisonEvidence::sourceDisplayName(FdsEvidenceSourceKind kind)
{
    switch (kind) {
    case FdsEvidenceSourceKind::NativeFds:
        return QStringLiteral("Native FDS");
    case FdsEvidenceSourceKind::FireCae:
        return QStringLiteral("FireCAE");
    case FdsEvidenceSourceKind::PyroSim:
        return QStringLiteral("PyroSim");
    }
    return {};
}
