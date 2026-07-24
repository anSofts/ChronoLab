#include "app/MainWindow.hpp"

#include "app/LanguageManager.hpp"
#include "audio/WavFile.hpp"
#include "core/SyntheticWatch.hpp"
#include "widgets/SignalPlotWidget.hpp"
#include "widgets/TimegrapherPlotWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

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

MainWindow::MainWindow(LanguageManager& languageManager, QWidget* parent)
    : QMainWindow(parent)
    , m_languageManager(languageManager)
    , m_capture(this)
{
    buildInterface();
    applyTheme();

    m_analysisWatcher = new QFutureWatcher<AnalysisJobResult>(this);
    connect(m_analysisWatcher, &QFutureWatcher<AnalysisJobResult>::finished,
            this, &MainWindow::finishAnalysis);

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
    setWindowTitle(tr("ChronoLab 0.3 — Open Timegrapher"));
    resize(1360, 850);
    setMinimumSize(1024, 680);
    loadSettings();
    setStatus(tr("Pronto. Seleziona il sensore USB e avvia l'ascolto."));
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (m_analysisWatcher && m_analysisWatcher->isRunning())
        m_analysisWatcher->waitForFinished();
}

void MainWindow::changeLanguage(int index)
{
    if (index < 0)
        return;

    const QString code = m_languageCombo->itemData(index).toString();
    m_languageManager.setLanguage(code);
    setStatus(tr("La modifica avrà effetto al prossimo avvio."));
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

    auto* languageLabel = new QLabel(tr("LINGUA"));
    languageLabel->setObjectName(QStringLiteral("controlLabel"));
    m_languageCombo = new QComboBox;
    m_languageCombo->setMinimumWidth(112);
    for (const auto& language : LanguageManager::availableLanguages())
        m_languageCombo->addItem(language.nativeName, language.code);
    const int languageIndex =
        m_languageCombo->findData(m_languageManager.currentLanguage());
    if (languageIndex >= 0)
        m_languageCombo->setCurrentIndex(languageIndex);

    auto* openButton = makeButton(tr("Apri WAV"));
    auto* simulationButton = makeButton(tr("Simulatore"));
    m_saveWavButton = makeButton(tr("Salva WAV"));
    m_exportButton = makeButton(tr("Esporta CSV"));
    auto* clearButton = makeButton(tr("Nuova sessione"));
    header->addWidget(languageLabel);
    header->addWidget(m_languageCombo);
    header->addSpacing(6);
    header->addWidget(openButton);
    header->addWidget(simulationButton);
    header->addWidget(m_saveWavButton);
    header->addWidget(m_exportButton);
    header->addWidget(clearButton);
    root->addLayout(header);

    connect(m_languageCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::changeLanguage);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openWav);
    connect(simulationButton, &QPushButton::clicked,
            this, &MainWindow::runSimulation);
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

    auto* positionBar = new QFrame;
    positionBar->setObjectName(QStringLiteral("controlBar"));
    auto* positionLayout = new QHBoxLayout(positionBar);
    positionLayout->setContentsMargins(15, 8, 15, 8);
    positionLayout->setSpacing(10);
    auto* positionLabel = new QLabel(tr("POSIZIONE"));
    positionLabel->setObjectName(QStringLiteral("controlLabel"));
    m_positionCombo = new QComboBox;
    m_positionCombo->setMinimumWidth(190);
    m_advancedPositionsCheck = new QCheckBox(tr("Modalità avanzata: 6 posizioni"));
    m_advancedPositionsCheck->setToolTip(
        tr("Facoltativa: la modalità standard usa solo quadrante e fondello"));
    m_capturePositionButton = makeButton(tr("Registra questa posizione"));
    m_sessionButton = makeButton(tr("Riepilogo (0/2)"));
    positionLayout->addWidget(positionLabel);
    positionLayout->addWidget(m_positionCombo);
    positionLayout->addWidget(m_advancedPositionsCheck);
    positionLayout->addStretch();
    positionLayout->addWidget(m_capturePositionButton);
    positionLayout->addWidget(m_sessionButton);
    root->addWidget(positionBar);

    connect(m_advancedPositionsCheck, &QCheckBox::toggled,
            this, &MainWindow::configurePositionMode);
    connect(m_capturePositionButton, &QPushButton::clicked,
            this, &MainWindow::capturePosition);
    connect(m_sessionButton, &QPushButton::clicked,
            this, &MainWindow::showPositionSummary);
    configurePositionMode(false);
    m_capturePositionButton->setEnabled(false);

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
        tr("ChronoLab 0.3 · GPL-3.0-or-later · Elaborazione locale, nessun dato inviato"));
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

    ++m_analysisGeneration;
    m_analysisPending = false;
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
    const int previous = m_deviceCombo->currentIndex();
    m_deviceCombo->clear();
    m_deviceCombo->addItems(devices);
    if (previous >= 0 && previous < m_deviceCombo->count())
        m_deviceCombo->setCurrentIndex(previous);

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

