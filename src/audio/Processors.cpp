#include "Processors.h"

#include <array>
#include <cmath>
#include <cstdlib>

#if SIGNALPATCH_HAS_NAM
 #include "NAM/get_dsp.h"
#endif

namespace signalpatch
{

// A follower's view of a Clock input: rising edges, and whether pulses have
// arrived recently enough to own the stepping (four seconds of silence hands
// control back to the node's own tempo). Audio-thread only.
struct ClockFollower
{
    int port = -1;             // input port index, -1 when the node has none
    bool wasHigh = false;
    int samplesSincePulse = 1 << 30;

    void reset() noexcept { wasHigh = false; samplesSincePulse = 1 << 30; lastInterval = 0; }

    /** Call once per sample; returns true on a rising edge. */
    int lastInterval = 0;      // samples between the last two pulses

    bool tick (const float* clock, int sample, double sampleRate) noexcept
    {
        const bool high = clock != nullptr && clock[sample] > 0.5f;
        const bool edge = high && ! wasHigh;
        wasHigh = high;
        if (edge)
        {
            if (samplesSincePulse < (1 << 30))
                lastInterval = samplesSincePulse;
            samplesSincePulse = 0;
        }
        else if (samplesSincePulse < (1 << 30))
            ++samplesSincePulse;
        juce::ignoreUnused (sampleRate);
        return edge;
    }
    /** Pulses count as "the clock" for four seconds, or 2.5 intervals when they are slower (a bar at 40 bpm is 6 s). */
    [[nodiscard]] bool external (double sampleRate) const noexcept
    {
        const auto window = juce::jmax (static_cast<double> (lastInterval) * 2.5, sampleRate * 4.0);
        return samplesSincePulse < static_cast<int> (juce::jmin (window, 1.0e9));
    }
    [[nodiscard]] const float* pointer (const juce::AudioBuffer<float>& inputs) const noexcept
    {
        return port >= 0 && inputs.getNumChannels() > port ? inputs.getReadPointer (port) : nullptr;
    }
};

namespace
{
juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre, float interval = 0.0f)
{
    juce::NormalisableRange<float> range (minimum, maximum, interval);
    range.setSkewForCentre (centre);
    return range;
}

class HardwareInputNode final : public DspNode
{
public:
    HardwareInputNode (const juce::StringArray& channelNames, const std::vector<int>& callbackChannels)
        : DspNode (NodeKind::hardwareInput, nodeKindName (NodeKind::hardwareInput))
    {
        for (int channel = 0; channel < channelNames.size(); ++channel)
        {
            auto& port = addOutputPort (channelNames[channel], SignalType::audio);
            port.hardwareChannelIndex = channel;
            port.callbackChannelIndex = juce::isPositiveAndBelow (channel, static_cast<int> (callbackChannels.size()))
                ? callbackChannels[static_cast<std::size_t> (channel)] : channel;
            port.active = port.callbackChannelIndex >= 0;
        }
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}
    void processDsp (const juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, int) noexcept override {}
};

class HardwareOutputNode final : public DspNode
{
public:
    HardwareOutputNode (const juce::StringArray& channelNames, const std::vector<int>& callbackChannels)
        : DspNode (NodeKind::hardwareOutput, nodeKindName (NodeKind::hardwareOutput))
    {
        for (int channel = 0; channel < channelNames.size(); ++channel)
        {
            auto& port = addInputPort (channelNames[channel], SignalType::audio);
            port.hardwareChannelIndex = channel;
            port.callbackChannelIndex = juce::isPositiveAndBelow (channel, static_cast<int> (callbackChannels.size()))
                ? callbackChannels[static_cast<std::size_t> (channel)] : channel;
            port.active = port.callbackChannelIndex >= 0;
        }
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}
    void processDsp (const juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, int) noexcept override {}
};

class GainNode final : public DspNode
{
public:
    GainNode() : DspNode (NodeKind::gain, nodeKindName (NodeKind::gain))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("gain-db", "Gain", "dB", juce::NormalisableRange<float> (-60.0f, 24.0f, 0.01f), 0.0f, 0.25f);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
            output[sample] = input[sample] * juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
    }
};

class MixerNode final : public DspNode
{
public:
    MixerNode() : DspNode (NodeKind::mixer, nodeKindName (NodeKind::mixer))
    {
        for (int channel = 0; channel < 4; ++channel)
            addInputPort ("Input " + juce::String (channel + 1), SignalType::audio);
        addOutputPort ("Mix", SignalType::audio);
        for (int channel = 0; channel < 4; ++channel)
            addParameter ("level-" + juce::String (channel + 1), "Level " + juce::String (channel + 1), "dB",
                          juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f), 0.0f, 0.25f);
        addParameter ("master", "Master", "dB", juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f), 0.0f, 0.25f);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float mixed = 0.0f;
            for (int channel = 0; channel < 4; ++channel)
                mixed += inputs.getSample (channel, sample)
                       * juce::Decibels::decibelsToGain (parameterValue (channel, inputs, sample));
            output[sample] = mixed * juce::Decibels::decibelsToGain (parameterValue (4, inputs, sample));
        }
    }
};

class DistortionNode final : public DspNode
{
public:
    DistortionNode() : DspNode (NodeKind::distortion, nodeKindName (NodeKind::distortion))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("drive", "Drive", "dB", juce::NormalisableRange<float> (0.0f, 42.0f, 0.01f), 18.0f, 0.35f);
        addParameter ("tone", "Tone", "Hz", skewedRange (300.0f, 20000.0f, 3000.0f), 6500.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        toneState = 0.0f;
    }

    void resetDsp() noexcept override { toneState = 0.0f; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto drive = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto cutoff = parameterValue (1, inputs, sample);
            const auto mix = parameterValue (2, inputs, sample) * 0.01f;
            const auto shaped = std::tanh (input[sample] * drive);
            const auto coefficient = std::exp (-juce::MathConstants<double>::twoPi * cutoff / sampleRate);
            toneState = static_cast<float> ((1.0 - coefficient) * shaped + coefficient * toneState);
            output[sample] = input[sample] + mix * (toneState - input[sample]);
        }
    }

    double sampleRate = 48000.0;
    float toneState = 0.0f;
};

class DelayNode final : public DspNode
{
public:
    DelayNode() : DspNode (NodeKind::delay, nodeKindName (NodeKind::delay))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("time-ms", "Time", "ms", skewedRange (1.0f, 2000.0f, 180.0f), 280.0f, 0.4f);
        addParameter ("feedback", "Feedback", "%", juce::NormalisableRange<float> (0.0f, 98.0f, 0.1f), 35.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        delayLine.assign (static_cast<std::size_t> (std::ceil (sampleRate * 2.1))
                                                    + static_cast<std::size_t> (maximumBlockSize) + 4u, 0.0f);
        writePosition = 0;
    }

    void resetDsp() noexcept override
    {
        std::fill (delayLine.begin(), delayLine.end(), 0.0f);
        writePosition = 0;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto size = static_cast<int> (delayLine.size());
        if (size < 4)
            return;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto delaySamples = juce::jlimit (1.0f, static_cast<float> (size - 3),
                                                    parameterValue (0, inputs, sample)
                                                        * static_cast<float> (sampleRate * 0.001));
            float readPosition = static_cast<float> (writePosition) - delaySamples;
            while (readPosition < 0.0f)
                readPosition += static_cast<float> (size);
            const auto indexA = static_cast<int> (readPosition) % size;
            const auto indexB = (indexA + 1) % size;
            const auto fraction = readPosition - std::floor (readPosition);
            const auto delayed = delayLine[static_cast<std::size_t> (indexA)]
                               + fraction * (delayLine[static_cast<std::size_t> (indexB)]
                                             - delayLine[static_cast<std::size_t> (indexA)]);
            const auto feedback = parameterValue (1, inputs, sample) * 0.01f;
            const auto mix = parameterValue (2, inputs, sample) * 0.01f;
            delayLine[static_cast<std::size_t> (writePosition)] = std::tanh (input[sample] + delayed * feedback);
            writePosition = (writePosition + 1) % size;
            output[sample] = input[sample] + mix * (delayed - input[sample]);
        }
    }

    double sampleRate = 48000.0;
    std::vector<float> delayLine;
    int writePosition = 0;
};

class CompressorNode final : public DspNode
{
public:
    CompressorNode() : DspNode (NodeKind::compressor, nodeKindName (NodeKind::compressor))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("threshold", "Threshold", "dB", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, 0.35f);
        addParameter ("ratio", "Ratio", ":1", skewedRange (1.0f, 20.0f, 4.0f, 0.01f), 4.0f, 0.25f);
        addParameter ("attack", "Attack", "ms", skewedRange (0.1f, 100.0f, 10.0f), 10.0f, 0.25f);
        addParameter ("release", "Release", "ms", skewedRange (5.0f, 1000.0f, 120.0f), 120.0f, 0.25f);
        addParameter ("makeup", "Makeup", "dB", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, 0.25f);
    }

    float gainReductionDb() const noexcept override
    {
        return reduction.load (std::memory_order_relaxed);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        envelope = 0.0f;
        reduction.store (0.0f, std::memory_order_relaxed);
    }

    void resetDsp() noexcept override
    {
        envelope = 0.0f;
        reduction.store (0.0f, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        float lastReduction = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto threshold = parameterValue (0, inputs, sample);
            const auto ratio = parameterValue (1, inputs, sample);
            const auto attackMs = parameterValue (2, inputs, sample);
            const auto releaseMs = parameterValue (3, inputs, sample);
            const auto makeup = parameterValue (4, inputs, sample);
            const auto magnitude = std::abs (input[sample]);
            const auto coefficient = std::exp (-1.0 / (sampleRate * 0.001
                                                       * (magnitude > envelope ? attackMs : releaseMs)));
            envelope = static_cast<float> (coefficient * envelope + (1.0 - coefficient) * magnitude);
            const auto inputDb = juce::Decibels::gainToDecibels (juce::jmax (envelope, 1.0e-9f), -120.0f);
            const auto above = juce::jmax (0.0f, inputDb - threshold);
            lastReduction = above - above / ratio;
            output[sample] = input[sample] * juce::Decibels::decibelsToGain (makeup - lastReduction);
        }
        reduction.store (lastReduction, std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    float envelope = 0.0f;
    std::atomic<float> reduction { 0.0f };
};

class LimiterNode final : public DspNode
{
public:
    LimiterNode() : DspNode (NodeKind::limiter, nodeKindName (NodeKind::limiter))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("ceiling", "Ceiling", "dB", juce::NormalisableRange<float> (-24.0f, -0.1f, 0.1f), -1.0f, 0.25f);
        addParameter ("release", "Release", "ms", skewedRange (5.0f, 500.0f, 80.0f), 80.0f, 0.25f);
    }

    float gainReductionDb() const noexcept override
    {
        return reduction.load (std::memory_order_relaxed);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        currentGain = 1.0f;
    }

    void resetDsp() noexcept override { currentGain = 1.0f; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        float lastReduction = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto ceiling = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto releaseMs = parameterValue (1, inputs, sample);
            const auto magnitude = std::abs (input[sample]);
            const auto targetGain = magnitude > ceiling ? ceiling / juce::jmax (magnitude, 1.0e-9f) : 1.0f;
            if (targetGain < currentGain)
                currentGain = targetGain;
            else
            {
                const auto releaseCoefficient = std::exp (-1.0 / (sampleRate * releaseMs * 0.001));
                currentGain = static_cast<float> (releaseCoefficient * currentGain + (1.0 - releaseCoefficient));
            }
            const auto limited = input[sample] * currentGain;
            output[sample] = juce::jlimit (-ceiling, ceiling, limited);
            lastReduction = -juce::Decibels::gainToDecibels (juce::jmax (currentGain, 1.0e-6f));
        }
        reduction.store (lastReduction, std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    float currentGain = 1.0f;
    std::atomic<float> reduction { 0.0f };
};

class FeedbackGuardNode final : public DspNode
{
public:
    FeedbackGuardNode() : DspNode (NodeKind::feedbackGuard, nodeKindName (NodeKind::feedbackGuard))
    {
        addInputPort ("Loop return", SignalType::audio);
        addOutputPort ("Delayed send", SignalType::audio);
        addParameter ("amount", "Amount", "%", juce::NormalisableRange<float> (0.0f, 120.0f, 0.1f), 55.0f, 0.25f);
        addParameter ("ceiling", "Ceiling", "dB", juce::NormalisableRange<float> (-24.0f, -0.1f, 0.1f), -3.0f, 0.2f);
    }

    bool safetyTripped() const noexcept override
    {
        return tripped.load (std::memory_order_relaxed);
    }

    void resetSafety() noexcept override
    {
        resetRequested.store (true, std::memory_order_release);
    }

    juce::String statusText() const override
    {
        return safetyTripped() ? "TRIPPED - click RESET" : "Causal block delay + DC block + hard bound";
    }

private:
    void prepareDsp (double, int maximumBlockSize) override
    {
        delayed.assign (static_cast<std::size_t> (maximumBlockSize), 0.0f);
        setReportedLatencySamples (maximumBlockSize);
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        std::fill (delayed.begin(), delayed.end(), 0.0f);
        delayCursor = 0;
        currentReadStart = 0;
        currentReadSamples = 0;
        previousInput = 0.0f;
        previousDcOutput = 0.0f;
        dangerousBlocks = 0;
    }

    void processDsp (const juce::AudioBuffer<float>&,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            tripped.store (false, std::memory_order_relaxed);
            resetDsp();
        }
        auto* output = outputs.getWritePointer (0);
        const auto size = static_cast<int> (delayed.size());
        const auto available = juce::jmin (numSamples, size);
        currentReadStart = delayCursor;
        currentReadSamples = available;
        for (int sample = 0; sample < available; ++sample)
            output[sample] = delayed[static_cast<std::size_t> ((currentReadStart + sample) % size)];
        if (available < numSamples)
            juce::FloatVectorOperations::clear (output + available, numSamples - available);
    }

    void processFeedbackWriteDsp (const juce::AudioBuffer<float>& inputs, int numSamples) noexcept override
    {
        if (delayed.empty())
            return;
        const auto* input = inputs.getReadPointer (0);
        const auto size = static_cast<int> (delayed.size());
        const auto available = juce::jmin (numSamples, currentReadSamples);
        bool dangerous = false;

        for (int sample = 0; sample < available; ++sample)
        {
            const auto raw = input[sample];
            if (! std::isfinite (raw) || std::abs (raw) > 16.0f)
                dangerous = true;
            const auto safeInput = std::isfinite (raw) ? juce::jlimit (-16.0f, 16.0f, raw) : 0.0f;
            const auto amount = parameterValue (0, inputs, sample) * 0.01f;
            const auto ceiling = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            const auto driven = safeInput * amount;
            const auto dcBlocked = driven - previousInput + 0.995f * previousDcOutput;
            previousInput = driven;
            previousDcOutput = dcBlocked;
            const auto bounded = std::tanh (dcBlocked);
            delayed[static_cast<std::size_t> ((currentReadStart + sample) % size)] = safetyTripped()
                ? 0.0f
                : juce::jlimit (-ceiling, ceiling, bounded);
        }
        delayCursor = (currentReadStart + available) % size;
        currentReadSamples = 0;

        dangerousBlocks = dangerous ? dangerousBlocks + 1 : juce::jmax (0, dangerousBlocks - 1);
        if (dangerousBlocks >= 3)
        {
            tripped.store (true, std::memory_order_relaxed);
            std::fill (delayed.begin(), delayed.end(), 0.0f);
        }
    }

    std::vector<float> delayed;
    int delayCursor = 0;
    int currentReadStart = 0;
    int currentReadSamples = 0;
    float previousInput = 0.0f;
    float previousDcOutput = 0.0f;
    int dangerousBlocks = 0;
    std::atomic<bool> tripped { false };
    std::atomic<bool> resetRequested { false };
};

class LfoNode final : public DspNode
{
public:
    LfoNode() : DspNode (NodeKind::lfo, nodeKindName (NodeKind::lfo))
    {
        addOutputPort ("Control", SignalType::control);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.02f, 30.0f, 1.0f), 1.0f, 0.35f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
        addParameter ("shape", "Shape", "", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f, 0.0f);
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; }
    void resetDsp() noexcept override { phase = 0.0; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto depth = parameterValue (1, inputs, sample) * 0.01f;
            const auto shape = juce::roundToInt (parameterValue (2, inputs, sample));
            float value = 0.0f;
            switch (shape)
            {
                case 1: value = static_cast<float> (4.0 * std::abs (phase - 0.5) - 1.0); break;
                case 2: value = phase < 0.5 ? 1.0f : -1.0f; break;
                case 3: value = static_cast<float> (2.0 * phase - 1.0); break;
                default:value = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase)); break;
            }
            output[sample] = value * depth;
            phase += rate / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
};

class EnvelopeFollowerNode final : public DspNode
{
public:
    EnvelopeFollowerNode() : DspNode (NodeKind::envelopeFollower, nodeKindName (NodeKind::envelopeFollower))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Control", SignalType::control);
        addParameter ("attack", "Attack", "ms", skewedRange (0.1f, 100.0f, 10.0f), 10.0f, 0.25f);
        addParameter ("release", "Release", "ms", skewedRange (5.0f, 1000.0f, 150.0f), 150.0f, 0.25f);
        addParameter ("gain", "Sensitivity", "dB", juce::NormalisableRange<float> (-24.0f, 48.0f, 0.1f), 12.0f, 0.25f);
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; }
    void resetDsp() noexcept override { envelope = 0.0f; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto attack = parameterValue (0, inputs, sample);
            const auto release = parameterValue (1, inputs, sample);
            const auto sensitivity = juce::Decibels::decibelsToGain (parameterValue (2, inputs, sample));
            const auto magnitude = std::abs (input[sample]) * sensitivity;
            const auto coefficient = std::exp (-1.0 / (sampleRate * 0.001
                                                       * (magnitude > envelope ? attack : release)));
            envelope = static_cast<float> (coefficient * envelope + (1.0 - coefficient) * magnitude);
            output[sample] = juce::jlimit (0.0f, 1.0f, envelope);
        }
    }

    double sampleRate = 48000.0;
    float envelope = 0.0f;
};

class StepSequencerNode final : public DspNode
{
public:
    StepSequencerNode() : DspNode (NodeKind::stepSequencer, nodeKindName (NodeKind::stepSequencer))
    {
        addOutputPort ("Control", SignalType::control);
        addParameter ("rate", "Step rate", "Hz", skewedRange (0.25f, 30.0f, 4.0f), 4.0f, 0.25f);
        addParameter ("glide", "Glide", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, 0.35f);
        constexpr std::array<float, 8> defaults { 0.8f, -0.4f, 0.2f, -0.8f, 0.6f, 0.0f, 1.0f, -0.2f };
        for (int step = 0; step < 8; ++step)
            addParameter ("step-" + juce::String (step + 1), "Step " + juce::String (step + 1), "",
                          juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), defaults[static_cast<std::size_t> (step)], 0.25f);
        addInputPort ("Clock", SignalType::control); // last, so older cables keep their indices
        clock.port = getNumInputPorts() - 1;
    }

    int currentStep() const noexcept override
    {
        return activeStep.load (std::memory_order_relaxed);
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; }

    void resetDsp() noexcept override
    {
        stepPhase = 0.0;
        stepIndex = 0;
        smoothedOutput = 0.0f;
        clock.reset();
        activeStep.store (0, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        const auto* clockIn = clock.pointer (inputs);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto glide = parameterValue (1, inputs, sample) * 0.01f;
            const auto target = parameterValue (2 + stepIndex, inputs, sample);
            const auto glideTime = 0.001 + glide * 0.2;
            const auto coefficient = std::exp (-1.0 / (sampleRate * glideTime));
            smoothedOutput = static_cast<float> (coefficient * smoothedOutput + (1.0 - coefficient) * target);
            output[sample] = smoothedOutput;
            const bool clockEdge = clock.tick (clockIn, sample, sampleRate);
            bool advance = false;
            if (clock.external (sampleRate))
            {
                advance = clockEdge;
                stepPhase = 0.0;
            }
            else
            {
                stepPhase += rate / sampleRate;
                if (stepPhase >= 1.0)
                {
                    stepPhase -= 1.0;
                    advance = true;
                }
            }
            if (advance)
            {
                stepIndex = (stepIndex + 1) % 8;
                activeStep.store (stepIndex, std::memory_order_relaxed);
            }
        }
    }

    double sampleRate = 48000.0;
    double stepPhase = 0.0;
    int stepIndex = 0;
    float smoothedOutput = 0.0f;
    ClockFollower clock;
    std::atomic<int> activeStep { 0 };
};

// Tone controls around a neural capture: RBJ biquads whose coefficients are
// recomputed on the audio thread only when a knob moved (a few flops), so the
// per-sample cost is one multiply-add chain per band.
struct ToneBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    float process (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }

    void set (double bb0, double bb1, double bb2, double aa0, double aa1, double aa2) noexcept
    {
        b0 = static_cast<float> (bb0 / aa0); b1 = static_cast<float> (bb1 / aa0); b2 = static_cast<float> (bb2 / aa0);
        a1 = static_cast<float> (aa1 / aa0); a2 = static_cast<float> (aa2 / aa0);
    }
    void lowShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto cw = std::cos (w), sw = std::sin (w), alpha = sw / 2.0 * std::sqrt (2.0), beta = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) - (A - 1) * cw + beta), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - beta),
             (A + 1) + (A - 1) * cw + beta, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - beta);
    }
    void highShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto cw = std::cos (w), sw = std::sin (w), alpha = sw / 2.0 * std::sqrt (2.0), beta = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) + (A - 1) * cw + beta), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - beta),
             (A + 1) - (A - 1) * cw + beta, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - beta);
    }
    void peak (double sampleRate, double frequency, double q, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }
};

struct AmpToneStack
{
    ToneBiquad bass, mid, treble, presence;
    float lastBass = 1.0e9f, lastMid = 1.0e9f, lastTreble = 1.0e9f, lastPresence = 1.0e9f;

    void update (double sampleRate, float bassDb, float midDb, float trebleDb, float presenceDb) noexcept
    {
        if (std::abs (bassDb - lastBass) > 0.01f)         { bass.lowShelf (sampleRate, 110.0, bassDb);          lastBass = bassDb; }
        if (std::abs (midDb - lastMid) > 0.01f)           { mid.peak (sampleRate, 750.0, 0.7, midDb);            lastMid = midDb; }
        if (std::abs (trebleDb - lastTreble) > 0.01f)     { treble.highShelf (sampleRate, 2200.0, trebleDb);     lastTreble = trebleDb; }
        if (std::abs (presenceDb - lastPresence) > 0.01f) { presence.peak (sampleRate, 4800.0, 1.2, presenceDb); lastPresence = presenceDb; }
    }
    float process (float x) noexcept { return presence.process (treble.process (mid.process (bass.process (x)))); }
    void reset() noexcept { bass.reset(); mid.reset(); treble.reset(); presence.reset(); }
};

