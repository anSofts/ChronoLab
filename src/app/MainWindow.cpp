#include "app/MainWindow.hpp"

#include "audio/WavFile.hpp"
#include "widgets/SignalPlotWidget.hpp"
#include "widgets/TimegrapherPlotWidget.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <span>

namespace chronolab {
namespace {

QPushButton* makeButton(const QString& text, const QString& objectName = {})
{
    auto* button = new QPushButton(text);
    if (!objectName.isEmpty())
        button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_capture(this)
{
    buildInterface();
    applyTheme();

    m_analysisTimer = new QTimer(this);
    m_analysisTimer->setInterval(900);
    connect(m_analysisTimer, &QTimer::timeout,
            this, &MainWindow::analyzeBuffer);

    connect(&m_capture, &AudioCapture::devicesChanged,
            this, &MainWindow::onDevicesChanged);
    connect(&m_capture, &AudioCapture::samplesReady,
            this, &MainWindow::onSamples);
    connect(&m_capture, &AudioCapture::levelChanged,
            this, &MainWindow::updateLevel);
    connect(&m_capture, &AudioCapture::captureError, this,
            [this](const QString& message) {
                setStatus(message, true);
                QMessageBox::warning(this, tr("Errore audio"), message);
            });
    connect(&m_capture, &AudioCapture::statusChanged,
            this, [this](const QString& text) { setStatus(text); });
    connect(&m_capture, &AudioCapture::runningChanged, this,
            [this](bool running) {
                m_startButton->setText(running ? tr("Arresta") : tr("Avvia ascolto"));
                m_startButton->setProperty("running", running);
                m_startButton->style()->unpolish(m_startButton);
                m_startButton->style()->polish(m_startButton);
                if (running)
                    m_analysisTimer->start();
                else
                    m_analysisTimer->stop();
            });

    onDevicesChanged(m_capture.inputDeviceNames());
    setWindowTitle(tr("ChronoLab 0.1 — Open Timegrapher"));
    resize(1360, 850);
    setMinimumSize(1024, 680);
    setStatus(tr("Pronto. Seleziona il sensore USB e avvia l'ascolto."));
}

QWidget* MainWindow::createMetricCard(
    const QString& title,
    QLabel*& valueLabel,
    const QString& initialValue,
    const QString& suffix)
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("metricCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title.toUpper());
    titleLabel->setObjectName(QStringLiteral("metricTitle"));
    valueLabel = new QLabel(initialValue);
    valueLabel->setObjectName(QStringLiteral("metricValue"));
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(titleLabel);
    if (suffix.isEmpty()) {
        layout->addWidget(valueLabel);
    } else {
        auto* row = new QHBoxLayout;
        row->setSpacing(7);
        row->addWidget(valueLabel);
        auto* suffixLabel = new QLabel(suffix);
        suffixLabel->setObjectName(QStringLiteral("metricSuffix"));
        row->addWidget(suffixLabel, 0, Qt::AlignBottom);
        row->addStretch();
        layout->addLayout(row);
    }
    return card;
}

void MainWindow::buildInterface()
{
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(22, 18, 22, 16);
    root->setSpacing(14);

    auto* header = new QHBoxLayout;
    auto* brandColumn = new QVBoxLayout;
    brandColumn->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("CHRONO<span style='color:#4ceeb9'>LAB</span>"));
    title->setObjectName(QStringLiteral("brand"));
    title->setTextFormat(Qt::RichText);
    auto* subtitle = new QLabel(tr("ANALISI ACUSTICA PER OROLOGI MECCANICI"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    brandColumn->addWidget(title);
    brandColumn->addWidget(subtitle);
    header->addLayout(brandColumn);
    header->addStretch();

    auto* openButton = makeButton(tr("Apri WAV"));
    m_saveWavButton = makeButton(tr("Salva WAV"));
    m_exportButton = makeButton(tr("Esporta CSV"));
    auto* clearButton = makeButton(tr("Nuova sessione"));
    header->addWidget(openButton);
    header->addWidget(m_saveWavButton);
    header->addWidget(m_exportButton);
    header->addWidget(clearButton);
    root->addLayout(header);

    connect(openButton, &QPushButton::clicked, this, &MainWindow::openWav);
    connect(m_saveWavButton, &QPushButton::clicked, this, &MainWindow::saveWav);
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearSession);

    auto* controls = new QFrame;
    controls->setObjectName(QStringLiteral("controlBar"));
    auto* controlLayout = new QHBoxLayout(controls);
    controlLayout->setContentsMargins(15, 12, 15, 12);
    controlLayout->setSpacing(10);

    auto* inputLabel = new QLabel(tr("INGRESSO"));
    inputLabel->setObjectName(QStringLiteral("controlLabel"));
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setMinimumWidth(260);
    auto* refreshButton = makeButton(tr("↻"));
    refreshButton->setToolTip(tr("Aggiorna dispositivi audio"));
    refreshButton->setFixedWidth(42);

    auto* bphLabel = new QLabel(tr("A/H"));
    bphLabel->setObjectName(QStringLiteral("controlLabel"));
    m_bphCombo = new QComboBox;
    m_bphCombo->addItem(tr("Automatico"), 0.0);
    for (const double rate : TimegrapherAnalyzer::standardBeatRates())
        m_bphCombo->addItem(QString::number(rate, 'f', 0), rate);

    auto* liftLabel = new QLabel(tr("ANGOLO"));
    liftLabel->setObjectName(QStringLiteral("controlLabel"));
    m_liftAngleCombo = new QComboBox;
    for (double angle = 38.0; angle <= 60.0; angle += 0.5)
        m_liftAngleCombo->addItem(tr("%1°").arg(angle, 0, 'f', 1), angle);
    m_liftAngleCombo->setCurrentIndex(
        m_liftAngleCombo->findData(52.0));

    m_startButton = makeButton(tr("Avvia ascolto"), QStringLiteral("startButton"));
    auto* helpButton = makeButton(tr("Audio pulito?"));

    controlLayout->addWidget(inputLabel);
    controlLayout->addWidget(m_deviceCombo, 1);
    controlLayout->addWidget(refreshButton);
    controlLayout->addSpacing(8);
    controlLayout->addWidget(bphLabel);
    controlLayout->addWidget(m_bphCombo);
    controlLayout->addWidget(liftLabel);
    controlLayout->addWidget(m_liftAngleCombo);
    controlLayout->addSpacing(8);
    controlLayout->addWidget(helpButton);
    controlLayout->addWidget(m_startButton);
    root->addWidget(controls);

    connect(refreshButton, &QPushButton::clicked,
            &m_capture, &AudioCapture::refreshDevices);
    connect(m_startButton, &QPushButton::clicked,
            this, &MainWindow::toggleCapture);
    connect(helpButton, &QPushButton::clicked,
            this, &MainWindow::showAudioHelp);
    connect(m_bphCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::analyzeBuffer);
    connect(m_liftAngleCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::analyzeBuffer);

    auto* metrics = new QGridLayout;
    metrics->setHorizontalSpacing(10);
    metrics->setVerticalSpacing(10);
    metrics->addWidget(createMetricCard(
                           tr("Marcia"), m_rateValue, QStringLiteral("—"), tr("s/g")),
                       0, 0);
    metrics->addWidget(createMetricCard(
                           tr("Ampiezza"), m_amplitudeValue, QStringLiteral("—"), tr("°")),
                       0, 1);
    metrics->addWidget(createMetricCard(
                           tr("Beat error"), m_beatErrorValue, QStringLiteral("—"), tr("ms")),
                       0, 2);
    metrics->addWidget(createMetricCard(
                           tr("Frequenza"), m_bphValue, QStringLiteral("—"), tr("A/h")),
                       0, 3);
    metrics->addWidget(createMetricCard(
                           tr("Affidabilità"), m_confidenceValue, QStringLiteral("0"), tr("%")),
                       0, 4);
    for (int i = 0; i < 5; ++i)
        metrics->setColumnStretch(i, 1);
    root->addLayout(metrics);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    m_timegrapherPlot = new TimegrapherPlotWidget;
    splitter->addWidget(m_timegrapherPlot);

    auto* diagnosticPanel = new QFrame;
    diagnosticPanel->setObjectName(QStringLiteral("diagnosticPanel"));
    auto* diagnosticLayout = new QVBoxLayout(diagnosticPanel);
    diagnosticLayout->setContentsMargins(12, 12, 12, 12);
    diagnosticLayout->setSpacing(9);
    m_signalPlot = new SignalPlotWidget;
    diagnosticLayout->addWidget(m_signalPlot, 1);

    auto* meterRow = new QHBoxLayout;
    auto* meterLabel = new QLabel(tr("LIVELLO"));
    meterLabel->setObjectName(QStringLiteral("controlLabel"));
    m_levelMeter = new QProgressBar;
    m_levelMeter->setRange(0, 1000);
    m_levelMeter->setValue(0);
    m_levelMeter->setTextVisible(false);
    meterRow->addWidget(meterLabel);
    meterRow->addWidget(m_levelMeter, 1);
    diagnosticLayout->addLayout(meterRow);

    m_formatLabel = new QLabel(tr("Nessun flusso audio"));
    m_formatLabel->setObjectName(QStringLiteral("formatLabel"));
    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("analysisStatus"));
    m_statusLabel->setWordWrap(true);
    diagnosticLayout->addWidget(m_formatLabel);
    diagnosticLayout->addWidget(m_statusLabel);
    splitter->addWidget(diagnosticPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    auto* footer = new QLabel(
        tr("ChronoLab 0.1 · GPL-3.0-or-later · Elaborazione locale, nessun dato inviato"));
    footer->setObjectName(QStringLiteral("footer"));
    root->addWidget(footer, 0, Qt::AlignRight);

    m_saveWavButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #0d141b;
            color: #eaf1f5;
            font-family: "Segoe UI", "Inter", sans-serif;
            font-size: 10pt;
        }
        QLabel#brand { font-size: 24pt; font-weight: 800; letter-spacing: 2px; }
        QLabel#subtitle { color: #718392; font-size: 8pt; letter-spacing: 1px; }
        QFrame#controlBar, QFrame#diagnosticPanel, QFrame#metricCard {
            background: #121c25;
            border: 1px solid #243541;
            border-radius: 8px;
        }
        QFrame#metricCard:hover { border-color: #3e776a; }
        QLabel#metricTitle, QLabel#controlLabel {
            color: #8295a3;
            font-size: 8pt;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#metricValue { font-size: 23pt; font-weight: 700; color: #f1f7f9; }
        QLabel#metricSuffix { color: #718794; font-size: 9pt; padding-bottom: 5px; }
        QLabel#formatLabel, QLabel#footer { color: #718392; font-size: 8.5pt; }
        QLabel#analysisStatus {
            color: #a9bcc7;
            background: #0d151c;
            border: 1px solid #243541;
            border-radius: 6px;
            padding: 9px;
        }
        QPushButton {
            background: #18252f;
            border: 1px solid #30424f;
            border-radius: 6px;
            padding: 8px 13px;
            color: #dce7ec;
        }
        QPushButton:hover { background: #22333e; border-color: #4b6473; }
        QPushButton:disabled { color: #53636e; background: #111a21; }
        QPushButton#startButton {
            background: #3fe0ad;
            color: #07150f;
            border: 1px solid #64f4c6;
            font-weight: 800;
            padding-left: 20px;
            padding-right: 20px;
        }
        QPushButton#startButton[running="true"] {
            background: #f08c75;
            border-color: #ffae9b;
            color: #1c0b08;
        }
        QComboBox {
            background: #0d161d;
            border: 1px solid #30424f;
            border-radius: 5px;
            padding: 7px 10px;
            min-height: 18px;
        }
        QComboBox:hover { border-color: #4ceeb9; }
        QComboBox QAbstractItemView {
            background: #16222b;
            selection-background-color: #276a58;
            border: 1px solid #30424f;
        }
        QProgressBar {
            background: #0b1218;
            border: 1px solid #263844;
            border-radius: 4px;
            min-height: 9px;
            max-height: 9px;
        }
        QProgressBar::chunk { background: #4ceeb9; border-radius: 3px; }
        QSplitter::handle { background: #1d2b35; width: 6px; }
        QStatusBar { background: #091015; color: #758995; }
    )"));
}

void MainWindow::toggleCapture()
{
    if (m_capture.isRunning()) {
        m_capture.stop();
        return;
    }

    m_audioBuffer.clear();
    m_lastResult = {};
    m_timegrapherPlot->clear();
    updateMeasurementUi({});
    if (m_capture.start(m_deviceCombo->currentIndex())) {
        m_sampleRate = m_capture.sampleRate();
        m_formatLabel->setText(m_capture.activeFormatDescription());
    }
}

void MainWindow::onDevicesChanged(const QStringList& devices)
{
    const QString previous = m_deviceCombo->currentText();
    m_deviceCombo->clear();
    m_deviceCombo->addItems(devices);
    const int oldIndex = m_deviceCombo->findText(previous);
    if (oldIndex >= 0)
        m_deviceCombo->setCurrentIndex(oldIndex);

    m_startButton->setEnabled(!devices.isEmpty());
    if (devices.isEmpty())
        setStatus(tr("Nessun ingresso audio rilevato"), true);
}

void MainWindow::onSamples(const QVector<float>& samples, int sampleRate)
{
    m_sampleRate = sampleRate;
    m_audioBuffer.append(samples);
    const qsizetype maximumSamples = static_cast<qsizetype>(sampleRate) * 30;
    if (m_audioBuffer.size() > maximumSamples)
        m_audioBuffer.remove(0, m_audioBuffer.size() - maximumSamples);

    const qsizetype waveformSamples =
        std::min<qsizetype>(m_audioBuffer.size(), sampleRate / 7);
    m_signalPlot->setSamples(
        m_audioBuffer.mid(m_audioBuffer.size() - waveformSamples, waveformSamples));
    m_saveWavButton->setEnabled(!m_audioBuffer.isEmpty());
}

void MainWindow::updateLevel(float peak, float rms)
{
    Q_UNUSED(rms);
    const double db = 20.0 * std::log10(std::max(peak, 1.0e-6f));
    const int normalized = static_cast<int>(
        std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * 1000.0);
    m_levelMeter->setValue(normalized);
    if (peak > 0.98f)
        m_levelMeter->setStyleSheet(QStringLiteral(
            "QProgressBar::chunk { background:#f08c75; border-radius:3px; }"));
    else
        m_levelMeter->setStyleSheet({});
}

AnalyzerConfig MainWindow::analyzerConfig() const
{
    AnalyzerConfig config;
    config.nominalBph = m_bphCombo->currentData().toDouble();
    config.liftAngleDegrees = m_liftAngleCombo->currentData().toDouble();
    return config;
}

void MainWindow::analyzeBuffer()
{
    if (m_sampleRate <= 0 || m_audioBuffer.size() < m_sampleRate * 2)
        return;

    const qsizetype analysisSamples =
        std::min<qsizetype>(m_audioBuffer.size(), static_cast<qsizetype>(m_sampleRate) * 18);
    const float* begin = m_audioBuffer.constData()
        + (m_audioBuffer.size() - analysisSamples);
    m_lastResult = m_analyzer.analyze(
        std::span<const float>(begin, static_cast<std::size_t>(analysisSamples)),
        m_sampleRate,
        analyzerConfig());
    updateMeasurementUi(m_lastResult);
    m_timegrapherPlot->setAnalysis(m_lastResult);
    m_exportButton->setEnabled(m_lastResult.valid);
}

void MainWindow::updateMeasurementUi(const AnalysisResult& result)
{
    if (!result.valid) {
        m_rateValue->setText(QStringLiteral("—"));
        m_amplitudeValue->setText(QStringLiteral("—"));
        m_beatErrorValue->setText(QStringLiteral("—"));
        m_bphValue->setText(QStringLiteral("—"));
        m_confidenceValue->setText(QStringLiteral("0"));
        if (!result.status.empty())
            setStatus(QString::fromStdString(result.status), true);
        return;
    }

    m_rateValue->setText(
        QStringLiteral("%1%2")
            .arg(result.rateSecondsPerDay >= 0.0 ? QStringLiteral("+") : QString())
            .arg(result.rateSecondsPerDay, 0, 'f', 1));
    m_amplitudeValue->setText(
        result.amplitudeAvailable
            ? QString::number(result.amplitudeDegrees, 'f', 0)
            : QStringLiteral("…"));
    m_amplitudeValue->setToolTip(result.amplitudeAvailable
        ? tr("Ampiezza calcolata con angolo di levata %1°")
              .arg(analyzerConfig().liftAngleDegrees)
        : tr("In validazione: non mostriamo un valore non dimostrato"));
    m_beatErrorValue->setText(
        QString::number(result.beatErrorMilliseconds, 'f', 2));
    m_bphValue->setText(
        QString::number(result.nominalBph, 'f', 0));
    m_bphValue->setToolTip(
        tr("Frequenza misurata: %1 A/h").arg(result.measuredBph, 0, 'f', 2));
    m_confidenceValue->setText(
        QString::number(result.confidence, 'f', 0));

    setStatus(
        tr("%1 · SNR %2 dB · jitter %3 ms · %4 impulsi validi")
            .arg(QString::fromStdString(result.status))
            .arg(result.signalToNoiseDb, 0, 'f', 1)
            .arg(result.intervalJitterMilliseconds, 0, 'f', 2)
            .arg(result.events.size()),
        result.confidence < 65.0);
}

void MainWindow::openWav()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Apri registrazione"), {}, tr("Audio WAV (*.wav)"));
    if (path.isEmpty())
        return;

    m_capture.stop();
    const WavData wav = WavFile::load(path);
    if (!wav.isValid()) {
        QMessageBox::critical(this, tr("WAV non leggibile"), wav.error);
        return;
    }

    m_audioBuffer = wav.samples;
    m_sampleRate = wav.sampleRate;
    const qsizetype waveformSamples =
        std::min<qsizetype>(m_audioBuffer.size(), m_sampleRate / 7);
    m_signalPlot->setSamples(
        m_audioBuffer.mid(m_audioBuffer.size() - waveformSamples, waveformSamples));
    m_formatLabel->setText(
        tr("File WAV · %1 Hz · mono normalizzato").arg(m_sampleRate));
    m_saveWavButton->setEnabled(true);
    analyzeBuffer();
    setStatus(tr("Registrazione caricata: %1").arg(path));
}

void MainWindow::saveWav()
{
    if (m_audioBuffer.isEmpty() || m_sampleRate <= 0)
        return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Salva segnale grezzo"), QStringLiteral("chronolab-session.wav"),
        tr("Audio WAV (*.wav)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive))
        path += QStringLiteral(".wav");

    QString error;
    if (!WavFile::savePcm16(path, m_audioBuffer, m_sampleRate, &error))
        QMessageBox::critical(this, tr("Salvataggio fallito"), error);
    else
        setStatus(tr("Segnale WAV salvato"));
}

void MainWindow::exportCsv()
{
    if (!m_lastResult.valid)
        return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Esporta misurazione"), QStringLiteral("chronolab-measurement.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Esportazione fallita"),
                              tr("Impossibile creare il file CSV"));
        return;
    }

    QTextStream out(&file);
    out << "parameter,value,unit\n"
        << "nominal_bph," << m_lastResult.nominalBph << ",bph\n"
        << "measured_bph," << m_lastResult.measuredBph << ",bph\n"
        << "rate," << m_lastResult.rateSecondsPerDay << ",s/day\n"
        << "beat_error," << m_lastResult.beatErrorMilliseconds << ",ms\n"
        << "confidence," << m_lastResult.confidence << ",percent\n"
        << "snr," << m_lastResult.signalToNoiseDb << ",dB\n"
        << "jitter," << m_lastResult.intervalJitterMilliseconds << ",ms\n"
        << "lift_angle," << analyzerConfig().liftAngleDegrees << ",degrees\n";
    setStatus(tr("Misurazione CSV esportata"));
}

void MainWindow::clearSession()
{
    m_capture.stop();
    m_audioBuffer.clear();
    m_sampleRate = 0;
    m_lastResult = {};
    m_signalPlot->setSamples({});
    m_timegrapherPlot->clear();
    m_levelMeter->setValue(0);
    m_formatLabel->setText(tr("Nessun flusso audio"));
    m_saveWavButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    updateMeasurementUi({});
    setStatus(tr("Nuova sessione pronta"));
}

void MainWindow::showAudioHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Preparare l'ingresso USB"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("Per acquisire il segnale senza alterazioni:"));
    box.setInformativeText(
        tr("1. Apri le proprietà audio del sensore in Windows.\n"
           "2. Disattiva miglioramenti, soppressione rumore, cancellazione eco e AGC.\n"
           "3. Imposta il livello senza raggiungere il rosso.\n"
           "4. Appoggia saldamente la cassa al sensore e non muovere il cavo.\n\n"
           "ChronoLab applica i propri filtri al segnale grezzo."));
    box.exec();
}

void MainWindow::setStatus(const QString& text, bool warning)
{
    m_statusLabel->setText(text);
    if (warning) {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "color:#efaa96; background:#0d151c; border:1px solid #5e3b36;"
            "border-radius:6px; padding:9px;"));
    } else {
        m_statusLabel->setStyleSheet({});
    }
    statusBar()->showMessage(text);
}

} // namespace chronolab
