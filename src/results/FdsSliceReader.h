#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <limits>

struct FdsSliceFrame
{
    float time = 0.0F;
    QVector<float> values;
    float minimum = std::numeric_limits<float>::quiet_NaN();
    float maximum = std::numeric_limits<float>::quiet_NaN();
};

struct FdsSliceData
{
    QString filePath;
    QString longLabel;
    QString shortLabel;
    QString unit;
    std::array<int, 6> indexBounds = {0, -1, 0, -1, 0, -1};
    int nx = 0;
    int ny = 0;
    int nz = 0;
    bool littleEndian = true;
    QVector<FdsSliceFrame> frames;
    QStringList warnings;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty(); }
    qsizetype valuesPerFrame() const;
    bool isPlanar() const;
    float value(int frame, int i, int j, int k) const;
};

class FdsSliceReader final
{
public:
    struct Limits
    {
        qsizetype maximumFrames = 100000;
        qsizetype maximumValuesPerFrame = 100000000;
        qsizetype maximumTotalValues = 50000000;
        qint64 maximumRecordBytes = 1024LL * 1024LL * 1024LL;
    };

    static FdsSliceData read(const QString& filePath,
                             const Limits& limits = Limits{});
};
