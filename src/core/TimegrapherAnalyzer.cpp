#include "core/TimegrapherAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <utility>

namespace chronolab {
namespace {

constexpr double kSecondsPerDay = 86400.0;
constexpr double kCorrelationFilterHz = 3000.0;
constexpr double kCorrelationSampleRate = 8000.0;

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
    std::nth_element(
        values.begin(),
        values.begin() + static_cast<std::ptrdiff_t>(index),
        values.end());
    return values[index];
}

double medianOfResults(
    const std::vector<AnalysisResult>& results,
    double AnalysisResult::*member)
{
    std::vector<double> values;
    values.reserve(results.size());
    for (const AnalysisResult& result : results)
        values.push_back(result.*member);
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if ((values.size() & 1U) != 0U)
        return values[middle];
    return (values[middle - 1] + values[middle]) / 2.0;
}

double circularDifference(double value, double reference, double period)
{
    double difference = std::fmod(value - reference, period);
    if (difference > period / 2.0)
        difference -= period;
    else if (difference < -period / 2.0)
        difference += period;
    return difference;
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
            const double fit =
                std::exp(-0.5 * std::pow(normalizedError / 0.045, 2.0));
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

class Biquad {
public:
    enum class Type {
        HighPass,
        LowPass
    };

    Biquad(Type type, double cutoffHz, double sampleRate)
    {
        const double safeCutoff =
            std::clamp(cutoffHz, 10.0, sampleRate * 0.45);
        const double k = std::tan(std::numbers::pi * safeCutoff / sampleRate);
        const double normalization =
            1.0 / (1.0 + std::numbers::sqrt2 * k + k * k);

        if (type == Type::HighPass) {
            m_a0 = normalization;
            m_a1 = -2.0 * m_a0;
            m_a2 = m_a0;
        } else {
            m_a0 = k * k * normalization;
            m_a1 = 2.0 * m_a0;
            m_a2 = m_a0;
        }

        m_b1 = 2.0 * (k * k - 1.0) * normalization;
        m_b2 = (1.0 - std::numbers::sqrt2 * k + k * k) * normalization;
    }

    double process(double input)
    {
        const double output = input * m_a0 + m_z1;
        m_z1 = input * m_a1 + m_z2 - m_b1 * output;
        m_z2 = input * m_a2 - m_b2 * output;
        return output;
    }

private:
    double m_a0 = 0.0;
    double m_a1 = 0.0;
    double m_a2 = 0.0;
    double m_b1 = 0.0;
    double m_b2 = 0.0;
    double m_z1 = 0.0;
    double m_z2 = 0.0;
};

void fft(std::vector<std::complex<double>>& values, bool inverse)
{
    const std::size_t size = values.size();
    for (std::size_t i = 1, reversed = 0; i < size; ++i) {
        std::size_t bit = size >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (i < reversed)
            std::swap(values[i], values[reversed]);
    }

    for (std::size_t length = 2; length <= size; length <<= 1) {
        const double angle =
            2.0 * std::numbers::pi / static_cast<double>(length)
            * (inverse ? 1.0 : -1.0);
        const std::complex<double> root(std::cos(angle), std::sin(angle));
        for (std::size_t offset = 0; offset < size; offset += length) {
            std::complex<double> factor(1.0, 0.0);
            for (std::size_t index = 0; index < length / 2; ++index) {
                const std::complex<double> even = values[offset + index];
                const std::complex<double> odd =
                    values[offset + index + length / 2] * factor;
                values[offset + index] = even + odd;
                values[offset + index + length / 2] = even - odd;
                factor *= root;
            }
        }
    }

    if (inverse) {
        for (auto& value : values)
            value /= static_cast<double>(size);
    }
}

std::vector<double> autocorrelation(const std::vector<double>& input)
{
    if (input.empty())
        return {};

    const double mean =
        std::accumulate(input.begin(), input.end(), 0.0) / input.size();
    std::size_t fftSize = 1;
    while (fftSize < input.size() * 2)
        fftSize <<= 1;

    std::vector<std::complex<double>> spectrum(fftSize);
    for (std::size_t i = 0; i < input.size(); ++i)
        spectrum[i] = {input[i] - mean, 0.0};

    fft(spectrum, false);
    for (auto& value : spectrum)
        value = {std::norm(value), 0.0};
    fft(spectrum, true);

    std::vector<double> result(input.size());
    for (std::size_t lag = 0; lag < input.size(); ++lag) {
        result[lag] =
            spectrum[lag].real() / static_cast<double>(input.size() - lag);
    }
    return result;
}

double refinedPeak(const std::vector<double>& values, std::size_t index)
{
    if (index == 0 || index + 1 >= values.size())
        return static_cast<double>(index);

    const double denominator =
        values[index - 1] - 2.0 * values[index] + values[index + 1];
    if (std::abs(denominator) < std::numeric_limits<double>::epsilon())
        return static_cast<double>(index);

    const double offset =
        0.5 * (values[index - 1] - values[index + 1]) / denominator;
    return static_cast<double>(index) + std::clamp(offset, -1.0, 1.0);
}

struct PulseSignal {
    std::vector<double> samples;
    double sampleRate = 0.0;
};

PulseSignal makePulseSignal(std::span<const float> samples, int sampleRate)
{
    const double cutoff =
        std::min(kCorrelationFilterHz, sampleRate * 0.40);
    Biquad highPass(Biquad::Type::HighPass, cutoff, sampleRate);
    Biquad lowPass(Biquad::Type::LowPass, cutoff, sampleRate);

    std::vector<double> shaped(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double high = highPass.process(samples[i]);
        shaped[i] = lowPass.process(std::abs(high));
    }

    const int blockSize = std::max(
        1, static_cast<int>(std::lround(sampleRate / kCorrelationSampleRate)));
    const std::size_t outputSize = shaped.size()
        / static_cast<std::size_t>(blockSize);
    PulseSignal result;
    result.sampleRate =
        static_cast<double>(sampleRate) / static_cast<double>(blockSize);
    result.samples.resize(outputSize);

    for (std::size_t block = 0; block < outputSize; ++block) {
        const auto begin =
            shaped.begin() + static_cast<std::ptrdiff_t>(block * blockSize);
        result.samples[block] =
            std::accumulate(begin, begin + blockSize, 0.0) / blockSize;
    }
    return result;
}

struct PeriodLock {
    bool valid = false;
    double cycleSamples = 0.0;
    double madSamples = 0.0;
    double correlation = 0.0;
    std::size_t estimates = 0;
};

PeriodLock estimateCyclePeriod(
    const std::vector<double>& correlation,
    double analysisSampleRate,
    double nominalBph)
{
    PeriodLock result;
    if (correlation.size() < 100 || correlation.front() <= 0.0
        || nominalBph <= 0.0) {
        return result;
    }

    double estimate = 7200.0 * analysisSampleRate / nominalBph;
    const double searchRadius = analysisSampleRate * 0.020;
    std::vector<double> estimates;

    for (int cycle = 1;; ++cycle) {
        const double predicted = estimate * cycle;
        const auto lower = static_cast<std::size_t>(
            std::max(1.0, std::floor(predicted - searchRadius)));
        const auto upper = static_cast<std::size_t>(
            std::min(
                static_cast<double>(correlation.size() - 2),
                std::ceil(predicted + searchRadius)));
        if (upper >= correlation.size() * 2 / 3 || lower >= upper)
            break;

        const auto peak = static_cast<std::size_t>(
            std::distance(
                correlation.begin(),
                std::max_element(
                    correlation.begin() + static_cast<std::ptrdiff_t>(lower),
                    correlation.begin() + static_cast<std::ptrdiff_t>(upper + 1))));
        if (peak == lower || peak == upper)
            return result;

        const double newEstimate =
            refinedPeak(correlation, peak) / static_cast<double>(cycle);
        if (std::abs(newEstimate - estimate) > searchRadius)
            return result;
        estimate = newEstimate;

        if (lower > correlation.size() / 3)
            estimates.push_back(estimate);
    }

    if (estimates.size() < 3)
        return result;

    const double median = percentile(estimates, 0.50);
    std::vector<double> deviations;
    deviations.reserve(estimates.size());
    for (const double value : estimates)
        deviations.push_back(std::abs(value - median));
    const double mad = percentile(deviations, 0.50);
    const double tolerance =
        std::max(3.0 * 1.4826 * mad, analysisSampleRate * 0.005);

    double sum = 0.0;
    std::size_t accepted = 0;
    for (const double value : estimates) {
        if (std::abs(value - median) <= tolerance) {
            sum += value;
            ++accepted;
        }
    }
    if (accepted < 3)
        return result;

    result.cycleSamples = sum / static_cast<double>(accepted);
    result.madSamples = mad;
    result.estimates = accepted;

    const double localRadius = analysisSampleRate * 0.003;
    const auto lower = static_cast<std::size_t>(
        std::max(1.0, std::floor(result.cycleSamples - localRadius)));
    const auto upper = static_cast<std::size_t>(
        std::min(
            static_cast<double>(correlation.size() - 2),
            std::ceil(result.cycleSamples + localRadius)));
    const double peak = *std::max_element(
        correlation.begin() + static_cast<std::ptrdiff_t>(lower),
        correlation.begin() + static_cast<std::ptrdiff_t>(upper + 1));
    result.correlation = peak / correlation.front();

    const double madSeconds = result.madSamples / analysisSampleRate;
    result.valid = result.correlation >= 0.025 && madSeconds <= 0.00025;
    return result;
}

double interpolate(const std::vector<double>& values, double position)
{
    if (values.empty())
        return 0.0;
    if (position <= 0.0)
        return values.front();
    if (position >= values.size() - 1)
        return values.back();

    const auto lower = static_cast<std::size_t>(std::floor(position));
    const double fraction = position - lower;
    return values[lower] * (1.0 - fraction)
        + values[lower + 1] * fraction;
}

double estimateBeatError(
    const std::vector<double>& pulseSignal,
    double analysisSampleRate,
    double cycleSamples)
{
    const int profileSize = static_cast<int>(std::lround(cycleSamples));
    const int cycleCount = static_cast<int>(
        std::floor((pulseSignal.size() - 2.0) / cycleSamples));
    if (profileSize < 16 || cycleCount < 6)
        return 0.0;

    std::vector<double> profile(profileSize);
    std::vector<double> bin;
    bin.reserve(cycleCount);
    const int discarded =
        std::max(1, static_cast<int>(std::ceil(cycleCount * 0.20)));
    const int kept = cycleCount - discarded;
    if (kept < 3)
        return 0.0;

    for (int index = 0; index < profileSize; ++index) {
        bin.clear();
        const double withinCycle =
            static_cast<double>(index) * cycleSamples / profileSize;
        for (int cycle = 0; cycle < cycleCount; ++cycle) {
            bin.push_back(interpolate(
                pulseSignal, cycle * cycleSamples + withinCycle));
        }
        std::sort(bin.begin(), bin.end());
        profile[index] =
            std::accumulate(bin.begin(), bin.begin() + kept, 0.0) / kept;
    }

    const double baseline = percentile(profile, 0.50);
    for (double& value : profile)
        value -= baseline;

    const double center = profileSize / 2.0;
    const int radius = std::max(
        2, static_cast<int>(std::lround(analysisSampleRate * 0.020)));
    const int lower = std::max(1, static_cast<int>(std::floor(center)) - radius);
    const int upper =
        std::min(profileSize - 2, static_cast<int>(std::ceil(center)) + radius);
    std::vector<double> localCorrelation(
        static_cast<std::size_t>(upper - lower + 1));

    for (int lag = lower; lag <= upper; ++lag) {
        double sum = 0.0;
        // Compare tick to tock in one direction only. Summing the complete
        // folded cycle also adds the inverse tock-to-tick comparison; the two
        // offsets then cancel and can create a false maximum at exactly
        // 0.00 ms even when the escapement is asymmetric.
        for (int index = 0; index < profileSize / 2; ++index) {
            sum += profile[index]
                * profile[(index + lag) % profileSize];
        }
        localCorrelation[static_cast<std::size_t>(lag - lower)] = sum;
    }

    struct BeatErrorPeak {
        double correlation = 0.0;
        double milliseconds = 0.0;
    };
    std::vector<BeatErrorPeak> peaks;
    for (std::size_t index = 1; index + 1 < localCorrelation.size(); ++index) {
        if (localCorrelation[index] < localCorrelation[index - 1]
            || localCorrelation[index] < localCorrelation[index + 1]) {
            continue;
        }
        const double lag =
            lower + refinedPeak(localCorrelation, index);
        peaks.push_back({
            localCorrelation[index],
            std::abs(center - lag) / analysisSampleRate * 1000.0
        });
    }

    if (peaks.empty())
        return 0.0;
    std::sort(
        peaks.begin(), peaks.end(),
        [](const BeatErrorPeak& left, const BeatErrorPeak& right) {
            return left.correlation > right.correlation;
        });

    const double strongest = peaks.front().correlation;
    if (peaks.front().milliseconds >= 0.08)
        return peaks.front().milliseconds;

    for (const BeatErrorPeak& peak : peaks) {
        if (peak.milliseconds >= 0.08
            && peak.correlation >= strongest * 0.995) {
            return peak.milliseconds;
        }
    }
    return 0.0;
}

struct LockedEvent {
    double timeSeconds = 0.0;
    double strength = 0.0;
    long long index = 0;
    double residualSeconds = 0.0;
};

std::vector<LockedEvent> lockEvents(
    const std::vector<double>& eventTimes,
    const std::vector<double>& eventStrengths,
    double beatPeriod)
{
    std::vector<LockedEvent> result;
    if (eventTimes.empty() || beatPeriod <= 0.0)
        return result;

    std::vector<double> phases(eventTimes.size());
    for (std::size_t i = 0; i < eventTimes.size(); ++i) {
        phases[i] = std::fmod(eventTimes[i], beatPeriod);
        if (phases[i] < 0.0)
            phases[i] += beatPeriod;
    }

    double bestPhase = phases.front();
    double bestScore = -1.0;
    for (const double candidate : phases) {
        double score = 0.0;
        for (std::size_t i = 0; i < phases.size(); ++i) {
            const double distance =
                std::abs(circularDifference(phases[i], candidate, beatPeriod));
            const double strength =
                std::min(eventStrengths[i], 3.0);
            score += strength
                * std::exp(-0.5 * std::pow(distance / 0.004, 2.0));
        }
        if (score > bestScore) {
            bestScore = score;
            bestPhase = candidate;
        }
    }

    double weightedOffset = 0.0;
    double weightSum = 0.0;
    for (std::size_t i = 0; i < phases.size(); ++i) {
        const double offset =
            circularDifference(phases[i], bestPhase, beatPeriod);
        if (std::abs(offset) <= 0.006) {
            const double weight = std::min(eventStrengths[i], 3.0);
            weightedOffset += offset * weight;
            weightSum += weight;
        }
    }
    if (weightSum > 0.0)
        bestPhase = std::fmod(bestPhase + weightedOffset / weightSum + beatPeriod,
                             beatPeriod);

    const double tolerance = std::min(0.004, beatPeriod * 0.08);
    std::map<long long, LockedEvent> byIndex;
    for (std::size_t i = 0; i < eventTimes.size(); ++i) {
        const long long index = std::llround(
            (eventTimes[i] - bestPhase) / beatPeriod);
        const double expected = bestPhase + index * beatPeriod;
        const double residual = eventTimes[i] - expected;
        if (std::abs(residual) > tolerance)
            continue;

        const LockedEvent candidate {
            eventTimes[i], eventStrengths[i], index, residual
        };
        const auto existing = byIndex.find(index);
        if (existing == byIndex.end()
            || std::abs(residual) < std::abs(existing->second.residualSeconds)) {
            byIndex[index] = candidate;
        }
    }

    result.reserve(byIndex.size());
    for (const auto& [index, event] : byIndex) {
        static_cast<void>(index);
        result.push_back(event);
    }
    return result;
}

double bandLimitedSignalToNoiseDb(const std::vector<double>& pulseSignal)
{
    if (pulseSignal.empty())
        return 0.0;

    std::vector<double> sample;
    const std::size_t stride =
        std::max<std::size_t>(1, pulseSignal.size() / 12000);
    sample.reserve(pulseSignal.size() / stride + 1);
    for (std::size_t i = 0; i < pulseSignal.size(); i += stride)
        sample.push_back(pulseSignal[i]);

    const double noiseFloor = percentile(sample, 0.50);
    const double pulseLevel = percentile(std::move(sample), 0.98);
    return 20.0 * std::log10(
        std::max(pulseLevel, 1.0e-9)
        / std::max(noiseFloor, 1.0e-9));
}

struct AmplitudeEstimate {
    bool valid = false;
    double degrees = 0.0;
};

AmplitudeEstimate estimateAmplitude(
    const std::vector<double>& envelope,
    int sampleRate,
    const std::vector<LockedEvent>& lockedEvents,
    double nominalBph,
    double liftAngleDegrees)
{
    AmplitudeEstimate result;
    if (envelope.empty() || sampleRate <= 0 || nominalBph <= 0.0
        || liftAngleDegrees < 20.0 || liftAngleDegrees > 80.0) {
        return result;
    }

    constexpr double kMinimumPlausibleAmplitude = 120.0;
    constexpr double kMaximumPlausibleAmplitude = 380.0;
    const double formulaNumerator = 3600.0 * liftAngleDegrees
        / (std::numbers::pi * nominalBph);
    const double minimumLiftTime =
        formulaNumerator / kMaximumPlausibleAmplitude;
    const double maximumLiftTime =
        formulaNumerator / kMinimumPlausibleAmplitude;
    const double beatPeriod = 3600.0 / nominalBph;
    const double preSeconds =
        std::min(beatPeriod * 0.40, maximumLiftTime + 0.006);
    const double postSeconds = preSeconds;
    const int preSamples =
        static_cast<int>(std::ceil(preSeconds * sampleRate));
    const int postSamples =
        static_cast<int>(std::ceil(postSeconds * sampleRate));
    const int profileSize = preSamples + postSamples + 1;

    struct Window {
        std::size_t center = 0;
        double baseline = 0.0;
        double scale = 0.0;
    };
    std::array<std::vector<Window>, 2> windows;
    for (const LockedEvent& event : lockedEvents) {
        const auto center = static_cast<long long>(
            std::llround(event.timeSeconds * sampleRate));
        if (center < preSamples
            || center + postSamples >= static_cast<long long>(envelope.size())) {
            continue;
        }

        std::vector<double> local;
        local.reserve(static_cast<std::size_t>(profileSize));
        for (int offset = -preSamples; offset <= postSamples; ++offset) {
            local.push_back(
                envelope[static_cast<std::size_t>(center + offset)]);
        }
        const double baseline = percentile(local, 0.10);
        const double high = percentile(std::move(local), 0.98);
        const double scale = high - baseline;
        if (scale <= std::max(1.0e-8, baseline * 0.30))
            continue;

        windows[static_cast<std::size_t>(event.index & 1LL)].push_back({
            static_cast<std::size_t>(center), baseline, scale
        });
    }

    std::array<double, 2> parityAmplitude {};
    std::array<bool, 2> parityValid {};
    for (std::size_t parity = 0; parity < windows.size(); ++parity) {
        if (windows[parity].size() < 6)
            continue;

        std::vector<double> profile(
            static_cast<std::size_t>(profileSize));
        std::vector<double> bin;
        bin.reserve(windows[parity].size());
        for (int index = 0; index < profileSize; ++index) {
            bin.clear();
            const int relative = index - preSamples;
            for (const Window& window : windows[parity]) {
                const double value =
                    envelope[static_cast<std::size_t>(
                        static_cast<long long>(window.center) + relative)];
                bin.push_back(
                    std::max(0.0, value - window.baseline)
                    / window.scale);
            }
            profile[static_cast<std::size_t>(index)] =
                percentile(bin, 0.50);
        }

        const int smoothingRadius =
            std::max(1, static_cast<int>(std::lround(sampleRate * 0.00020)));
        std::vector<double> smooth(profile.size());
        std::vector<double> prefix(profile.size() + 1);
        for (int index = 0; index < profileSize; ++index) {
            prefix[static_cast<std::size_t>(index + 1)] =
                prefix[static_cast<std::size_t>(index)]
                + profile[static_cast<std::size_t>(index)];
        }
        for (int index = 0; index < profileSize; ++index) {
            const int lower = std::max(0, index - smoothingRadius);
            const int upper =
                std::min(profileSize - 1, index + smoothingRadius);
            const double sum =
                prefix[static_cast<std::size_t>(upper + 1)]
                - prefix[static_cast<std::size_t>(lower)];
            const int count = upper - lower + 1;
            smooth[static_cast<std::size_t>(index)] =
                count > 0 ? sum / count : 0.0;
        }

        const double profileMaximum =
            *std::max_element(smooth.begin(), smooth.end());
        if (profileMaximum <= 0.0)
            continue;

        std::vector<int> peakCandidates;
        for (int index = 1; index + 1 < profileSize; ++index) {
            if (smooth[static_cast<std::size_t>(index)]
                    >= profileMaximum * 0.08
                && smooth[static_cast<std::size_t>(index)]
                    >= smooth[static_cast<std::size_t>(index - 1)]
                && smooth[static_cast<std::size_t>(index)]
                    > smooth[static_cast<std::size_t>(index + 1)]) {
                peakCandidates.push_back(index);
            }
        }

        std::sort(
            peakCandidates.begin(), peakCandidates.end(),
            [&smooth](int left, int right) {
                return smooth[static_cast<std::size_t>(left)]
                    > smooth[static_cast<std::size_t>(right)];
            });
        const int minimumPeakDistance =
            std::max(2, static_cast<int>(std::lround(sampleRate * 0.0012)));
        std::vector<int> peaks;
        for (const int candidate : peakCandidates) {
            const bool separated = std::all_of(
                peaks.begin(), peaks.end(),
                [candidate, minimumPeakDistance](int accepted) {
                    return std::abs(candidate - accepted)
                        >= minimumPeakDistance;
                });
            if (separated)
                peaks.push_back(candidate);
        }
        if (peaks.size() < 3)
            continue;

        std::sort(peaks.begin(), peaks.end());
        int firstPeak = -1;
        int thirdPeak = -1;
        double bestScore = -1.0;
        for (std::size_t first = 0; first + 2 < peaks.size(); ++first) {
            for (std::size_t third = first + 2; third < peaks.size(); ++third) {
                const double duration =
                    static_cast<double>(peaks[third] - peaks[first])
                    / sampleRate;
                if (duration < minimumLiftTime || duration > maximumLiftTime)
                    continue;

                double intermediateStrength = 0.0;
                for (std::size_t middle = first + 1; middle < third; ++middle) {
                    intermediateStrength = std::max(
                        intermediateStrength,
                        smooth[static_cast<std::size_t>(peaks[middle])]);
                }

                const double score =
                    smooth[static_cast<std::size_t>(peaks[first])]
                    + smooth[static_cast<std::size_t>(peaks[third])]
                    + 0.60 * intermediateStrength;
                if (score > bestScore) {
                    bestScore = score;
                    firstPeak = peaks[first];
                    thirdPeak = peaks[third];
                }
            }
        }
        if (firstPeak < 0 || thirdPeak < 0)
            continue;

        const double liftTime =
            static_cast<double>(thirdPeak - firstPeak) / sampleRate;
        const double amplitude = formulaNumerator / liftTime;
        if (amplitude < kMinimumPlausibleAmplitude
            || amplitude > kMaximumPlausibleAmplitude) {
            continue;
        }
        parityAmplitude[parity] = amplitude;
        parityValid[parity] = true;
    }

    if (!parityValid[0] || !parityValid[1]
        || std::abs(parityAmplitude[0] - parityAmplitude[1]) > 60.0) {
        return result;
    }

    result.degrees = (parityAmplitude[0] + parityAmplitude[1]) / 2.0;
    result.valid = true;
    return result;
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
    if (sampleRate < 4000
        || samples.size() < static_cast<std::size_t>(sampleRate * 2)) {
        result.status = "Servono almeno due secondi di audio valido";
        return result;
    }

    // A light high-pass and a short RMS envelope are used only for transient
    // candidates and diagnostics. Precision comes from the independent
    // periodic correlation lock below, not from whichever resonance happens
    // to be the loudest inside an acoustic tick.
    const double cutoffHz = 90.0;
    const double dt = 1.0 / static_cast<double>(sampleRate);
    const double rc = 1.0 / (2.0 * std::numbers::pi * cutoffHz);
    const double alpha = rc / (rc + dt);
    std::vector<double> filtered(samples.size());
    double previousInput = samples.front();
    double previousOutput = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double value =
            alpha * (previousOutput + samples[i] - previousInput);
        filtered[i] = value;
        previousInput = samples[i];
        previousOutput = value;
    }

    const int window =
        std::max(3, static_cast<int>(std::lround(sampleRate * 0.0009)));
    std::vector<double> envelope(samples.size());
    double energy = 0.0;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        energy += filtered[i] * filtered[i];
        if (i >= static_cast<std::size_t>(window))
            energy -= filtered[i - window] * filtered[i - window];
        envelope[i] = std::sqrt(std::max(0.0, energy) / window);
    }

