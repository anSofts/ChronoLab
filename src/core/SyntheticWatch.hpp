#pragma once

#include <cstdint>
#include <vector>

namespace chronolab {

struct SyntheticWatchConfig {
    int sampleRate = 48000;
    double durationSeconds = 20.0;
    double nominalBph = 21600.0;
    double rateSecondsPerDay = 8.0;
    double beatErrorMilliseconds = 0.4;
    double noiseLevel = 0.004;
    double signalLevel = 0.72;
    double impulseShapeVariation = 0.0;
    int dropEvery = 0;
    std::uint32_t randomSeed = 0xC0FFEEu;
};

class SyntheticWatch {
public:
    [[nodiscard]] static std::vector<float> generate(
        const SyntheticWatchConfig& config);
};

} // namespace chronolab
