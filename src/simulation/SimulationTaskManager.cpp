#include "simulation/SimulationTaskManager.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <cmath>

namespace {
bool saveBytes(const QString& path, const QByteArray& bytes, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot freeze task input: %1").arg(path);
        return false;
    }
    return true;
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file)
        ? QString::fromLatin1(hash.result().toHex()) : QString{};
}

bool copyStableFile(const QString& source, const QString& destination, QString* hash, QString* error)
{
    const QFileInfo before(source);
    const QString expected = fileSha256(source);
    if (expected.isEmpty() || !QDir().mkpath(QFileInfo(destination).absolutePath()) ||
        !QFile::copy(source, destination)) {
        if (error) *error = QStringLiteral("Cannot freeze RESTART file: %1").arg(source);
        return false;
    }
    const QFileInfo after(source);
    if (before.size() != after.size() || before.lastModified() != after.lastModified() ||
        expected != fileSha256(source) || expected != fileSha256(destination)) {
        if (error) *error = QStringLiteral("RESTART source changed while copying; stop its writer before retrying: %1").arg(source);
        return false;
    }
    if (hash) *hash = expected;
    return true;
}

QJsonObject jsonObject(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}

double logEndTime(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1.0;
    const QRegularExpression expression(QStringLiteral(R"(Total Time:\s*([-+0-9.eEdD]+)\s*s)"));
    auto matches = expression.globalMatch(QString::fromLocal8Bit(file.readAll()));
    double result = -1.0;
    while (matches.hasNext()) {
        QString token = matches.next().captured(1);
        token.replace('D', 'E', Qt::CaseInsensitive);
        bool ok = false;
        const double value = token.toDouble(&ok);
        if (ok && std::isfinite(value)) result = qMax(result, value);
    }
    return result;
}

// Keep character offsets so quoted strings can be rewritten without altering
// other FDS fields. Comments and strings cannot introduce apparent assignments.
QString maskFds(QString text, bool maskStrings)
{
    QChar quote;
    bool comment = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (comment) {
            if (ch == QLatin1Char('\n')) comment = false;
            else text[i] = QLatin1Char(' ');
        } else if (!quote.isNull()) {
            if (maskStrings) text[i] = QLatin1Char(' ');
            if (ch == quote) {
                if (i + 1 < text.size() && text.at(i + 1) == quote) {
                    if (maskStrings) text[i + 1] = QLatin1Char(' ');
                    ++i;
                } else quote = {};
            }
        } else if (ch == QLatin1Char('!')) {
            comment = true;
            text[i] = QLatin1Char(' ');
        } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
            quote = ch;
            if (maskStrings) text[i] = QLatin1Char(' ');
        }
    }
    return text;
}

