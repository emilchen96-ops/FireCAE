#include "ui/NativeResultViewerWidget.h"

#include "core/FcDocument.h"
#include "core/FcObject.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsImporter.h"
#include "fds/FdsScene.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "results/FcResultFile.h"
#include "results/FdsSliceImageRenderer.h"
#include "results/FdsSliceReader.h"
#include "results/SmokeviewFrameRenderer.h"
#include "ui/UiLanguage.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
QString u(const char* text)
{
    return UiLanguageManager::text(QString::fromUtf8(text));
}

QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString sanitizedFileKey(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                  QStringLiteral("_"));
    return value.isEmpty() ? QStringLiteral("field") : value;
}

const QColor kSeriesColors[] = {
    QColor(0, 114, 178), QColor(213, 94, 0), QColor(0, 158, 115),
    QColor(204, 121, 167), QColor(230, 159, 0), QColor(86, 180, 233),
    QColor(240, 228, 66), QColor(0, 0, 0)};

QString nativeCapability(FcResultFileType type)
{
    switch (type) {
    case FcResultFileType::Devices:
    case FcResultFileType::DeviceControlLog:
    case FcResultFileType::Controls:
    case FcResultFileType::HeatReleaseRate:
    case FcResultFileType::Hvac:
    case FcResultFileType::PressureIterations:
    case FcResultFileType::Steps:
    case FcResultFileType::Cpu:
    case FcResultFileType::Csv:
        return u("Native time-series chart available.");
    case FcResultFileType::Smoke3D:
    case FcResultFileType::Slice:
    case FcResultFileType::Boundary:
    case FcResultFileType::Particle:
    case FcResultFileType::Isosurface:
    case FcResultFileType::Plot3D:
        return u("The result is indexed, but this binary field currently opens in Smokeview for rendering.");
    default:
        return u("Select a CSV result to draw native charts.");
    }
}

QString resultCategory(const FdsResultFileInfo& file)
{
    if (file.type == FcResultFileType::Smoke3D) return QStringLiteral("3D Smoke/Fire");
    if (file.type == FcResultFileType::Slice)
        return file.vectorField ? QStringLiteral("Vector Slice") : QStringLiteral("Slice");
    if (file.type == FcResultFileType::Boundary) return QStringLiteral("Boundary");
    if (file.type == FcResultFileType::Isosurface) return QStringLiteral("Isosurface");
    if (file.type == FcResultFileType::Particle) return QStringLiteral("Particles");
    if (file.type == FcResultFileType::Plot3D) return QStringLiteral("Plot3D");
    if (file.type == FcResultFileType::Devices ||
        file.type == FcResultFileType::DeviceControlLog ||
        file.type == FcResultFileType::Controls)
        return QStringLiteral("Devices");
    if (file.type == FcResultFileType::Hvac) return QStringLiteral("HVAC");
    return QStringLiteral("CSV");
}

QString inferredComparisonLabel(const FdsResultScanResult& scan)
{
    const QString path = QDir::fromNativeSeparators(scan.resultDirectory).toLower();
    QString source = QStringLiteral("Native FDS");
    if (path.contains(QStringLiteral("pyrosim"))) source = QStringLiteral("PyroSim");
    else if (path.contains(QStringLiteral("firecae"))) source = QStringLiteral("FireCAE");
    return QStringLiteral("%1: %2").arg(source, scan.caseName);
}
}

class ResultPlotCanvas final : public QWidget
{
public:
    explicit ResultPlotCanvas(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("NativeResultPlot"));
        setMinimumSize(520, 320);
        setMouseTracking(true);
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Base);
    }

    void setData(const FdsCsvData* data, const QVector<int>& series)
    {
        m_data = data;
        m_series = series;
        m_zoom = 1.0;
        if (m_data && !m_data->times.isEmpty())
            m_viewCenter = (m_data->startTime() + m_data->endTime()) * 0.5;
        update();
    }

    void setOverlays(const QVector<FdsCsvData>* overlays,
                     const QStringList& labels)
    {
        m_overlays = overlays;
        m_overlayLabels = labels;
        update();
    }

    void setTimeRange(double minimum, double maximum, bool enabled)
    {
        m_rangeEnabled = enabled && maximum > minimum;
        m_rangeMinimum = minimum;
        m_rangeMaximum = maximum;
        m_zoom = 1.0;
        m_viewCenter = (minimum + maximum) * 0.5;
        update();
    }

    void setCurrentTime(double value) { m_currentTime = value; update(); }
    void setDualAxis(bool enabled) { m_dualAxis = enabled; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().base());
        if (!m_data || !m_data->success() || m_data->times.isEmpty() || m_series.isEmpty()) {
            painter.setPen(palette().text().color());
            painter.drawText(rect(), Qt::AlignCenter,
                             u("Select a CSV result and one or more quantities."));
            return;
        }

        const QRectF plotRect(68.0, 34.0, std::max(20, width() - 138),
                              std::max(20, height() - 94));
        double fullMinX = m_rangeEnabled ? m_rangeMinimum : m_data->startTime();
        double fullMaxX = m_rangeEnabled ? m_rangeMaximum : m_data->endTime();
        if (fullMaxX <= fullMinX) fullMaxX = fullMinX + 1.0;
        const double visibleWidth = (fullMaxX - fullMinX) / m_zoom;
        const double center = std::clamp(m_viewCenter, fullMinX, fullMaxX);
        double minX = std::max(fullMinX, center - visibleWidth / 2.0);
        double maxX = std::min(fullMaxX, minX + visibleWidth);
        minX = std::max(fullMinX, maxX - visibleWidth);

        const QString firstUnit = m_data->series.at(m_series.constFirst()).unit;
        auto axisFor = [&](int index) {
            return m_dualAxis && m_data->series.at(index).unit != firstUnit ? 1 : 0;
        };
        double minY[2] = {std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::max()};
        double maxY[2] = {std::numeric_limits<double>::lowest(),
                          std::numeric_limits<double>::lowest()};
        for (int index : m_series) {
            const int axis = axisFor(index);
            const FdsCsvSeries& series = m_data->series.at(index);
            for (qsizetype row = 0; row < m_data->times.size(); ++row) {
                const double time = m_data->times.at(row);
                if (time < minX || time > maxX) continue;
                minY[axis] = std::min(minY[axis], series.values.at(row));
                maxY[axis] = std::max(maxY[axis], series.values.at(row));
            }
        }
        if (m_overlays) {
            for (const FdsCsvData& overlay : *m_overlays) {
                for (int primaryIndex : m_series) {
                    const FdsCsvSeries& primary = m_data->series.at(primaryIndex);
                    const auto match = std::find_if(
                        overlay.series.cbegin(), overlay.series.cend(),
                        [&primary](const FdsCsvSeries& candidate) {
                            return candidate.name.compare(primary.name, Qt::CaseInsensitive) == 0 &&
                                   candidate.unit.compare(primary.unit, Qt::CaseInsensitive) == 0;
                        });
                    if (match == overlay.series.cend()) continue;
                    const int axis = axisFor(primaryIndex);
                    for (qsizetype row = 0; row < overlay.times.size(); ++row) {
                        if (overlay.times.at(row) < minX || overlay.times.at(row) > maxX) continue;
                        minY[axis] = std::min(minY[axis], match->values.at(row));
                        maxY[axis] = std::max(maxY[axis], match->values.at(row));
                    }
                }
            }
        }
        for (int axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(minY[axis]) || !std::isfinite(maxY[axis])) {
                minY[axis] = 0.0; maxY[axis] = 1.0;
            }
            if (maxY[axis] <= minY[axis]) {
                const double padding = std::max(1.0, std::abs(maxY[axis]) * 0.1);
                minY[axis] -= padding; maxY[axis] += padding;
            } else {
                const double padding = (maxY[axis] - minY[axis]) * 0.05;
                minY[axis] -= padding; maxY[axis] += padding;
            }
        }

        painter.setPen(QPen(QColor(218, 222, 228), 1));
        for (int step = 0; step <= 5; ++step) {
            const double fraction = step / 5.0;
            const double x = plotRect.left() + fraction * plotRect.width();
            const double y = plotRect.bottom() - fraction * plotRect.height();
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }
        painter.setPen(QPen(palette().text().color(), 1));
        painter.drawRect(plotRect);
        for (int step = 0; step <= 5; ++step) {
            const double fraction = step / 5.0;
            const double xValue = minX + fraction * (maxX - minX);
            const double leftValue = minY[0] + fraction * (maxY[0] - minY[0]);
            const double y = plotRect.bottom() - fraction * plotRect.height();
            painter.drawText(QRectF(plotRect.left() - 64, y - 9, 58, 18),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(leftValue, 'g', 4));
            painter.drawText(QRectF(plotRect.left() + fraction * plotRect.width() - 35,
                                    plotRect.bottom() + 6, 70, 18),
                             Qt::AlignCenter, QString::number(xValue, 'g', 4));
            if (m_dualAxis) {
                const double rightValue = minY[1] + fraction * (maxY[1] - minY[1]);
                painter.drawText(QRectF(plotRect.right() + 6, y - 9, 60, 18),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QString::number(rightValue, 'g', 4));
            }
        }
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 29,
                                plotRect.width(), 20), Qt::AlignCenter,
                         QStringLiteral("%1 [%2]").arg(m_data->timeName,
                                                       m_data->timeUnit));

        painter.save();
        painter.setClipRect(plotRect.adjusted(-1, -1, 1, 1));
        for (qsizetype curve = 0; curve < m_series.size(); ++curve) {
            const int seriesIndex = m_series.at(curve);
            const int axis = axisFor(seriesIndex);
            const auto& series = m_data->series.at(seriesIndex);
            QPainterPath path;
            bool started = false;
            for (qsizetype row = 0; row < m_data->times.size(); ++row) {
                const double time = m_data->times.at(row);
                if (time < minX || time > maxX) continue;
                const double x = plotRect.left() +
                                 (time - minX) / (maxX - minX) * plotRect.width();
                const double y = plotRect.bottom() -
                    (series.values.at(row) - minY[axis]) /
                    (maxY[axis] - minY[axis]) * plotRect.height();
                if (!started) { path.moveTo(x, y); started = true; }
                else path.lineTo(x, y);
            }
            painter.setPen(QPen(kSeriesColors[curve % std::size(kSeriesColors)], 2));
            painter.drawPath(path);
        }
        if (m_overlays) {
            for (qsizetype overlayIndex = 0; overlayIndex < m_overlays->size(); ++overlayIndex) {
                const FdsCsvData& overlay = m_overlays->at(overlayIndex);
                for (qsizetype curve = 0; curve < m_series.size(); ++curve) {
                    const int primaryIndex = m_series.at(curve);
                    const FdsCsvSeries& primary = m_data->series.at(primaryIndex);
                    const auto match = std::find_if(
                        overlay.series.cbegin(), overlay.series.cend(),
                        [&primary](const FdsCsvSeries& candidate) {
                            return candidate.name.compare(primary.name, Qt::CaseInsensitive) == 0 &&
                                   candidate.unit.compare(primary.unit, Qt::CaseInsensitive) == 0;
                        });
                    if (match == overlay.series.cend()) continue;
                    const int axis = axisFor(primaryIndex);
                    QPainterPath path;
                    bool started = false;
                    for (qsizetype row = 0; row < overlay.times.size(); ++row) {
                        const double time = overlay.times.at(row);
                        if (time < minX || time > maxX) continue;
                        const double x = plotRect.left() +
                            (time - minX) / (maxX - minX) * plotRect.width();
                        const double y = plotRect.bottom() -
                            (match->values.at(row) - minY[axis]) /
                            (maxY[axis] - minY[axis]) * plotRect.height();
                        if (!started) { path.moveTo(x, y); started = true; }
                        else path.lineTo(x, y);
                    }
                    QColor color = kSeriesColors[curve % std::size(kSeriesColors)];
                    color = color.lighter(115 + static_cast<int>(overlayIndex) * 20);
                    painter.setPen(QPen(color, 2, Qt::DashLine));
                    painter.drawPath(path);
                }
            }
        }
        if (m_currentTime >= minX && m_currentTime <= maxX) {
            const double x = plotRect.left() +
                (m_currentTime - minX) / (maxX - minX) * plotRect.width();
            painter.setPen(QPen(QColor(190, 20, 20), 1, Qt::DashLine));
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        painter.restore();

        int legendX = static_cast<int>(plotRect.left());
        for (qsizetype curve = 0; curve < m_series.size(); ++curve) {
            const auto& series = m_data->series.at(m_series.at(curve));
            const QString label = series.unit.isEmpty()
                ? series.name : QStringLiteral("%1 [%2]").arg(series.name, series.unit);
            painter.setPen(QPen(kSeriesColors[curve % std::size(kSeriesColors)], 3));
            painter.drawLine(legendX, 18, legendX + 18, 18);
            painter.setPen(palette().text().color());
            painter.drawText(legendX + 23, 10, 190, 18, Qt::AlignLeft, label);
            legendX += std::min(220, 45 + painter.fontMetrics().horizontalAdvance(label));
        }
        if (m_overlays && !m_overlays->isEmpty()) {
            painter.setPen(palette().text().color());
            const QString labels = m_overlayLabels.isEmpty()
                ? u("Comparison cases: dashed curves")
                : QStringLiteral("%1: %2")
                      .arg(u("Dashed comparison cases"), m_overlayLabels.join(QStringLiteral(", ")));
            painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 48,
                                    plotRect.width(), 18), Qt::AlignLeft, labels);
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!m_data || m_data->times.isEmpty()) return;
        m_zoom = std::clamp(m_zoom * (event->angleDelta().y() > 0 ? 1.25 : 0.8),
                            1.0, 100.0);
        const QRectF plotRect(68.0, 34.0, std::max(20, width() - 138),
                              std::max(20, height() - 94));
        const double fraction = std::clamp((event->position().x() - plotRect.left()) /
                                           plotRect.width(), 0.0, 1.0);
        m_viewCenter = m_data->startTime() + fraction *
                       (m_data->endTime() - m_data->startTime());
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_data || m_data->times.isEmpty() || m_series.isEmpty()) return;
        const QRectF plotRect(68.0, 34.0, std::max(20, width() - 138),
                              std::max(20, height() - 94));
        if (!plotRect.contains(event->position())) return;
        const double fraction = (event->position().x() - plotRect.left()) / plotRect.width();
        const double fullWidth = m_data->endTime() - m_data->startTime();
        const double visibleWidth = fullWidth / m_zoom;
        const double minX = std::max(m_data->startTime(), m_viewCenter - visibleWidth / 2.0);
        const double time = minX + fraction * visibleWidth;
        QStringList readings{QStringLiteral("%1 = %2 %3")
                                 .arg(m_data->timeName)
                                 .arg(time, 0, 'g', 5)
                                 .arg(m_data->timeUnit)};
        for (int seriesIndex : m_series) {
            bool ok = false;
            const double value = m_data->interpolatedValue(seriesIndex, time, &ok);
            if (ok) readings.append(QStringLiteral("%1 = %2 %3")
                                        .arg(m_data->series.at(seriesIndex).name)
                                        .arg(value, 0, 'g', 6)
                                        .arg(m_data->series.at(seriesIndex).unit));
        }
        QToolTip::showText(event->globalPosition().toPoint(), readings.join(QLatin1Char('\n')), this);
    }

