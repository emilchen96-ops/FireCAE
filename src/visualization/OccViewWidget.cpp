#include "visualization/OccViewWidget.h"

#include "visualization/GeometryDisplayManager.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <QMouseEvent>
#include <QColor>
#include <QFile>
#include <QKeyEvent>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QRubberBand>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
const QString kWallSketchPreviewId = QStringLiteral("FIRECAE:WALL-SKETCH-PREVIEW");
}

OccViewWidget::OccViewWidget(QWidget* parent)
    : QWidget(parent)
    , m_viewer(std::make_unique<OccViewer>())
{
    setObjectName(QStringLiteral("OccViewWidget"));
    // initializeViewer() creates the native target with winId() when this
    // view is actually shown. Forcing it during construction also makes
    // hidden views' ancestors native before their dock layout is ready.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_PaintOnScreen);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

OccViewWidget::~OccViewWidget() = default;

bool OccViewWidget::isInitialized() const
{
    return m_viewer && m_viewer->isInitialized();
}

GeometryDisplayManager* OccViewWidget::displayManager() const
{
    return m_displayManager.get();
}

void OccViewWidget::fitAll()
{
    if (m_viewer) {
        m_viewer->fitAll();
    }
}

void OccViewWidget::setOrientation(OccViewOrientation orientation)
{
    if (m_viewer) {
        m_viewer->setOrientation(orientation);
    }
}

void OccViewWidget::setProjectionDirection(double x, double y, double z)
{
    if (m_viewer) m_viewer->setProjectionDirection(x, y, z);
}

void OccViewWidget::setPerspective(bool perspective)
{
    if (m_viewer) m_viewer->setPerspective(perspective);
}

void OccViewWidget::showTrihedron(bool visible)
{
    if (m_viewer) m_viewer->showTrihedron(visible);
}

void OccViewWidget::setBackgroundColor(const QColor& color)
{
    if (m_viewer && color.isValid()) {
        m_viewer->setBackgroundColor(color.redF(), color.greenF(), color.blueF());
    }
}

void OccViewWidget::saveView()
{
    if (m_viewer) m_viewer->saveCamera();
}

bool OccViewWidget::restoreView()
{
    return m_viewer && m_viewer->restoreCamera();
}

void OccViewWidget::setRotationCenter(double x, double y, double z)
{
    if (m_viewer) m_viewer->setRotationCenter(x, y, z);
}

bool OccViewWidget::fitSelection()
{
    return m_displayManager && m_viewer &&
           m_displayManager->fitSelection(m_viewer->view());
}

void OccViewWidget::setClippingPlane(bool enabled, int axis, double position)
{
    if (m_viewer) m_viewer->setClippingPlane(enabled, axis, position);
}

void OccViewWidget::setClippingBox(bool enabled, double xMin, double xMax,
                                   double yMin, double yMax,
                                   double zMin, double zMax)
{
    if (m_viewer) {
        m_viewer->setClippingBox(enabled, xMin, xMax, yMin, yMax, zMin, zMax);
    }
}

QStringList OccViewWidget::selectRectangle(const QRect& widgetRectangle, bool toggle)
{
    if (!isInitialized() || !m_displayManager || widgetRectangle.isEmpty()) return {};
    const QRect normalized = widgetRectangle.normalized();
    const QPoint viewerStart = viewerPixelPosition(normalized.topLeft());
    const QPoint viewerEnd = viewerPixelPosition(normalized.bottomRight());
    const QStringList selectedIds = m_displayManager->selectRectangle(
        viewerStart.x(), viewerStart.y(), viewerEnd.x(), viewerEnd.y(),
        m_viewer->view(), toggle);
    if (selectedIds.isEmpty()) emit selectionCleared();
    else emit objectsSelected(selectedIds);
    return selectedIds;
}

bool OccViewWidget::saveViewImage(const QString& filePath) const
{
    if (!isInitialized() || filePath.isEmpty()) return false;
    const QByteArray nativeFilePath = QFile::encodeName(filePath);
    return m_viewer->view()->Dump(nativeFilePath.constData());
}

bool OccViewWidget::showTransformManipulator(const QStringList& objectIds)
{
    return m_displayManager && m_displayManager->attachManipulator(objectIds);
}

void OccViewWidget::hideTransformManipulator()
{
    if (m_displayManager) m_displayManager->detachManipulator();
}

bool OccViewWidget::isTransformManipulatorVisible() const
{
    return m_displayManager && m_displayManager->isManipulatorAttached();
}

void OccViewWidget::beginWallSketch(double elevation,
                                    double thickness,
                                    double height,
                                    int baseline,
                                    bool snapEnabled,
                                    double snapStep,
                                    bool continuous,
                                    bool orthogonal,
                                    double exactLength,
                                    double exactAngleDegrees)
{
    cancelWallSketch();
    m_wallSketchElevation = elevation;
    m_wallSketchThickness = std::max(thickness, 1.0e-6);
    m_wallSketchHeight = std::max(height, 1.0e-6);
    m_wallSketchBaseline = std::clamp(baseline, 0, 2);
    m_wallSketchSnapEnabled = snapEnabled;
    m_wallSketchSnapStep = std::max(snapStep, 1.0e-9);
    m_wallSketchContinuous = continuous;
    m_wallSketchOrthogonal = orthogonal;
    m_wallSketchExactLength = std::max(0.0, exactLength);
    m_wallSketchExactAngleDegrees = exactAngleDegrees;
    m_wallSketchActive = true;
    m_wallSketchHasStart = false;
    m_wallSketchHasPreview = false;
    m_wallSketchLastSnapTarget.clear();
    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
}

void OccViewWidget::setWallSketchSnapPoints(const QVector<QPointF>& points)
{
    m_wallSketchSnapPoints = points;
}

void OccViewWidget::cancelWallSketch()
{
    const bool wasActive = m_wallSketchActive;
    m_wallSketchActive = false;
    m_wallSketchHasStart = false;
    m_wallSketchHasPreview = false;
    unsetCursor();
    if (m_displayManager) {
        m_displayManager->removeObject(kWallSketchPreviewId);
    }
    if (wasActive) emit wallSketchCancelled();
}

bool OccViewWidget::isWallSketchActive() const
{
    return m_wallSketchActive;
}

QPaintEngine* OccViewWidget::paintEngine() const
{
    return nullptr;
}

void OccViewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // QStackedWidget and QMainWindow finish assigning their native child
    // geometries after the first show event.  Initializing OCCT synchronously
    // here leaves its OpenGL viewport at QWidget's temporary 100 x 30 size.
    QTimer::singleShot(0, this, [this]() {
        initializeViewer();
        synchronizeViewerSize();
        scheduleViewerSizeSynchronization();
    });
}

void OccViewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    if (m_viewer) {
        m_viewer->redraw();
    }
}

void OccViewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    synchronizeViewerSize();
    scheduleViewerSizeSynchronization();
}

void OccViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (!isInitialized()) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_wallSketchActive) {
            double worldX = 0.0;
            double worldY = 0.0;
            if (!pointOnSketchPlane(event->position().toPoint(), worldX, worldY)) {
                event->accept();
                return;
            }
            if (!m_wallSketchHasStart) {
                m_wallSketchStartX = worldX;
                m_wallSketchStartY = worldY;
                m_wallSketchHasStart = true;
                m_wallSketchHasPreview = false;
                emit wallSketchCursorChanged(worldX, worldY, 0.0, 0.0,
                                             m_wallSketchLastSnapTarget);
            } else {
                constrainWallSketchEndpoint(worldX, worldY, event->modifiers());
                commitWallSketchSegment(worldX, worldY, false);
            }
            event->accept();
            return;
        }
        const QPoint viewerPosition = viewerPixelPosition(event->position().toPoint());
        if (m_displayManager && m_displayManager->beginManipulatorDrag(
                viewerPosition.x(), viewerPosition.y(), m_viewer->view())) {
            m_manipulatorDragging = true;
            event->accept();
            return;
        }
        m_leftPressPosition = event->position().toPoint();
        m_leftButtonPressed = true;
        m_leftClickMoved = false;
        if (!m_selectionBand) {
            m_selectionBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
        m_selectionBand->setGeometry(QRect(m_leftPressPosition, QSize()));
        event->accept();
        return;
    }

    if (event->button() != Qt::MiddleButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_lastMousePosition = event->position().toPoint();
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        m_navigationMode = NavigationMode::Pan;
    } else {
        m_navigationMode = NavigationMode::Rotate;
        const QPoint viewerPosition = viewerPixelPosition(m_lastMousePosition);
        m_viewer->view()->StartRotation(viewerPosition.x(), viewerPosition.y());
    }
    event->accept();
}

void OccViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!isInitialized()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_wallSketchActive && m_wallSketchHasStart) {
        double worldX = 0.0;
        double worldY = 0.0;
        if (pointOnSketchPlane(event->position().toPoint(), worldX, worldY)) {
            constrainWallSketchEndpoint(worldX, worldY, event->modifiers());
            updateWallSketchPreview(worldX, worldY);
        }
        event->accept();
        return;
    }

    if (m_manipulatorDragging && m_displayManager) {
        const QPoint viewerPosition = viewerPixelPosition(event->position().toPoint());
        m_displayManager->updateManipulatorDrag(
            viewerPosition.x(), viewerPosition.y(), m_viewer->view());
        event->accept();
        return;
    }

    if (m_leftButtonPressed) {
        constexpr int clickDragThreshold = 4;
        const QPoint currentPosition = event->position().toPoint();
        if ((currentPosition - m_leftPressPosition).manhattanLength() >
            clickDragThreshold) {
            m_leftClickMoved = true;
            if (m_selectionBand) {
                m_selectionBand->setGeometry(
                    QRect(m_leftPressPosition, currentPosition).normalized());
                m_selectionBand->show();
            }
        }
        event->accept();
        return;
    }

    if (m_navigationMode == NavigationMode::None) {
        if (m_displayManager && m_displayManager->isManipulatorAttached()) {
            const QPoint viewerPosition = viewerPixelPosition(event->position().toPoint());
            m_displayManager->updateManipulatorDetection(
                viewerPosition.x(), viewerPosition.y(), m_viewer->view());
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint currentPosition = event->position().toPoint();
    if (m_navigationMode == NavigationMode::Rotate) {
        const QPoint viewerPosition = viewerPixelPosition(currentPosition);
        m_viewer->view()->Rotation(viewerPosition.x(), viewerPosition.y());
    } else {
        const QPoint delta = viewerPixelPosition(currentPosition) -
                             viewerPixelPosition(m_lastMousePosition);
        m_viewer->view()->Pan(delta.x(), -delta.y(), 1.0, Standard_False);
    }
    m_lastMousePosition = currentPosition;
    event->accept();
}

void OccViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_manipulatorDragging) {
        m_manipulatorDragging = false;
        if (m_displayManager) {
            const QStringList objectIds = m_displayManager->finishManipulatorDrag(true);
            if (!objectIds.isEmpty()) emit manipulatorTransformFinished(objectIds);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_leftButtonPressed) {
        const bool shouldPick = isInitialized() && !m_leftClickMoved;
        const bool shouldRectangleSelect = isInitialized() && m_leftClickMoved;
        const QPoint position = event->position().toPoint();
        const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
        m_leftButtonPressed = false;
        m_leftClickMoved = false;
        if (m_selectionBand) m_selectionBand->hide();

        if (shouldPick && m_displayManager) {
            const QPoint viewerPosition = viewerPixelPosition(position);
            const QString objectId =
                m_displayManager->selectAt(viewerPosition.x(), viewerPosition.y(),
                                           m_viewer->view(), toggle);
            const QStringList selectedIds = m_displayManager->selectedObjectIds();
            if (selectedIds.isEmpty()) {
                emit selectionCleared();
            } else {
                emit objectsSelected(selectedIds);
                if (selectedIds.size() == 1) emit objectSelected(selectedIds.constFirst());
            }
        } else if (shouldRectangleSelect && m_displayManager) {
            selectRectangle(QRect(m_leftPressPosition, position), toggle);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_navigationMode = NavigationMode::None;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void OccViewWidget::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        m_wallSketchActive) {
        if (m_wallSketchHasStart && m_wallSketchHasPreview) {
            commitWallSketchSegment(m_wallSketchLastX, m_wallSketchLastY, true);
        } else {
            cancelWallSketch();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_wallSketchActive) {
            cancelWallSketch();
            event->accept();
            return;
        }
        if (m_manipulatorDragging && m_displayManager) {
            m_displayManager->finishManipulatorDrag(false);
            m_manipulatorDragging = false;
        }
        if (m_displayManager) m_displayManager->clearSelection();
        emit selectionCleared();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OccViewWidget::wheelEvent(QWheelEvent* event)
{
    if (!isInitialized()) {
        QWidget::wheelEvent(event);
        return;
    }

    const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (steps != 0.0) {
        const double factor = std::clamp(1.0 + steps * 0.12, 0.25, 4.0);
        m_viewer->view()->SetScale(m_viewer->view()->Scale() * factor);
        m_viewer->redraw();
    }
    event->accept();
}

void OccViewWidget::initializeViewer()
{
    if (m_initializationAttempted) {
        return;
    }
    m_initializationAttempted = true;

    const WId nativeId = winId();
    const bool success =
        m_viewer->initialize(reinterpret_cast<void*>(static_cast<quintptr>(nativeId)));
    if (success) {
        m_displayManager =
            std::make_unique<GeometryDisplayManager>(m_viewer->context());
    }
    emit viewerInitializationFinished(success);
}

void OccViewWidget::synchronizeViewerSize()
{
    if (!isVisible() || !isInitialized()) {
        return;
    }

    // winId() commits the current QWidget geometry to the Win32 child HWND
    // before WNT_Window queries its physical client size.
    (void)winId();
    m_viewer->resize();
}

void OccViewWidget::scheduleViewerSizeSynchronization()
{
    QTimer::singleShot(0, this, [this]() {
        synchronizeViewerSize();
    });
    QTimer::singleShot(100, this, [this]() {
        synchronizeViewerSize();
    });
}

QPoint OccViewWidget::viewerPixelPosition(const QPoint& widgetPosition) const
{
    // QMouseEvent positions are device-independent while OCCT's WNT_Window
    // consumes physical client pixels.  Failing to convert here offsets picks,
    // box selection, pan and rotation whenever Windows scaling is above 100%.
    const qreal scale = devicePixelRatioF();
    return QPoint(qRound(widgetPosition.x() * scale),
                  qRound(widgetPosition.y() * scale));
}

bool OccViewWidget::pointOnSketchPlane(const QPoint& widgetPosition,
                                       double& worldX,
                                       double& worldY)
{
    if (!isInitialized()) return false;
    const QPoint position = viewerPixelPosition(widgetPosition);
    Standard_Real originX = 0.0;
    Standard_Real originY = 0.0;
    Standard_Real originZ = 0.0;
    Standard_Real directionX = 0.0;
    Standard_Real directionY = 0.0;
    Standard_Real directionZ = 0.0;
    m_viewer->view()->ConvertWithProj(position.x(), position.y(),
                                     originX, originY, originZ,
                                     directionX, directionY, directionZ);
    if (std::abs(directionZ) < 1.0e-12) return false;
    const double parameter = (m_wallSketchElevation - originZ) / directionZ;
    worldX = originX + parameter * directionX;
    worldY = originY + parameter * directionY;
    if (m_wallSketchSnapEnabled) {
        worldX = std::round(worldX / m_wallSketchSnapStep) * m_wallSketchSnapStep;
        worldY = std::round(worldY / m_wallSketchSnapStep) * m_wallSketchSnapStep;
        m_wallSketchLastSnapTarget = QStringLiteral("Grid");
        double bestDistance = std::max(0.05, m_wallSketchSnapStep * 0.75);
        for (const QPointF& point : m_wallSketchSnapPoints) {
            const double distance = std::hypot(point.x() - worldX, point.y() - worldY);
            if (distance <= bestDistance) {
                bestDistance = distance;
                worldX = point.x();
                worldY = point.y();
                m_wallSketchLastSnapTarget = QStringLiteral("Geometry");
            }
        }
    } else {
        m_wallSketchLastSnapTarget.clear();
    }
    return std::isfinite(worldX) && std::isfinite(worldY);
}

void OccViewWidget::updateWallSketchPreview(double endX, double endY)
{
    const double dx = endX - m_wallSketchStartX;
    const double dy = endY - m_wallSketchStartY;
    const double length = std::hypot(dx, dy);
    m_wallSketchLastX = endX;
    m_wallSketchLastY = endY;
    m_wallSketchHasPreview = length > 1.0e-8;
    const double angle = length > 1.0e-8
        ? std::atan2(dy, dx) * 180.0 / 3.14159265358979323846 : 0.0;
    emit wallSketchCursorChanged(endX, endY, length, angle,
                                 m_wallSketchLastSnapTarget);
    if (!m_displayManager) return;
    m_displayManager->removeObject(kWallSketchPreviewId);
    if (length <= 1.0e-8) return;

    const double normalX = -dy / length;
    const double normalY = dx / length;
    double baselineOffset = 0.0;
    if (m_wallSketchBaseline == 1) baselineOffset = -0.5 * m_wallSketchThickness;
    if (m_wallSketchBaseline == 2) baselineOffset = -m_wallSketchThickness;
    const gp_Pnt origin(m_wallSketchStartX + normalX * baselineOffset,
                        m_wallSketchStartY + normalY * baselineOffset,
                        m_wallSketchElevation);
    const gp_Ax2 axes(origin, gp_Dir(0.0, 0.0, 1.0),
                      gp_Dir(dx / length, dy / length, 0.0));
    const TopoDS_Shape preview = BRepPrimAPI_MakeBox(
        axes, length, m_wallSketchThickness, m_wallSketchHeight).Shape();
    GeometryDisplayStyle style;
    style.red = 0.1;
    style.green = 0.85;
    style.blue = 1.0;
    style.transparency = 0.45;
    m_displayManager->appendShape(kWallSketchPreviewId, preview, style);
    m_displayManager->updateViewer();
}

void OccViewWidget::constrainWallSketchEndpoint(
    double& endX, double& endY, Qt::KeyboardModifiers modifiers) const
{
    double dx = endX - m_wallSketchStartX;
    double dy = endY - m_wallSketchStartY;
    double length = std::hypot(dx, dy);
    if (length <= 1.0e-12) return;
    double angle = std::atan2(dy, dx);
    if (std::isfinite(m_wallSketchExactAngleDegrees)) {
        angle = m_wallSketchExactAngleDegrees * 3.14159265358979323846 / 180.0;
    } else if (m_wallSketchOrthogonal || modifiers.testFlag(Qt::ShiftModifier)) {
        angle = std::abs(dx) >= std::abs(dy)
            ? (dx >= 0.0 ? 0.0 : 3.14159265358979323846)
            : (dy >= 0.0 ? 0.5 * 3.14159265358979323846
                         : -0.5 * 3.14159265358979323846);
    }
    if (m_wallSketchExactLength > 0.0) length = m_wallSketchExactLength;
    endX = m_wallSketchStartX + length * std::cos(angle);
    endY = m_wallSketchStartY + length * std::sin(angle);
}

bool OccViewWidget::commitWallSketchSegment(double endX, double endY,
                                            bool finishAfter)
{
    const double length = std::hypot(endX - m_wallSketchStartX,
                                     endY - m_wallSketchStartY);
    if (length <= 1.0e-8) return false;
    const double startX = m_wallSketchStartX;
    const double startY = m_wallSketchStartY;
    if (m_displayManager) m_displayManager->removeObject(kWallSketchPreviewId);
    m_wallSketchHasPreview = false;
    if (m_wallSketchContinuous && !finishAfter) {
        m_wallSketchStartX = endX;
        m_wallSketchStartY = endY;
        m_wallSketchHasStart = true;
    } else {
        m_wallSketchActive = false;
        m_wallSketchHasStart = false;
        unsetCursor();
    }
    emit wallSketchCompleted(startX, startY, endX, endY);
    return true;
}
