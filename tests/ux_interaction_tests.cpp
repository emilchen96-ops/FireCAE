#include "app/MainWindow.h"
#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FcFdsModel.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsBlockConversionService.h"
#include "fds/FdsWriter.h"
#include "geometry/FcGeometryObject.h"
#include "geometry/FcIfcObject.h"
#include "modeling/GeometryEditService.h"
#include "settings/ApplicationSettings.h"
#include "ui/ModelTreeWidget.h"
#include "ui/BuildingElementDialog.h"
#include "ui/UiLanguage.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <iostream>

namespace {
int failures=0;
void check(bool ok,const char* label) {std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;if(!ok)++failures;}
QAction* action(QMenu* menu,const char* name) {return menu->findChild<QAction*>(QString::fromLatin1(name));}
}
int main(int argc,char** argv)
{
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("FireCAE.UxInteractionTests");
    QCoreApplication::setApplicationName("UxInteractionTests");
    QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    qputenv("FIRECAE_DISABLE_RECOVERY_PROMPT","1");
    qputenv("FIRECAE_AUTOMATION_DISCARD_UNSAVED","1");
    qputenv("FIRECAE_DISABLE_LAYOUT_RESTORE","1");
    qputenv("FIRECAE_RECOVERY_DIRECTORY",temp.filePath("recovery").toUtf8());
    auto settings=ApplicationSettingsStore().load();
    settings.autoSaveEnabled=false; settings.language="en"; ApplicationSettingsStore().save(settings);
    FcProject fixture("Interaction");fixture.setChid("ux_interaction");fixture.setEndTime(0.1);
    BuildingGeometryRequest request;
    request.kind=FcGeometryKind::Box;request.width=1;request.depth=1;request.height=1;
    auto first=std::make_shared<FcGeometryObject>("Same Name",BuildingGeometryService::createShape(request));
    first->setGeometryKind(request.kind);first->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    request.x=2;
    auto second=std::make_shared<FcGeometryObject>("Same Name",BuildingGeometryService::createShape(request));
    second->setGeometryKind(request.kind);second->setGeometryParameters(BuildingGeometryService::requestToParameters(request));
    auto hidden=std::make_shared<FcIfcObject>("Hidden IFC","IFCWALL","fixture-global-id");
    hidden->setVisible(false);
    auto generic=std::make_shared<FcGeometryObject>("Raw CAD",BuildingGeometryService::createShape(request));
    generic->setGeometryKind(FcGeometryKind::Generic);
    fixture.document()->geometryGroup()->addChild(generic);
    fixture.document()->geometryGroup()->addChild(first);
    fixture.document()->geometryGroup()->addChild(second);
    fixture.document()->geometryGroup()->addChild(hidden);
    auto mesh=std::make_shared<FcFdsMesh>("Mesh","MESH",std::array<int,3>{8,8,8},FcFdsBounds{0,4,0,4,0,4});
    fixture.document()->meshesGroup()->addChild(mesh);
    auto converted=FdsBlockConversionService::convert({first,second},{mesh});
    for(const auto& object:converted.fdsObjects) fixture.document()->geometryGroup()->addChild(object);
    const QString input=temp.filePath("input.firecae"),saved=temp.filePath("saved.firecae");
    QString error;
    check(FcProjectSerializer::save(fixture,input,&error),"Fixture saved");
    MainWindow window; // Never show a native OCCT window.
    check(window.openProjectFile(input),"Fixture opens offscreen");
    auto* tree=window.findChild<ModelTreeWidget*>();
    auto* undo=window.findChild<QUndoStack*>();
    check(tree && undo,"Production selection and undo are available");
    {
        std::unique_ptr<QMenu> menu(window.createModelContextMenu({first->id(),second->id()}));
        check(tree->selectedObjectIds().size()==2,"Context menu preserves multi selection");
        check(action(menu.get(),"ContextHideSelected") && action(menu.get(),"ContextIsolateSelected"),"Multi context has real visibility commands");
        check(!action(menu.get(),"ContextProperties"),"Multi context does not edit an arbitrary member");
        action(menu.get(),"ContextIsolateSelected")->trigger();
    }
    {
        std::unique_ptr<QMenu> menu(window.createModelContextMenu({}));
        check(action(menu.get(),"ContextShowAll") && action(menu.get(),"ContextExitIsolation"),"Blank context exposes both distinct recovery commands");
        action(menu.get(),"ContextExitIsolation")->trigger();
        check(window.saveProjectFile(saved),"Save after isolation");
        auto loaded=FcProjectSerializer::load(saved);
        check(loaded.success() && !loaded.project->document()->findObject(hidden->id())->isVisible(),"Exit isolation preserves previously hidden objects");
        action(menu.get(),"ContextShowAll")->trigger();
        window.saveProjectFile(saved); loaded=FcProjectSerializer::load(saved);
        check(loaded.success() && loaded.project->document()->findObject(hidden->id())->isVisible(),"Show all explicitly restores hidden objects");
    }
    {
        std::unique_ptr<QMenu> menu(window.createModelContextMenu({first->id(),second->id()}));
        action(menu.get(),"ContextHideSelected")->trigger();
        window.saveProjectFile(saved);auto loaded=FcProjectSerializer::load(saved);
        check(!loaded.project->document()->findObject(first->id())->isVisible() &&
              !loaded.project->document()->findObject(second->id())->isVisible(),"Hide applies to every selected UUID");
        check(FdsWriter::render(*loaded.project).text==FdsWriter::render(fixture).text,"Visibility changes leave FDS input identical");
        std::unique_ptr<QMenu> blank(window.createModelContextMenu({}));
        action(blank.get(),"ContextShowAll")->trigger();
    }
    QTimer::singleShot(0,[&]() {
        auto* dialog=qobject_cast<BuildingElementDialog*>(QApplication::activeModalWidget());
        check(dialog!=nullptr,"UUID property entry opens editable geometry dialog");
        if(dialog) {
            auto* x=dialog->findChild<QDoubleSpinBox*>("BuildingStartXSpin");
            check(x && x->value()==2,"Same-name object opens the requested UUID");
            if(x)x->setValue(9);
            dialog->reject();
        }
    });
    window.openObjectProperties(second->id());
    window.saveProjectFile(saved);
    auto loaded=FcProjectSerializer::load(saved);
    auto savedSecond=std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(second->id()));
    check(savedSecond->geometryParameters().value("x").toDouble()==2,"Cancel leaves persisted geometry unchanged");
    {
        QTimer::singleShot(0,[&]() {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog && dialog->property("objectId").toString()==hidden->id(),"IFC properties is read-only and UUID-bound");
            if(dialog)dialog->reject();
        });
        window.openObjectProperties(hidden->id());
    }
    {
        QTimer::singleShot(0,[&]() {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(dialog && dialog->property("objectId").toString()==generic->id() &&
                  !qobject_cast<BuildingElementDialog*>(dialog),"Generic CAD is not coerced into a parametric box");
            if(dialog)dialog->reject();
        });
        window.openObjectProperties(generic->id());
    }
    const auto before=BuildingGeometryService::requestFromParameters(first->geometryKind(),first->geometryParameters());
    BuildingGeometryRequest after;
    check(GeometryEditService::moveHandle(before,"X+",1,0,&after,&error),"One-sided parameter proposal valid");
    const int count=undo->count();
    check(window.commitGeometryParameters(first->id(),BuildingGeometryService::requestToParameters(after),&error),"Direct proposal commits through production transaction");
    if(!error.isEmpty())std::cout<<error.toStdString()<<std::endl;
    check(undo->count()==count+1,"One edit adds exactly one undo step");
    window.saveProjectFile(saved);loaded=FcProjectSerializer::load(saved);
    auto edited=std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(first->id()));
    check(edited->geometryParameters().value("width").toDouble()==2,"Committed width survives save/reopen");
    const QString afterFds=FdsWriter::render(*loaded.project).text;
    check(afterFds!=FdsWriter::render(fixture).text,"Existing derived FDS follows edited dimensions");
    undo->undo();window.saveProjectFile(saved);loaded=FcProjectSerializer::load(saved);
    edited=std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(first->id()));
    check(edited->geometryParameters().value("width").toDouble()==1,"Undo restores original width");
    check(FdsWriter::render(*loaded.project).text==FdsWriter::render(fixture).text,"Undo restores derived FDS exactly");
    undo->redo();window.saveProjectFile(saved);loaded=FcProjectSerializer::load(saved);
    check(FdsWriter::render(*loaded.project).text==afterFds,"Redo restores geometry and derived FDS");
    edited=std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(first->id()));
    auto current=BuildingGeometryService::requestFromParameters(edited->geometryKind(),edited->geometryParameters());
    gp_Trsf transform;transform.SetScale(gp_Pnt(),1.25);transform.SetTranslationPart(gp_Vec(0.2,0.3,0.1));
    BuildingGeometryRequest transformed;
    check(GeometryEditService::transformRequest(current,transform,&transformed,&error),"Whole scale and move create consistent parameters");
    check(window.commitGeometryParameters(first->id(),BuildingGeometryService::requestToParameters(transformed),&error),
          "Whole transform uses the same derived-FDS transaction");
    window.saveProjectFile(saved);loaded=FcProjectSerializer::load(saved);
    edited=std::dynamic_pointer_cast<FcGeometryObject>(loaded.project->document()->findObject(first->id()));
    check(GeometryEditService::matchesShape(transformed,edited->shape()),"Transformed shape and saved parameters agree");
    undo->undo();window.saveProjectFile(saved);loaded=FcProjectSerializer::load(saved);
    check(FdsWriter::render(*loaded.project).text==afterFds,"Whole transform undo restores derived FDS exactly");
    std::cout<<"Failures: "<<failures<<std::endl;
    return failures?1:0;
}