// One engine, two personalities: the amp head and the stompbox. Both run any
// .nam capture; the pedal adds a wet/dry mix because drive captures are often
// blended, and both can step through the models folder like a pedal library.
class NeuralAmpNode final : public DspNode
{
public:
    explicit NeuralAmpNode (NodeKind kindToUse)
        : DspNode (kindToUse, nodeKindName (kindToUse))
    {
        const bool pedal = kindToUse == NodeKind::neuralPedal;
        addInputPort (pedal ? "In" : "Guitar", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        // Keys and the first indices are stable (saved patches store values by
        // index); the tone knobs came later and sit at the end.
        addParameter ("input-trim", pedal ? "Drive" : "Gain", "dB", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("output-trim", pedal ? "Level" : "Master", "dB", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, 0.25f);
        if (pedal)
        {
            addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
            addParameter ("tone", "Tone", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, 0.5f);
        }
        else
        {
            addParameter ("bass", "Bass", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, 0.5f);
            addParameter ("mid", "Mid", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, 0.5f);
            addParameter ("treble", "Treble", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, 0.5f);
            addParameter ("presence", "Presence", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, 0.5f);
        }
    }

    /** Amp: Bass/Mid/Treble/Presence after the model; Pedal: Tone as a high shelf. Called once per block. */
    void updateTone (const juce::AudioBuffer<float>& inputs) noexcept
    {
        // The knob's target (plus its mod socket at the block start) rather than
        // parameterValue(): that steps the per-sample smoother, and one step per
        // block would take seconds to arrive.
        auto target = [&] (int index) { return parameterTarget (index, inputs); };
        if (getKind() == NodeKind::neuralPedal)
            tone.update (sampleRate, 0.0f, 0.0f, target (3), 0.0f);
        else
            tone.update (sampleRate, target (2), target (3), target (4), target (5));
    }

    /** Where model cycling looks when the node has no model yet. */
    static juce::File defaultModelsDirectory()
    {
        if (const auto* env = std::getenv ("SIGNALPATCH_MODELS_DIR"))
        {
            const juce::File dir { juce::String (env) };
            if (dir.isDirectory())
                return dir;
        }
        const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("SignalPatch").getChildFile ("models");
        if (documents.isDirectory())
            return documents;
        const auto lv2Clone = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Projects/GitHub/external_clones/neural-amp-modeler-lv2-a2/models");
        if (lv2Clone.isDirectory())
            return lv2Clone;
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "prev-model")
            return cycleModel (-1);
        if (command == "next-model")
            return cycleModel (1);
        return false;
    }

    juce::var getExtraState() const override
    {
        if (modelPath.isEmpty())
            return {};
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("model", modelPath);
        return juce::var (object.release());
    }

    // Message thread. Steps to the neighbouring .nam file in the current
    // model's folder (or the default models folder when empty).
    bool cycleModel (int delta)
    {
        const auto currentFile = juce::File (modelPath);
        const auto directory = modelPath.isNotEmpty() && currentFile.getParentDirectory().isDirectory()
            ? currentFile.getParentDirectory()
            : defaultModelsDirectory();
        auto candidates = directory.findChildFiles (juce::File::findFiles, false, "*.nam");
        if (candidates.isEmpty())
            return false;
        candidates.sort();
        int index = 0;
        for (int i = 0; i < candidates.size(); ++i)
            if (candidates.getReference (i) == currentFile)
            {
                index = i + delta;
                break;
            }
        index = (index % candidates.size() + candidates.size()) % candidates.size();
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("model", candidates.getReference (index).getFullPathName());
        setExtraState (juce::var (object.release()));
        return true;
    }

#if SIGNALPATCH_HAS_NAM
    void setExtraState (const juce::var& state) override
    {
        const auto path = state.getProperty ("model", juce::String()).toString();
        if (path.isEmpty())
            return;
        modelPath = path;
        loadError.clear();

        // Headless (tests): no message loop, load synchronously.
        if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        {
            juce::String error;
            finishModelLoad (path, parseModel (path, error), error);
            return;
        }

        // Parsing a big WaveNet can take a second; do it on a worker so the
        // UI never freezes, then apply on the message thread. The weak handle
        // survives node deletion while the worker runs.
        loading.store (true, std::memory_order_relaxed);
        std::weak_ptr<DspNode> weakSelf = weak_from_this();
        juce::Thread::launch ([weakSelf, path]
        {
            juce::String error;
            auto loaded = std::make_shared<std::unique_ptr<nam::DSP>> (parseModel (path, error));
            juce::MessageManager::callAsync ([weakSelf, path, error, loaded]
            {
                if (auto locked = weakSelf.lock())
                    static_cast<NeuralAmpNode&> (*locked).finishModelLoad (path, std::move (*loaded), error);
            });
        });
    }

    juce::String statusText() const override
    {
        if (loading.load (std::memory_order_relaxed))
            return "loading " + juce::File (modelPath).getFileName() + "...";
        if (loadError.isNotEmpty())
            return "LOAD FAILED: " + loadError;
        if (activeModel.load (std::memory_order_relaxed) == nullptr)
            return "no model - LOAD or step with the arrows";
        auto label = juce::File (modelPath).getFileNameWithoutExtension();
        const auto expected = currentModel != nullptr ? currentModel->GetExpectedSampleRate() : -1.0;
        if (expected > 0.0 && std::abs (expected - sampleRate) > 1.0)
            label += " (" + juce::String (expected / 1000.0, 1) + "k model at "
                   + juce::String (sampleRate / 1000.0, 1) + "k)";
        const auto permille = costPermille.load (std::memory_order_relaxed);
        if (permille > 0)
            label += " - " + juce::String (permille / 10.0f, 1) + "% of block";
        return label;
    }

private:
    static std::unique_ptr<nam::DSP> parseModel (const juce::String& path, juce::String& error)
    {
        try
        {
            auto loaded = nam::get_dsp (std::filesystem::path (path.toStdString()));
            if (loaded == nullptr)
                throw std::runtime_error ("unrecognized model format");
            if (loaded->NumInputChannels() != 1 || loaded->NumOutputChannels() != 1)
                throw std::runtime_error ("only mono models are supported");
            return loaded;
        }
        catch (const std::exception& caught)
        {
            error = caught.what();
            return nullptr;
        }
    }

    // Message thread only. Warms the model for the current device settings
    // and hands it to the callback atomically; the previous model is retained
    // until the next successful load so audio never frees or races it.
    void finishModelLoad (const juce::String& path, std::unique_ptr<nam::DSP> loaded,
                          const juce::String& error)
    {
        if (path != modelPath)
            return; // superseded by a newer request
        loading.store (false, std::memory_order_relaxed);
        if (loaded == nullptr)
        {
            loadError = error.isNotEmpty() ? error : "could not load the model";
            activeModel.store (nullptr, std::memory_order_release);
            return;
        }
        loadError.clear();
        loaded->ResetAndPrewarm (sampleRate, juce::jmax (16, maximumBlockSize));
        activeModel.store (nullptr, std::memory_order_release);
        retiredModel = std::move (currentModel);
        currentModel = std::move (loaded);
        activeModel.store (currentModel.get(), std::memory_order_release);
    }
    void prepareDsp (double newSampleRate, int newMaximumBlockSize) override
    {
        sampleRate = newSampleRate;
        maximumBlockSize = juce::jmax (16, newMaximumBlockSize);
        scratchIn.assign (static_cast<std::size_t> (maximumBlockSize), 0.0f);
        scratchOut.assign (static_cast<std::size_t> (maximumBlockSize), 0.0f);
        tone = AmpToneStack {}; // coefficients depend on the sample rate
        // prepareAll runs while the device callback is stopped, so resetting
        // the loaded model here is safe.
        if (currentModel != nullptr)
            currentModel->ResetAndPrewarm (sampleRate, maximumBlockSize);
    }

    void resetDsp() noexcept override { tone.reset(); }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        auto* model = activeModel.load (std::memory_order_acquire);
        const auto frames = juce::jmin (numSamples, static_cast<int> (scratchIn.size()));

        updateTone (inputs);
        if (model == nullptr || frames <= 0)
        {
            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto inputGain = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
                const auto outputGain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
                const auto x = std::isfinite (input[sample]) ? input[sample] : 0.0f;
                output[sample] = tone.process (x * inputGain) * outputGain;
            }
            return;
        }

        for (int sample = 0; sample < frames; ++sample)
        {
            const auto inputGain = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto raw = input[sample] * inputGain;
            scratchIn[static_cast<std::size_t> (sample)] = std::isfinite (raw) ? raw : 0.0f;
        }

        const auto startTicks = juce::Time::getHighResolutionTicks();
        auto* inPointer = scratchIn.data();
        auto* outPointer = scratchOut.data();
        model->process (&inPointer, &outPointer, frames);
        const auto elapsed = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - startTicks);
        const auto blockDuration = static_cast<double> (frames) / juce::jmax (1.0, sampleRate);
        costPermille.store (juce::jlimit (0, 4000,
                                          juce::roundToInt (1000.0 * elapsed / blockDuration)),
                            std::memory_order_relaxed);

        const bool isPedal = getKind() == NodeKind::neuralPedal;
        for (int sample = 0; sample < frames; ++sample)
        {
            const auto outputGain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            auto modelled = scratchOut[static_cast<std::size_t> (sample)];
            if (! std::isfinite (modelled))
                modelled = 0.0f;
            auto value = tone.process (modelled);
            if (isPedal)
            {
                // Mix blends the untouched input (no Drive) with the pedal; Level sets the result.
                const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
                const auto dry = std::isfinite (input[sample]) ? input[sample] : 0.0f;
                value = dry + mix * (value - dry);
            }
            output[sample] = juce::jlimit (-4.0f, 4.0f, value * outputGain);
        }
        if (frames < numSamples)
            juce::FloatVectorOperations::clear (output + frames, numSamples - frames);
    }

    std::unique_ptr<nam::DSP> currentModel;
    std::unique_ptr<nam::DSP> retiredModel;
    std::atomic<nam::DSP*> activeModel { nullptr };
    std::atomic<bool> loading { false };
    std::atomic<int> costPermille { 0 }; // model cost as 0.1% units of the block deadline
    std::vector<float> scratchIn;
    std::vector<float> scratchOut;
    int maximumBlockSize = 128;
#else
    void setExtraState (const juce::var& state) override
    {
        const auto path = state.getProperty ("model", juce::String()).toString();
        if (path.isNotEmpty())
            modelPath = path; // preserved so the patch survives a NAM-less build
    }

    juce::String statusText() const override
    {
        return "NAM runtime is not compiled; safe bypass is active";
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; tone = AmpToneStack {}; }
    void resetDsp() noexcept override { tone.reset(); }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        updateTone (inputs);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto inputGain = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto outputGain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            output[sample] = tone.process (input[sample] * inputGain) * outputGain;
        }
    }
#endif

    double sampleRate = 48000.0;
    juce::String modelPath;
    juce::String loadError;
    AmpToneStack tone;
};
class CrossfadeNode final : public DspNode
{
public:
    CrossfadeNode() : DspNode (NodeKind::crossfade, nodeKindName (NodeKind::crossfade))
    {
        addInputPort ("A", SignalType::audio);
        addInputPort ("B", SignalType::audio);
        addOutputPort ("Mix", SignalType::audio);
        addParameter ("position", "A <-> B", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, 0.5f);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* inputA = inputs.getReadPointer (0);
        const auto* inputB = inputs.getReadPointer (1);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto position = juce::jlimit (0.0f, 1.0f, parameterValue (0, inputs, sample) * 0.01f);
            const auto angle = position * juce::MathConstants<float>::halfPi;
            output[sample] = inputA[sample] * std::cos (angle) + inputB[sample] * std::sin (angle);
        }
    }
};

class FilterNode final : public DspNode
{
public:
    FilterNode() : DspNode (NodeKind::filter, nodeKindName (NodeKind::filter))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("cutoff", "Cutoff", "Hz", skewedRange (20.0f, 20000.0f, 1000.0f), 1200.0f, 0.4f);
        addParameter ("resonance", "Resonance", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f, 0.35f);
        addParameter ("mode", "Mode", "", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f, 0.0f);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto cutoff = juce::jlimit (10.0f, static_cast<float> (sampleRate * 0.49),
                                              parameterValue (0, inputs, sample));
            const auto resonance = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto mode = juce::roundToInt (parameterValue (2, inputs, sample));

            // Zavalishin TPT state-variable filter, stable under modulation.
            const auto g = std::tan (juce::MathConstants<float>::pi
                                     * cutoff / static_cast<float> (sampleRate));
            const auto k = 2.0f - 1.9f * resonance;
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;

            const auto v0 = input[sample];
            const auto v3 = v0 - ic2eq;
            const auto v1 = a1 * ic1eq + a2 * v3;
            const auto v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            if (! std::isfinite (ic1eq) || ! std::isfinite (ic2eq))
            {
                ic1eq = 0.0f;
                ic2eq = 0.0f;
            }

            switch (mode)
            {
                case 1:  output[sample] = v1; break;                       // band-pass
                case 2:  output[sample] = v0 - k * v1 - v2; break;         // high-pass
                case 3:  output[sample] = v0 - k * v1; break;              // notch
                default: output[sample] = v2; break;                       // low-pass
            }
        }
    }

    double sampleRate = 48000.0;
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

class ReverbNode final : public DspNode
{
public:
    ReverbNode() : DspNode (NodeKind::reverb, nodeKindName (NodeKind::reverb))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("size", "Size", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 55.0f, 0.35f);
        addParameter ("damp", "Damping", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, 0.5f);
    }

private:
    static constexpr std::size_t combCount = 8;
    static constexpr std::size_t allpassCount = 4;

    void prepareDsp (double newSampleRate, int) override
    {
        // Freeverb tunings, scaled from their reference 44.1 kHz rate.
        static constexpr int combTunings[combCount] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int allpassTunings[allpassCount] { 556, 441, 341, 225 };
        const auto scale = newSampleRate / 44100.0;
        for (std::size_t comb = 0; comb < combCount; ++comb)
        {
            combBuffers[comb].assign (static_cast<std::size_t> (
                juce::jmax (4, juce::roundToInt (combTunings[comb] * scale))), 0.0f);
            combIndices[comb] = 0;
            combFilters[comb] = 0.0f;
        }
        for (std::size_t allpass = 0; allpass < allpassCount; ++allpass)
        {
            allpassBuffers[allpass].assign (static_cast<std::size_t> (
                juce::jmax (4, juce::roundToInt (allpassTunings[allpass] * scale))), 0.0f);
            allpassIndices[allpass] = 0;
        }
    }

    void resetDsp() noexcept override
    {
        for (std::size_t comb = 0; comb < combCount; ++comb)
        {
            std::fill (combBuffers[comb].begin(), combBuffers[comb].end(), 0.0f);
            combIndices[comb] = 0;
            combFilters[comb] = 0.0f;
        }
        for (std::size_t allpass = 0; allpass < allpassCount; ++allpass)
        {
            std::fill (allpassBuffers[allpass].begin(), allpassBuffers[allpass].end(), 0.0f);
            allpassIndices[allpass] = 0;
        }
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        if (combBuffers[0].empty())
            return;
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto size = juce::jlimit (0.0f, 1.0f, parameterValue (0, inputs, sample) * 0.01f);
            const auto damp = juce::jlimit (0.0f, 0.95f, parameterValue (1, inputs, sample) * 0.01f * 0.9f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto roomFeedback = 0.72f + 0.26f * size;
            const auto dry = input[sample];
            const auto fed = dry * 0.015f;

            float wet = 0.0f;
            for (std::size_t comb = 0; comb < combCount; ++comb)
            {
                auto& buffer = combBuffers[comb];
                auto& index = combIndices[comb];
                const auto delayed = buffer[static_cast<std::size_t> (index)];
                wet += delayed;
                combFilters[comb] = delayed * (1.0f - damp) + combFilters[comb] * damp;
                if (! std::isfinite (combFilters[comb]))
                    combFilters[comb] = 0.0f;
                buffer[static_cast<std::size_t> (index)] = fed + combFilters[comb] * roomFeedback;
                if (++index >= static_cast<int> (buffer.size()))
                    index = 0;
            }

            for (std::size_t allpass = 0; allpass < allpassCount; ++allpass)
            {
                auto& buffer = allpassBuffers[allpass];
                auto& index = allpassIndices[allpass];
                const auto buffered = buffer[static_cast<std::size_t> (index)];
                buffer[static_cast<std::size_t> (index)] = wet + buffered * 0.5f;
                wet = buffered - wet;
                if (++index >= static_cast<int> (buffer.size()))
                    index = 0;
            }

            output[sample] = dry + mix * (wet - dry);
        }
    }

    std::array<std::vector<float>, combCount> combBuffers;
    std::array<int, combCount> combIndices {};
    std::array<float, combCount> combFilters {};
    std::array<std::vector<float>, allpassCount> allpassBuffers;
    std::array<int, allpassCount> allpassIndices {};
};

class ChorusNode final : public DspNode
{
public:
    ChorusNode() : DspNode (NodeKind::chorus, nodeKindName (NodeKind::chorus))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.05f, 6.0f, 0.8f), 0.8f, 0.35f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 45.0f, 0.5f);
        addParameter ("delay", "Delay", "ms", juce::NormalisableRange<float> (5.0f, 30.0f, 0.1f), 14.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        delayLine.assign (static_cast<std::size_t> (std::ceil (sampleRate * 0.045))
                              + static_cast<std::size_t> (maximumBlockSize) + 4u, 0.0f);
        writePosition = 0;
        phase = 0.0;
    }

    void resetDsp() noexcept override
    {
        std::fill (delayLine.begin(), delayLine.end(), 0.0f);
        writePosition = 0;
        phase = 0.0;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto size = static_cast<int> (delayLine.size());
        if (size < 4)
            return;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto depth = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto centreMs = parameterValue (2, inputs, sample);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);

            const auto wobble = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            const auto delayMs = juce::jmax (1.0f, centreMs + wobble * depth * 8.0f);
            const auto delaySamples = juce::jlimit (1.0f, static_cast<float> (size - 3),
                                                    delayMs * static_cast<float> (sampleRate * 0.001));

            float readPosition = static_cast<float> (writePosition) - delaySamples;
            while (readPosition < 0.0f)
                readPosition += static_cast<float> (size);
            const auto indexA = static_cast<int> (readPosition) % size;
            const auto indexB = (indexA + 1) % size;
            const auto fraction = readPosition - std::floor (readPosition);
            const auto delayed = delayLine[static_cast<std::size_t> (indexA)]
                               + fraction * (delayLine[static_cast<std::size_t> (indexB)]
                                             - delayLine[static_cast<std::size_t> (indexA)]);

            delayLine[static_cast<std::size_t> (writePosition)] = input[sample];
            writePosition = (writePosition + 1) % size;
            output[sample] = input[sample] + mix * (delayed - input[sample]);

            phase += rate / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    std::vector<float> delayLine;
    int writePosition = 0;
    double phase = 0.0;
};

class PhaserNode final : public DspNode
{
public:
    PhaserNode() : DspNode (NodeKind::phaser, nodeKindName (NodeKind::phaser))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.05f, 8.0f, 0.5f), 0.5f, 0.35f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f, 0.5f);
        addParameter ("feedback", "Feedback", "%", juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 30.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, 0.5f);
    }

private:
    static constexpr int stageCount = 6;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        std::fill (stageStates.begin(), stageStates.end(), 0.0f);
        feedbackSample = 0.0f;
        phase = 0.0;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto depth = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto feedback = juce::jlimit (0.0f, 0.9f, parameterValue (2, inputs, sample) * 0.01f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);

            const auto sweep = 0.5f + 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            const auto frequency = 220.0f * std::pow (10.0f, sweep * depth);
            const auto tangent = std::tan (juce::MathConstants<float>::pi
                                           * juce::jlimit (30.0f, static_cast<float> (sampleRate * 0.45f), frequency)
                                           / static_cast<float> (sampleRate));
            const auto coefficient = (tangent - 1.0f) / (tangent + 1.0f);

            if (! std::isfinite (feedbackSample))
                feedbackSample = 0.0f;
            auto stageSignal = input[sample] + feedbackSample * feedback;
            for (int stage = 0; stage < stageCount; ++stage)
            {
                const auto stageInput = stageSignal;
                stageSignal = coefficient * stageInput + stageStates[static_cast<std::size_t> (stage)];
                stageStates[static_cast<std::size_t> (stage)] = stageInput - coefficient * stageSignal;
                if (! std::isfinite (stageStates[static_cast<std::size_t> (stage)]))
                    stageStates[static_cast<std::size_t> (stage)] = 0.0f;
            }
            feedbackSample = stageSignal;
            // Equal blend of dry and all-passed signal creates the notches.
            output[sample] = input[sample] * (1.0f - mix * 0.5f) + stageSignal * mix * 0.5f;

            phase += rate / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    std::array<float, stageCount> stageStates {};
    float feedbackSample = 0.0f;
    double phase = 0.0;
};

class TremoloNode final : public DspNode
{
public:
    TremoloNode() : DspNode (NodeKind::tremolo, nodeKindName (NodeKind::tremolo))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.1f, 30.0f, 5.0f), 5.0f, 0.35f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, 0.5f);
        addParameter ("shape", "Shape", "", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f, 0.0f);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        phase = 0.0;
    }

    void resetDsp() noexcept override { phase = 0.0; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto depth = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto square = juce::roundToInt (parameterValue (2, inputs, sample)) == 1;

            auto wave = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            if (square)
                wave = std::tanh (wave * 6.0f); // rounded square avoids clicks
            const auto gain = 1.0f - depth * (0.5f + 0.5f * wave);
            output[sample] = input[sample] * gain;

            phase += rate / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
};

class BitcrusherNode final : public DspNode
{
public:
    BitcrusherNode() : DspNode (NodeKind::bitcrusher, nodeKindName (NodeKind::bitcrusher))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("bits", "Bits", "", juce::NormalisableRange<float> (1.0f, 16.0f, 1.0f), 8.0f, 0.35f);
        addParameter ("downsample", "Downsample", "x", juce::NormalisableRange<float> (1.0f, 50.0f, 1.0f), 4.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    void prepareDsp (double, int) override
    {
        holdCounter = 0;
        heldSample = 0.0f;
    }

    void resetDsp() noexcept override
    {
        holdCounter = 0;
        heldSample = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto bits = juce::jlimit (1, 16, juce::roundToInt (parameterValue (0, inputs, sample)));
            const auto downsample = juce::jlimit (1, 50, juce::roundToInt (parameterValue (1, inputs, sample)));
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);

            if (holdCounter <= 0)
            {
                const auto levels = static_cast<float> (1 << (bits - 1));
                const auto clamped = juce::jlimit (-1.0f, 1.0f,
                                                   std::isfinite (input[sample]) ? input[sample] : 0.0f);
                heldSample = std::round (clamped * levels) / levels;
                holdCounter = downsample;
            }
            --holdCounter;
            output[sample] = input[sample] + mix * (heldSample - input[sample]);
        }
    }

    int holdCounter = 0;
    float heldSample = 0.0f;
};