private:
    const FdsCsvData* m_data = nullptr;
    const QVector<FdsCsvData>* m_overlays = nullptr;
    QStringList m_overlayLabels;
    QVector<int> m_series;
    double m_currentTime = 0.0;
    double m_zoom = 1.0;
    double m_viewCenter = 0.0;
    double m_rangeMinimum = 0.0;
    double m_rangeMaximum = 0.0;
    bool m_rangeEnabled = false;
    bool m_dualAxis = false;
};

class ResultFieldImageLabel final : public QLabel
{
public:
    explicit ResultFieldImageLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(520, 320);
        setMouseTracking(true);
        setStyleSheet(QStringLiteral("background:#20242a;color:#e8e8e8;"));
        setProperty("zoomFactor", m_zoom);
    }

    void setSourcePixmap(const QPixmap& pixmap)
    {
        m_source = pixmap;
        QLabel::setPixmap(pixmap);
        setText(QString());
        m_zoom = 1.0;
        setProperty("zoomFactor", m_zoom);
        m_pan = {};
        update();
    }

    void clearFrame(const QString& message)
    {
        m_source = {};
        QLabel::setPixmap(QPixmap());
        setText(message);
        m_zoom = 1.0;
        setProperty("zoomFactor", m_zoom);
        m_pan = {};
        update();
    }

    double zoomFactor() const { return m_zoom; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (m_source.isNull()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.fillRect(rect(), QColor(32, 36, 42));
        const double fit = std::min(
            std::max(1.0, width() - 20.0) / m_source.width(),
            std::max(1.0, height() - 20.0) / m_source.height());
        const QSizeF size(m_source.width() * fit * m_zoom,
                          m_source.height() * fit * m_zoom);
        const QPointF topLeft((width() - size.width()) * 0.5 + m_pan.x(),
                              (height() - size.height()) * 0.5 + m_pan.y());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 4.0);
        painter.drawPixmap(QRectF(topLeft, size), m_source,
                           QRectF(m_source.rect()));
        painter.setPen(QColor(230, 234, 240));
        painter.drawText(QRect(10, 8, width() - 20, 24), Qt::AlignRight,
                         QStringLiteral("%1%").arg(qRound(m_zoom * 100.0)));
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (m_source.isNull()) return QLabel::wheelEvent(event);
        const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        m_zoom = std::clamp(m_zoom * factor, 0.25, 20.0);
        setProperty("zoomFactor", m_zoom);
        if (m_zoom <= 1.0) m_pan = {};
        event->accept();
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!m_source.isNull() && event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStart = event->position();
            m_panStart = m_pan;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging) {
            m_pan = m_panStart + event->position() - m_dragStart;
            event->accept();
            update();
            return;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false;
            unsetCursor();
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (!m_source.isNull() && event->button() == Qt::LeftButton) {
            m_zoom = 1.0;
            setProperty("zoomFactor", m_zoom);
            m_pan = {};
            event->accept();
            update();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }

private:
    QPixmap m_source;
    double m_zoom = 1.0;
    QPointF m_pan;
    QPointF m_dragStart;
    QPointF m_panStart;
    bool m_dragging = false;
};

class ResultColorBarWidget final : public QWidget
{
public:
    explicit ResultColorBarWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(28);
    }
    void setSettings(double minimum, double maximum, double opacity, int map)
    {
        m_minimum = minimum; m_maximum = maximum; m_opacity = opacity; m_map = map;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QRect gradientRect = rect().adjusted(45, 4, -45, -7);
        QLinearGradient gradient(gradientRect.topLeft(), gradientRect.topRight());
        if (m_map == 1) {
            gradient.setColorAt(0.0, QColor(68, 1, 84));
            gradient.setColorAt(0.5, QColor(33, 145, 140));
            gradient.setColorAt(1.0, QColor(253, 231, 37));
        } else if (m_map == 2) {
            gradient.setColorAt(0.0, QColor(0, 0, 0));
            gradient.setColorAt(0.45, QColor(210, 0, 0));
            gradient.setColorAt(0.75, QColor(255, 190, 0));
            gradient.setColorAt(1.0, QColor(255, 255, 255));
        } else {
            gradient.setColorAt(0.0, QColor(0, 45, 180));
            gradient.setColorAt(0.5, QColor(40, 220, 120));
            gradient.setColorAt(0.75, QColor(255, 220, 30));
            gradient.setColorAt(1.0, QColor(210, 20, 20));
        }
        painter.setOpacity(m_opacity);
        painter.fillRect(gradientRect, gradient);
        painter.setOpacity(1.0);
        painter.setPen(palette().text().color());
        painter.drawRect(gradientRect);
        painter.drawText(QRect(0, 0, 42, height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(m_minimum, 'g', 4));
        painter.drawText(QRect(width() - 42, 0, 42, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(m_maximum, 'g', 4));
    }
private:
    double m_minimum = 0.0;
    double m_maximum = 1.0;
    double m_opacity = 1.0;
    int m_map = 0;
};

struct ResultGeometryBox
{
    FcFdsBounds bounds;
    QColor color{170, 176, 186};
    bool wireframe = false;
    QString label;
};

class ResultGeometryCanvas final : public QWidget
{
public:
    explicit ResultGeometryCanvas(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("NativeResultGeometryCanvas"));
        setMinimumSize(520, 320);
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Dark);
    }

    void setGeometry(const QVector<ResultGeometryBox>& boxes,
                     const QString& provenance, int displayMode)
    {
        m_boxes = boxes;
        m_provenance = provenance;
        m_displayMode = displayMode;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(72, 78, 90));
        painter.setPen(QColor(238, 240, 244));
        painter.drawText(QRect(12, 8, width() - 24, 24),
                         Qt::AlignLeft | Qt::AlignVCenter, m_provenance);
        if (m_boxes.isEmpty()) {
            painter.drawText(rect().adjusted(20, 40, -20, -20), Qt::AlignCenter,
                             u("No geometry is available for this display mode."));
            return;
        }

        const auto project = [](double x, double y, double z) {
            return QPointF((x - y) * 0.82, (x + y) * 0.36 - z * 0.92);
        };
        QRectF world;
        bool first = true;
        for (const ResultGeometryBox& box : m_boxes) {
            for (int xi = 0; xi < 2; ++xi)
                for (int yi = 0; yi < 2; ++yi)
                    for (int zi = 0; zi < 2; ++zi) {
                        const QPointF point = project(
                            xi ? box.bounds.xMax : box.bounds.xMin,
                            yi ? box.bounds.yMax : box.bounds.yMin,
                            zi ? box.bounds.zMax : box.bounds.zMin);
                        if (first) { world = QRectF(point, QSizeF(0, 0)); first = false; }
                        else world |= QRectF(point, QSizeF(0, 0));
                    }
        }
        const QRectF target = rect().adjusted(45, 46, -45, -38);
        const double scale = std::min(target.width() / std::max(1.0, world.width()),
                                      target.height() / std::max(1.0, world.height()));
        const QPointF offset = target.center() - world.center() * scale;
        const auto screen = [&](double x, double y, double z) {
            return project(x, y, z) * scale + offset;
        };

        QVector<int> order;
        order.reserve(m_boxes.size());
        for (int i = 0; i < m_boxes.size(); ++i) order.append(i);
        std::sort(order.begin(), order.end(), [this](int left, int right) {
            const FcFdsBounds& a = m_boxes.at(left).bounds;
            const FcFdsBounds& b = m_boxes.at(right).bounds;
            return a.xMax + a.yMax + a.zMax < b.xMax + b.yMax + b.zMax;
        });
        for (int index : order) {
            const ResultGeometryBox& box = m_boxes.at(index);
            const QPointF p000 = screen(box.bounds.xMin, box.bounds.yMin, box.bounds.zMin);
            const QPointF p100 = screen(box.bounds.xMax, box.bounds.yMin, box.bounds.zMin);
            const QPointF p010 = screen(box.bounds.xMin, box.bounds.yMax, box.bounds.zMin);
            const QPointF p001 = screen(box.bounds.xMin, box.bounds.yMin, box.bounds.zMax);
            const QPointF p101 = screen(box.bounds.xMax, box.bounds.yMin, box.bounds.zMax);
            const QPointF p011 = screen(box.bounds.xMin, box.bounds.yMax, box.bounds.zMax);
            const QPointF p111 = screen(box.bounds.xMax, box.bounds.yMax, box.bounds.zMax);
            QColor base = box.color;
            if (m_displayMode == 5) base.setAlpha(50);
            else if (m_displayMode == 6) base.setAlpha(105);
            else base.setAlpha(205);
            const bool wire = box.wireframe || m_displayMode == 4;
            const QVector<QPolygonF> faces = {
                QPolygonF{p001, p101, p111, p011},
                QPolygonF{p000, p100, p101, p001},
                QPolygonF{p000, p010, p011, p001}};
            const QVector<int> shades{115, 100, 88};
            for (int face = 0; face < faces.size(); ++face) {
                painter.setBrush(wire ? Qt::NoBrush : QBrush(base.lighter(shades.at(face))));
                painter.setPen(QPen(base.darker(170), wire ? 1.6 : 0.8));
                painter.drawPolygon(faces.at(face));
            }
        }
        painter.setPen(QColor(230, 232, 236));
        painter.drawText(QRect(12, height() - 28, width() - 24, 20),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1: %2").arg(u("Objects")).arg(m_boxes.size()));
    }

