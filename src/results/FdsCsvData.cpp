#include "results/FdsCsvData.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QStringList splitCsvLine(const QString& line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size() &&
                line.at(index + 1) == QLatin1Char('"')) {
                field.append(character);
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == QLatin1Char(',') && !quoted) {
            fields.append(field.trimmed());
            field.clear();
        } else {
            field.append(character);
        }
    }
    fields.append(field.trimmed());
    return fields;
}

bool numericRow(const QStringList& fields, QVector<double>& values)
{
    values.clear();
    values.reserve(fields.size());
    for (const QString& field : fields) {
        bool ok = false;
        const double value = QLocale::c().toDouble(field.trimmed(), &ok);
        if (!ok || !std::isfinite(value)) {
            values.clear();
            return false;
        }
        values.append(value);
    }
    return values.size() >= 2;
}
}

double FdsCsvData::startTime() const
{
    return times.isEmpty() ? 0.0 : times.constFirst();
}

double FdsCsvData::endTime() const
{
    return times.isEmpty() ? 0.0 : times.constLast();
}

double FdsCsvData::interpolatedValue(int seriesIndex, double time, bool* ok) const
{
    if (ok) *ok = false;
    if (seriesIndex < 0 || seriesIndex >= series.size() || times.isEmpty() ||
        series.at(seriesIndex).values.size() != times.size()) {
        return 0.0;
    }
    const auto upper = std::lower_bound(times.cbegin(), times.cend(), time);
    if (upper == times.cbegin()) {
        if (ok) *ok = true;
        return series.at(seriesIndex).values.constFirst();
    }
    if (upper == times.cend()) {
        if (ok) *ok = true;
        return series.at(seriesIndex).values.constLast();
    }
    const qsizetype upperIndex = std::distance(times.cbegin(), upper);
    if (*upper == time) {
        if (ok) *ok = true;
        return series.at(seriesIndex).values.at(upperIndex);
    }
    const qsizetype lowerIndex = upperIndex - 1;
    const double interval = times.at(upperIndex) - times.at(lowerIndex);
    if (interval <= 0.0) return 0.0;
    const double fraction = (time - times.at(lowerIndex)) / interval;
    if (ok) *ok = true;
    return series.at(seriesIndex).values.at(lowerIndex) +
           fraction * (series.at(seriesIndex).values.at(upperIndex) -
                       series.at(seriesIndex).values.at(lowerIndex));
}

FdsSeriesStatistics FdsCsvData::statistics(int seriesIndex) const
{
    FdsSeriesStatistics result;
    if (seriesIndex < 0 || seriesIndex >= series.size() ||
        series.at(seriesIndex).values.isEmpty()) {
        return result;
    }
    result.minimum = std::numeric_limits<double>::max();
    result.maximum = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    for (double value : series.at(seriesIndex).values) {
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        result.peakAbsolute = std::max(result.peakAbsolute, std::abs(value));
        sum += value;
        ++result.sampleCount;
    }
    result.mean = result.sampleCount > 0 ? sum / result.sampleCount : 0.0;
    return result;
}

FdsCsvData FdsCsvReader::read(const QString& filePath,
                              const FdsCsvReadOptions& options)
{
    FdsCsvData result;
    result.filePath = QFileInfo(filePath).absoluteFilePath();
    const QFileInfo info(result.filePath);
    if (!info.exists() || !info.isFile()) {
        result.errorMessage = QStringLiteral("CSV file does not exist: %1")
                                  .arg(result.filePath);
        return result;
    }
    if (info.size() > options.maximumFileBytes) {
        result.errorMessage = QStringLiteral(
            "CSV file is larger than the configured safety limit (%1 MiB).")
                                  .arg(options.maximumFileBytes / (1024 * 1024));
        return result;
    }
    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("CSV file cannot be read: %1")
                                  .arg(result.filePath);
        return result;
    }

    QTextStream stream(&file);
    QVector<QStringList> textRows;
    QVector<QVector<double>> rows;
    int expectedColumns = -1;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList fields = splitCsvLine(line);
        QVector<double> numeric;
        if (!numericRow(fields, numeric)) {
            textRows.append(fields);
            continue;
        }
        if (expectedColumns < 0) expectedColumns = numeric.size();
        if (numeric.size() != expectedColumns) {
            result.warnings.append(
                QStringLiteral("Skipped a CSV row whose column count is inconsistent."));
            continue;
        }
        if (!rows.isEmpty() && numeric.constFirst() < rows.constLast().constFirst()) {
            result.warnings.append(
                QStringLiteral("Skipped an out-of-order time row."));
            continue;
        }
        rows.append(std::move(numeric));
        if (rows.size() >= options.maximumRows) {
            result.warnings.append(
                QStringLiteral("CSV loading stopped at the configured row limit."));
            break;
        }
    }
    if (rows.isEmpty() || expectedColumns < 2) {
        result.errorMessage = QStringLiteral("CSV file has no numeric time-series rows: %1")
                                  .arg(result.filePath);
        return result;
    }

    QStringList headers;
    QStringList units;
    for (qsizetype index = textRows.size() - 1; index >= 0; --index) {
        if (textRows.at(index).size() != expectedColumns) continue;
        if (headers.isEmpty()) headers = textRows.at(index);
        else {
            units = textRows.at(index);
            break;
        }
    }
    if (headers.size() != expectedColumns) {
        headers.clear();
        headers.append(QStringLiteral("Time"));
        for (int index = 1; index < expectedColumns; ++index)
            headers.append(QStringLiteral("Column %1").arg(index + 1));
    }
    if (units.size() != expectedColumns) units = QStringList(expectedColumns, QString());

    result.timeName = headers.constFirst();
    result.timeUnit = units.constFirst().isEmpty() ? QStringLiteral("s")
                                                   : units.constFirst();
    result.series.reserve(expectedColumns - 1);
    for (int column = 1; column < expectedColumns; ++column) {
        FdsCsvSeries series;
        series.name = headers.at(column);
        series.unit = units.at(column);
        series.values.reserve(rows.size());
        result.series.append(series);
    }
    result.times.reserve(rows.size());
    for (const QVector<double>& row : rows) {
        result.times.append(row.constFirst());
        for (int column = 1; column < expectedColumns; ++column)
            result.series[column - 1].values.append(row.at(column));
    }
    return result;
}
