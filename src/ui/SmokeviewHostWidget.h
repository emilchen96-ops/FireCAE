#pragma once

#include "results/SmokeviewLauncher.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

class SmokeviewHostWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SmokeviewHostWidget(QWidget* parent = nullptr);
    ~SmokeviewHostWidget() override;

    SmokeviewLaunchResult openCase(const QString& smvFilePath,
                                   SmokeviewLaunchMode mode);
    void closeViewer();
    bool hasEmbeddedViewer() const;
    qint64 processId() const;
    void retranslateUi();

signals:
    void viewerEmbedded(qint64 processId);
    void viewerClosed();
    void embeddingFailed(const QString& errorMessage);
    void returnToModelRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class SmokeviewHostWidgetTestAccess;

    void tryAttachWindow();
    bool canEmbedWindow(quintptr candidate) const;
    static bool haveCompatibleDpi(quintptr viewer, quintptr host);
    void useSeparateWindow(quintptr window, const QString& reason);
    void resizeEmbeddedWindow();
    void sendCharacter(unsigned int character);
    void togglePlayback();
    void firstFrame();
    void previousFrame();
    void nextFrame();
    void updateControlState();
    void clearEmbeddedState(bool emitClosedSignal);

    QWidget* m_nativeHost = nullptr;
    QLabel* m_caseLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_firstButton = nullptr;
    QPushButton* m_previousButton = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_zoomInButton = nullptr;
    QPushButton* m_zoomOutButton = nullptr;
    QPushButton* m_returnButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QTimer* m_windowTimer = nullptr;
    qint64 m_processId = 0;
    quintptr m_embeddedWindow = 0;
    quintptr m_separateWindow = 0;
    quintptr m_originalParentWindow = 0;
    qintptr m_originalWindowStyle = 0;
    int m_attachAttempts = 0;
    bool m_closeRequested = false;
    QString m_caseName;
};