private:
    QVector<ResultGeometryBox> m_boxes;
    QString m_provenance;
    int m_displayMode = 1;
};

NativeResultViewerWidget::NativeResultViewerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("NativeResultViewerWidget"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto* toolbar = new QHBoxLayout;
    m_caseLabel = new QLabel(u("No result case is open"), this);
    m_caseLabel->setObjectName(QStringLiteral("NativeResultCaseLabel"));
    m_caseLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(m_caseLabel);
    auto* geometrySourceLabel = new QLabel(u("Geometry source:"), this);
    geometrySourceLabel->setToolTip(u(
        "Choose whether geometry comes from the editable model, FDS input, mesh snapping, or CAD/IFC reference"));
    toolbar->addWidget(geometrySourceLabel);
    m_sceneCombo = new QComboBox(this);
    m_sceneCombo->setObjectName(QStringLiteral("ResultSceneModeCombo"));
    m_sceneCombo->addItems({u("FireCAE Editable Geometry"),
                            u("FDS Input Geometry"),
                            u("FDS Mesh-Snapped Preview"),
                            u("CAD/IFC Reference Geometry")});
    m_sceneCombo->setToolTip(u(
        "Editable geometry is the FireCAE model; FDS input geometry is exported intent; mesh-snapped preview shows the geometry solved on the FDS grid"));
    toolbar->addWidget(m_sceneCombo);
    toolbar->addWidget(new QLabel(u("Display:"), this));
    m_displayCombo = new QComboBox(this);
    m_displayCombo->setObjectName(QStringLiteral("ResultDisplayModeCombo"));
    m_displayCombo->addItems({u("Solid"), u("Solid with Edges"), u("Realistic"),
                              u("Realistic with Edges"), u("Wireframe"),
                              u("X-Ray"), u("Translucent")});
    toolbar->addWidget(m_displayCombo);
    m_geometryVisibleCheck = new QCheckBox(u("Show geometry"), this);
    m_geometryVisibleCheck->setObjectName(QStringLiteral("NativeResultGeometryVisibleCheck"));
    m_geometryVisibleCheck->setChecked(true);
    toolbar->addWidget(m_geometryVisibleCheck);
    m_smokeviewButton = new QPushButton(u("Open in Smokeview"), this);
    m_smokeviewButton->setObjectName(QStringLiteral("NativeResultSmokeviewButton"));
    toolbar->addWidget(m_smokeviewButton);
    m_returnButton = new QPushButton(u("Return to Model"), this);
    toolbar->addWidget(m_returnButton);
    root->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* sources = new QWidget(splitter);
    auto* sourceLayout = new QVBoxLayout(sources);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->addWidget(new QLabel(u("Result objects"), sources));
    m_resultTree = new QTreeWidget(sources);
    m_resultTree->setObjectName(QStringLiteral("NativeResultObjectTree"));
    m_resultTree->setHeaderHidden(true);
    m_resultTree->setAlternatingRowColors(true);
    sourceLayout->addWidget(m_resultTree, 3);
    // Kept as a hidden compatibility index for older automation. Production
    // interaction is exclusively through the semantic result tree above.
    m_fileList = new QListWidget(sources);
    m_fileList->setObjectName(QStringLiteral("NativeResultFileList"));
    m_fileList->setVisible(false);
    sourceLayout->addWidget(new QLabel(u("Quantities"), sources));
    m_seriesList = new QListWidget(sources);
    m_seriesList->setObjectName(QStringLiteral("NativeResultSeriesList"));
    m_seriesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sourceLayout->addWidget(m_seriesList, 2);
    m_dualAxisCheck = new QCheckBox(u("Use second Y axis for different units"), sources);
    m_dualAxisCheck->setObjectName(QStringLiteral("NativeResultDualAxisCheck"));
    sourceLayout->addWidget(m_dualAxisCheck);
    m_formatStatus = new QLabel(u("Open an FDS result case."), sources);
    m_formatStatus->setWordWrap(true);
    sourceLayout->addWidget(m_formatStatus);
    auto* renderButtons = new QHBoxLayout;
    m_renderFieldButton = new QPushButton(u("Build frame cache"), sources);
    m_renderFieldButton->setObjectName(QStringLiteral("NativeResultRenderFieldButton"));
    m_cancelRenderButton = new QPushButton(u("Cancel"), sources);
    m_cancelRenderButton->setObjectName(QStringLiteral("NativeResultCancelRenderButton"));
    m_cancelRenderButton->setEnabled(false);
    renderButtons->addWidget(m_renderFieldButton);
    renderButtons->addWidget(m_cancelRenderButton);
    sourceLayout->addLayout(renderButtons);

    auto* center = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    m_resultDisplayStack = new QStackedWidget(center);
    m_resultDisplayStack->setObjectName(QStringLiteral("NativeResultDisplayStack"));
    m_plot = new ResultPlotCanvas(m_resultDisplayStack);
    m_geometryCanvas = new ResultGeometryCanvas(m_resultDisplayStack);
    m_renderedFrameLabel = new ResultFieldImageLabel(m_resultDisplayStack);
    m_renderedFrameLabel->setObjectName(QStringLiteral("NativeResultRenderedFrame"));
    m_renderedFrameLabel->clearFrame(
        u("Build a frame cache to view this binary field here."));
    m_resultDisplayStack->addWidget(m_plot);
    m_resultDisplayStack->addWidget(m_renderedFrameLabel);
    m_resultDisplayStack->addWidget(m_geometryCanvas);
    centerLayout->addWidget(m_resultDisplayStack, 1);
    m_statisticsTable = new QTableWidget(0, 7, center);
    m_statisticsTable->setObjectName(QStringLiteral("NativeResultStatisticsTable"));
    m_statisticsTable->setHorizontalHeaderLabels(
        {u("Quantity"), u("Unit"), u("Samples"), u("Minimum"),
         u("Maximum"), u("Mean")});
    m_statisticsTable->setHorizontalHeaderItem(6, new QTableWidgetItem(u("Absolute Peak")));
    m_statisticsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_statisticsTable->setMaximumHeight(150);
    centerLayout->addWidget(m_statisticsTable);

    auto* colorGroup = new QGroupBox(u("Color scale"), splitter);
    auto* colorLayout = new QGridLayout(colorGroup);
    m_autoRangeCheck = new QCheckBox(u("Automatic range"), colorGroup);
    m_autoRangeCheck->setChecked(true);
    m_logRangeCheck = new QCheckBox(u("Logarithmic"), colorGroup);
    m_globalColorCheck = new QCheckBox(u("Global scale"), colorGroup);
    m_colorMapCombo = new QComboBox(colorGroup);
    m_colorMapCombo->addItems({u("Rainbow"), QStringLiteral("Viridis"), u("Fire")});
    m_colorMinimum = new QDoubleSpinBox(colorGroup);
    m_colorMaximum = new QDoubleSpinBox(colorGroup);
    m_colorOpacity = new QDoubleSpinBox(colorGroup);
    for (QDoubleSpinBox* spin : {m_colorMinimum, m_colorMaximum}) {
        spin->setRange(-1.0e12, 1.0e12); spin->setDecimals(6);
    }
    m_colorMaximum->setValue(1.0);
    m_colorOpacity->setRange(0.0, 1.0); m_colorOpacity->setSingleStep(0.05);
    m_colorOpacity->setValue(1.0);
    colorLayout->addWidget(m_autoRangeCheck, 0, 0, 1, 2);
    colorLayout->addWidget(m_logRangeCheck, 1, 0, 1, 2);
    colorLayout->addWidget(m_globalColorCheck, 2, 0, 1, 2);
    colorLayout->addWidget(new QLabel(u("Color map"), colorGroup), 3, 0);
    colorLayout->addWidget(m_colorMapCombo, 3, 1);
    colorLayout->addWidget(new QLabel(u("Minimum"), colorGroup), 4, 0);
    colorLayout->addWidget(m_colorMinimum, 4, 1);
    colorLayout->addWidget(new QLabel(u("Maximum"), colorGroup), 5, 0);
    colorLayout->addWidget(m_colorMaximum, 5, 1);
    colorLayout->addWidget(new QLabel(u("Opacity"), colorGroup), 6, 0);
    colorLayout->addWidget(m_colorOpacity, 6, 1);
    m_colorBar = new ResultColorBarWidget(colorGroup);
    colorLayout->addWidget(m_colorBar, 7, 0, 1, 2);
    colorLayout->setRowStretch(8, 1);
    splitter->addWidget(sources); splitter->addWidget(center); splitter->addWidget(colorGroup);
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({230, 850, 220});
    root->addWidget(splitter, 1);

    auto* timeline = new QHBoxLayout;
    m_previousButton = new QPushButton(this);
    m_previousButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    m_playButton = new QPushButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_pauseButton = new QPushButton(this);
    m_pauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_stopButton = new QPushButton(this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_nextButton = new QPushButton(this);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    m_previousButton->setObjectName(QStringLiteral("NativeResultPreviousButton"));
    m_playButton->setObjectName(QStringLiteral("NativeResultPlayButton"));
    m_pauseButton->setObjectName(QStringLiteral("NativeResultPauseButton"));
    m_stopButton->setObjectName(QStringLiteral("NativeResultStopButton"));
    m_nextButton->setObjectName(QStringLiteral("NativeResultNextButton"));
    for (QPushButton* button : {m_previousButton, m_playButton, m_pauseButton,
                                m_stopButton, m_nextButton}) timeline->addWidget(button);
    m_timeline = new QSlider(Qt::Horizontal, this);
    m_timeline->setObjectName(QStringLiteral("NativeResultTimeline"));
    timeline->addWidget(m_timeline, 1);
    m_timeLabel = new QLabel(QStringLiteral("0 / 0 s"), this);
    m_timeLabel->setMinimumWidth(120); timeline->addWidget(m_timeLabel);
    m_timeSpin = new QDoubleSpinBox(this);
    m_timeSpin->setObjectName(QStringLiteral("NativeResultTimeSpin"));
    m_timeSpin->setDecimals(6);
    m_timeSpin->setKeyboardTracking(false);
    m_timeSpin->setMinimumWidth(105);
    timeline->addWidget(new QLabel(u("Jump to time"), this));
    timeline->addWidget(m_timeSpin);
    timeline->addWidget(new QLabel(u("Speed"), this));
    m_speedSpin = new QDoubleSpinBox(this);
    m_speedSpin->setRange(0.1, 16.0); m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setSuffix(QStringLiteral("x")); m_speedSpin->setValue(1.0);
    timeline->addWidget(m_speedSpin);
    m_loopCheck = new QCheckBox(u("Loop"), this); m_loopCheck->setChecked(true);
    timeline->addWidget(m_loopCheck);
    m_autoRefreshCheck = new QCheckBox(u("Auto refresh"), this);
    m_autoRefreshCheck->setObjectName(QStringLiteral("NativeResultAutoRefreshCheck"));
    timeline->addWidget(m_autoRefreshCheck);
    m_refreshIntervalSpin = new QSpinBox(this);
    m_refreshIntervalSpin->setObjectName(QStringLiteral("NativeResultRefreshInterval"));
    m_refreshIntervalSpin->setRange(1, 60);
    m_refreshIntervalSpin->setValue(3);
    m_refreshIntervalSpin->setSuffix(QStringLiteral(" s"));
    timeline->addWidget(m_refreshIntervalSpin);
    m_cacheProgress = new QProgressBar(this);
    m_cacheProgress->setObjectName(QStringLiteral("NativeResultCacheProgress"));
    m_cacheProgress->setRange(0, 100); m_cacheProgress->setValue(0);
    m_cacheProgress->setFormat(u("Cache %p%")); m_cacheProgress->setMaximumWidth(120);
    timeline->addWidget(m_cacheProgress);
    auto* exportCsv = new QPushButton(u("Export CSV..."), this);
    auto* screenshot = new QPushButton(u("Save PNG..."), this);
    auto* sequence = new QPushButton(u("Export Frames..."), this);
    auto* video = new QPushButton(u("Export Video..."), this);
    exportCsv->setObjectName(QStringLiteral("NativeResultExportCsvButton"));
    screenshot->setObjectName(QStringLiteral("NativeResultScreenshotButton"));
    sequence->setObjectName(QStringLiteral("NativeResultExportFramesButton"));
    video->setObjectName(QStringLiteral("NativeResultExportVideoButton"));
    timeline->addWidget(exportCsv); timeline->addWidget(screenshot);
    timeline->addWidget(sequence); timeline->addWidget(video);
    root->addLayout(timeline);

    auto* analysisBar = new QHBoxLayout;
    analysisBar->addWidget(new QLabel(u("Chart time range"), this));
    m_rangeStartSpin = new QDoubleSpinBox(this);
    m_rangeEndSpin = new QDoubleSpinBox(this);
    m_rangeStartSpin->setObjectName(QStringLiteral("NativeResultRangeStart"));
    m_rangeEndSpin->setObjectName(QStringLiteral("NativeResultRangeEnd"));
    for (QDoubleSpinBox* spin : {m_rangeStartSpin, m_rangeEndSpin}) {
        spin->setRange(-1.0e12, 1.0e12);
        spin->setDecimals(6);
    }
    auto* applyRange = new QPushButton(u("Apply range"), this);
    applyRange->setObjectName(QStringLiteral("NativeResultApplyRangeButton"));
    auto* resetRange = new QPushButton(u("Full range"), this);
    m_refreshButton = new QPushButton(u("Refresh now"), this);
    m_refreshButton->setObjectName(QStringLiteral("NativeResultRefreshButton"));
    auto* addComparison = new QPushButton(u("Add comparison case..."), this);
    addComparison->setObjectName(QStringLiteral("NativeResultAddComparisonButton"));
    m_clearComparisonButton = new QPushButton(u("Clear comparisons"), this);
    m_clearComparisonButton->setEnabled(false);
    analysisBar->addWidget(m_rangeStartSpin);
    analysisBar->addWidget(m_rangeEndSpin);
    analysisBar->addWidget(applyRange);
    analysisBar->addWidget(resetRange);
    analysisBar->addStretch(1);
    analysisBar->addWidget(m_refreshButton);
    analysisBar->addWidget(addComparison);
    analysisBar->addWidget(m_clearComparisonButton);
    root->addLayout(analysisBar);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(100);
    connect(m_playTimer, &QTimer::timeout, this, [this]() {
        if (frameCount() == 0) return;
        int next = m_currentFrame + 1;
        if (next >= frameCount()) {
            if (!m_loopCheck->isChecked()) { pause(); return; }
            next = 0;
        }
        setCurrentFrame(next);
    });
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(m_refreshIntervalSpin->value() * 1000);
    connect(m_refreshTimer, &QTimer::timeout,
            this, &NativeResultViewerWidget::refreshCaseInteractive);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && hasOpenCase()) m_refreshTimer->start();
        else m_refreshTimer->stop();
    });
    connect(m_refreshIntervalSpin, &QSpinBox::valueChanged, this, [this](int seconds) {
        m_refreshTimer->setInterval(seconds * 1000);
    });
    connect(m_resultTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) {
                if (!current) return;
                const int row = current->data(0, Qt::UserRole + 6).toInt();
                if (row >= 0 && row < m_fileList->count()) {
                    m_fileList->setCurrentRow(row);
                } else if (current->data(0, Qt::UserRole + 7).toString() ==
                           QStringLiteral("geometry")) {
                    pause();
                    rebuildGeometryView();
                }
            });
    connect(m_fileList, &QListWidget::currentRowChanged, this,
            [this]() { loadSelectedFile(); });
    connect(m_seriesList, &QListWidget::itemSelectionChanged, this,
            [this]() { updateSelectedSeries(); });
    connect(m_timeline, &QSlider::valueChanged, this,
            [this](int frame) { setCurrentFrame(frame); });
    connect(m_timeSpin, &QDoubleSpinBox::valueChanged, this, [this](double time) {
        if (m_frameTimes.isEmpty()) return;
        const auto upper = std::lower_bound(m_frameTimes.cbegin(), m_frameTimes.cend(), time);
        int frame = upper == m_frameTimes.cend()
            ? static_cast<int>(m_frameTimes.size()) - 1
            : static_cast<int>(std::distance(m_frameTimes.cbegin(), upper));
        if (frame > 0 && std::abs(m_frameTimes.at(frame - 1) - time) <
                         std::abs(m_frameTimes.at(frame) - time)) --frame;
        setCurrentFrame(frame);
    });
    connect(m_previousButton, &QPushButton::clicked, this,
            [this]() { pause(); setCurrentFrame(std::max(0, m_currentFrame - 1)); });
    connect(m_nextButton, &QPushButton::clicked, this,
            [this]() { pause(); setCurrentFrame(std::min(frameCount() - 1, m_currentFrame + 1)); });
    connect(m_playButton, &QPushButton::clicked, this, &NativeResultViewerWidget::play);
    connect(m_pauseButton, &QPushButton::clicked, this, &NativeResultViewerWidget::pause);
    connect(m_stopButton, &QPushButton::clicked, this, &NativeResultViewerWidget::stop);
    connect(m_speedSpin, &QDoubleSpinBox::valueChanged, this, [this](double speed) {
        m_playTimer->setInterval(std::max(10, qRound(100.0 / speed)));
    });
    connect(m_dualAxisCheck, &QCheckBox::toggled, m_plot, &ResultPlotCanvas::setDualAxis);
    connect(exportCsv, &QPushButton::clicked, this, &NativeResultViewerWidget::exportCsvInteractive);
    connect(screenshot, &QPushButton::clicked, this, &NativeResultViewerWidget::saveScreenshotInteractive);
    connect(sequence, &QPushButton::clicked, this, &NativeResultViewerWidget::exportFrameSequenceInteractive);
    connect(video, &QPushButton::clicked, this, &NativeResultViewerWidget::exportVideoInteractive);
    connect(m_refreshButton, &QPushButton::clicked,
            this, &NativeResultViewerWidget::refreshCaseInteractive);
    connect(addComparison, &QPushButton::clicked,
            this, &NativeResultViewerWidget::addComparisonCaseInteractive);
    connect(m_clearComparisonButton, &QPushButton::clicked, this, [this]() {
        clearComparisonCases();
        loadSelectedFile();
    });
    connect(applyRange, &QPushButton::clicked,
            this, &NativeResultViewerWidget::applyTimeRange);
    connect(resetRange, &QPushButton::clicked, this, [this]() {
        if (!m_csvData.success()) return;
        m_rangeStartSpin->setValue(m_csvData.startTime());
        m_rangeEndSpin->setValue(m_csvData.endTime());
        m_plot->setTimeRange(0.0, 0.0, false);
    });
    connect(m_sceneCombo, &QComboBox::currentIndexChanged,
            this, &NativeResultViewerWidget::rebuildGeometryView);
    connect(m_displayCombo, &QComboBox::currentIndexChanged,
            this, &NativeResultViewerWidget::rebuildGeometryView);
    connect(m_geometryVisibleCheck, &QCheckBox::toggled,
            this, &NativeResultViewerWidget::rebuildGeometryView);
    connect(m_returnButton, &QPushButton::clicked, this, &NativeResultViewerWidget::returnToModelRequested);
    connect(m_smokeviewButton, &QPushButton::clicked, this, [this]() {
        if (!m_scan.smvFilePath.isEmpty()) emit openInSmokeviewRequested(m_scan.smvFilePath);
    });
    m_frameRenderer = new SmokeviewFrameRenderer(this);
    connect(m_renderFieldButton, &QPushButton::clicked,
            this, &NativeResultViewerWidget::renderSelectedField);
    connect(m_cancelRenderButton, &QPushButton::clicked,
            m_frameRenderer, &SmokeviewFrameRenderer::cancel);
    connect(m_frameRenderer, &SmokeviewFrameRenderer::progressChanged,
            this, [this](int completed, int total, const QString& detail) {
                m_cacheProgress->setRange(0, std::max(1, total));
                m_cacheProgress->setValue(completed);
                m_cacheProgress->setFormat(detail + QStringLiteral(" %v/%m"));
            });
    connect(m_frameRenderer, &SmokeviewFrameRenderer::finished,
            this, [this](bool success, const QStringList& images, const QString& error) {
                m_renderFieldButton->setEnabled(true);
                m_cancelRenderButton->setEnabled(false);
                if (!success) {
                    m_formatStatus->setText(error);
                    return;
                }
                m_renderedFrames = images;
                m_formatStatus->setText(
                    QStringLiteral("%1 %2 %3")
                        .arg(u("Frame cache ready:"))
                        .arg(images.size())
                        .arg(u("frames")));
                showRenderedFrame();
            });
    const auto updateColor = [this]() {
        m_colorMinimum->setEnabled(!m_autoRangeCheck->isChecked());
        m_colorMaximum->setEnabled(!m_autoRangeCheck->isChecked());
        m_colorBar->setSettings(m_colorMinimum->value(), m_colorMaximum->value(),
                                m_colorOpacity->value(), m_colorMapCombo->currentIndex());
        saveColorSettings();
        showRenderedFrame();
    };
    connect(m_autoRangeCheck, &QCheckBox::toggled, this, updateColor);
    connect(m_logRangeCheck, &QCheckBox::toggled, this, updateColor);
    connect(m_globalColorCheck, &QCheckBox::toggled, this, updateColor);
    connect(m_colorMinimum, &QDoubleSpinBox::valueChanged, this, updateColor);
    connect(m_colorMaximum, &QDoubleSpinBox::valueChanged, this, updateColor);
    connect(m_colorOpacity, &QDoubleSpinBox::valueChanged, this, updateColor);
    connect(m_colorMapCombo, &QComboBox::currentIndexChanged, this, updateColor);
    restoreColorSettings();
    updateTimeline();
}