QString MainWindow::translatedAnalysisStatus(const std::string& status) const
{
    if (status == "Servono almeno due secondi di audio valido")
        return tr("Servono almeno due secondi di audio valido");
    if (status == "Segnale insufficiente: avvicinare o riposizionare il sensore")
        return tr("Segnale insufficiente: avvicinare o riposizionare il sensore");
    if (status == "Battito non identificato con sufficiente affidabilità")
        return tr("Battito non identificato con sufficiente affidabilità");
    if (status == "Troppi impulsi scartati: controllare il contatto del sensore")
        return tr("Troppi impulsi scartati: controllare il contatto del sensore");
    if (status == "Impossibile stimare la marcia")
        return tr("Impossibile stimare la marcia");
    if (status == "Misurazione stabile")
        return tr("Misurazione stabile");
    if (status == "Misurazione acquisita, qualità da migliorare")
        return tr("Misurazione acquisita, qualità da migliorare");
    return QString::fromStdString(status);
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

    if (m_analysisWatcher->isRunning()) {
        m_analysisPending = true;
        return;
    }

    const qsizetype analysisSamples =
        std::min<qsizetype>(m_audioBuffer.size(), static_cast<qsizetype>(m_sampleRate) * 18);
    const float* begin = m_audioBuffer.constData()
        + (m_audioBuffer.size() - analysisSamples);
    std::vector<float> samples(begin, begin + analysisSamples);
    const int sampleRate = m_sampleRate;
    const AnalyzerConfig config = analyzerConfig();
    const quint64 generation = m_analysisGeneration;

    m_analysisWatcher->setFuture(QtConcurrent::run(
        [samples = std::move(samples), sampleRate, config, generation]() {
            TimegrapherAnalyzer analyzer;
            AnalysisJobResult job;
            job.generation = generation;
            job.result = analyzer.analyze(samples, sampleRate, config);
            return job;
        }));
}

void MainWindow::finishAnalysis()
{
    const AnalysisJobResult job = m_analysisWatcher->result();
    if (job.generation == m_analysisGeneration) {
        m_lastResult = job.result;
        updateMeasurementUi(m_lastResult);
        m_timegrapherPlot->setAnalysis(m_lastResult);
        m_exportButton->setEnabled(m_lastResult.valid);
        m_capturePositionButton->setEnabled(m_lastResult.valid);
    }

    if (m_analysisPending) {
        m_analysisPending = false;
        QTimer::singleShot(0, this, &MainWindow::analyzeBuffer);
    }
}

