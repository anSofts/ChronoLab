#include "core/TimegrapherAnalyzer.hpp"
#include "core/SyntheticWatch.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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
    chronolab::SyntheticWatchConfig config;
    config.sampleRate = sampleRate;
    config.durationSeconds = durationSeconds;
    config.nominalBph = nominalBph;
    config.rateSecondsPerDay = rateSecondsPerDay;
    config.beatErrorMilliseconds = beatErrorMilliseconds;
    config.noiseLevel = noiseLevel;
    config.dropEvery = dropEvery;
    return chronolab::SyntheticWatch::generate(config);
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

    require(result.valid,
            "analysis should be valid at "
                + std::to_string(bph) + " A/h: " + result.status);
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

void testKnownAmplitude()
{
    constexpr int sampleRate = 48000;
    chronolab::SyntheticWatchConfig config;
    config.sampleRate = sampleRate;
    config.durationSeconds = 20.0;
    config.nominalBph = 21600.0;
    config.amplitudeDegrees = 278.0;
    const auto samples = chronolab::SyntheticWatch::generate(config);
    chronolab::TimegrapherAnalyzer analyzer;
    const auto result = analyzer.analyze(samples, sampleRate);

    require(result.valid, "amplitude fixture should be valid");
    require(result.amplitudeAvailable,
            "known three-phase impulse should produce amplitude");
    require(std::abs(result.amplitudeDegrees - config.amplitudeDegrees) <= 25.0,
            "amplitude outside tolerance: got "
                + std::to_string(result.amplitudeDegrees));
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

void testChangingRealWorldResonancesRemainLocked()
{
    constexpr int sampleRate = 16000;
    chronolab::SyntheticWatchConfig config;
    config.sampleRate = sampleRate;
    config.durationSeconds = 30.0;
    config.nominalBph = 21600.0;
    config.rateSecondsPerDay = -9.0;
    config.beatErrorMilliseconds = 0.90;
    config.noiseLevel = 0.010;
    config.impulseShapeVariation = 1.0;
    config.dropEvery = 11;
    const auto samples = chronolab::SyntheticWatch::generate(config);

    chronolab::AnalyzerConfig analyzerConfig;
    analyzerConfig.nominalBph = config.nominalBph;
    chronolab::TimegrapherAnalyzer analyzer;
    for (int endSecond = 18; endSecond <= 30; ++endSecond) {
        const auto begin = samples.begin()
            + static_cast<std::ptrdiff_t>((endSecond - 18) * sampleRate);
        const auto end = begin + 18 * sampleRate;
        const std::vector<float> window(begin, end);
        const auto result =
            analyzer.analyze(window, sampleRate, analyzerConfig);
        require(result.valid,
                "changing impulse resonances must keep a valid lock");
        require(std::abs(result.rateSecondsPerDay + 9.0) < 2.0,
                "correlation lock allowed a real-world rate spike");
        require(std::abs(result.beatErrorMilliseconds - 0.90) < 0.25,
                "beat error became unstable with changing resonances: got "
                    + std::to_string(result.beatErrorMilliseconds));
    }
}

chronolab::AnalysisResult measurement(
    double rate,
    double beatError = 0.5,
    double confidence = 85.0)
{
    chronolab::AnalysisResult result;
    result.valid = true;
    result.nominalBph = 21600.0;
    result.measuredBph = 21600.0 * (1.0 + rate / 86400.0);
    result.rateSecondsPerDay = rate;
    result.beatErrorMilliseconds = beatError;
    result.confidence = confidence;
    result.status = "Misurazione stabile";
    return result;
}

void testTransientRateOutlierIsRejected()
{
    chronolab::MeasurementStabilizer stabilizer;
    static_cast<void>(stabilizer.processAt(measurement(8.0, 0.8), 0.0));
    static_cast<void>(stabilizer.processAt(measurement(7.6, 0.9), 0.5));
    const auto result =
        stabilizer.processAt(measurement(-500.0, 0.0), 1.0);

    require(result.valid, "a rejected outlier must preserve the stable result");
    require(std::abs(result.rateSecondsPerDay - 7.6) < 1.0,
            "single-window rate spike was allowed through");
    require(result.beatErrorMilliseconds > 0.5,
            "single-window beat-error collapse was allowed through");
}

void testPersistentRateChangeIsEventuallyAccepted()
{
    chronolab::MeasurementStabilizer stabilizer;
    static_cast<void>(stabilizer.processAt(measurement(8.0), 0.0));
    static_cast<void>(stabilizer.processAt(measurement(40.0), 1.0));
    static_cast<void>(stabilizer.processAt(measurement(41.0), 1.2));
    const auto result = stabilizer.processAt(measurement(39.0), 1.4);

    require(std::abs(result.rateSecondsPerDay - 39.0) < 1.0,
            "a confirmed new measurement cluster was not accepted");
}

void testConfidenceDisplayDoesNotPump()
{
    chronolab::MeasurementStabilizer stabilizer;
    std::vector<double> displayed;
    displayed.push_back(
        stabilizer.processAt(measurement(8.0, 0.8, 88.0), 0.0).confidence);
    displayed.push_back(
        stabilizer.processAt(measurement(8.1, 0.8, 55.0), 0.9).confidence);
    displayed.push_back(
        stabilizer.processAt(measurement(7.9, 0.8, 92.0), 1.8).confidence);
    displayed.push_back(
        stabilizer.processAt(measurement(8.2, 0.8, 50.0), 2.7).confidence);
    displayed.push_back(
        stabilizer.processAt(measurement(8.0, 0.8, 90.0), 3.6).confidence);

    for (std::size_t index = 1; index < displayed.size(); ++index) {
        require(std::abs(displayed[index] - displayed[index - 1]) < 4.0,
                "confidence display changed too abruptly");
    }
}

void testTransientInvalidWindowPreservesLock()
{
    chronolab::MeasurementStabilizer stabilizer;
    const auto stable =
        stabilizer.processAt(measurement(8.0, 0.8, 87.0), 0.0);

    chronolab::AnalysisResult invalid;
    invalid.status = "Battito non identificato con sufficiente affidabilità";
    const auto degraded = stabilizer.processAt(invalid, 0.6);

    require(degraded.valid,
            "a brief invalid window must preserve the last measurement");
    require(
        degraded.state == chronolab::MeasurementState::Degraded,
        "a held measurement must be marked as degraded");
    require(std::abs(degraded.rateSecondsPerDay - stable.rateSecondsPerDay)
                < 0.01,
            "a brief invalid window changed the held rate");
    require(degraded.confidence > 0.0 && degraded.confidence < stable.confidence,
            "degraded confidence must decay gradually instead of dropping to zero");
}

void testSustainedInvalidSignalEventuallyUnlocks()
{
    chronolab::MeasurementStabilizer stabilizer;
    static_cast<void>(
        stabilizer.processAt(measurement(8.0, 0.8, 87.0), 0.0));

    chronolab::AnalysisResult invalid;
    invalid.status = "Battito non identificato con sufficiente affidabilità";
    const auto lost = stabilizer.processAt(invalid, 3.1);

    require(!lost.valid,
            "a sustained invalid signal must eventually release the lock");
    require(lost.state == chronolab::MeasurementState::Lost,
            "a sustained invalid signal must be marked as lost");
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
        testKnownAmplitude();
        testManualBph();
        testMissedBeats();
        testChangingRealWorldResonancesRemainLocked();
        testTransientRateOutlierIsRejected();
        testPersistentRateChangeIsEventuallyAccepted();
        testConfidenceDisplayDoesNotPump();
        testTransientInvalidWindowPreservesLock();
        testSustainedInvalidSignalEventuallyUnlocks();
        testSilenceRejected();
    } catch (const TestFailure& failure) {
        std::cerr << "FAILED: " << failure.message << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All ChronoLab analyzer tests passed.\n";
    return EXIT_SUCCESS;
}