class GateNode final : public DspNode
{
public:
    GateNode() : DspNode (NodeKind::gate, nodeKindName (NodeKind::gate))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("threshold", "Threshold", "dB", juce::NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -45.0f, 0.35f);
        addParameter ("attack", "Attack", "ms", skewedRange (0.1f, 100.0f, 5.0f), 5.0f, 0.25f);
        addParameter ("release", "Release", "ms", skewedRange (5.0f, 2000.0f, 200.0f), 200.0f, 0.25f);
        addParameter ("range", "Range", "dB", juce::NormalisableRange<float> (0.0f, 80.0f, 0.1f), 60.0f, 0.25f);
    }

    float gainReductionDb() const noexcept override
    {
        return reduction.load (std::memory_order_relaxed);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        envelope = 0.0f;
        gateGain = 0.0f;
        reduction.store (0.0f, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        float lastReduction = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto threshold = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto attackMs = parameterValue (1, inputs, sample);
            const auto releaseMs = parameterValue (2, inputs, sample);
            const auto range = parameterValue (3, inputs, sample);

            const auto magnitude = std::abs (input[sample]);
            const auto envelopeCoefficient = std::exp (-1.0 / (sampleRate * 0.001 * 10.0));
            envelope = static_cast<float> (envelopeCoefficient * envelope
                                           + (1.0 - envelopeCoefficient) * magnitude);
            if (! std::isfinite (envelope))
                envelope = 0.0f;

            const auto open = envelope > threshold;
            const auto smoothingMs = open ? attackMs : releaseMs;
            const auto gainCoefficient = std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (0.1f, smoothingMs)));
            gateGain = static_cast<float> (gainCoefficient * gateGain
                                           + (1.0 - gainCoefficient) * (open ? 1.0f : 0.0f));

            const auto floorGain = juce::Decibels::decibelsToGain (-range);
            const auto gain = floorGain + (1.0f - floorGain) * gateGain;
            output[sample] = input[sample] * gain;
            lastReduction = -juce::Decibels::gainToDecibels (juce::jmax (gain, 1.0e-5f));
        }
        reduction.store (juce::jmax (0.0f, lastReduction), std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    float envelope = 0.0f;
    float gateGain = 0.0f;
    std::atomic<float> reduction { 0.0f };
};

class RandomLfoNode final : public DspNode
{
public:
    RandomLfoNode() : DspNode (NodeKind::randomLfo, nodeKindName (NodeKind::randomLfo))
    {
        addOutputPort ("Control", SignalType::control);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.05f, 30.0f, 2.0f), 2.0f, 0.35f);
        addParameter ("smooth", "Smooth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f, 0.5f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; }

    void resetDsp() noexcept override
    {
        phase = 0.0;
        target = 0.0f;
        current = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto smooth = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto depth = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);

            phase += rate / sampleRate;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                target = random.nextFloat() * 2.0f - 1.0f; // arithmetic only; safe in the callback
            }

            const auto slewTime = 0.0005 + smooth * 0.25;
            const auto coefficient = std::exp (-1.0 / (sampleRate * slewTime));
            current = static_cast<float> (coefficient * current + (1.0 - coefficient) * target);
            output[sample] = current * depth;
        }
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
    float target = 0.0f;
    float current = 0.0f;
    juce::Random random; // seeded on the message thread at construction
};

class MacroNode final : public DspNode
{
public:
    MacroNode() : DspNode (NodeKind::macro, nodeKindName (NodeKind::macro))
    {
        addOutputPort ("Control", SignalType::control);
        addParameter ("value", "Value", "", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f, 0.5f);
        addParameter ("slew", "Slew", "ms", skewedRange (1.0f, 2000.0f, 80.0f), 80.0f, 0.25f);
    }

private:
    void prepareDsp (double newSampleRate, int) override { sampleRate = newSampleRate; }
    void resetDsp() noexcept override { current = 0.0f; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto value = parameterValue (0, inputs, sample);
            const auto slewMs = juce::jmax (1.0f, parameterValue (1, inputs, sample));
            const auto coefficient = std::exp (-1.0 / (sampleRate * 0.001 * slewMs));
            current = static_cast<float> (coefficient * current + (1.0 - coefficient) * value);
            output[sample] = current;
        }
    }

    double sampleRate = 48000.0;
    float current = 0.0f;
};
// A minimal TPT state-variable band-pass used by the voice effects. The
// coefficients are precomputed where the centre frequency is fixed.
struct BandpassState
{
    float g = 0.1f;
    float k = 0.4f;
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    void setup (float frequency, float q, double sampleRate) noexcept
    {
        g = std::tan (juce::MathConstants<float>::pi
                      * juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), frequency)
                      / static_cast<float> (sampleRate));
        k = 1.0f / juce::jmax (0.1f, q);
    }

    float processBand (float input) noexcept
    {
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto v3 = input - ic2eq;
        const auto v1 = a1 * ic1eq + a2 * v3;
        const auto v2 = ic2eq + a2 * ic1eq + g * a2 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        if (! std::isfinite (ic1eq) || ! std::isfinite (ic2eq))
        {
            ic1eq = 0.0f;
            ic2eq = 0.0f;
            return 0.0f;
        }
        return k * v1; // unity-gain band-pass
    }

    void clear() noexcept
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }
};

// Dual-tap windowed delay-line pitch shifter shared by the shifter and the
// autotune. Two read taps scan a short window half a cycle apart; Hann
// weights sum to one so the crossfade is click-free.
struct PitchShiftEngine
{
    std::vector<float> buffer;
    int writePosition = 0;
    double tapPhase = 0.0;

    void prepare (double sampleRate, int maximumBlockSize)
    {
        buffer.assign (static_cast<std::size_t> (std::ceil (sampleRate * 0.25))
                           + static_cast<std::size_t> (maximumBlockSize) + 4u, 0.0f);
        writePosition = 0;
        tapPhase = 0.0;
    }

    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePosition = 0;
        tapPhase = 0.0;
    }

    float processSample (float input, float ratio, float windowSamples) noexcept
    {
        const auto size = static_cast<int> (buffer.size());
        if (size < 8)
            return input;
        buffer[static_cast<std::size_t> (writePosition)] = input;

        auto readTap = [this, size, windowSamples] (double phase) noexcept
        {
            const auto delay = juce::jlimit (1.0, static_cast<double> (size - 3),
                                             1.0 + phase * windowSamples);
            auto readPosition = static_cast<double> (writePosition) - delay;
            while (readPosition < 0.0)
                readPosition += size;
            const auto indexA = static_cast<int> (readPosition) % size;
            const auto indexB = (indexA + 1) % size;
            const auto fraction = static_cast<float> (readPosition - std::floor (readPosition));
            const auto sample = buffer[static_cast<std::size_t> (indexA)]
                              + fraction * (buffer[static_cast<std::size_t> (indexB)]
                                            - buffer[static_cast<std::size_t> (indexA)]);
            const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                        * static_cast<float> (phase));
            return sample * window;
        };

        const auto phaseB = tapPhase + 0.5 >= 1.0 ? tapPhase - 0.5 : tapPhase + 0.5;
        const auto output = readTap (tapPhase) + readTap (phaseB);

        tapPhase += (1.0 - static_cast<double> (ratio)) / juce::jmax (8.0f, windowSamples);
        tapPhase -= std::floor (tapPhase);
        writePosition = (writePosition + 1) % size;
        return output;
    }
};

class RingModNode final : public DspNode
{
public:
    RingModNode() : DspNode (NodeKind::ringMod, nodeKindName (NodeKind::ringMod))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("freq", "Frequency", "Hz", skewedRange (0.5f, 4000.0f, 100.0f), 30.0f, 0.4f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        phase = 0.0;
    }

    void resetDsp() noexcept override { phase = 0.0; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto frequency = parameterValue (0, inputs, sample);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto carrier = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            output[sample] = input[sample] + mix * (input[sample] * carrier - input[sample]);
            phase += frequency / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
};

class VowelFilterNode final : public DspNode
{
public:
    VowelFilterNode() : DspNode (NodeKind::vowelFilter, nodeKindName (NodeKind::vowelFilter))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("vowel", "Vowel A-U", "", juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 0.0f, 0.5f);
        addParameter ("resonance", "Resonance", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    // First three formants for A, E, I, O, U.
    static constexpr float formantTable[5][3] {
        { 800.0f, 1150.0f, 2900.0f },
        { 400.0f, 1600.0f, 2700.0f },
        { 350.0f, 1700.0f, 2700.0f },
        { 450.0f,  800.0f, 2830.0f },
        { 325.0f,  700.0f, 2700.0f }
    };
    static constexpr float formantGains[3] { 1.0f, 0.55f, 0.3f };

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        for (auto& band : bands)
            band.clear();
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto vowel = juce::jlimit (0.0f, 4.0f, parameterValue (0, inputs, sample));
            const auto resonance = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);

            const auto lower = juce::jmin (3, static_cast<int> (vowel));
            const auto blend = vowel - static_cast<float> (lower);
            const auto q = 4.0f + resonance * 14.0f;

            float wet = 0.0f;
            for (std::size_t formant = 0; formant < 3; ++formant)
            {
                const auto frequency = formantTable[lower][formant]
                                     + blend * (formantTable[lower + 1][formant]
                                                - formantTable[lower][formant]);
                bands[formant].setup (frequency, q, sampleRate);
                wet += bands[formant].processBand (input[sample]) * formantGains[formant];
            }
            wet *= 2.2f;
            output[sample] = input[sample] + mix * (wet - input[sample]);
        }
    }

    double sampleRate = 48000.0;
    std::array<BandpassState, 3> bands;
};

class PitchShifterNode final : public DspNode
{
public:
    PitchShifterNode() : DspNode (NodeKind::pitchShifter, nodeKindName (NodeKind::pitchShifter))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("pitch", "Pitch", "st", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 7.0f, 0.35f);
        addParameter ("window", "Window", "ms", juce::NormalisableRange<float> (20.0f, 120.0f, 0.1f), 55.0f, 0.25f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        engine.prepare (newSampleRate, maximumBlockSize);
    }

    void resetDsp() noexcept override { engine.clear(); }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto semitones = parameterValue (0, inputs, sample);
            const auto windowMs = parameterValue (1, inputs, sample);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto ratio = std::pow (2.0f, juce::jlimit (-24.0f, 24.0f, semitones) / 12.0f);
            const auto windowSamples = windowMs * static_cast<float> (sampleRate * 0.001);
            const auto shifted = engine.processSample (input[sample], ratio, windowSamples);
            output[sample] = input[sample] + mix * (shifted - input[sample]);
        }
    }

    double sampleRate = 48000.0;
    PitchShiftEngine engine;
};

class VocoderNode final : public DspNode
{
public:
    VocoderNode() : DspNode (NodeKind::vocoder, nodeKindName (NodeKind::vocoder))
    {
        addInputPort ("Voice", SignalType::audio);
        addInputPort ("Carrier", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("response", "Response", "ms", skewedRange (2.0f, 200.0f, 20.0f), 20.0f, 0.25f);
        addParameter ("bright", "Bright", "dB", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 3.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

private:
    static constexpr std::size_t bandCount = 12;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        for (std::size_t band = 0; band < bandCount; ++band)
        {
            const auto frequency = 120.0f * std::pow (7500.0f / 120.0f,
                                                      static_cast<float> (band)
                                                          / static_cast<float> (bandCount - 1));
            frequencies[band] = frequency;
            voiceBands[band].setup (frequency, 3.2f, sampleRate);
            carrierBands[band].setup (frequency, 3.2f, sampleRate);
        }
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        for (std::size_t band = 0; band < bandCount; ++band)
        {
            voiceBands[band].clear();
            carrierBands[band].clear();
            envelopes[band] = 0.0f;
        }
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* voice = inputs.getReadPointer (0);
        const auto* carrier = inputs.getReadPointer (1);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto responseMs = parameterValue (0, inputs, sample);
            const auto bright = parameterValue (1, inputs, sample);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto coefficient = static_cast<float> (
                std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (1.0f, responseMs))));

            float wet = 0.0f;
            for (std::size_t band = 0; band < bandCount; ++band)
            {
                const auto analysed = voiceBands[band].processBand (voice[sample]);
                const auto magnitude = std::abs (analysed);
                envelopes[band] = coefficient * envelopes[band] + (1.0f - coefficient) * magnitude;
                if (! std::isfinite (envelopes[band]))
                    envelopes[band] = 0.0f;
                const auto tilt = juce::Decibels::decibelsToGain (
                    bright * (static_cast<float> (band) / static_cast<float> (bandCount - 1) - 0.5f) * 2.0f);
                wet += carrierBands[band].processBand (carrier[sample]) * envelopes[band] * tilt;
            }
            wet *= 4.5f;
            output[sample] = voice[sample] + mix * (wet - voice[sample]);
        }
    }

    double sampleRate = 48000.0;
    std::array<BandpassState, bandCount> voiceBands;
    std::array<BandpassState, bandCount> carrierBands;
    std::array<float, bandCount> frequencies {};
    std::array<float, bandCount> envelopes {};
};

class PitchCorrectorNode final : public DspNode
{
public:
    PitchCorrectorNode() : DspNode (NodeKind::pitchCorrector, nodeKindName (NodeKind::pitchCorrector))
    {
        addInputPort ("Voice", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("speed", "Speed", "ms", skewedRange (1.0f, 250.0f, 20.0f), 15.0f, 0.35f);
        addParameter ("scale", "Scale", "", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f, 0.0f);
        addParameter ("key", "Key", "", juce::NormalisableRange<float> (0.0f, 11.0f, 1.0f), 0.0f, 0.0f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
    }

    juce::String statusText() const override
    {
        const auto frequency = detectedHz.load (std::memory_order_relaxed);
        if (frequency < 40.0f)
            return "listening...";
        return juce::String (frequency, 1) + " Hz detected";
    }

private:
    static constexpr int analysisSize = 1024;   // decimated-by-2 history
    static constexpr int correlationWindow = 256;
    static constexpr int hopSamples = 256;      // full-rate samples between detections
    static constexpr int minimumLag = 28;       // ~860 Hz at 24 kHz
    static constexpr int maximumLag = 400;      // ~60 Hz at 24 kHz

    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        shifter.prepare (newSampleRate, maximumBlockSize);
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        shifter.clear();
        history.fill (0.0f);
        historyIndex = 0;
        hopCounter = 0;
        decimationToggle = false;
        smoothedRatio = 1.0f;
        targetRatio = 1.0f;
        detectedHz.store (0.0f, std::memory_order_relaxed);
    }

    void detectPitch (int scale, int key) noexcept
    {
        // Normalized autocorrelation over the decimated history.
        const auto decimatedRate = sampleRate * 0.5;
        float bestScore = 0.0f;
        int bestLag = 0;
        float energy = 1.0e-9f;
        const auto start = historyIndex; // oldest sample position in ring
        auto sampleAt = [this, start] (int index) noexcept
        {
            return history[static_cast<std::size_t> ((start + index) % analysisSize)];
        };
        for (int index = 0; index < correlationWindow; ++index)
            energy += sampleAt (analysisSize - correlationWindow - maximumLag + index)
                    * sampleAt (analysisSize - correlationWindow - maximumLag + index);

        for (int lag = minimumLag; lag < maximumLag; ++lag)
        {
            float correlation = 0.0f;
            for (int index = 0; index < correlationWindow; ++index)
            {
                const auto base = analysisSize - correlationWindow - maximumLag + index;
                correlation += sampleAt (base) * sampleAt (base + lag);
            }
            if (correlation > bestScore)
            {
                bestScore = correlation;
                bestLag = lag;
            }
        }

        if (bestLag == 0 || bestScore / energy < 0.25f)
        {
            targetRatio = 1.0f;
            detectedHz.store (0.0f, std::memory_order_relaxed);
            return;
        }

        const auto frequency = static_cast<float> (decimatedRate / bestLag);
        detectedHz.store (frequency, std::memory_order_relaxed);

        // Snap to the nearest allowed semitone.
        const auto midi = 69.0f + 12.0f * std::log2 (frequency / 440.0f);
        auto nearest = juce::roundToInt (midi);
        if (scale != 0)
        {
            static constexpr int majorScale[7] { 0, 2, 4, 5, 7, 9, 11 };
            static constexpr int minorScale[7] { 0, 2, 3, 5, 7, 8, 10 };
            const auto* degrees = scale == 1 ? majorScale : minorScale;
            int bestNote = nearest;
            float bestDistance = 1.0e9f;
            for (int octave = -1; octave <= 1; ++octave)
                for (int degree = 0; degree < 7; ++degree)
                {
                    const auto note = (nearest / 12 + octave) * 12 + ((degrees[degree] + key) % 12);
                    const auto distance = std::abs (midi - static_cast<float> (note));
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestNote = note;
                    }
                }
            nearest = bestNote;
        }
        const auto targetHz = 440.0f * std::pow (2.0f, (static_cast<float> (nearest) - 69.0f) / 12.0f);
        targetRatio = juce::jlimit (0.5f, 2.0f, targetHz / frequency);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto speedMs = parameterValue (0, inputs, sample);
            const auto scale = juce::roundToInt (parameterValue (1, inputs, sample));
            const auto key = juce::roundToInt (parameterValue (2, inputs, sample));
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);

            // Decimate by two into the analysis ring.
            decimationToggle = ! decimationToggle;
            if (decimationToggle)
            {
                history[static_cast<std::size_t> (historyIndex)] =
                    std::isfinite (input[sample]) ? input[sample] : 0.0f;
                historyIndex = (historyIndex + 1) % analysisSize;
            }
            if (++hopCounter >= hopSamples)
            {
                hopCounter = 0;
                detectPitch (scale, key);
            }

            const auto coefficient = static_cast<float> (
                std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (1.0f, speedMs))));
            smoothedRatio = coefficient * smoothedRatio + (1.0f - coefficient) * targetRatio;
            if (! std::isfinite (smoothedRatio))
                smoothedRatio = 1.0f;

            const auto windowSamples = 40.0f * static_cast<float> (sampleRate * 0.001);
            const auto corrected = shifter.processSample (input[sample], smoothedRatio, windowSamples);
            output[sample] = input[sample] + mix * (corrected - input[sample]);
        }
    }

    double sampleRate = 48000.0;
    PitchShiftEngine shifter;
    std::array<float, analysisSize> history {};
    int historyIndex = 0;
    int hopCounter = 0;
    bool decimationToggle = false;
    float smoothedRatio = 1.0f;
    float targetRatio = 1.0f;
    std::atomic<float> detectedHz { 0.0f };
};

class GranularNode final : public DspNode
{
public:
    GranularNode() : DspNode (NodeKind::granular, nodeKindName (NodeKind::granular))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("position", "Position", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, 0.5f);
        addParameter ("size", "Size", "ms", skewedRange (20.0f, 400.0f, 90.0f), 90.0f, 0.35f);
        addParameter ("density", "Density", "Hz", skewedRange (0.5f, 40.0f, 8.0f), 8.0f, 0.35f);
        addParameter ("pitch", "Pitch", "st", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, 0.35f);
        addParameter ("spread", "Spread", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, 0.5f);
        addParameter ("feedback", "Feedback", "%", juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 0.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, 0.5f);
    }

private:
    static constexpr std::size_t grainCount = 8;

    struct Grain
    {
        bool active = false;
        double position = 0.0; // read start in buffer samples
        double phase = 0.0;    // samples into the grain
        double rate = 1.0;
        double length = 4800.0;
    };

    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        buffer.assign (static_cast<std::size_t> (std::ceil (sampleRate * 4.0))
                           + static_cast<std::size_t> (maximumBlockSize) + 4u, 0.0f);
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePosition = 0;
        spawnAccumulator = 0.0;
        previousOutput = 0.0f;
        for (auto& grain : grains)
            grain.active = false;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto size = static_cast<int> (buffer.size());
        if (size < 64)
            return;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto position = juce::jlimit (0.0f, 1.0f, parameterValue (0, inputs, sample) * 0.01f);
            const auto sizeMs = parameterValue (1, inputs, sample);
            const auto density = parameterValue (2, inputs, sample);
            const auto pitch = parameterValue (3, inputs, sample);
            const auto spread = juce::jlimit (0.0f, 1.0f, parameterValue (4, inputs, sample) * 0.01f);
            const auto feedback = juce::jlimit (0.0f, 0.9f, parameterValue (5, inputs, sample) * 0.01f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (6, inputs, sample) * 0.01f);

            const auto safeInput = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            buffer[static_cast<std::size_t> (writePosition)] =
                std::tanh (safeInput + previousOutput * feedback);

            spawnAccumulator += density / sampleRate;
            if (spawnAccumulator >= 1.0)
            {
                spawnAccumulator -= 1.0;
                for (auto& grain : grains)
                {
                    if (grain.active)
                        continue;
                    const auto jitter = (random.nextFloat() * 2.0f - 1.0f) * spread;
                    const auto back = juce::jlimit (0.05, 0.95,
                                                    static_cast<double> (position) + jitter * 0.4);
                    grain.position = static_cast<double> (writePosition)
                                   - back * (size - 8);
                    while (grain.position < 0.0)
                        grain.position += size;
                    grain.rate = std::pow (2.0, static_cast<double> (juce::jlimit (-24.0f, 24.0f, pitch)) / 12.0);
                    grain.length = juce::jmax (32.0, static_cast<double> (sizeMs) * sampleRate * 0.001);
                    grain.phase = 0.0;
                    grain.active = true;
                    break;
                }
            }

            float wet = 0.0f;
            for (auto& grain : grains)
            {
                if (! grain.active)
                    continue;
                auto readPosition = grain.position + grain.phase * grain.rate;
                while (readPosition >= size)
                    readPosition -= size;
                const auto indexA = static_cast<int> (readPosition) % size;
                const auto indexB = (indexA + 1) % size;
                const auto fraction = static_cast<float> (readPosition - std::floor (readPosition));
                const auto grainSample = buffer[static_cast<std::size_t> (indexA)]
                                       + fraction * (buffer[static_cast<std::size_t> (indexB)]
                                                     - buffer[static_cast<std::size_t> (indexA)]);
                const auto progress = static_cast<float> (grain.phase / grain.length);
                const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * progress);
                wet += grainSample * window * 0.6f;
                grain.phase += 1.0;
                if (grain.phase >= grain.length)
                    grain.active = false;
            }

            previousOutput = std::isfinite (wet) ? wet : 0.0f;
            output[sample] = safeInput + mix * (wet - safeInput);
            writePosition = (writePosition + 1) % size;
        }
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    std::array<Grain, grainCount> grains;
    int writePosition = 0;
    double spawnAccumulator = 0.0;
    float previousOutput = 0.0f;
    juce::Random random;
};
class MonoSynthNode final : public DspNode
{
public:
    MonoSynthNode() : DspNode (NodeKind::monoSynth, nodeKindName (NodeKind::monoSynth))
    {
        addInputPort ("Gate", SignalType::control);
        gatePort = getNumInputPorts() - 1;
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("wave", "Wave", "", juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f, 0.0f);
        addParameter ("pitch", "Pitch", "st", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, 0.5f);
        addParameter ("glide", "Glide", "ms", skewedRange (1.0f, 500.0f, 40.0f), 10.0f, 0.25f);
        addParameter ("cutoff", "Cutoff", "Hz", skewedRange (40.0f, 18000.0f, 1200.0f), 2500.0f, 0.4f);
        addParameter ("resonance", "Resonance", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f, 0.35f);
        addParameter ("attack", "Attack", "ms", skewedRange (0.5f, 500.0f, 20.0f), 4.0f, 0.25f);
        addParameter ("decay", "Decay", "ms", skewedRange (5.0f, 2000.0f, 200.0f), 250.0f, 0.25f);
        addParameter ("hold", "Hold", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.5f);
        // Exact pitch from outside (MIDI Note, a sequencer): 0..1 = four octaves, 48 st per unit,
        // added to the Pitch knob. Added last so older cables keep their port indices.
        addInputPort ("Pitch", SignalType::control);
        pitchPort = getNumInputPorts() - 1;
    }
    int pitchPort = -1;

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        phase = 0.0;
        smoothedFrequency = 110.0f;
        envelope = 0.0f;
        gateWasOpen = false;
        attacking = false;
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        const auto* gateInput = gatePort < inputs.getNumChannels()
                                    ? inputs.getReadPointer (gatePort) : nullptr;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto wave = juce::roundToInt (parameterValue (0, inputs, sample));
            auto pitch = parameterValue (1, inputs, sample);
            if (pitchPort >= 0 && inputs.getNumChannels() > pitchPort)
            {
                const auto external = inputs.getSample (pitchPort, sample);
                if (std::isfinite (external))
                    pitch += external * 48.0f;
            }
            const auto glideMs = parameterValue (2, inputs, sample);
            const auto cutoff = parameterValue (3, inputs, sample);
            const auto resonance = juce::jlimit (0.0f, 1.0f, parameterValue (4, inputs, sample) * 0.01f);
            const auto attackMs = parameterValue (5, inputs, sample);
            const auto decayMs = parameterValue (6, inputs, sample);
            const auto hold = juce::jlimit (0.0f, 1.0f, parameterValue (7, inputs, sample) * 0.01f);

