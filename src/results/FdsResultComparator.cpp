#include "results/FdsResultComparator.h"

#include "results/FdsResultScanner.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QFont>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
struct CsvTable
{
    QStringList columns;
    QStringList units;
    std::vector<std::vector<double>> rows;
};

QStringList splitCsvLine(const QString& line)
{
    QStringList values;
    QString value;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size() &&
                line.at(index + 1) == QLatin1Char('"')) {
                value.append(character);
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == QLatin1Char(',') && !quoted) {
            values.append(value.trimmed());
            value.clear();
        } else {
            value.append(character);
        }
    }
    values.append(value.trimmed());
    return values;
}

QString normalizedColumnName(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('"'));
    value.remove(QLatin1Char('\''));
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return value;
}

bool readCsvTable(const QString& filePath, CsvTable& table, QString& errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = QStringLiteral("CSV file cannot be read: %1").arg(filePath);
        return false;
    }

    QTextStream stream(&file);
    QStringList lastHeader;
    QStringList previousHeader;
    qsizetype lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList fields = splitCsvLine(line);
        if (fields.isEmpty()) {
            continue;
        }
        bool firstIsNumeric = false;
        fields.constFirst().toDouble(&firstIsNumeric);
        if (!firstIsNumeric) {
            if (!table.rows.empty()) {
                errorMessage = QStringLiteral("Invalid CSV data at line %1: %2")
                                   .arg(lineNumber).arg(filePath);
                return false;
            }
            previousHeader = lastHeader;
            lastHeader = fields;
            continue;
        }

        std::vector<double> row;
        row.reserve(static_cast<std::size_t>(fields.size()));
        bool validRow = true;
        for (const QString& field : fields) {
            bool numeric = false;
            const double value = QLocale::c().toDouble(field.trimmed(), &numeric);
            if (!numeric || !std::isfinite(value)) {
                validRow = false;
                break;
            }
            row.push_back(value);
        }
        if (!validRow || row.size() < 2 ||
            (!table.rows.empty() && row.size() != table.rows.front().size()) ||
            (!lastHeader.isEmpty() && fields.size() != lastHeader.size()) ||
            (!table.rows.empty() && row.front() <= table.rows.back().front())) {
            errorMessage = QStringLiteral(
                "Invalid CSV row %1 (finite values, consistent columns and increasing times required): %2")
                               .arg(lineNumber).arg(filePath);
            return false;
        }
        table.rows.push_back(std::move(row));
    }

    if (table.rows.empty()) {
        errorMessage = QStringLiteral("CSV file has no numeric data rows: %1").arg(filePath);
        return false;
    }
    const qsizetype valueCount = static_cast<qsizetype>(table.rows.front().size());
    if (lastHeader.size() == valueCount) {
        table.columns = lastHeader;
    } else {
        table.columns.reserve(valueCount);
        table.columns.append(QStringLiteral("Time"));
        for (qsizetype index = 1; index < valueCount; ++index) {
            table.columns.append(QStringLiteral("Column %1").arg(index + 1));
        }
    }
    table.units = previousHeader.size() == valueCount
                      ? previousHeader : QStringList(valueCount, QString());
    return true;
}

bool isComparableCsv(const FdsResultFileInfo& file)
{
    if (!file.exists || !file.filePath.endsWith(QStringLiteral(".csv"),
                                                Qt::CaseInsensitive)) {
        return false;
    }
    switch (file.type) {
    case FcResultFileType::Cpu:
    case FcResultFileType::PressureIterations:
    case FcResultFileType::Steps:
    // Device/control event logs contain identifiers, states and optional values.
    // They are retained as provenance, not sampled physical time series.
    case FcResultFileType::DeviceControlLog:
        return false;
    default:
        return true;
    }
}

QString semanticFileKey(const FdsResultFileInfo& file, const QString& caseName)
{
    QString key = QFileInfo(file.name).completeBaseName().toLower();
    const QString prefix = caseName.trimmed().toLower();
    if (!prefix.isEmpty() && key.startsWith(prefix)) {
        key.remove(0, prefix.size());
    }
    while (key.startsWith(QLatin1Char('_')) || key.startsWith(QLatin1Char('-')) ||
           key.startsWith(QLatin1Char(' '))) {
        key.remove(0, 1);
    }
    if (!key.isEmpty()) {
        return key;
    }
    return QStringLiteral("csv");
}

