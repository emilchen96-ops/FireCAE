#pragma once

#include <AIS_InteractiveContext.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_Window.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ClipPlane.hxx>

#include <vector>

enum class OccViewOrientation
{
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Isometric
};

class OccViewer final
{
public:
    OccViewer() = default;
    ~OccViewer();

    OccViewer(const OccViewer&) = delete;
    OccViewer& operator=(const OccViewer&) = delete;

    bool initialize(void* nativeWindowHandle);
    bool isInitialized() const;

    const Handle(AIS_InteractiveContext)& context() const;
    const Handle(V3d_View)& view() const;

    void resize();
    void redraw();
    void fitAll();
    void setOrientation(OccViewOrientation orientation);
    void setProjectionDirection(double x, double y, double z);
    void setPerspective(bool perspective);
    bool isPerspective() const;
    void showTrihedron(bool visible);
    void setBackgroundColor(double red, double green, double blue);
    void saveCamera();
    bool restoreCamera();
    void setRotationCenter(double x, double y, double z);
    void setClippingPlane(bool enabled, int axis, double position);
    void setClippingBox(bool enabled, double xMin, double xMax,
                        double yMin, double yMax, double zMin, double zMax);

private:
    Handle(Aspect_DisplayConnection) m_displayConnection;
    Handle(OpenGl_GraphicDriver) m_graphicDriver;
    Handle(V3d_Viewer) m_viewer;
    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View) m_view;
    Handle(Aspect_Window) m_window;
    Handle(Graphic3d_Camera) m_savedCamera;
    Handle(Graphic3d_ClipPlane) m_clippingPlane;
    std::vector<Handle(Graphic3d_ClipPlane)> m_clippingBoxPlanes;
};