            const auto targetFrequency = 110.0f * std::pow (2.0f, juce::jlimit (-36.0f, 36.0f, pitch) / 12.0f);
            const auto glideCoefficient = static_cast<float> (
                std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (1.0f, glideMs))));
            smoothedFrequency = glideCoefficient * smoothedFrequency
                              + (1.0f - glideCoefficient) * targetFrequency;

            const bool gateOpen = gateInput != nullptr && gateInput[sample] > 0.5f;
            if (gateOpen && ! gateWasOpen)
                attacking = true;
            gateWasOpen = gateOpen;

            if (attacking)
            {
                const auto attackStep = static_cast<float> (1.0 / (sampleRate * 0.001 * juce::jmax (0.5f, attackMs)));
                envelope += attackStep;
                if (envelope >= 1.0f)
                {
                    envelope = 1.0f;
                    attacking = false;
                }
            }
            else
            {
                const auto decayCoefficient = static_cast<float> (
                    std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (1.0f, decayMs))));
                // With no gate patched the voice drones at the HOLD level;
                // squared so the knob feels roughly perceptual.
                const auto target = gateOpen ? 1.0f : hold * hold;
                envelope = decayCoefficient * envelope + (1.0f - decayCoefficient) * target;
            }
            const auto level = envelope;

            float oscillator = 0.0f;
            const auto p = static_cast<float> (phase);
            switch (wave)
            {
                case 1:  oscillator = p < 0.5f ? 1.0f : -1.0f; break;                   // square
                case 2:  oscillator = p < 0.25f ? 1.0f : -1.0f / 3.0f; break;           // pulse
                case 3:  oscillator = std::sin (juce::MathConstants<float>::twoPi * p); break;
                default: oscillator = 2.0f * p - 1.0f; break;                           // saw
            }

            // TPT low-pass for the classic subtractive voice.
            const auto g = std::tan (juce::MathConstants<float>::pi
                                     * juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), cutoff)
                                     / static_cast<float> (sampleRate));
            const auto k = 2.0f - 1.85f * resonance;
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto a2 = g * a1;
            const auto v3 = oscillator - ic2eq;
            const auto v1 = a1 * ic1eq + a2 * v3;
            const auto v2 = ic2eq + a2 * ic1eq + g * a2 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            if (! std::isfinite (ic1eq) || ! std::isfinite (ic2eq))
            {
                ic1eq = 0.0f;
                ic2eq = 0.0f;
            }

            output[sample] = v2 * level * 0.6f;
            phase += smoothedFrequency / sampleRate;
            phase -= std::floor (phase);
        }
    }

    int gatePort = 0;
    double sampleRate = 48000.0;
    double phase = 0.0;
    float smoothedFrequency = 110.0f;
    float envelope = 0.0f;
    bool gateWasOpen = false;
    bool attacking = false;
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

class NoiseSourceNode final : public DspNode
{
public:
    NoiseSourceNode() : DspNode (NodeKind::noiseSource, nodeKindName (NodeKind::noiseSource))
    {
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("colour", "Colour", "", juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 0.0f, 0.5f);
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -12.0f, 0.25f);
    }

private:
    void prepareDsp (double, int) override { resetDsp(); }

    void resetDsp() noexcept override
    {
        pink0 = pink1 = pink2 = 0.0f;
        brown = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto colour = juce::jlimit (0.0f, 2.0f, parameterValue (0, inputs, sample));
            const auto level = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            const auto white = random.nextFloat() * 2.0f - 1.0f;

            // Paul Kellet pink approximation.
            pink0 = 0.99765f * pink0 + white * 0.0990460f;
            pink1 = 0.96300f * pink1 + white * 0.2965164f;
            pink2 = 0.57000f * pink2 + white * 1.0526913f;
            const auto pink = (pink0 + pink1 + pink2 + white * 0.1848f) * 0.28f;

            brown = juce::jlimit (-1.0f, 1.0f, brown * 0.997f + white * 0.025f);

            float mixed;
            if (colour <= 1.0f)
                mixed = white + colour * (pink - white);
            else
                mixed = pink + (colour - 1.0f) * (brown * 3.0f - pink);
            output[sample] = mixed * level;
        }
    }

    juce::Random random;
    float pink0 = 0.0f, pink1 = 0.0f, pink2 = 0.0f;
    float brown = 0.0f;
};

class PluckNode final : public DspNode
{
public:
    PluckNode() : DspNode (NodeKind::pluck, nodeKindName (NodeKind::pluck))
    {
        addInputPort ("Trigger", SignalType::control);
        triggerPort = getNumInputPorts() - 1;
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("pitch", "Pitch", "st", juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f), 0.0f, 0.5f);
        addParameter ("damp", "Damp", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, 0.35f);
        addParameter ("bright", "Bright", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, 0.35f);
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), -3.0f, 0.25f);
        addInputPort ("Pitch", SignalType::control); // 0..1 = four octaves, 48 st per unit, on top of the Pitch knob
        pitchPort = getNumInputPorts() - 1;
    }
    int pitchPort = -1;

private:
    static constexpr int maximumDelay = 4096;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        delayLine.fill (0.0f);
        readIndex = 0;
        activeLength = 480;
        excitationFilter = 0.0f;
        triggerWasHigh = false;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        const auto* trigger = triggerPort < inputs.getNumChannels()
                                  ? inputs.getReadPointer (triggerPort) : nullptr;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            auto pitch = parameterValue (0, inputs, sample);
            if (pitchPort >= 0 && inputs.getNumChannels() > pitchPort)
            {
                const auto external = inputs.getSample (pitchPort, sample);
                if (std::isfinite (external))
                    pitch += external * 48.0f;
            }
            const auto damp = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto bright = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto level = juce::Decibels::decibelsToGain (parameterValue (3, inputs, sample));

            const bool triggerHigh = trigger != nullptr && trigger[sample] > 0.5f;
            if (triggerHigh && ! triggerWasHigh)
            {
                const auto frequency = 110.0f * std::pow (2.0f, juce::jlimit (-24.0f, 36.0f, pitch) / 12.0f);
                activeLength = juce::jlimit (16, maximumDelay - 2,
                                             juce::roundToInt (sampleRate / frequency));
                // Excite with brightness-filtered noise.
                float filterState = 0.0f;
                for (int index = 0; index < activeLength; ++index)
                {
                    const auto noise = random.nextFloat() * 2.0f - 1.0f;
                    filterState += (0.05f + bright * 0.9f) * (noise - filterState);
                    delayLine[static_cast<std::size_t> (index)] = filterState;
                }
                readIndex = 0;
            }
            triggerWasHigh = triggerHigh;

            const auto nextIndex = (readIndex + 1) % activeLength;
            const auto current = delayLine[static_cast<std::size_t> (readIndex)];
            const auto next = delayLine[static_cast<std::size_t> (nextIndex)];
            const auto feedback = 0.9995f - damp * 0.01f;
            auto value = 0.5f * (current + next) * feedback;
            if (! std::isfinite (value))
                value = 0.0f;
            delayLine[static_cast<std::size_t> (readIndex)] = value;
            readIndex = nextIndex;
            output[sample] = current * level;
        }
    }

    int triggerPort = 0;
    double sampleRate = 48000.0;
    std::array<float, maximumDelay> delayLine {};
    int readIndex = 0;
    int activeLength = 480;
    float excitationFilter = 0.0f;
    bool triggerWasHigh = false;
    juce::Random random;
};

class DrumMachineNode final : public DspNode
{
public:
    DrumMachineNode() : DspNode (NodeKind::drumMachine, nodeKindName (NodeKind::drumMachine))
    {
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("bpm", "Tempo", "bpm", juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f, 0.25f);
        addParameter ("swing", "Swing", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, 0.35f);
        addParameter ("kick-level", "Kick", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -3.0f, 0.25f);
        addParameter ("snare-level", "Snare", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -6.0f, 0.25f);
        addParameter ("hat-level", "Hat", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -10.0f, 0.25f);
        static constexpr float kickDefaults[8] { 1, 0, 0, 0, 1, 0, 0, 0 };
        static constexpr float snareDefaults[8] { 0, 0, 1, 0, 0, 0, 1, 0 };
        static constexpr float hatDefaults[8] { 1, 1, 1, 1, 1, 1, 1, 1 };
        // Grid steps are toggled in the preview; a mod socket per step would
        // add 24 useless ports to the panel.
        for (int step = 0; step < 8; ++step)
            addParameter ("k" + juce::String (step + 1), "K" + juce::String (step + 1), "",
                          juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), kickDefaults[step], 0.0f, false);
        for (int step = 0; step < 8; ++step)
            addParameter ("s" + juce::String (step + 1), "S" + juce::String (step + 1), "",
                          juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), snareDefaults[step], 0.0f, false);
        for (int step = 0; step < 8; ++step)
            addParameter ("h" + juce::String (step + 1), "H" + juce::String (step + 1), "",
                          juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), hatDefaults[step], 0.0f, false);
        // Last input port so older cables keep their indices: a Clock's
        // pulses step the grid instead of the internal tempo while they arrive.
        addInputPort ("Clock", SignalType::control);
        clock.port = getNumInputPorts() - 1;
    }

    int currentStep() const noexcept override
    {
        return activeStep.load (std::memory_order_relaxed);
    }

    /** "tap": tap tempo from a button or footswitch (message thread). Up to
        four intervals are averaged; a pause over two seconds starts over. */
    bool handleUiCommand (const juce::String& command) override
    {
        if (command != "tap")
            return false;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (tapCount > 0 && now - tapTimes[static_cast<std::size_t> ((tapCount - 1) % tapTimes.size())] > 2000.0)
            tapCount = 0;
        tapTimes[static_cast<std::size_t> (tapCount % tapTimes.size())] = now;
        ++tapCount;
        const auto samples = juce::jmin (tapCount, static_cast<int> (tapTimes.size()));
        if (samples < 2)
            return true;
        // Oldest kept tap to the newest, over the intervals between them.
        const auto newest = tapTimes[static_cast<std::size_t> ((tapCount - 1) % tapTimes.size())];
        const auto oldest = tapTimes[static_cast<std::size_t> ((tapCount - samples) % tapTimes.size())];
        const auto interval = (newest - oldest) / static_cast<double> (samples - 1);
        if (interval > 0.0)
            getParameter (0).setValue (static_cast<float> (60000.0 / interval));
        return true;
    }

private:
    std::array<double, 4> tapTimes {};
    int tapCount = 0;
    ClockFollower clock;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        stepSamplesLeft = 0.0;
        stepIndex = 7;
        clock.reset();
        kickPhase = 0.0;
        kickEnvelope = 0.0f;
        snareEnvelope = 0.0f;
        snareTonePhase = 0.0;
        hatEnvelope = 0.0f;
        hatHighpassState = 0.0f;
        activeStep.store (0, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        const auto* clockIn = clock.pointer (inputs);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto bpm = parameterValue (0, inputs, sample);
            const auto swing = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto kickLevel = juce::Decibels::decibelsToGain (parameterValue (2, inputs, sample));
            const auto snareLevel = juce::Decibels::decibelsToGain (parameterValue (3, inputs, sample));
            const auto hatLevel = juce::Decibels::decibelsToGain (parameterValue (4, inputs, sample));

            const bool clockEdge = clock.tick (clockIn, sample, sampleRate);
            const bool external = clock.external (sampleRate);
            if (external ? clockEdge : stepSamplesLeft <= 0.0)
            {
                stepIndex = (stepIndex + 1) % 8;
                activeStep.store (stepIndex, std::memory_order_relaxed);
                if (external)
                    stepSamplesLeft = 0.0; // the next internal step is due the moment the clock goes away
                else
                {
                    const auto baseSamples = sampleRate * 60.0 / juce::jmax (40.0f, bpm) * 0.5; // eighths
                    const auto swung = stepIndex % 2 == 0 ? 1.0 + swing * 0.5 : 1.0 - swing * 0.5;
                    stepSamplesLeft += baseSamples * swung;
                }

                if (getParameter (5 + stepIndex).getValue() > 0.5f)
                {
                    kickPhase = 0.0;
                    kickEnvelope = 1.0f;
                }
                if (getParameter (13 + stepIndex).getValue() > 0.5f)
                {
                    snareEnvelope = 1.0f;
                    snareTonePhase = 0.0;
                }
                if (getParameter (21 + stepIndex).getValue() > 0.5f)
                    hatEnvelope = 1.0f;
            }
            if (! external)
                stepSamplesLeft -= 1.0;

            // Kick: exponentially falling sine sweep.
            const auto kickFrequency = 45.0 + 110.0 * kickEnvelope * kickEnvelope;
            kickPhase += kickFrequency / sampleRate;
            kickPhase -= std::floor (kickPhase);
            const auto kick = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * kickPhase))
                            * kickEnvelope;
            kickEnvelope *= static_cast<float> (std::exp (-1.0 / (sampleRate * 0.20)));

            // Snare: noise burst plus a 190 Hz body.
            snareTonePhase += 190.0 / sampleRate;
            snareTonePhase -= std::floor (snareTonePhase);
            const auto snareNoise = (random.nextFloat() * 2.0f - 1.0f) * snareEnvelope;
            const auto snareBody = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * snareTonePhase))
                                 * snareEnvelope * 0.5f;
            const auto snare = snareNoise * 0.7f + snareBody;
            snareEnvelope *= static_cast<float> (std::exp (-1.0 / (sampleRate * 0.09)));

            // Hat: high-passed noise, very fast decay.
            const auto hatNoise = random.nextFloat() * 2.0f - 1.0f;
            const auto highpassed = hatNoise - hatHighpassState;
            hatHighpassState += 0.35f * highpassed;
            const auto hat = highpassed * hatEnvelope;
            hatEnvelope *= static_cast<float> (std::exp (-1.0 / (sampleRate * 0.035)));

            output[sample] = kick * kickLevel + snare * snareLevel + hat * hatLevel;
        }
    }

    double sampleRate = 48000.0;
    double stepSamplesLeft = 0.0;
    int stepIndex = 7;
    double kickPhase = 0.0;
    float kickEnvelope = 0.0f;
    float snareEnvelope = 0.0f;
    double snareTonePhase = 0.0;
    float hatEnvelope = 0.0f;
    float hatHighpassState = 0.0f;
    std::atomic<int> activeStep { 0 };
    juce::Random random;
};

