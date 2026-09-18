#include "results/FdsSliceReader.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace
{
enum class RecordStatus
{
    Ok,
    EndOfFile,
    Error
};

quint32 unsigned32(const char* bytes, bool littleEndian)
{
    const auto b0 = static_cast<quint32>(static_cast<unsigned char>(bytes[0]));
    const auto b1 = static_cast<quint32>(static_cast<unsigned char>(bytes[1]));
    const auto b2 = static_cast<quint32>(static_cast<unsigned char>(bytes[2]));
    const auto b3 = static_cast<quint32>(static_cast<unsigned char>(bytes[3]));
    return littleEndian ? b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U)
                        : b3 | (b2 << 8U) | (b1 << 16U) | (b0 << 24U);
}

RecordStatus readRecord(QFile& file, bool littleEndian, qint64 maximumBytes,
                        QByteArray& payload, QString& error)
{
    char markerBytes[4];
    const qint64 markerRead = file.read(markerBytes, 4);
    if (markerRead == 0 && file.atEnd()) {
        return RecordStatus::EndOfFile;
    }
    if (markerRead != 4) {
        error = QStringLiteral("Truncated Fortran record marker at byte %1.")
                    .arg(file.pos() - std::max<qint64>(0, markerRead));
        return RecordStatus::Error;
    }
    const quint32 length = unsigned32(markerBytes, littleEndian);
    if (length > static_cast<quint64>(maximumBytes)) {
        error = QStringLiteral("Fortran record length %1 exceeds the safety limit.")
                    .arg(length);
        return RecordStatus::Error;
    }
    payload = file.read(static_cast<qint64>(length));
    if (payload.size() != static_cast<qsizetype>(length)) {
        error = QStringLiteral("Truncated Fortran record payload at byte %1.")
                    .arg(file.pos());
        return RecordStatus::Error;
    }
    char footerBytes[4];
    if (file.read(footerBytes, 4) != 4) {
        error = QStringLiteral("Missing Fortran record footer at byte %1.")
                    .arg(file.pos());
        return RecordStatus::Error;
    }
    const quint32 footer = unsigned32(footerBytes, littleEndian);
    if (footer != length) {
        error = QStringLiteral("Fortran record marker mismatch (%1 versus %2).")
                    .arg(length).arg(footer);
        return RecordStatus::Error;
    }
    return RecordStatus::Ok;
}

QString fixedText(const QByteArray& record)
{
    return QString::fromLatin1(record).trimmed();
}

void configureStream(QDataStream& stream, bool littleEndian)
{
    stream.setByteOrder(littleEndian ? QDataStream::LittleEndian
                                    : QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
}
}

qsizetype FdsSliceData::valuesPerFrame() const
{
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        return 0;
    }
    return static_cast<qsizetype>(nx) * static_cast<qsizetype>(ny) *
           static_cast<qsizetype>(nz);
}

bool FdsSliceData::isPlanar() const
{
    return nx == 1 || ny == 1 || nz == 1;
}

