#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <memory>

class QAudioSource;
class QIODevice;

namespace chronolab {

class AudioCapture final : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    [[nodiscard]] QStringList inputDeviceNames() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] QString activeFormatDescription() const;

public slots:
    void refreshDevices();
    bool start(int deviceIndex);
    void stop();

signals:
    void devicesChanged(const QStringList& devices);
    void samplesReady(const QVector<float>& samples, int sampleRate);
    void levelChanged(float peak, float rms);
    void runningChanged(bool running);
    void statusChanged(const QString& status);
    void captureError(const QString& message);

private slots:
    void readAvailableAudio();

private:
    QAudioFormat chooseFormat(const QAudioDevice& device) const;
    QVector<float> decode(const QByteArray& bytes) const;

    QMediaDevices m_mediaDevices;
    QList<QAudioDevice> m_devices;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice* m_stream = nullptr;
    QAudioFormat m_format;
    bool m_running = false;
};

} // namespace chronolab
