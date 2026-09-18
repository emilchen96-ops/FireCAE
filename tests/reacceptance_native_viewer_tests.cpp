#include "ui/NativeResultViewerWidget.h"
#include "ui/SmokeviewHostWidget.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <iostream>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Supply only a hidden message receiver, never a real viewer process or window.
class SmokeviewHostWidgetTestAccess
{
public:
    static void attach(SmokeviewHostWidget& host, HWND receiver)
    {
        host.m_embeddedWindow = reinterpret_cast<quintptr>(receiver);
        host.updateControlState();
    }

    static void detach(SmokeviewHostWidget& host)
    {
        host.m_embeddedWindow = 0;
        host.updateControlState();
    }

    static bool haveCompatibleDpi(HWND viewer, HWND host)
    {
        return SmokeviewHostWidget::haveCompatibleDpi(
            reinterpret_cast<quintptr>(viewer), reinterpret_cast<quintptr>(host));
    }

    static QWidget* nativeHost(SmokeviewHostWidget& host)
    {
        return host.m_nativeHost;
    }

    static bool canEmbed(SmokeviewHostWidget& host, HWND viewer)
    {
        return host.canEmbedWindow(reinterpret_cast<quintptr>(viewer));
    }
};
#endif

namespace {
int failures = 0;
bool check(bool condition, const char* message)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!condition) ++failures;
    return condition;
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray readScriptText(const QString& path)
{
    // SSF is line-oriented text (native Smokeview uses fopen("r") and fgets).
    // Qt's text writer emits CRLF on Windows. Keep commands/arguments exact,
    // but compare their text meaning independently of the platform line ending.
    // User INI/cache preservation checks continue to use binary readFile above.
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? file.readAll() : QByteArray{};
}

