#include "comparison/FdsComparisonReport.h"
#include "comparison/FdsInputComparator.h"
#include "comparison/ThreeWayComparisonEvidence.h"
#include "core/FcProject.h"
#include "fds/FcProjectSerializer.h"
#include "results/FdsResultComparator.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FireCAEComparisonReporter"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Generate reproducible FireCAE FDS input/result comparison reports."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption referenceOption(
        {QStringLiteral("r"), QStringLiteral("reference-smv")},
        QStringLiteral("Reference Smokeview file."), QStringLiteral("file"));
    const QCommandLineOption candidateOption(
        {QStringLiteral("c"), QStringLiteral("candidate-smv")},
        QStringLiteral("Candidate Smokeview file."), QStringLiteral("file"));
    const QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output-directory")},
        QStringLiteral("Report output directory."), QStringLiteral("directory"));
    const QCommandLineOption referenceFdsOption(
        QStringLiteral("reference-fds"),
        QStringLiteral("Reference FDS input override (useful when the SMV is in a result subdirectory)."),
        QStringLiteral("file"));
    const QCommandLineOption candidateFdsOption(
        QStringLiteral("candidate-fds"),
        QStringLiteral("Candidate FDS input override."), QStringLiteral("file"));
    const QCommandLineOption projectOption(
        QStringLiteral("project"),
        QStringLiteral("FireCAE project used to emit UUID-to-FDS-ID mappings."),
        QStringLiteral("file"));
    const QCommandLineOption baseOption(
        {QStringLiteral("b"), QStringLiteral("base-name")},
        QStringLiteral("Base name for HTML/PDF/CSV files."), QStringLiteral("name"),
        QStringLiteral("firecae-comparison"));
    const QCommandLineOption titleOption(
        {QStringLiteral("t"), QStringLiteral("title")},
        QStringLiteral("Report title."), QStringLiteral("title"),
        QStringLiteral("FireCAE / FDS comparison report"));
    const QCommandLineOption screenshotOption(
        {QStringLiteral("s"), QStringLiteral("screenshot")},
        QStringLiteral("Evidence screenshot to embed; may be repeated."),
        QStringLiteral("file"));
    const QCommandLineOption threeWayManifestOption(
        QStringLiteral("three-way-manifest"),
        QStringLiteral("Three-source Native FDS / FireCAE / PyroSim evidence manifest."),
        QStringLiteral("file"));
    parser.addOptions({referenceOption, candidateOption, outputOption,
                       referenceFdsOption, candidateFdsOption, projectOption,
                       baseOption, titleOption, screenshotOption,
                       threeWayManifestOption});
    parser.process(application);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    if (parser.isSet(threeWayManifestOption)) {
        const QString directory = parser.value(outputOption);
        if (directory.isEmpty()) {
            errors << "output-directory is required for a three-way manifest.\n";
            return 2;
        }
        const FdsThreeWayComparisonResult comparison =
            ThreeWayComparisonEvidence::loadAndCompare(
                parser.value(threeWayManifestOption));
        const FdsThreeWayReportArtifacts artifacts =
            ThreeWayComparisonEvidence::writeReports(
                comparison, directory, parser.value(baseOption));
        if (!artifacts.success()) {
            errors << artifacts.errorMessage << '\n';
            return 4;
        }
        output << "three_way_status=" << comparison.statusCode() << '\n';
        output << "markdown=" << artifacts.markdownFile << '\n';
        output << "json=" << artifacts.jsonFile << '\n';
        if (comparison.statusCode() == QStringLiteral("PASS")) return 0;
        if (comparison.statusCode() == QStringLiteral("PENDING_EVIDENCE")) return 6;
        if (comparison.statusCode() == QStringLiteral("INVALID_EVIDENCE") ||
            comparison.statusCode() == QStringLiteral("INVALID_MANIFEST")) return 7;
        return 8;
    }
    const QString reference = parser.value(referenceOption);
    const QString candidate = parser.value(candidateOption);
    const QString directory = parser.value(outputOption);
    if (reference.isEmpty() || candidate.isEmpty() || directory.isEmpty()) {
        errors << "reference-smv, candidate-smv, and output-directory are required.\n";
        parser.showHelp(2);
    }

    FdsResultComparison result =
        FdsResultComparator::compareSmvFiles(reference, candidate);
    if (!result.success()) {
        errors << result.errorMessage << '\n';
        return 3;
    }
    FdsInputComparison input;
    const QString referenceFds = parser.value(referenceFdsOption).isEmpty()
        ? result.referenceProvenance.fdsInputFile
        : QFileInfo(parser.value(referenceFdsOption)).absoluteFilePath();
    const QString candidateFds = parser.value(candidateFdsOption).isEmpty()
        ? result.candidateProvenance.fdsInputFile
        : QFileInfo(parser.value(candidateFdsOption)).absoluteFilePath();
    const bool hasInputs = QFileInfo::exists(referenceFds) &&
                           QFileInfo::exists(candidateFds);
    if (hasInputs) {
        input = FdsInputComparator::compareFiles(referenceFds, candidateFds);
        if (input.success()) {
            result.referenceProvenance.fdsInputFile = referenceFds;
            result.referenceProvenance.fdsInputSha256 = input.referenceSha256;
            result.candidateProvenance.fdsInputFile = candidateFds;
            result.candidateProvenance.fdsInputSha256 = input.candidateSha256;
        }
    }
    QVector<FdsUuidIdMapping> mappings;
    if (!parser.value(projectOption).isEmpty()) {
        const FcProjectLoadResult loaded =
            FcProjectSerializer::load(parser.value(projectOption));
        if (!loaded.success()) {
            errors << "FireCAE project could not be loaded for UUID mappings: "
                   << loaded.errorMessage << '\n';
            return 5;
        }
        mappings = FdsInputComparator::uuidToFdsIdMappings(*loaded.project);
    }
    FdsComparisonReportOptions options;
    options.outputDirectory = directory;
    options.baseFileName = parser.value(baseOption);
    options.title = parser.value(titleOption);
    options.fireCaeVersion = QCoreApplication::applicationVersion();
    options.fdsVersion = result.referenceProvenance.solverRevision ==
                                 result.candidateProvenance.solverRevision
                             ? result.referenceProvenance.solverRevision
                             : QStringLiteral("reference=%1; candidate=%2")
                                   .arg(result.referenceProvenance.solverRevision,
                                        result.candidateProvenance.solverRevision);
    options.runTimeSeconds = QStringLiteral("reference=%1; candidate=%2")
                                 .arg(result.referenceProvenance.completedTime,
                                      result.candidateProvenance.completedTime);
    options.screenshots = parser.values(screenshotOption);
    options.reproduciblePaths = {
        QFileInfo(reference).absoluteFilePath(),
        QFileInfo(candidate).absoluteFilePath(),
        referenceFds, candidateFds};
    const FdsComparisonReportArtifacts artifacts = FdsComparisonReport::write(
        hasInputs && input.success() ? &input : nullptr, result, mappings, options);
    if (!artifacts.success()) {
        errors << artifacts.errorMessage << '\n';
        return 4;
    }
    output << "result_status=" << (result.passed() ? "PASS" : "FAIL") << '\n';
    output << "input_status="
           << (hasInputs && input.success()
                   ? (input.semanticallyEquivalent() ? "PASS" : "FAIL")
                   : "UNAVAILABLE") << '\n';
    output << "quantities=" << result.quantities.size() << '\n';
    output << "html=" << artifacts.htmlFile << '\n';
    output << "pdf=" << artifacts.pdfFile << '\n';
    output << "csv=" << artifacts.csvFile << '\n';
    return 0;
}
