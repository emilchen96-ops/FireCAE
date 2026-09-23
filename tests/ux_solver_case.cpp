#include "core/FcProject.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "fds/FdsExamples.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsWriter.h"
#include "modeling/GeometryEditService.h"
#include "modeling/GeometryEditDependencyService.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    QDir output(QString::fromLocal8Bit(argv[1]));if(!output.mkpath("."))return 2;
    auto project=FdsExamples::createSimpleTestProject();
    project->setChid("ux_bcd_wall_opening");project->setEndTime(1.0);
    BuildingGeometryRequest wall;
    wall.kind=FcGeometryKind::Wall;wall.x=0.8;wall.y=0.5;wall.endX=2.8;wall.endY=0.5;
    wall.z=0;wall.height=2.0;wall.thickness=0.2;
    wall.extraParameters.insert("fdsAdditionalFields",QVariantMap{{"BNDF_OBST",".TRUE."},
        {"THICKEN",".TRUE."},{"PERMIT_HOLE",".TRUE."},{"ALLOW_VENT",".TRUE."},{"REMOVABLE",".FALSE."}});
    BuildingGeometryRequest edited;QString error;
    if(!GeometryEditService::moveHandle(wall,"Z+",0.2,0.1,&edited,&error))return 3;
    auto host=std::make_shared<FcGeometryObject>("Edited wall",BuildingGeometryService::createShape(edited));
    host->setGeometryKind(edited.kind);host->setGeometryParameters(BuildingGeometryService::requestToParameters(edited));
    BuildingGeometryRequest hole;
    hole.kind=FcGeometryKind::RectangularOpening;hole.x=1.4;hole.y=0.38;hole.z=0.1;
    hole.width=0.6;hole.depth=0.24;hole.height=1.5;
    if(!GeometryEditDependencyService::validateOpeningPlacement(hole,edited).isEmpty())return 3;
    auto opening=std::make_shared<FcGeometryObject>("Wall opening",BuildingGeometryService::createShape(hole));
    opening->setGeometryKind(hole.kind);opening->setGeometryParameters(BuildingGeometryService::requestToParameters(hole));
    opening->setHostObjectId(host->id());
    project->document()->geometryGroup()->addChild(host);project->document()->geometryGroup()->addChild(opening);
    QVector<std::shared_ptr<FcFdsMesh>> meshes;
    for(const auto& item:project->document()->meshesGroup()->children())
        if(auto mesh=std::dynamic_pointer_cast<FcFdsMesh>(item))meshes.append(mesh);
    const auto conversion=FdsBlockConversionService::convert({host,opening},meshes);
    if(!conversion.success())return 4;
    for(const auto& item:conversion.fdsObjects)project->document()->geometryGroup()->addChild(item);
    if(!FcProjectSerializer::save(*project,output.filePath("ux_bcd_wall_opening.firecae"),&error) ||
       !FdsWriter::writeFile(*project,output.filePath("ux_bcd_wall_opening.fds"),&error)) {
        std::cerr<<error.toStdString();return 5;
    }
    std::cout<<"Generated from geometry edit + conversion + production writer; no hand-edited FDS.\n";
    return 0;
}