    std::vector<double> noiseSample;
    const std::size_t stride =
        std::max<std::size_t>(1, samples.size() / 12000);
    noiseSample.reserve(samples.size() / stride + 1);
    for (std::size_t i = 0; i < envelope.size(); i += stride)
        noiseSample.push_back(envelope[i]);

    const double median = percentile(noiseSample, 0.50);
    std::vector<double> deviations;
    deviations.reserve(noiseSample.size());
    for (const double value : noiseSample)
        deviations.push_back(std::abs(value - median));
    const double mad = percentile(std::move(deviations), 0.50);
    const double threshold = median
        + std::max(
            config.detectionSensitivity * 1.4826 * mad,
            median * 1.8 + 1.0e-7);

    const int refractorySamples =
        std::max(1, static_cast<int>(std::lround(sampleRate * 0.040)));
    std::vector<double> eventTimes;
    std::vector<double> eventStrengths;

    for (std::size_t i = 1; i + 1 < envelope.size();) {
        if (envelope[i] < threshold) {
            ++i;
            continue;
        }

        const std::size_t regionEnd = std::min(
            envelope.size() - 1,
            i + static_cast<std::size_t>(refractorySamples));
        std::size_t peakIndex = i;
        for (std::size_t cursor = i + 1; cursor <= regionEnd; ++cursor) {
            if (envelope[cursor] > envelope[peakIndex])
                peakIndex = cursor;
        }

        // A constant-fraction rising edge is substantially less sensitive to
        // changing resonance amplitudes than the local maximum itself.
        const double target =
            threshold + 0.25 * (envelope[peakIndex] - threshold);
        std::size_t crossing = i;
        while (crossing < peakIndex && envelope[crossing] < target)
            ++crossing;
        double crossingSample = static_cast<double>(crossing);
        if (crossing > i && envelope[crossing] > envelope[crossing - 1]) {
            crossingSample = crossing - 1
                + (target - envelope[crossing - 1])
                    / (envelope[crossing] - envelope[crossing - 1]);
        }

        eventTimes.push_back(crossingSample / sampleRate);
        eventStrengths.push_back(
            envelope[peakIndex] / std::max(threshold, 1.0e-9));
        i = regionEnd + 1;
    }

