#include "audio/AudioCapture.hpp"

#include <QAudioSource>
#include <QByteArray>
#include <QIODevice>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chronolab {

AudioCapture::AudioCapture(QObject* parent)
    : QObject(parent)
{
    connect(&m_mediaDevices, &QMediaDevices::audioInputsChanged,
            this, &AudioCapture::refreshDevices);
    refreshDevices();
}

AudioCapture::~AudioCapture()
{
    stop();
}

QStringList AudioCapture::inputDeviceNames() const
{
    QStringList names;
    names.reserve(m_devices.size());
    for (const auto& device : m_devices) {
        QString name = device.description();
        if (device.isDefault())
            name += tr(" (predefinito)");
        names.push_back(name);
    }
    return names;
}

bool AudioCapture::isRunning() const
{
    return m_running;
}

int AudioCapture::sampleRate() const
{
    return m_format.sampleRate();
}

QString AudioCapture::activeFormatDescription() const
{
    if (!m_format.isValid())
        return tr("Nessun formato");

    QString formatName;
    switch (m_format.sampleFormat()) {
    case QAudioFormat::UInt8: formatName = QStringLiteral("UInt8"); break;
    case QAudioFormat::Int16: formatName = QStringLiteral("Int16"); break;
    case QAudioFormat::Int32: formatName = QStringLiteral("Int32"); break;
    case QAudioFormat::Float: formatName = QStringLiteral("Float32"); break;
    default: formatName = tr("Sconosciuto"); break;
    }

    return tr("%1 Hz · %2 canale/i · %3")
        .arg(m_format.sampleRate())
        .arg(m_format.channelCount())
        .arg(formatName);
}

void AudioCapture::refreshDevices()
{
    const auto refreshed = QMediaDevices::audioInputs();
    const bool changed = refreshed.size() != m_devices.size()
        || !std::equal(refreshed.begin(), refreshed.end(), m_devices.begin(),
                      [](const QAudioDevice& left, const QAudioDevice& right) {
                          return left.id() == right.id();
                      });
    m_devices = refreshed;
    if (changed)
        emit devicesChanged(inputDeviceNames());
}

QAudioFormat AudioCapture::chooseFormat(const QAudioDevice& device) const
{
    const QList<int> rates {48000, 44100, 96000};
    const QList<QAudioFormat::SampleFormat> sampleFormats {
        QAudioFormat::Int16,
        QAudioFormat::Float,
        QAudioFormat::Int32
    };

    for (const int rate : rates) {
        for (const auto sampleFormat : sampleFormats) {
            QAudioFormat candidate;
            candidate.setSampleRate(rate);
            candidate.setChannelCount(1);
            candidate.setSampleFormat(sampleFormat);
            if (device.isFormatSupported(candidate))
                return candidate;
        }
    }

    QAudioFormat preferred = device.preferredFormat();
    if (preferred.channelCount() > 2)
        preferred.setChannelCount(2);
    return preferred;
}

bool AudioCapture::start(int deviceIndex)
{
    stop();
    refreshDevices();
    if (deviceIndex < 0 || deviceIndex >= m_devices.size()) {
        emit captureError(tr("Selezionare un ingresso audio valido"));
        return false;
    }

    const QAudioDevice device = m_devices.at(deviceIndex);
    m_format = chooseFormat(device);
    if (!m_format.isValid()) {
        emit captureError(tr("Il dispositivo non espone un formato audio utilizzabile"));
        return false;
    }

    m_source = std::make_unique<QAudioSource>(device, m_format);
    m_source->setBufferSize(std::max<qsizetype>(
        4096, m_format.bytesForDuration(120000)));
    connect(m_source.get(), &QAudioSource::stateChanged, this,
            [this](QtAudio::State state) {
                if (!m_source)
                    return;
                if (state == QtAudio::StoppedState
                    && m_source->error() != QtAudio::NoError) {
                    emit captureError(tr("Acquisizione interrotta (errore audio %1)")
                                          .arg(static_cast<int>(m_source->error())));
                    stop();
                }
            });

    m_stream = m_source->start();
    if (!m_stream) {
        emit captureError(tr("Impossibile avviare l'ingresso audio"));
        m_source.reset();
        return false;
    }

    connect(m_stream, &QIODevice::readyRead,
            this, &AudioCapture::readAvailableAudio);
    m_running = true;
    emit runningChanged(true);
    emit statusChanged(tr("In ascolto: %1 · %2")
                           .arg(device.description(), activeFormatDescription()));
    return true;
}

void AudioCapture::stop()
{
    if (m_source)
        m_source->stop();
    m_stream = nullptr;
    m_source.reset();
    if (m_running) {
        m_running = false;
        emit runningChanged(false);
        emit statusChanged(tr("Acquisizione arrestata"));
    }
}

QVector<float> AudioCapture::decode(const QByteArray& bytes) const
{
    QVector<float> output;
    const int channels = std::max(1, m_format.channelCount());
    const int bytesPerSample = m_format.bytesPerSample();
    const int bytesPerFrame = bytesPerSample * channels;
    if (bytesPerFrame <= 0)
        return output;

    const qsizetype frames = bytes.size() / bytesPerFrame;
    output.resize(frames);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());

    auto sampleAt = [&](qsizetype frame, int channel) -> float {
        const auto* pointer = data + frame * bytesPerFrame + channel * bytesPerSample;
        switch (m_format.sampleFormat()) {
        case QAudioFormat::UInt8:
            return (static_cast<int>(*pointer) - 128) / 128.0f;
        case QAudioFormat::Int16: {
            qint16 value;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<float>(value / 32768.0);
        }
        case QAudioFormat::Int32: {
            qint32 value;
            std::memcpy(&value, pointer, sizeof(value));
            return static_cast<float>(value / 2147483648.0);
        }
        case QAudioFormat::Float: {
            float value;
            std::memcpy(&value, pointer, sizeof(value));
            return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
        }
        default:
            return 0.0f;
        }
    };

    for (qsizetype frame = 0; frame < frames; ++frame) {
        float mixed = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            mixed += sampleAt(frame, channel);
        output[frame] = mixed / channels;
    }
    return output;
}

void AudioCapture::readAvailableAudio()
{
    if (!m_stream)
        return;

    const QByteArray bytes = m_stream->readAll();
    QVector<float> samples = decode(bytes);
    if (samples.isEmpty())
        return;

    float peak = 0.0f;
    double squared = 0.0;
    for (const float sample : samples) {
        peak = std::max(peak, std::abs(sample));
        squared += static_cast<double>(sample) * sample;
    }
    const float rms = static_cast<float>(std::sqrt(squared / samples.size()));
    emit levelChanged(peak, rms);
    emit samplesReady(samples, m_format.sampleRate());
}

} // namespace chronolab
