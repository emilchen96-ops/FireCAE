#include "modeling/GeometryEditService.h"
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <cmath>
#include <algorithm>

namespace {
using Vec = std::array<double, 3>;
Vec add(Vec a, Vec b, double s = 1) { for (int i=0;i<3;++i) a[i]+=s*b[i]; return a; }
Vec unit(Vec v) {
    const double n=std::hypot(v[0],v[1],v[2]);
    if (n>1e-12) for (double& x:v) x/=n;
    return v;
}
Vec cross(Vec a, Vec b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
QVector<Vec> profile(const BuildingGeometryRequest& r) {
    if (!r.profile3d.isEmpty()) return r.profile3d;
    QVector<Vec> p; for (const auto& xy:r.profile) p.append({xy.x(),xy.y(),r.z}); return p;
}
Vec normal(const QVector<Vec>& p) {
    Vec n{};
    for (qsizetype i=0;i<p.size();++i) n=add(n,cross(p[i],p[(i+1)%p.size()]));
    return unit(n);
}
}

QVector<GeometryEditHandle> GeometryEditService::handles(const BuildingGeometryRequest& r)
{
    QVector<GeometryEditHandle> out;
    const Vec z{0,0,1};
    if (r.kind==FcGeometryKind::Box || r.kind==FcGeometryKind::Slab) {
        const double a=r.rotationDegrees*std::acos(-1.0)/180;
        const Vec x{std::cos(a),std::sin(a),0}, y{-std::sin(a),std::cos(a),0};
        const Vec center=add(add(add({r.x,r.y,r.z},x,r.width/2),y,r.depth/2),z,r.height/2);
        out={{"X-",add(center,x,-r.width/2),x},{"X+",add(center,x,r.width/2),x},
             {"Y-",add(center,y,-r.depth/2),y},{"Y+",add(center,y,r.depth/2),y},
             {"Z-",add(center,z,-r.height/2),z},{"Z+",add(center,z,r.height/2),z}};
    } else if (r.kind==FcGeometryKind::Wall && r.path.isEmpty()) {
        const Vec t=unit({r.endX-r.x,r.endY-r.y,0}), n{-t[1],t[0],0};
        const double shift=r.baseline==FcWallBaseline::Left ? r.thickness/2 :
                           r.baseline==FcWallBaseline::Right ? -r.thickness/2 : 0;
        const Vec start=add({r.x,r.y,r.z+r.height/2},n,shift);
        const Vec end=add({r.endX,r.endY,r.z+r.height/2},n,shift);
        const Vec middle{(start[0]+end[0])/2,(start[1]+end[1])/2,r.z+r.height/2};
        out={{"Start",start,t},{"End",end,t},
             {"Thickness-",add(middle,n,-r.thickness/2),n},
             {"Thickness+",add(middle,n,r.thickness/2),n},
             {"Z-",add(middle,z,-r.height/2),z},{"Z+",add(middle,z,r.height/2),z}};
    } else if (r.kind==FcGeometryKind::PolygonPrism || r.kind==FcGeometryKind::ProfileExtrusion) {
        const auto p=profile(r);
        if (p.size()<3) return out;
        const Vec n=normal(p), u=unit(add(p[1],p[0],-1)), v=unit(cross(n,u));
        Vec center{};
        for (int i=0;i<p.size();++i) {
            center=add(center,p[i],1.0/p.size());
            // Separate in-plane U/V handles keep editing planar, including tilted profiles.
            out.append({QStringLiteral("Point:%1:U").arg(i),p[i],u});
            out.append({QStringLiteral("Point:%1:V").arg(i),p[i],v});
        }
        const Vec d=r.profile3d.isEmpty() ? z : (r.extrusionNormal ? n : unit(r.extrusionDirection));
        const double distance=r.profile3d.isEmpty() ? r.height : r.extrusionDistance;
        out.append({"Extrusion",add(center,d,distance),d});
    }
    return out;
}

bool GeometryEditService::moveHandle(const BuildingGeometryRequest& before, const QString& key,
                                    double delta, double snapStep, BuildingGeometryRequest* after,
                                    QString* error)
{
    if (error) error->clear();
    const auto fail=[error](const char* text) { if(error)*error=QString::fromLatin1(text); return false; };
    if (!after || !std::isfinite(delta) || !std::isfinite(snapStep) || snapStep<0)
        return fail("Invalid edit displacement or snap increment.");
    const auto available=handles(before);
    const auto found=std::find_if(available.cbegin(),available.cend(),[&](const auto& h){return h.key==key;});
    if (found==available.cend()) return fail("This geometry does not support the requested edit handle.");
    if (snapStep>0) delta=std::round(delta/snapStep)*snapStep;
    BuildingGeometryRequest r=before;
    const Vec d=found->direction;
    if (key.startsWith(QStringLiteral("Point:"))) {
        r.profile3d=profile(before);
        const int index=key.split(':')[1].toInt();
        r.profile3d[index]=add(r.profile3d[index],d,delta);
        if (before.profile3d.isEmpty()) {
            r.extrusionNormal=false; r.extrusionDirection={0,0,1}; r.extrusionDistance=before.height;
        }
    } else if (key==QStringLiteral("Extrusion")) {
        if (r.profile3d.isEmpty()) r.height+=delta;
        else r.extrusionDistance+=delta;
    } else if (key.startsWith('Z')) {
        if (key.endsWith('-')) {r.z+=delta; r.height-=delta;} else r.height+=delta;
        if (r.kind==FcGeometryKind::Slab) r.thickness=r.height;
    } else if (r.kind==FcGeometryKind::Wall) {
        if (key==QStringLiteral("Start")) {r.x+=d[0]*delta; r.y+=d[1]*delta;}
        else if (key==QStringLiteral("End")) {r.endX+=d[0]*delta; r.endY+=d[1]*delta;}
        else {
            const bool lower=key.endsWith('-');
            const double change=lower ? -delta : delta;
            const double baselineFactor=r.baseline==FcWallBaseline::Center ? -0.5 :
                                        r.baseline==FcWallBaseline::Right ? -1.0 : 0.0;
            // Keep the opposite face fixed regardless of the chosen wall baseline.
            const double shift=(lower ? delta : 0.0)-baselineFactor*change;
            r.thickness+=change;
            r.x+=d[0]*shift; r.y+=d[1]*shift;
            r.endX+=d[0]*shift; r.endY+=d[1]*shift;
        }
        const double along=(r.endX-r.x)* (before.endX-before.x)+(r.endY-r.y)*(before.endY-before.y);
        if (along<=1e-12) return fail("A wall endpoint cannot cross its fixed endpoint.");
    } else {
        double& extent=key.startsWith('X') ? r.width : r.depth;
        if (key.endsWith('-')) {r.x+=d[0]*delta;r.y+=d[1]*delta;extent-=delta;}
        else extent+=delta;
    }
    if (r.width<=1e-7 || r.depth<=1e-7 || r.height<=1e-7 || r.thickness<=1e-7 ||
        (!r.profile3d.isEmpty() && r.extrusionDistance<=1e-7))
        return fail("Dimensions must remain positive; a moving face cannot cross its fixed face.");
    QString shapeError;
    if (BuildingGeometryService::createShape(r,&shapeError).IsNull()) {
        if(error)*error=shapeError; return false;
    }
    *after=r;
    return true;
}

bool GeometryEditService::transformRequest(const BuildingGeometryRequest& before,
                                           const gp_Trsf& transform,
                                           BuildingGeometryRequest* after,
                                           QString* error)
{
    if (error) error->clear();
    const auto fail = [error](const char* message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    if (!after) return fail("A transformed geometry request is required.");
    for (int row = 1; row <= 3; ++row)
        for (int column = 1; column <= 4; ++column)
            if (!std::isfinite(transform.Value(row, column)))
                return fail("Transform values must be finite numbers.");
    const double scale = transform.ScaleFactor();
    if (!std::isfinite(scale) || scale <= 1.0e-12 || transform.IsNegative())
        return fail("Parametric transforms require a positive uniform scale and cannot mirror geometry.");
    const bool isBox = before.kind == FcGeometryKind::Box || before.kind == FcGeometryKind::Slab;
    const bool isWall = before.kind == FcGeometryKind::Wall && before.path.isEmpty();
    const bool isProfile = before.kind == FcGeometryKind::PolygonPrism || before.kind == FcGeometryKind::ProfileExtrusion;
    if (!isBox && !isWall && !isProfile)
        return fail("This geometry kind does not support parameter-preserving transforms.");
    try {
        const gp_Vec xAxis = gp_Vec(1., 0., 0.).Transformed(transform) / scale;
        const gp_Vec yAxis = gp_Vec(0., 1., 0.).Transformed(transform) / scale;
        const gp_Vec zAxis = gp_Vec(0., 0., 1.).Transformed(transform) / scale;
        if (std::abs(xAxis.Magnitude() - 1.) > 1.0e-10 ||
            std::abs(yAxis.Magnitude() - 1.) > 1.0e-10 ||
            std::abs(zAxis.Magnitude() - 1.) > 1.0e-10 ||
            std::abs(xAxis.Dot(yAxis)) > 1.0e-10 ||
            std::abs(xAxis.Dot(zAxis)) > 1.0e-10 ||
            std::abs(yAxis.Dot(zAxis)) > 1.0e-10 || xAxis.Crossed(yAxis).Dot(zAxis) < 0.)
            return fail("Parametric transforms require a positive uniform scale and a proper rotation.");
        const bool preservesUp = (zAxis - gp_Vec(0., 0., 1.)).Magnitude() <= 1.0e-10;
        if ((isBox || isWall) && !preservesUp)
            return fail("Boxes, slabs and straight walls support rotation about world Z only.");
        QString shapeError;
        if (BuildingGeometryService::createShape(before, &shapeError).IsNull()) {
            if (error) *error = shapeError;
            return false;
        }
        BuildingGeometryRequest result = before;
        const gp_Pnt origin = gp_Pnt(before.x, before.y, before.z).Transformed(transform);
        const gp_Pnt endpoint = gp_Pnt(before.endX, before.endY, before.z).Transformed(transform);
        result.x = origin.X(); result.y = origin.Y(); result.z = origin.Z();
        result.endX = endpoint.X(); result.endY = endpoint.Y();
        result.width *= scale; result.depth *= scale; result.height *= scale;
        result.thickness *= scale; result.radius *= scale; result.rise *= scale;
        result.extrusionDistance *= scale;
        if (preservesUp) {
            const double rotation = std::atan2(xAxis.Y(), xAxis.X()) * 180. / std::acos(-1.);
            result.rotationDegrees = std::remainder(before.rotationDegrees + rotation, 360.);
        }
        if (isProfile) {
            if (before.profile3d.isEmpty() && preservesUp) {
                result.profile.clear();
                for (const QPointF& point : before.profile) {
                    const gp_Pnt transformed = gp_Pnt(point.x(), point.y(), before.z).Transformed(transform);
                    result.profile.append(QPointF(transformed.X(), transformed.Y()));
                }
                // Legacy profiles extrude along world Z by height.
                result.extrusionDistance = result.height;
            } else {
                result.profile3d.clear();
                for (const auto& point : profile(before)) {
                    const gp_Pnt transformed = gp_Pnt(point[0], point[1], point[2]).Transformed(transform);
                    result.profile3d.append({transformed.X(), transformed.Y(), transformed.Z()});
                }
                result.profile.clear();
                gp_Vec direction(before.extrusionDirection[0], before.extrusionDirection[1], before.extrusionDirection[2]);
                if (before.profile3d.isEmpty()) {
                    result.extrusionNormal = false;
                    result.extrusionDistance = result.height;
                    direction = gp_Vec(0., 0., 1.);
                }
                direction.Transform(transform);
                if (direction.Magnitude() > 1.0e-12) direction.Normalize();
                result.extrusionDirection = {direction.X(), direction.Y(), direction.Z()};
            }
        }
        if (BuildingGeometryService::createShape(result, &shapeError).IsNull()) {
            if (error) *error = shapeError;
            return false;
        }
        *after = result;
        return true;
    } catch (const Standard_Failure&) {
        return fail("OpenCascade could not preserve geometry parameters for this transform.");
    }
}

bool GeometryEditService::matchesShape(const BuildingGeometryRequest& request,
                                       const TopoDS_Shape& actual,
                                       double tolerance, QString* error)
{
    if (error) error->clear();
    const auto fail = [error]() {
        if (error) *error = QStringLiteral(
            "Stored geometry parameters do not match the current shape; rebuild or explicitly convert the geometry before parametric editing.");
        return false;
    };
    if (actual.IsNull() || !std::isfinite(tolerance) || tolerance <= 0.) return fail();
    try {
        const TopoDS_Shape expected = BuildingGeometryService::createShape(request);
        if (expected.IsNull()) return fail();
        Bnd_Box expectedBounds, actualBounds;
        BRepBndLib::AddOptimal(expected, expectedBounds, false, false);
        BRepBndLib::AddOptimal(actual, actualBounds, false, false);
        if (expectedBounds.IsVoid() || actualBounds.IsVoid()) return fail();
        double a[6], b[6];
        expectedBounds.Get(a[0], a[1], a[2], a[3], a[4], a[5]);
        actualBounds.Get(b[0], b[1], b[2], b[3], b[4], b[5]);
        for (int i = 0; i < 6; ++i)
            if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i] - b[i]) > tolerance) return fail();
        const double length = std::max(1., expectedBounds.CornerMin().Distance(expectedBounds.CornerMax()));
        GProp_GProps expectedVolume, actualVolume, expectedArea, actualArea;
        BRepGProp::VolumeProperties(expected, expectedVolume);
        BRepGProp::VolumeProperties(actual, actualVolume);
        BRepGProp::SurfaceProperties(expected, expectedArea);
        BRepGProp::SurfaceProperties(actual, actualArea);
        if (!std::isfinite(actualVolume.Mass()) || !std::isfinite(actualArea.Mass()) ||
            std::abs(expectedVolume.Mass() - actualVolume.Mass()) > tolerance * length * length ||
            std::abs(expectedArea.Mass() - actualArea.Mass()) > tolerance * length ||
            expectedVolume.CentreOfMass().Distance(actualVolume.CentreOfMass()) > tolerance ||
            expectedArea.CentreOfMass().Distance(actualArea.CentreOfMass()) > tolerance) return fail();
        TopTools_IndexedMapOfShape expectedFaces, actualFaces;
        TopExp::MapShapes(expected, TopAbs_FACE, expectedFaces);
        TopExp::MapShapes(actual, TopAbs_FACE, actualFaces);
        if (expectedFaces.Extent() != actualFaces.Extent()) return fail();
        QVector<GProp_GProps> actualProperties;
        for (int i = 1; i <= actualFaces.Extent(); ++i) {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(actualFaces(i), properties);
            actualProperties.append(properties);
        }
        QVector<bool> used(actualProperties.size(), false);
        for (int i = 1; i <= expectedFaces.Extent(); ++i) {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(expectedFaces(i), properties);
            bool found = false;
            for (qsizetype j = 0; j < actualProperties.size(); ++j) {
                if (!used[j] && std::abs(properties.Mass() - actualProperties[j].Mass()) <= tolerance * length &&
                    properties.CentreOfMass().Distance(actualProperties[j].CentreOfMass()) <= tolerance) {
                    found = true; used[j] = true; break;
                }
            }
            if (!found) return fail();
        }
        return true;
    } catch (const Standard_Failure&) {
        return fail();
    }
}
