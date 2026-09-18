#include "comparison/FdsComparisonReport.h"

#include <QCoreApplication>
#include <QAbstractTextDocumentLayout>
#include <QPageLayout>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPageSize>
#include <QPrinter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextDocument>

namespace
{
QString html(QString value)
{
    return value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString csv(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString comparisonScope()
{
    return QStringLiteral(
        "Numerical acceptance covers only the sampled time-series quantities listed in this report. "
        "Files or quantities outside that list are not covered by the numerical PASS; "
        "review the comparator warnings for explicit exclusions.");
}

QString comparisonWarnings(const FdsResultComparison& comparison)
{
    QString value = QStringLiteral(
        "<h2>Comparison scope and warnings</h2><p>%1</p>"
        "<p><b>Matched files:</b> %2<br><b>Compared quantities:</b> %3<br>"
        "<b>Comparator warnings:</b> %4</p>")
        .arg(html(comparisonScope()))
        .arg(comparison.matchedFileCount)
        .arg(comparison.quantities.size())
        .arg(comparison.warnings.size());
    if (comparison.warnings.isEmpty()) {
        return value + QStringLiteral("<p>No comparator warnings were reported.</p>");
    }
    value += QStringLiteral("<ul>");
    for (const QString& warning : comparison.warnings) {
        value += QStringLiteral("<li>%1</li>").arg(html(warning));
    }
    return value + QStringLiteral("</ul>");
}

QString paginatedReportHtml(QString document)
{
    // Give the raw diff and the separate statistics table explicit print-page
    // starts. Qt does not reliably keep a heading with the following table;
    // otherwise a table ending near the page edge can orphan the next heading.
    document.replace(QStringLiteral("<h3>Raw text differences</h3>"),
                     QStringLiteral("<h3 style='page-break-before:always;'>"
                                    "Raw text differences</h3>"));
    // A fresh result section also keeps a short table's final label/file-name
    // pair from becoming an almost-empty continuation after a long raw diff.
    document.replace(QStringLiteral("<h2>FDS result comparison</h2>"),
                     QStringLiteral("<h2 style='page-break-before:always;'>"
                                    "FDS result comparison</h2>"));
    document.replace(QStringLiteral("<h3>Peak, timing, and mean statistics</h3>"),
                     QStringLiteral("<h3 style='page-break-before:always;'>"
                                    "Peak, timing, and mean statistics</h3>"));
    // One preformatted block spanning pages can repaint a clipped prior line
    // and its background. Keep every original logical line in its own block.
    const QRegularExpression preExpression(
        QStringLiteral("<pre>(.*?)</pre>"), QRegularExpression::DotMatchesEverythingOption);
    for (auto match = preExpression.match(document); match.hasMatch();
         match = preExpression.match(document)) {
        QString lines;
        for (const QString& line : match.captured(1).split(QStringLiteral("<br>"),
                                                         Qt::KeepEmptyParts)) {
            lines += QStringLiteral("<p style='margin:0; font-family:Consolas,monospace; "
                                    "font-size:8pt; white-space:pre-wrap;'>");
            lines += line.isEmpty() ? QStringLiteral("&#160;") : line;
            lines += QStringLiteral("</p>");
        }
        document.replace(match.capturedStart(), match.capturedLength(), lines);
    }
    const QRegularExpression headerExpression(
        QStringLiteral("(<table[^>]*>)(<tr><th>.*?</tr>)"),
        QRegularExpression::DotMatchesEverythingOption);
    document.replace(headerExpression, QStringLiteral("\\1<thead>\\2</thead>"));
    return document;
}

bool writeFile(const QString& path, const QByteArray& contents, QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Report file cannot be opened: %1").arg(path);
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        error = QStringLiteral("Report file cannot be written atomically: %1").arg(path);
        return false;
    }
    return true;
}

QString embeddedImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString mime = suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
                             ? QStringLiteral("image/jpeg")
                             : QStringLiteral("image/png");
    return QStringLiteral("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(file.readAll().toBase64()));
}

QString provenanceTable(const FdsResultComparison& comparison)
{
    const auto row = [](const QString& label, const QString& reference,
                        const QString& candidate) {
        return QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
            .arg(html(label), html(reference), html(candidate));
    };
    QString table = QStringLiteral(
        "<h2>Provenance</h2><table><tr><th>Field</th><th>Reference</th>"
        "<th>Candidate</th></tr>");
    table += row(QStringLiteral("FDS input"),
                 comparison.referenceProvenance.fdsInputFile,
                 comparison.candidateProvenance.fdsInputFile);
    table += row(QStringLiteral("Input SHA-256"),
                 comparison.referenceProvenance.fdsInputSha256,
                 comparison.candidateProvenance.fdsInputSha256);
    table += row(QStringLiteral("FDS revision"),
                 comparison.referenceProvenance.solverRevision,
                 comparison.candidateProvenance.solverRevision);
    table += row(QStringLiteral("Solver time (s)"),
                 comparison.referenceProvenance.completedTime,
                 comparison.candidateProvenance.completedTime);
    table += row(QStringLiteral("Physical simulation end time (s)"),
                 comparison.referenceProvenance.simulationEndTime,
                 comparison.candidateProvenance.simulationEndTime);
    table += row(QStringLiteral("Normal termination"),
                 comparison.referenceProvenance.normalTermination
                     ? QStringLiteral("yes") : QStringLiteral("no"),
                 comparison.candidateProvenance.normalTermination
                     ? QStringLiteral("yes") : QStringLiteral("no"));
    return table + QStringLiteral("</table>");
}

QString mappingTable(const QVector<FdsUuidIdMapping>& mappings)
{
    QString value = QStringLiteral(
        "<h2>FireCAE UUID to FDS ID mapping</h2>"
        "<table><tr><th>UUID</th><th>Keyword</th><th>FDS ID</th><th>Name</th></tr>");
    if (mappings.isEmpty()) {
        value += QStringLiteral("<tr><td colspan='4'>No current FireCAE project mapping was supplied.</td></tr>");
    }
    for (const FdsUuidIdMapping& mapping : mappings) {
        value += QStringLiteral("<tr><td><code>%1</code></td><td>%2</td><td>%3</td><td>%4</td></tr>")
                     .arg(html(mapping.uuid), html(mapping.keyword), html(mapping.fdsId),
                          html(mapping.objectName));
    }
    return value + QStringLiteral("</table>");
}

QString reportCsv(const FdsInputComparison* input,
                  const FdsResultComparison& result,
                  const QVector<FdsUuidIdMapping>& mappings,
                  const FdsComparisonReportOptions& options)
{
    QString value = QStringLiteral("section,key,value\n");
    value += QStringLiteral("report,title,%1\n").arg(csv(options.title));
    value += QStringLiteral("report,firecae_version,%1\n").arg(csv(options.fireCaeVersion));
    value += QStringLiteral("report,fds_version,%1\n").arg(csv(options.fdsVersion));
    value += QStringLiteral("report,environment,%1\n").arg(csv(options.solveEnvironment));
    value += QStringLiteral("report,generated_utc,%1\n\n")
                 .arg(csv(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
    value += QStringLiteral("[COMPARISON_SCOPE]\nkey,value\nscope,%1\n"
                            "matched_files,%2\ncompared_quantities,%3\nwarning_count,%4\n")
                 .arg(csv(comparisonScope()))
                 .arg(result.matchedFileCount)
                 .arg(result.quantities.size())
                 .arg(result.warnings.size());
    value += QStringLiteral("\n[RESULT_WARNINGS]\nwarning\n");
    for (const QString& warning : result.warnings) {
        value += csv(warning) + QLatin1Char('\n');
    }
    value += QLatin1Char('\n');
    if (input) {
        value += QStringLiteral("[FDS_INPUT_COMPARISON]\n") + input->csvReport() +
                 QStringLiteral("\n");
    }
    value += QStringLiteral("[UUID_TO_FDS_ID]\nuuid,keyword,fds_id,name\n");
    for (const FdsUuidIdMapping& mapping : mappings) {
        value += QStringLiteral("%1,%2,%3,%4\n")
                     .arg(csv(mapping.uuid), csv(mapping.keyword), csv(mapping.fdsId),
                          csv(mapping.objectName));
    }
    value += QStringLiteral("\n[FDS_RESULT_COMPARISON]\n") + result.csvReport();
    return value;
}
}

FdsComparisonReportArtifacts FdsComparisonReport::write(
    const FdsInputComparison* inputComparison,
    const FdsResultComparison& resultComparison,
    const QVector<FdsUuidIdMapping>& uuidMappings,
    const FdsComparisonReportOptions& options)
{
    FdsComparisonReportArtifacts artifacts;
    if (!resultComparison.success()) {
        artifacts.errorMessage = QStringLiteral("A successful result comparison is required.");
        return artifacts;
    }
    const QString outputDirectory = QDir::cleanPath(options.outputDirectory);
    if (outputDirectory.isEmpty() || !QDir().mkpath(outputDirectory)) {
        artifacts.errorMessage = QStringLiteral("Report directory cannot be created: %1")
                                     .arg(outputDirectory);
        return artifacts;
    }
    QString base = options.baseFileName.trimmed();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")),
                 QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("firecae-comparison");
    artifacts.htmlFile = QDir(outputDirectory).filePath(base + QStringLiteral(".html"));
    artifacts.pdfFile = QDir(outputDirectory).filePath(base + QStringLiteral(".pdf"));
    artifacts.csvFile = QDir(outputDirectory).filePath(base + QStringLiteral(".csv"));

    const QString environment = options.solveEnvironment.isEmpty()
        ? QStringLiteral("%1; %2").arg(QSysInfo::prettyProductName(),
                                       QSysInfo::currentCpuArchitecture())
        : options.solveEnvironment;
    QString document = QStringLiteral(
        "<!doctype html><html><head><meta charset='utf-8'><title>%1</title>"
        "<style>body{font-family:'Segoe UI',Arial,sans-serif;color:#202124;margin:28px;}"
        "h1{color:#8b2b16}h2{border-bottom:1px solid #ccc;padding-bottom:4px}"
        "h2,h3{margin-top:22px;}"
        // Qt includes a table's bottom margin in its frame height, then repeats
        // headers on every page touched by that frame. Keep trailing whitespace
        // on the following heading so an empty tail cannot repeat a table header
        // over the next section. Real multi-page tables retain their thead.
        "table{border-collapse:collapse;width:100%;font-size:9pt;margin:10px 0 0;}"
        "th,td{border:1px solid #bbb;padding:5px;vertical-align:top;}th{background:#eee;}"
        ".pass,.acceptable{color:#087f23;font-weight:600}.fail,.different{color:#b00020;font-weight:600}"
        "code,pre{font-family:Consolas,monospace;font-size:8pt}pre{white-space:pre-wrap;background:#f5f5f5;padding:10px;}"
        "img{max-width:100%;height:auto;border:1px solid #bbb}"
        ".evidence-shot{page-break-inside:avoid;margin:0 0 18px;}"
        ".evidence-shot img{display:block;width:680px;height:auto;}"
        ".series-chart{page-break-inside:avoid;margin:0 0 14px;}"
        ".series-chart h4{margin:8px 0 4px;}"
        ".series-legend{font-size:8pt;margin-left:28px;}"
        ".series-legend span{margin-right:18px;}</style></head><body>"
        "<h1>%2</h1><p><b>Generated:</b> %3 UTC<br><b>FireCAE:</b> %4<br>"
        "<b>FDS:</b> %5<br><b>Environment:</b> %6<br><b>Reported run time:</b> %7 s</p>")
        .arg(html(options.title), html(options.title),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             html(options.fireCaeVersion.isEmpty()
                      ? QCoreApplication::applicationVersion()
                      : options.fireCaeVersion),
             html(options.fdsVersion), html(environment), html(options.runTimeSeconds));
    document += provenanceTable(resultComparison);
    document += comparisonWarnings(resultComparison);
    if (inputComparison) document += inputComparison->htmlReport();
    document += mappingTable(uuidMappings);
    document += resultComparison.htmlReport();
    document += QStringLiteral("<h2>Evidence screenshots</h2>");
    if (options.screenshots.isEmpty()) {
        document += QStringLiteral("<p>No screenshots were attached to this report run.</p>");
    }
    for (const QString& screenshot : options.screenshots) {
        const QString data = embeddedImage(screenshot);
        if (data.isEmpty()) continue;
        // QTextDocument's PDF layout does not consistently honour max-width for
        // data-URI images.  An explicit width keeps a 2800x1800 GUI capture on
        // one A4 page instead of printing it at its natural pixel size and
        // splitting it across multiple pages.
        document += QStringLiteral(
                        "<div class='evidence-shot'><h3>%1</h3>"
                        "<img width='680' src='%2'></div>")
                        .arg(html(QFileInfo(screenshot).fileName()), data);
    }
    document += QStringLiteral("<h2>Reproducible paths</h2><ul>");
    for (const QString& path : options.reproduciblePaths) {
        document += QStringLiteral("<li><code>%1</code></li>").arg(html(path));
    }
    document += QStringLiteral(
        "</ul><h2>Conclusion</h2><p class='%1'>%2</p></body></html>")
        .arg(resultComparison.passed() &&
                     (!inputComparison || inputComparison->semanticallyEquivalent())
                 ? QStringLiteral("pass") : QStringLiteral("fail"),
             resultComparison.passed() &&
                     (!inputComparison || inputComparison->semanticallyEquivalent())
                 ? QStringLiteral("PASS — compared input semantics and numerical outputs are within the configured acceptance criteria.")
                 : QStringLiteral("FAIL / REVIEW — one or more semantic or numerical differences require review."));

    // QTextDocument supports these table attributes directly; keep cells
    // separated even when importing fragments with no per-cell styling.
    document.replace(QStringLiteral("<table>"),
                     QStringLiteral("<table width='100%' border='1' cellspacing='0' cellpadding='4'>"));
    document = paginatedReportHtml(document);
    if (!writeFile(artifacts.htmlFile, document.toUtf8(), artifacts.errorMessage))
        return artifacts;
    if (!writeFile(artifacts.csvFile,
                   reportCsv(inputComparison, resultComparison, uuidMappings, options).toUtf8(),
                   artifacts.errorMessage)) return artifacts;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(artifacts.pdfFile);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    QTextDocument pdfDocument;
    pdfDocument.documentLayout()->setPaintDevice(&printer);
    pdfDocument.setHtml(document);
    pdfDocument.setPageSize(printer.pageRect(QPrinter::DevicePixel).size());
    pdfDocument.print(&printer);
    if (!QFileInfo::exists(artifacts.pdfFile) || QFileInfo(artifacts.pdfFile).size() == 0) {
        artifacts.errorMessage = QStringLiteral("PDF report could not be generated: %1")
                                     .arg(artifacts.pdfFile);
    }
    return artifacts;
}
