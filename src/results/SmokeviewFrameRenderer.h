#pragma once

#include "results/FcResultFile.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;

struct SmokeviewFrameRenderRequest
{
    QString smvFilePath;
    QString outputDirectory;
    FcResultFileType fieldType = FcResultFileType::Smoke3D;
    QString fieldFilePath;
    QString quantity;
    QString slicePlaneKeyword;
    double slicePlaneValue = 0.0;
    QVector<double> times;
    QString filePrefix = QStringLiteral("firecae_frame");
};

class SmokeviewFrameRenderer final : public QObject
{
    Q_OBJECT

public:
    explicit SmokeviewFrameRenderer(QObject* parent = nullptr);

    bool start(const SmokeviewFrameRenderRequest& request,
               QString* errorMessage = nullptr);
    void cancel();
    bool isRunning() const;
    QString scriptFilePath() const;
    QStringList renderedFiles() const;

    static QString scriptText(const SmokeviewFrameRenderRequest& request,
                              QString* errorMessage = nullptr);

signals:
    void progressChanged(int completed, int total, const QString& detail);
    void outputReceived(const QString& text);
    void finished(bool success, const QStringList& imageFiles,
                  const QString& errorMessage);

private:
    void collectRenderedFiles();

    QProcess* m_process = nullptr;
    SmokeviewFrameRenderRequest m_request;
    QString m_scriptFilePath;
    QStringList m_renderedFiles;
    QString m_processOutput;
    int m_completedFrames = 0;
    int m_retryCount = 0;
    bool m_cancelled = false;
};
