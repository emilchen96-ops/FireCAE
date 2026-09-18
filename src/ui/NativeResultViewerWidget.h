#pragma once

#include "results/FdsCsvData.h"
#include "results/FdsResultScanner.h"
#include "results/FdsSliceReader.h"

#include <QWidget>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class FcProject;
class ResultPlotCanvas;
class ResultColorBarWidget;
class ResultGeometryCanvas;
class ResultFieldImageLabel;
class SmokeviewFrameRenderer;

class NativeResultViewerWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit NativeResultViewerWidget(QWidget* parent = nullptr);
    ~NativeResultViewerWidget() override;

    void setProject(const FcProject* project);
    bool openCase(const QString& smvFilePath, QString* errorMessage = nullptr);
    bool refreshCase(QString* errorMessage = nullptr);
    bool addComparisonCase(const QString& smvFilePath,
                           const QString& sourceLabel = {},
                           QString* errorMessage = nullptr);
    void clearComparisonCases();
    void closeCase();
    bool hasOpenCase() const;
    QString caseName() const;
    int frameCount() const;
    int csvSeriesCount() const;
    double currentTime() const;
    bool saveScreenshot(const QString& filePath) const;
    bool exportVisibleCsv(const QString& filePath) const;
    bool exportVideo(const QString& filePath, int framesPerSecond = 10,
                     QString* errorMessage = nullptr);
    void retranslateUi();

signals:
    void returnToModelRequested();
    void openInSmokeviewRequested(const QString& smvFilePath);
    void caseOpened(const QString& caseName);
    void frameChanged(double time);

private:
    void populateResultFiles();
    void loadSelectedFile();
    void updateSelectedSeries();
    void updateTimeline();
    void setCurrentFrame(int frame);
    void play();
    void pause();
    void stop();
    void exportCsvInteractive();
    void saveScreenshotInteractive();
    void exportFrameSequenceInteractive();
    void exportVideoInteractive();
    void addComparisonCaseInteractive();
    void refreshCaseInteractive();
    void renderSelectedField();
    void showRenderedFrame();
    void loadComparisonData(const QString& primaryFilePath);
    void rebuildGeometryView();
    void applyTimeRange();
    void restoreColorSettings();
    void saveColorSettings() const;

    FdsResultScanResult m_scan;
    QVector<FdsResultScanResult> m_comparisonScans;
    FdsCsvData m_csvData;
    QVector<FdsCsvData> m_comparisonCsvData;
    QStringList m_comparisonLabels;
    QVector<double> m_caseTimes;
    QVector<double> m_frameTimes;
    QStringList m_renderedFrames;
    FdsSliceData m_nativeSliceData;
    QString m_caseName;
    const FcProject* m_project = nullptr;
    std::unique_ptr<FcProject> m_resultProject;
    int m_currentFrame = 0;
    QTimer* m_playTimer = nullptr;
    QTimer* m_refreshTimer = nullptr;

    QLabel* m_caseLabel = nullptr;
    QComboBox* m_sceneCombo = nullptr;
    QComboBox* m_displayCombo = nullptr;
    QCheckBox* m_geometryVisibleCheck = nullptr;
    QTreeWidget* m_resultTree = nullptr;
    QListWidget* m_fileList = nullptr;
    QListWidget* m_seriesList = nullptr;
    QLabel* m_formatStatus = nullptr;
    ResultPlotCanvas* m_plot = nullptr;
    ResultGeometryCanvas* m_geometryCanvas = nullptr;
    ResultFieldImageLabel* m_renderedFrameLabel = nullptr;
    QStackedWidget* m_resultDisplayStack = nullptr;
    QTableWidget* m_statisticsTable = nullptr;
    QSlider* m_timeline = nullptr;
    QDoubleSpinBox* m_timeSpin = nullptr;
    QLabel* m_timeLabel = nullptr;
    QProgressBar* m_cacheProgress = nullptr;
    QDoubleSpinBox* m_speedSpin = nullptr;
    QDoubleSpinBox* m_rangeStartSpin = nullptr;
    QDoubleSpinBox* m_rangeEndSpin = nullptr;
    QCheckBox* m_loopCheck = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QSpinBox* m_refreshIntervalSpin = nullptr;
    QCheckBox* m_dualAxisCheck = nullptr;
    QCheckBox* m_autoRangeCheck = nullptr;
    QCheckBox* m_logRangeCheck = nullptr;
    QCheckBox* m_globalColorCheck = nullptr;
    QDoubleSpinBox* m_colorMinimum = nullptr;
    QDoubleSpinBox* m_colorMaximum = nullptr;
    QDoubleSpinBox* m_colorOpacity = nullptr;
    QComboBox* m_colorMapCombo = nullptr;
    ResultColorBarWidget* m_colorBar = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_previousButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_smokeviewButton = nullptr;
    QPushButton* m_renderFieldButton = nullptr;
    QPushButton* m_cancelRenderButton = nullptr;
    QPushButton* m_returnButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_clearComparisonButton = nullptr;
    SmokeviewFrameRenderer* m_frameRenderer = nullptr;
};
