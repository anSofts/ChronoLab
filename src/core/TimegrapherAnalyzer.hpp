#pragma once

#include <span>
#include <string>
#include <vector>

namespace chronolab {

struct AnalyzerConfig {
    double nominalBph = 0.0;       // 0 = automatic detection
    double liftAngleDegrees = 52.0;
    double minimumBph = 7200.0;
    double maximumBph = 43200.0;
    double detectionSensitivity = 6.0;
};

struct BeatEvent {
    double timeSeconds = 0.0;
    double strength = 0.0;
    long long beatIndex = 0;
};

struct AnalysisResult {
    bool valid = false;
    double nominalBph = 0.0;
    double measuredBph = 0.0;
    double rateSecondsPerDay = 0.0;
    double beatErrorMilliseconds = 0.0;
    double amplitudeDegrees = 0.0;
    bool amplitudeAvailable = false;
    double confidence = 0.0;
    double signalToNoiseDb = 0.0;
    double intervalJitterMilliseconds = 0.0;
    std::vector<BeatEvent> events;
    std::vector<double> stripResidualMilliseconds;
    std::string status;
};

class TimegrapherAnalyzer {
public:
    [[nodiscard]] AnalysisResult analyze(
        std::span<const float> samples,
        int sampleRate,
        const AnalyzerConfig& config = {}) const;

    [[nodiscard]] static const std::vector<double>& standardBeatRates();
};

class MeasurementStabilizer {
public:
    [[nodiscard]] AnalysisResult process(const AnalysisResult& candidate);
    void reset();

private:
    std::vector<AnalysisResult> m_history;
    AnalysisResult m_pendingCandidate;
    int m_pendingCount = 0;
};

} // namespace chronolab
