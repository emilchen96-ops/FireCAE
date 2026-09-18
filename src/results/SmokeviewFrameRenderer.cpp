#include "results/SmokeviewFrameRenderer.h"

#include "results/SmokeviewLauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace
{
QString safePrefix(QString prefix)
{
    prefix = prefix.trimmed();
    prefix.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                   QStringLiteral("_"));
    return prefix.isEmpty() ? QStringLiteral("firecae_frame") : prefix;
}

QString loadCommand(FcResultFileType type, const QString& fieldFilePath,
                    const QString& quantity,
                    const QString& slicePlaneKeyword, double slicePlaneValue,
                    QString* errorMessage)
{
    switch (type) {
    case FcResultFileType::Smoke3D:
        if (quantity.trimmed().isEmpty()) break;
        return QStringLiteral("LOAD3DSMOKE\n %1\n").arg(quantity.trimmed());
    case FcResultFileType::Slice: {
        if (quantity.trimmed().isEmpty()) break;
        if (slicePlaneKeyword != QStringLiteral("PBX") &&
            slicePlaneKeyword != QStringLiteral("PBY") &&
            slicePlaneKeyword != QStringLiteral("PBZ")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The selected slice has no PBX/PBY/PBZ metadata.");
            return {};
        }
        // A slice can be invisible when Smokeview's saved/default camera sees
        // the plane edge-on.  Select the matching axis view before rendering
        // the cache so a PBX/PBY/PBZ result is demonstrably visible.
        const QString viewCommand =
            slicePlaneKeyword == QStringLiteral("PBX") ? QStringLiteral("VIEWXMIN") :
            slicePlaneKeyword == QStringLiteral("PBY") ? QStringLiteral("VIEWYMIN") :
                                                          QStringLiteral("VIEWZMAX");
        const QString loadSlice = QFileInfo::exists(fieldFilePath)
            ? QStringLiteral("LOADFILE\n %1\n").arg(
                  QFileInfo(fieldFilePath).fileName())
            : QStringLiteral("LOADSLCF\n QUANTITY='%1' %2=%3\n")
                  .arg(quantity.trimmed(), slicePlaneKeyword)
                  .arg(slicePlaneValue, 0, 'g', 12);
        return QStringLiteral("%1%2\n").arg(loadSlice, viewCommand);
    }
    case FcResultFileType::Boundary:
        if (quantity.trimmed().isEmpty()) break;
        return QStringLiteral("LOADBOUNDARY\n %1\n").arg(quantity.trimmed());
    case FcResultFileType::Particle:
        return QStringLiteral("LOADPARTICLES\n");
    default:
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "Automated frame rendering is not available for this result type yet.");
        return {};
    }
    if (errorMessage) *errorMessage = QStringLiteral("The selected result has no quantity metadata.");
    return {};
}
}

SmokeviewFrameRenderer::SmokeviewFrameRenderer(QObject* parent)
    : QObject(parent), m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QString text = QString::fromLocal8Bit(m_process->readAll());
        if (text.isEmpty()) return;
        m_processOutput += text;
        emit outputReceived(text);
        const int rendered = text.count(QStringLiteral("Rendering to:"));
        if (rendered > 0) {
            const int totalFrames = static_cast<int>(m_request.times.size());
            m_completedFrames = std::min(totalFrames, m_completedFrames + rendered);
            emit progressChanged(m_completedFrames, totalFrames,
                                 QStringLiteral("Rendering result frames"));
        }
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
                collectRenderedFiles();
                const bool success = !m_cancelled && status == QProcess::NormalExit &&
                                     exitCode == 0 &&
                                     m_renderedFiles.size() == m_request.times.size();
                if (!success && !m_cancelled && m_retryCount == 0) {
                    ++m_retryCount;
                    for (const QString& imageFile : std::as_const(m_renderedFiles)) {
                        QFile::remove(imageFile);
                    }
                    m_renderedFiles.clear();
                    m_completedFrames = 0;
                    const QString detail = QStringLiteral(
                        "Smokeview produced an incomplete frame cache; retrying once.");
                    m_processOutput += QStringLiteral("\n") + detail;
                    emit outputReceived(detail);
                    emit progressChanged(0, static_cast<int>(m_request.times.size()),
                                         detail);
                    QTimer::singleShot(250, this, [this]() {
                        m_process->start();
                        if (m_process->waitForStarted(5000)) return;
                        const QString error = QStringLiteral(
                            "Smokeview frame rendering retry could not start: %1")
                                                  .arg(m_process->errorString());
                        emit progressChanged(0,
                                             static_cast<int>(m_request.times.size()),
                                             error);
                        emit finished(false, {}, error);
                    });
                    return;
                }
                QString error;
                if (m_cancelled) error = QStringLiteral("Frame rendering was cancelled.");
                else if (!success) {
                    error = QStringLiteral(
                        "Smokeview frame rendering failed or produced %1 of %2 requested frames.")
                                .arg(m_renderedFiles.size())
                                .arg(m_request.times.size());
                    const QString tail = m_processOutput.right(2000).trimmed();
                    if (!tail.isEmpty()) error += QStringLiteral("\n") + tail;
                }
                emit progressChanged(static_cast<int>(m_renderedFiles.size()),
                                     static_cast<int>(m_request.times.size()),
                                     success ? QStringLiteral("Frame cache ready") : error);
                emit finished(success, m_renderedFiles, error);
            });
}