QHash<QString, FdsResultFileInfo> comparableFiles(const FdsResultScanResult& scan,
                                                  QStringList& duplicateWarnings)
{
    QHash<QString, FdsResultFileInfo> files;
    for (const FdsResultFileInfo& file : scan.files) {
        if (!isComparableCsv(file)) {
            if (file.exists && file.type == FcResultFileType::DeviceControlLog) {
                duplicateWarnings.append(QStringLiteral(
                    "Event log retained but excluded from numerical time-series comparison: %1")
                                             .arg(file.filePath));
            }
            continue;
        }
        const QString key = semanticFileKey(file, scan.caseName);
        if (files.contains(key)) {
            duplicateWarnings.append(
                QStringLiteral("More than one comparable CSV maps to key '%1'; using %2.")
                    .arg(key, files.value(key).name));
            continue;
        }
        files.insert(key, file);
    }
    return files;
}

bool interpolatedValue(const CsvTable& table,
                       std::size_t column,
                       double time,
                       double& value)
{
    if (table.rows.empty() || column >= table.rows.front().size()) {
        return false;
    }
    const auto byTime = [](const std::vector<double>& row, double target) {
        return row.front() < target;
    };
    const auto upper = std::lower_bound(table.rows.cbegin(), table.rows.cend(), time,
                                        byTime);
    if (upper == table.rows.cbegin()) {
        const double tolerance = std::max(1.0e-9, std::abs(time) * 1.0e-9);
        if (std::abs(upper->front() - time) > tolerance) {
            return false;
        }
        value = upper->at(column);
        return true;
    }
    if (upper == table.rows.cend()) {
        const auto& last = table.rows.back();
        const double tolerance = std::max(1.0e-9, std::abs(time) * 1.0e-9);
        if (std::abs(last.front() - time) > tolerance) {
            return false;
        }
        value = last.at(column);
        return true;
    }
    if (upper->front() == time) {
        value = upper->at(column);
        return true;
    }
    const auto lower = std::prev(upper);
    const double interval = upper->front() - lower->front();
    if (interval <= 0.0) {
        return false;
    }
    const double fraction = (time - lower->front()) / interval;
    value = lower->at(column) + fraction * (upper->at(column) - lower->at(column));
    return std::isfinite(value);
}

QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString number(double value)
{
    return QLocale::c().toString(value, 'g', 12);
}

