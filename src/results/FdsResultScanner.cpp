#include "results/FdsResultScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

namespace
{
bool isSupportedResultFile(const QString& fileName)
{
    const QString lower = fileName.toLower();
    static const QStringList suffixes = {
        QStringLiteral(".smv"), QStringLiteral(".fds"), QStringLiteral(".out"),
        QStringLiteral(".err"), QStringLiteral(".csv"), QStringLiteral(".sf"),
        QStringLiteral(".bf"), QStringLiteral(".bnd"), QStringLiteral(".prt5"),
        QStringLiteral(".iso"), QStringLiteral(".s3d"), QStringLiteral(".q")};
    return std::any_of(suffixes.cbegin(), suffixes.cend(),
                       [&lower](const QString& suffix) { return lower.endsWith(suffix); });
}

QString normalizedReferencedFile(QString line)
{
    line = line.trimmed();
    if ((line.startsWith(QLatin1Char('"')) && line.endsWith(QLatin1Char('"'))) ||
        (line.startsWith(QLatin1Char('\'')) && line.endsWith(QLatin1Char('\'')))) {
        line = line.mid(1, line.size() - 2);
    }
    return isSupportedResultFile(line) ? QDir::fromNativeSeparators(line) : QString();
}

QStringList csvColumns(const QString& line)
{
    QStringList columns;
    QString value;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar character = line.at(i);
        if (character == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                value.append(character);
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (character == QLatin1Char(',') && !quoted) {
            columns.append(value.trimmed());
            value.clear();
        } else {
            value.append(character);
        }
    }
    columns.append(value.trimmed());
    return columns;
}

void readCsvMetadata(FdsResultFileInfo& info)
{
    if (!info.exists || !info.filePath.endsWith(QStringLiteral(".csv"),
                                                 Qt::CaseInsensitive)) return;
    QFile file(info.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream stream(&file);
    QStringList lastHeader;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList columns = csvColumns(line);
        if (columns.isEmpty()) continue;
        bool numeric = false;
        columns.constFirst().toDouble(&numeric);
        if (!numeric) {
            lastHeader = columns;
            continue;
        }
        if (info.startTime.isEmpty()) info.startTime = columns.constFirst();
        info.endTime = columns.constFirst();
        ++info.dataRowCount;
        info.columnCount = std::max(info.columnCount, static_cast<int>(columns.size()));
    }
    if (!lastHeader.isEmpty()) {
        info.columnNames = lastHeader;
        info.columnCount = std::max(info.columnCount, static_cast<int>(lastHeader.size()));
    }
}

bool parseSmvDescriptor(const QStringList& lines, int index,
                        QString& fileName, QString& quantity,
                        QString& shortName, QString& unit, int& meshIndex)
{
    if (index < 0 || index >= lines.size()) return false;
    const QString header = lines.at(index).trimmed();
    const QString keyword = header.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0)
                                .toUpper();
    const bool field = keyword.startsWith(QStringLiteral("SMOK")) ||
                       keyword.startsWith(QStringLiteral("SLC")) ||
                       keyword.startsWith(QStringLiteral("BND")) ||
                       keyword.startsWith(QStringLiteral("ISO")) ||
                       keyword.startsWith(QStringLiteral("PL3D"));
    const bool particles = keyword.startsWith(QStringLiteral("PRT5"));
    if (!field && !particles) return false;
    if (index + 1 >= lines.size()) return false;
    fileName = normalizedReferencedFile(lines.at(index + 1));
    if (fileName.isEmpty()) return false;
    const QStringList headerFields = header.split(QRegularExpression(QStringLiteral("\\s+")),
                                                   Qt::SkipEmptyParts);
    if (headerFields.size() > 1) meshIndex = headerFields.at(1).toInt();
    if (field && index + 4 < lines.size()) {
        quantity = lines.at(index + 2).trimmed();
        shortName = lines.at(index + 3).trimmed();
        unit = lines.at(index + 4).trimmed();
    } else if (particles) {
        quantity = QStringLiteral("Particles");
    }
    return true;
}
}

