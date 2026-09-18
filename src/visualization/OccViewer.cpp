#include "visualization/OccViewer.h"

#include <AIS_DisplayMode.hxx>
#include <Graphic3d_TypeOfShadingModel.hxx>
#include <Quantity_Color.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Standard_Failure.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <WNT_Window.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <gp_Pln.hxx>

#include <array>
#include <cmath>

OccViewer::~OccViewer()
{
    if (!m_view.IsNull()) {
        if (!m_clippingPlane.IsNull()) m_view->RemoveClipPlane(m_clippingPlane);
        for (const Handle(Graphic3d_ClipPlane)& plane : m_clippingBoxPlanes) {
            if (!plane.IsNull()) m_view->RemoveClipPlane(plane);
        }
        m_clippingBoxPlanes.clear();
        m_view->Remove();
    }
    m_savedCamera.Nullify();
    m_clippingPlane.Nullify();
    m_window.Nullify();
    m_view.Nullify();
    m_context.Nullify();
    m_viewer.Nullify();
    m_graphicDriver.Nullify();
    m_displayConnection.Nullify();
}

bool OccViewer::initialize(void* nativeWindowHandle)
{
    if (isInitialized()) {
        return true;
    }
    if (!nativeWindowHandle) {
        return false;
    }

    try {
        m_displayConnection = new Aspect_DisplayConnection();
        m_graphicDriver = new OpenGl_GraphicDriver(m_displayConnection);
        m_viewer = new V3d_Viewer(m_graphicDriver);
        m_viewer->SetDefaultLights();
        m_viewer->SetLightOn();

        m_context = new AIS_InteractiveContext(m_viewer);
        m_context->SetDisplayMode(AIS_Shaded, Standard_False);

        // Make selected and detected objects unmistakable against both dark
        // backgrounds and translucent FDS geometry.
        const Quantity_Color selectedColor(1.0, 0.45, 0.0, Quantity_TOC_RGB);
        const Handle(Prs3d_Drawer)& selectedStyle = m_context->SelectionStyle();
        selectedStyle->SetColor(selectedColor);
        selectedStyle->SetDisplayMode(AIS_Shaded);
        selectedStyle->SetTransparency(0.0f);
        selectedStyle->SetFaceBoundaryDraw(Standard_True);
        selectedStyle->SetFaceBoundaryAspect(new Prs3d_LineAspect(
            selectedColor, Aspect_TOL_SOLID, 4.0));
        selectedStyle->SetLineAspect(new Prs3d_LineAspect(
            selectedColor, Aspect_TOL_SOLID, 4.0));

        const Quantity_Color detectedColor(1.0, 0.95, 0.1, Quantity_TOC_RGB);
        const Handle(Prs3d_Drawer)& detectedStyle = m_context->HighlightStyle();
        detectedStyle->SetColor(detectedColor);
        detectedStyle->SetDisplayMode(AIS_Shaded);
        detectedStyle->SetFaceBoundaryDraw(Standard_True);
        detectedStyle->SetFaceBoundaryAspect(new Prs3d_LineAspect(
            detectedColor, Aspect_TOL_SOLID, 2.5));
        detectedStyle->SetLineAspect(new Prs3d_LineAspect(
            detectedColor, Aspect_TOL_SOLID, 2.5));

        m_view = m_viewer->CreateView();
        m_window = new WNT_Window(reinterpret_cast<Aspect_Handle>(nativeWindowHandle));
        m_view->SetWindow(m_window);
        if (!m_window->IsMapped()) {
            m_window->Map();
        }

        m_view->SetBackgroundColor(
            Quantity_Color(0.12, 0.14, 0.18, Quantity_TOC_RGB));
        m_view->SetShadingModel(Graphic3d_TOSM_FRAGMENT);
        m_view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight);
        m_view->MustBeResized();
        m_view->Redraw();
        return true;
    } catch (const Standard_Failure&) {
        m_window.Nullify();
        m_view.Nullify();
        m_context.Nullify();
        m_viewer.Nullify();
        m_graphicDriver.Nullify();
        m_displayConnection.Nullify();
        return false;
    }
}

bool OccViewer::isInitialized() const
{
    return !m_context.IsNull() && !m_view.IsNull();
}

const Handle(AIS_InteractiveContext)& OccViewer::context() const
{
    return m_context;
}

const Handle(V3d_View)& OccViewer::view() const
{
    return m_view;
}

void OccViewer::resize()
{
    if (isInitialized()) {
        m_view->MustBeResized();
        m_view->Invalidate();
        m_view->Redraw();
    }
}

void OccViewer::redraw()
{
    if (isInitialized()) {
        m_view->Redraw();
    }
}

void OccViewer::fitAll()
{
    if (!isInitialized()) {
        return;
    }
    m_view->FitAll();
    m_view->ZFitAll();
    m_view->Redraw();
}