NativeResultViewerWidget::~NativeResultViewerWidget() = default;

void NativeResultViewerWidget::setProject(const FcProject* project)
{
    m_project = project;
    if (m_resultDisplayStack->currentWidget() == m_geometryCanvas)
        rebuildGeometryView();
}

bool NativeResultViewerWidget::openCase(const QString& smvFilePath, QString* errorMessage)
{
    pause();
    m_scan = FdsResultScanner().scanSmvFile(smvFilePath);
    if (!m_scan.success()) {
        if (errorMessage) *errorMessage = m_scan.errorMessage;
        closeCase();
        return false;
    }
    m_caseName = m_scan.caseName;
    m_caseLabel->setText(QStringLiteral("%1 — %2").arg(m_caseName, m_scan.resultDirectory));
    m_resultProject.reset();
    if (!m_scan.fdsInputFilePath.isEmpty()) {
        FdsImportResult imported = FdsImporter().importFile(m_scan.fdsInputFilePath);
        if (imported.success()) m_resultProject = std::move(imported.project);
    }
    m_caseTimes.clear();
    auto loadCaseTimes = [this](FcResultFileType preferred) {
        for (const FdsResultFileInfo& file : m_scan.files) {
            if (file.type != preferred || !file.exists ||
                !file.filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) continue;
            const FdsCsvData data = FdsCsvReader::read(file.filePath);
            if (data.success() && !data.times.isEmpty()) {
                m_caseTimes = data.times;
                return true;
            }
        }
        return false;
    };
    if (!loadCaseTimes(FcResultFileType::HeatReleaseRate))
        if (!loadCaseTimes(FcResultFileType::Devices))
            loadCaseTimes(FcResultFileType::Csv);
    populateResultFiles();
    if (m_autoRefreshCheck->isChecked()) m_refreshTimer->start();
    emit caseOpened(m_caseName);
    return true;
}

