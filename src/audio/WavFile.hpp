#pragma once

#include <QString>
#include <QVector>

namespace chronolab {

struct WavData {
    QVector<float> samples;
    int sampleRate = 0;
    QString error;

    [[nodiscard]] bool isValid() const {
        return sampleRate > 0 && !samples.isEmpty() && error.isEmpty();
    }
};

class WavFile {
public:
    [[nodiscard]] static WavData load(const QString& path);
    [[nodiscard]] static bool savePcm16(
        const QString& path,
        const QVector<float>& samples,
        int sampleRate,
        QString* error = nullptr);
};

} // namespace chronolab