class SamplerNode final : public DspNode
{
public:
    SamplerNode() : DspNode (NodeKind::sampler, nodeKindName (NodeKind::sampler))
    {
        addInputPort ("Audio", SignalType::audio);
        addInputPort ("Trigger", SignalType::control);
        triggerPort = getNumInputPorts() - 1;
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("pitch", "Pitch", "st", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, 0.5f);
        addParameter ("start", "Start", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, 0.5f);
        addParameter ("length", "Length", "%", juce::NormalisableRange<float> (1.0f, 100.0f, 0.1f), 100.0f, 0.5f);
        addParameter ("loop", "Loop", "", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f, 0.0f);
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, 0.25f);
    }

    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "rec")
        {
            sampleContentVersion.fetch_add (1, std::memory_order_relaxed);
            if (recording.load (std::memory_order_relaxed))
                stopRecordRequest.store (true, std::memory_order_release);
            else
                startRecordRequest.store (true, std::memory_order_release);
            return true;
        }
        if (command == "play")
        {
            if (playing.load (std::memory_order_relaxed))
                stopPlayRequest.store (true, std::memory_order_release);
            else
                startPlayRequest.store (true, std::memory_order_release);
            return true;
        }
        if (command == "clear")
        {
            clearRequest.store (true, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool uiToggleState (const juce::String& command) const override
    {
        if (command == "rec")
            return recording.load (std::memory_order_relaxed);
        if (command == "play")
            return playing.load (std::memory_order_relaxed);
        return false;
    }

    juce::String statusText() const override
    {
        const auto seconds = static_cast<float> (recordedLength.load (std::memory_order_relaxed))
                           / static_cast<float> (juce::jmax (1.0, sampleRate));
        if (recording.load (std::memory_order_relaxed))
            return "REC " + juce::String (seconds, 1) + "s";
        if (seconds < 0.01f)
            return "empty - press REC";
        return juce::String (seconds, 1) + "s sample"
             + (playing.load (std::memory_order_relaxed) ? " - playing" : "");
    }

    bool hasAudioContent() const noexcept override { return recordedLength.load (std::memory_order_acquire) > 0; }
    juce::uint32 audioContentVersion() const noexcept override { return sampleContentVersion.load (std::memory_order_relaxed); }

    juce::AudioBuffer<float> exportAudioContent() const override
    {
        const auto len = juce::jmin (recordedLength.load (std::memory_order_acquire), static_cast<int> (buffer.size()));
        juce::AudioBuffer<float> out (1, juce::jmax (0, len));
        for (int i = 0; i < len; ++i)
            out.setSample (0, i, buffer[static_cast<std::size_t> (i)]);
        return out;
    }

    void importAudioContent (const juce::AudioBuffer<float>& audio) override
    {
        if (audio.getNumChannels() == 0)
            return;
        const auto len = juce::jmin (audio.getNumSamples(), static_cast<int> (buffer.size()));
        for (int i = 0; i < len; ++i)
            buffer[static_cast<std::size_t> (i)] = audio.getSample (0, i);
        recordedLength.store (len, std::memory_order_release);
        sampleContentVersion.fetch_add (1, std::memory_order_relaxed);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        // Re-allocate only when the capacity actually changes so a device
        // restart does not erase the take.
        const auto capacity = static_cast<std::size_t> (std::ceil (newSampleRate * 10.0)) + 4u;
        if (capacity != buffer.size())
        {
            buffer.assign (capacity, 0.0f);
            recordedLength.store (0, std::memory_order_relaxed);
        }
        sampleRate = newSampleRate;
    }

    void resetDsp() noexcept override
    {
        playing.store (false, std::memory_order_relaxed);
        recording.store (false, std::memory_order_relaxed);
        playhead = 0.0;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        const auto* trigger = triggerPort < inputs.getNumChannels()
                                  ? inputs.getReadPointer (triggerPort) : nullptr;
        auto* output = outputs.getWritePointer (0);
        const auto capacity = static_cast<int> (buffer.size());
        if (capacity < 64)
            return;

        if (clearRequest.exchange (false, std::memory_order_acq_rel))
        {
            recordedLength.store (0, std::memory_order_relaxed);
            playing.store (false, std::memory_order_relaxed);
            recording.store (false, std::memory_order_relaxed);
        }
        if (startRecordRequest.exchange (false, std::memory_order_acq_rel))
        {
            writeIndex = 0;
            recordedLength.store (0, std::memory_order_relaxed);
            recording.store (true, std::memory_order_relaxed);
            playing.store (false, std::memory_order_relaxed);
        }
        if (stopRecordRequest.exchange (false, std::memory_order_acq_rel)
            && recording.load (std::memory_order_relaxed))
        {
            recording.store (false, std::memory_order_relaxed);
            recordedLength.store (writeIndex, std::memory_order_relaxed);
        }
        if (startPlayRequest.exchange (false, std::memory_order_acq_rel))
        {
            playing.store (true, std::memory_order_relaxed);
            playheadToStart = true;
        }
        if (stopPlayRequest.exchange (false, std::memory_order_acq_rel))
            playing.store (false, std::memory_order_relaxed);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto pitch = parameterValue (0, inputs, sample);
            const auto startFraction = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto lengthFraction = juce::jlimit (0.01f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto loop = juce::roundToInt (parameterValue (3, inputs, sample)) == 1;
            const auto level = juce::Decibels::decibelsToGain (parameterValue (4, inputs, sample));

            if (recording.load (std::memory_order_relaxed))
            {
                buffer[static_cast<std::size_t> (writeIndex)] =
                    std::isfinite (input[sample]) ? input[sample] : 0.0f;
                if (++writeIndex >= capacity)
                {
                    recording.store (false, std::memory_order_relaxed);
                    recordedLength.store (capacity, std::memory_order_relaxed);
                }
            }

            const auto recorded = recordedLength.load (std::memory_order_relaxed);
            const auto regionStart = static_cast<double> (startFraction) * recorded;
            const auto regionLength = juce::jmax (32.0, static_cast<double> (lengthFraction)
                                                            * (recorded - regionStart));

            const bool triggerHigh = trigger != nullptr && trigger[sample] > 0.5f;
            if ((triggerHigh && ! triggerWasHigh) || playheadToStart)
            {
                playhead = regionStart;
                playheadToStart = false;
                if (triggerHigh && recorded > 64)
                    playing.store (true, std::memory_order_relaxed);
            }
            triggerWasHigh = triggerHigh;

            float voice = 0.0f;
            if (playing.load (std::memory_order_relaxed) && recorded > 64)
            {
                const auto indexA = juce::jlimit (0, recorded - 2, static_cast<int> (playhead));
                const auto fraction = static_cast<float> (playhead - std::floor (playhead));
                voice = buffer[static_cast<std::size_t> (indexA)]
                      + fraction * (buffer[static_cast<std::size_t> (indexA + 1)]
                                    - buffer[static_cast<std::size_t> (indexA)]);
                playhead += std::pow (2.0, static_cast<double> (juce::jlimit (-24.0f, 24.0f, pitch)) / 12.0);
                if (playhead >= regionStart + regionLength || playhead >= recorded - 1)
                {
                    if (loop)
                        playhead = regionStart;
                    else
                        playing.store (false, std::memory_order_relaxed);
                }
            }
            output[sample] = voice * level;
        }
    }

    int triggerPort = 0;
    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;
    double playhead = 0.0;
    bool playheadToStart = false;
    bool triggerWasHigh = false;
    std::atomic<int> recordedLength { 0 };
    std::atomic<juce::uint32> sampleContentVersion { 0 };
    std::atomic<bool> recording { false };
    std::atomic<bool> playing { false };
    std::atomic<bool> startRecordRequest { false };
    std::atomic<bool> stopRecordRequest { false };
    std::atomic<bool> startPlayRequest { false };
    std::atomic<bool> stopPlayRequest { false };
    std::atomic<bool> clearRequest { false };
};

class FourTrackNode final : public DspNode
{
public:
    FourTrackNode() : DspNode (NodeKind::fourTrack, nodeKindName (NodeKind::fourTrack))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Mix", SignalType::audio);
        for (int track = 0; track < trackCount; ++track)
            addParameter ("level-" + juce::String (track + 1), "Track " + juce::String (track + 1), "dB",
                          juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("master", "Master", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("speed", "Speed", "%", juce::NormalisableRange<float> (50.0f, 200.0f, 0.1f), 100.0f, 0.35f);
        // Per-track speed came later, so it sits after the older knobs (saved patches store values by index).
        for (int track = 0; track < trackCount; ++track)
            addParameter ("speed-" + juce::String (track + 1), "Speed " + juce::String (track + 1), "%",
                          juce::NormalisableRange<float> (25.0f, 400.0f, 0.1f), 100.0f, 0.35f);
        for (auto& running : trackRunning)
            running.store (true, std::memory_order_relaxed);
    }

    // Transport: PLAY runs the machine, REC writes the armed tracks, RTZ rewinds
    // every track, SYNC locks the four playheads to track 1 (free-running with
    // their own speed when off). Per track: R (arm) and P (that track runs).
    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "play")
        {
            playing.store (! playing.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;
        }
        if (command == "rec")
        {
            recording.store (! recording.load (std::memory_order_relaxed), std::memory_order_relaxed);
            recordedContentVersion.fetch_add (1, std::memory_order_relaxed); // a take started or ended
            return true;
        }
        if (command == "rtz")
        {
            returnToZero.store (true, std::memory_order_release);
            return true;
        }
        if (command == "sync")
        {
            synced.store (! synced.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;
        }
        for (int track = 0; track < trackCount; ++track)
        {
            const auto index = static_cast<std::size_t> (track);
            if (command == "arm" + juce::String (track + 1))
            {
                armed[index].store (! armed[index].load (std::memory_order_relaxed), std::memory_order_relaxed);
                return true;
            }
            if (command == "play" + juce::String (track + 1))
            {
                trackRunning[index].store (! trackRunning[index].load (std::memory_order_relaxed), std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    bool uiToggleState (const juce::String& command) const override
    {
        if (command == "play") return playing.load (std::memory_order_relaxed);
        if (command == "rec")  return recording.load (std::memory_order_relaxed);
        if (command == "sync") return synced.load (std::memory_order_relaxed);
        for (int track = 0; track < trackCount; ++track)
        {
            const auto index = static_cast<std::size_t> (track);
            if (command == "arm" + juce::String (track + 1))  return armed[index].load (std::memory_order_relaxed);
            if (command == "play" + juce::String (track + 1)) return trackRunning[index].load (std::memory_order_relaxed);
        }
        return false;
    }

    juce::String statusText() const override
    {
        const auto seconds = playheadSeconds[0].load (std::memory_order_relaxed);
        juce::String state = playing.load (std::memory_order_relaxed)
            ? (recording.load (std::memory_order_relaxed) ? "REC" : "PLAY") : "STOP";
        return state + "  " + juce::String (seconds, 1) + "s / " + juce::String (tapeSeconds) + "s" + (synced.load (std::memory_order_relaxed) ? "  SYNC" : "  FREE");
    }

    /** Lane position 0..1 while the track runs; an idle track reports -1 - position so the reels still show where it stands. */
    float lanePosition (int lane) const noexcept override
    {
        if (! juce::isPositiveAndBelow (lane, trackCount))
            return -1.0f;
        const auto index = static_cast<std::size_t> (lane);
        const auto position = juce::jlimit (0.0f, 1.0f, playheadSeconds[index].load (std::memory_order_relaxed) / static_cast<float> (tapeSeconds));
        if (! playing.load (std::memory_order_relaxed) || ! trackRunning[index].load (std::memory_order_relaxed))
            return -1.0f - position;
        return position;
    }

    // Recording persistence: the four tapes as four channels, trimmed to the
    // last audible sample so an empty tape costs nothing on disk.
    bool hasAudioContent() const noexcept override { return recordedContentVersion.load (std::memory_order_relaxed) > 0 && contentLength() > 0; }
    juce::uint32 audioContentVersion() const noexcept override { return recordedContentVersion.load (std::memory_order_relaxed) + takesEnded.load (std::memory_order_relaxed); }

    juce::AudioBuffer<float> exportAudioContent() const override
    {
        const auto len = contentLength();
        juce::AudioBuffer<float> out (trackCount, juce::jmax (0, len));
        for (int track = 0; track < trackCount; ++track)
            for (int i = 0; i < len; ++i)
                out.setSample (track, i, tracks[static_cast<std::size_t> (track)][static_cast<std::size_t> (i)]);
        return out;
    }

    void importAudioContent (const juce::AudioBuffer<float>& audio) override
    {
        for (int track = 0; track < trackCount; ++track)
        {
            auto& tape = tracks[static_cast<std::size_t> (track)];
            std::fill (tape.begin(), tape.end(), 0.0f);
            if (track >= audio.getNumChannels())
                continue;
            const auto len = juce::jmin (audio.getNumSamples(), static_cast<int> (tape.size()));
            for (int i = 0; i < len; ++i)
                tape[static_cast<std::size_t> (i)] = audio.getSample (track, i);
        }
        recordedContentVersion.fetch_add (1, std::memory_order_relaxed);
    }

private:
    static constexpr int trackCount = 4;
    static constexpr int tapeSeconds = 60;

    int contentLength() const noexcept
    {
        int last = 0;
        for (const auto& tape : tracks)
            for (int i = static_cast<int> (tape.size()) - 1; i >= last; --i)
                if (std::abs (tape[static_cast<std::size_t> (i)]) > 1.0e-6f)
                {
                    last = i + 1;
                    break;
                }
        return last;
    }

    void prepareDsp (double newSampleRate, int) override
    {
        const auto capacity = static_cast<std::size_t> (std::ceil (newSampleRate * tapeSeconds)) + 4u;
        for (auto& track : tracks)
            if (track.size() != capacity)
                track.assign (capacity, 0.0f);
        sampleRate = newSampleRate;
    }

    void resetDsp() noexcept override
    {
        playing.store (false, std::memory_order_relaxed);
        recording.store (false, std::memory_order_relaxed);
        for (auto& head : playhead) head = 0.0;
        transport = 0.0;
        wroteLastBlock = false;
        for (auto& slot : lastRecordSlot) slot = -1;
        for (auto& seconds : playheadSeconds) seconds.store (0.0f, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto capacity = static_cast<int> (tracks[0].size());
        if (capacity < 64)
        {
            juce::FloatVectorOperations::copy (output, input, numSamples);
            return;
        }

        if (returnToZero.exchange (false, std::memory_order_acq_rel))
        {
            transport = 0.0;
            for (auto& head : playhead)
                head = 0.0;
            for (auto& slot : lastRecordSlot)
                slot = -1;
        }
        const bool sync = synced.load (std::memory_order_relaxed);
        if (sync && ! wasSynced)
            transport = playhead[0]; // switching SYNC on locks everything to track 1 where it stands
        wasSynced = sync;
        if (sync)
            for (auto& head : playhead)
                head = transport; // synced tracks follow the machine's own head, whichever tracks run

        const bool run = playing.load (std::memory_order_relaxed);
        const bool doRecord = run && recording.load (std::memory_order_relaxed);
        bool wroteThisBlock = false;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto master = juce::Decibels::decibelsToGain (parameterValue (trackCount, inputs, sample));
            const auto masterSpeed = juce::jlimit (0.25f, 4.0f, parameterValue (trackCount + 1, inputs, sample) * 0.01f);
            const auto dry = std::isfinite (input[sample]) ? input[sample] : 0.0f;

            float mixed = 0.0f;
            for (int track = 0; track < trackCount; ++track)
            {
                const auto index = static_cast<std::size_t> (track);
                auto& tape = tracks[index];
                auto& head = playhead[index];
                if (! run || ! trackRunning[index].load (std::memory_order_relaxed))
                {
                    lastRecordSlot[index] = -1;
                    continue;
                }
                const auto trackSpeed = sync ? masterSpeed
                                             : masterSpeed * juce::jlimit (0.25f, 4.0f, parameterValue (trackCount + 2 + track, inputs, sample) * 0.01f);
                const auto writeSlot = juce::jlimit (0, capacity - 1, static_cast<int> (head));
                const auto indexA = juce::jlimit (0, capacity - 2, static_cast<int> (head));
                const auto fraction = static_cast<float> (head - std::floor (head));
                const bool recordingHere = doRecord && armed[index].load (std::memory_order_relaxed);
                if (recordingHere)
                {
                    // Varispeed advances more than one slot per sample; fill the
                    // whole span so fast-tape recordings have no silent gaps. Slow
                    // tape revisits a slot (nothing to add); a jump (RTZ, a sync
                    // snap) starts afresh at the head.
                    const auto last = lastRecordSlot[index];
                    const auto ahead = last >= 0 ? (writeSlot - last + capacity) % capacity : -1;
                    if (ahead != 0)
                    {
                        auto slot = ahead > 0 && ahead <= 8 ? (last + 1) % capacity : writeSlot;
                        for (int guard = 0; guard < 8; ++guard)
                        {
                            tape[static_cast<std::size_t> (slot)] = dry;
                            if (slot == writeSlot)
                                break;
                            slot = (slot + 1) % capacity;
                        }
                    }
                    lastRecordSlot[index] = writeSlot;
                    wroteThisBlock = true;
                }
                else
                    lastRecordSlot[index] = -1;
                // A track being recorded is heard through the input monitor, not read back from tape.
                if (! recordingHere)
                {
                    const auto trackLevel = juce::Decibels::decibelsToGain (parameterValue (track, inputs, sample));
                    const auto value = tape[static_cast<std::size_t> (indexA)]
                                     + fraction * (tape[static_cast<std::size_t> (indexA + 1)] - tape[static_cast<std::size_t> (indexA)]);
                    mixed += value * trackLevel;
                }

                head += trackSpeed;
                if (head >= capacity - 1)
                {
                    head = 0.0; // tape loop
                    lastRecordSlot[index] = -1;
                }
            }
            if (run)
            {
                transport += masterSpeed;
                if (transport >= capacity - 1)
                    transport = 0.0;
            }
            // A tape machine monitors its input: the dry signal always passes, the tapes add to it.
            output[sample] = dry + mixed * master;
        }
        // A take that just ended (PLAY, an arm or REC toggle) moves the saved-content version.
        if (wroteLastBlock && ! wroteThisBlock)
            takesEnded.fetch_add (1, std::memory_order_relaxed);
        wroteLastBlock = wroteThisBlock;
        for (int track = 0; track < trackCount; ++track)
            playheadSeconds[static_cast<std::size_t> (track)].store (static_cast<float> (playhead[static_cast<std::size_t> (track)] / juce::jmax (1.0, sampleRate)),
                                                                     std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    std::array<std::vector<float>, trackCount> tracks;
    std::array<double, trackCount> playhead {};
    double transport = 0.0;            // the machine's head; synced tracks follow it
    bool wasSynced = true;
    bool wroteLastBlock = false;
    std::atomic<juce::uint32> takesEnded { 0 };
    std::array<int, trackCount> lastRecordSlot { -1, -1, -1, -1 };
    std::array<std::atomic<float>, trackCount> playheadSeconds {};
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<bool> returnToZero { false };
    std::atomic<bool> synced { true };
    std::array<std::atomic<bool>, trackCount> armed {};
    std::array<std::atomic<bool>, trackCount> trackRunning {};
    std::atomic<juce::uint32> recordedContentVersion { 0 };
};
class SpectralFollowerNode final : public DspNode
{
public:
    SpectralFollowerNode() : DspNode (NodeKind::spectralFollower, nodeKindName (NodeKind::spectralFollower))
    {
        addInputPort ("Audio", SignalType::audio);
        addOutputPort ("Low", SignalType::control);
        addOutputPort ("Mid", SignalType::control);
        addOutputPort ("High", SignalType::control);
        addOutputPort ("Air", SignalType::control);
        addOutputPort ("Centroid", SignalType::control);
        addOutputPort ("Level", SignalType::control);
        addParameter ("response", "Response", "ms", skewedRange (5.0f, 500.0f, 80.0f), 80.0f, 0.35f);
        addParameter ("gain", "Gain", "dB", juce::NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 6.0f, 0.25f);
    }

private:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder; // 1024
    static constexpr int hopSize = fftSize / 2;
    static constexpr int outputCount = 6;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        for (int index = 0; index < fftSize; ++index)
            window[static_cast<std::size_t> (index)] =
                0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                        * static_cast<float> (index) / static_cast<float> (fftSize - 1));
        // Band edges in bins for 200 Hz / 1.2 kHz / 5 kHz / 16 kHz.
        const auto binHz = sampleRate / fftSize;
        bandEdges[0] = 1;
        bandEdges[1] = juce::jlimit (2, fftSize / 2 - 4, juce::roundToInt (200.0 / binHz));
        bandEdges[2] = juce::jlimit (bandEdges[1] + 1, fftSize / 2 - 3, juce::roundToInt (1200.0 / binHz));
        bandEdges[3] = juce::jlimit (bandEdges[2] + 1, fftSize / 2 - 2, juce::roundToInt (5000.0 / binHz));
        bandEdges[4] = juce::jlimit (bandEdges[3] + 1, fftSize / 2 - 1, juce::roundToInt (16000.0 / binHz));
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        fifo.fill (0.0f);
        fifoIndex = 0;
        hopCounter = 0;
        targets.fill (0.0f);
        smoothed.fill (0.0f);
    }

    void analyse (float gain) noexcept
    {
        // Copy the most recent fftSize samples, windowed, into the transform
        // buffer (all storage is preallocated members).
        for (int index = 0; index < fftSize; ++index)
        {
            const auto source = (fifoIndex + index) % fftSize;
            transformBuffer[static_cast<std::size_t> (index)] =
                fifo[static_cast<std::size_t> (source)] * window[static_cast<std::size_t> (index)];
        }
        std::fill (transformBuffer.begin() + fftSize, transformBuffer.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (transformBuffer.data());

        float centroidWeight = 0.0f;
        float centroidSum = 0.0f;
        float total = 0.0f;
        for (std::size_t band = 0; band < 4; ++band)
        {
            float energy = 0.0f;
            const auto begin = bandEdges[band];
            const auto end = bandEdges[band + 1];
            for (int bin = begin; bin < end; ++bin)
            {
                const auto magnitude = transformBuffer[static_cast<std::size_t> (bin)];
                energy += magnitude * magnitude;
                centroidSum += magnitude * static_cast<float> (bin);
                centroidWeight += magnitude;
                total += magnitude * magnitude;
            }
            const auto bins = juce::jmax (1, end - begin);
            targets[band] = juce::jlimit (0.0f, 1.0f,
                                          std::sqrt (energy / static_cast<float> (bins))
                                              * gain * (0.06f + 0.05f * static_cast<float> (band)));
        }
        if (centroidWeight > 1.0e-6f)
        {
            const auto centroidBin = centroidSum / centroidWeight;
            const auto centroidHz = juce::jmax (20.0f, static_cast<float> (centroidBin * sampleRate / fftSize));
            targets[4] = juce::jlimit (0.0f, 1.0f,
                                       std::log2 (centroidHz / 20.0f) / std::log2 (20000.0f / 20.0f));
        }
        targets[5] = juce::jlimit (0.0f, 1.0f,
                                   std::sqrt (total / static_cast<float> (fftSize / 2)) * gain * 0.05f);
        for (auto& value : targets)
            if (! std::isfinite (value))
                value = 0.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto responseMs = parameterValue (0, inputs, sample);
            const auto gain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));

            fifo[static_cast<std::size_t> (fifoIndex)] =
                std::isfinite (input[sample]) ? input[sample] : 0.0f;
            fifoIndex = (fifoIndex + 1) % fftSize;
            if (++hopCounter >= hopSize)
            {
                hopCounter = 0;
                analyse (gain);
            }

            const auto coefficient = static_cast<float> (
                std::exp (-1.0 / (sampleRate * 0.001 * juce::jmax (1.0f, responseMs))));
            for (std::size_t channel = 0; channel < outputCount; ++channel)
            {
                smoothed[channel] = coefficient * smoothed[channel]
                                  + (1.0f - coefficient) * targets[channel];
                outputs.setSample (static_cast<int> (channel), sample, smoothed[channel]);
            }
        }
    }

    double sampleRate = 48000.0;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {};
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> transformBuffer {};
    std::array<int, 5> bandEdges {};
    std::array<float, outputCount> targets {};
    std::array<float, outputCount> smoothed {};
    int fifoIndex = 0;
    int hopCounter = 0;
};

// ---------------------------------------------------------------------------
// Script node: a per-sample expression compiled on the message thread into a
// stack program the callback can evaluate without allocation or recursion.
namespace expr
{
enum class Op
{
    pushConst, pushX, pushA, pushB, pushC, pushT, pushSampleRate,
    add, subtract, multiply, divide, modulo, power, negate,
    sine, cosine, tangent, hyperbolicTangent, absolute, squareRoot,
    exponential, logarithm, floorOf, signOf,
    minimum, maximum, clampTo, noise
};

struct Step
{
    Op op = Op::pushConst;
    float value = 0.0f;
};

struct Program
{
    std::vector<Step> steps;
    juce::String source;
};

struct FunctionInfo
{
    const char* name;
    Op op;
    int arity;
};

constexpr FunctionInfo functionTable[] {
    { "sin", Op::sine, 1 },       { "cos", Op::cosine, 1 },   { "tan", Op::tangent, 1 },
    { "tanh", Op::hyperbolicTangent, 1 }, { "abs", Op::absolute, 1 }, { "sqrt", Op::squareRoot, 1 },
    { "exp", Op::exponential, 1 },{ "log", Op::logarithm, 1 },{ "floor", Op::floorOf, 1 },
    { "sign", Op::signOf, 1 },    { "min", Op::minimum, 2 },  { "max", Op::maximum, 2 },
    { "pow", Op::power, 2 },      { "clamp", Op::clampTo, 3 },{ "noise", Op::noise, 0 }
};

// Compiles an infix expression to a stack program. Returns an error message
// on failure. Runs on the message thread only.
inline juce::String compile (const juce::String& text, Program& program)
{
    program.steps.clear();
    program.source = text;

    struct Token
    {
        enum class Type { number, identifier, op, open, close, comma } type;
        juce::String text;
        float value = 0.0f;
    };

    // Tokenize.
    std::vector<Token> tokens;
    const auto source = text.trim();
    if (source.isEmpty())
        return "expression is empty";
    int index = 0;
    while (index < source.length())
    {
        const auto character = source[index];
        if (juce::CharacterFunctions::isWhitespace (character))
        {
            ++index;
            continue;
        }
        if (juce::CharacterFunctions::isDigit (character) || character == '.')
        {
            int end = index;
            while (end < source.length()
                   && (juce::CharacterFunctions::isDigit (source[end]) || source[end] == '.'))
                ++end;
            tokens.push_back ({ Token::Type::number, source.substring (index, end),
                                source.substring (index, end).getFloatValue() });
            index = end;
            continue;
        }
        if (juce::CharacterFunctions::isLetter (character))
        {
            int end = index;
            while (end < source.length()
                   && (juce::CharacterFunctions::isLetterOrDigit (source[end]) || source[end] == '_'))
                ++end;
            tokens.push_back ({ Token::Type::identifier, source.substring (index, end).toLowerCase() });
            index = end;
            continue;
        }
        if (character == '(')
            tokens.push_back ({ Token::Type::open, "(" });
        else if (character == ')')
            tokens.push_back ({ Token::Type::close, ")" });
        else if (character == ',')
            tokens.push_back ({ Token::Type::comma, "," });
        else if (juce::String ("+-*/%^").containsChar (character))
            tokens.push_back ({ Token::Type::op, juce::String::charToString (character) });
        else
            return "unexpected character '" + juce::String::charToString (character) + "'";
        ++index;
    }

    // Shunting-yard.
    struct StackEntry
    {
        juce::String symbol; // operator, "(" or function name
        bool isFunction = false;
    };
    std::vector<StackEntry> stack;

    auto precedence = [] (const juce::String& symbol)
    {
        if (symbol == "u-") return 5;
        if (symbol == "^") return 4;
        if (symbol == "*" || symbol == "/" || symbol == "%") return 3;
        return 2; // + -
    };
    auto emitOperator = [&program] (const juce::String& symbol) -> bool
    {
        if (symbol == "+") program.steps.push_back ({ Op::add, 0.0f });
        else if (symbol == "-") program.steps.push_back ({ Op::subtract, 0.0f });
        else if (symbol == "*") program.steps.push_back ({ Op::multiply, 0.0f });
        else if (symbol == "/") program.steps.push_back ({ Op::divide, 0.0f });
        else if (symbol == "%") program.steps.push_back ({ Op::modulo, 0.0f });
        else if (symbol == "^") program.steps.push_back ({ Op::power, 0.0f });
        else if (symbol == "u-") program.steps.push_back ({ Op::negate, 0.0f });
        else return false;
        return true;
    };
    auto emitFunction = [&program] (const juce::String& name) -> bool
    {
        for (const auto& entry : functionTable)
            if (name == entry.name)
            {
                program.steps.push_back ({ entry.op, 0.0f });
                return true;
            }
        return false;
    };

    bool expectOperand = true;
    for (std::size_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
    {
        const auto& token = tokens[tokenIndex];
        switch (token.type)
        {
            case Token::Type::number:
                program.steps.push_back ({ Op::pushConst, token.value });
                expectOperand = false;
                break;
            case Token::Type::identifier:
            {
                const bool isCall = tokenIndex + 1 < tokens.size()
                                 && tokens[tokenIndex + 1].type == Token::Type::open;
                if (isCall)
                {
                    stack.push_back ({ token.text, true });
                    expectOperand = true;
                }
                else
                {
                    if (token.text == "x") program.steps.push_back ({ Op::pushX, 0.0f });
                    else if (token.text == "a") program.steps.push_back ({ Op::pushA, 0.0f });
                    else if (token.text == "b") program.steps.push_back ({ Op::pushB, 0.0f });
                    else if (token.text == "c") program.steps.push_back ({ Op::pushC, 0.0f });
                    else if (token.text == "t") program.steps.push_back ({ Op::pushT, 0.0f });
                    else if (token.text == "sr") program.steps.push_back ({ Op::pushSampleRate, 0.0f });
                    else if (token.text == "pi") program.steps.push_back ({ Op::pushConst, juce::MathConstants<float>::pi });
                    else if (token.text == "twopi") program.steps.push_back ({ Op::pushConst, juce::MathConstants<float>::twoPi });
                    else return "unknown name '" + token.text + "'";
                    expectOperand = false;
                }
                break;
            }
            case Token::Type::op:
            {
                auto symbol = token.text;
                if (symbol == "-" && expectOperand)
                    symbol = "u-";
                else if (expectOperand)
                    return "operator '" + symbol + "' needs a value before it";
                while (! stack.empty() && ! stack.back().isFunction && stack.back().symbol != "("
                       && (precedence (stack.back().symbol) > precedence (symbol)
                           || (precedence (stack.back().symbol) == precedence (symbol) && symbol != "^"
                               && symbol != "u-")))
                {
                    emitOperator (stack.back().symbol);
                    stack.pop_back();
                }
                stack.push_back ({ symbol, false });
                expectOperand = true;
                break;
            }
            case Token::Type::open:
                stack.push_back ({ "(", false });
                expectOperand = true;
                break;
            case Token::Type::comma:
            case Token::Type::close:
                while (! stack.empty() && stack.back().symbol != "(")
                {
                    if (stack.back().isFunction)
                        return "misplaced parenthesis";
                    emitOperator (stack.back().symbol);
                    stack.pop_back();
                }
                if (stack.empty())
                    return "unbalanced parentheses";
                if (token.type == Token::Type::close)
                {
                    stack.pop_back(); // the "("
                    if (! stack.empty() && stack.back().isFunction)
                    {
                        if (! emitFunction (stack.back().symbol))
                            return "unknown function '" + stack.back().symbol + "'";
                        stack.pop_back();
                    }
                    expectOperand = false;
                }
                else
                {
                    // Comma: operators up to the "(" are already emitted; the
                    // "(" stays on the stack for the remaining arguments.
                    expectOperand = true;
                }
                break;
        }
    }
    while (! stack.empty())
    {
        if (stack.back().symbol == "(" || stack.back().isFunction)
            return "unbalanced parentheses";
        emitOperator (stack.back().symbol);
        stack.pop_back();
    }

    // Verify stack behaviour so evaluation can never underflow.
    int depth = 0;
    for (const auto& step : program.steps)
    {
        const auto pops = [] (Op op)
        {
            if (op == Op::pushConst || op == Op::pushX || op == Op::pushA || op == Op::pushB
                || op == Op::pushC || op == Op::pushT || op == Op::pushSampleRate || op == Op::noise)
                return 0;
            if (op == Op::negate || op == Op::sine || op == Op::cosine || op == Op::tangent
                || op == Op::hyperbolicTangent || op == Op::absolute || op == Op::squareRoot
                || op == Op::exponential || op == Op::logarithm || op == Op::floorOf || op == Op::signOf)
                return 1;
            if (op == Op::clampTo)
                return 3;
            return 2;
        } (step.op);
        if (depth < pops)
            return "malformed expression";
        depth -= pops;
        ++depth;
        if (depth > 60)
            return "expression too deep";
    }
    if (depth != 1)
        return "expression must produce exactly one value";
    return {};
}
} // namespace expr

class ScriptNode final : public DspNode
{
public:
    ScriptNode() : DspNode (NodeKind::script, nodeKindName (NodeKind::script))
    {
        addInputPort ("x", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addParameter ("a", "A", "", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, 0.5f);
        addParameter ("b", "B", "", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, 0.5f);
        addParameter ("c", "C", "", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, 0.5f);
        setExtraState (defaultState());
    }

    juce::var getExtraState() const override
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("expr", expressionText);
        return juce::var (object.release());
    }

    void setExtraState (const juce::var& state) override
    {
        const auto text = state.getProperty ("expr", juce::String()).toString();
        if (text.isEmpty())
            return;
        auto candidate = std::make_unique<expr::Program> ();
        const auto error = expr::compile (text, *candidate);
        expressionText = text;
        if (error.isNotEmpty())
        {
            compileError = error;
            return;
        }
        compileError.clear();
        // Swap for the callback; the previous program stays alive until the
        // next successful compile so the audio thread never frees it.
        retiredProgram = std::move (currentProgram);
        currentProgram = std::move (candidate);
        activeProgram.store (currentProgram.get(), std::memory_order_release);
    }

    juce::String statusText() const override
    {
        return compileError.isNotEmpty() ? "ERROR: " + compileError
                                         : "y = " + expressionText;
    }

private:
    static juce::var defaultState()
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty ("expr", "tanh(x * (1 + a * 9))");
        return juce::var (object.release());
    }

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        timeSeconds = 0.0;
    }

    void resetDsp() noexcept override { timeSeconds = 0.0; }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto* program = activeProgram.load (std::memory_order_acquire);
        if (program == nullptr)
        {
            juce::FloatVectorOperations::copy (output, input, numSamples);
            return;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto a = parameterValue (0, inputs, sample);
            const auto b = parameterValue (1, inputs, sample);
            const auto c = parameterValue (2, inputs, sample);

            float stack[64];
            int top = 0;
            for (const auto& step : program->steps)
            {
                using expr::Op;
                switch (step.op)
                {
                    case Op::pushConst:      stack[top++] = step.value; break;
                    case Op::pushX:          stack[top++] = std::isfinite (input[sample]) ? input[sample] : 0.0f; break;
                    case Op::pushA:          stack[top++] = a; break;
                    case Op::pushB:          stack[top++] = b; break;
                    case Op::pushC:          stack[top++] = c; break;
                    case Op::pushT:          stack[top++] = static_cast<float> (timeSeconds); break;
                    case Op::pushSampleRate: stack[top++] = static_cast<float> (sampleRate); break;
                    case Op::noise:          stack[top++] = random.nextFloat() * 2.0f - 1.0f; break;
                    case Op::add:            --top; stack[top - 1] += stack[top]; break;
                    case Op::subtract:       --top; stack[top - 1] -= stack[top]; break;
                    case Op::multiply:       --top; stack[top - 1] *= stack[top]; break;
                    case Op::divide:         --top; stack[top - 1] = std::abs (stack[top]) > 1.0e-12f ? stack[top - 1] / stack[top] : 0.0f; break;
                    case Op::modulo:         --top; stack[top - 1] = std::abs (stack[top]) > 1.0e-12f ? std::fmod (stack[top - 1], stack[top]) : 0.0f; break;
                    case Op::power:          --top; stack[top - 1] = std::pow (stack[top - 1], stack[top]); break;
                    case Op::negate:         stack[top - 1] = -stack[top - 1]; break;
                    case Op::sine:           stack[top - 1] = std::sin (stack[top - 1]); break;
                    case Op::cosine:         stack[top - 1] = std::cos (stack[top - 1]); break;
                    case Op::tangent:        stack[top - 1] = std::tan (stack[top - 1]); break;
                    case Op::hyperbolicTangent: stack[top - 1] = std::tanh (stack[top - 1]); break;
                    case Op::absolute:       stack[top - 1] = std::abs (stack[top - 1]); break;
                    case Op::squareRoot:     stack[top - 1] = std::sqrt (juce::jmax (0.0f, stack[top - 1])); break;
                    case Op::exponential:    stack[top - 1] = std::exp (juce::jlimit (-30.0f, 30.0f, stack[top - 1])); break;
                    case Op::logarithm:      stack[top - 1] = std::log (juce::jmax (1.0e-9f, stack[top - 1])); break;
                    case Op::floorOf:        stack[top - 1] = std::floor (stack[top - 1]); break;
                    case Op::signOf:         stack[top - 1] = stack[top - 1] > 0.0f ? 1.0f : (stack[top - 1] < 0.0f ? -1.0f : 0.0f); break;
                    case Op::minimum:        --top; stack[top - 1] = juce::jmin (stack[top - 1], stack[top]); break;
                    case Op::maximum:        --top; stack[top - 1] = juce::jmax (stack[top - 1], stack[top]); break;
                    case Op::clampTo:        top -= 2; stack[top - 1] = juce::jlimit (stack[top], stack[top + 1], stack[top - 1]); break;
                }
            }
            auto result = stack[0];
            if (! std::isfinite (result))
                result = 0.0f;
            output[sample] = juce::jlimit (-2.0f, 2.0f, result);

            timeSeconds += 1.0 / sampleRate;
            if (timeSeconds > 1.0e6)
                timeSeconds = 0.0;
        }
    }

    double sampleRate = 48000.0;
    double timeSeconds = 0.0;
    juce::String expressionText;
    juce::String compileError;
    std::unique_ptr<expr::Program> currentProgram;
    std::unique_ptr<expr::Program> retiredProgram;
    std::atomic<const expr::Program*> activeProgram { nullptr };
    juce::Random random;
};
} // namespace

// Speaker cabinet: zero-latency partitioned convolution of one or two impulse
// responses (A <-> B blend), then low/high cut. Impulse files are read on a
// worker thread and handed to juce::dsp::Convolution, whose own background
// thread builds the engine and swaps it in wait-free on a later block, so the
// callback never touches a file or allocates. With no impulse loaded the node
// passes the dry signal so a rig keeps sounding while a cab is chosen.
class CabinetNode final : public DspNode
{
public:
    static constexpr double maximumImpulseSeconds = 1.0;

    CabinetNode()
        : DspNode (NodeKind::cabinet, nodeKindName (NodeKind::cabinet))
    {
        addInputPort ("In", SignalType::audio);
        addOutputPort ("Audio", SignalType::audio);
        addOutputPort ("R (IR B)", SignalType::audio); // stereo cab: A left, B right
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("blend", "A <-> B", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, 0.5f);
        addParameter ("low-cut", "Low cut", "Hz", juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.5f), 60.0f, 0.25f);
        addParameter ("high-cut", "High cut", "Hz", juce::NormalisableRange<float> (2000.0f, 20000.0f, 1.0f, 0.5f), 12000.0f, 0.25f);
    }

    /** Where impulse cycling looks when slot A is empty. */
    static juce::File defaultImpulsesDirectory()
    {
        if (const auto* env = std::getenv ("SIGNALPATCH_IRS_DIR"))
        {
            const juce::File dir { juce::String (env) };
            if (dir.isDirectory())
                return dir;
        }
        const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("SignalPatch").getChildFile ("irs");
        if (documents.isDirectory())
            return documents;
        const juce::File guitarix { "/usr/share/gx_head/sounds/amps" };
        if (guitarix.isDirectory())
            return guitarix;
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "prev-ir")
            return cycleImpulse (-1);
        if (command == "next-ir")
            return cycleImpulse (1);
        return false;
    }

    juce::var getExtraState() const override
    {
        if (slots[0].path.isEmpty() && slots[1].path.isEmpty())
            return {};
        auto object = std::make_unique<juce::DynamicObject>();
        if (slots[0].path.isNotEmpty())
            object->setProperty ("ir", slots[0].path);
        if (slots[1].path.isNotEmpty())
            object->setProperty ("irB", slots[1].path);
        return juce::var (object.release());
    }

    // Message thread. Both slots are set from one state object so undo can
    // restore "no cab" as well as any pair of impulses.
    void setExtraState (const juce::var& state) override
    {
        setSlotPath (0, state.getProperty ("ir", juce::String()).toString());
        setSlotPath (1, state.getProperty ("irB", juce::String()).toString());
    }

    juce::String statusText() const override
    {
        juce::StringArray parts;
        for (int index = 0; index < 2; ++index)
        {
            const auto& slot = slots[static_cast<std::size_t> (index)];
            const juce::String label = index == 0 ? "A: " : "B: ";
            if (slot.loading.load (std::memory_order_relaxed))
                parts.add (label + "loading " + juce::File (slot.path).getFileName() + "...");
            else if (slot.error.isNotEmpty())
                parts.add (label + "LOAD FAILED: " + slot.error);
            else if (slot.path.isNotEmpty())
                parts.add (label + juce::File (slot.path).getFileNameWithoutExtension());
        }
        if (parts.isEmpty())
            return "no IR - LOAD or step with the arrows";
        return parts.joinIntoString ("  |  ");
    }