bool NativeResultViewerWidget::refreshCase(QString* errorMessage)
{
    if (m_scan.smvFilePath.isEmpty()) {
        if (errorMessage) *errorMessage = u("No result case is open.");
        return false;
    }
    const QString currentPath = m_fileList->currentItem()
        ? m_fileList->currentItem()->data(Qt::UserRole).toString() : QString{};
    const double oldTime = currentTime();
    QStringList selectedSeries;
    for (QListWidgetItem* item : m_seriesList->selectedItems())
        selectedSeries.append(item->text());
    const FdsResultScanResult refreshed = FdsResultScanner().scanSmvFile(m_scan.smvFilePath);
    if (!refreshed.success()) {
        if (errorMessage) *errorMessage = refreshed.errorMessage;
        return false;
    }
    m_scan = refreshed;
    m_caseLabel->setText(QStringLiteral("%1 — %2").arg(m_caseName, m_scan.resultDirectory));
    populateResultFiles();
    for (int row = 0; row < m_fileList->count(); ++row) {
        if (QDir::cleanPath(m_fileList->item(row)->data(Qt::UserRole).toString()) ==
            QDir::cleanPath(currentPath)) {
            m_fileList->setCurrentRow(row);
            break;
        }
    }
    for (int row = 0; row < m_seriesList->count(); ++row)
        m_seriesList->item(row)->setSelected(selectedSeries.contains(m_seriesList->item(row)->text()));
    if (!m_frameTimes.isEmpty()) m_timeSpin->setValue(oldTime);
    m_formatStatus->setText(QStringLiteral("%1 — %2")
                                .arg(u("Results refreshed"),
                                     QDateTime::currentDateTime().toString(Qt::ISODate)));
    return true;
}

bool NativeResultViewerWidget::addComparisonCase(const QString& smvFilePath,
                                                  const QString& sourceLabel,
                                                  QString* errorMessage)
{
    const FdsResultScanResult scan = FdsResultScanner().scanSmvFile(smvFilePath);
    if (!scan.success()) {
        if (errorMessage) *errorMessage = scan.errorMessage;
        return false;
    }
    const QString canonical = QFileInfo(scan.smvFilePath).canonicalFilePath();
    if (canonical == QFileInfo(m_scan.smvFilePath).canonicalFilePath()) {
        if (errorMessage) *errorMessage = u("The comparison case is the active case.");
        return false;
    }
    for (const FdsResultScanResult& existing : m_comparisonScans) {
        if (QFileInfo(existing.smvFilePath).canonicalFilePath() == canonical) return true;
    }
    m_comparisonScans.append(scan);
    m_comparisonLabels.append(sourceLabel.trimmed().isEmpty()
                                  ? inferredComparisonLabel(scan)
                                  : sourceLabel.trimmed());
    m_clearComparisonButton->setEnabled(true);
    loadSelectedFile();
    return true;
}

void NativeResultViewerWidget::clearComparisonCases()
{
    m_comparisonScans.clear();
    m_comparisonCsvData.clear();
    m_comparisonLabels.clear();
    m_clearComparisonButton->setEnabled(false);
    m_plot->setOverlays(nullptr, {});
}

void NativeResultViewerWidget::closeCase()
{
    pause();
    if (m_frameRenderer && m_frameRenderer->isRunning()) m_frameRenderer->cancel();
    if (m_refreshTimer) m_refreshTimer->stop();
    m_scan = {};
    m_resultProject.reset();
    clearComparisonCases();
    m_csvData = {};
    m_comparisonCsvData.clear();
    m_caseTimes.clear();
    m_frameTimes.clear();
    m_renderedFrames.clear();
    m_nativeSliceData = {};
    m_caseName.clear();
    m_fileList->clear(); m_resultTree->clear();
    m_seriesList->clear(); m_statisticsTable->setRowCount(0);
    m_caseLabel->setText(u("No result case is open"));
    m_formatStatus->setText(u("Open an FDS result case."));
    updateTimeline();
    m_plot->setData(nullptr, {});
}

bool NativeResultViewerWidget::hasOpenCase() const { return !m_caseName.isEmpty(); }
QString NativeResultViewerWidget::caseName() const { return m_caseName; }
int NativeResultViewerWidget::frameCount() const { return m_frameTimes.size(); }
int NativeResultViewerWidget::csvSeriesCount() const { return m_csvData.series.size(); }
double NativeResultViewerWidget::currentTime() const
{
    return frameCount() > 0 ? m_frameTimes.at(std::clamp(m_currentFrame, 0, frameCount() - 1)) : 0.0;
}

void NativeResultViewerWidget::populateResultFiles()
{
    m_fileList->clear();
    m_resultTree->clear();
    static const QStringList categories = {
        QStringLiteral("Geometry"), QStringLiteral("3D Smoke/Fire"),
        QStringLiteral("Slice"), QStringLiteral("Vector Slice"),
        QStringLiteral("Boundary"), QStringLiteral("Isosurface"),
        QStringLiteral("Particles"), QStringLiteral("Plot3D"),
        QStringLiteral("Devices"), QStringLiteral("CSV"),
        QStringLiteral("HVAC")};
    QHash<QString, QTreeWidgetItem*> categoryItems;
    for (const QString& category : categories) {
        auto* categoryItem = new QTreeWidgetItem(m_resultTree, {u(category.toUtf8().constData())});
        categoryItem->setData(0, Qt::UserRole + 6, -1);
        categoryItem->setData(0, Qt::UserRole + 7,
                              category == QStringLiteral("Geometry")
                                  ? QStringLiteral("geometry") : QStringLiteral("category"));
        QFont font = categoryItem->font(0);
        font.setBold(true);
        categoryItem->setFont(0, font);
        categoryItems.insert(category, categoryItem);
    }
    int firstCsv = -1;
    QTreeWidgetItem* firstTreeItem = nullptr;
    for (const FdsResultFileInfo& file : m_scan.files) {
        if (!file.exists) continue;
        const QString descriptor = file.quantity.isEmpty()
            ? QStringLiteral("%1  [%2]").arg(file.name, resultFileTypeName(file.type))
            : QStringLiteral("%1 — %2 [%3]").arg(file.name, file.quantity,
                                                  resultFileTypeName(file.type));
        auto* item = new QListWidgetItem(descriptor, m_fileList);
        item->setData(Qt::UserRole, file.filePath);
        item->setData(Qt::UserRole + 1, static_cast<int>(file.type));
        item->setData(Qt::UserRole + 2, file.quantity);
        item->setData(Qt::UserRole + 3, file.unit);
        item->setData(Qt::UserRole + 4, file.slicePlaneKeyword);
        item->setData(Qt::UserRole + 5, file.slicePlaneValue);
        item->setData(Qt::UserRole + 8, file.vectorField);
        const int row = m_fileList->count() - 1;
        QTreeWidgetItem* category = categoryItems.value(resultCategory(file));
        if (!category) category = categoryItems.value(QStringLiteral("CSV"));
        auto* treeItem = new QTreeWidgetItem(category, {descriptor});
        for (int role = 0; role <= 5; ++role)
            treeItem->setData(0, Qt::UserRole + role,
                              item->data(Qt::UserRole + role));
        treeItem->setData(0, Qt::UserRole + 6, row);
        treeItem->setData(0, Qt::UserRole + 8, file.vectorField);
        treeItem->setToolTip(0, QDir::toNativeSeparators(file.filePath));
        if (!firstTreeItem) firstTreeItem = treeItem;
        if (file.filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive) && firstCsv < 0)
            firstCsv = row;
    }
    for (auto iterator = categoryItems.cbegin(); iterator != categoryItems.cend(); ++iterator)
        iterator.value()->setExpanded(iterator.value()->childCount() > 0);
    if (firstCsv >= 0) {
        m_fileList->setCurrentRow(firstCsv);
        const QList<QTreeWidgetItem*> matches = m_resultTree->findItems(
            m_fileList->item(firstCsv)->text(), Qt::MatchExactly | Qt::MatchRecursive, 0);
        if (!matches.isEmpty()) m_resultTree->setCurrentItem(matches.constFirst());
    } else if (m_fileList->count() > 0) {
        m_fileList->setCurrentRow(0);
        if (firstTreeItem) m_resultTree->setCurrentItem(firstTreeItem);
    } else {
        m_resultTree->setCurrentItem(categoryItems.value(QStringLiteral("Geometry")));
    }
}

