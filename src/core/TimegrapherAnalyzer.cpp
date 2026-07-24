#include "core/TimegrapherAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <utility>

namespace chronolab {
namespace {

constexpr double kSecondsPerDay = 86400.0;

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;

    const auto index = static_cast<std::size_t>(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

struct CandidateScore {
    double bph = 0.0;
    double score = 0.0;
};

CandidateScore scoreCandidate(const std::vector<double>& eventTimes, double bph)
{
    const double period = 3600.0 / bph;
    double scoreSum = 0.0;
    int accepted = 0;
    int considered = 0;

    for (std::size_t i = 1; i < eventTimes.size(); ++i) {
        const double delta = eventTimes[i] - eventTimes[i - 1];
        if (delta < 0.035 || delta > period * 5.25)
            continue;

        ++considered;
        const int missingFactor = std::clamp(
            static_cast<int>(std::llround(delta / period)), 1, 5);
        const double normalizedError =
            std::abs(delta - static_cast<double>(missingFactor) * period) / period;

        if (normalizedError <= 0.16) {
            const double fit = std::exp(-0.5 * std::pow(normalizedError / 0.045, 2.0));
            scoreSum += fit / std::sqrt(static_cast<double>(missingFactor));
            ++accepted;
        }
    }

    if (considered == 0)
        return {bph, 0.0};

    const double coverage = static_cast<double>(accepted) / considered;
    const double meanFit = accepted > 0 ? scoreSum / accepted : 0.0;
    return {bph, coverage * meanFit};
}

std::pair<double, double> linearFit(
    const std::vector<long long>& indices,
    const std::vector<double>& times)
{
    if (indices.size() < 2 || indices.size() != times.size())
        return {0.0, 0.0};

    const double meanX = std::accumulate(indices.begin(), indices.end(), 0.0)
        / static_cast<double>(indices.size());
    const double meanY = std::accumulate(times.begin(), times.end(), 0.0)
        / static_cast<double>(times.size());

    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const double dx = static_cast<double>(indices[i]) - meanX;
        covariance += dx * (times[i] - meanY);
        variance += dx * dx;
    }

    if (variance <= std::numeric_limits<double>::epsilon())
        return {0.0, 0.0};

    const double slope = covariance / variance;
    return {meanY - slope * meanX, slope};
}

} // namespace

const std::vector<double>& TimegrapherAnalyzer::standardBeatRates()
{
    static const std::vector<double> rates {
        7200, 9000, 12000, 14400, 18000, 19800,
        21600, 25200, 28800, 36000, 43200
    };
    return rates;
}

AnalysisResult TimegrapherAnalyzer::analyze(
    std::span<const float> samples,
    int sampleRate,
    const AnalyzerConfig& config) const
{
    AnalysisResult result;
    if (sampleRate < 4000 || samples.size() < static_cast<std::size_t>(sampleRate * 2)) {
        result.status = "Servono almeno due secondi di audio valido";
        return result;
    }

    // A first-order high-pass removes DC and slow mechanical handling noise.
    const double cutoffHz = 90.0;
    const double dt = 1.0 / static_cast<double>(sampleRate);
    const double rc = 1.0 / (2.0 * std::numbers::pi * cutoffHz);
    const double alpha = rc / (rc + dt);
    std::vector<double> filtered(samples.size());
    double previousInput = samples.front();
    double previousOutput = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double value = alpha * (previousOutput + samples[i] - previousInput);
        filtered[i] = value;
        previousInput = samples[i];
        previousOutput = value;
    }

