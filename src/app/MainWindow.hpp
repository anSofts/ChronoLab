#pragma once

#include "audio/AudioCapture.hpp"
#include "core/TimegrapherAnalyzer.hpp"

#include <QMainWindow>
#include <QVector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace chronolab {

class SignalPlotWidget;
class TimegrapherPlotWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void toggleCapture();
    void onDevicesChanged(const QStringList& devices);
    void onSamples(const QVector<float>& samples, int sampleRate);
    void updateLevel(float peak, float rms);
    void analyzeBuffer();
    void openWav();
    void saveWav();
    void exportCsv();
    void clearSession();
    void showAudioHelp();

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

    AudioCapture m_capture;
    TimegrapherAnalyzer m_analyzer;
    AnalysisResult m_lastResult;
    QVector<float> m_audioBuffer;
    int m_sampleRate = 0;

    QComboBox* m_deviceCombo = nullptr;
    QComboBox* m_bphCombo = nullptr;
    QComboBox* m_liftAngleCombo = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_saveWavButton = nullptr;
    QPushButton* m_exportButton = nullptr;
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
};

} // namespace chronolab