float FdsSliceData::value(int frame, int i, int j, int k) const
{
    if (frame < 0 || frame >= frames.size() || i < 0 || i >= nx ||
        j < 0 || j >= ny || k < 0 || k >= nz) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const qsizetype offset =
        (static_cast<qsizetype>(i) * ny + j) * nz + k;
    if (offset < 0 || offset >= frames.at(frame).values.size()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return frames.at(frame).values.at(offset);
}

FdsSliceData FdsSliceReader::read(const QString& filePath,
                                  const Limits& limits)
{
    FdsSliceData result;
    result.filePath = QFileInfo(filePath).absoluteFilePath();
    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("FDS slice file cannot be read: %1")
                                  .arg(result.filePath);
        return result;
    }
    char firstMarker[4];
    if (file.peek(firstMarker, 4) != 4) {
        result.errorMessage = QStringLiteral("FDS slice file is too short.");
        return result;
    }
    const quint32 littleMarker = unsigned32(firstMarker, true);
    const quint32 bigMarker = unsigned32(firstMarker, false);
    if (littleMarker == 30U) {
        result.littleEndian = true;
    } else if (bigMarker == 30U) {
        result.littleEndian = false;
    } else {
        result.errorMessage = QStringLiteral(
            "Unsupported slice encoding: expected a 30-byte Fortran label record. "
            "Compressed or non-classic slice files are not yet supported.");
        return result;
    }

    QByteArray record;
    QString recordError;
    const auto requiredRecord = [&](qsizetype expected, const QString& description) {
        const RecordStatus status = readRecord(file, result.littleEndian,
                                               limits.maximumRecordBytes,
                                               record, recordError);
        if (status != RecordStatus::Ok) {
            result.errorMessage = QStringLiteral("%1: %2")
                                      .arg(description, recordError);
            return false;
        }
        if (record.size() != expected) {
            result.errorMessage = QStringLiteral("%1 has %2 bytes; expected %3.")
                                      .arg(description).arg(record.size()).arg(expected);
            return false;
        }
        return true;
    };
    if (!requiredRecord(30, QStringLiteral("Long label record"))) return result;
    result.longLabel = fixedText(record);
    if (!requiredRecord(30, QStringLiteral("Short label record"))) return result;
    result.shortLabel = fixedText(record);
    if (!requiredRecord(30, QStringLiteral("Unit record"))) return result;
    result.unit = fixedText(record);
    if (!requiredRecord(24, QStringLiteral("Slice index record"))) return result;
    {
        QDataStream stream(&record, QIODevice::ReadOnly);
        configureStream(stream, result.littleEndian);
        for (int& bound : result.indexBounds) {
            qint32 value = 0;
            stream >> value;
            bound = value;
        }
        if (stream.status() != QDataStream::Ok) {
            result.errorMessage = QStringLiteral("Slice index record cannot be decoded.");
            return result;
        }
    }
    result.nx = result.indexBounds[1] - result.indexBounds[0] + 1;
    result.ny = result.indexBounds[3] - result.indexBounds[2] + 1;
    result.nz = result.indexBounds[5] - result.indexBounds[4] + 1;
    const qsizetype valueCount = result.valuesPerFrame();
    if (valueCount <= 0 || valueCount > limits.maximumValuesPerFrame) {
        result.errorMessage = QStringLiteral(
            "Slice dimensions %1 x %2 x %3 are invalid or exceed the safety limit.")
                                  .arg(result.nx).arg(result.ny).arg(result.nz);
        return result;
    }
    if (valueCount > std::numeric_limits<qsizetype>::max() / 4) {
        result.errorMessage = QStringLiteral("Slice frame byte count overflows.");
        return result;
    }
    const qsizetype expectedValueBytes = valueCount * 4;
    while (!file.atEnd()) {
        if (result.frames.size() >= limits.maximumFrames) {
            result.warnings.append(QStringLiteral(
                "Frame safety limit reached; remaining frames were not loaded."));
            break;
        }
        if (valueCount > 0 &&
            result.frames.size() >= limits.maximumTotalValues / valueCount) {
            result.warnings.append(QStringLiteral(
                "Total decoded-value safety limit reached; remaining frames were not loaded."));
            break;
        }
        const RecordStatus timeStatus = readRecord(file, result.littleEndian,
                                                   limits.maximumRecordBytes,
                                                   record, recordError);
        if (timeStatus == RecordStatus::EndOfFile) break;
        if (timeStatus != RecordStatus::Ok || record.size() != 4) {
            result.errorMessage = timeStatus == RecordStatus::Ok
                ? QStringLiteral("Frame %1 time record has %2 bytes; expected 4.")
                      .arg(result.frames.size()).arg(record.size())
                : QStringLiteral("Frame %1 time record: %2")
                      .arg(result.frames.size()).arg(recordError);
            result.frames.clear();
            return result;
        }
        FdsSliceFrame frame;
        {
            QDataStream stream(&record, QIODevice::ReadOnly);
            configureStream(stream, result.littleEndian);
            stream >> frame.time;
            if (stream.status() != QDataStream::Ok || !std::isfinite(frame.time)) {
                result.errorMessage = QStringLiteral("Frame time is not a finite float.");
                result.frames.clear();
                return result;
            }
        }
        const RecordStatus valuesStatus = readRecord(file, result.littleEndian,
                                                     limits.maximumRecordBytes,
                                                     record, recordError);
        if (valuesStatus != RecordStatus::Ok || record.size() != expectedValueBytes) {
            result.errorMessage = valuesStatus == RecordStatus::Ok
                ? QStringLiteral("Frame %1 has %2 value bytes; expected %3.")
                      .arg(result.frames.size()).arg(record.size()).arg(expectedValueBytes)
                : QStringLiteral("Frame %1 values: %2")
                      .arg(result.frames.size()).arg(recordError);
            result.frames.clear();
            return result;
        }
        frame.values.resize(valueCount);
        QDataStream stream(&record, QIODevice::ReadOnly);
        configureStream(stream, result.littleEndian);
        frame.minimum = std::numeric_limits<float>::max();
        frame.maximum = std::numeric_limits<float>::lowest();
        for (float& value : frame.values) {
            stream >> value;
            if (!std::isfinite(value)) {
                result.errorMessage = QStringLiteral(
                    "Frame %1 contains a non-finite field value.")
                                          .arg(result.frames.size());
                result.frames.clear();
                return result;
            }
            frame.minimum = std::min(frame.minimum, value);
            frame.maximum = std::max(frame.maximum, value);
        }
        if (stream.status() != QDataStream::Ok) {
            result.errorMessage = QStringLiteral("Frame values cannot be decoded.");
            result.frames.clear();
            return result;
        }
        if (!result.frames.isEmpty() &&
            frame.time < result.frames.constLast().time) {
            result.warnings.append(QStringLiteral(
                "Frame times are not monotonically increasing."));
        }
        result.frames.append(std::move(frame));
    }
    if (result.frames.isEmpty()) {
        result.errorMessage = QStringLiteral("FDS slice file contains no complete frames.");
    }
    return result;
}