void MainWindow::updateMeasurementUi(const AnalysisResult& result)
{
    if (!result.valid) {
        m_capturePositionButton->setEnabled(false);
        m_rateValue->setText(QStringLiteral("—"));
        m_amplitudeValue->setText(QStringLiteral("—"));
        m_beatErrorValue->setText(QStringLiteral("—"));
        m_bphValue->setText(QStringLiteral("—"));
        m_confidenceValue->setText(QStringLiteral("0"));
        if (!result.status.empty())
            setStatus(translatedAnalysisStatus(result.status), true);
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
            .arg(translatedAnalysisStatus(result.status))
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
    ++m_analysisGeneration;
    m_analysisPending = false;
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

void MainWindow::runSimulation()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Simulatore di orologio"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        tr("Genera un segnale di laboratorio che attraversa lo stesso motore "
           "DSP usato dal microfono. Non sostituisce la validazione reale."));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout;
    auto* bph = new QComboBox;
    for (const double value : TimegrapherAnalyzer::standardBeatRates())
        bph->addItem(QString::number(value, 'f', 0), value);
    bph->setCurrentIndex(bph->findData(21600.0));

    auto* rate = new QDoubleSpinBox;
    rate->setRange(-300.0, 300.0);
    rate->setDecimals(1);
    rate->setSuffix(tr(" s/g"));
    rate->setValue(8.0);

    auto* beatError = new QDoubleSpinBox;
    beatError->setRange(0.0, 9.9);
    beatError->setDecimals(2);
    beatError->setSuffix(tr(" ms"));
    beatError->setValue(0.40);

    auto* noise = new QDoubleSpinBox;
    noise->setRange(0.0, 0.10);
    noise->setDecimals(4);
    noise->setSingleStep(0.001);
    noise->setValue(0.004);

    auto* duration = new QSpinBox;
    duration->setRange(5, 60);
    duration->setSuffix(tr(" s"));
    duration->setValue(20);

    auto* dropEvery = new QSpinBox;
    dropEvery->setRange(0, 30);
    dropEvery->setSpecialValueText(tr("Nessuno"));
    dropEvery->setValue(0);

    form->addRow(tr("Frequenza:"), bph);
    form->addRow(tr("Marcia:"), rate);
    form->addRow(tr("Beat error:"), beatError);
    form->addRow(tr("Rumore:"), noise);
    form->addRow(tr("Durata:"), duration);
    form->addRow(tr("Perdi un impulso ogni:"), dropEvery);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Genera e analizza"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    SyntheticWatchConfig config;
    config.sampleRate = 48000;
    config.durationSeconds = duration->value();
    config.nominalBph = bph->currentData().toDouble();
    config.rateSecondsPerDay = rate->value();
    config.beatErrorMilliseconds = beatError->value();
    config.noiseLevel = noise->value();
    config.dropEvery = dropEvery->value();
    const std::vector<float> generated = SyntheticWatch::generate(config);

    m_capture.stop();
    ++m_analysisGeneration;
    m_analysisPending = false;
    m_audioBuffer.resize(static_cast<qsizetype>(generated.size()));
    std::copy(generated.begin(), generated.end(), m_audioBuffer.begin());
    m_sampleRate = config.sampleRate;
    m_bphCombo->setCurrentIndex(m_bphCombo->findData(0.0));

    const qsizetype waveformSamples =
        std::min<qsizetype>(m_audioBuffer.size(), m_sampleRate / 7);
    m_signalPlot->setSamples(
        m_audioBuffer.mid(m_audioBuffer.size() - waveformSamples, waveformSamples));
    m_formatLabel->setText(
        tr("SIMULAZIONE · %1 A/h · %2 Hz · segnale sintetico")
            .arg(config.nominalBph, 0, 'f', 0)
            .arg(config.sampleRate));
    m_levelMeter->setValue(820);
    m_saveWavButton->setEnabled(true);
    setStatus(tr("Simulazione generata; analisi in corso…"));
    analyzeBuffer();
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
    ++m_analysisGeneration;
    m_analysisPending = false;
    m_audioBuffer.clear();
    m_sampleRate = 0;
    m_lastResult = {};
    m_signalPlot->setSamples({});
    m_timegrapherPlot->clear();
    m_levelMeter->setValue(0);
    m_formatLabel->setText(tr("Nessun flusso audio"));
    m_saveWavButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    m_capturePositionButton->setEnabled(false);
    m_positionResults.fill(std::nullopt);
    updateSessionButton();
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

QStringList MainWindow::positionNames()
{
    return {
        tr("Quadrante in alto"),
        tr("Fondello in alto"),
        tr("Corona in alto"),
        tr("Corona in basso"),
        tr("Corona a sinistra"),
        tr("Corona a destra")
    };
}

void MainWindow::configurePositionMode(bool advanced)
{
    const int previousPosition = m_positionCombo->currentData().toInt();
    m_positionCombo->clear();
    const QStringList names = positionNames();
    const int count = advanced ? 6 : 2;
    for (int i = 0; i < count; ++i)
        m_positionCombo->addItem(names.at(i), i);

    const int restored = m_positionCombo->findData(previousPosition);
    if (restored >= 0)
        m_positionCombo->setCurrentIndex(restored);
    updateSessionButton();
}

void MainWindow::capturePosition()
{
    if (!m_lastResult.valid)
        return;

    const int position = m_positionCombo->currentData().toInt();
    if (position < 0 || position >= static_cast<int>(m_positionResults.size()))
        return;

    m_positionResults[static_cast<std::size_t>(position)] = m_lastResult;
    updateSessionButton();
    setStatus(tr("Misurazione registrata: %1")
                  .arg(m_positionCombo->currentText()));

    if (m_positionCombo->currentIndex() + 1 < m_positionCombo->count())
        m_positionCombo->setCurrentIndex(m_positionCombo->currentIndex() + 1);
}

void MainWindow::updateSessionButton()
{
    if (!m_sessionButton)
        return;

    const int total = m_advancedPositionsCheck
        && m_advancedPositionsCheck->isChecked() ? 6 : 2;
    int recorded = 0;
    for (int i = 0; i < total; ++i) {
        if (m_positionResults[static_cast<std::size_t>(i)].has_value())
            ++recorded;
    }
    m_sessionButton->setText(
        tr("Riepilogo (%1/%2)").arg(recorded).arg(total));
}

