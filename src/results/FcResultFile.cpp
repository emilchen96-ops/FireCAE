#include "results/FcResultFile.h"

QString resultFileTypeName(FcResultFileType type)
{
    switch (type) {
    case FcResultFileType::Smokeview: return QStringLiteral("Smokeview Case");
    case FcResultFileType::FdsInput: return QStringLiteral("FDS Input");
    case FcResultFileType::OutputLog: return QStringLiteral("FDS Output Log");
    case FcResultFileType::ErrorLog: return QStringLiteral("FDS Error Log");
    case FcResultFileType::Devices: return QStringLiteral("Device CSV");
    case FcResultFileType::DeviceControlLog: return QStringLiteral("Device Control Log");
    case FcResultFileType::Controls: return QStringLiteral("Control CSV");
    case FcResultFileType::HeatReleaseRate: return QStringLiteral("HRR CSV");
    case FcResultFileType::Hvac: return QStringLiteral("HVAC CSV");
    case FcResultFileType::PressureIterations: return QStringLiteral("Pressure Iterations");
    case FcResultFileType::Steps: return QStringLiteral("Time Steps");
    case FcResultFileType::Cpu: return QStringLiteral("CPU Usage");
    case FcResultFileType::Csv: return QStringLiteral("CSV Data");
    case FcResultFileType::Slice: return QStringLiteral("Slice");
    case FcResultFileType::Boundary: return QStringLiteral("Boundary Data");
    case FcResultFileType::Particle: return QStringLiteral("Particles");
    case FcResultFileType::Isosurface: return QStringLiteral("Isosurface");
    case FcResultFileType::Smoke3D: return QStringLiteral("3D Smoke");
    case FcResultFileType::Plot3D: return QStringLiteral("Plot3D");
    case FcResultFileType::Other: return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

QString resultFileCategoryName(FcResultFileType type)
{
    switch (type) {
    case FcResultFileType::Smokeview:
    case FcResultFileType::FdsInput: return QStringLiteral("Summary");
    case FcResultFileType::OutputLog:
    case FcResultFileType::ErrorLog: return QStringLiteral("Log Files");
    case FcResultFileType::Devices:
    case FcResultFileType::DeviceControlLog: return QStringLiteral("Devices");
    case FcResultFileType::Controls: return QStringLiteral("Controls");
    case FcResultFileType::HeatReleaseRate: return QStringLiteral("Heat Release Rate");
    case FcResultFileType::Hvac: return QStringLiteral("HVAC");
    case FcResultFileType::PressureIterations:
    case FcResultFileType::Steps:
    case FcResultFileType::Cpu: return QStringLiteral("Solver Diagnostics");
    case FcResultFileType::Csv: return QStringLiteral("CSV Data");
    case FcResultFileType::Slice:
    case FcResultFileType::Plot3D: return QStringLiteral("Slices");
    case FcResultFileType::Boundary: return QStringLiteral("Boundary Data");
    case FcResultFileType::Particle: return QStringLiteral("Particles");
    case FcResultFileType::Isosurface: return QStringLiteral("Isosurfaces");
    case FcResultFileType::Smoke3D: return QStringLiteral("3D Smoke");
    case FcResultFileType::Other: return QStringLiteral("Other Files");
    }
    return QStringLiteral("Other Files");
}

FcResultFile::FcResultFile(const QString& name, FcResultFileType fileType,
                           const QString& filePath, qint64 fileSize, bool exists,
                           const QDateTime& lastModified, int columnCount,
                           qint64 dataRowCount, const QString& startTime,
                           const QString& endTime, const QStringList& columnNames)
    : FcObject(name, FcObjectType::ResultFile), m_fileType(fileType),
      m_filePath(filePath), m_fileSize(fileSize), m_exists(exists),
      m_lastModified(lastModified), m_columnCount(columnCount),
      m_dataRowCount(dataRowCount), m_startTime(startTime), m_endTime(endTime),
      m_columnNames(columnNames)
{
}

FcResultFileType FcResultFile::fileType() const { return m_fileType; }
const QString& FcResultFile::filePath() const { return m_filePath; }
qint64 FcResultFile::fileSize() const { return m_fileSize; }
bool FcResultFile::exists() const { return m_exists; }
const QDateTime& FcResultFile::lastModified() const { return m_lastModified; }
int FcResultFile::columnCount() const { return m_columnCount; }
qint64 FcResultFile::dataRowCount() const { return m_dataRowCount; }
const QString& FcResultFile::startTime() const { return m_startTime; }
const QString& FcResultFile::endTime() const { return m_endTime; }
const QStringList& FcResultFile::columnNames() const { return m_columnNames; }