private:
    struct Slot
    {
        explicit Slot (juce::dsp::ConvolutionMessageQueue& queue) : convolver (queue) {}

        juce::String path;
        juce::String error;
        std::atomic<bool> loading { false };
        std::atomic<bool> active { false }; // an impulse has been handed to the convolver
        juce::dsp::Convolution convolver;
        std::vector<float> scratch;
    };

    // RBJ biquad with fixed Q; coefficients refreshed once per block.
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

        void set (double sampleRate, double frequency, bool highpass) noexcept
        {
            const auto w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sampleRate * 0.45, frequency) / sampleRate;
            const auto cosW = std::cos (w0);
            const auto alpha = std::sin (w0) / (2.0 * juce::MathConstants<double>::sqrt2 * 0.5);
            const auto a0 = 1.0 + alpha;
            const auto sign = highpass ? 1.0 : -1.0;
            b0 = static_cast<float> ((1.0 + sign * cosW) * 0.5 / a0);
            b1 = static_cast<float> (-sign * (1.0 + sign * cosW) / a0);
            b2 = b0;
            a1 = static_cast<float> (-2.0 * cosW / a0);
            a2 = static_cast<float> ((1.0 - alpha) / a0);
        }

        float process (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    static std::unique_ptr<juce::AudioBuffer<float>> readImpulse (const juce::String& path,
                                                                  double& sampleRateOut,
                                                                  juce::String& error)
    {
        juce::AudioFormatManager manager;
        manager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (juce::File (path)));
        if (reader == nullptr)
        {
            error = "unsupported or missing audio file";
            return nullptr;
        }
        const auto maximumSamples = static_cast<juce::int64> (reader->sampleRate * maximumImpulseSeconds);
        const auto length = static_cast<int> (juce::jmin (reader->lengthInSamples, maximumSamples));
        if (length <= 0)
        {
            error = "empty impulse";
            return nullptr;
        }
        auto buffer = std::make_unique<juce::AudioBuffer<float>> (1, length);
        reader->read (buffer.get(), 0, length, 0, true, false); // first channel only: cabs are mono here
        sampleRateOut = reader->sampleRate;
        return buffer;
    }

    // Message thread. Steps slot A to the neighbouring impulse in its folder.
    bool cycleImpulse (int delta)
    {
        const auto& current = slots[0].path;
        const auto currentFile = juce::File (current);
        const auto directory = current.isNotEmpty() && currentFile.getParentDirectory().isDirectory()
            ? currentFile.getParentDirectory()
            : defaultImpulsesDirectory();
        auto candidates = directory.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
        if (candidates.isEmpty())
            return false;
        candidates.sort();
        int index = 0;
        for (int i = 0; i < candidates.size(); ++i)
            if (candidates.getReference (i) == currentFile)
            {
                index = i + delta;
                break;
            }
        index = (index % candidates.size() + candidates.size()) % candidates.size();
        setSlotPath (0, candidates.getReference (index).getFullPathName());
        return true;
    }

    void setSlotPath (int index, const juce::String& path)
    {
        auto& slot = slots[static_cast<std::size_t> (index)];
        if (path == slot.path)
            return;
        slot.path = path;
        slot.error.clear();
        if (path.isEmpty())
        {
            slot.active.store (false, std::memory_order_release);
            slot.loading.store (false, std::memory_order_relaxed);
            return;
        }

        // Headless (tests): no message loop, read synchronously.
        if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        {
            double rate = 0.0;
            juce::String error;
            auto buffer = readImpulse (path, rate, error); // sequenced: rate/error are outputs
            finishImpulseLoad (index, path, std::move (buffer), rate, error);
            return;
        }

        slot.loading.store (true, std::memory_order_relaxed);
        std::weak_ptr<DspNode> weakSelf = weak_from_this();
        juce::Thread::launch ([weakSelf, index, path]
        {
            double rate = 0.0;
            juce::String error;
            auto loaded = std::make_shared<std::unique_ptr<juce::AudioBuffer<float>>> (readImpulse (path, rate, error));
            juce::MessageManager::callAsync ([weakSelf, index, path, rate, error, loaded]
            {
                if (auto locked = weakSelf.lock())
                    static_cast<CabinetNode&> (*locked).finishImpulseLoad (index, path, std::move (*loaded), rate, error);
            });
        });
    }

    // Message thread only. Hands the impulse to the convolver (wait-free; the
    // engine is built on its background thread and adopted on a later block).
    void finishImpulseLoad (int index, const juce::String& path,
                            std::unique_ptr<juce::AudioBuffer<float>> buffer,
                            double bufferSampleRate, const juce::String& error)
    {
        auto& slot = slots[static_cast<std::size_t> (index)];
        if (path != slot.path)
            return; // superseded by a newer request
        slot.loading.store (false, std::memory_order_relaxed);
        if (buffer == nullptr)
        {
            slot.error = error.isNotEmpty() ? error : "could not read the impulse";
            slot.active.store (false, std::memory_order_release);
            return;
        }
        slot.error.clear();
        slot.convolver.loadImpulseResponse (std::move (*buffer), bufferSampleRate,
                                            juce::dsp::Convolution::Stereo::no,
                                            juce::dsp::Convolution::Trim::no,
                                            juce::dsp::Convolution::Normalise::yes);
        slot.active.store (true, std::memory_order_release);
    }

    void prepareDsp (double newSampleRate, int newMaximumBlockSize) override
    {
        sampleRate = newSampleRate;
        maximumBlockSize = juce::jmax (16, newMaximumBlockSize);
        const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maximumBlockSize), 1 };
        for (auto& slot : slots)
        {
            slot.convolver.prepare (spec);
            slot.scratch.assign (static_cast<std::size_t> (maximumBlockSize), 0.0f);
        }
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        for (auto& slot : slots)
            slot.convolver.reset();
        lowCut.reset();
        highCut.reset();
        lowCutR.reset();
        highCutR.reset();
        lastLowCut = lastHighCut = -1.0f;
    }

    void processDsp (const juce::AudioBuffer<float>& inputs,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto frames = juce::jmin (numSamples, maximumBlockSize);
        if (frames <= 0)
        {
            juce::FloatVectorOperations::clear (output, numSamples);
            return;
        }

        const auto lowHz = parameterTarget (2, inputs);
        const auto highHz = parameterTarget (3, inputs);
        if (lowHz != lastLowCut)
        {
            lowCut.set (sampleRate, lowHz, true);
            lowCutR.set (sampleRate, lowHz, true);
            lastLowCut = lowHz;
        }
        if (highHz != lastHighCut)
        {
            highCut.set (sampleRate, highHz, false);
            highCutR.set (sampleRate, highHz, false);
            lastHighCut = highHz;
        }

        bool ready[2] { false, false };
        for (std::size_t index = 0; index < 2; ++index)
        {
            auto& slot = slots[index];
            // Once an impulse has been handed over, always run the convolver:
            // it installs the pending engine inside process() and cross-fades
            // from dry to wet itself, so the cab fades in rather than jumping.
            ready[index] = slot.active.load (std::memory_order_acquire);
            if (! ready[index])
                continue;
            for (int sample = 0; sample < frames; ++sample)
            {
                const auto raw = input[sample];
                slot.scratch[static_cast<std::size_t> (sample)] = std::isfinite (raw) ? raw : 0.0f;
            }
            float* channels[1] { slot.scratch.data() };
            juce::dsp::AudioBlock<float> block (channels, 1, static_cast<std::size_t> (frames));
            juce::dsp::ProcessContextReplacing<float> context (block);
            slot.convolver.process (context);
        }

        const auto* a = slots[0].scratch.data();
        const auto* b = slots[1].scratch.data();
        auto* outputR = outputs.getNumChannels() > 1 ? outputs.getWritePointer (1) : nullptr;
        for (int sample = 0; sample < frames; ++sample)
        {
            const auto level = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto blend = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto index = static_cast<std::size_t> (sample);
            const auto dry = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            float value;
            if (ready[0] && ready[1])
                value = a[index] + blend * (b[index] - a[index]);
            else if (ready[0])
                value = a[index];
            else if (ready[1])
                value = b[index];
            else
                value = dry;

            value = highCut.process (lowCut.process (value)) * level;
            if (! std::isfinite (value))
                value = 0.0f;
            output[sample] = juce::jlimit (-4.0f, 4.0f, value);

            if (outputR != nullptr)
            {
                // Right: impulse B on its own when both are loaded (a two-cab
                // stereo image), otherwise the same as the main output.
                float valueR = ready[0] && ready[1] ? b[index] : ready[1] ? b[index] : ready[0] ? a[index] : dry;
                valueR = highCutR.process (lowCutR.process (valueR)) * level;
                if (! std::isfinite (valueR))
                    valueR = 0.0f;
                outputR[sample] = juce::jlimit (-4.0f, 4.0f, valueR);
            }
        }
        if (frames < numSamples)
        {
            juce::FloatVectorOperations::clear (output + frames, numSamples - frames);
            if (outputR != nullptr)
                juce::FloatVectorOperations::clear (outputR + frames, numSamples - frames);
        }
    }

    double sampleRate = 48000.0;
    int maximumBlockSize = 128;
    juce::dsp::ConvolutionMessageQueue queue; // declared before the slots: they hold a reference
    std::array<Slot, 2> slots { Slot { queue }, Slot { queue } };
    Biquad lowCut, highCut, lowCutR, highCutR;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;
};

// Looper: record a loop, close it, overdub on top, undo the last pass, play
// it back at half speed or in reverse. The dry signal always passes through.
// Undo is real-time safe: the pre-overdub audio is saved one sample at a time
// as the pass proceeds, never with a whole-buffer copy on the audio thread.
class LooperNode final : public DspNode
{
public:
    LooperNode() : DspNode (NodeKind::looper, nodeKindName (NodeKind::looper))
    {
        addInputPort ("In", SignalType::audio);
        addOutputPort ("Out", SignalType::audio);
        addParameter ("loop-level", "Loop", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("dry-level", "Dry", "dB", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, 0.25f);
        addParameter ("feedback", "Feedback", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.25f);
        addParameter ("speed", "Speed", "%", juce::NormalisableRange<float> (25.0f, 200.0f, 0.1f), 100.0f, 0.35f);
        addParameter ("bpm", "Tempo", "bpm", juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f, 0.25f, false);
        addParameter ("bars", "Bars", "", juce::NormalisableRange<float> (0.0f, 8.0f, 1.0f), 0.0f, 0.0f, false);
        // While a Clock feeds this port, REC / PLAY wait for its next pulse so
        // loops start and close on the beat (a bar pulse gives whole bars).
        addInputPort ("Clock", SignalType::control);
        clock.port = getNumInputPorts() - 1;
    }

    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "rec")      { pendingCommand.store (commandRecord, std::memory_order_release); return true; }
        if (command == "play")     { pendingCommand.store (commandPlay, std::memory_order_release); return true; }
        if (command == "undo")     { pendingCommand.store (commandUndo, std::memory_order_release); return true; }
        if (command == "clear")    { pendingCommand.store (commandClear, std::memory_order_release); return true; }
        if (command == "half")     { halfSpeed.store (! halfSpeed.load (std::memory_order_relaxed), std::memory_order_relaxed); return true; }
        if (command == "reverse")  { reversed.store (! reversed.load (std::memory_order_relaxed), std::memory_order_relaxed); return true; }
        return false;
    }

    juce::var getExtraState() const override
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("half", halfSpeed.load (std::memory_order_relaxed));
        object->setProperty ("reverse", reversed.load (std::memory_order_relaxed));
        return juce::var (object);
    }

    void setExtraState (const juce::var& value) override
    {
        if (const auto* object = value.getDynamicObject())
        {
            halfSpeed.store (static_cast<bool> (object->getProperty ("half")), std::memory_order_relaxed);
            reversed.store (static_cast<bool> (object->getProperty ("reverse")), std::memory_order_relaxed);
        }
    }

    bool uiToggleState (const juce::String& command) const override
    {
        if (command == "rec")     return state.load (std::memory_order_relaxed) == recording || state.load (std::memory_order_relaxed) == overdubbing;
        if (command == "play")    return state.load (std::memory_order_relaxed) == playing || state.load (std::memory_order_relaxed) == overdubbing;
        if (command == "half")    return halfSpeed.load (std::memory_order_relaxed);
        if (command == "reverse") return reversed.load (std::memory_order_relaxed);
        return false;
    }

    /** Playhead position in percent (0-99) for the UI's loop bar. */
    int currentStep() const noexcept override
    {
        const auto len = lengthSamples.load (std::memory_order_relaxed);
        if (len <= 0)
            return -1;
        return juce::jlimit (0, 99, static_cast<int> (100.0 * playheadForUi.load (std::memory_order_relaxed) / len));
    }

    juce::String statusText() const override
    {
        const auto len = lengthSamples.load (std::memory_order_relaxed);
        const auto seconds = juce::String (len / juce::jmax (1.0, sampleRate), 1) + "s";
        if (queuedForUi.load (std::memory_order_relaxed))
            return "ON THE NEXT PULSE...";
        switch (state.load (std::memory_order_relaxed))
        {
            case empty:      return "EMPTY - REC to start a loop";
            case recording:  return "REC " + juce::String (recordedSamples.load (std::memory_order_relaxed) / juce::jmax (1.0, sampleRate), 1) + "s";
            case playing:    return "PLAY " + seconds + "  " + juce::String (layers.load (std::memory_order_relaxed)) + (layers.load (std::memory_order_relaxed) == 1 ? " layer" : " layers");
            case overdubbing:return "OVERDUB " + seconds + "  " + juce::String (layers.load (std::memory_order_relaxed)) + " layers";
            case stopped:    return "STOP " + seconds + "  " + juce::String (layers.load (std::memory_order_relaxed)) + (layers.load (std::memory_order_relaxed) == 1 ? " layer" : " layers");
        }
        return {};
    }

    // Recording persistence (message thread). Reading the loop while the
    // callback plays it is a benign race on floats; the length is atomic.
    bool hasAudioContent() const noexcept override { return lengthSamples.load (std::memory_order_acquire) > 0; }
    juce::uint32 audioContentVersion() const noexcept override { return contentVersion.load (std::memory_order_relaxed); }

    juce::AudioBuffer<float> exportAudioContent() const override
    {
        const auto len = lengthSamples.load (std::memory_order_acquire);
        juce::AudioBuffer<float> out (1, juce::jmax (0, len));
        for (int i = 0; i < len; ++i)
            out.setSample (0, i, loop[static_cast<std::size_t> (i)]);
        return out;
    }

    void importAudioContent (const juce::AudioBuffer<float>& audio) override
    {
        if (audio.getNumChannels() == 0)
            return;
        const auto len = juce::jmin (audio.getNumSamples(), static_cast<int> (loop.size()) - 4);
        for (int i = 0; i < len; ++i)
            loop[static_cast<std::size_t> (i)] = audio.getSample (0, i);
        lengthSamples.store (len, std::memory_order_release);
        layers.store (len > 0 ? 1 : 0, std::memory_order_relaxed);
        state.store (len > 0 ? stopped : empty, std::memory_order_release);
        contentVersion.fetch_add (1, std::memory_order_relaxed);
    }

