#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FdsImporter.h"
#include "fds/FdsWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        std::cerr << "usage: FireCAEFdsRoundTrip input.fds output.fds\n";
        return 2;
    }
    FdsImporter importer;
    FdsImportResult imported = importer.importFile(application.arguments().at(1));
    if (!imported.success()) {
        std::cerr << imported.errorMessage.toStdString() << '\n';
        return 3;
    }
    QString error;
    if (!FdsWriter::writeFile(*imported.project, application.arguments().at(2), &error)) {
        std::cerr << error.toStdString() << '\n';
        return 4;
    }
    std::cout << "CHID=" << imported.project->chid().toStdString()
              << " OBJECTS=" << imported.objectCount;
    for (const auto& group : imported.project->document()->groups()) {
        if (!group->children().empty()) {
            std::cout << ' ' << group->name().toStdString() << '='
                      << group->children().size();
        }
    }
    std::cout << " WARNINGS=" << imported.warnings.size() << '\n';
    return 0;
}
