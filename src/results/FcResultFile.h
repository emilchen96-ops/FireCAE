#pragma once

#include "core/FcObject.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

enum class FcResultFileType
{
    Smokeview, FdsInput, OutputLog, ErrorLog, Devices, DeviceControlLog,
    Controls, HeatReleaseRate, Hvac, PressureIterations, Steps, Cpu, Csv,
    Slice, Boundary, Particle, Isosurface, Smoke3D, Plot3D, Other
};

QString resultFileTypeName(FcResultFileType type);
QString resultFileCategoryName(FcResultFileType type);

class FcResultFile final : public FcObject
{
public:
    FcResultFile(const QString& name, FcResultFileType fileType,
                 const QString& filePath, qint64 fileSize, bool exists,
                 const QDateTime& lastModified, int columnCount = 0,
                 qint64 dataRowCount = 0, const QString& startTime = {},
                 const QString& endTime = {}, const QStringList& columnNames = {});

    FcResultFileType fileType() const;
    const QString& filePath() const;
    qint64 fileSize() const;
    bool exists() const;
    const QDateTime& lastModified() const;
    int columnCount() const;
    qint64 dataRowCount() const;
    const QString& startTime() const;
    const QString& endTime() const;
    const QStringList& columnNames() const;

private:
    FcResultFileType m_fileType = FcResultFileType::Other;
    QString m_filePath;
    qint64 m_fileSize = 0;
    bool m_exists = false;
    QDateTime m_lastModified;
    int m_columnCount = 0;
    qint64 m_dataRowCount = 0;
    QString m_startTime;
    QString m_endTime;
    QStringList m_columnNames;
};