    if (eventTimes.size() < 8) {
        result.status =
            "Segnale insufficiente: avvicinare o riposizionare il sensore";
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
        result.status =
            "Battito non identificato con sufficiente affidabilità";
        return result;
    }

    const PulseSignal pulse = makePulseSignal(samples, sampleRate);
    result.signalToNoiseDb =
        bandLimitedSignalToNoiseDb(pulse.samples);
    const std::vector<double> pulseCorrelation =
        autocorrelation(pulse.samples);
    const PeriodLock lock =
        estimateCyclePeriod(pulseCorrelation, pulse.sampleRate, best.bph);
    if (!lock.valid) {
        result.status =
            "Battito non identificato con sufficiente affidabilità";
        return result;
    }

    const double cycleSeconds = lock.cycleSamples / pulse.sampleRate;
    const double secondsPerBeat = cycleSeconds / 2.0;
    result.nominalBph = best.bph;
    result.measuredBph = 3600.0 / secondsPerBeat;
    result.rateSecondsPerDay =
        (result.measuredBph / result.nominalBph - 1.0) * kSecondsPerDay;
    const double profileBeatError = estimateBeatError(
        pulse.samples, pulse.sampleRate, lock.cycleSamples);

    const std::vector<LockedEvent> lockedEvents =
        lockEvents(eventTimes, eventStrengths, secondsPerBeat);
    if (lockedEvents.size() < 8) {
        result.status =
            "Troppi impulsi scartati: controllare il contatto del sensore";
        return result;
    }