void checkIsolatedSmokeviewStartup()
{
    QTemporaryDir fixture;
    if (!check(fixture.isValid(), "creates an isolated startup profile fixture")) return;
    const QDir directory(fixture.path());
    const QString smv = directory.filePath(QStringLiteral("profile.smv"));
    const QString ini = directory.filePath(QStringLiteral("profile.ini"));
    const QString bounds = directory.filePath(QStringLiteral("profile_1.sf.bnd"));
    const QByteArray originalIni("# case settings must survive\nZOOM\n 0 1.5\n");
    const QByteArray originalBounds(" 0 17 48\n");
    check(writeFile(smv, "TITLE\nProfile isolation\nSLCF 1\n profile_1.sf\n TEMPERATURE\n temp\n C\n"
                        "SLCF 1\n 01_second.sf\n VELOCITY\n vel\n m/s\n") && writeFile(ini, originalIni) &&
              writeFile(bounds, originalBounds) &&
              writeFile(directory.filePath(QStringLiteral("profile_1.sf")), "fixture") &&
              writeFile(directory.filePath(QStringLiteral("00_neighbor.sf")), "other case") &&
              writeFile(directory.filePath(QStringLiteral("01_second.sf")), "other quantity"),
          "creates case settings and bounds-cache sentinels");
    QString error;
    const QString first = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Standard, &error);
    const QString second = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Slice, &error);
    const QByteArray rawStandardScript = readFile(first);
    const QByteArray rawSliceScript = readFile(second);
    std::cout << "RAW standard SSF hex: " << rawStandardScript.toHex().toStdString() << std::endl;
    std::cout << "RAW slice SSF hex: " << rawSliceScript.toHex().toStdString() << std::endl;
    std::cout << "RAW SSF CRLF pairs: standard=" << rawStandardScript.count("\r\n")
              << " slice=" << rawSliceScript.count("\r\n") << std::endl;
    check(!first.isEmpty() && !second.isEmpty() && first != second && error.isEmpty(),
          "every open receives a unique documented startup script");
    check(QFileInfo(first).absolutePath() != fixture.path() &&
              QFileInfo(second).absolutePath() != fixture.path(),
          "startup scripts are outside the user's result directory");
    const QByteArray profile = readFile(first + QStringLiteral(".ini"));
    check(profile.isEmpty(),
          "no per-launch INI is created; bundled defaults are overridden by case settings");
    check(!readScriptText(first).contains("LOADINIFILE"),
          "interactive scripts avoid the unstable LOADINIFILE initialization path");
    check(!readScriptText(first).contains("LOADINIFILE") &&
              readScriptText(first).contains("SETTIMEVAL\n0.0\nNOEXIT\n") && !readScriptText(first).contains("UNLOADALL"),
          "standard opening initializes the graphics loop at the first frame and stays interactive");
    check(readScriptText(second).startsWith("UNLOADALL\n") &&
              readScriptText(second).contains("LOADFILE\nprofile_1.sf\n") &&
              !readScriptText(second).contains("00_neighbor.sf") && !readScriptText(second).contains("01_second.sf") &&
              readScriptText(second).contains("SETTIMEVAL\n0.0\n"),
          "slice opening respects the SMV's first declared slice, not directory sort order or a neighboring case");
    check(readFile(ini) == originalIni && readFile(bounds) == originalBounds,
          "profile creation never rewrites the user's case INI or bounds cache");

    // A title/metadata value can begin with SLC or even equal a valid header.
    // The old prefix scan stopped there and falsely reported the first slice
    // missing. Use real FDS header forms while keeping the neighbor sentinels.
    const QByteArray sliceDescriptor("SLCF 1 # STRUCTURED & 0 2 0 2 1 1 ! 1 0 3\n"
                                    " profile_1.sf\n TEMPERATURE\n temp\n C\n");
    for (const QByteArray& title : {QByteArray("Slice demo"), QByteArray("SLCF 1")}) {
        check(writeFile(smv, "TITLE\n" + title + "\n\n" + sliceDescriptor),
              "writes a slice-like title followed by a genuine structured slice");
        const QString titled = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Slice, &error);
        check(!titled.isEmpty() && error.isEmpty() &&
                  readScriptText(titled).contains("LOADFILE\nprofile_1.sf\n"),
              "slice-like TITLE content does not masquerade as the first slice record");
        if (!titled.isEmpty()) {
            QFile::remove(titled);
            QFile::remove(titled + QStringLiteral(".ini"));
        }
    }
    check(writeFile(smv, "TITLE\nMetadata collision\n\nFDSVERSION\nSLC metadata\n\n"
                        "INPF\nSLCF\n\nREVISION\nSLCT 1\n\nCHID\nSLCC\n\n"
                        "CSVF\nSLCF\nSLCC\n\n"
                        " SLCF 1\n 00_neighbor.sf\n"
                        "SLCF_NOT_A_RECORD 1\n 00_neighbor.sf\n"
                        "SLCF not_a_mesh\n 00_neighbor.sf\n" + sliceDescriptor),
          "writes metadata and non-header lookalikes before the actual slice declaration");
    const QString metadata = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Slice, &error);
    check(!metadata.isEmpty() && error.isEmpty() &&
              readScriptText(metadata).contains("LOADFILE\nprofile_1.sf\n") &&
              !readScriptText(metadata).contains("00_neighbor.sf"),
          "metadata, indented payload, prefix lookalikes and invalid mesh fields cannot select neighboring data");
    if (!metadata.isEmpty()) {
        QFile::remove(metadata);
        QFile::remove(metadata + QStringLiteral(".ini"));
    }
    for (const QByteArray& header : {QByteArray("SLCF"), QByteArray("SLCC 1"), QByteArray("SLCT 1 0.25")}) {
        check(writeFile(smv, "TITLE\nSlice record forms\n" + header +
                             "\n profile_1.sf\n TEMPERATURE\n temp\n C\n"),
              "writes a supported native slice record form");
        const QString supported = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Slice, &error);
        check(!supported.isEmpty() && error.isEmpty() &&
                  readScriptText(supported).contains("LOADFILE\nprofile_1.sf\n"),
              "bare single-mesh, cell-centred and terrain slice declarations remain supported");
        if (!supported.isEmpty()) {
            QFile::remove(supported);
            QFile::remove(supported + QStringLiteral(".ini"));
        }
    }

    const QByteArray customIni("V2_SLICE\n 0 20 0 100 temp\nFONTSIZE\n 2\n");
    check(writeFile(ini, customIni), "writes an explicit user display preference");
    const QString custom = SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Standard, &error);
    const QByteArray customProfile = readFile(custom + QStringLiteral(".ini"));
    check(!custom.isEmpty() && !customProfile.contains("V2_SLICE") &&
              !customProfile.contains("FONTSIZE") && readFile(ini) == customIni,
          "explicit case bounds and font are respected without overwriting their file");
    check(SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Particles, &error).isEmpty() &&
              !error.isEmpty() && readFile(ini) == customIni && readFile(bounds) == originalBounds,
          "missing requested data fails without changing case settings or caches");
    check(writeFile(smv, "TITLE\nFirst declaration must exist\n" + sliceDescriptor +
                        "SLCF 1\n 01_second.sf\n VELOCITY\n vel\n m/s\n"),
          "restores two declared quantities before testing the missing first slice");
    QFile::remove(directory.filePath(QStringLiteral("profile_1.sf")));
    check(SmokeviewLauncher::createAnimationScript(smv, SmokeviewLaunchMode::Slice, &error).isEmpty() &&
              !error.isEmpty() && readFile(ini) == customIni && readFile(bounds) == originalBounds,
          "missing declared first slice fails instead of loading a neighboring case or second quantity");
    for (const auto& path : {first, second, custom}) {
        if (!path.isEmpty()) {
            QFile::remove(path);
            QFile::remove(path + QStringLiteral(".ini"));
        }
    }
}