void OccViewer::setOrientation(OccViewOrientation orientation)
{
    if (!isInitialized()) {
        return;
    }

    V3d_TypeOfOrientation projection = V3d_TypeOfOrientation_Zup_AxoRight;
    switch (orientation) {
    case OccViewOrientation::Front:
        projection = V3d_TypeOfOrientation_Zup_Front;
        break;
    case OccViewOrientation::Back:
        projection = V3d_TypeOfOrientation_Zup_Back;
        break;
    case OccViewOrientation::Left:
        projection = V3d_TypeOfOrientation_Zup_Left;
        break;
    case OccViewOrientation::Right:
        projection = V3d_TypeOfOrientation_Zup_Right;
        break;
    case OccViewOrientation::Top:
        projection = V3d_TypeOfOrientation_Zup_Top;
        break;
    case OccViewOrientation::Bottom:
        projection = V3d_TypeOfOrientation_Zup_Bottom;
        break;
    case OccViewOrientation::Isometric:
        projection = V3d_TypeOfOrientation_Zup_AxoRight;
        break;
    }

    m_view->SetProj(projection);
    fitAll();
}

void OccViewer::setProjectionDirection(double x, double y, double z)
{
    if (!isInitialized()) return;
    const double magnitude = std::sqrt(x * x + y * y + z * z);
    if (magnitude <= 1.0e-12) return;
    m_view->SetProj(x / magnitude, y / magnitude, z / magnitude);
    m_view->Redraw();
}

void OccViewer::setPerspective(bool perspective)
{
    if (!isInitialized()) return;
    m_view->Camera()->SetProjectionType(
        perspective ? Graphic3d_Camera::Projection_Perspective
                    : Graphic3d_Camera::Projection_Orthographic);
    m_view->Redraw();
}

bool OccViewer::isPerspective() const
{
    return isInitialized() && !m_view->Camera()->IsOrthographic();
}

void OccViewer::showTrihedron(bool visible)
{
    if (!isInitialized()) return;
    if (visible) {
        m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_WHITE,
                               0.08, V3d_ZBUFFER);
    } else {
        m_view->TriedronErase();
    }
    m_view->Redraw();
}

void OccViewer::setBackgroundColor(double red, double green, double blue)
{
    if (!isInitialized()) return;
    m_view->SetBackgroundColor(Quantity_Color(red, green, blue, Quantity_TOC_RGB));
    m_view->Redraw();
}

void OccViewer::saveCamera()
{
    if (!isInitialized()) return;
    m_savedCamera = new Graphic3d_Camera(m_view->Camera());
}

bool OccViewer::restoreCamera()
{
    if (!isInitialized() || m_savedCamera.IsNull()) return false;
    m_view->SetCamera(new Graphic3d_Camera(m_savedCamera));
    m_view->Redraw();
    return true;
}

void OccViewer::setRotationCenter(double x, double y, double z)
{
    if (!isInitialized()) return;
    m_view->Camera()->SetCenter(gp_Pnt(x, y, z));
    m_view->Redraw();
}

void OccViewer::setClippingPlane(bool enabled, int axis, double position)
{
    if (!isInitialized()) return;
    if (!m_clippingPlane.IsNull()) {
        m_view->RemoveClipPlane(m_clippingPlane);
        m_clippingPlane.Nullify();
    }
    if (enabled) {
        gp_Dir normal(1.0, 0.0, 0.0);
        gp_Pnt point(position, 0.0, 0.0);
        if (axis == 1) { normal = gp_Dir(0.0, 1.0, 0.0); point = gp_Pnt(0.0, position, 0.0); }
        if (axis == 2) { normal = gp_Dir(0.0, 0.0, 1.0); point = gp_Pnt(0.0, 0.0, position); }
        m_clippingPlane = new Graphic3d_ClipPlane(gp_Pln(point, normal));
        m_clippingPlane->SetOn(Standard_True);
        m_view->AddClipPlane(m_clippingPlane);
    }
    m_view->Redraw();
}

void OccViewer::setClippingBox(bool enabled, double xMin, double xMax,
                               double yMin, double yMax,
                               double zMin, double zMax)
{
    if (!isInitialized()) return;
    for (const Handle(Graphic3d_ClipPlane)& plane : m_clippingBoxPlanes) {
        if (!plane.IsNull()) m_view->RemoveClipPlane(plane);
    }
    m_clippingBoxPlanes.clear();
    if (enabled && xMax > xMin && yMax > yMin && zMax > zMin) {
        const std::array<gp_Pln, 6> planes = {
            gp_Pln(gp_Pnt(xMin, 0.0, 0.0), gp_Dir(-1.0, 0.0, 0.0)),
            gp_Pln(gp_Pnt(xMax, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
            gp_Pln(gp_Pnt(0.0, yMin, 0.0), gp_Dir(0.0, -1.0, 0.0)),
            gp_Pln(gp_Pnt(0.0, yMax, 0.0), gp_Dir(0.0, 1.0, 0.0)),
            gp_Pln(gp_Pnt(0.0, 0.0, zMin), gp_Dir(0.0, 0.0, -1.0)),
            gp_Pln(gp_Pnt(0.0, 0.0, zMax), gp_Dir(0.0, 0.0, 1.0))};
        for (const gp_Pln& equation : planes) {
            Handle(Graphic3d_ClipPlane) plane = new Graphic3d_ClipPlane(equation);
            m_view->AddClipPlane(plane);
            m_clippingBoxPlanes.push_back(plane);
        }
    }
    m_view->Redraw();
}
