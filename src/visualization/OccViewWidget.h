#pragma once
#include "modeling/GeometryEditService.h"
#include <AIS_Shape.hxx>
#include <AIS_Point.hxx>

#include "visualization/OccViewer.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <limits>
#include <memory>

class GeometryDisplayManager;
class QMouseEvent;
class QKeyEvent;
class QPaintEngine;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;
class QRubberBand;
class QColor;

class OccViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit OccViewWidget(QWidget* parent = nullptr);
    ~OccViewWidget() override;

    bool isInitialized() const;
    GeometryDisplayManager* displayManager() const;

    void fitAll();
    void setOrientation(OccViewOrientation orientation);
    void setProjectionDirection(double x, double y, double z);
    void setPerspective(bool perspective);
    void showTrihedron(bool visible);
    void setBackgroundColor(const QColor& color);
    void saveView();
    bool restoreView();
    void setRotationCenter(double x, double y, double z);
    bool fitSelection();
    // Hit-test without replacing an existing multi-selection when its member
    // is right-clicked. Blank space retains selection but returns an empty ID.
    QString prepareContextSelection(const QPoint& position);
    void showGeometryPreview(const TopoDS_Shape& shape);
    void clearGeometryPreview();
    bool beginDirectEditing(const QString& objectId, const BuildingGeometryRequest& request,
                            double snapStep = 0.0);
    void endDirectEditing();
    QString directEditingObjectId() const;
    void setClippingPlane(bool enabled, int axis, double position);
    void setClippingBox(bool enabled, double xMin, double xMax,
                        double yMin, double yMax, double zMin, double zMax);
    QStringList selectRectangle(const QRect& widgetRectangle, bool toggle = false);
    bool saveViewImage(const QString& filePath) const;
    bool showTransformManipulator(const QStringList& objectIds);
    void hideTransformManipulator();
    bool isTransformManipulatorVisible() const;
    void beginWallSketch(double elevation,
                         double thickness,
                         double height,
                         int baseline,
                         bool snapEnabled,
                         double snapStep,
                         bool continuous = false,
                         bool orthogonal = false,
                         double exactLength = 0.0,
                         double exactAngleDegrees = std::numeric_limits<double>::quiet_NaN());
    void setWallSketchSnapPoints(const QVector<QPointF>& points);
    void cancelWallSketch();
    bool isWallSketchActive() const;

signals:
    void objectActivated(const QString& objectId);
    void geometryEditRequested(const QString& objectId, const QVariantMap& parameters);
    void directEditStatus(const QString& text);
    void directEditEnded();
    void viewerInitializationFinished(bool success);
    void objectSelected(const QString& objectId);
    void objectsSelected(const QStringList& objectIds);
    void selectionCleared();
    void manipulatorTransformFinished(const QStringList& objectIds);
    void wallSketchCompleted(double startX, double startY,
                             double endX, double endY);
    void wallSketchCancelled();
    void wallSketchCursorChanged(double x, double y, double length,
                                 double angleDegrees, const QString& snapTarget);

protected:
    QPaintEngine* paintEngine() const override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class NavigationMode
    {
        None,
        Rotate,
        Pan
    };

    void initializeViewer();
    void synchronizeViewerSize();
    void scheduleViewerSizeSynchronization();
    QPoint viewerPixelPosition(const QPoint& widgetPosition) const;
    bool pointOnSketchPlane(const QPoint& widgetPosition,
                            double& worldX,
                            double& worldY);
    void updateWallSketchPreview(double endX, double endY);
    void constrainWallSketchEndpoint(double& endX, double& endY,
                                     Qt::KeyboardModifiers modifiers) const;
    bool commitWallSketchSegment(double endX, double endY, bool finishAfter);
    int directHandleAt(const QPoint& position) const;
    QPointF directHandleScreenPoint(int index) const;
    void rebuildDirectHandles();

    std::unique_ptr<OccViewer> m_viewer;
    std::unique_ptr<GeometryDisplayManager> m_displayManager;
    NavigationMode m_navigationMode = NavigationMode::None;
    QPoint m_lastMousePosition;
    QPoint m_leftPressPosition;
    bool m_leftButtonPressed = false;
    bool m_leftClickMoved = false;
    bool m_manipulatorDragging = false;
    bool m_wallSketchActive = false;
    bool m_wallSketchHasStart = false;
    double m_wallSketchStartX = 0.0;
    double m_wallSketchStartY = 0.0;
    double m_wallSketchElevation = 0.0;
    double m_wallSketchThickness = 0.2;
    double m_wallSketchHeight = 3.0;
    double m_wallSketchSnapStep = 0.1;
    int m_wallSketchBaseline = 1;
    bool m_wallSketchSnapEnabled = true;
    bool m_wallSketchContinuous = false;
    bool m_wallSketchOrthogonal = false;
    double m_wallSketchExactLength = 0.0;
    double m_wallSketchExactAngleDegrees = std::numeric_limits<double>::quiet_NaN();
    double m_wallSketchLastX = 0.0;
    double m_wallSketchLastY = 0.0;
    bool m_wallSketchHasPreview = false;
    QString m_wallSketchLastSnapTarget;
    QVector<QPointF> m_wallSketchSnapPoints;
    QRubberBand* m_selectionBand = nullptr;
    bool m_initializationAttempted = false;
    Handle(AIS_Shape) m_geometryPreview;
    QVector<Handle(AIS_Point)> m_directPresentations;
    QVector<GeometryEditHandle> m_directHandles;
    QString m_directObjectId;
    BuildingGeometryRequest m_directBefore;
    BuildingGeometryRequest m_directDraft;
    int m_directHandle = -1;
    QPoint m_directStart;
    double m_directSnapStep = 0.0;
    bool m_directValid = false;
    bool m_directWasPerspective = false;
};
