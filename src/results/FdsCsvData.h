#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct FdsSeriesStatistics
{
    int sampleCount = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double peakAbsolute = 0.0;
};

struct FdsCsvSeries
{
    QString name;
    QString unit;
    QVector<double> values;
};

struct FdsCsvData
{
    QString filePath;
    QString timeName = QStringLiteral("Time");
    QString timeUnit = QStringLiteral("s");
    QVector<double> times;
    QVector<FdsCsvSeries> series;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
    double startTime() const;
    double endTime() const;
    double interpolatedValue(int seriesIndex, double time, bool* ok = nullptr) const;
    FdsSeriesStatistics statistics(int seriesIndex) const;
};

struct FdsCsvReadOptions
{
    qint64 maximumFileBytes = 1024LL * 1024LL * 1024LL;
    qsizetype maximumRows = 5'000'000;
};

class FdsCsvReader final
{
public:
    static FdsCsvData read(const QString& filePath,
                           const FdsCsvReadOptions& options = {});
};
