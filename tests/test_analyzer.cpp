#include "core/TimegrapherAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestFailure {
    std::string message;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw TestFailure {message};
}

std::vector<float> synthesizeWatch(
    int sampleRate,
    double durationSeconds,
    double nominalBph,
    double rateSecondsPerDay,
    double beatErrorMilliseconds,
    double noiseLevel = 0.004,
    int dropEvery = 0)
{
    const std::size_t count = static_cast<std::size_t>(
        std::lround(sampleRate * durationSeconds));
    std::vector<float> samples(count, 0.0f);

    unsigned int randomState = 0xC0FFEEu;
    for (float& sample : samples) {
        randomState = 1664525u * randomState + 1013904223u;
        const double unit = static_cast<double>((randomState >> 8) & 0xFFFFu) / 65535.0;
        sample = static_cast<float>((unit * 2.0 - 1.0) * noiseLevel);
    }

    const double measuredBph =
        nominalBph * (1.0 + rateSecondsPerDay / 86400.0);
    const double meanPeriod = 3600.0 / measuredBph;
    const double halfBeatError = beatErrorMilliseconds / 2000.0;

    double eventTime = 0.45;
    long long beat = 0;
    while (eventTime < durationSeconds - 0.1) {
        if (dropEvery <= 0 || beat % dropEvery != dropEvery - 1) {
            const std::size_t center = static_cast<std::size_t>(
                std::lround(eventTime * sampleRate));

            // A damped high-frequency burst approximates the contact pickup's
            // escapement transient and exercises the envelope detector.
            const int burstLength = static_cast<int>(sampleRate * 0.006);
            for (int offset = 0; offset < burstLength; ++offset) {
                const std::size_t index = center + static_cast<std::size_t>(offset);
                if (index >= samples.size())
                    break;
                const double time = static_cast<double>(offset) / sampleRate;
                const double burst = 0.72 * std::exp(-time * 600.0)
                    * std::sin(2.0 * 3.14159265358979323846 * 1450.0 * time);
                samples[index] += static_cast<float>(burst);
            }
        }

        const double interval = meanPeriod
            + ((beat & 1LL) == 0 ? halfBeatError : -halfBeatError);
        eventTime += interval;
        ++beat;
    }
    return samples;
}

void testKnownRate(
    double bph,
    double expectedRate,
    double expectedBeatError,
    double rateTolerance,
    double beatErrorTolerance)
{
    constexpr int sampleRate = 16000;
    const auto samples = synthesizeWatch(
        sampleRate, 20.0, bph, expectedRate, expectedBeatError);
    chronolab::TimegrapherAnalyzer analyzer;
    const auto result = analyzer.analyze(samples, sampleRate);

    require(result.valid, "analysis should be valid: " + result.status);
    require(std::abs(result.nominalBph - bph) < 1.0,
            "wrong nominal BPH");
    require(std::abs(result.rateSecondsPerDay - expectedRate) <= rateTolerance,
            "rate outside tolerance: got "
                + std::to_string(result.rateSecondsPerDay));
    require(std::abs(result.beatErrorMilliseconds - expectedBeatError)
                <= beatErrorTolerance,
            "beat error outside tolerance: got "
                + std::to_string(result.beatErrorMilliseconds));
    require(result.confidence >= 60.0, "confidence unexpectedly low");
}

void testSilenceRejected()
{
    constexpr int sampleRate = 8000;
    std::vector<float> silence(sampleRate * 5, 0.0f);
    chronolab::TimegrapherAnalyzer analyzer;
    const auto result = analyzer.analyze(silence, sampleRate);
    require(!result.valid, "silence must not produce a measurement");
}

void testManualBph()
{
    constexpr int sampleRate = 16000;
    const auto samples = synthesizeWatch(sampleRate, 16.0, 19800.0, 4.0, 0.2);
    chronolab::AnalyzerConfig config;
    config.nominalBph = 19800.0;
    chronolab::TimegrapherAnalyzer analyzer;
    const auto result = analyzer.analyze(samples, sampleRate, config);
    require(result.valid, "manual BPH analysis should be valid");
    require(result.nominalBph == 19800.0, "manual BPH was not preserved");
}

void testMissedBeats()
{
    constexpr int sampleRate = 16000;
    const auto samples = synthesizeWatch(
        sampleRate, 22.0, 21600.0, 6.0, 0.4, 0.004, 7);
    chronolab::TimegrapherAnalyzer analyzer;
    const auto result = analyzer.analyze(samples, sampleRate);
    require(result.valid, "analysis should tolerate regularly missed beats");
    require(result.nominalBph == 21600.0, "dropouts changed BPH detection");
    require(std::abs(result.rateSecondsPerDay - 6.0) < 2.0,
            "dropouts distorted rate");
}

} // namespace

int main()
{
    try {
        testKnownRate(7200.0, 3.0, 0.50, 2.0, 0.20);
        testKnownRate(9000.0, -7.0, 0.60, 2.0, 0.20);
        testKnownRate(21600.0, 8.0, 0.40, 1.6, 0.15);
        testKnownRate(18000.0, -12.0, 0.70, 1.8, 0.18);
        testKnownRate(14400.0, 5.0, 0.30, 2.0, 0.20);
        testKnownRate(28800.0, -3.0, 0.20, 1.6, 0.15);
        testManualBph();
        testMissedBeats();
        testSilenceRejected();
    } catch (const TestFailure& failure) {
        std::cerr << "FAILED: " << failure.message << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All ChronoLab analyzer tests passed.\n";
    return EXIT_SUCCESS;
}