private:
    enum State { empty = 0, recording, playing, overdubbing, stopped };
    enum Command { commandNone = 0, commandRecord, commandPlay, commandUndo, commandClear };
    static constexpr int maximumSeconds = 60;
    ClockFollower clock;
    Command queuedCommand = commandNone;
    std::atomic<bool> queuedForUi { false };

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        const auto capacity = static_cast<std::size_t> (std::ceil (newSampleRate * maximumSeconds)) + 4u;
        if (loop.size() != capacity)
        {
            loop.assign (capacity, 0.0f);
            previous.assign (capacity, 0.0f);
            lengthSamples.store (0, std::memory_order_relaxed);
            state.store (empty, std::memory_order_relaxed);
        }
    }

    void resetDsp() noexcept override
    {
        playhead = 0.0;
        playheadForUi.store (0.0, std::memory_order_relaxed);
        clock.reset();
        queuedCommand = commandNone;
        queuedForUi.store (false, std::memory_order_relaxed);
    }

    void applyCommand (Command command, float bpm, int bars) noexcept
    {
        const auto current = state.load (std::memory_order_relaxed);
        const auto capacity = static_cast<int> (loop.size()) - 4;
        switch (command)
        {
            case commandRecord:
                if (current == empty || current == stopped)
                {
                    if (current == empty)
                    {
                        // Fresh loop; a bar count fixes the length up front.
                        targetLength = bars > 0 ? juce::jmin (capacity, static_cast<int> (std::round (bars * 4.0 * 60.0 / juce::jmax (40.0f, bpm) * sampleRate))) : 0;
                        recorded = 0;
                        playhead = 0.0;
                        state.store (recording, std::memory_order_release);
                    }
                    else
                        beginOverdub();
                }
                else if (current == recording)
                    closeLoop();
                else if (current == playing)
                    beginOverdub();
                else if (current == overdubbing)
                {
                    state.store (playing, std::memory_order_release);
                    contentVersion.fetch_add (1, std::memory_order_relaxed);
                }
                break;
            case commandPlay:
                if (current == playing || current == overdubbing)
                {
                    state.store (stopped, std::memory_order_release);
                    if (current == overdubbing)
                        contentVersion.fetch_add (1, std::memory_order_relaxed); // the overdubbed layers must reach the saved file
                }
                else if (current == stopped)
                {
                    playhead = 0.0;
                    state.store (playing, std::memory_order_release);
                }
                else if (current == recording)
                    closeLoop();
                break;
            case commandUndo:
                if (undoAvailable && restoreRemaining == 0 && (current == playing || current == overdubbing || current == stopped))
                {
                    // The slots the session touched form one run from sessionStart in
                    // its direction; copy them back a chunk per block (continueRestore)
                    // so a 60 s loop never costs one long callback.
                    const auto len = lengthSamples.load (std::memory_order_relaxed);
                    restoreRemaining = juce::jmin (len, savedInSession);
                    restoreCursor = sessionStart;
                    restoreDirection = sessionDirection;
                    undoAvailable = false;
                    layers.store (juce::jmax (1, layers.load (std::memory_order_relaxed) - 1), std::memory_order_relaxed);
                    if (current == overdubbing)
                        state.store (playing, std::memory_order_release);
                    if (restoreRemaining == 0)
                        contentVersion.fetch_add (1, std::memory_order_relaxed);
                }
                break;
            case commandClear:
                restoreRemaining = 0;
                lengthSamples.store (0, std::memory_order_release);
                layers.store (0, std::memory_order_relaxed);
                recorded = 0;
                playhead = 0.0;
                undoAvailable = false;
                state.store (empty, std::memory_order_release);
                contentVersion.fetch_add (1, std::memory_order_relaxed);
                break;
            case commandNone: break;
        }
    }

    /** Undo's copy, bounded per block. */
    void continueRestore() noexcept
    {
        const auto len = lengthSamples.load (std::memory_order_relaxed);
        if (restoreRemaining <= 0 || len <= 0)
        {
            restoreRemaining = 0;
            return;
        }
        auto chunk = juce::jmin (restoreRemaining, maxRestorePerBlock);
        restoreRemaining -= chunk;
        auto index = juce::jlimit (0, len - 1, restoreCursor);
        while (chunk-- > 0)
        {
            loop[static_cast<std::size_t> (index)] = previous[static_cast<std::size_t> (index)];
            index += restoreDirection;
            if (index >= len) index = 0;
            if (index < 0) index = len - 1;
        }
        restoreCursor = index;
        if (restoreRemaining == 0)
            contentVersion.fetch_add (1, std::memory_order_relaxed);
    }

    /** One loop slot during an overdub: remember it the first time this session reaches it, then add the input once. */
    void overdubSlot (int slot, float dry, float feedback, int len) noexcept
    {
        if (! sessionFrozen && savedInSession < len)
        {
            const auto distance = sessionDirection > 0 ? (slot - sessionStart + len) % len : (sessionStart - slot + len) % len;
            if (distance == savedInSession)
            {
                previous[static_cast<std::size_t> (slot)] = loop[static_cast<std::size_t> (slot)];
                ++savedInSession;
            }
        }
        auto& value = loop[static_cast<std::size_t> (slot)];
        value = value * feedback + dry;
    }

    void closeLoop() noexcept
    {
        const auto len = juce::jmax (0, juce::jmin (recorded, static_cast<int> (loop.size()) - 4));
        lengthSamples.store (len, std::memory_order_release);
        layers.store (len > 0 ? 1 : 0, std::memory_order_relaxed);
        playhead = 0.0;
        undoAvailable = false;
        state.store (len > 0 ? playing : empty, std::memory_order_release);
        contentVersion.fetch_add (1, std::memory_order_relaxed);
    }

    void beginOverdub() noexcept
    {
        const auto len = lengthSamples.load (std::memory_order_relaxed);
        if (len <= 0)
            return;
        if (restoreRemaining > 0)
            return; // an undo is still copying back; the next press starts the overdub
        sessionStart = juce::jlimit (0, len - 1, static_cast<int> (playhead));
        sessionDirection = reversed.load (std::memory_order_relaxed) ? -1 : 1;
        sessionFrozen = false;
        lastOverdubSlot = -1;
        savedInSession = 0;
        undoAvailable = true;
        layers.store (layers.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        state.store (overdubbing, std::memory_order_release);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        const auto capacity = static_cast<int> (loop.size()) - 4;
        if (capacity < 64)
        {
            juce::FloatVectorOperations::copy (output, input, numSamples);
            return;
        }
        const auto bpm = parameterTarget (4, inputs);
        const auto bars = static_cast<int> (std::round (parameterTarget (5, inputs)));
        const auto* clockIn = clock.pointer (inputs);
        if (const auto command = static_cast<Command> (pendingCommand.exchange (commandNone, std::memory_order_acq_rel)); command != commandNone)
        {
            // Transport commands wait for the clock while one is running;
            // undo and clear are immediate either way.
            if ((command == commandRecord || command == commandPlay) && clock.external (sampleRate))
                queuedCommand = command;
            else
                applyCommand (command, bpm, bars);
        }
        if (queuedCommand != commandNone && ! clock.external (sampleRate))
        {
            applyCommand (queuedCommand, bpm, bars); // the clock went away: do not leave the player hanging
            queuedCommand = commandNone;
        }
        queuedForUi.store (queuedCommand != commandNone, std::memory_order_relaxed);
        continueRestore();

        const bool half = halfSpeed.load (std::memory_order_relaxed);
        const bool backwards = reversed.load (std::memory_order_relaxed);
        const int direction = backwards ? -1 : 1;
        if (state.load (std::memory_order_relaxed) == overdubbing && direction != sessionDirection && ! sessionFrozen)
        {
            sessionFrozen = true;   // REV flipped mid-overdub: undo keeps the run it has, new slots are not remembered
            lastOverdubSlot = -1;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            if (clock.tick (clockIn, sample, sampleRate) && queuedCommand != commandNone)
            {
                applyCommand (queuedCommand, bpm, bars);
                queuedCommand = commandNone;
                queuedForUi.store (false, std::memory_order_relaxed);
            }
            const auto dryGain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            const auto loopGain = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto feedback = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto speed = juce::jlimit (0.25f, 4.0f, parameterValue (3, inputs, sample) * 0.01f) * (half ? 0.5f : 1.0f);
            const auto dry = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            float loopOut = 0.0f;
            const auto current = state.load (std::memory_order_relaxed);

            if (current == recording)
            {
                if (recorded < capacity)
                    loop[static_cast<std::size_t> (recorded++)] = dry;
                if (targetLength > 0 && recorded >= targetLength)
                {
                    recorded = targetLength;
                    closeLoop();
                }
                else if (recorded >= capacity)
                    closeLoop();
                recordedSamples.store (recorded, std::memory_order_relaxed);
            }
            else if (current == playing || current == overdubbing)
            {
                const auto len = lengthSamples.load (std::memory_order_relaxed);
                if (len > 0)
                {
                    const auto indexA = juce::jlimit (0, len - 1, static_cast<int> (playhead));
                    const auto indexB = (indexA + 1) % len;
                    const auto fraction = static_cast<float> (playhead - std::floor (playhead));
                    loopOut = loop[static_cast<std::size_t> (indexA)] + fraction * (loop[static_cast<std::size_t> (indexB)] - loop[static_cast<std::size_t> (indexA)]);
                    if (current == overdubbing && indexA != lastOverdubSlot)
                    {
                        // Once per slot, whatever the speed: half speed does not add the
                        // input twice, fast speed fills the slots it jumps over.
                        auto slot = lastOverdubSlot < 0 ? indexA : lastOverdubSlot + direction;
                        for (int guard = 0; guard < 8; ++guard)
                        {
                            if (slot >= len) slot = 0;
                            if (slot < 0) slot = len - 1;
                            overdubSlot (slot, dry, feedback, len);
                            if (slot == indexA)
                                break;
                            slot += direction;
                        }
                        lastOverdubSlot = indexA;
                    }
                    else if (current != overdubbing)
                        lastOverdubSlot = -1;
                    playhead += backwards ? -speed : speed;
                    if (playhead >= len) playhead -= len;
                    if (playhead < 0.0) playhead += len;
                }
            }
            output[sample] = dry * dryGain + loopOut * loopGain;
        }
        playheadForUi.store (playhead, std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    std::vector<float> loop, previous;
    double playhead = 0.0;
    int recorded = 0, targetLength = 0, sessionStart = 0, savedInSession = 0;
    int sessionDirection = 1, lastOverdubSlot = -1;
    bool sessionFrozen = false;
    int restoreRemaining = 0, restoreCursor = 0, restoreDirection = 1;
    static constexpr int maxRestorePerBlock = 32768;
    bool undoAvailable = false;
    std::atomic<int> state { empty };
    std::atomic<int> pendingCommand { commandNone };
    std::atomic<int> lengthSamples { 0 };
    std::atomic<int> recordedSamples { 0 };
    std::atomic<int> layers { 0 };
    std::atomic<double> playheadForUi { 0.0 };
    std::atomic<bool> halfSpeed { false };
    std::atomic<bool> reversed { false };
    std::atomic<juce::uint32> contentVersion { 0 };
};

// ---------------------------------------------------------------- stereo
// Stereo lives on explicit L/R port pairs; the graph's buffers stay mono, so
// the allocation-free render path is untouched and a stereo path is two
// cables (the UI connects both in one drag). "Sum inputs" makes a mono cable
// into In L feed both channels, which is what a guitar rig wants by default.

class PanNode final : public DspNode
{
public:
    PanNode() : DspNode (NodeKind::pan, nodeKindName (NodeKind::pan))
    {
        addInputPort ("In", SignalType::audio);
        addOutputPort ("Out L", SignalType::audio);
        addOutputPort ("Out R", SignalType::audio);
        addParameter ("pan", "Pan", "", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, 0.5f);
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, 0.25f);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}
    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* left = outputs.getWritePointer (0);
        auto* right = outputs.getWritePointer (1);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto position = juce::jlimit (-1.0f, 1.0f, parameterValue (0, inputs, sample) * 0.01f);
            const auto gain = juce::Decibels::decibelsToGain (parameterValue (1, inputs, sample));
            const auto angle = (position + 1.0f) * 0.25f * juce::MathConstants<float>::pi; // equal power
            const auto dry = std::isfinite (input[sample]) ? input[sample] * gain : 0.0f;
            left[sample] = dry * std::cos (angle);
            right[sample] = dry * std::sin (angle);
        }
    }
};

class StereoMergeNode final : public DspNode
{
public:
    StereoMergeNode() : DspNode (NodeKind::stereoMerge, nodeKindName (NodeKind::stereoMerge))
    {
        addInputPort ("In L", SignalType::audio);
        addInputPort ("In R", SignalType::audio);
        addOutputPort ("Out", SignalType::audio);
        addParameter ("level", "Level", "dB", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, 0.25f);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}
    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        const auto* left = inputs.getReadPointer (0);
        const auto* right = inputs.getReadPointer (1);
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto gain = juce::Decibels::decibelsToGain (parameterValue (0, inputs, sample));
            const auto l = std::isfinite (left[sample]) ? left[sample] : 0.0f;
            const auto r = std::isfinite (right[sample]) ? right[sample] : 0.0f;
            output[sample] = (l + r) * 0.5f * gain;
        }
    }
};

// Shared by the stereo effects: a pair of interpolated delay lines.
struct StereoDelayLines
{
    std::array<std::vector<float>, 2> lines;
    std::array<int, 2> write { 0, 0 };

    void prepare (double sampleRate, double seconds, int maximumBlockSize)
    {
        for (auto& line : lines)
            line.assign (static_cast<std::size_t> (std::ceil (sampleRate * seconds)) + static_cast<std::size_t> (maximumBlockSize) + 4u, 0.0f);
        write = { 0, 0 };
    }
    void clear() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        write = { 0, 0 };
    }
    [[nodiscard]] int size() const noexcept { return static_cast<int> (lines[0].size()); }
    [[nodiscard]] float read (int channel, float delaySamples) const noexcept
    {
        const auto& line = lines[static_cast<std::size_t> (channel)];
        const auto n = static_cast<int> (line.size());
        float position = static_cast<float> (write[static_cast<std::size_t> (channel)]) - juce::jlimit (1.0f, static_cast<float> (n - 3), delaySamples);
        while (position < 0.0f)
            position += static_cast<float> (n);
        const auto a = static_cast<int> (position) % n;
        const auto b = (a + 1) % n;
        const auto fraction = position - std::floor (position);
        return line[static_cast<std::size_t> (a)] + fraction * (line[static_cast<std::size_t> (b)] - line[static_cast<std::size_t> (a)]);
    }
    void push (int channel, float value) noexcept
    {
        auto& line = lines[static_cast<std::size_t> (channel)];
        auto& index = write[static_cast<std::size_t> (channel)];
        line[static_cast<std::size_t> (index)] = value;
        index = (index + 1) % static_cast<int> (line.size());
    }
};

class StereoDelayNode final : public DspNode
{
public:
    StereoDelayNode() : DspNode (NodeKind::stereoDelay, nodeKindName (NodeKind::stereoDelay))
    {
        addInputPort ("In L", SignalType::audio);
        addInputPort ("In R", SignalType::audio);
        addOutputPort ("Out L", SignalType::audio);
        addOutputPort ("Out R", SignalType::audio);
        addParameter ("time-ms", "Time", "ms", skewedRange (1.0f, 2000.0f, 180.0f), 320.0f, 0.4f);
        addParameter ("feedback", "Feedback", "%", juce::NormalisableRange<float> (0.0f, 98.0f, 0.1f), 40.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, 0.5f);
        addParameter ("ping-pong", "Ping-pong", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, 0.5f);
        addParameter ("spread", "R offset", "%", juce::NormalisableRange<float> (50.0f, 150.0f, 0.1f), 100.0f, 0.35f);
        addParameter ("sum-in", "Sum inputs", "", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f, 0.0f, false);
    }

private:
    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        lines.prepare (sampleRate, 2.1 * 1.5, maximumBlockSize);
    }
    void resetDsp() noexcept override { lines.clear(); }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        if (lines.size() < 4)
            return;
        const auto* inL = inputs.getReadPointer (0);
        const auto* inR = inputs.getReadPointer (1);
        auto* outL = outputs.getWritePointer (0);
        auto* outR = outputs.getWritePointer (1);
        // "Sum inputs": a mono cable into In L feeds both sides while In R has no cable.
        const bool monoIn = parameterTarget (5, inputs) > 0.5f && ! isInputConnected (1);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto timeL = parameterValue (0, inputs, sample) * static_cast<float> (sampleRate * 0.001);
            const auto timeR = timeL * juce::jlimit (0.5f, 1.5f, parameterValue (4, inputs, sample) * 0.01f);
            const auto feedback = juce::jlimit (0.0f, 0.98f, parameterValue (1, inputs, sample) * 0.01f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto cross = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);
            auto l = std::isfinite (inL[sample]) ? inL[sample] : 0.0f;
            auto r = std::isfinite (inR[sample]) ? inR[sample] : 0.0f;
            if (monoIn)
                r = l;
            const auto delayedL = lines.read (0, timeL);
            const auto delayedR = lines.read (1, timeR);
            // Ping-pong: each line's feedback comes partly from the other side.
            lines.push (0, std::tanh (l + feedback * (delayedL * (1.0f - cross) + delayedR * cross)));
            lines.push (1, std::tanh (r * (1.0f - cross) + feedback * (delayedR * (1.0f - cross) + delayedL * cross)));
            outL[sample] = l + mix * (delayedL - l);
            outR[sample] = r + mix * (delayedR - r);
        }
    }

    double sampleRate = 48000.0;
    StereoDelayLines lines;
};

class StereoChorusNode final : public DspNode
{
public:
    StereoChorusNode() : DspNode (NodeKind::stereoChorus, nodeKindName (NodeKind::stereoChorus))
    {
        addInputPort ("In L", SignalType::audio);
        addInputPort ("In R", SignalType::audio);
        addOutputPort ("Out L", SignalType::audio);
        addOutputPort ("Out R", SignalType::audio);
        addParameter ("rate", "Rate", "Hz", skewedRange (0.05f, 6.0f, 0.8f), 0.8f, 0.35f);
        addParameter ("depth", "Depth", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 45.0f, 0.5f);
        addParameter ("delay", "Delay", "ms", juce::NormalisableRange<float> (5.0f, 30.0f, 0.1f), 14.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, 0.5f);
        addParameter ("spread", "Spread", "deg", juce::NormalisableRange<float> (0.0f, 180.0f, 1.0f), 90.0f, 0.35f);
        addParameter ("sum-in", "Sum inputs", "", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f, 0.0f, false);
    }

private:
    void prepareDsp (double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        lines.prepare (sampleRate, 0.045, maximumBlockSize);
        phase = 0.0;
    }
    void resetDsp() noexcept override { lines.clear(); phase = 0.0; }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        if (lines.size() < 4)
            return;
        const auto* inL = inputs.getReadPointer (0);
        const auto* inR = inputs.getReadPointer (1);
        auto* outL = outputs.getWritePointer (0);
        auto* outR = outputs.getWritePointer (1);
        // "Sum inputs": a mono cable into In L feeds both sides while In R has no cable.
        const bool monoIn = parameterTarget (5, inputs) > 0.5f && ! isInputConnected (1);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rate = parameterValue (0, inputs, sample);
            const auto depth = juce::jlimit (0.0f, 1.0f, parameterValue (1, inputs, sample) * 0.01f);
            const auto centreMs = parameterValue (2, inputs, sample);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);
            const auto spread = parameterValue (4, inputs, sample) / 360.0f;
            auto l = std::isfinite (inL[sample]) ? inL[sample] : 0.0f;
            auto r = std::isfinite (inR[sample]) ? inR[sample] : 0.0f;
            if (monoIn)
                r = l;
            const auto wobbleL = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
            const auto wobbleR = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * (phase + spread)));
            const auto msL = juce::jmax (1.0f, centreMs + wobbleL * depth * 8.0f) * static_cast<float> (sampleRate * 0.001);
            const auto msR = juce::jmax (1.0f, centreMs + wobbleR * depth * 8.0f) * static_cast<float> (sampleRate * 0.001);
            const auto delayedL = lines.read (0, msL);
            const auto delayedR = lines.read (1, msR);
            lines.push (0, l);
            lines.push (1, r);
            outL[sample] = l + mix * (delayedL - l);
            outR[sample] = r + mix * (delayedR - r);
            phase += rate / sampleRate;
            phase -= std::floor (phase);
        }
    }

    double sampleRate = 48000.0;
    StereoDelayLines lines;
    double phase = 0.0;
};

class StereoReverbNode final : public DspNode
{
public:
    StereoReverbNode() : DspNode (NodeKind::stereoReverb, nodeKindName (NodeKind::stereoReverb))
    {
        addInputPort ("In L", SignalType::audio);
        addInputPort ("In R", SignalType::audio);
        addOutputPort ("Out L", SignalType::audio);
        addOutputPort ("Out R", SignalType::audio);
        addParameter ("size", "Size", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 55.0f, 0.35f);
        addParameter ("damp", "Damping", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, 0.35f);
        addParameter ("width", "Width", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, 0.35f);
        addParameter ("mix", "Mix", "%", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, 0.5f);
    }

private:
    static constexpr std::size_t combCount = 8;
    static constexpr std::size_t allpassCount = 4;
    static constexpr int stereoSpread = 23; // Freeverb's classic right-channel offset

    struct Side
    {
        std::array<std::vector<float>, combCount> combBuffers;
        std::array<int, combCount> combIndices {};
        std::array<float, combCount> combFilters {};
        std::array<std::vector<float>, allpassCount> allpassBuffers;
        std::array<int, allpassCount> allpassIndices {};

