#include "audio/WavFile.hpp"

#include <QDataStream>
#include <QFile>
#include <QObject>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chronolab {
namespace {

quint32 fourCc(const char text[5])
{
    return static_cast<quint32>(static_cast<unsigned char>(text[0]))
        | (static_cast<quint32>(static_cast<unsigned char>(text[1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(text[2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(text[3])) << 24);
}

} // namespace

WavData WavFile::load(const QString& path)
{
    WavData result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QObject::tr("Impossibile aprire il file WAV");
        return result;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 riff = 0;
    quint32 fileSize = 0;
    quint32 wave = 0;
    stream >> riff >> fileSize >> wave;
    Q_UNUSED(fileSize);
    if (riff != fourCc("RIFF") || wave != fourCc("WAVE")) {
        result.error = QObject::tr("Il file non è un WAV RIFF valido");
        return result;
    }

    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bitsPerSample = 0;
    QByteArray audioBytes;

    while (!stream.atEnd()) {
        quint32 chunkId = 0;
        quint32 chunkSize = 0;
        stream >> chunkId >> chunkSize;
        if (stream.status() != QDataStream::Ok)
            break;

        if (chunkId == fourCc("fmt ")) {
            quint32 byteRate = 0;
            quint16 blockAlign = 0;
            stream >> audioFormat >> channels >> sampleRate
                   >> byteRate >> blockAlign >> bitsPerSample;
            Q_UNUSED(byteRate);
            Q_UNUSED(blockAlign);
            if (chunkSize > 16)
                stream.skipRawData(static_cast<int>(chunkSize - 16));
        } else if (chunkId == fourCc("data")) {
            audioBytes.resize(static_cast<qsizetype>(chunkSize));
            if (stream.readRawData(audioBytes.data(), static_cast<int>(chunkSize))
                != static_cast<int>(chunkSize)) {
                result.error = QObject::tr("Dati audio WAV incompleti");
                return result;
            }
        } else {
            stream.skipRawData(static_cast<int>(chunkSize));
        }

        if ((chunkSize & 1U) != 0)
            stream.skipRawData(1);
    }

    if (channels == 0 || sampleRate == 0 || audioBytes.isEmpty()) {
        result.error = QObject::tr("Formato WAV incompleto");
        return result;
    }

    const int bytesPerSample = bitsPerSample / 8;
    const int frameSize = bytesPerSample * channels;
    if (frameSize <= 0) {
        result.error = QObject::tr("Profondità WAV non supportata");
        return result;
    }

    const qsizetype frameCount = audioBytes.size() / frameSize;
    result.samples.resize(frameCount);
    const auto* data = reinterpret_cast<const unsigned char*>(audioBytes.constData());

    for (qsizetype frame = 0; frame < frameCount; ++frame) {
        double mixed = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            const auto* pointer = data + frame * frameSize + channel * bytesPerSample;
            double sample = 0.0;
            if (audioFormat == 1 && bitsPerSample == 16) {
                qint16 value;
                std::memcpy(&value, pointer, sizeof(value));
                sample = value / 32768.0;
            } else if (audioFormat == 1 && bitsPerSample == 24) {
                qint32 value = static_cast<qint32>(pointer[0])
                    | (static_cast<qint32>(pointer[1]) << 8)
                    | (static_cast<qint32>(pointer[2]) << 16);
                if ((value & 0x00800000) != 0)
                    value |= ~static_cast<qint32>(0x00FFFFFF);
                sample = value / 8388608.0;
            } else if (audioFormat == 1 && bitsPerSample == 32) {
                qint32 value;
                std::memcpy(&value, pointer, sizeof(value));
                sample = value / 2147483648.0;
            } else if (audioFormat == 3 && bitsPerSample == 32) {
                float value;
                std::memcpy(&value, pointer, sizeof(value));
                sample = std::isfinite(value) ? value : 0.0;
            } else {
                result.error = QObject::tr("WAV %1 bit formato %2 non supportato")
                                   .arg(bitsPerSample)
                                   .arg(audioFormat);
                result.samples.clear();
                return result;
            }
            mixed += sample;
        }
        result.samples[frame] = static_cast<float>(
            std::clamp(mixed / channels, -1.0, 1.0));
    }

    result.sampleRate = static_cast<int>(sampleRate);
    return result;
}

bool WavFile::savePcm16(
    const QString& path,
    const QVector<float>& samples,
    int sampleRate,
    QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("Impossibile creare il file");
        return false;
    }

    const quint16 channels = 1;
    const quint16 bitsPerSample = 16;
    const quint32 dataSize = static_cast<quint32>(samples.size() * sizeof(qint16));
    const quint32 riffSize = 36 + dataSize;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << fourCc("RIFF") << riffSize << fourCc("WAVE")
           << fourCc("fmt ") << quint32(16)
           << quint16(1) << channels << quint32(sampleRate)
           << quint32(sampleRate * channels * bitsPerSample / 8)
           << quint16(channels * bitsPerSample / 8) << bitsPerSample
           << fourCc("data") << dataSize;

    for (const float sample : samples) {
        const qint16 value = static_cast<qint16>(
            std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        stream << value;
    }

    if (stream.status() != QDataStream::Ok) {
        if (error)
            *error = QObject::tr("Errore durante la scrittura del WAV");
        return false;
    }
    return true;
}

} // namespace chronolab