void MainWindow::showPositionSummary()
{
    const int total = m_advancedPositionsCheck->isChecked() ? 6 : 2;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Riepilogo posizionale"));
    dialog.resize(760, 400);
    auto* layout = new QVBoxLayout(&dialog);

    auto* modeLabel = new QLabel(total == 2
        ? tr("Sessione standard: quadrante e fondello")
        : tr("Sessione avanzata facoltativa: sei posizioni"));
    modeLabel->setObjectName(QStringLiteral("subtitle"));
    layout->addWidget(modeLabel);

    auto* table = new QTableWidget(total, 5);
    table->setHorizontalHeaderLabels({
        tr("Posizione"), tr("Marcia"), tr("Beat error"),
        tr("A/h"), tr("Affidabilità")
    });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    const QStringList names = positionNames();
    std::vector<double> rates;
    for (int row = 0; row < total; ++row) {
        table->setItem(row, 0, new QTableWidgetItem(names.at(row)));
        const auto& measurement = m_positionResults[static_cast<std::size_t>(row)];
        if (!measurement) {
            for (int column = 1; column < 5; ++column)
                table->setItem(row, column, new QTableWidgetItem(QStringLiteral("—")));
            continue;
        }

        rates.push_back(measurement->rateSecondsPerDay);
        table->setItem(row, 1, new QTableWidgetItem(
            tr("%1 s/g").arg(measurement->rateSecondsPerDay, 0, 'f', 1)));
        table->setItem(row, 2, new QTableWidgetItem(
            tr("%1 ms").arg(measurement->beatErrorMilliseconds, 0, 'f', 2)));
        table->setItem(row, 3, new QTableWidgetItem(
            QString::number(measurement->nominalBph, 'f', 0)));
        table->setItem(row, 4, new QTableWidgetItem(
            tr("%1%").arg(measurement->confidence, 0, 'f', 0)));
    }
    layout->addWidget(table);

    auto* summary = new QLabel;
    if (rates.empty()) {
        summary->setText(tr("Nessuna posizione registrata."));
    } else {
        const double mean = std::accumulate(rates.begin(), rates.end(), 0.0)
            / rates.size();
        const auto [minimum, maximum] =
            std::minmax_element(rates.begin(), rates.end());
        summary->setText(
            tr("Media: %1 s/g · Scarto posizionale: %2 s/g · Posizioni: %3/%4")
                .arg(mean, 0, 'f', 1)
                .arg(*maximum - *minimum, 0, 'f', 1)
                .arg(rates.size())
                .arg(total));
    }
    summary->setObjectName(QStringLiteral("analysisStatus"));
    layout->addWidget(summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* reset = buttons->addButton(
        tr("Azzera posizioni"), QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(reset, &QPushButton::clicked, &dialog, [this, &dialog]() {
        m_positionResults.fill(std::nullopt);
        updateSessionButton();
        dialog.accept();
    });
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::loadSettings()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());

    const int bphIndex = m_bphCombo->findData(
        settings.value(QStringLiteral("analysis/bph"), 0.0).toDouble());
    if (bphIndex >= 0)
        m_bphCombo->setCurrentIndex(bphIndex);

    const int angleIndex = m_liftAngleCombo->findData(
        settings.value(QStringLiteral("analysis/liftAngle"), 52.0).toDouble());
    if (angleIndex >= 0)
        m_liftAngleCombo->setCurrentIndex(angleIndex);

    const QString device = settings.value(
        QStringLiteral("audio/lastDevice")).toString();
    const int deviceIndex = m_deviceCombo->findText(device);
    const int savedDeviceIndex = settings.value(
        QStringLiteral("audio/lastDeviceIndex"), -1).toInt();
    if (deviceIndex >= 0) {
        m_deviceCombo->setCurrentIndex(deviceIndex);
    } else if (savedDeviceIndex >= 0
               && savedDeviceIndex < m_deviceCombo->count()) {
        m_deviceCombo->setCurrentIndex(savedDeviceIndex);
    }

    m_advancedPositionsCheck->setChecked(
        settings.value(QStringLiteral("session/sixPositions"), false).toBool());
}

void MainWindow::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("analysis/bph"), m_bphCombo->currentData());
    settings.setValue(
        QStringLiteral("analysis/liftAngle"), m_liftAngleCombo->currentData());
    settings.setValue(
        QStringLiteral("audio/lastDevice"), m_deviceCombo->currentText());
    settings.setValue(
        QStringLiteral("audio/lastDeviceIndex"), m_deviceCombo->currentIndex());
    settings.setValue(
        QStringLiteral("session/sixPositions"),
        m_advancedPositionsCheck->isChecked());
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
