#include "ui/SmokeviewHostWidget.h"

#include "ui/UiLanguage.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
constexpr int kAttachIntervalMs = 100;
constexpr int kMaximumAttachAttempts = 100;

QString u(const char* englishText)
{
    return UiLanguageManager::text(QString::fromUtf8(englishText));
}

QSize nativeHostPixelSize(const QWidget* widget)
{
    // QWidget geometry is expressed in device-independent pixels.  Smokeview's
    // Win32/OpenGL window, however, receives physical pixels after SetParent().
    // Converting here keeps the embedded child flush with its Qt host on displays
    // using 125%, 150%, or other non-100% Windows scaling factors.
    const qreal scale = widget->devicePixelRatioF();
    return QSize(qRound(widget->width() * scale),
                 qRound(widget->height() * scale));
}

#ifdef Q_OS_WIN
struct WindowSearch
{
    DWORD processId = 0;
    HWND bestWindow = nullptr;
    long long bestArea = 0;
};

BOOL CALLBACK findProcessWindow(HWND window, LPARAM parameter)
{
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    RECT rectangle{};
    if (!GetClientRect(window, &rectangle)) {
        return TRUE;
    }
    const long long width = rectangle.right - rectangle.left;
    const long long height = rectangle.bottom - rectangle.top;
    const long long area = width * height;
    if (area > search->bestArea) {
        search->bestArea = area;
        search->bestWindow = window;
    }
    return TRUE;
}
#endif
}

SmokeviewHostWidget::SmokeviewHostWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SmokeviewHostWidget"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* controls = new QWidget(this);
    controls->setObjectName(QStringLiteral("SmokeviewPlaybackControls"));
    auto* controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(8, 6, 8, 6);

    m_caseLabel = new QLabel(controls);
    m_caseLabel->setObjectName(QStringLiteral("SmokeviewCaseLabel"));
    m_caseLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    controlsLayout->addWidget(m_caseLabel);

    m_firstButton = new QPushButton(controls);
    m_firstButton->setObjectName(QStringLiteral("SmokeviewFirstButton"));
    m_firstButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    controlsLayout->addWidget(m_firstButton);

    m_previousButton = new QPushButton(controls);
    m_previousButton->setObjectName(QStringLiteral("SmokeviewPreviousButton"));
    m_previousButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    controlsLayout->addWidget(m_previousButton);

    m_playButton = new QPushButton(controls);
    m_playButton->setObjectName(QStringLiteral("SmokeviewPlayButton"));
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    controlsLayout->addWidget(m_playButton);

    m_nextButton = new QPushButton(controls);
    m_nextButton->setObjectName(QStringLiteral("SmokeviewNextButton"));
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    controlsLayout->addWidget(m_nextButton);

    m_zoomInButton = new QPushButton(controls);
    m_zoomInButton->setObjectName(QStringLiteral("SmokeviewZoomInButton"));
    controlsLayout->addWidget(m_zoomInButton);

    m_zoomOutButton = new QPushButton(controls);
    m_zoomOutButton->setObjectName(QStringLiteral("SmokeviewZoomOutButton"));
    controlsLayout->addWidget(m_zoomOutButton);

    m_returnButton = new QPushButton(controls);
    m_returnButton->setObjectName(QStringLiteral("SmokeviewReturnButton"));
    controlsLayout->addWidget(m_returnButton);

    m_closeButton = new QPushButton(controls);
    m_closeButton->setObjectName(QStringLiteral("SmokeviewCloseButton"));
    controlsLayout->addWidget(m_closeButton);

    rootLayout->addWidget(controls);

    m_nativeHost = new QWidget(this);
    m_nativeHost->setObjectName(QStringLiteral("SmokeviewNativeHost"));
    // Keep the unused result page an ordinary Qt widget. Creating its native
    // child during construction can invalidate Qt's hidden-window hierarchy
    // when floating docks are restored and later destroyed. The actual attach
    // path calls winId() only when a viewer window has been found.
    m_nativeHost->setAutoFillBackground(true);
    m_nativeHost->setStyleSheet(QStringLiteral("background: #20242a;"));
    m_nativeHost->setMinimumSize(400, 300);
    rootLayout->addWidget(m_nativeHost, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("SmokeviewStatusLabel"));
    m_statusLabel->setContentsMargins(8, 4, 8, 4);
    m_statusLabel->setWordWrap(true);
    rootLayout->addWidget(m_statusLabel);

    m_windowTimer = new QTimer(this);
    m_windowTimer->setInterval(kAttachIntervalMs);
    connect(m_windowTimer, &QTimer::timeout, this, &SmokeviewHostWidget::tryAttachWindow);
    connect(m_firstButton, &QPushButton::clicked, this, &SmokeviewHostWidget::firstFrame);
    connect(m_previousButton, &QPushButton::clicked,
            this, &SmokeviewHostWidget::previousFrame);
    connect(m_playButton, &QPushButton::clicked, this, &SmokeviewHostWidget::togglePlayback);
    connect(m_nextButton, &QPushButton::clicked, this, &SmokeviewHostWidget::nextFrame);
    connect(m_returnButton, &QPushButton::clicked,
            this, &SmokeviewHostWidget::returnToModelRequested);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        closeViewer();
        emit returnToModelRequested();
    });

    retranslateUi();
    updateControlState();
}