bool selectFile(QListWidget* files, const QString& name)
{
    for (int row = 0; row < files->count(); ++row) {
        if (QFileInfo(files->item(row)->data(Qt::UserRole).toString()).fileName() == name) {
            files->setCurrentRow(row);
            return true;
        }
    }
    return false;
}

#ifdef Q_OS_WIN
// Controlled protocol model, not a Smokeview integration test. The independent
// semantics come from firemodels/smv revision 27cbe5960515dade2273f94c4b2afcea967ac786:
// callbacks.c:2824-2827 ('0' calls SetTimeFrameIndex(0, NO_PAUSE_TIME)),
// smokeviewdefs.h:256-257 (NO_PAUSE_TIME=1), callbacks.c:2614 ('t' toggles stept),
// and callbacks.c:3043-3046 (single stepping preserves stept).
struct SmokeviewPlaybackReceiver
{
    static constexpr const wchar_t* className = L"FireCAEFirstFrameProtocolReceiver";
    HWND window = nullptr;
    ATOM windowClass = 0;
    SmokeviewHostWidget* host = nullptr;
    bool playing = false;
    int frame = 7;
    QByteArray keys;
    WPARAM pendingKey = 0;
    int keyUps = 0;
    bool completeKeyPairs = true;
    int mouseMessages = 0;

    SmokeviewPlaybackReceiver()
    {
        WNDCLASSW windowType{};
        windowType.lpfnWndProc = receive;
        windowType.hInstance = GetModuleHandleW(nullptr);
        windowType.lpszClassName = className;
        windowClass = RegisterClassW(&windowType);
        if (windowClass) {
            window = CreateWindowExW(0, className, L"", 0, 0, 0, 1, 1,
                                     HWND_MESSAGE, nullptr, windowType.hInstance, this);
        }
    }

    ~SmokeviewPlaybackReceiver()
    {
        if (host) SmokeviewHostWidgetTestAccess::detach(*host);
        if (window) DestroyWindow(window);
        if (windowClass) UnregisterClassW(className, GetModuleHandleW(nullptr));
    }

    static LRESULT CALLBACK receive(HWND window, UINT message, WPARAM key, LPARAM detail)
    {
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(detail);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* receiver = reinterpret_cast<SmokeviewPlaybackReceiver*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (receiver && message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
            ++receiver->mouseMessages;
        }
        if (receiver && message == WM_KEYDOWN) {
            receiver->completeKeyPairs &= receiver->pendingKey == 0;
            receiver->pendingKey = key;
            receiver->keys.append(static_cast<char>(key));
            switch (key) {
            case '0': receiver->frame = 0; receiver->playing = true; break;
            case 'T': receiver->playing = !receiver->playing; break;
            case VK_SPACE: ++receiver->frame; break;
            case VK_OEM_MINUS: --receiver->frame; break;
            default: break;
            }
            return 0;
        }
        if (receiver && message == WM_KEYUP) {
            receiver->completeKeyPairs &= receiver->pendingKey == key &&
                                          (detail & (1LL << 30)) != 0 &&
                                          (detail & (1LL << 31)) != 0;
            receiver->pendingKey = 0;
            ++receiver->keyUps;
            return 0;
        }
        return DefWindowProcW(window, message, key, detail);
    }