QString html(QString value)
{
    return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString seriesChart(const FdsCsvQuantityComparison& quantity)
{
    if (quantity.sampleTimes.size() < 2 ||
        quantity.sampleTimes.size() != quantity.referenceSamples.size() ||
        quantity.sampleTimes.size() != quantity.candidateSamples.size()) return {};
    constexpr int width = 620;
    constexpr int height = 220;
    constexpr double left = 92.0, right = 48.0, top = 24.0, bottom = 50.0;
    constexpr double plotWidth = width - left - right;
    constexpr double plotHeight = height - top - bottom;
    const auto timeRange = std::minmax_element(quantity.sampleTimes.cbegin(),
                                               quantity.sampleTimes.cend());
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (double value : quantity.referenceSamples) {
        minimum = std::min(minimum, value); maximum = std::max(maximum, value);
    }
    for (double value : quantity.candidateSamples) {
        minimum = std::min(minimum, value); maximum = std::max(maximum, value);
    }
    const double timeSpan = std::max(1.0e-12, *timeRange.second - *timeRange.first);
    const double valueSpan = std::max(1.0e-12, maximum - minimum);
    const QString unit = quantity.unit.isEmpty() ? QStringLiteral("unit not supplied") : quantity.unit;
    const QString timeUnit = quantity.timeUnit.isEmpty() ? QStringLiteral("unit not supplied") : quantity.timeUnit;
    const auto points = [&](const QVector<double>& values) {
        QPolygonF result;
        const int stride = std::max(1, static_cast<int>(values.size() / 300));
        for (int index = 0; index < values.size(); index += stride) {
            const double x = left + (quantity.sampleTimes.at(index) - *timeRange.first) /
                                         timeSpan * plotWidth;
            const double y = height - bottom - (values.at(index) - minimum) /
                                                 valueSpan * plotHeight;
            result.append(QPointF(x, y));
        }
        if ((values.size() - 1) % stride != 0) {
            const int index = values.size() - 1;
            const double x = left + (quantity.sampleTimes.at(index) - *timeRange.first) /
                                         timeSpan * plotWidth;
            const double y = height - bottom - (values.at(index) - minimum) /
                                                 valueSpan * plotHeight;
            result.append(QPointF(x, y));
        }
        return result;
    };
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hasGuiFonts =
        qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;
    if (hasGuiFonts) {
        QFont labelFont = painter.font();
        labelFont.setPixelSize(11);
        painter.setFont(labelFont);
    }
    painter.setPen(QColor(QStringLiteral("#333333")));
    if (hasGuiFonts) {
        painter.drawText(QRectF(0, 0, width, top - 3), Qt::AlignCenter,
                         QStringLiteral("%1 (%2)").arg(quantity.quantity, unit));
        painter.drawText(QRectF(left, height - 18, plotWidth, 16), Qt::AlignCenter,
                         QStringLiteral("Time (%1)").arg(timeUnit));
    }
    for (int tick = 0; tick <= 2; ++tick) {
        const double x = left + tick * plotWidth / 2.0;
        const double time = *timeRange.first + tick * (*timeRange.second - *timeRange.first) / 2.0;
        painter.drawLine(QPointF(x, height - bottom), QPointF(x, height - bottom + 4));
        if (hasGuiFonts) {
            painter.drawText(QRectF(x - 44, height - bottom + 6, 88, 16), Qt::AlignCenter,
                             QLocale::c().toString(time, 'g', 4));
        }
        if (tick > 0 && minimum == maximum) continue;
        const double y = height - bottom - tick * plotHeight / 2.0;
        const double value = minimum + tick * (maximum - minimum) / 2.0;
        painter.drawLine(QPointF(left - 4, y), QPointF(left, y));
        if (hasGuiFonts) {
            painter.drawText(QRectF(0, y - 8, left - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                             QLocale::c().toString(value, 'g', 4));
        }
    }
    painter.setPen(QPen(QColor(QStringLiteral("#777777")), 1.0));
    painter.drawLine(QPointF(left, height - bottom), QPointF(width - right, height - bottom));
    painter.drawLine(QPointF(left, top), QPointF(left, height - bottom));
    painter.setPen(QPen(QColor(QStringLiteral("#1565c0")), 2.0));
    painter.drawPolyline(points(quantity.referenceSamples));
    painter.setPen(QPen(QColor(QStringLiteral("#ef6c00")), 2.0));
    painter.drawPolyline(points(quantity.candidateSamples));
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    const QString caption = QStringLiteral("Time (%1): %2 .. %3; %4 (%5): %6 .. %7")
        .arg(html(timeUnit), number(*timeRange.first), number(*timeRange.second),
             html(quantity.quantity), html(unit), number(minimum), number(maximum));
    QString chart = QStringLiteral(
        "<div class='series-legend'><span style='color:#1565c0'>Reference</span> "
        "<span style='color:#ef6c00'>Candidate</span></div>"
        "<p style='font-size:8pt;margin:2px 0'>%1</p>"
        "<img alt='%2 time series' width='620' height='220' "
        "src='data:image/png;base64,%3'>")
        .arg(caption, html(quantity.quantity), QString::fromLatin1(png.toBase64()));
    if (!hasGuiFonts) {
        // QCoreApplication callers can render image geometry but cannot use
        // Qt's font database. Keep every axis label next to the headless PNG.
        chart += QStringLiteral(
            "<div class='headless-axis-labels'>"
            "<p>Horizontal axis: Time (%1); left: %2; middle: %3; right: %4.</p>")
                     .arg(html(timeUnit), number(*timeRange.first),
                          number(*timeRange.first + (*timeRange.second - *timeRange.first) / 2.0),
                          number(*timeRange.second));
        if (minimum == maximum) {
            chart += QStringLiteral(
                "<p>Vertical axis: %1 (%2); constant value: %3 (single tick).</p>")
                         .arg(html(quantity.quantity), html(unit), number(minimum));
        } else {
            chart += QStringLiteral(
                "<p>Vertical axis: %1 (%2); bottom: %3; middle: %4; top: %5.</p>")
                         .arg(html(quantity.quantity), html(unit), number(minimum),
                              number(minimum + (maximum - minimum) / 2.0), number(maximum));
        }
        chart += QStringLiteral("</div>");
    }
    return chart;
}

QString fileSha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

FdsResultProvenance provenance(const FdsResultScanResult& scan)
{
    FdsResultProvenance value;
    value.fdsInputFile = scan.fdsInputFilePath;
    value.fdsInputSha256 = fileSha256(scan.fdsInputFilePath);
    value.simulationEndTime = scan.endTime;
    for (const FdsResultFileInfo& file : scan.files) {
        if (file.type == FcResultFileType::OutputLog && file.exists &&
            QFileInfo(file.filePath).completeBaseName().compare(
                scan.caseName, Qt::CaseInsensitive) == 0) {
            value.outputLogFile = file.filePath;
            break;
        }
    }
    QFile output(value.outputLogFile);
    if (!output.open(QIODevice::ReadOnly | QIODevice::Text)) return value;
    const QString contents = QString::fromUtf8(output.readAll());
    const QRegularExpression revisionExpression(
        QStringLiteral(R"(^\s*Revision\s*:\s*(.+?)\s*$)"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch revisionMatch =
        revisionExpression.match(contents);
    if (revisionMatch.hasMatch()) {
        value.solverRevision = revisionMatch.captured(1).trimmed();
    }
    const QRegularExpression timeExpression(
        QStringLiteral(R"(Total Time:\s*([0-9Ee+\-.]+)\s*s)"));
    QRegularExpressionMatchIterator times = timeExpression.globalMatch(contents);
    while (times.hasNext()) {
        value.completedTime = times.next().captured(1);
    }
    value.normalTermination = contents.contains(
        QStringLiteral("STOP: FDS completed successfully"));
    return value;
}
}

QStringList FdsResultComparison::provenanceIssues() const
{
    QStringList issues;
    if (QFileInfo(referenceSmvFile).canonicalFilePath() ==
        QFileInfo(candidateSmvFile).canonicalFilePath()) {
        return issues;
    }
    if (referenceProvenance.solverRevision.isEmpty() ||
        candidateProvenance.solverRevision.isEmpty()) {
        issues.append(QStringLiteral("One or both FDS solver revisions are unavailable."));
    } else if (referenceProvenance.solverRevision.compare(
                   candidateProvenance.solverRevision, Qt::CaseInsensitive) != 0) {
        issues.append(QStringLiteral("FDS solver revisions differ."));
    }
    if (!referenceProvenance.normalTermination ||
        !candidateProvenance.normalTermination) {
        issues.append(QStringLiteral("One or both FDS runs did not terminate normally."));
    }
    bool referenceTimeOk = false;
    bool candidateTimeOk = false;
    const double referenceTime =
        referenceProvenance.simulationEndTime.toDouble(&referenceTimeOk);
    const double candidateTime =
        candidateProvenance.simulationEndTime.toDouble(&candidateTimeOk);
    if (!referenceTimeOk || !candidateTimeOk) {
        issues.append(QStringLiteral("One or both physical simulation end times are unavailable."));
    } else {
        const double scale = std::max({1.0, std::abs(referenceTime),
                                      std::abs(candidateTime)});
        if (std::abs(referenceTime - candidateTime) > 1.0e-6 * scale) {
            issues.append(QStringLiteral("Physical simulation end times differ (%1 s versus %2 s).")
                              .arg(number(referenceTime), number(candidateTime)));
        }
    }
    return issues;
}

bool FdsResultComparison::provenanceComparable() const
{
    return provenanceIssues().isEmpty();
}

bool FdsResultComparison::passed() const
{
    if (!success() || quantities.empty() || !missingCandidateFiles.isEmpty() ||
        !missingCandidateQuantities.isEmpty() || !provenanceComparable()) {
        return false;
    }
    return std::all_of(quantities.cbegin(), quantities.cend(),
                       [](const FdsCsvQuantityComparison& quantity) {
                           return quantity.withinTolerance;
                       });
}

QString FdsResultComparison::csvReport() const
{
    const QString hashStatus =
        !referenceProvenance.fdsInputSha256.isEmpty() &&
                referenceProvenance.fdsInputSha256 ==
                    candidateProvenance.fdsInputSha256
            ? QStringLiteral("MATCH")
            : QStringLiteral("DIFFERENT_OR_UNAVAILABLE");
    QString report = QStringLiteral("record,key,reference,candidate,status\n");
    report += QStringLiteral("provenance,fds_input_sha256,%1,%2,%3\n")
                  .arg(csvField(referenceProvenance.fdsInputSha256),
                       csvField(candidateProvenance.fdsInputSha256), hashStatus);
    report += QStringLiteral("provenance,solver_revision,%1,%2,INFO\n")
                  .arg(csvField(referenceProvenance.solverRevision),
                       csvField(candidateProvenance.solverRevision));
    report += QStringLiteral("provenance,completed_time_s,%1,%2,INFO\n")
                  .arg(csvField(referenceProvenance.completedTime),
                       csvField(candidateProvenance.completedTime));
    report += QStringLiteral("provenance,simulation_end_time_s,%1,%2,%3\n")
                  .arg(csvField(referenceProvenance.simulationEndTime),
                       csvField(candidateProvenance.simulationEndTime),
                       provenanceComparable() ? QStringLiteral("PASS")
                                              : QStringLiteral("REVIEW"));
    report += QStringLiteral("provenance,normal_termination,%1,%2,%3\n")
                  .arg(referenceProvenance.normalTermination
                           ? QStringLiteral("true") : QStringLiteral("false"),
                       candidateProvenance.normalTermination
                           ? QStringLiteral("true") : QStringLiteral("false"),
                       provenanceComparable() ? QStringLiteral("PASS")
                                              : QStringLiteral("REVIEW"));
    for (const QString& issue : provenanceIssues()) {
        report += QStringLiteral("provenance,issue,%1,,REVIEW\n")
                      .arg(csvField(issue));
    }
    report += QLatin1Char('\n');
    report += QStringLiteral(
        "file_key,reference_file,candidate_file,quantity,samples,reference_final,"
        "candidate_final,reference_peak,candidate_peak,reference_peak_time,"
        "candidate_peak_time,reference_mean,candidate_mean,relative_final_error,"
        "rmse,nrmse,max_abs_error,status\n");
    for (const FdsCsvQuantityComparison& quantity : quantities) {
        report += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,"
                                 "%14,%15,%16,%17,%18\n")
                      .arg(csvField(quantity.fileKey),
                           csvField(quantity.referenceFile),
                           csvField(quantity.candidateFile),
                           csvField(quantity.quantity))
                      .arg(quantity.sampleCount)
                      .arg(number(quantity.referenceFinal),
                           number(quantity.candidateFinal),
                           number(quantity.referencePeak),
                           number(quantity.candidatePeak),
                           number(quantity.referencePeakTime),
                           number(quantity.candidatePeakTime),
                           number(quantity.referenceMean),
                           number(quantity.candidateMean),
                           number(quantity.relativeFinalError),
                           number(quantity.rootMeanSquareError),
                           number(quantity.normalizedRootMeanSquareError),
                           number(quantity.maximumAbsoluteError),
                           quantity.withinTolerance ? QStringLiteral("PASS")
                                                    : QStringLiteral("FAIL"));
    }
    return report;
}

QString FdsResultComparison::htmlReport() const
{
    QString report = QStringLiteral(
        "<h2>FDS result comparison</h2><p><b>Status:</b> %1<br>"
        "<b>Reference:</b> %2<br><b>Candidate:</b> %3<br>"
        "<b>Relative tolerance:</b> %4%<br><b>Provenance gate:</b> %5</p>"
        "<table><tr><th>Quantity</th><th>Samples</th><th>Final (R/C)</th>"
        "<th>RMSE</th><th>NRMSE</th><th>Max abs.</th><th>Status</th></tr>")
        .arg(passed() ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             html(referenceSmvFile), html(candidateSmvFile))
        .arg(relativeTolerance * 100.0, 0, 'g', 5)
        .arg(provenanceComparable() ? QStringLiteral("PASS")
                                    : QStringLiteral("REVIEW"));
    if (!provenanceComparable()) {
        report += QStringLiteral("<ul>");
        for (const QString& issue : provenanceIssues()) {
            report += QStringLiteral("<li>%1</li>").arg(html(issue));
        }
        report += QStringLiteral("</ul>");
    }
    for (const FdsCsvQuantityComparison& quantity : quantities) {
        report += QStringLiteral(
            "<tr><td>%1<br><small>%2</small></td><td>%3</td><td>%4 / %5</td>"
            "<td>%6</td><td>%7</td><td>%8</td><td class='%9'>%10</td></tr>")
            .arg(html(quantity.quantity), html(quantity.referenceFile))
            .arg(quantity.sampleCount)
            .arg(number(quantity.referenceFinal), number(quantity.candidateFinal),
                 number(quantity.rootMeanSquareError),
                 number(quantity.normalizedRootMeanSquareError),
                 number(quantity.maximumAbsoluteError),
                 quantity.withinTolerance ? QStringLiteral("pass")
                                          : QStringLiteral("fail"),
                 quantity.withinTolerance ? QStringLiteral("PASS")
                                          : QStringLiteral("FAIL"));
    }
    report += QStringLiteral(
        "</table><h3>Peak, timing, and mean statistics</h3>"
        "<table><tr><th>Quantity</th><th>Peak (R/C)</th><th>Peak time (R/C)</th>"
        "<th>Mean (R/C)</th><th>Final relative error</th></tr>");
    for (const FdsCsvQuantityComparison& quantity : quantities) {
        report += QStringLiteral("<tr><td>%1</td><td>%2 / %3</td><td>%4 / %5</td>"
                                 "<td>%6 / %7</td><td>%8</td></tr>")
                      .arg(html(quantity.quantity),
                           number(quantity.referencePeak), number(quantity.candidatePeak),
                           number(quantity.referencePeakTime), number(quantity.candidatePeakTime),
                           number(quantity.referenceMean), number(quantity.candidateMean),
                           number(quantity.relativeFinalError));
    }
    report += QStringLiteral(
        "</table><div style='page-break-before:always'><h3>Time-series plots</h3>");
    int chartCount = 0;
    for (const FdsCsvQuantityComparison& quantity : quantities) {
        if (chartCount >= 24) {
            report += QStringLiteral("<p>Additional series omitted from HTML plots; all metrics remain in CSV.</p>");
            break;
        }
        if (chartCount > 0 && chartCount % 3 == 0) {
            report += QStringLiteral(
                "</div><div style='page-break-before:always'>");
        }
        report += QStringLiteral("<div class='series-chart'><h4>%1 — %2</h4>%3</div>")
                      .arg(html(quantity.referenceFile), html(quantity.quantity),
                           seriesChart(quantity));
        ++chartCount;
    }
    report += QStringLiteral("</div>");
    return report;
}

FdsResultComparison FdsResultComparator::compareSmvFiles(
    const QString& referenceSmvFile,
    const QString& candidateSmvFile,
    double relativeTolerance,
    double absoluteTolerance)
{
    FdsResultComparison result;
    result.referenceSmvFile = QFileInfo(referenceSmvFile).absoluteFilePath();
    result.candidateSmvFile = QFileInfo(candidateSmvFile).absoluteFilePath();
    result.relativeTolerance = std::max(0.0, relativeTolerance);
    result.absoluteTolerance = std::max(0.0, absoluteTolerance);

    const FdsResultScanResult referenceScan =
        FdsResultScanner().scanSmvFile(referenceSmvFile);
    if (!referenceScan.success()) {
        result.errorMessage = QStringLiteral("Reference results could not be scanned: %1")
                                  .arg(referenceScan.errorMessage);
        return result;
    }
    const FdsResultScanResult candidateScan =
        FdsResultScanner().scanSmvFile(candidateSmvFile);
    if (!candidateScan.success()) {
        result.errorMessage = QStringLiteral("Candidate results could not be scanned: %1")
                                  .arg(candidateScan.errorMessage);
        return result;
    }
    result.referenceCase = referenceScan.caseName;
    result.candidateCase = candidateScan.caseName;
    result.referenceProvenance = provenance(referenceScan);
    result.candidateProvenance = provenance(candidateScan);

    QStringList duplicateWarnings;
    const QHash<QString, FdsResultFileInfo> referenceFiles =
        comparableFiles(referenceScan, duplicateWarnings);
    const QHash<QString, FdsResultFileInfo> candidateFiles =
        comparableFiles(candidateScan, duplicateWarnings);
    result.warnings.append(duplicateWarnings);

    QStringList referenceKeys = referenceFiles.keys();
    std::sort(referenceKeys.begin(), referenceKeys.end(),
              [](const QString& left, const QString& right) {
                  return left.compare(right, Qt::CaseInsensitive) < 0;
              });
    for (const QString& key : referenceKeys) {
        if (!candidateFiles.contains(key)) {
            result.missingCandidateFiles.append(referenceFiles.value(key).name);
            continue;
        }
        ++result.matchedFileCount;
        const FdsResultFileInfo referenceFile = referenceFiles.value(key);
        const FdsResultFileInfo candidateFile = candidateFiles.value(key);
        CsvTable referenceTable;
        CsvTable candidateTable;
        QString readError;
        if (!readCsvTable(referenceFile.filePath, referenceTable, readError) ||
            !readCsvTable(candidateFile.filePath, candidateTable, readError)) {
            result.errorMessage = readError;
            return result;
        }

        const auto sameTime = [](double left, double right) {
            return std::abs(left - right) <=
                   1.0e-6 * std::max({1.0, std::abs(left), std::abs(right)});
        };
        if (referenceTable.units.constFirst().trimmed().compare(
                candidateTable.units.constFirst().trimmed(), Qt::CaseSensitive) != 0) {
            result.errorMessage = QStringLiteral("Time units differ for %1 (%2 versus %3).")
                                      .arg(key, referenceTable.units.constFirst(),
                                           candidateTable.units.constFirst());
            return result;
        }
        if (!sameTime(referenceTable.rows.front().front(), candidateTable.rows.front().front()) ||
            !sameTime(referenceTable.rows.back().front(), candidateTable.rows.back().front())) {
            result.errorMessage = QStringLiteral(
                "Physical CSV time coverage differs for %1 (%2..%3 versus %4..%5).")
                                      .arg(key)
                                      .arg(referenceTable.rows.front().front())
                                      .arg(referenceTable.rows.back().front())
                                      .arg(candidateTable.rows.front().front())
                                      .arg(candidateTable.rows.back().front());
            return result;
        }

        QHash<QString, std::size_t> candidateColumns;
        for (qsizetype index = 1; index < candidateTable.columns.size(); ++index) {
            candidateColumns.insert(normalizedColumnName(candidateTable.columns.at(index)),
                                    static_cast<std::size_t>(index));
        }
        for (qsizetype referenceIndex = 1;
             referenceIndex < referenceTable.columns.size(); ++referenceIndex) {
            const QString columnKey =
                normalizedColumnName(referenceTable.columns.at(referenceIndex));
            if (!candidateColumns.contains(columnKey)) {
                result.missingCandidateQuantities.append(
                    QStringLiteral("%1: %2")
                        .arg(referenceFile.name,
                             referenceTable.columns.at(referenceIndex)));
                result.warnings.append(
                    QStringLiteral("Quantity '%1' is missing from %2.")
                        .arg(referenceTable.columns.at(referenceIndex), candidateFile.name));
                continue;
            }
            const std::size_t candidateIndex = candidateColumns.value(columnKey);
            if (referenceTable.units.at(referenceIndex).trimmed().compare(
                    candidateTable.units.at(static_cast<qsizetype>(candidateIndex)).trimmed(),
                    Qt::CaseSensitive) != 0) {
                result.errorMessage = QStringLiteral("Physical units differ for %1 / %2 (%3 versus %4).")
                                          .arg(key, referenceTable.columns.at(referenceIndex),
                                               referenceTable.units.at(referenceIndex),
                                               candidateTable.units.at(static_cast<qsizetype>(candidateIndex)));
                return result;
            }
            const std::size_t referenceColumn =
                static_cast<std::size_t>(referenceIndex);
            FdsCsvQuantityComparison comparison;
            comparison.fileKey = key;
            comparison.referenceFile = referenceFile.name;
            comparison.candidateFile = candidateFile.name;
            comparison.quantity = referenceTable.columns.at(referenceIndex);
            comparison.unit = referenceTable.units.at(referenceIndex).trimmed();
            comparison.timeUnit = referenceTable.units.constFirst().trimmed();
            double squaredError = 0.0;
            double referenceSum = 0.0;
            double candidateSum = 0.0;
            for (const std::vector<double>& row : referenceTable.rows) {
                if (referenceColumn >= row.size()) {
                    continue;
                }
                double candidateValue = 0.0;
                if (!interpolatedValue(candidateTable, candidateIndex, row.front(),
                                       candidateValue)) {
                    result.errorMessage = QStringLiteral(
                        "Candidate CSV cannot cover reference sample %1 for %2 / %3.")
                                              .arg(row.front()).arg(key, comparison.quantity);
                    return result;
                }
                const double referenceValue = row.at(referenceColumn);
                const double difference = candidateValue - referenceValue;
                squaredError += difference * difference;
                comparison.maximumAbsoluteError =
                    std::max(comparison.maximumAbsoluteError, std::abs(difference));
                if (std::abs(referenceValue) >= comparison.referencePeak) {
                    comparison.referencePeak = std::abs(referenceValue);
                    comparison.referencePeakTime = row.front();
                }
                if (std::abs(candidateValue) >= comparison.candidatePeak) {
                    comparison.candidatePeak = std::abs(candidateValue);
                    comparison.candidatePeakTime = row.front();
                }
                comparison.referenceFinal = referenceValue;
                comparison.candidateFinal = candidateValue;
                referenceSum += referenceValue;
                candidateSum += candidateValue;
                comparison.sampleTimes.append(row.front());
                comparison.referenceSamples.append(referenceValue);
                comparison.candidateSamples.append(candidateValue);
                ++comparison.sampleCount;
            }
            if (comparison.sampleCount == 0) {
                result.errorMessage = QStringLiteral("Quantity '%1' has no overlapping time samples.")
                                          .arg(comparison.quantity);
                return result;
            }
            comparison.rootMeanSquareError =
                std::sqrt(squaredError / comparison.sampleCount);
            comparison.referenceMean = referenceSum / comparison.sampleCount;
            comparison.candidateMean = candidateSum / comparison.sampleCount;
            const double scale = std::max(comparison.referencePeak,
                                          result.absoluteTolerance);
            comparison.relativeFinalError =
                std::abs(comparison.candidateFinal - comparison.referenceFinal) /
                std::max(std::abs(comparison.referenceFinal), result.absoluteTolerance);
            comparison.normalizedRootMeanSquareError =
                scale > 0.0 ? comparison.rootMeanSquareError / scale
                            : comparison.rootMeanSquareError;
            comparison.withinTolerance =
                comparison.rootMeanSquareError <=
                result.absoluteTolerance + result.relativeTolerance * scale;
            result.quantities.push_back(comparison);
        }
    }

    QStringList candidateKeys = candidateFiles.keys();
    std::sort(candidateKeys.begin(), candidateKeys.end());
    for (const QString& key : candidateKeys) {
        if (!referenceFiles.contains(key)) {
            result.candidateOnlyFiles.append(candidateFiles.value(key).name);
        }
    }
    if (result.quantities.empty()) {
        result.errorMessage = QStringLiteral(
            "The two result cases do not contain comparable physical CSV quantities.");
    }
    return result;
}