SmokeviewHostWidget::~SmokeviewHostWidget()
{
    closeViewer();
}

SmokeviewLaunchResult SmokeviewHostWidget::openCase(const QString& smvFilePath,
                                                     SmokeviewLaunchMode mode)
{
    if (m_embeddedWindow != 0 || m_processId != 0) {
        closeViewer();
    }

    SmokeviewLaunchResult result = SmokeviewLauncher::launch(smvFilePath, mode);
    if (!result.success) {
        return result;
    }

    m_processId = result.processId;
    m_caseName = QFileInfo(smvFilePath).completeBaseName();
    m_caseLabel->setText(m_caseName);
    m_attachAttempts = 0;
    m_closeRequested = false;
    m_statusLabel->setText(u("Starting Smokeview and attaching its result window..."));
    updateControlState();
    m_windowTimer->start();
    QTimer::singleShot(0, this, &SmokeviewHostWidget::tryAttachWindow);
    return result;
}

void SmokeviewHostWidget::closeViewer()
{
    m_closeRequested = true;
    m_windowTimer->stop();
#ifdef Q_OS_WIN
    // Smokeview is launched detached, so the host cannot rely on QProcess
    // ownership to reap it.  Keep a real process handle while requesting a
    // graceful window close and use a bounded termination fallback.  This
    // also covers the interval before a top-level Smokeview HWND has been
    // discovered/embedded.
    HANDLE processHandle = nullptr;
    if (m_processId > 0) {
        processHandle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE,
                                    FALSE,
                                    static_cast<DWORD>(m_processId));
    }
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow != 0
                                                  ? m_embeddedWindow : m_separateWindow);
    if (window && IsWindow(window)) {
        // Detach the cross-process child before Qt destroys m_nativeHost.  Leaving
        // a foreign HWND parented to a QWidget during teardown can make Windows
        // deliver destruction messages into an object whose destructor is already
        // running (and triggers a debug stack-corruption check in MainWindow).
        if (m_embeddedWindow != 0) {
            ShowWindow(window, SW_HIDE);
            SetParent(window, reinterpret_cast<HWND>(m_originalParentWindow));
            if (m_originalWindowStyle != 0) {
                SetWindowLongPtrW(window, GWL_STYLE, m_originalWindowStyle);
            }
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_HIDEWINDOW);
        }
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    if (processHandle) {
        DWORD waitResult = WaitForSingleObject(processHandle, 1500);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(processHandle, 0);
            WaitForSingleObject(processHandle, 1000);
        }
        CloseHandle(processHandle);
    }
#endif
    clearEmbeddedState(false);
}

bool SmokeviewHostWidget::hasEmbeddedViewer() const
{
    return m_embeddedWindow != 0;
}

qint64 SmokeviewHostWidget::processId() const
{
    return m_processId;
}

void SmokeviewHostWidget::retranslateUi()
{
    m_firstButton->setText(u("First Frame"));
    m_previousButton->setText(u("Previous Frame"));
    m_playButton->setText(u("Play / Pause"));
    m_playButton->setToolTip(u("Toggle playback. Smokeview manages the current playback state."));
    m_firstButton->setToolTip(u("Return to the first frame and pause."));
    const QString stepHint = u("Pause with Play / Pause before stepping. Stepping preserves Smokeview's playback state.");
    m_previousButton->setToolTip(stepHint);
    m_nextButton->setToolTip(stepHint);
    m_nextButton->setText(u("Next Frame"));
    m_zoomInButton->setText(u("Zoom In"));
    m_zoomOutButton->setText(u("Zoom Out"));
    m_zoomInButton->setToolTip(
        u("Use Smokeview's right-click menu: Show/Hide > Viewpoints (user) > Zoom."));
    m_zoomOutButton->setToolTip(
        u("Use Smokeview's right-click menu: Show/Hide > Viewpoints (user) > Zoom."));
    m_returnButton->setText(u("Return to Model"));
    m_closeButton->setText(u("Close Viewer"));
    if (m_processId == 0) {
        m_caseLabel->setText(u("No Smokeview result is open"));
        m_statusLabel->setText(u("Open an animation from the Results menu."));
    } else if (m_embeddedWindow != 0) {
        m_statusLabel->setText(u("Play / Pause toggles playback. Pause before stepping; use the right-click Show/Hide > Viewpoints (user) > Zoom menu to zoom."));
    } else if (m_separateWindow != 0) {
        m_statusLabel->setText(u("Smokeview is open in a separate window to preserve readable display scaling. Use its playback controls and right-click Show/Hide > Viewpoints (user) > Zoom menu."));
    }
}

void SmokeviewHostWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    resizeEmbeddedWindow();
}

void SmokeviewHostWidget::tryAttachWindow()
{
#ifndef Q_OS_WIN
    m_windowTimer->stop();
    emit embeddingFailed(u("Embedded Smokeview is currently supported on Windows only."));
    return;
#else
    if (m_separateWindow != 0) {
        if (!IsWindow(reinterpret_cast<HWND>(m_separateWindow))) {
            clearEmbeddedState(true);
        }
        return;
    }
    if (m_embeddedWindow != 0) {
        const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
        if (!IsWindow(window)) {
            clearEmbeddedState(true);
        } else {
            resizeEmbeddedWindow();
        }
        return;
    }
    if (m_processId <= 0 || m_closeRequested) {
        return;
    }

    WindowSearch search;
    search.processId = static_cast<DWORD>(m_processId);
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.bestWindow) {
        ++m_attachAttempts;
        if (m_attachAttempts >= kMaximumAttachAttempts) {
            m_windowTimer->stop();
            m_statusLabel->setText(u("Smokeview started separately because its window could not be embedded."));
            emit embeddingFailed(m_statusLabel->text());
        }
        return;
    }

    const HWND parentWindow = reinterpret_cast<HWND>(m_nativeHost->winId());
    // Cross-process SetParent with differing awareness modes forces a DPI reset
    // in the child process. This also affects its later GLUT/GLUI dialogs. Keep
    // the unmodified top-level viewer when embedding cannot preserve its mode.
    if (!canEmbedWindow(reinterpret_cast<quintptr>(search.bestWindow))) {
        useSeparateWindow(reinterpret_cast<quintptr>(search.bestWindow),
                          u("Smokeview is open in a separate window to preserve readable display scaling. Use its playback controls and right-click Show/Hide > Viewpoints (user) > Zoom menu."));
        return;
    }
    LONG_PTR style = GetWindowLongPtrW(search.bestWindow, GWL_STYLE);
    m_originalParentWindow = reinterpret_cast<quintptr>(GetParent(search.bestWindow));
    m_originalWindowStyle = static_cast<qintptr>(style);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
               WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_VISIBLE;
    SetWindowLongPtrW(search.bestWindow, GWL_STYLE, style);
    SetLastError(ERROR_SUCCESS);
    const HWND originalParent = SetParent(search.bestWindow, parentWindow);
    if (originalParent == nullptr && GetLastError() != ERROR_SUCCESS) {
        SetWindowLongPtrW(search.bestWindow, GWL_STYLE, m_originalWindowStyle);
        useSeparateWindow(reinterpret_cast<quintptr>(search.bestWindow),
                          u("Smokeview started separately because its window could not be embedded."));
        return;
    }
    const QSize hostPixelSize = nativeHostPixelSize(m_nativeHost);
    SetWindowPos(search.bestWindow, nullptr, 0, 0,
                 hostPixelSize.width(), hostPixelSize.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    m_embeddedWindow = reinterpret_cast<quintptr>(search.bestWindow);
    SetFocus(search.bestWindow);
    m_statusLabel->setText(u("Play / Pause toggles playback. Pause before stepping; use the right-click Show/Hide > Viewpoints (user) > Zoom menu to zoom."));
    updateControlState();
    emit viewerEmbedded(m_processId);
#endif
}

bool SmokeviewHostWidget::canEmbedWindow(quintptr candidate) const
{
    return haveCompatibleDpi(candidate, static_cast<quintptr>(m_nativeHost->winId()));
}