    std::vector<double> evenResiduals;
    std::vector<double> oddResiduals;
    for (const LockedEvent& event : lockedEvents) {
        ((event.index & 1LL) == 0 ? evenResiduals : oddResiduals)
            .push_back(event.residualSeconds);
    }
    const double evenMedian = percentile(evenResiduals, 0.50);
    const double oddMedian = percentile(oddResiduals, 0.50);
    const double eventBeatError =
        std::abs(evenMedian - oddMedian) * 1000.0;

    // The directional folded-profile correlation is insensitive to which
    // internal resonance happens to be loudest. Event parity is only a
    // conservative fallback for small offsets: on difficult microphones,
    // changing resonances can move a transient edge by several milliseconds.
    result.beatErrorMilliseconds = profileBeatError;
    if (result.beatErrorMilliseconds < 0.08
        && evenResiduals.size() >= 4 && oddResiduals.size() >= 4
        && eventBeatError >= 0.08 && eventBeatError <= 2.5) {
        result.beatErrorMilliseconds = eventBeatError;
    }

    std::vector<double> correctedResiduals;
    correctedResiduals.reserve(lockedEvents.size());
    result.events.reserve(lockedEvents.size());
    result.stripResidualMilliseconds.reserve(lockedEvents.size());
    for (const LockedEvent& event : lockedEvents) {
        const double parityCenter =
            (event.index & 1LL) == 0 ? evenMedian : oddMedian;
        correctedResiduals.push_back(event.residualSeconds - parityCenter);
        result.events.push_back({
            event.timeSeconds,
            event.strength,
            event.index
        });
        result.stripResidualMilliseconds.push_back(
            event.residualSeconds * 1000.0);
    }