    void drainPostedKeys()
    {
        MSG message{};
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    void advanceIdleFrames(int count)
    {
        // Model elapsed native display ticks without a wall-clock sleep.
        if (playing) frame += count;
    }
};
#endif

void checkSmokeviewFirstFrame()
{
#ifdef Q_OS_WIN
    SmokeviewHostWidget host;
    SmokeviewPlaybackReceiver receiver;
    if (!check(receiver.window != nullptr, "creates a hidden Win32 playback protocol receiver")) return;
    receiver.host = &host;
    auto* first = host.findChild<QPushButton*>(QStringLiteral("SmokeviewFirstButton"));
    auto* next = host.findChild<QPushButton*>(QStringLiteral("SmokeviewNextButton"));
    auto* play = host.findChild<QPushButton*>(QStringLiteral("SmokeviewPlayButton"));
    auto* previous = host.findChild<QPushButton*>(QStringLiteral("SmokeviewPreviousButton"));
    auto* zoomIn = host.findChild<QPushButton*>(QStringLiteral("SmokeviewZoomInButton"));
    auto* zoomOut = host.findChild<QPushButton*>(QStringLiteral("SmokeviewZoomOutButton"));
    if (!check(first && next && play && previous && zoomIn && zoomOut,
               "Smokeview playback controls are available")) return;
    check(!host.findChild<QPushButton*>(QStringLiteral("SmokeviewPauseButton")),
          "host has no separate absolute pause button without native state feedback");

    for (const bool initiallyPlaying : {false, true}) {
        std::cout << "STAGE: first frame from " << (initiallyPlaying ? "playing" : "paused") << std::endl;
        receiver.playing = initiallyPlaying;
        receiver.frame = 7;
        receiver.keys.clear();
        receiver.keyUps = 0;
        receiver.pendingKey = 0;
        receiver.completeKeyPairs = true;
        SmokeviewHostWidgetTestAccess::attach(host, receiver.window);
        first->click();
        check(receiver.keys.isEmpty(), "first-frame keys are posted to the native queue");
        receiver.drainPostedKeys();
        std::cout << "DELIVERED WM_KEYDOWN: " << receiver.keys.toStdString() << std::endl;
        check(receiver.completeKeyPairs && receiver.pendingKey == 0 &&
                  receiver.keyUps == receiver.keys.size(),
              "native receiver gets complete ordered key-down/key-up pairs");
        check(receiver.frame == 0 && !receiver.playing,
              "first frame resets and pauses the native protocol, independent of its prior state");
        check(play->isEnabled(), "first frame leaves the native playback toggle usable");
        receiver.advanceIdleFrames(20);
        check(receiver.frame == 0, "first frame remains at zero across later native idle ticks");

        next->click();
        receiver.drainPostedKeys();
        receiver.advanceIdleFrames(20);
        check(receiver.frame == 1 && !receiver.playing,
              "next after first frame advances once and remains paused");
        play->click();
        receiver.drainPostedKeys();
        receiver.advanceIdleFrames(3);
        check(receiver.frame == 4 && receiver.playing && play->isEnabled(),
              "toggle after first frame starts progression and remains available");
        play->click();
        receiver.drainPostedKeys();
        receiver.advanceIdleFrames(20);
        check(receiver.frame == 4 && !receiver.playing && play->isEnabled(),
              "toggle after resumed playback stops progression and remains available");

        // Native Load/Unload, a native keyboard action or the time bar can
        // change playback without any event reaching the Qt host.
        receiver.playing = true;
        play->click();
        receiver.drainPostedKeys();
        check(!receiver.playing && play->isEnabled(),
              "native restart while host was paused cannot disable or invert the labeled toggle");
        receiver.playing = false;
        play->click();
        receiver.drainPostedKeys();
        check(receiver.playing && play->isEnabled(),
              "native pause while host was playing cannot disable the labeled toggle");
        receiver.keys.clear();
        next->click();
        receiver.drainPostedKeys();
        check(receiver.playing && receiver.keys == QByteArray(1, static_cast<char>(VK_SPACE)),
              "next frame never guesses playback state or emits an unsolicited pause toggle");
        receiver.keys.clear();
        previous->click();
        receiver.drainPostedKeys();
        check(receiver.playing && receiver.keys == QByteArray(1, static_cast<char>(VK_OEM_MINUS)),
              "previous frame never guesses playback state or emits an unsolicited pause toggle");
        receiver.keys.clear();
        const int priorMouseMessages = receiver.mouseMessages;
        zoomIn->click();
        zoomOut->click();
        receiver.drainPostedKeys();
        check(!zoomIn->isEnabled() && !zoomOut->isEnabled() &&
                  receiver.keys.isEmpty() && receiver.mouseMessages == priorMouseMessages,
              "unsupported host zoom cannot dispatch a rotating drag or modifiers");
        check(zoomIn->toolTip().contains(QStringLiteral("Zoom")),
              "disabled host zoom directs users to the supported native zoom menu");
    }
#else
    std::cout << "SKIP: Win32 Smokeview playback protocol regression" << std::endl;
#endif
}

void checkDpiEmbeddingPolicy()
{
#ifdef Q_OS_WIN
    SmokeviewHostWidget host;
    QWidget* nativeHost = SmokeviewHostWidgetTestAccess::nativeHost(host);
    check(nativeHost && !nativeHost->testAttribute(Qt::WA_NativeWindow) &&
              nativeHost->internalWinId() == 0 && host.internalWinId() == 0,
          "constructing an unused Smokeview page does not eagerly create a native child or ancestor");
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    HWND unaware = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1,
                                   HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND aware = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1,
                                 HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetThreadDpiAwarenessContext(previous);
    check(unaware && aware, "creates hidden DPI policy fixtures without a viewer");
    if (unaware && aware) {
        check(!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(unaware),
                                             GetWindowDpiAwarenessContext(aware)),
              "fixtures genuinely have different DPI contexts");
        check(!SmokeviewHostWidgetTestAccess::haveCompatibleDpi(unaware, aware),
              "DPI mismatch is rejected before cross-process embedding can reset the viewer");
        check(SmokeviewHostWidgetTestAccess::haveCompatibleDpi(aware, aware),
              "matching DPI awareness remains eligible for embedding");
        check(!SmokeviewHostWidgetTestAccess::haveCompatibleDpi(nullptr, aware),
              "unknown window context cannot be assumed safe to embed");
        const bool canEmbed = SmokeviewHostWidgetTestAccess::canEmbed(host, aware);
        const auto lazyHostId = nativeHost->internalWinId();
        check(lazyHostId != 0 && nativeHost->testAttribute(Qt::WA_NativeWindow),
              "the actual embedding policy path creates the native host on demand");
        const auto actualHostContext = GetWindowDpiAwarenessContext(reinterpret_cast<HWND>(lazyHostId));
        const auto actualViewerContext = GetWindowDpiAwarenessContext(aware);
        const bool contextsMatch = actualHostContext && actualViewerContext &&
            AreDpiAwarenessContextsEqual(actualViewerContext, actualHostContext);
        check(canEmbed == contextsMatch,
              "lazy host creation still applies the real Windows DPI-context check before embedding");
    }
    if (unaware) DestroyWindow(unaware);
    if (aware) DestroyWindow(aware);
#endif
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FireCAEReacceptanceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("NativeViewerReset"));
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "temporary fixture directory exists")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    checkSmokeviewFirstFrame();
    checkDpiEmbeddingPolicy();
    checkIsolatedSmokeviewStartup();
    const QDir directory(temporary.path());
    const QString smv = directory.filePath(QStringLiteral("reset.smv"));
    const QString hrr = directory.filePath(QStringLiteral("reset_hrr.csv"));
    const QByteArray fourSeries(
        "s,kW,kW,kW,kW\nTime,HRR,Radiation,Convection,Other\n"
        "0,0,1,2,3\n1,10,2,3,4\n2,20,3,4,5\n");
    const QByteArray oneSeries("s,C\nTime,Temperature\n0,20\n1,30\n2,40\n");
    if (!check(writeFile(smv, "TITLE\nNative viewer reset regression\n") &&
                   writeFile(hrr, fourSeries) &&
                   writeFile(directory.filePath(QStringLiteral("reset_zones.csv")), oneSeries),
               "SMV and CSV fixtures are written without a solver")) return 1;

    NativeResultViewerWidget viewer;
    auto* files = viewer.findChild<QListWidget*>(QStringLiteral("NativeResultFileList"));
    auto* series = viewer.findChild<QListWidget*>(QStringLiteral("NativeResultSeriesList"));
    auto* tree = viewer.findChild<QTreeWidget*>(QStringLiteral("NativeResultObjectTree"));
    auto* statistics = viewer.findChild<QTableWidget*>(QStringLiteral("NativeResultStatisticsTable"));
    auto* play = viewer.findChild<QPushButton*>(QStringLiteral("NativeResultPlayButton"));
    if (!check(files && series && tree && statistics && play,
               "native result controls exist")) return 1;
    QString error;
    if (!check(viewer.openCase(smv, &error), "opens four-series result")) {
        std::cerr << error.toStdString() << std::endl;
        return 1;
    }
    if (!check(viewer.csvSeriesCount() == 4 && series->selectedItems().size() == 4 &&
                   statistics->rowCount() == 4,
               "all four default curves and statistics are active")) return 1;
    play->click();
    std::cout << "STAGE: close with multiple selected series" << std::endl;
    viewer.closeCase();
    QApplication::processEvents();
    check(!viewer.hasOpenCase() && viewer.csvSeriesCount() == 0 && viewer.frameCount() == 0 &&
              files->count() == 0 && series->count() == 0 &&
              tree->topLevelItemCount() == 0 && statistics->rowCount() == 0,
          "closing clears data, selection and statistics together");
    bool timersStopped = true;
    for (auto* timer : viewer.findChildren<QTimer*>()) timersStopped &= !timer->isActive();
    check(timersStopped && !play->isEnabled(), "closing stops playback and refresh timers");
    check(!viewer.exportVisibleCsv(directory.filePath(QStringLiteral("closed.csv"))),
          "closed viewer cannot export obsolete data");
    viewer.closeCase();

    if (!check(viewer.openCase(smv, &error), "reopens after close")) return 1;
    std::cout << "STAGE: switch from four curves to one" << std::endl;
    check(selectFile(files, QStringLiteral("reset_zones.csv")), "selects second CSV");
    check(viewer.csvSeriesCount() == 1 && series->selectedItems().size() == 1 &&
              statistics->rowCount() == 1 && statistics->item(0, 0) &&
              statistics->item(0, 0)->text() == QStringLiteral("Temperature"),
          "switching to fewer quantities removes old selections and statistics");
    check(selectFile(files, QStringLiteral("reset_hrr.csv")), "returns to four-series CSV");
    check(series->selectedItems().size() == 4, "four curves selected before refresh");
    if (!check(writeFile(hrr, oneSeries), "CSV schema is replaced on disk")) return 1;
    std::cout << "STAGE: refresh after series count decreases" << std::endl;
    check(viewer.refreshCase(&error), "refresh accepts changed CSV");
    check(viewer.csvSeriesCount() == 1 && statistics->rowCount() == series->selectedItems().size(),
          "refresh does not use selection indices from the previous CSV");
    check(viewer.openCase(smv, &error), "opening another result state resets the existing state");
    check(selectFile(files, QStringLiteral("reset_zones.csv")), "selects readable CSV before read failure");
    if (!check(writeFile(directory.filePath(QStringLiteral("reset_zones.csv")), "invalid CSV\n"),
               "selected CSV becomes unreadable as result data")) return 1;
    check(viewer.refreshCase(&error), "refresh handles invalid CSV without losing the case");
    check(viewer.csvSeriesCount() == 0 && series->count() == 0 && statistics->rowCount() == 0,
          "CSV failure leaves no stale curves or statistics");
    std::cout << "STAGE: failed case open clears current result" << std::endl;
    check(!viewer.openCase(directory.filePath(QStringLiteral("missing.smv")), &error) &&
              !viewer.hasOpenCase() && files->count() == 0,
          "failed open safely clears the old case");
    QApplication::processEvents();
    std::cout << "Native viewer reset failures: " << failures << std::endl;
    return failures ? 1 : 0;
}