bool freezeRestart(FdsRunRequest& request, const QDir& inputDirectory,
                   const QDir& runDirectory, int meshCount, QString* error)
{
    const QString chid = FdsRunner::caseIdFromInput(request.inputFilePath);
    if (request.restartSourceChid.isEmpty()) request.restartSourceChid = chid;
    const QString sourceChid = request.restartSourceChid;
    if (sourceChid.contains('/') || sourceChid.contains('\\') || sourceChid == "." || sourceChid == "..") {
        if (error) *error = QStringLiteral("RESTART_CHID must be a case identifier; use restartSourceDirectory for its directory.");
        return false;
    }
    request.restartAppend = sourceChid == chid;
    if (meshCount < 1) {
        if (error) *error = QStringLiteral("RESTART requires an explicit, unchanged MESH layout.");
        return false;
    }
    QDir source(request.restartSnapshotDirectory.isEmpty()
        ? (request.restartSourceDirectory.isEmpty() ? inputDirectory.absolutePath() : request.restartSourceDirectory)
        : request.restartSnapshotDirectory);
    QString parentTaskId;
    const auto checkpoint = [&](const QDir& dir, int mesh) {
        return dir.filePath(sourceChid + QStringLiteral("_%1.restart").arg(mesh));
    };
    // UI continuation means the latest successful run of this exact source
    // input and CHID. An explicit directory or local checkpoint takes priority.
    if (request.restartSnapshotDirectory.isEmpty() && request.restartSourceDirectory.isEmpty() &&
        !QFileInfo::exists(checkpoint(source, 1))) {
        QDateTime latest;
        const QDir runs(QDir(request.outputRootDirectory).filePath(".firecae-runs"));
        for (const QFileInfo& entry : runs.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir prior(entry.absoluteFilePath());
            const auto manifest = jsonObject(prior.filePath("run-manifest.json"));
            const auto result = jsonObject(prior.filePath("run-result.json"));
            const QString origin = manifest.value("sourceInput").toString();
            const QDateTime finished = QDateTime::fromString(result.value("finishedAt").toString(), Qt::ISODateWithMs);
            if (!result.value("success").toBool() ||
                QFileInfo(origin).absoluteFilePath().compare(QFileInfo(request.sourceInputFilePath).absoluteFilePath(),
                                                           Qt::CaseInsensitive) != 0 ||
                QFileInfo(result.value("smvPath").toString()).completeBaseName() != sourceChid ||
                !QFileInfo::exists(checkpoint(prior, 1)) || !finished.isValid()) continue;
            if (!latest.isValid() || finished > latest) {
                latest = finished; source = prior; parentTaskId = result.value("taskId").toString();
            }
        }
    }
    if (parentTaskId.isEmpty()) parentTaskId = jsonObject(source.filePath("run-result.json")).value("taskId").toString();
    if (parentTaskId.isEmpty()) parentTaskId = jsonObject(source.filePath("restart-baseline.json")).value("parentTaskId").toString();
    QSet<QString> names;
    for (int mesh = 1; mesh <= meshCount; ++mesh) {
        const QFileInfo file(checkpoint(source, mesh));
        if (!file.isFile() || file.size() == 0) {
            if (error) *error = QStringLiteral("Missing RESTART checkpoint for mesh %1: %2").arg(mesh).arg(file.absoluteFilePath());
            return false;
        }
        names.insert(file.fileName());
    }
    if (request.restartAppend) {
        for (const QString& suffix : {QStringLiteral(".smv"), QStringLiteral(".out"),
                                      QStringLiteral("_hrr.csv"), QStringLiteral("_steps.csv")}) {
            const QString name = chid + suffix;
            if (!QFileInfo(source.filePath(name)).isFile()) {
                if (error) *error = QStringLiteral("Missing inherited RESTART output: %1").arg(source.filePath(name));
                return false;
            }
            names.insert(name);
        }
        QStringList neighbors;
        for (const QFileInfo& file : source.entryInfoList({"*.smv"}, QDir::Files))
            if (file.completeBaseName() != chid) neighbors.append(file.completeBaseName());
        for (const QFileInfo& file : source.entryInfoList(QDir::Files | QDir::Hidden)) {
            const QString name = file.fileName();
            if (!(name.startsWith(chid + '.') || name.startsWith(chid + '_'))) continue;
            if (name.endsWith(".fds", Qt::CaseInsensitive) || name.endsWith(".stop", Qt::CaseInsensitive) ||
                name.endsWith(".notready", Qt::CaseInsensitive)) continue;
            const bool foreign = std::any_of(neighbors.cbegin(), neighbors.cend(), [&](const QString& neighbor) {
                return neighbor.size() > chid.size() && (name.startsWith(neighbor + '.') || name.startsWith(neighbor + '_'));
            });
            if (!foreign) names.insert(name);
        }
        QFile smv(source.filePath(chid + ".smv"));
        if (!smv.open(QIODevice::ReadOnly)) return false;
        const QRegularExpression fileLine(QStringLiteral(R"(^\S+\.(?:csv|sf|bf|bnd|prt5|iso|s3d|q|ge|ge2|geom|sz|szz|rle)$)"),
                                          QRegularExpression::CaseInsensitiveOption);
        for (QString line : QString::fromLocal8Bit(smv.readAll()).split('\n')) {
            line = line.trimmed();
            if (!fileLine.match(line).hasMatch()) continue;
            line = QDir::fromNativeSeparators(line);
            if (QDir::isAbsolutePath(line) || line.split('/').contains("..")) {
                if (error) *error = QStringLiteral("RESTART SMV references an external output path: %1").arg(line);
                return false;
            }
            if (!QFileInfo(source.filePath(line)).isFile()) {
                if (error) *error = QStringLiteral("Missing RESTART SMV dependency: %1").arg(source.filePath(line));
                return false;
            }
            names.insert(line);
        }
    }
    request.restartBaselineTime = logEndTime(source.filePath(sourceChid + ".out"));
    if (request.restartAppend && request.restartBaselineTime < 0) {
        if (error) *error = QStringLiteral("RESTART inherited output has no reliable simulation-time baseline.");
        return false;
    }
    if (request.restartBaselineTime >= 0 && request.requestedEndTime <= request.restartBaselineTime + 1e-9) {
        if (error) *error = QStringLiteral("RESTART T_END must advance beyond the inherited simulation time %1 s.")
                                .arg(request.restartBaselineTime, 0, 'g', 15);
        return false;
    }
    qint64 totalBytes = 0;
    for (const QString& name : names) totalBytes += QFileInfo(source.filePath(name)).size();
    const QStorageInfo storage(runDirectory.absolutePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0 && totalBytes > storage.bytesAvailable() / 2) {
        if (error) *error = QStringLiteral("Insufficient space for immutable RESTART baseline and independent run copies (%1 bytes).")
                                .arg(totalBytes * 2);
        return false;
    }
    const QDir baseline(runDirectory.filePath("_restart_source"));
    QJsonArray files;
    for (const QString& name : names) {
        QString hash;
        if (!copyStableFile(source.filePath(name), baseline.filePath(name), &hash, error) ||
            !copyStableFile(baseline.filePath(name), runDirectory.filePath(name), nullptr, error)) return false;
        files.append(QJsonObject{{"source", source.filePath(name)}, {"path", name}, {"sha256", hash},
                                 {"size", QFileInfo(source.filePath(name)).size()}});
    }
    // Recheck the complete set after copying, not just each individual file.
    for (const QJsonValue& value : files) {
        const auto file = value.toObject();
        if (fileSha256(file.value("source").toString()) != file.value("sha256").toString()) {
            if (error) *error = QStringLiteral("RESTART source set changed during snapshot; stop its writer first.");
            return false;
        }
    }
    request.restartSourceDirectory = source.absolutePath();
    request.restartSnapshotDirectory = baseline.absolutePath();
    const QJsonObject metadata{{"enabled", true}, {"append", request.restartAppend},
        {"sourceChid", sourceChid}, {"sourceDirectory", source.absolutePath()}, {"parentTaskId", parentTaskId},
        {"baselineTime", request.restartBaselineTime}, {"requestedEndTime", request.requestedEndTime},
        {"meshCount", meshCount}, {"files", files}};
    return saveBytes(baseline.filePath("restart-baseline.json"), QJsonDocument(metadata).toJson(), error);
}