    // Short RMS envelope: long enough to reject single-sample spikes, short
    // enough to preserve the escapement's impulse cluster.
    const int window = std::max(3, static_cast<int>(std::lround(sampleRate * 0.0009)));
    std::vector<double> envelope(samples.size());
    double energy = 0.0;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        energy += filtered[i] * filtered[i];
        if (i >= static_cast<std::size_t>(window))
            energy -= filtered[i - window] * filtered[i - window];
        envelope[i] = std::sqrt(std::max(0.0, energy) / window);
    }

    std::vector<double> noiseSample;
    const std::size_t stride = std::max<std::size_t>(1, samples.size() / 12000);
    noiseSample.reserve(samples.size() / stride + 1);
    for (std::size_t i = 0; i < envelope.size(); i += stride)
        noiseSample.push_back(envelope[i]);

    const double median = percentile(noiseSample, 0.50);
    std::vector<double> deviations;
    deviations.reserve(noiseSample.size());
    for (const double value : noiseSample)
        deviations.push_back(std::abs(value - median));
    const double mad = percentile(std::move(deviations), 0.50);
    const double highLevel = percentile(noiseSample, 0.98);
    const double threshold = median
        + std::max(config.detectionSensitivity * 1.4826 * mad, median * 1.8 + 1.0e-7);

    result.signalToNoiseDb = 20.0 * std::log10(
        std::max(highLevel, 1.0e-9) / std::max(median, 1.0e-9));

    const int refractorySamples =
        std::max(1, static_cast<int>(std::lround(sampleRate * 0.028)));
    std::vector<double> eventTimes;
    std::vector<double> eventStrengths;

    for (std::size_t i = 1; i + 1 < envelope.size();) {
        if (envelope[i] < threshold) {
            ++i;
            continue;
        }

        const std::size_t regionEnd = std::min(
            envelope.size() - 1, i + static_cast<std::size_t>(refractorySamples));
        std::size_t peakIndex = i;
        for (std::size_t cursor = i + 1; cursor <= regionEnd; ++cursor) {
            if (envelope[cursor] > envelope[peakIndex])
                peakIndex = cursor;
        }

        eventTimes.push_back(static_cast<double>(peakIndex) / sampleRate);
        eventStrengths.push_back(envelope[peakIndex] / std::max(threshold, 1.0e-9));
        i = regionEnd + 1;
    }

    if (eventTimes.size() < 8) {
        result.status = "Segnale insufficiente: avvicinare o riposizionare il sensore";
        return result;
    }

    CandidateScore best;
    if (config.nominalBph > 0.0) {
        best = scoreCandidate(eventTimes, config.nominalBph);
    } else {
        for (const double bph : standardBeatRates()) {
            if (bph < config.minimumBph || bph > config.maximumBph)
                continue;
            const CandidateScore candidate = scoreCandidate(eventTimes, bph);
            if (candidate.score > best.score)
                best = candidate;
        }
    }

    if (best.bph <= 0.0 || best.score < 0.20) {
        result.status = "Battito non identificato con sufficiente affidabilità";
        return result;
    }

    const double nominalPeriod = 3600.0 / best.bph;
    std::vector<long long> beatIndices;
    std::vector<double> acceptedTimes;
    std::vector<double> acceptedStrengths;
    beatIndices.reserve(eventTimes.size());
    acceptedTimes.reserve(eventTimes.size());
    acceptedStrengths.reserve(eventTimes.size());

    long long beatIndex = 0;
    beatIndices.push_back(beatIndex);
    acceptedTimes.push_back(eventTimes.front());
    acceptedStrengths.push_back(eventStrengths.front());

    for (std::size_t i = 1; i < eventTimes.size(); ++i) {
        const double delta = eventTimes[i] - acceptedTimes.back();
        const int step = std::clamp(
            static_cast<int>(std::llround(delta / nominalPeriod)), 1, 5);
        const double error = std::abs(delta - step * nominalPeriod) / nominalPeriod;
        if (error > 0.16)
            continue;

        beatIndex += step;
        beatIndices.push_back(beatIndex);
        acceptedTimes.push_back(eventTimes[i]);
        acceptedStrengths.push_back(eventStrengths[i]);
    }

    if (acceptedTimes.size() < 8) {
        result.status = "Troppi impulsi scartati: controllare il contatto del sensore";
        return result;
    }

    const auto [intercept, secondsPerBeat] = linearFit(beatIndices, acceptedTimes);
    if (secondsPerBeat <= 0.0) {
        result.status = "Impossibile stimare la marcia";
        return result;
    }

    result.nominalBph = best.bph;
    result.measuredBph = 3600.0 / secondsPerBeat;
    result.rateSecondsPerDay =
        (result.measuredBph / result.nominalBph - 1.0) * kSecondsPerDay;

    std::vector<double> evenIntervals;
    std::vector<double> oddIntervals;
    std::vector<double> intervalErrors;
    for (std::size_t i = 1; i < acceptedTimes.size(); ++i) {
        const long long step = beatIndices[i] - beatIndices[i - 1];
        if (step != 1)
            continue;
        const double interval = acceptedTimes[i] - acceptedTimes[i - 1];
        intervalErrors.push_back(interval - secondsPerBeat);
        ((beatIndices[i - 1] & 1LL) == 0 ? evenIntervals : oddIntervals).push_back(interval);
    }

    if (!evenIntervals.empty() && !oddIntervals.empty()) {
        const double evenMean = std::accumulate(evenIntervals.begin(), evenIntervals.end(), 0.0)
            / evenIntervals.size();
        const double oddMean = std::accumulate(oddIntervals.begin(), oddIntervals.end(), 0.0)
            / oddIntervals.size();
        result.beatErrorMilliseconds = std::abs(evenMean - oddMean) * 1000.0;
    }

    if (!intervalErrors.empty()) {
        const double squared = std::inner_product(
            intervalErrors.begin(), intervalErrors.end(),
            intervalErrors.begin(), 0.0);
        result.intervalJitterMilliseconds =
            std::sqrt(squared / intervalErrors.size()) * 1000.0;
    }

    result.events.reserve(acceptedTimes.size());
    result.stripResidualMilliseconds.reserve(acceptedTimes.size());
    for (std::size_t i = 0; i < acceptedTimes.size(); ++i) {
        result.events.push_back({
            acceptedTimes[i],
            acceptedStrengths[i],
            beatIndices[i]
        });
        result.stripResidualMilliseconds.push_back(
            (acceptedTimes[i] - (intercept + secondsPerBeat * beatIndices[i])) * 1000.0);
    }

    const double eventFactor = clamp01((acceptedTimes.size() - 7.0) / 30.0);
    const double snrFactor = clamp01((result.signalToNoiseDb - 6.0) / 22.0);
    const double jitterFactor = std::exp(
        -std::pow(result.intervalJitterMilliseconds / 1.8, 2.0));
    result.confidence = 100.0 * clamp01(
        0.50 * best.score + 0.20 * eventFactor
        + 0.15 * snrFactor + 0.15 * jitterFactor);

    // Amplitude deliberately remains unavailable until the three acoustic
    // escapement phases can be validated against real reference recordings.
    result.amplitudeAvailable = false;
    result.valid = true;
    result.status = result.confidence >= 65.0
        ? "Misurazione stabile"
        : "Misurazione acquisita, qualità da migliorare";
    return result;
}

} // namespace chronolab