QString SmokeviewFrameRenderer::scriptText(const SmokeviewFrameRenderRequest& request,
                                            QString* errorMessage)
{
    if (request.times.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("At least one result time is required.");
        return {};
    }
    QString command = loadCommand(request.fieldType, request.fieldFilePath,
                                  request.quantity,
                                  request.slicePlaneKeyword,
                                  request.slicePlaneValue, errorMessage);
    if (command.isEmpty()) return {};
    const QString prefix = safePrefix(request.filePrefix);
    QString text = QStringLiteral("RENDERDIR\n %1\nUNLOADALL\n%2")
                       .arg(QDir::toNativeSeparators(request.outputDirectory), command);
    for (qsizetype index = 0; index < request.times.size(); ++index) {
        text += QStringLiteral("SETTIMEVAL\n %1\nRENDERONCE\n %2_%3\n")
                    .arg(request.times.at(index), 0, 'g', 12)
                    .arg(prefix)
                    .arg(index, 6, 10, QLatin1Char('0'));
    }
    return text;
}

bool SmokeviewFrameRenderer::start(const SmokeviewFrameRenderRequest& request,
                                   QString* errorMessage)
{
    if (isRunning()) {
        if (errorMessage) *errorMessage = QStringLiteral("A frame render is already running.");
        return false;
    }
    const QFileInfo smv(request.smvFilePath);
    if (!smv.exists() || smv.suffix().compare(QStringLiteral("smv"), Qt::CaseInsensitive) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The Smokeview case file is invalid.");
        return false;
    }
    const QString executable = SmokeviewLauncher::detectExecutable();
    if (executable.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Smokeview is not configured.");
        return false;
    }
    if (!QDir().mkpath(request.outputDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("The frame cache directory cannot be created.");
        return false;
    }
    QString scriptError;
    const QString script = scriptText(request, &scriptError);
    if (script.isEmpty()) {
        if (errorMessage) *errorMessage = scriptError;
        return false;
    }
    m_request = request;
    m_request.filePrefix = safePrefix(request.filePrefix);
    m_scriptFilePath = QDir(request.outputDirectory).filePath(
        QStringLiteral(".firecae-render-%1.ssf")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QSaveFile scriptFile(m_scriptFilePath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("The Smokeview render script cannot be written.");
        return false;
    }
    scriptFile.write(script.toUtf8());
    if (!scriptFile.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("The Smokeview render script cannot be committed.");
        return false;
    }
    m_renderedFiles.clear();
    m_processOutput.clear();
    m_completedFrames = 0;
    m_retryCount = 0;
    m_cancelled = false;
    QString casePath = smv.absoluteFilePath();
    casePath.chop(smv.suffix().size() + 1);
    m_process->setWorkingDirectory(smv.absolutePath());
    m_process->setProgram(executable);
    m_process->setArguments({QStringLiteral("-script"), m_scriptFilePath,
                             QStringLiteral("-render_overwrite"), casePath});
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        if (errorMessage) *errorMessage = m_process->errorString();
        return false;
    }
    emit progressChanged(0, static_cast<int>(m_request.times.size()),
                         QStringLiteral("Starting Smokeview renderer"));
    return true;
}

void SmokeviewFrameRenderer::cancel()
{
    if (!isRunning()) return;
    m_cancelled = true;
    m_process->terminate();
    if (!m_process->waitForFinished(1500)) m_process->kill();
}

bool SmokeviewFrameRenderer::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

QString SmokeviewFrameRenderer::scriptFilePath() const { return m_scriptFilePath; }
QStringList SmokeviewFrameRenderer::renderedFiles() const { return m_renderedFiles; }

void SmokeviewFrameRenderer::collectRenderedFiles()
{
    m_renderedFiles.clear();
    const QDir directory(m_request.outputDirectory);
    for (qsizetype index = 0; index < m_request.times.size(); ++index) {
        const QString path = directory.filePath(
            QStringLiteral("%1_%2.png").arg(m_request.filePrefix)
                .arg(index, 6, 10, QLatin1Char('0')));
        if (QFileInfo::exists(path)) m_renderedFiles.append(QFileInfo(path).absoluteFilePath());
    }
}