bool freezeRequest(FdsRunRequest& request, const QString& taskId, QString* error)
{
    const QFileInfo input(request.inputFilePath);
    const QDir sourceDirectory(input.absolutePath());
    if (request.sourceInputFilePath.isEmpty()) request.sourceInputFilePath = input.absoluteFilePath();
    if (request.outputRootDirectory.isEmpty()) request.outputRootDirectory = input.absolutePath();
    const QDir runDirectory(QDir(request.outputRootDirectory).filePath(
        QStringLiteral(".firecae-runs/") + taskId));
    if (!QDir().mkpath(runDirectory.filePath(QStringLiteral("_inputs"))) ||
        !QDir().mkpath(runDirectory.filePath(QStringLiteral("_source")))) {
        if (error) *error = QStringLiteral("Cannot create an isolated task directory: %1")
                                .arg(runDirectory.absolutePath());
        return false;
    }
    QFile source(input.absoluteFilePath());
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot read task input: %1").arg(input.absoluteFilePath());
        return false;
    }
    const QByteArray sourceBytes = source.readAll();
    QJsonArray dependencies;
    QHash<QString, QString> copiedFiles;
    QHash<QString, QString> csvFamilyDirectories;
    QSet<QString> activeIncludes;
    int explicitMeshes = 0;
    bool multipliedMeshes = false;
    bool scaledTime = false;
    request.restartEnabled = false;
    request.restartSourceChid.clear();
    request.requestedEndTime = -1.0;
    struct Replacement { qsizetype start, length; QString text; };
    std::function<bool(const QString&, QString&, int)> freezeText;
    freezeText = [&](const QString& contents, QString& rewritten, int depth) {
        if (depth > 32) {
            if (error) *error = QStringLiteral("CATF dependency nesting exceeds 32 levels.");
            return false;
        }
        const QString uncommented = maskFds(contents, false);
        const QString masked = maskFds(contents, true);
        const QRegularExpression recordExpression(QStringLiteral(R"(&([A-Za-z][A-Za-z0-9_]*)\b([^/]*)/)"));
        const QRegularExpression assignmentExpression(QStringLiteral(R"(\b([A-Za-z][A-Za-z0-9_]*)(?:\([^=]*?\))?\s*=)"));
        const QRegularExpression stringExpression(QStringLiteral(R"('(?:''|[^'])*'|"(?:""|[^"])*")"));
        QVector<Replacement> replacements;
        auto records = recordExpression.globalMatch(masked);
        while (records.hasNext()) {
            const auto record = records.next();
            const QString keyword = record.captured(1).toUpper();
            if (keyword == QStringLiteral("MESH")) ++explicitMeshes;
            const qsizetype bodyStart = record.capturedStart(2);
            const QString body = record.captured(2);
            QVector<QRegularExpressionMatch> assignments;
            auto keys = assignmentExpression.globalMatch(body);
            while (keys.hasNext()) assignments.append(keys.next());
            for (qsizetype index = 0; index < assignments.size(); ++index) {
                const auto& assignment = assignments.at(index);
                const QString key = assignment.captured(1).toUpper();
                const qsizetype begin = bodyStart + assignment.capturedEnd();
                const qsizetype end = index + 1 < assignments.size()
                    ? bodyStart + assignments.at(index + 1).capturedStart()
                    : record.capturedEnd(2);
                const QString value = uncommented.mid(begin, end - begin);
                const bool enabled = QRegularExpression(
                    QStringLiteral(R"(^\s*\.?(?:TRUE|T)\.?(?:\s|,|$))"),
                    QRegularExpression::CaseInsensitiveOption).match(value).hasMatch();
                if (keyword == QStringLiteral("MISC") && key == QStringLiteral("RESTART")) request.restartEnabled = enabled;
                if (keyword == QStringLiteral("MESH") && key == QStringLiteral("MULT_ID")) multipliedMeshes = true;
                if (key == QStringLiteral("T_END") || key == QStringLiteral("TIME_SHRINK_FACTOR")) {
                    QString token = value.trimmed().section(',', 0, 0).trimmed();
                    token.replace('D', 'E', Qt::CaseInsensitive);
                    bool ok = false;
                    const double number = token.toDouble(&ok);
                    if (ok && std::isfinite(number)) {
                        if (keyword == QStringLiteral("TIME") && key == QStringLiteral("T_END")) request.requestedEndTime = number;
                        if (key == QStringLiteral("TIME_SHRINK_FACTOR") && number != 1.0) scaledTime = true;
                    }
                }
                if (key == QStringLiteral("EXTERNAL_FILE") && enabled) {
                    if (error) *error = QStringLiteral(
                        "%1 requires live or implicit external files. Isolated task execution cannot freeze this mode yet.")
                                            .arg(key);
                    return false;
                }
                auto strings = stringExpression.globalMatch(value);
                while (strings.hasNext()) {
                    const auto literal = strings.next();
                    const QChar quote = literal.captured().front();
                    QString path = literal.captured().mid(1, literal.capturedLength() - 2);
                    path.replace(QString(2, quote), QString(quote));
                    if (keyword == QStringLiteral("MISC") && key == QStringLiteral("RESTART_CHID"))
                        request.restartSourceChid = path.trimmed();
                    if (path.isEmpty() || path.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0) continue;
                    if (key == QStringLiteral("RESULTS_DIR") || key == QStringLiteral("BINGEOM_DIR")) {
                        if (error) *error = QStringLiteral(
                            "%1 redirects solver outputs outside the tracked task directory and is not supported by isolated runs.")
                                                .arg(key);
                        return false;
                    }
                    // DUMP FILE switches/filenames designate outputs, not inputs.
                    const bool inputFile = keyword != QStringLiteral("DUMP") &&
                        (key.contains(QStringLiteral("FILE")) || key == QStringLiteral("TERRAIN_IMAGE") ||
                         key == QStringLiteral("TEXTURE_MAP"));
                    if (!inputFile) continue;
                    if (key == QStringLiteral("EXTERNAL_FILENAME")) {
                        if (error) *error = QStringLiteral("Live external control files cannot be frozen for isolated tasks.");
                        return false;
                    }
                    const QFileInfo dependency(QDir::isAbsolutePath(path)
                        ? path : sourceDirectory.filePath(path));
                    // For multiple meshes FDS READ_CSVF replaces the final five
                    // characters of each filename with <mesh index>.csv. Keep
                    // the exact input and every existing member of that narrow
                    // family together, without guessing MESH/MULT expansion or
                    // copying unrelated historical result files. FDS still
                    // diagnoses any required mesh member absent in the source.
                    const bool csvfFamily = keyword == QStringLiteral("CSVF") &&
                        (key == QStringLiteral("TMPFILE") || key == QStringLiteral("UVWFILE") ||
                         key == QStringLiteral("SPECFILE")) && dependency.fileName().size() >= 5;
                    if (csvfFamily) {
                        const QString prefix = dependency.fileName().chopped(5);
                        const QString familyKey = QDir::cleanPath(dependency.absolutePath()) +
                                                  QLatin1Char('/') + prefix;
                        QString familyDirectory = csvFamilyDirectories.value(familyKey);
                        if (familyDirectory.isEmpty()) {
                            const QRegularExpression memberExpression(
                                QStringLiteral("^%1[1-9][0-9]*\\.csv$")
                                    .arg(QRegularExpression::escape(prefix)));
                            QFileInfoList members;
                            for (const QFileInfo& member : dependency.absoluteDir().entryInfoList(
                                     QDir::Files | QDir::Hidden, QDir::Name)) {
                                if (member.fileName() == dependency.fileName() ||
                                    memberExpression.match(member.fileName()).hasMatch())
                                    members.append(member);
                            }
                            if (members.isEmpty()) {
                                if (error) *error = QStringLiteral("Cannot freeze %1 dependency '%2': no input or mesh-family files exist.")
                                                        .arg(key, dependency.absoluteFilePath());
                                return false;
                            }
                            if (dependencies.size() + members.size() > 512) {
                                if (error) *error = QStringLiteral("Task dependency count exceeds 512 files including CSVF mesh families.");
                                return false;
                            }
                            familyDirectory = QStringLiteral("_inputs/csvf_%1")
                                                  .arg(csvFamilyDirectories.size());
                            if (!QDir().mkpath(runDirectory.filePath(familyDirectory))) {
                                if (error) *error = QStringLiteral("Cannot create CSVF dependency directory.");
                                return false;
                            }
                            for (const QFileInfo& member : members) {
                                const QString canonical = member.canonicalFilePath();
                                QFile file(canonical);
                                if (!file.open(QIODevice::ReadOnly)) {
                                    if (error) *error = QStringLiteral("Cannot read CSVF dependency: %1").arg(canonical);
                                    return false;
                                }
                                const QByteArray bytes = file.readAll();
                                const QString relative = familyDirectory + QLatin1Char('/') + member.fileName();
                                if (!saveBytes(runDirectory.filePath(relative), bytes, error)) return false;
                                copiedFiles.insert(canonical, relative);
                                dependencies.append(QJsonObject{{"source", canonical}, {"path", relative},
                                    {"sourceSha256", sha256(bytes)}, {"runSha256", sha256(bytes)},
                                    {"csvfFamily", familyKey},
                                    {"implicit", member.fileName() != dependency.fileName()}});
                            }
                            csvFamilyDirectories.insert(familyKey, familyDirectory);
                        }
                        QString relative = familyDirectory + QLatin1Char('/') + dependency.fileName();
                        relative.replace(quote, QString(2, quote));
                        replacements.append({begin + literal.capturedStart(), literal.capturedLength(),
                                             quote + relative + quote});
                        continue;
                    }
                    if (!dependency.isFile()) {
                        if (error) *error = QStringLiteral("Cannot freeze %1 dependency '%2': an explicit existing file is required.")
                                                .arg(key, dependency.absoluteFilePath());
                        return false;
                    }
                    const QString canonical = dependency.canonicalFilePath();
                    if (activeIncludes.contains(canonical)) {
                        if (error) *error = QStringLiteral("Cyclic CATF dependency: %1").arg(canonical);
                        return false;
                    }
                    QString relative = copiedFiles.value(canonical);
                    if (relative.isEmpty()) {
                        if (dependencies.size() >= 512) {
                            if (error) *error = QStringLiteral("Task dependency count exceeds 512 explicit files.");
                            return false;
                        }
                        relative = QStringLiteral("_inputs/%1_%2").arg(copiedFiles.size()).arg(dependency.fileName());
                        copiedFiles.insert(canonical, relative);
                        QFile dependencyFile(canonical);
                        if (!dependencyFile.open(QIODevice::ReadOnly)) {
                            if (error) *error = QStringLiteral("Cannot read dependency: %1").arg(canonical);
                            return false;
                        }
                        const QByteArray original = dependencyFile.readAll();
                        QByteArray frozen = original;
                        if (keyword == QStringLiteral("CATF") && key == QStringLiteral("OTHER_FILES")) {
                            activeIncludes.insert(canonical);
                            QString included;
                            const QString decoded = QString::fromUtf8(original);
                            if (!freezeText(decoded, included, depth + 1)) return false;
                            activeIncludes.remove(canonical);
                            if (included != decoded) {
                                if (decoded.toUtf8() != original) {
                                    if (error) *error = QStringLiteral(
                                        "Dependency path rewriting requires UTF-8 CATF input: %1").arg(canonical);
                                    return false;
                                }
                                frozen = included.toUtf8();
                            }
                        }
                        if (!saveBytes(runDirectory.filePath(relative), frozen, error)) return false;
                        dependencies.append(QJsonObject{{"source", canonical}, {"path", relative},
                            {"sourceSha256", sha256(original)}, {"runSha256", sha256(frozen)}});
                    }
                    QString escaped = relative;
                    escaped.replace(quote, QString(2, quote));
                    replacements.append({begin + literal.capturedStart(), literal.capturedLength(),
                                         quote + escaped + quote});
                }
            }
        }
        rewritten = contents;
        std::sort(replacements.begin(), replacements.end(),
            [](const Replacement& left, const Replacement& right) { return left.start > right.start; });
        for (const auto& replacement : replacements)
            rewritten.replace(replacement.start, replacement.length, replacement.text);
        return true;
    };
    QString frozen;
    const QString decodedSource = QString::fromUtf8(sourceBytes);
    if (!freezeText(decodedSource, frozen, 0)) return false;
    if (request.restartEnabled) {
        if (multipliedMeshes || scaledTime || request.requestedEndTime < 0) {
            if (error) *error = QStringLiteral("RESTART currently requires explicit MESH records, unscaled time, and an explicit finite T_END.");
            return false;
        }
        if (!freezeRestart(request, sourceDirectory, runDirectory, explicitMeshes, error)) return false;
    }
    QByteArray runBytes = sourceBytes;
    if (frozen != decodedSource) {
        if (decodedSource.toUtf8() != sourceBytes) {
            if (error) *error = QStringLiteral(
                "Dependency path rewriting requires UTF-8 input; the original input has been preserved.");
            return false;
        }
        runBytes = frozen.toUtf8();
    }
    const QString runInput = runDirectory.filePath(input.fileName());
    if (!saveBytes(runDirectory.filePath(QStringLiteral("_source/") + input.fileName()), sourceBytes, error) ||
        !saveBytes(runInput, runBytes, error)) return false;
    const QJsonObject manifest{{"schemaVersion", 1}, {"taskId", taskId},
        {"sourceInput", request.sourceInputFilePath}, {"sourceSnapshotInput", input.absoluteFilePath()},
        {"sourceSha256", sha256(sourceBytes)}, {"runInput", runInput}, {"runInputSha256", sha256(runBytes)},
        {"dependencies", dependencies},
        {"restart", request.restartEnabled ? jsonObject(QDir(request.restartSnapshotDirectory).filePath("restart-baseline.json")) : QJsonObject{}},
        {"queuedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    if (!saveBytes(runDirectory.filePath(QStringLiteral("run-manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented), error)) return false;
    request.inputFilePath = runInput;
    return true;
}

void saveCompletion(const SimulationTaskRecord& record)
{
    const QJsonObject result{{"taskId", record.taskId}, {"success", record.summary.success},
        {"cancelled", record.summary.cancelled}, {"state", simulationTaskStateName(record.state)},
        {"smvPath", record.summary.smvFilePath}, {"outPath", record.summary.outputFilePath},
        {"inputPath", record.request.inputFilePath}, {"finishedAt", record.finishedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"restarted", record.summary.restarted}, {"observedEndTime", record.summary.observedEndTime},
        {"inheritedOutputBytes", record.summary.inheritedOutputBytes},
        {"restartSnapshotDirectory", record.request.restartSnapshotDirectory},
        {"error", record.errorMessage}};
    saveBytes(QDir(record.outputDirectory).filePath(QStringLiteral("run-result.json")),
              QJsonDocument(result).toJson(QJsonDocument::Indented), nullptr);
}
}