void NativeResultViewerWidget::loadSelectedFile()
{
    pause();
    if (m_frameRenderer && m_frameRenderer->isRunning()) m_frameRenderer->cancel();
    m_csvData = {};
    m_frameTimes.clear();
    m_renderedFrames.clear();
    m_nativeSliceData = {};
    m_seriesList->clear();
    m_statisticsTable->setRowCount(0);
    QListWidgetItem* item = m_fileList->currentItem();
    if (!item) { updateTimeline(); return; }
    const auto type = static_cast<FcResultFileType>(item->data(Qt::UserRole + 1).toInt());
    const QString filePath = item->data(Qt::UserRole).toString();
    m_formatStatus->setText(nativeCapability(type));
    if (!filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        m_frameTimes = m_caseTimes;
        m_resultDisplayStack->setCurrentWidget(m_renderedFrameLabel);
        m_renderedFrameLabel->clearFrame(
            u("Smokeview frame-cache display (not FireCAE native binary decoding).\nBuild a frame cache to view this field here."));
        m_statisticsTable->setVisible(false);
        m_renderFieldButton->setEnabled(
            type == FcResultFileType::Smoke3D || type == FcResultFileType::Slice ||
            type == FcResultFileType::Boundary || type == FcResultFileType::Particle);
        if (type == FcResultFileType::Slice) {
            FdsSliceReader::Limits limits;
            limits.maximumFrames = 2000;
            limits.maximumValuesPerFrame = 5000000;
            limits.maximumTotalValues = 20000000;
            m_nativeSliceData = FdsSliceReader::read(filePath, limits);
            if (m_nativeSliceData.success() && m_nativeSliceData.isPlanar()) {
                m_frameTimes.clear();
                m_frameTimes.reserve(m_nativeSliceData.frames.size());
                for (const FdsSliceFrame& frame : m_nativeSliceData.frames)
                    m_frameTimes.append(frame.time);
                m_formatStatus->setText(
                    QStringLiteral("%1 — %2, %3×%4×%5, %6 %7")
                        .arg(u("FireCAE native SLCF decoding"))
                        .arg(m_nativeSliceData.longLabel)
                        .arg(m_nativeSliceData.nx)
                        .arg(m_nativeSliceData.ny)
                        .arg(m_nativeSliceData.nz)
                        .arg(m_nativeSliceData.frames.size())
                        .arg(u("frames")));
                showRenderedFrame();
            } else if (!m_nativeSliceData.errorMessage.isEmpty()) {
                m_formatStatus->setText(
                    QStringLiteral("%1\n%2")
                        .arg(nativeCapability(type), m_nativeSliceData.errorMessage));
            }
        }
        updateTimeline();
        m_plot->setData(nullptr, {});
        return;
    }
    m_cacheProgress->setRange(0, 100);
    m_cacheProgress->setFormat(u("Cache %p%"));
    m_cacheProgress->setValue(10);
    m_renderFieldButton->setEnabled(false);
    m_statisticsTable->setVisible(true);
    m_resultDisplayStack->setCurrentWidget(m_plot);
    m_csvData = FdsCsvReader::read(filePath);
    if (!m_csvData.success()) {
        m_formatStatus->setText(m_csvData.errorMessage);
        m_cacheProgress->setValue(0);
        updateTimeline();
        return;
    }
    loadComparisonData(filePath);
    m_frameTimes = m_csvData.times;
    m_rangeStartSpin->setValue(m_csvData.startTime());
    m_rangeEndSpin->setValue(m_csvData.endTime());
    m_plot->setTimeRange(0.0, 0.0, false);
    for (const FdsCsvSeries& series : m_csvData.series) {
        auto* seriesItem = new QListWidgetItem(
            series.unit.isEmpty() ? series.name
                                  : QStringLiteral("%1 [%2]").arg(series.name, series.unit),
            m_seriesList);
        seriesItem->setData(Qt::UserRole, m_seriesList->count() - 1);
    }
    for (int index = 0; index < std::min(4, m_seriesList->count()); ++index)
        m_seriesList->item(index)->setSelected(true);
    m_cacheProgress->setValue(100);
    m_formatStatus->setText(QStringLiteral("%1 — %2 %3, %4 %5")
                                .arg(u("Loaded"))
                                .arg(frameCount())
                                .arg(u("frames"))
                                .arg(csvSeriesCount())
                                .arg(u("quantities")));
    updateTimeline();
    updateSelectedSeries();
}

void NativeResultViewerWidget::updateSelectedSeries()
{
    QVector<int> selected;
    for (QListWidgetItem* item : m_seriesList->selectedItems())
        selected.append(item->data(Qt::UserRole).toInt());
    std::sort(selected.begin(), selected.end());
    m_plot->setData(m_csvData.success() ? &m_csvData : nullptr, selected);
    m_plot->setOverlays(m_comparisonCsvData.isEmpty() ? nullptr : &m_comparisonCsvData,
                        m_comparisonLabels);
    m_plot->setCurrentTime(currentTime());
    m_statisticsTable->setRowCount(selected.size());
    double overallMin = std::numeric_limits<double>::max();
    double overallMax = std::numeric_limits<double>::lowest();
    for (qsizetype row = 0; row < selected.size(); ++row) {
        const int index = selected.at(row);
        const FdsSeriesStatistics stats = m_csvData.statistics(index);
        const FdsCsvSeries& series = m_csvData.series.at(index);
        const QStringList values{series.name, series.unit,
                                 QString::number(stats.sampleCount),
                                 QString::number(stats.minimum, 'g', 8),
                                 QString::number(stats.maximum, 'g', 8),
                                 QString::number(stats.mean, 'g', 8),
                                 QString::number(stats.peakAbsolute, 'g', 8)};
        for (int column = 0; column < values.size(); ++column)
            m_statisticsTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        overallMin = std::min(overallMin, stats.minimum);
        overallMax = std::max(overallMax, stats.maximum);
    }
    if (m_autoRangeCheck->isChecked() && std::isfinite(overallMin) && std::isfinite(overallMax)) {
        m_colorMinimum->setValue(overallMin);
        m_colorMaximum->setValue(overallMax > overallMin ? overallMax : overallMin + 1.0);
    }
}

void NativeResultViewerWidget::loadComparisonData(const QString& primaryFilePath)
{
    m_comparisonCsvData.clear();
    const QString primaryName = QFileInfo(primaryFilePath).fileName();
    const auto primaryType = m_fileList->currentItem()
        ? static_cast<FcResultFileType>(
              m_fileList->currentItem()->data(Qt::UserRole + 1).toInt())
        : FcResultFileType::Csv;
    for (const FdsResultScanResult& scan : m_comparisonScans) {
        QString candidate;
        for (const FdsResultFileInfo& file : scan.files) {
            if (!file.exists || !file.filePath.endsWith(QStringLiteral(".csv"),
                                                        Qt::CaseInsensitive)) continue;
            if (file.name.compare(primaryName, Qt::CaseInsensitive) == 0) {
                candidate = file.filePath;
                break;
            }
            if (candidate.isEmpty() && file.type == primaryType)
                candidate = file.filePath;
        }
        m_comparisonCsvData.append(candidate.isEmpty()
                                       ? FdsCsvData{}
                                       : FdsCsvReader::read(candidate));
    }
}

void NativeResultViewerWidget::applyTimeRange()
{
    if (!m_csvData.success()) return;
    const double minimum = m_rangeStartSpin->value();
    const double maximum = m_rangeEndSpin->value();
    if (maximum <= minimum) {
        QMessageBox::warning(this, u("Chart time range"),
                             u("The end time must be greater than the start time."));
        return;
    }
    m_plot->setTimeRange(minimum, maximum, true);
}

void NativeResultViewerWidget::refreshCaseInteractive()
{
    QString error;
    if (!refreshCase(&error) && !error.isEmpty())
        m_formatStatus->setText(QStringLiteral("%1: %2").arg(u("Refresh failed"), error));
}

void NativeResultViewerWidget::addComparisonCaseInteractive()
{
    const QString path = QFileDialog::getOpenFileName(
        this, u("Add result comparison case"), m_scan.resultDirectory,
        u("Smokeview result case (*.smv)"));
    if (path.isEmpty()) return;
    const QString automatic = inferredComparisonLabel(FdsResultScanner().scanSmvFile(path));
    bool accepted = false;
    const QString label = QInputDialog::getText(
        this, u("Comparison source"),
        u("Source label (for example FireCAE, Native FDS, or PyroSim):"),
        QLineEdit::Normal, automatic, &accepted);
    if (!accepted) return;
    QString error;
    if (!addComparisonCase(path, label, &error))
        QMessageBox::warning(this, u("Add comparison case"), error);
}