    if (!correctedResiduals.empty()) {
        const double squared = std::inner_product(
            correctedResiduals.begin(),
            correctedResiduals.end(),
            correctedResiduals.begin(),
            0.0);
        result.intervalJitterMilliseconds =
            std::sqrt(squared / correctedResiduals.size()) * 1000.0;
    }

    const AmplitudeEstimate amplitude = estimateAmplitude(
        envelope,
        sampleRate,
        lockedEvents,
        result.nominalBph,
        config.liftAngleDegrees);
    result.amplitudeAvailable = amplitude.valid;
    result.amplitudeDegrees = amplitude.degrees;

    const double eventFactor =
        clamp01((lockedEvents.size() - 7.0) / 45.0);
    const double snrFactor =
        clamp01((result.signalToNoiseDb - 4.0) / 16.0);
    const double lockFactor =
        clamp01((lock.correlation - 0.025) / 0.30);
    const double periodMadMicroseconds =
        lock.madSamples / pulse.sampleRate * 1.0e6;
    const double stabilityFactor =
        std::exp(-std::pow(periodMadMicroseconds / 80.0, 2.0));
    const double jitterFactor = std::exp(
        -std::pow(result.intervalJitterMilliseconds / 1.8, 2.0));
    result.confidence = 100.0 * clamp01(
        0.25 * best.score
        + 0.30 * lockFactor
        + 0.20 * stabilityFactor
        + 0.15 * eventFactor
        + 0.05 * snrFactor
        + 0.05 * jitterFactor);