        void prepare (double rate, int offset)
        {
            static constexpr int combTunings[combCount] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static constexpr int allpassTunings[allpassCount] { 556, 441, 341, 225 };
            const auto scale = rate / 44100.0;
            for (std::size_t comb = 0; comb < combCount; ++comb)
            {
                combBuffers[comb].assign (static_cast<std::size_t> (juce::jmax (4, juce::roundToInt ((combTunings[comb] + offset) * scale))), 0.0f);
                combIndices[comb] = 0;
                combFilters[comb] = 0.0f;
            }
            for (std::size_t allpass = 0; allpass < allpassCount; ++allpass)
            {
                allpassBuffers[allpass].assign (static_cast<std::size_t> (juce::jmax (4, juce::roundToInt ((allpassTunings[allpass] + offset) * scale))), 0.0f);
                allpassIndices[allpass] = 0;
            }
        }
        void clear() noexcept
        {
            for (std::size_t comb = 0; comb < combCount; ++comb) { std::fill (combBuffers[comb].begin(), combBuffers[comb].end(), 0.0f); combIndices[comb] = 0; combFilters[comb] = 0.0f; }
            for (std::size_t allpass = 0; allpass < allpassCount; ++allpass) { std::fill (allpassBuffers[allpass].begin(), allpassBuffers[allpass].end(), 0.0f); allpassIndices[allpass] = 0; }
        }
        float process (float fed, float damp, float roomFeedback) noexcept
        {
            float wet = 0.0f;
            for (std::size_t comb = 0; comb < combCount; ++comb)
            {
                auto& buffer = combBuffers[comb];
                auto& index = combIndices[comb];
                const auto delayed = buffer[static_cast<std::size_t> (index)];
                wet += delayed;
                combFilters[comb] = delayed * (1.0f - damp) + combFilters[comb] * damp;
                if (! std::isfinite (combFilters[comb]))
                    combFilters[comb] = 0.0f;
                buffer[static_cast<std::size_t> (index)] = fed + combFilters[comb] * roomFeedback;
                if (++index >= static_cast<int> (buffer.size()))
                    index = 0;
            }
            for (std::size_t allpass = 0; allpass < allpassCount; ++allpass)
            {
                auto& buffer = allpassBuffers[allpass];
                auto& index = allpassIndices[allpass];
                const auto buffered = buffer[static_cast<std::size_t> (index)];
                buffer[static_cast<std::size_t> (index)] = wet + buffered * 0.5f;
                wet = buffered - wet;
                if (++index >= static_cast<int> (buffer.size()))
                    index = 0;
            }
            return wet;
        }
    };

    void prepareDsp (double newSampleRate, int) override
    {
        left.prepare (newSampleRate, 0);
        right.prepare (newSampleRate, stereoSpread);
    }
    void resetDsp() noexcept override { left.clear(); right.clear(); }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        if (left.combBuffers[0].empty())
            return;
        const auto* inL = inputs.getReadPointer (0);
        const auto* inR = inputs.getReadPointer (1);
        auto* outL = outputs.getWritePointer (0);
        auto* outR = outputs.getWritePointer (1);
        const bool monoIn = ! isInputConnected (1); // a mono cable into In L feeds both sides
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto size = juce::jlimit (0.0f, 1.0f, parameterValue (0, inputs, sample) * 0.01f);
            const auto damp = juce::jlimit (0.0f, 0.95f, parameterValue (1, inputs, sample) * 0.01f * 0.9f);
            const auto width = juce::jlimit (0.0f, 1.0f, parameterValue (2, inputs, sample) * 0.01f);
            const auto mix = juce::jlimit (0.0f, 1.0f, parameterValue (3, inputs, sample) * 0.01f);
            const auto roomFeedback = 0.72f + 0.26f * size;
            const auto l = std::isfinite (inL[sample]) ? inL[sample] : 0.0f;
            const auto r = monoIn ? l : (std::isfinite (inR[sample]) ? inR[sample] : 0.0f);
            const auto fed = (l + r) * 0.0075f; // Freeverb sums the input
            const auto wetL = left.process (fed, damp, roomFeedback);
            const auto wetR = right.process (fed, damp, roomFeedback);
            const auto wet1 = (1.0f + width) * 0.5f, wet2 = (1.0f - width) * 0.5f;
            const auto mixedL = wetL * wet1 + wetR * wet2;
            const auto mixedR = wetR * wet1 + wetL * wet2;
            outL[sample] = l + mix * (mixedL - l);
            outR[sample] = r + mix * (mixedR - r);
        }
    }

    Side left, right;
};

// Tuner: passes audio through and shows note / cents. Detection is YIN on a
// snapshot of the last ~85 ms, run on the message thread when the UI asks
// (statusText / currentStep), so the audio callback only copies samples.
class TunerNode final : public DspNode
{
public:
    TunerNode() : DspNode (NodeKind::tuner, nodeKindName (NodeKind::tuner))
    {
        addInputPort ("In", SignalType::audio);
        addOutputPort ("Out", SignalType::audio);
        addParameter ("reference", "A =", "Hz", juce::NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f, 0.0f, false);
    }

    juce::String statusText() const override
    {
        analyse();
        if (detectedHz <= 0.0f)
            return "- - -";
        return noteName + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + " cents   " + juce::String (detectedHz, 1) + " Hz";
    }

    /** Needle position 0-100 (50 = in tune), -1 when nothing is detected. */
    int currentStep() const noexcept override
    {
        analyse();
        return detectedHz > 0.0f ? juce::jlimit (0, 100, 50 + cents) : -1;
    }

private:
    static constexpr int frameSize = 2048;   // analysis frame after decimation
    int windowSize = 4096;                   // ring length: frameSize x decimation, ~85 ms at any rate
    int decimation = 2;

    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        decimation = juce::jmax (2, juce::nextPowerOfTwo (juce::roundToInt (newSampleRate / 24000.0)));
        windowSize = frameSize * decimation;
        ring.assign (static_cast<std::size_t> (windowSize), 0.0f);
        writeIndex.store (0, std::memory_order_relaxed);
    }
    void resetDsp() noexcept override {}

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        const auto* input = inputs.getReadPointer (0);
        auto* output = outputs.getWritePointer (0);
        auto index = writeIndex.load (std::memory_order_relaxed);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto value = std::isfinite (input[sample]) ? input[sample] : 0.0f;
            output[sample] = value;
            ring[static_cast<std::size_t> (index)] = value;
            index = (index + 1) % windowSize;
        }
        writeIndex.store (index, std::memory_order_release);
    }

    // Message thread. Cached for 40 ms so a busy UI does not repeat the work.
    void analyse() const
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastAnalysis < 40.0 || ring.empty())
            return;
        lastAnalysis = now;
        // Snapshot, decimated to about 24 kHz (plenty for guitar and bass).
        std::array<float, frameSize> frame {};
        const auto start = writeIndex.load (std::memory_order_acquire);
        float energy = 0.0f;
        for (int i = 0; i < frameSize; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < decimation; ++k)
                sum += ring[static_cast<std::size_t> ((start + decimation * i + k) % windowSize)];
            frame[static_cast<std::size_t> (i)] = sum / static_cast<float> (decimation);
            energy += frame[static_cast<std::size_t> (i)] * frame[static_cast<std::size_t> (i)];
        }
        if (energy / frameSize < 1.0e-6f) // silence
        {
            detectedHz = 0.0f;
            return;
        }
        const auto rate = sampleRate / decimation;
        const int minLag = juce::jmax (2, static_cast<int> (rate / 1200.0));  // 1.2 kHz
        const int maxLag = juce::jmin (frameSize / 2, static_cast<int> (rate / 27.5)); // A0
        // YIN difference function with cumulative mean normalisation.
        static thread_local std::vector<float> d;
        d.assign (static_cast<std::size_t> (maxLag + 1), 0.0f);
        const int half = frameSize / 2;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float sum = 0.0f;
            for (int i = 0; i < half; ++i)
            {
                const auto delta = frame[static_cast<std::size_t> (i)] - frame[static_cast<std::size_t> (i + lag)];
                sum += delta * delta;
            }
            d[static_cast<std::size_t> (lag)] = sum;
        }
        // Cumulative mean normalisation over the whole range first, then the
        // search: comparing normalised against raw values sent the walk astray.
        float running = 0.0f;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            running += d[static_cast<std::size_t> (lag)];
            d[static_cast<std::size_t> (lag)] = running > 0.0f ? d[static_cast<std::size_t> (lag)] * static_cast<float> (lag - minLag + 1) / running : 1.0f;
        }
        int best = -1;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            if (d[static_cast<std::size_t> (lag)] < 0.15f)
            {
                best = lag; // first dip under the threshold: walk to its bottom
                while (best + 1 <= maxLag && d[static_cast<std::size_t> (best + 1)] < d[static_cast<std::size_t> (best)])
                    ++best;
                break;
            }
        }
        if (best < 0)
        {
            detectedHz = 0.0f;
            return;
        }
        // Parabolic interpolation around the minimum.
        auto period = static_cast<double> (best);
        if (best > minLag && best < maxLag)
        {
            const auto s0 = d[static_cast<std::size_t> (best - 1)], s1 = d[static_cast<std::size_t> (best)], s2 = d[static_cast<std::size_t> (best + 1)];
            const auto denominator = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denominator) > 1.0e-9f)
                period += (s2 - s0) / denominator;
        }
        const auto hz = static_cast<float> (rate / period);
        // Smooth a little between analyses so the needle does not jitter.
        detectedHz = detectedHz > 0.0f && std::abs (hz - detectedHz) < detectedHz * 0.06f ? detectedHz * 0.6f + hz * 0.4f : hz;
        const auto reference = getParameter (0).getValue();
        const auto midi = 69.0 + 12.0 * std::log2 (detectedHz / reference);
        const auto nearest = static_cast<int> (std::round (midi));
        cents = juce::jlimit (-50, 50, static_cast<int> (std::round ((midi - nearest) * 100.0)));
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        noteName = juce::String (names[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1);
    }

    double sampleRate = 48000.0;
    std::vector<float> ring;
    std::atomic<int> writeIndex { 0 };
    mutable double lastAnalysis = 0.0;
    mutable float detectedHz = 0.0f;
    mutable int cents = 0;
    mutable juce::String noteName;
};

// Transport for everything that steps: one tempo, pulses for beats, eighths
// and bars, so the drum machine, the sequencer and the looper agree. Tap
// tempo and reset arrive on the message thread; the pulses are written per
// sample (2 ms high) so a follower can catch the edge exactly.
class ClockNode final : public DspNode
{
public:
    ClockNode() : DspNode (NodeKind::clock, nodeKindName (NodeKind::clock))
    {
        addOutputPort ("Beat", SignalType::control);
        addOutputPort ("Eighth", SignalType::control);
        addOutputPort ("Bar", SignalType::control);
        addParameter ("bpm", "Tempo", "bpm", juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f, 0.25f);
        addParameter ("beats", "Beats/bar", "", juce::NormalisableRange<float> (1.0f, 8.0f, 1.0f), 4.0f, 0.0f, false);
    }

    bool handleUiCommand (const juce::String& command) override
    {
        if (command == "run")
        {
            running.store (! running.load (std::memory_order_relaxed), std::memory_order_relaxed);
            return true;
        }
        if (command == "reset")
        {
            resetRequested.store (true, std::memory_order_release);
            return true;
        }
        if (command != "tap")
            return false;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (tapCount > 0 && now - tapTimes[static_cast<std::size_t> ((tapCount - 1) % tapTimes.size())] > 2000.0)
            tapCount = 0;
        tapTimes[static_cast<std::size_t> (tapCount % tapTimes.size())] = now;
        ++tapCount;
        const auto samples = juce::jmin (tapCount, static_cast<int> (tapTimes.size()));
        if (samples >= 2)
        {
            const auto newest = tapTimes[static_cast<std::size_t> ((tapCount - 1) % tapTimes.size())];
            const auto oldest = tapTimes[static_cast<std::size_t> ((tapCount - samples) % tapTimes.size())];
            const auto interval = (newest - oldest) / static_cast<double> (samples - 1);
            if (interval > 0.0)
                getParameter (0).setValue (static_cast<float> (60000.0 / interval));
        }
        return true;
    }

    bool uiToggleState (const juce::String& command) const override
    {
        return command == "run" && running.load (std::memory_order_relaxed);
    }

    int currentStep() const noexcept override { return beatForUi.load (std::memory_order_relaxed); }

    juce::String statusText() const override
    {
        return juce::String (getParameter (0).getValue(), 1) + " bpm   beat " + juce::String (beatForUi.load (std::memory_order_relaxed) + 1);
    }

private:
    void prepareDsp (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        resetDsp();
    }

    void resetDsp() noexcept override
    {
        beatPhase = 0.0;
        beatCount = 0;
        beatPulse = eighthPulse = barPulse = 0;
        primed = false;
        beatForUi.store (0, std::memory_order_relaxed);
    }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        auto* beat = outputs.getWritePointer (0);
        auto* eighth = outputs.getWritePointer (1);
        auto* bar = outputs.getWritePointer (2);
        const auto pulseLength = juce::jmax (16, static_cast<int> (sampleRate * 0.002));
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            beatPhase = 0.0;
            beatCount = 0;
            primed = false;
        }
        const bool run = running.load (std::memory_order_relaxed);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto bpm = juce::jlimit (40.0f, 240.0f, parameterValue (0, inputs, sample));
            const auto beatsPerBar = juce::jlimit (1, 8, static_cast<int> (std::round (parameterValue (1, inputs, sample))));
            if (run)
            {
                if (! primed)
                {
                    // The first sample after start / reset is the downbeat.
                    primed = true;
                    beatPulse = eighthPulse = barPulse = pulseLength;
                    beatForUi.store (0, std::memory_order_relaxed);
                }
                else
                {
                    const auto before = beatPhase;
                    beatPhase += static_cast<double> (bpm) / 60.0 / sampleRate;
                    if (before < 0.5 && beatPhase >= 0.5)
                        eighthPulse = pulseLength;
                    if (beatPhase >= 1.0)
                    {
                        beatPhase -= 1.0;
                        beatCount = (beatCount + 1) % beatsPerBar;
                        beatPulse = eighthPulse = pulseLength;
                        if (beatCount == 0)
                            barPulse = pulseLength;
                        beatForUi.store (beatCount, std::memory_order_relaxed);
                    }
                }
            }
            beat[sample] = beatPulse > 0 ? 1.0f : 0.0f;
            eighth[sample] = eighthPulse > 0 ? 1.0f : 0.0f;
            bar[sample] = barPulse > 0 ? 1.0f : 0.0f;
            if (beatPulse > 0) --beatPulse;
            if (eighthPulse > 0) --eighthPulse;
            if (barPulse > 0) --barPulse;
        }
    }

    double sampleRate = 48000.0;
    double beatPhase = 0.0;
    int beatCount = 0;
    int beatPulse = 0, eighthPulse = 0, barPulse = 0;
    bool primed = false;
    std::atomic<bool> running { true };
    std::atomic<bool> resetRequested { false };
    std::atomic<int> beatForUi { 0 };
    std::array<double, 4> tapTimes {};
    int tapCount = 0;
};

// MIDI Note: turns keyboard notes into control signals for the synth and
// pluck. Gate is 1 while a note is held (last-note priority), Pitch is
// (note - base) / 48 so a mod depth of 100% on a +/-24 st pitch knob gives
// exact semitones, Velocity is 0-1. Events arrive on the audio thread
// through the plan, so timing is block-accurate.
class MidiNoteNode final : public DspNode
{
public:
    MidiNoteNode() : DspNode (NodeKind::midiNote, nodeKindName (NodeKind::midiNote))
    {
        addOutputPort ("Gate", SignalType::control);
        addOutputPort ("Pitch", SignalType::control);
        addOutputPort ("Velocity", SignalType::control);
        // Base 45 = A2: the synth and pluck read 0 on Pitch as 110 Hz, so keys play at their real pitch.
        addParameter ("base", "Base note", "", juce::NormalisableRange<float> (21.0f, 84.0f, 1.0f), 45.0f, 0.0f, false);
        addParameter ("channel", "Channel", "", juce::NormalisableRange<float> (0.0f, 16.0f, 1.0f), 0.0f, 0.0f, false);
    }

    void handleMidiNote (int channel, int note, int velocity, bool on) noexcept override
    {
        const auto wanted = static_cast<int> (std::round (getParameter (1).getValue()));
        if (wanted != 0 && channel != wanted)
            return;
        auto remove = [this] (int which)
        {
            for (int i = 0; i < heldCount; ++i)
                if (held[static_cast<std::size_t> (i)] == which)
                {
                    for (int j = i; j + 1 < heldCount; ++j)
                        held[static_cast<std::size_t> (j)] = held[static_cast<std::size_t> (j + 1)];
                    --heldCount;
                    return;
                }
        };
        if (on)
        {
            remove (note);
            if (heldCount == static_cast<int> (held.size()))
                remove (held[0]); // oldest falls off the stack
            held[static_cast<std::size_t> (heldCount++)] = note;
            retrigger = true; // a new key always makes a gate edge, legato included
            currentNote.store (note, std::memory_order_relaxed);
            currentVelocity.store (velocity, std::memory_order_relaxed);
        }
        else
        {
            remove (note);
            currentNote.store (heldCount > 0 ? held[static_cast<std::size_t> (heldCount - 1)] : -1, std::memory_order_relaxed);
        }
    }

    juce::String statusText() const override
    {
        const auto note = currentNote.load (std::memory_order_relaxed);
        if (note < 0)
            return "waiting for notes";
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1) + "  vel " + juce::String (currentVelocity.load (std::memory_order_relaxed));
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override { heldCount = 0; currentNote.store (-1, std::memory_order_relaxed); }

    void processDsp (const juce::AudioBuffer<float>& inputs, juce::AudioBuffer<float>& outputs, int numSamples) noexcept override
    {
        const auto note = currentNote.load (std::memory_order_relaxed);
        const auto base = parameterTarget (0, inputs);
        const auto gate = note >= 0 ? 1.0f : 0.0f;
        const auto pitch = note >= 0 ? juce::jlimit (-1.0f, 1.0f, (static_cast<float> (note) - base) / 48.0f) : lastPitch;
        const auto velocity = static_cast<float> (currentVelocity.load (std::memory_order_relaxed)) / 127.0f;
        lastPitch = pitch; // keep the pitch through the release so the tail does not jump
        juce::FloatVectorOperations::fill (outputs.getWritePointer (0), gate, numSamples);
        if (retrigger && gate > 0.0f)
            juce::FloatVectorOperations::clear (outputs.getWritePointer (0), juce::jmin (numSamples, 16)); // short low so followers see an edge
        retrigger = false;
        juce::FloatVectorOperations::fill (outputs.getWritePointer (1), pitch, numSamples);
        juce::FloatVectorOperations::fill (outputs.getWritePointer (2), velocity, numSamples);
    }

    std::array<int, 8> held {};
    int heldCount = 0;
    float lastPitch = 0.0f;
    bool retrigger = false;
    std::atomic<int> currentNote { -1 };
    std::atomic<int> currentVelocity { 0 };
};

std::shared_ptr<DspNode> createNodeProcessor (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::gain:                 return std::make_shared<GainNode>();
        case NodeKind::mixer:                return std::make_shared<MixerNode>();
        case NodeKind::crossfade:            return std::make_shared<CrossfadeNode>();
        case NodeKind::distortion:           return std::make_shared<DistortionNode>();
        case NodeKind::filter:               return std::make_shared<FilterNode>();
        case NodeKind::delay:                return std::make_shared<DelayNode>();
        case NodeKind::reverb:               return std::make_shared<ReverbNode>();
        case NodeKind::chorus:               return std::make_shared<ChorusNode>();
        case NodeKind::phaser:               return std::make_shared<PhaserNode>();
        case NodeKind::tremolo:              return std::make_shared<TremoloNode>();
        case NodeKind::bitcrusher:           return std::make_shared<BitcrusherNode>();
        case NodeKind::ringMod:              return std::make_shared<RingModNode>();
        case NodeKind::vowelFilter:          return std::make_shared<VowelFilterNode>();
        case NodeKind::pitchShifter:         return std::make_shared<PitchShifterNode>();
        case NodeKind::vocoder:              return std::make_shared<VocoderNode>();
        case NodeKind::pitchCorrector:       return std::make_shared<PitchCorrectorNode>();
        case NodeKind::granular:             return std::make_shared<GranularNode>();
        case NodeKind::compressor:           return std::make_shared<CompressorNode>();
        case NodeKind::limiter:              return std::make_shared<LimiterNode>();
        case NodeKind::gate:                 return std::make_shared<GateNode>();
        case NodeKind::feedbackGuard:        return std::make_shared<FeedbackGuardNode>();
        case NodeKind::monoSynth:            return std::make_shared<MonoSynthNode>();
        case NodeKind::noiseSource:          return std::make_shared<NoiseSourceNode>();
        case NodeKind::pluck:                return std::make_shared<PluckNode>();
        case NodeKind::drumMachine:          return std::make_shared<DrumMachineNode>();
        case NodeKind::sampler:              return std::make_shared<SamplerNode>();
        case NodeKind::fourTrack:            return std::make_shared<FourTrackNode>();
        case NodeKind::lfo:                  return std::make_shared<LfoNode>();
        case NodeKind::randomLfo:            return std::make_shared<RandomLfoNode>();
        case NodeKind::envelopeFollower:     return std::make_shared<EnvelopeFollowerNode>();
        case NodeKind::stepSequencer:        return std::make_shared<StepSequencerNode>();
        case NodeKind::macro:                return std::make_shared<MacroNode>();
        case NodeKind::spectralFollower:     return std::make_shared<SpectralFollowerNode>();
        case NodeKind::script:               return std::make_shared<ScriptNode>();
        case NodeKind::neuralAmpPlaceholder: return std::make_shared<NeuralAmpNode> (NodeKind::neuralAmpPlaceholder);
        case NodeKind::neuralPedal:          return std::make_shared<NeuralAmpNode> (NodeKind::neuralPedal);
        case NodeKind::cabinet:              return std::make_shared<CabinetNode>();
        case NodeKind::looper:               return std::make_shared<LooperNode>();
        case NodeKind::pan:                  return std::make_shared<PanNode>();
        case NodeKind::stereoMerge:          return std::make_shared<StereoMergeNode>();
        case NodeKind::stereoDelay:          return std::make_shared<StereoDelayNode>();
        case NodeKind::stereoChorus:         return std::make_shared<StereoChorusNode>();
        case NodeKind::stereoReverb:         return std::make_shared<StereoReverbNode>();
        case NodeKind::tuner:                return std::make_shared<TunerNode>();
        case NodeKind::midiNote:             return std::make_shared<MidiNoteNode>();
        case NodeKind::clock:                return std::make_shared<ClockNode>();
        case NodeKind::hardwareInput:
        case NodeKind::hardwareOutput:       break;
    }
    return {};
}

std::shared_ptr<DspNode> createHardwareInputProcessor (const juce::StringArray& channelNames,
                                                       const std::vector<int>& callbackChannels)
{
    return std::make_shared<HardwareInputNode> (channelNames, callbackChannels);
}

std::shared_ptr<DspNode> createHardwareOutputProcessor (const juce::StringArray& channelNames,
                                                        const std::vector<int>& callbackChannels)
{
    return std::make_shared<HardwareOutputNode> (channelNames, callbackChannels);
}
} // namespace signalpatch