bool SmokeviewHostWidget::haveCompatibleDpi(quintptr viewer, quintptr host)
{
#ifdef Q_OS_WIN
    const auto viewerContext = GetWindowDpiAwarenessContext(reinterpret_cast<HWND>(viewer));
    const auto hostContext = GetWindowDpiAwarenessContext(reinterpret_cast<HWND>(host));
    return viewerContext && hostContext &&
           AreDpiAwarenessContextsEqual(viewerContext, hostContext);
#else
    Q_UNUSED(viewer)
    Q_UNUSED(host)
    return false;
#endif
}

void SmokeviewHostWidget::useSeparateWindow(quintptr window, const QString& reason)
{
    m_separateWindow = window;
    m_originalParentWindow = 0;
    m_originalWindowStyle = 0;
    m_statusLabel->setText(reason);
    updateControlState();
    emit embeddingFailed(reason);
}

void SmokeviewHostWidget::resizeEmbeddedWindow()
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
    if (window && IsWindow(window)) {
        const QSize hostPixelSize = nativeHostPixelSize(m_nativeHost);
        MoveWindow(window, 0, 0,
                   hostPixelSize.width(), hostPixelSize.height(), TRUE);
    }
#endif
}

void SmokeviewHostWidget::sendCharacter(unsigned int character)
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
    if (window && IsWindow(window)) {
        // Smokeview's GLUT/freeglut callback is driven by native key messages.
        // WM_CHAR alone is ignored by current Smokeview builds, so post a full
        // key press and let its message loop translate it to the character.
        const SHORT keyAndModifiers = VkKeyScanW(static_cast<wchar_t>(character));
        if (keyAndModifiers == -1) {
            return;
        }
        const UINT virtualKey = LOBYTE(keyAndModifiers);
        const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
        const LPARAM keyDown = 1 | (static_cast<LPARAM>(scanCode) << 16);
        const LPARAM keyUp = keyDown | (1LL << 30) | (1LL << 31);
        PostMessageW(window, WM_KEYDOWN, virtualKey, keyDown);
        PostMessageW(window, WM_KEYUP, virtualKey, keyUp);
    }
#else
    Q_UNUSED(character)
#endif
}

void SmokeviewHostWidget::togglePlayback()
{
    if (m_embeddedWindow != 0) {
        // 't' is a toggle, not an absolute play or pause command. Native menus,
        // data loading and keyboard input can change stept without notifying us.
        // Do not infer native state or disable the only usable toggle from a
        // stale host-side boolean.
        sendCharacter(static_cast<unsigned int>('t'));
    }
}

void SmokeviewHostWidget::firstFrame()
{
    if (m_embeddedWindow == 0) {
        return;
    }
    // Smokeview 6.11.2 resets to frame zero AND starts playback on '0'.
    // Queue the pause after that reset, independent of the previous host state.
    sendCharacter(static_cast<unsigned int>('0'));
    sendCharacter(static_cast<unsigned int>('t'));
    updateControlState();
}

void SmokeviewHostWidget::previousFrame()
{
    if (m_embeddedWindow == 0) {
        return;
    }
    sendCharacter(static_cast<unsigned int>('-'));
    updateControlState();
}

void SmokeviewHostWidget::nextFrame()
{
    if (m_embeddedWindow == 0) {
        return;
    }
    sendCharacter(static_cast<unsigned int>(' '));
    updateControlState();
}

void SmokeviewHostWidget::updateControlState()
{
    const bool ready = m_embeddedWindow != 0;
    m_firstButton->setEnabled(ready);
    m_previousButton->setEnabled(ready);
    m_playButton->setEnabled(ready);
    m_nextButton->setEnabled(ready);
    // Synthetic Shift+drag was interpreted as rotation by legacy GLUT. Native
    // Show/Hide > Viewpoints (user) > Zoom is the supported camera command until a verified IPC exists.
    m_zoomInButton->setEnabled(false);
    m_zoomOutButton->setEnabled(false);
    m_closeButton->setEnabled(m_processId != 0);
}

void SmokeviewHostWidget::clearEmbeddedState(bool emitClosedSignal)
{
    const bool hadViewer = m_processId != 0 || m_embeddedWindow != 0;
    m_embeddedWindow = 0;
    m_separateWindow = 0;
    m_originalParentWindow = 0;
    m_originalWindowStyle = 0;
    m_processId = 0;
    m_attachAttempts = 0;
    m_caseName.clear();
    m_caseLabel->setText(u("No Smokeview result is open"));
    m_statusLabel->setText(u("Open an animation from the Results menu."));
    updateControlState();
    if (emitClosedSignal && hadViewer) {
        emit viewerClosed();
    }
}