    result.valid = true;
    result.state = MeasurementState::Locked;
    result.status = result.confidence >= 65.0
        ? "Misurazione stabile"
        : "Misurazione acquisita, qualità da migliorare";
    return result;
}

AnalysisResult MeasurementStabilizer::process(
    const AnalysisResult& candidate)
{
    const double nowSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return processAt(candidate, nowSeconds);
}

AnalysisResult MeasurementStabilizer::processAt(
    const AnalysisResult& candidate,
    double timestampSeconds)
{
    constexpr double kHistorySeconds = 4.5;
    constexpr double kDegradedGraceSeconds = 3.0;
    constexpr double kPendingConfirmationSeconds = 0.30;

    if (!candidate.valid) {
        m_pendingCount = 0;
        if (m_hasLastOutput && m_hasLastValidTimestamp) {
            const double elapsed = std::max(
                0.0, timestampSeconds - m_lastValidTimestampSeconds);
            if (elapsed < kDegradedGraceSeconds) {
                AnalysisResult held = m_lastOutput;
                held.valid = true;
                held.state = MeasurementState::Degraded;
                held.status = "Segnale temporaneamente instabile";
                held.confidence = std::clamp(
                    m_smoothedConfidence
                        * (1.0 - elapsed / kDegradedGraceSeconds),
                    0.0,
                    100.0);
                m_lastProcessTimestampSeconds = timestampSeconds;
                m_hasLastProcessTimestamp = true;
                return held;
            }
        }

        AnalysisResult lost = candidate;
        lost.state = m_hasLastOutput
            ? MeasurementState::Lost
            : MeasurementState::Searching;
        m_history.clear();
        m_historyTimestamps.clear();
        m_hasLastValidTimestamp = false;
        m_lastProcessTimestampSeconds = timestampSeconds;
        m_hasLastProcessTimestamp = true;
        return lost;
    }

    m_lastValidTimestampSeconds = timestampSeconds;
    m_hasLastValidTimestamp = true;

    if (m_history.empty()) {
        AnalysisResult locked = candidate;
        locked.state = MeasurementState::Locked;
        m_history.push_back(locked);
        m_historyTimestamps.push_back(timestampSeconds);
        m_smoothedConfidence = candidate.confidence;
        m_lastOutput = locked;
        m_hasLastOutput = true;
        m_lastProcessTimestampSeconds = timestampSeconds;
        m_hasLastProcessTimestamp = true;
        return m_lastOutput;
    }

    const double referenceBph = m_history.back().nominalBph;
    const double referenceRate =
        medianOfResults(m_history, &AnalysisResult::rateSecondsPerDay);
    constexpr double kMaximumInstantRateJump = 15.0;
    constexpr double kPendingClusterTolerance = 10.0;
    constexpr int kRequiredConfirmations = 3;

    const bool harmonicChanged =
        std::abs(candidate.nominalBph - referenceBph) > 1.0;
    const bool rateJumped =
        std::abs(candidate.rateSecondsPerDay - referenceRate)
        > kMaximumInstantRateJump;

    if (harmonicChanged || rateJumped) {
        const bool continuesPendingCluster =
            m_pendingCount > 0
            && std::abs(
                   candidate.nominalBph - m_pendingCandidate.nominalBph)
                <= 1.0
            && std::abs(
                   candidate.rateSecondsPerDay
                   - m_pendingCandidate.rateSecondsPerDay)
                <= kPendingClusterTolerance;

        if (continuesPendingCluster)
            ++m_pendingCount;
        else {
            m_pendingCount = 1;
            m_pendingSinceSeconds = timestampSeconds;
        }
        m_pendingCandidate = candidate;

        if (m_pendingCount < kRequiredConfirmations
            || timestampSeconds - m_pendingSinceSeconds
                < kPendingConfirmationSeconds) {
            m_lastProcessTimestampSeconds = timestampSeconds;
            m_hasLastProcessTimestamp = true;
            return m_hasLastOutput ? m_lastOutput : m_history.back();
        }

        m_history.clear();
        m_historyTimestamps.clear();
        m_pendingCount = 0;
    } else {
        m_pendingCount = 0;
    }

    AnalysisResult lockedCandidate = candidate;
    lockedCandidate.state = MeasurementState::Locked;
    m_history.push_back(lockedCandidate);
    m_historyTimestamps.push_back(timestampSeconds);
    while (m_history.size() > 1
           && m_historyTimestamps.front()
               < timestampSeconds - kHistorySeconds) {
        m_history.erase(m_history.begin());
        m_historyTimestamps.erase(m_historyTimestamps.begin());
    }
    if (m_history.size() > 128) {
        m_history.erase(m_history.begin());
        m_historyTimestamps.erase(m_historyTimestamps.begin());
    }

    AnalysisResult stabilized = lockedCandidate;
    stabilized.rateSecondsPerDay =
        medianOfResults(m_history, &AnalysisResult::rateSecondsPerDay);
    stabilized.beatErrorMilliseconds =
        medianOfResults(m_history, &AnalysisResult::beatErrorMilliseconds);
    stabilized.signalToNoiseDb =
        medianOfResults(m_history, &AnalysisResult::signalToNoiseDb);
    stabilized.intervalJitterMilliseconds =
        medianOfResults(m_history, &AnalysisResult::intervalJitterMilliseconds);
    const double medianConfidence =
        medianOfResults(m_history, &AnalysisResult::confidence);
    const double deltaSeconds = m_hasLastProcessTimestamp
        ? std::clamp(
              timestampSeconds - m_lastProcessTimestampSeconds,
              0.0,
              1.0)
        : 0.0;
    constexpr double kConfidenceTimeConstantSeconds = 3.5;
    const double confidenceSmoothing =
        1.0 - std::exp(-deltaSeconds / kConfidenceTimeConstantSeconds);
    m_smoothedConfidence += confidenceSmoothing
        * (medianConfidence - m_smoothedConfidence);
    stabilized.confidence = m_smoothedConfidence;

    std::vector<AnalysisResult> amplitudeResults;
    for (const AnalysisResult& result : m_history) {
        if (result.amplitudeAvailable)
            amplitudeResults.push_back(result);
    }
    const std::size_t requiredAmplitudeResults = std::min(
        m_history.size(),
        std::max<std::size_t>(
            3,
            (m_history.size() * 3 + 4) / 5));
    if (amplitudeResults.size() >= requiredAmplitudeResults) {
        stabilized.amplitudeDegrees =
            medianOfResults(amplitudeResults, &AnalysisResult::amplitudeDegrees);
        stabilized.amplitudeAvailable = true;
    } else {
        stabilized.amplitudeDegrees = 0.0;
        stabilized.amplitudeAvailable = false;
    }
    if (stabilized.nominalBph > 0.0) {
        stabilized.measuredBph = stabilized.nominalBph
            * (1.0 + stabilized.rateSecondsPerDay / kSecondsPerDay);
    }
    stabilized.state = MeasurementState::Locked;
    m_lastOutput = stabilized;
    m_hasLastOutput = true;
    m_lastProcessTimestampSeconds = timestampSeconds;
    m_hasLastProcessTimestamp = true;
    return m_lastOutput;
}

void MeasurementStabilizer::reset()
{
    m_history.clear();
    m_historyTimestamps.clear();
    m_pendingCandidate = {};
    m_lastOutput = {};
    m_pendingCount = 0;
    m_pendingSinceSeconds = 0.0;
    m_lastValidTimestampSeconds = 0.0;
    m_lastProcessTimestampSeconds = 0.0;
    m_smoothedConfidence = 0.0;
    m_hasLastOutput = false;
    m_hasLastValidTimestamp = false;
    m_hasLastProcessTimestamp = false;
}

} // namespace chronolab
