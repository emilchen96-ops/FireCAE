#include "fds/FdsExamples.h"
#include "fds/FcProjectSerializer.h"
#include "fds/FdsWriter.h"
#include "core/FcProject.h"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: FireCAEBenchmarkGenerator [case-name] <output.fds> "
                     "[output.firecae]\n"
                     "Cases: simple_test, activate_vents, bucket_test_2, couch, "
                     "couch_smoke_12s, HVAC_aircoil, tunnel_demo, "
                     "tunnel_smoke_10s\n";
        return 2;
    }

    const QString caseName = argc == 2
                                 ? QStringLiteral("simple_test")
                                 : QString::fromLocal8Bit(argv[1]).trimmed().toLower();
    const QString outputPath = QString::fromLocal8Bit(argv[argc == 2 ? 1 : 2]);
    std::unique_ptr<FcProject> project;
    if (caseName == QStringLiteral("simple_test")) {
        project = FdsExamples::createSimpleTestProject();
    } else if (caseName == QStringLiteral("activate_vents")) {
        project = FdsExamples::createActivateVentsProject();
    } else if (caseName == QStringLiteral("bucket_test_2")) {
        project = FdsExamples::createBucketTest2Project();
    } else if (caseName == QStringLiteral("couch")) {
        project = FdsExamples::createCouchProject();
    } else if (caseName == QStringLiteral("couch_smoke_12s")) {
        project = FdsExamples::createCouchSmoke12sProject();
    } else if (caseName == QStringLiteral("hvac_aircoil")) {
        project = FdsExamples::createHvacAircoilProject();
    } else if (caseName == QStringLiteral("tunnel_demo")) {
        project = FdsExamples::createTunnelDemoProject();
    } else if (caseName == QStringLiteral("tunnel_smoke_10s")) {
        project = FdsExamples::createTunnelSmoke10sProject();
    } else {
        std::cerr << "Unknown benchmark case: " << caseName.toStdString() << '\n';
        return 2;
    }
    QString error;
    if (!FdsWriter::writeFile(*project, outputPath, &error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    if (argc == 4 &&
        !FcProjectSerializer::save(*project, QString::fromLocal8Bit(argv[3]), &error)) {
        std::cerr << error.toStdString() << '\n';
        return 1;
    }
    std::cout << "Generated FireCAE " << caseName.toStdString()
              << " FDS input from editable business objects.\n";
    return 0;
}