QString simulationTaskStateName(SimulationTaskState state)
{
    switch (state) {
    case SimulationTaskState::Waiting: return QStringLiteral("Waiting");
    case SimulationTaskState::Preparing: return QStringLiteral("Preparing");
    case SimulationTaskState::Running: return QStringLiteral("Running");
    case SimulationTaskState::Stopping: return QStringLiteral("Stopping");
    case SimulationTaskState::Completed: return QStringLiteral("Completed");
    case SimulationTaskState::Failed: return QStringLiteral("Failed");
    case SimulationTaskState::Cancelled: return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

SimulationTaskManager::SimulationTaskManager(QObject* parent) : QObject(parent) {}

SimulationTaskManager::~SimulationTaskManager()
{
    for (FdsRunner* runner : m_runners) {
        if (runner && runner->isRunning()) runner->stopAndWait(5000);
    }
}

QString SimulationTaskManager::enqueue(const FdsRunRequest& request,
                                       const QString& projectName,
                                       const QString& sceneName,
                                       double endTime,
                                       QString* errorMessage)
{
    const QFileInfo input(request.inputFilePath);
    if (!input.exists() || !input.isFile() ||
        input.suffix().compare(QStringLiteral("fds"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Task input is not an existing .fds file.");
        return {};
    }
    const QString fdsExecutable = request.executablePath.isEmpty()
                                      ? FdsRunner::detectExecutable()
                                      : request.executablePath;
    SolverLaunchContext context;
    context.inputFilePath = input.absoluteFilePath();
    context.fdsExecutablePath = fdsExecutable;
    context.bfdsExecutablePath = SolverBackendRegistry::configuredBfdsExecutable();
    context.processCount = request.mode == FdsRunMode::Mpi
                               ? qMax(2, request.processCount) : 1;
    context.threadCount = request.mode == FdsRunMode::OpenMp
                              ? qMax(1, request.threadCount) : 1;
    const SolverBackendKind kind =
        request.mode == FdsRunMode::Mpi
            ? SolverBackendKind::FdsMpiCpu
            : request.mode == FdsRunMode::OpenMp
                  ? SolverBackendKind::FdsOpenMpCpu
                  : SolverBackendKind::FdsSerialCpu;
    const std::unique_ptr<SolverBackend> backendImplementation =
        SolverBackendRegistry::create(kind);
    const SolverBackendInfo backend = backendImplementation->info(context);
    if (!backend.available) {
        if (errorMessage) *errorMessage = backend.unavailableReason;
        return {};
    }
    SimulationTaskRecord record;
    record.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.projectName = projectName;
    record.sceneName = sceneName.isEmpty() ? QStringLiteral("Default") : sceneName;
    record.chid = FdsRunner::caseIdFromInput(input.absoluteFilePath());
    record.request = request;
    record.request.inputFilePath = input.absoluteFilePath();
    record.request.executablePath = fdsExecutable;
    record.request.processCount = context.processCount;
    record.request.threadCount = context.threadCount;
    if (!freezeRequest(record.request, record.taskId, errorMessage)) return {};
    if (record.request.restartEnabled) {
        for (const SimulationTaskRecord& prior : m_tasks) {
            if ((prior.state == SimulationTaskState::Running || prior.state == SimulationTaskState::Preparing ||
                 prior.state == SimulationTaskState::Stopping) &&
                QDir(prior.outputDirectory).absolutePath() == QDir(record.request.restartSourceDirectory).absolutePath()) {
                if (errorMessage) *errorMessage = QStringLiteral("Cannot restart from an active task; stop its writer before freezing checkpoints.");
                return {};
            }
        }
    }
    context.inputFilePath = record.request.inputFilePath;
    record.backendName = backend.displayName;
    record.outputDirectory = QFileInfo(record.request.inputFilePath).absolutePath();
    const SolverLaunchPlan launchPlan =
        backendImplementation->createLaunchPlan(context);
    record.commandLine = launchPlan.commandLine;
    record.environmentCheck = launchPlan.valid()
                                  ? QStringLiteral(
                                        "Ready: solver=%1; launcher=%2; "
                                        "OMP_NUM_THREADS=%3; working directory=%4")
                                        .arg(QFileInfo(launchPlan.solverExecutable).fileName(),
                                             QFileInfo(launchPlan.program).fileName(),
                                             launchPlan.environment.value(
                                                 QStringLiteral("OMP_NUM_THREADS"),
                                                 QStringLiteral("1")),
                                             QDir::toNativeSeparators(
                                                 launchPlan.workingDirectory))
                                  : launchPlan.errorMessage;
    record.endTime = qMax(0.0, endTime);
    record.queuedAt = QDateTime::currentDateTime();
    m_tasks.append(record);
    emit taskAdded(record.taskId);
    QTimer::singleShot(0, this, &SimulationTaskManager::startQueuedTasks);
    return record.taskId;
}

bool SimulationTaskManager::cancel(const QString& taskId)
{
    for (SimulationTaskRecord& record : m_tasks) {
        if (record.taskId != taskId) continue;
        if (record.state == SimulationTaskState::Waiting ||
            record.state == SimulationTaskState::Preparing) {
            record.state = SimulationTaskState::Cancelled;
            record.finishedAt = QDateTime::currentDateTime();
            record.errorMessage = QStringLiteral("Task cancelled before solver launch.");
            saveCompletion(record);
            emit taskUpdated(taskId);
            QTimer::singleShot(0, this, &SimulationTaskManager::startQueuedTasks);
            return true;
        }
        if (record.state == SimulationTaskState::Running) {
            record.state = SimulationTaskState::Stopping;
            emit taskUpdated(taskId);
            if (FdsRunner* runner = m_runners.value(taskId)) runner->stop();
            return true;
        }
        return false;
    }
    return false;
}

QString SimulationTaskManager::retry(const QString& taskId, QString* errorMessage)
{
    const SimulationTaskRecord* previous = task(taskId);
    if (!previous) {
        if (errorMessage) *errorMessage = QStringLiteral("Task was not found.");
        return {};
    }
    if (previous->state == SimulationTaskState::Running ||
        previous->state == SimulationTaskState::Preparing ||
        previous->state == SimulationTaskState::Stopping) {
        if (errorMessage) *errorMessage = QStringLiteral("Active tasks cannot be retried.");
        return {};
    }
    return enqueue(previous->request, previous->projectName, previous->sceneName,
                   previous->endTime, errorMessage);
}

void SimulationTaskManager::setMaximumConcurrentTasks(int count)
{
    m_maximumConcurrentTasks = qBound(1, count, 32);
    startQueuedTasks();
}

int SimulationTaskManager::maximumConcurrentTasks() const { return m_maximumConcurrentTasks; }

bool SimulationTaskManager::hasActiveTasks() const { return activeCount() > 0; }

QString SimulationTaskManager::firstActiveTaskId() const
{
    for (const SimulationTaskRecord& record : m_tasks) {
        if (record.state == SimulationTaskState::Running ||
            record.state == SimulationTaskState::Preparing ||
            record.state == SimulationTaskState::Stopping) return record.taskId;
    }
    return {};
}

QVector<SimulationTaskRecord> SimulationTaskManager::tasks() const { return m_tasks; }

const SimulationTaskRecord* SimulationTaskManager::task(const QString& taskId) const
{
    for (const SimulationTaskRecord& record : m_tasks) {
        if (record.taskId == taskId) return &record;
    }
    return nullptr;
}

void SimulationTaskManager::startQueuedTasks()
{
    while (activeCount() < m_maximumConcurrentTasks) {
        QString next;
        for (const SimulationTaskRecord& record : m_tasks) {
            if (record.state == SimulationTaskState::Waiting) { next = record.taskId; break; }
        }
        if (next.isEmpty()) break;
        startTask(next);
    }
}

void SimulationTaskManager::startTask(const QString& taskId)
{
    SimulationTaskRecord* record = nullptr;
    for (SimulationTaskRecord& candidate : m_tasks) {
        if (candidate.taskId == taskId) { record = &candidate; break; }
    }
    if (!record) return;
    record->state = SimulationTaskState::Preparing;
    emit taskUpdated(taskId);
    auto* runner = new FdsRunner(this);
    m_runners.insert(taskId, runner);
    connect(runner, &FdsRunner::runStarted, this,
            [this, taskId](const QString&, qint64) {
                for (SimulationTaskRecord& item : m_tasks) {
                    if (item.taskId != taskId) continue;
                    item.state = SimulationTaskState::Running;
                    item.startedAt = QDateTime::currentDateTime();
                    emit taskUpdated(taskId);
                    break;
                }
            });
    connect(runner, &FdsRunner::outputReceived, this,
            [this, taskId](const QString& output) {
                consumeOutput(taskId, output, false);
            });
    connect(runner, &FdsRunner::errorOutputReceived, this,
            [this, taskId](const QString& output) {
                consumeOutput(taskId, output, true);
            });
    connect(runner, &FdsRunner::runFinished, this,
            [this, taskId, runner](const FdsRunSummary& summary) {
                for (SimulationTaskRecord& item : m_tasks) {
                    if (item.taskId != taskId) continue;
                    item.summary = summary;
                    item.elapsedMilliseconds = summary.elapsedMilliseconds;
                    item.finishedAt = QDateTime::currentDateTime();
                    item.errorMessage = summary.errorMessage;
                    item.state = summary.cancelled ? SimulationTaskState::Cancelled
                                                   : summary.success ? SimulationTaskState::Completed
                                                                     : SimulationTaskState::Failed;
                    if (summary.success) {
                        item.simulationTime = item.endTime;
                        item.progress = 1.0;
                        item.estimatedRemainingMilliseconds = 0;
                    }
                    saveCompletion(item);
                    emit taskUpdated(taskId);
                    emit taskFinished(taskId, summary);
                    break;
                }
                m_runners.remove(taskId);
                runner->deleteLater();
                QTimer::singleShot(0, this, &SimulationTaskManager::startQueuedTasks);
            });
    QString error;
    if (!runner->start(record->request, &error)) {
        record->state = SimulationTaskState::Failed;
        record->errorMessage = error;
        record->finishedAt = QDateTime::currentDateTime();
        saveCompletion(*record);
        m_runners.remove(taskId);
        runner->deleteLater();
        emit taskUpdated(taskId);
        QTimer::singleShot(0, this, &SimulationTaskManager::startQueuedTasks);
    }
}

void SimulationTaskManager::consumeOutput(const QString& taskId,
                                          const QString& output,
                                          bool fromStandardError)
{
    static const QRegularExpression timeExpression(
        QStringLiteral(R"((?:Simulation\s+Time|Time)\s*[:=]\s*([0-9]+(?:\.[0-9]*)?))"),
        QRegularExpression::CaseInsensitiveOption);
    for (SimulationTaskRecord& record : m_tasks) {
        if (record.taskId != taskId) continue;
        record.log.append(output);
        (fromStandardError ? record.standardError : record.standardOutput)
            .append(output);
        constexpr qsizetype maximumLogCharacters = 2 * 1024 * 1024;
        if (record.log.size() > maximumLogCharacters) {
            record.log.remove(0, record.log.size() - maximumLogCharacters);
        }
        QString& channel = fromStandardError ? record.standardError
                                             : record.standardOutput;
        if (channel.size() > maximumLogCharacters) {
            channel.remove(0, channel.size() - maximumLogCharacters);
        }
        QRegularExpressionMatchIterator matches = timeExpression.globalMatch(output);
        while (matches.hasNext()) {
            bool ok = false;
            const double value = matches.next().captured(1).toDouble(&ok);
            if (ok) record.simulationTime = qMax(record.simulationTime, value);
        }
        if (record.endTime > 0.0) {
            record.progress = qBound(0.0, record.simulationTime / record.endTime, 1.0);
        }
        if (record.startedAt.isValid()) {
            record.elapsedMilliseconds = record.startedAt.msecsTo(QDateTime::currentDateTime());
            if (record.progress > 0.001 && record.progress < 1.0) {
                record.estimatedRemainingMilliseconds = static_cast<qint64>(
                    record.elapsedMilliseconds * (1.0 - record.progress) / record.progress);
            }
        }
        emit taskOutput(taskId, output);
        emit taskUpdated(taskId);
        return;
    }
}

int SimulationTaskManager::activeCount() const
{
    int count = 0;
    for (const SimulationTaskRecord& record : m_tasks) {
        if (record.state == SimulationTaskState::Preparing ||
            record.state == SimulationTaskState::Running ||
            record.state == SimulationTaskState::Stopping) ++count;
    }
    return count;
}