QStringList FdsResultScanner::findSmvFiles(const QString& directoryPath) const
{
    const QDir directory(directoryPath);
    if (!directory.exists()) return {};
    QStringList files;
    for (const QFileInfo& info :
         directory.entryInfoList({QStringLiteral("*.smv")}, QDir::Files, QDir::Name)) {
        files.append(info.absoluteFilePath());
    }
    return files;
}

FdsResultScanResult FdsResultScanner::scanSmvFile(const QString& smvFilePath) const
{
    FdsResultScanResult result;
    result.scanTime = QDateTime::currentDateTime();
    const QFileInfo smvInfo(smvFilePath);
    if (!smvInfo.exists() || !smvInfo.isFile()) {
        result.errorMessage = QStringLiteral("Smokeview file does not exist: %1")
                                  .arg(QDir::toNativeSeparators(smvFilePath));
        return result;
    }
    if (smvInfo.suffix().compare(QStringLiteral("smv"), Qt::CaseInsensitive) != 0) {
        result.errorMessage = QStringLiteral("Selected file is not a .smv result file.");
        return result;
    }
    QFile smvFile(smvInfo.absoluteFilePath());
    if (!smvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Smokeview file cannot be read: %1")
                                  .arg(smvInfo.absoluteFilePath());
        return result;
    }
    result.caseName = smvInfo.completeBaseName();
    result.resultDirectory = smvInfo.absolutePath();
    result.smvFilePath = smvInfo.absoluteFilePath();
    const QString expectedFds = QDir(result.resultDirectory).filePath(
        result.caseName + QStringLiteral(".fds"));
    if (QFileInfo::exists(expectedFds)) result.fdsInputFilePath = QFileInfo(expectedFds).absoluteFilePath();

    QSet<QString> paths;
    const auto appendFile = [&](const QString& path, bool referenced) {
        const QFileInfo info(path);
        const QString absolute = info.absoluteFilePath();
        const QString key = QDir::cleanPath(absolute).toLower();
        if (paths.contains(key)) return;
        paths.insert(key);
        FdsResultFileInfo fileInfo;
        fileInfo.name = info.fileName();
        fileInfo.type = classifyFile(info.fileName());
        fileInfo.filePath = absolute;
        fileInfo.exists = info.exists() && info.isFile();
        fileInfo.fileSize = fileInfo.exists ? info.size() : 0;
        fileInfo.lastModified = fileInfo.exists ? info.lastModified() : QDateTime();
        readCsvMetadata(fileInfo);
        result.files.push_back(fileInfo);
        if (referenced && !fileInfo.exists) {
            result.warnings.append(QStringLiteral("Referenced result file is missing: %1")
                                       .arg(info.fileName()));
        }
    };
    const QDir directory(result.resultDirectory);
    QString currentRestartInput;
    QFile taskResult(directory.filePath("run-result.json"));
    if (taskResult.open(QIODevice::ReadOnly)) {
        const auto completed = QJsonDocument::fromJson(taskResult.readAll()).object();
        const QFileInfo inputFile(completed.value("inputPath").toString());
        if (completed.value("restarted").toBool() && inputFile.isFile() &&
            QFileInfo(completed.value("smvPath").toString()).absoluteFilePath() == result.smvFilePath &&
            inputFile.absolutePath() == directory.absolutePath()) currentRestartInput = inputFile.absoluteFilePath();
    }
    QStringList neighboringCases;
    for (const QFileInfo& other : directory.entryInfoList(
             {QStringLiteral("*.smv")}, QDir::Files, QDir::Name)) {
        if (other.completeBaseName().compare(result.caseName, Qt::CaseInsensitive) != 0)
            neighboringCases.append(other.completeBaseName());
    }
    for (const QFileInfo& info : directory.entryInfoList(QDir::Files, QDir::Name)) {
        const QString base = info.completeBaseName();
        const bool belongsToCase = base.compare(result.caseName, Qt::CaseInsensitive) == 0 ||
            base.startsWith(result.caseName + QLatin1Char('_'), Qt::CaseInsensitive);
        const bool belongsToNeighbor = std::any_of(neighboringCases.cbegin(), neighboringCases.cend(),
            [&](const QString& other) {
                return other.size() > result.caseName.size() &&
                    (base.compare(other, Qt::CaseInsensitive) == 0 ||
                     base.startsWith(other + QLatin1Char('_'), Qt::CaseInsensitive));
            });
        if (belongsToCase && !belongsToNeighbor && isSupportedResultFile(info.fileName()))
            appendFile(info.absoluteFilePath(), false);
    }
    QTextStream smvStream(&smvFile);
    QStringList smvLines;
    while (!smvStream.atEnd()) smvLines.append(smvStream.readLine());
    for (const QString& line : smvLines) {
        const QString reference = normalizedReferencedFile(line);
        if (!reference.isEmpty()) {
            if (!currentRestartInput.isEmpty() && reference.endsWith(".fds", Qt::CaseInsensitive)) {
                result.fdsInputFilePath = currentRestartInput;
                appendFile(currentRestartInput, false);
                continue;
            }
            const QString path = directory.filePath(reference);
            appendFile(path, true);
            if (reference.endsWith(QStringLiteral(".fds"), Qt::CaseInsensitive) &&
                QFileInfo(path).isFile()) {
                result.fdsInputFilePath = QFileInfo(path).absoluteFilePath();
            }
        }
    }
    for (int index = 0; index < smvLines.size(); ++index) {
        QString fileName, quantity, shortName, unit;
        int meshIndex = 0;
        if (!parseSmvDescriptor(smvLines, index, fileName, quantity,
                                shortName, unit, meshIndex)) continue;
        const QString target = QFileInfo(fileName).fileName();
        for (FdsResultFileInfo& file : result.files) {
            if (file.name.compare(target, Qt::CaseInsensitive) != 0) continue;
            file.quantity = quantity;
            file.shortName = shortName;
            file.unit = unit;
            file.meshIndex = meshIndex;
            break;
        }
    }
    // An inherited restart SMV can still name its seed input. For a verified
    // task result, the recorded run input is the authoritative current source.
    QFile completionFile(directory.filePath("run-result.json"));
    if (completionFile.open(QIODevice::ReadOnly)) {
        const auto completion = QJsonDocument::fromJson(completionFile.readAll()).object();
        const QString recordedSmv = completion.value("smvPath").toString();
        const QFileInfo recordedInput(completion.value("inputPath").toString());
        if (completion.value("restarted").toBool() &&
            QFileInfo(recordedSmv).absoluteFilePath() == result.smvFilePath && recordedInput.isFile() &&
            recordedInput.absolutePath() == directory.absolutePath()) {
            result.fdsInputFilePath = recordedInput.absoluteFilePath();
            appendFile(result.fdsInputFilePath, false);
        }
    }
    struct SlicePlaneSpec {
        QString quantity, keyword;
        double value = 0.0;
        bool vectorField = false;
    };
    QVector<SlicePlaneSpec> slicePlanes;
    QFile fdsFile(result.fdsInputFilePath);
    if (fdsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString fdsText = QString::fromUtf8(fdsFile.readAll());
        const QRegularExpression recordExpression(
            QStringLiteral(R"(&SLCF\b([^/]*)/)"),
            QRegularExpression::CaseInsensitiveOption |
                QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator records = recordExpression.globalMatch(fdsText);
        const QRegularExpression quantityExpression(
            QStringLiteral(R"(QUANTITY\s*=\s*['\"]([^'\"]+)['\"])"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression planeExpression(
            QStringLiteral(R"((PB[XYZ])\s*=\s*([+\-0-9.Ee]+))"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression vectorExpression(
            QStringLiteral(R"(VECTOR\s*=\s*\.?(TRUE|T)\.?)"),
            QRegularExpression::CaseInsensitiveOption);
        while (records.hasNext()) {
            const QString body = records.next().captured(1);
            const auto quantity = quantityExpression.match(body);
            const auto plane = planeExpression.match(body);
            if (!plane.hasMatch()) continue;
            SlicePlaneSpec spec;
            spec.quantity = quantity.hasMatch() ? quantity.captured(1).trimmed() : QString();
            spec.keyword = plane.captured(1).toUpper();
            spec.value = plane.captured(2).toDouble();
            spec.vectorField = vectorExpression.match(body).hasMatch();
            slicePlanes.append(spec);
        }
    }
    for (FdsResultFileInfo& file : result.files) {
        if (file.type != FcResultFileType::Slice || slicePlanes.isEmpty()) continue;
        const auto matching = std::find_if(
            slicePlanes.cbegin(), slicePlanes.cend(), [&file](const SlicePlaneSpec& spec) {
                return spec.quantity.compare(file.quantity, Qt::CaseInsensitive) == 0;
            });
        const SlicePlaneSpec& plane = matching != slicePlanes.cend()
            ? *matching : slicePlanes.constFirst();
        file.slicePlaneKeyword = plane.keyword;
        file.slicePlaneValue = plane.value;
        file.vectorField = plane.vectorField;
    }
    for (const FdsResultFileInfo& file : result.files) {
        if (file.startTime.isEmpty()) continue;
        // CPU summaries use Rank as the first column, step logs use Time Step,
        // and pressure-iteration logs are not physical simulation timelines.
        // Only aggregate files whose first named column is an actual Time axis.
        if (file.columnNames.isEmpty() ||
            !file.columnNames.constFirst().trimmed().startsWith(
                QStringLiteral("Time"), Qt::CaseInsensitive) ||
            file.type == FcResultFileType::Cpu ||
            file.type == FcResultFileType::Steps ||
            file.type == FcResultFileType::PressureIterations) {
            continue;
        }
        if (result.startTime.isEmpty() || file.startTime.toDouble() < result.startTime.toDouble())
            result.startTime = file.startTime;
        if (result.endTime.isEmpty() || file.endTime.toDouble() > result.endTime.toDouble())
            result.endTime = file.endTime;
    }
    std::sort(result.files.begin(), result.files.end(),
              [](const auto& left, const auto& right) {
                  return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
              });
    if (!result.warnings.isEmpty()) result.status = FcResultStatus::MissingFiles;
    else if (result.files.size() <= 1) {
        result.status = FcResultStatus::Incomplete;
        result.warnings.append(QStringLiteral("No FDS output data files were found yet."));
    } else result.status = FcResultStatus::Ready;
    return result;
}

FcResultFileType FdsResultScanner::classifyFile(const QString& fileName)
{
    const QString lower = fileName.toLower();
    if (lower.endsWith(QStringLiteral(".smv"))) return FcResultFileType::Smokeview;
    if (lower.endsWith(QStringLiteral(".fds"))) return FcResultFileType::FdsInput;
    if (lower.endsWith(QStringLiteral(".out"))) return FcResultFileType::OutputLog;
    if (lower.endsWith(QStringLiteral(".err"))) return FcResultFileType::ErrorLog;
    if (lower.endsWith(QStringLiteral("_devc_ctrl_log.csv"))) return FcResultFileType::DeviceControlLog;
    if (lower.endsWith(QStringLiteral("_devc.csv"))) return FcResultFileType::Devices;
    if (lower.endsWith(QStringLiteral("_ctrl.csv"))) return FcResultFileType::Controls;
    if (lower.endsWith(QStringLiteral("_hrr.csv"))) return FcResultFileType::HeatReleaseRate;
    if (lower.endsWith(QStringLiteral("_hvac.csv"))) return FcResultFileType::Hvac;
    if (lower.endsWith(QStringLiteral("_pressit.csv"))) return FcResultFileType::PressureIterations;
    if (lower.endsWith(QStringLiteral("_steps.csv"))) return FcResultFileType::Steps;
    if (lower.endsWith(QStringLiteral("_cpu.csv"))) return FcResultFileType::Cpu;
    if (lower.endsWith(QStringLiteral(".csv"))) return FcResultFileType::Csv;
    if (lower.endsWith(QStringLiteral(".sf"))) return FcResultFileType::Slice;
    if (lower.endsWith(QStringLiteral(".bf")) || lower.endsWith(QStringLiteral(".bnd"))) return FcResultFileType::Boundary;
    if (lower.endsWith(QStringLiteral(".prt5"))) return FcResultFileType::Particle;
    if (lower.endsWith(QStringLiteral(".iso"))) return FcResultFileType::Isosurface;
    if (lower.endsWith(QStringLiteral(".s3d"))) return FcResultFileType::Smoke3D;
    if (lower.endsWith(QStringLiteral(".q"))) return FcResultFileType::Plot3D;
    return FcResultFileType::Other;
}