void NativeResultViewerWidget::rebuildGeometryView()
{
    if (!m_geometryCanvas) return;
    QVector<ResultGeometryBox> boxes;
    QString provenance;
    if (!m_geometryVisibleCheck->isChecked()) {
        provenance = u("Geometry hidden by the user.");
        m_geometryCanvas->setGeometry({}, provenance, m_displayCombo->currentIndex());
        m_resultDisplayStack->setCurrentWidget(m_geometryCanvas);
        return;
    }

    const FcProject* source = m_resultProject ? m_resultProject.get() : m_project;
    const int mode = m_sceneCombo->currentIndex();
    if ((mode == 0 || mode == 3) && m_project && m_project->document()) {
        const bool referencesOnly = mode == 3;
        const std::function<void(const FcObject::Ptr&)> collect =
            [&](const FcObject::Ptr& object) {
                if (!object || !object->isVisible()) return;
                TopoDS_Shape shape;
                bool reference = false;
                if (const auto geometry = std::dynamic_pointer_cast<FcGeometryObject>(object)) {
                    shape = geometry->shape();
                    const QVariantMap& parameters = geometry->geometryParameters();
                    reference = parameters.contains(QStringLiteral("sourceFile")) ||
                                parameters.contains(QStringLiteral("sourcePath")) ||
                                parameters.contains(QStringLiteral("importSource"));
                } else if (const auto ifc = std::dynamic_pointer_cast<FcIfcObject>(object)) {
                    shape = ifc->shape();
                    reference = true;
                }
                if (!shape.IsNull() && reference == referencesOnly) {
                    try {
                        Bnd_Box bound;
                        BRepBndLib::Add(shape, bound);
                        if (!bound.IsVoid()) {
                            ResultGeometryBox box;
                            bound.Get(box.bounds.xMin, box.bounds.yMin, box.bounds.zMin,
                                      box.bounds.xMax, box.bounds.yMax, box.bounds.zMax);
                            box.color = reference ? QColor(92, 170, 225)
                                                  : QColor(205, 207, 212);
                            box.label = object->name();
                            boxes.append(box);
                        }
                    } catch (...) {
                    }
                }
                for (const FcObject::Ptr& child : object->children()) collect(child);
            };
        collect(m_project->document()->geometryGroup());
        provenance = referencesOnly
            ? u("FireCAE native display — CAD/IFC reference geometry from the active project")
            : u("FireCAE native display — original editable FireCAE geometry");
    } else if (source) {
        FdsScene scene = FdsSceneBuilder::build(*source);
        QVector<std::shared_ptr<FcFdsMesh>> meshes;
        if (source->document()) {
            const std::function<void(const FcObject::Ptr&)> collectMeshes =
                [&](const FcObject::Ptr& object) {
                    if (!object) return;
                    if (const auto mesh = std::dynamic_pointer_cast<FcFdsMesh>(object))
                        meshes.append(mesh);
                    for (const FcObject::Ptr& child : object->children()) collectMeshes(child);
                };
            for (const auto& group : source->document()->groups()) collectMeshes(group);
        }
        for (const FdsScenePrimitive& primitive : scene.primitives) {
            if (primitive.kind != FdsScenePrimitiveKind::Box &&
                primitive.kind != FdsScenePrimitiveKind::Plane) continue;
            ResultGeometryBox box;
            box.bounds = primitive.bounds;
            box.color = QColor::fromRgbF(primitive.color.red, primitive.color.green,
                                         primitive.color.blue, primitive.color.alpha);
            box.wireframe = primitive.wireframe;
            box.label = primitive.name;
            if (mode == 2 && (primitive.keyword == QStringLiteral("OBST") ||
                              primitive.keyword == QStringLiteral("VENT")) &&
                !meshes.isEmpty()) {
                const double cx = (box.bounds.xMin + box.bounds.xMax) * 0.5;
                const double cy = (box.bounds.yMin + box.bounds.yMax) * 0.5;
                const double cz = (box.bounds.zMin + box.bounds.zMax) * 0.5;
                auto mesh = std::find_if(meshes.cbegin(), meshes.cend(),
                                         [=](const auto& candidate) {
                    const FcFdsBounds& domain = candidate->bounds();
                    return cx >= domain.xMin && cx <= domain.xMax &&
                           cy >= domain.yMin && cy <= domain.yMax &&
                           cz >= domain.zMin && cz <= domain.zMax;
                });
                if (mesh == meshes.cend()) mesh = meshes.cbegin();
                box.bounds = FdsBlockConversionService::snapBounds(box.bounds, **mesh);
                box.color = primitive.keyword == QStringLiteral("VENT")
                    ? QColor(230, 86, 55) : QColor(35, 205, 225);
            }
            const double epsilon = scene.hasDomainBounds
                ? std::max({scene.domainBounds.xMax - scene.domainBounds.xMin,
                            scene.domainBounds.yMax - scene.domainBounds.yMin,
                            scene.domainBounds.zMax - scene.domainBounds.zMin}) * 0.002
                : 0.01;
            if (box.bounds.xMax - box.bounds.xMin <= 1.0e-12) box.bounds.xMax += std::max(0.002, epsilon);
            if (box.bounds.yMax - box.bounds.yMin <= 1.0e-12) box.bounds.yMax += std::max(0.002, epsilon);
            if (box.bounds.zMax - box.bounds.zMin <= 1.0e-12) box.bounds.zMax += std::max(0.002, epsilon);
            boxes.append(box);
        }
        provenance = mode == 2
            ? u("FireCAE native display — FDS Actual mesh-snapped preview; confirm authoritative voxelization in Smokeview")
            : u("FireCAE native display — exact requested FDS input geometry");
    } else {
        provenance = u("No active project or FDS input geometry is available.");
    }
    m_geometryCanvas->setGeometry(boxes, provenance, m_displayCombo->currentIndex());
    m_resultDisplayStack->setCurrentWidget(m_geometryCanvas);
    m_formatStatus->setText(provenance);
}

void NativeResultViewerWidget::updateTimeline()
{
    const int count = frameCount();
    m_currentFrame = std::clamp(m_currentFrame, 0, std::max(0, count - 1));
    m_timeline->blockSignals(true);
    m_timeline->setRange(0, std::max(0, count - 1));
    m_timeline->setValue(m_currentFrame);
    m_timeline->blockSignals(false);
    const bool available = count > 0;
    const QList<QWidget*> timelineWidgets{m_previousButton, m_playButton,
                                          m_pauseButton, m_stopButton,
                                          m_nextButton, m_timeline};
    for (QWidget* widget : timelineWidgets)
        widget->setEnabled(available);
    const double end = available ? m_frameTimes.constLast() : 0.0;
    m_timeSpin->blockSignals(true);
    m_timeSpin->setEnabled(available);
    m_timeSpin->setRange(available ? m_frameTimes.constFirst() : 0.0, end);
    m_timeSpin->setValue(currentTime());
    m_timeSpin->setSuffix(QStringLiteral(" %1")
                              .arg(m_csvData.success() ? m_csvData.timeUnit
                                                       : QStringLiteral("s")));
    m_timeSpin->blockSignals(false);
    m_timeLabel->setText(QStringLiteral("%1 / %2 %3")
                             .arg(currentTime(), 0, 'g', 6)
                             .arg(end, 0, 'g', 6)
                             .arg(m_csvData.success() ? m_csvData.timeUnit : QStringLiteral("s")));
}

void NativeResultViewerWidget::setCurrentFrame(int frame)
{
    if (frameCount() <= 0) return;
    m_currentFrame = std::clamp(frame, 0, frameCount() - 1);
    m_timeline->blockSignals(true); m_timeline->setValue(m_currentFrame);
    m_timeline->blockSignals(false);
    const double time = currentTime();
    m_timeSpin->blockSignals(true); m_timeSpin->setValue(time);
    m_timeSpin->blockSignals(false);
    m_timeLabel->setText(QStringLiteral("%1 / %2 %3")
                             .arg(time, 0, 'g', 6)
                             .arg(m_frameTimes.constLast(), 0, 'g', 6)
                             .arg(m_csvData.success() ? m_csvData.timeUnit : QStringLiteral("s")));
    m_plot->setCurrentTime(time);
    showRenderedFrame();
    emit frameChanged(time);
}

void NativeResultViewerWidget::play()
{
    if (frameCount() > 0) m_playTimer->start();
}
void NativeResultViewerWidget::pause() { m_playTimer->stop(); }
void NativeResultViewerWidget::stop() { pause(); setCurrentFrame(0); }

bool NativeResultViewerWidget::saveScreenshot(const QString& filePath) const
{
    if (filePath.isEmpty()) return false;
    return const_cast<NativeResultViewerWidget*>(this)->grab().save(filePath, "PNG");
}

bool NativeResultViewerWidget::exportVisibleCsv(const QString& filePath) const
{
    if (!m_csvData.success() || filePath.isEmpty()) return false;
    QVector<int> selected;
    for (QListWidgetItem* item : m_seriesList->selectedItems())
        selected.append(item->data(Qt::UserRole).toInt());
    std::sort(selected.begin(), selected.end());
    if (selected.isEmpty()) return false;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream stream(&file);
    stream << csvField(m_csvData.timeUnit);
    for (int index : selected) stream << ',' << csvField(m_csvData.series.at(index).unit);
    for (const FdsCsvData& overlay : m_comparisonCsvData) {
        for (int index : selected) {
            const FdsCsvSeries& primary = m_csvData.series.at(index);
            const auto match = std::find_if(
                overlay.series.cbegin(), overlay.series.cend(),
                [&primary](const FdsCsvSeries& candidate) {
                    return candidate.name.compare(primary.name, Qt::CaseInsensitive) == 0 &&
                           candidate.unit.compare(primary.unit, Qt::CaseInsensitive) == 0;
                });
            stream << ',' << csvField(match == overlay.series.cend()
                                           ? primary.unit : match->unit);
        }
    }
    stream << '\n' << csvField(m_csvData.timeName);
    for (int index : selected) stream << ',' << csvField(m_csvData.series.at(index).name);
    for (qsizetype overlayIndex = 0; overlayIndex < m_comparisonCsvData.size(); ++overlayIndex) {
        const QString label = overlayIndex < m_comparisonLabels.size()
            ? m_comparisonLabels.at(overlayIndex)
            : QStringLiteral("Comparison %1").arg(overlayIndex + 1);
        for (int index : selected)
            stream << ',' << csvField(QStringLiteral("%1 — %2")
                                          .arg(label, m_csvData.series.at(index).name));
    }
    stream << '\n';
    for (qsizetype row = 0; row < m_csvData.times.size(); ++row) {
        const double time = m_csvData.times.at(row);
        stream << QString::number(time, 'g', 15);
        for (int index : selected)
            stream << ',' << QString::number(m_csvData.series.at(index).values.at(row), 'g', 15);
        for (const FdsCsvData& overlay : m_comparisonCsvData) {
            for (int index : selected) {
                const FdsCsvSeries& primary = m_csvData.series.at(index);
                const auto match = std::find_if(
                    overlay.series.cbegin(), overlay.series.cend(),
                    [&primary](const FdsCsvSeries& candidate) {
                        return candidate.name.compare(primary.name, Qt::CaseInsensitive) == 0 &&
                               candidate.unit.compare(primary.unit, Qt::CaseInsensitive) == 0;
                    });
                if (match == overlay.series.cend()) {
                    stream << ',';
                } else {
                    const int seriesIndex = static_cast<int>(
                        std::distance(overlay.series.cbegin(), match));
                    bool ok = false;
                    const double value = overlay.interpolatedValue(seriesIndex, time, &ok);
                    stream << ',';
                    if (ok) stream << QString::number(value, 'g', 15);
                }
            }
        }
        stream << '\n';
    }
    return stream.status() == QTextStream::Ok;
}

void NativeResultViewerWidget::exportCsvInteractive()
{
    const QString path = QFileDialog::getSaveFileName(
        this, u("Export visible result curves"),
        QDir(m_scan.resultDirectory).filePath(m_caseName + QStringLiteral("_curves.csv")),
        u("CSV files (*.csv)"));
    if (!path.isEmpty() && !exportVisibleCsv(path))
        QMessageBox::warning(this, u("Export CSV"), u("The selected curves could not be exported."));
}

void NativeResultViewerWidget::saveScreenshotInteractive()
{
    const QString path = QFileDialog::getSaveFileName(
        this, u("Save result screenshot"),
        QDir(m_scan.resultDirectory).filePath(m_caseName + QStringLiteral("_native.png")),
        u("PNG images (*.png)"));
    if (!path.isEmpty() && !saveScreenshot(path))
        QMessageBox::warning(this, u("Save PNG"), u("The screenshot could not be saved."));
}

void NativeResultViewerWidget::exportFrameSequenceInteractive()
{
    if (frameCount() <= 0) return;
    const QString directory = QFileDialog::getExistingDirectory(
        this, u("Export lossless PNG frame sequence"), m_scan.resultDirectory);
    if (directory.isEmpty()) return;
    pause();
    const int originalFrame = m_currentFrame;
    for (int frame = 0; frame < frameCount(); ++frame) {
        setCurrentFrame(frame);
        repaint();
        const QString path = QDir(directory).filePath(
            QStringLiteral("%1_%2.png").arg(m_caseName).arg(frame, 6, 10, QLatin1Char('0')));
        if (!saveScreenshot(path)) break;
    }
    setCurrentFrame(originalFrame);
}

