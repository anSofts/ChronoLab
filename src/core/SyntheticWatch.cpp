#include "core/SyntheticWatch.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace chronolab {
namespace {

void addBurst(
    std::vector<float>& samples,
    int sampleRate,
    double eventTime,
    double offsetSeconds,
    double level,
    double frequency,
    double decay)
{
    const auto center = static_cast<std::size_t>(std::max(
        0.0, std::round((eventTime + offsetSeconds) * sampleRate)));
    const int length = std::max(4, static_cast<int>(sampleRate * 0.0055));
    for (int offset = 0; offset < length; ++offset) {
        const std::size_t index = center + static_cast<std::size_t>(offset);
        if (index >= samples.size())
            break;
        const double time = static_cast<double>(offset) / sampleRate;
        const double value = level * std::exp(-time * decay)
            * std::sin(2.0 * std::numbers::pi * frequency * time);
        samples[index] = static_cast<float>(
            std::clamp(samples[index] + value, -1.0, 1.0));
    }
}

} // namespace

std::vector<float> SyntheticWatch::generate(const SyntheticWatchConfig& config)
{
    if (config.sampleRate < 4000 || config.durationSeconds <= 0.0
        || config.nominalBph <= 0.0) {
        return {};
    }

    const auto count = static_cast<std::size_t>(
        std::llround(config.sampleRate * config.durationSeconds));
    std::vector<float> samples(count, 0.0f);

    std::uint32_t state = config.randomSeed;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        state = 1664525u * state + 1013904223u;
        const double unit = static_cast<double>((state >> 8) & 0xFFFFu) / 65535.0;
        const double whiteNoise = (unit * 2.0 - 1.0) * config.noiseLevel;
        const double mainsHum = config.noiseLevel * 0.12
            * std::sin(2.0 * std::numbers::pi * 50.0 * i / config.sampleRate);
        samples[i] = static_cast<float>(whiteNoise + mainsHum);
    }

    const double measuredBph = config.nominalBph
        * (1.0 + config.rateSecondsPerDay / 86400.0);
    const double meanPeriod = 3600.0 / measuredBph;
    const double halfBeatError = config.beatErrorMilliseconds / 2000.0;

    double eventTime = 0.45;
    long long beat = 0;
    while (eventTime < config.durationSeconds - 0.1) {
        const bool dropped = config.dropEvery > 0
            && beat % config.dropEvery == config.dropEvery - 1;
        if (!dropped) {
            // Three acoustic phases, intentionally not identical. They are a
            // deterministic laboratory signal, not a claim of physical
            // equivalence to a specific calibre.
            addBurst(samples, config.sampleRate, eventTime, 0.0000,
                     config.signalLevel * 0.58, 1720.0, 760.0);
            addBurst(samples, config.sampleRate, eventTime, 0.0032,
                     config.signalLevel, 1280.0, 590.0);
            addBurst(samples, config.sampleRate, eventTime, 0.0071,
                     config.signalLevel * 0.43, 2050.0, 860.0);
        }

        const double interval = meanPeriod
            + ((beat & 1LL) == 0 ? halfBeatError : -halfBeatError);
        eventTime += interval;
        ++beat;
    }
    return samples;
}

} // namespace chronolab
