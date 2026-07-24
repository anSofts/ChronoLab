#pragma once

#include "audio/AudioCapture.hpp"
#include "core/TimegrapherAnalyzer.hpp"

#include <QMainWindow>
#include <QVector>

#include <array>
#include <optional>

class QComboBox;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
template <typename T> class QFutureWatcher;

namespace chronolab {

class SignalPlotWidget;
class TimegrapherPlotWidget;

struct AnalysisJobResult {
    quint64 generation = 0;
    AnalysisResult result;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleCapture();
    void onDevicesChanged(const QStringList& devices);
    void onSamples(const QVector<float>& samples, int sampleRate);
    void updateLevel(float peak, float rms);
    void analyzeBuffer();
    void finishAnalysis();
    void openWav();
    void saveWav();
    void exportCsv();
    void clearSession();
    void showAudioHelp();
    void runSimulation();
    void capturePosition();
    void showPositionSummary();
    void configurePositionMode(bool advanced);

private:
    QWidget* createMetricCard(
        const QString& title,
        QLabel*& valueLabel,
        const QString& initialValue,
        const QString& suffix = {});
    void buildInterface();
    void applyTheme();
    void updateMeasurementUi(const AnalysisResult& result);
    AnalyzerConfig analyzerConfig() const;
    void setStatus(const QString& text, bool warning = false);
    void loadSettings();
    void saveSettings() const;
    void updateSessionButton();
    static QStringList positionNames();

    AudioCapture m_capture;
    AnalysisResult m_lastResult;
    QVector<float> m_audioBuffer;
    int m_sampleRate = 0;

    QComboBox* m_deviceCombo = nullptr;
    QComboBox* m_bphCombo = nullptr;
    QComboBox* m_liftAngleCombo = nullptr;
    QComboBox* m_positionCombo = nullptr;
    QCheckBox* m_advancedPositionsCheck = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_saveWavButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_capturePositionButton = nullptr;
    QPushButton* m_sessionButton = nullptr;
    QLabel* m_rateValue = nullptr;
    QLabel* m_amplitudeValue = nullptr;
    QLabel* m_beatErrorValue = nullptr;
    QLabel* m_bphValue = nullptr;
    QLabel* m_confidenceValue = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QProgressBar* m_levelMeter = nullptr;
    SignalPlotWidget* m_signalPlot = nullptr;
    TimegrapherPlotWidget* m_timegrapherPlot = nullptr;
    QTimer* m_analysisTimer = nullptr;
    QFutureWatcher<AnalysisJobResult>* m_analysisWatcher = nullptr;
    bool m_analysisPending = false;
    quint64 m_analysisGeneration = 0;
    std::array<std::optional<AnalysisResult>, 6> m_positionResults;
};

} // namespace chronolab