bool NativeResultViewerWidget::exportVideo(const QString& filePath,
                                           int framesPerSecond,
                                           QString* errorMessage)
{
    if (filePath.isEmpty() || frameCount() <= 0) {
        if (errorMessage) *errorMessage = u("No result animation is available to export.");
        return false;
    }
    framesPerSecond = std::clamp(framesPerSecond, 1, 60);
    pause();
    const int originalFrame = m_currentFrame;
    QVector<QByteArray> jpegFrames;
    jpegFrames.reserve(frameCount());
    QSize frameSize;
    quint32 maximumFrameSize = 0;
    for (int frame = 0; frame < frameCount(); ++frame) {
        setCurrentFrame(frame);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QWidget* content = m_resultDisplayStack->currentWidget();
        QImage image = content->grab().toImage().convertToFormat(QImage::Format_RGB888);
        if (image.isNull()) {
            setCurrentFrame(originalFrame);
            if (errorMessage) *errorMessage = u("A video frame could not be captured.");
            return false;
        }
        if (frameSize.isEmpty()) frameSize = image.size();
        if (image.size() != frameSize)
            image = image.scaled(frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "JPG", 90)) {
            setCurrentFrame(originalFrame);
            if (errorMessage) *errorMessage = u("A video frame could not be encoded as Motion JPEG.");
            return false;
        }
        maximumFrameSize = std::max(maximumFrameSize,
                                    static_cast<quint32>(jpeg.size()));
        jpegFrames.append(jpeg);
    }
    setCurrentFrame(originalFrame);

    QByteArray avi;
    QBuffer output(&avi);
    output.open(QIODevice::ReadWrite);
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    const auto fourcc = [&stream](const char* text) { stream.writeRawData(text, 4); };
    const auto beginChunk = [&output, &stream, &fourcc](const char* id) {
        fourcc(id);
        const qint64 sizePosition = output.pos();
        stream << quint32(0);
        return sizePosition;
    };
    const auto finishChunk = [&output, &stream](qint64 sizePosition) {
        const qint64 end = output.pos();
        const quint32 size = static_cast<quint32>(end - sizePosition - 4);
        output.seek(sizePosition);
        stream << size;
        output.seek(end);
        if (size & 1U) stream << quint8(0);
    };

    fourcc("RIFF");
    const qint64 riffSizePosition = output.pos();
    stream << quint32(0);
    fourcc("AVI ");
    const qint64 headerList = beginChunk("LIST");
    fourcc("hdrl");
    const qint64 mainHeader = beginChunk("avih");
    stream << quint32(1000000 / framesPerSecond)
           << quint32(maximumFrameSize * framesPerSecond)
           << quint32(0) << quint32(0x10)
           << quint32(jpegFrames.size()) << quint32(0) << quint32(1)
           << maximumFrameSize
           << quint32(frameSize.width()) << quint32(frameSize.height());
    for (int i = 0; i < 4; ++i) stream << quint32(0);
    finishChunk(mainHeader);
    const qint64 streamList = beginChunk("LIST");
    fourcc("strl");
    const qint64 streamHeader = beginChunk("strh");
    fourcc("vids"); fourcc("MJPG");
    stream << quint32(0) << quint16(0) << quint16(0)
           << quint32(0) << quint32(1) << quint32(framesPerSecond)
           << quint32(0) << quint32(jpegFrames.size())
           << maximumFrameSize << quint32(0xFFFFFFFF) << quint32(0)
           << qint16(0) << qint16(0)
           << qint16(std::min(frameSize.width(), 32767))
           << qint16(std::min(frameSize.height(), 32767));
    finishChunk(streamHeader);
    const qint64 formatHeader = beginChunk("strf");
    stream << quint32(40) << qint32(frameSize.width()) << qint32(frameSize.height())
           << quint16(1) << quint16(24);
    fourcc("MJPG");
    stream << maximumFrameSize << qint32(0) << qint32(0)
           << quint32(0) << quint32(0);
    finishChunk(formatHeader);
    finishChunk(streamList);
    finishChunk(headerList);

    const qint64 movieList = beginChunk("LIST");
    const qint64 movieTagPosition = output.pos();
    fourcc("movi");
    struct IndexEntry { quint32 offset; quint32 size; };
    QVector<IndexEntry> index;
    index.reserve(jpegFrames.size());
    for (const QByteArray& jpeg : jpegFrames) {
        const qint64 chunkPosition = output.pos();
        const qint64 frameChunk = beginChunk("00dc");
        stream.writeRawData(jpeg.constData(), jpeg.size());
        finishChunk(frameChunk);
        index.append({static_cast<quint32>(chunkPosition - movieTagPosition),
                      static_cast<quint32>(jpeg.size())});
    }
    finishChunk(movieList);
    const qint64 indexChunk = beginChunk("idx1");
    for (const IndexEntry& entry : index) {
        fourcc("00dc");
        stream << quint32(0x10) << entry.offset << entry.size;
    }
    finishChunk(indexChunk);
    const qint64 end = output.pos();
    output.seek(riffSizePosition);
    stream << static_cast<quint32>(end - 8);
    output.close();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(avi) != avi.size()) {
        if (errorMessage) *errorMessage = u("The AVI video file could not be written.");
        return false;
    }
    return true;
}

void NativeResultViewerWidget::exportVideoInteractive()
{
    if (frameCount() <= 0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, u("Export Motion JPEG video"),
        QDir(m_scan.resultDirectory).filePath(m_caseName + QStringLiteral("_results.avi")),
        u("AVI Motion JPEG video (*.avi)"));
    if (path.isEmpty()) return;
    bool accepted = false;
    const int fps = QInputDialog::getInt(this, u("Video frame rate"),
                                         u("Frames per second:"), 10, 1, 60, 1,
                                         &accepted);
    if (!accepted) return;
    QString error;
    if (!exportVideo(path, fps, &error))
        QMessageBox::warning(this, u("Export Video"), error);
}

void NativeResultViewerWidget::renderSelectedField()
{
    QListWidgetItem* item = m_fileList->currentItem();
    if (!item || m_frameTimes.isEmpty() || m_scan.smvFilePath.isEmpty()) return;
    const auto type = static_cast<FcResultFileType>(item->data(Qt::UserRole + 1).toInt());
    const QString quantity = item->data(Qt::UserRole + 2).toString();
    QVector<double> times = m_frameTimes;
    constexpr qsizetype maximumFrames = 121;
    if (times.size() > maximumFrames) {
        QVector<double> sampled;
        sampled.reserve(maximumFrames);
        for (qsizetype index = 0; index < maximumFrames; ++index) {
            const qsizetype source = qRound(index * (times.size() - 1.0) /
                                            (maximumFrames - 1.0));
            sampled.append(times.at(source));
        }
        times = sampled;
        m_frameTimes = sampled;
        updateTimeline();
    }
    const QString fieldKey = QStringLiteral("%1_%2")
        .arg(static_cast<int>(type))
        .arg(quantity.isEmpty() ? QStringLiteral("field") : quantity);
    const QString cacheDirectory = QDir(m_scan.resultDirectory).filePath(
        QStringLiteral(".firecae-cache/%1/%2").arg(m_caseName, sanitizedFileKey(fieldKey)));
    SmokeviewFrameRenderRequest request;
    request.smvFilePath = m_scan.smvFilePath;
    request.outputDirectory = cacheDirectory;
    request.fieldType = type;
    request.fieldFilePath = item->data(Qt::UserRole).toString();
    request.quantity = quantity;
    request.slicePlaneKeyword = item->data(Qt::UserRole + 4).toString();
    request.slicePlaneValue = item->data(Qt::UserRole + 5).toDouble();
    request.times = times;
    request.filePrefix = sanitizedFileKey(m_caseName + QLatin1Char('_') + fieldKey);
    QString error;
    if (!m_frameRenderer->start(request, &error)) {
        m_formatStatus->setText(error);
        return;
    }
    m_nativeSliceData = {};
    m_renderedFrames.clear();
    m_renderFieldButton->setEnabled(false);
    m_cancelRenderButton->setEnabled(true);
}

void NativeResultViewerWidget::showRenderedFrame()
{
    if (m_resultDisplayStack->currentWidget() != m_renderedFrameLabel) return;
    if (m_nativeSliceData.success() && !m_nativeSliceData.frames.isEmpty()) {
        FdsSliceImageOptions options;
        options.frameIndex = std::clamp(
            m_currentFrame, 0, static_cast<int>(m_nativeSliceData.frames.size()) - 1);
        options.useGlobalRange = m_globalColorCheck->isChecked();
        options.useManualRange = !m_autoRangeCheck->isChecked();
        options.minimum = static_cast<float>(m_colorMinimum->value());
        options.maximum = static_cast<float>(m_colorMaximum->value());
        options.opacity = qRound(m_colorOpacity->value() * 255.0);
        const FdsSliceImageResult rendered =
            FdsSliceImageRenderer::render(m_nativeSliceData, options);
        if (rendered.success()) {
            m_renderedFrameLabel->setSourcePixmap(QPixmap::fromImage(rendered.image));
        } else {
            m_renderedFrameLabel->clearFrame(rendered.errorMessage);
        }
        return;
    }
    if (m_renderedFrames.isEmpty()) return;
    const int index = std::clamp(m_currentFrame, 0,
                                 static_cast<int>(m_renderedFrames.size()) - 1);
    const QPixmap image(m_renderedFrames.at(index));
    if (image.isNull()) return;
    m_renderedFrameLabel->setSourcePixmap(image);
}

void NativeResultViewerWidget::restoreColorSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("NativeResultViewer/ColorScale"));
    m_autoRangeCheck->setChecked(settings.value(QStringLiteral("auto"), true).toBool());
    m_logRangeCheck->setChecked(settings.value(QStringLiteral("log"), false).toBool());
    m_globalColorCheck->setChecked(settings.value(QStringLiteral("global"), false).toBool());
    m_colorMinimum->setValue(settings.value(QStringLiteral("minimum"), 0.0).toDouble());
    m_colorMaximum->setValue(settings.value(QStringLiteral("maximum"), 1.0).toDouble());
    m_colorOpacity->setValue(settings.value(QStringLiteral("opacity"), 1.0).toDouble());
    m_colorMapCombo->setCurrentIndex(settings.value(QStringLiteral("map"), 0).toInt());
    settings.endGroup();
    m_colorMinimum->setEnabled(!m_autoRangeCheck->isChecked());
    m_colorMaximum->setEnabled(!m_autoRangeCheck->isChecked());
    m_colorBar->setSettings(m_colorMinimum->value(), m_colorMaximum->value(),
                            m_colorOpacity->value(), m_colorMapCombo->currentIndex());
}

void NativeResultViewerWidget::saveColorSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("NativeResultViewer/ColorScale"));
    settings.setValue(QStringLiteral("auto"), m_autoRangeCheck->isChecked());
    settings.setValue(QStringLiteral("log"), m_logRangeCheck->isChecked());
    settings.setValue(QStringLiteral("global"), m_globalColorCheck->isChecked());
    settings.setValue(QStringLiteral("minimum"), m_colorMinimum->value());
    settings.setValue(QStringLiteral("maximum"), m_colorMaximum->value());
    settings.setValue(QStringLiteral("opacity"), m_colorOpacity->value());
    settings.setValue(QStringLiteral("map"), m_colorMapCombo->currentIndex());
    settings.endGroup();
}

void NativeResultViewerWidget::retranslateUi()
{
    // The viewer is rebuilt only once and follows the application's live language
    // through the shared translation function for dynamic status text.  Static
    // labels are refreshed by reopening the result workspace.
    updateTimeline();
}
