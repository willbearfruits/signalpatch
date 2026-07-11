#include "../src/audio/Graph.h"
#include "../src/audio/Processors.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
 #include <malloc.h> // _aligned_malloc / _aligned_free
#endif

// ---------------------------------------------------------------------------
// Real-time allocation trap. While armed, every route through the global
// allocator counts as a violation; the render-path test asserts zero. This
// implements the REALTIME_SAFETY.md gate "run an allocation/deallocation trap
// around the callback in test builds".
// Break on rtTrapViolation in a debugger to see exactly which callback-path
// code allocated.
#if defined(_MSC_VER)
extern "C" __declspec (noinline) void rtTrapViolation()
{
    static volatile int keepAlive = 0;
    keepAlive = keepAlive + 1; // prevent the empty function from folding away
}
#else
extern "C" __attribute__ ((noinline)) void rtTrapViolation()
{
    asm volatile (""); // keep the call site alive under optimization
}
#endif

namespace rtTrap
{
std::atomic<bool> armed { false };
std::atomic<int> violations { 0 };

inline void note() noexcept
{
    if (armed.load (std::memory_order_relaxed))
    {
        violations.fetch_add (1, std::memory_order_relaxed);
        rtTrapViolation();
    }
}

// Plain allocations pair with std::free; over-aligned ones pair with the
// platform's aligned allocator (the compiler always calls the matching
// aligned delete for over-aligned types, so the pairs never mix).
inline void* alignedAlloc (std::size_t size, std::size_t alignment) noexcept
{
#if defined(_WIN32)
    return _aligned_malloc (size == 0 ? 1 : size, alignment);
#else
    void* pointer = nullptr;
    const auto safeAlignment = alignment < sizeof (void*) ? sizeof (void*) : alignment;
    if (posix_memalign (&pointer, safeAlignment, size == 0 ? 1 : size) != 0)
        return nullptr;
    return pointer;
#endif
}

inline void alignedFree (void* pointer) noexcept
{
#if defined(_WIN32)
    _aligned_free (pointer);
#else
    std::free (pointer);
#endif
}
} // namespace rtTrap

// GCC cannot see that these replacement operators are internally consistent
// (plain new is malloc-backed, so free() is its correct pair).
#if defined(__GNUC__) && ! defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new (std::size_t size)
{
    rtTrap::note();
    if (auto* pointer = std::malloc (size == 0 ? 1 : size))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void* operator new (std::size_t size, std::align_val_t alignment)
{
    rtTrap::note();
    if (auto* pointer = rtTrap::alignedAlloc (size, static_cast<std::size_t> (alignment)))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

void operator delete (void* pointer) noexcept { rtTrap::note(); std::free (pointer); }
void operator delete[] (void* pointer) noexcept { rtTrap::note(); std::free (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { rtTrap::note(); std::free (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept { rtTrap::note(); std::free (pointer); }
void operator delete (void* pointer, std::align_val_t) noexcept { rtTrap::note(); rtTrap::alignedFree (pointer); }
void operator delete[] (void* pointer, std::align_val_t) noexcept { rtTrap::note(); rtTrap::alignedFree (pointer); }
void operator delete (void* pointer, std::size_t, std::align_val_t) noexcept { rtTrap::note(); rtTrap::alignedFree (pointer); }
void operator delete[] (void* pointer, std::size_t, std::align_val_t) noexcept { rtTrap::note(); rtTrap::alignedFree (pointer); }

#if defined(__GNUC__) && ! defined(__clang__)
 #pragma GCC diagnostic pop
#endif

namespace
{
using namespace signalpatch;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void expectOk (const juce::Result& result, const std::string& context)
{
    if (result.failed())
        throw std::runtime_error (context + ": " + result.getErrorMessage().toStdString());
}

juce::StringArray channelNames (const juce::String& prefix, int count)
{
    juce::StringArray names;
    for (int channel = 0; channel < count; ++channel)
        names.add (prefix + " " + juce::String (channel + 1));
    return names;
}

bool hasConnection (const PatchDocument& document, const Connection& expected)
{
    const auto& connections = document.getConnections();
    return std::find (connections.begin(), connections.end(), expected) != connections.end();
}

class NonFiniteControlNode final : public DspNode
{
public:
    NonFiniteControlNode() : DspNode (NodeKind::lfo, "Non-finite test control")
    {
        addOutputPort ("Control", SignalType::control);
    }

private:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override {}

    void processDsp (const juce::AudioBuffer<float>&,
                     juce::AudioBuffer<float>& outputs,
                     int numSamples) noexcept override
    {
        auto* output = outputs.getWritePointer (0);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            switch (sample % 4)
            {
                case 0:  output[sample] = std::numeric_limits<float>::quiet_NaN(); break;
                case 1:  output[sample] = std::numeric_limits<float>::infinity(); break;
                case 2:  output[sample] = -std::numeric_limits<float>::infinity(); break;
                default: output[sample] = std::numeric_limits<float>::max(); break;
            }
        }
    }
};

void testDynamicHardwarePortCounts()
{
    PatchDocument document;
    const auto inputs = channelNames ("Input", 6);
    const auto outputs = channelNames ("Output", 4);

    document.configureHardware (inputs, outputs);

    const auto* inputNode = document.findNode (PatchDocument::hardwareInputId);
    const auto* outputNode = document.findNode (PatchDocument::hardwareOutputId);
    expect (inputNode != nullptr, "hardware input node is missing");
    expect (outputNode != nullptr, "hardware output node is missing");
    expect (inputNode->processor->getNumOutputPorts() == 6,
            "hardware input node did not expose all six device inputs");
    expect (outputNode->processor->getNumInputPorts() == 4,
            "hardware output node did not expose the device output count");
    expect (outputNode->processor->getNumInputPorts() >= 2,
            "hardware output node must expose at least two outputs for this device");

    for (int channel = 0; channel < inputs.size(); ++channel)
    {
        const auto& port = inputNode->processor->getOutputPort (channel);
        expect (port.name == inputs[channel], "hardware input port name was not preserved");
        expect (port.type == SignalType::audio, "hardware input port is not audio typed");
    }

    for (int channel = 0; channel < outputs.size(); ++channel)
    {
        const auto& port = outputNode->processor->getInputPort (channel);
        expect (port.name == outputs[channel], "hardware output port name was not preserved");
        expect (port.type == SignalType::audio, "hardware output port is not audio typed");
    }
}

void testStraightThroughRendering()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 6), channelNames ("Output", 2));
    document.prepareAll (sampleRate, blockSize);

    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0,
                                         PatchDocument::hardwareOutputId, 0 }),
              "could not connect input 1 to output 1");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 5,
                                         PatchDocument::hardwareOutputId, 1 }),
              "could not connect input 6 to output 2");

    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "straight-through graph did not compile: "
                                  + compiled.error.toStdString());

    std::vector<std::vector<float>> inputStorage (6, std::vector<float> (blockSize, 0.0f));
    std::vector<std::vector<float>> outputStorage (2, std::vector<float> (blockSize, -9.0f));
    for (int sample = 0; sample < blockSize; ++sample)
    {
        inputStorage[0][static_cast<std::size_t> (sample)] =
            0.4f * std::sin (static_cast<float> (sample) * 0.17f);
        inputStorage[5][static_cast<std::size_t> (sample)] =
            -0.35f + 0.005f * static_cast<float> (sample);
    }

    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    for (const auto& channel : inputStorage)
        inputPointers.push_back (channel.data());
    for (auto& channel : outputStorage)
        outputPointers.push_back (channel.data());

    compiled.plan->render (inputPointers.data(), static_cast<int> (inputPointers.size()),
                           outputPointers.data(), static_cast<int> (outputPointers.size()),
                           0, blockSize);

    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto index = static_cast<std::size_t> (sample);
        expect (std::abs (outputStorage[0][index] - inputStorage[0][index]) < 1.0e-6f,
                "output 1 did not render input 1 sample-for-sample");
        expect (std::abs (outputStorage[1][index] - inputStorage[5][index]) < 1.0e-6f,
                "output 2 did not render input 6 sample-for-sample");
    }
}

void testSparsePhysicalCallbackMapping()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Physical input", 6),
                                channelNames ("Physical output", 4),
                                { -1, 0, -1, 1, -1, 2 },
                                { 1, -1, 0, -1 });
    document.prepareAll (sampleRate, blockSize);

    const auto* hardwareInput = document.findNode (PatchDocument::hardwareInputId);
    const auto* hardwareOutput = document.findNode (PatchDocument::hardwareOutputId);
    expect (hardwareInput != nullptr && hardwareOutput != nullptr,
            "sparse hardware nodes are missing");
    expect (! hardwareInput->processor->getOutputPort (0).active,
            "inactive physical input was marked active");
    expect (hardwareInput->processor->getOutputPort (3).callbackChannelIndex == 1,
            "physical input 4 did not map to packed callback input 2");
    expect (hardwareOutput->processor->getInputPort (0).callbackChannelIndex == 1,
            "physical output 1 did not map to packed callback output 2");
    expect (! hardwareOutput->processor->getInputPort (1).active,
            "inactive physical output was marked active");

    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 3,
                                         PatchDocument::hardwareOutputId, 0 }),
              "could not route sparse physical input 4 to physical output 1");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 5,
                                         PatchDocument::hardwareOutputId, 2 }),
              "could not route sparse physical input 6 to physical output 3");

    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "sparse hardware graph did not compile: "
                                  + compiled.error.toStdString());

    std::vector<std::vector<float>> callbackInputs (3, std::vector<float> (blockSize));
    std::vector<std::vector<float>> callbackOutputs (2, std::vector<float> (blockSize, -4.0f));
    for (int sample = 0; sample < blockSize; ++sample)
    {
        callbackInputs[0][static_cast<std::size_t> (sample)] = 0.1f;
        callbackInputs[1][static_cast<std::size_t> (sample)] = 0.2f + 0.001f * static_cast<float> (sample);
        callbackInputs[2][static_cast<std::size_t> (sample)] = -0.3f + 0.001f * static_cast<float> (sample);
    }

    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    for (const auto& channel : callbackInputs)
        inputPointers.push_back (channel.data());
    for (auto& channel : callbackOutputs)
        outputPointers.push_back (channel.data());

    compiled.plan->render (inputPointers.data(), static_cast<int> (inputPointers.size()),
                           outputPointers.data(), static_cast<int> (outputPointers.size()),
                           0, blockSize);

    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto index = static_cast<std::size_t> (sample);
        expect (std::abs (callbackOutputs[1][index] - callbackInputs[1][index]) < 1.0e-6f,
                "physical input 4 did not reach packed callback output 2");
        expect (std::abs (callbackOutputs[0][index] - callbackInputs[2][index]) < 1.0e-6f,
                "physical input 6 did not reach packed callback output 1");
    }
}

void testSavedSixInputRouteOnTwoInputLayout()
{
    const Connection savedRoute { PatchDocument::hardwareInputId, 5,
                                  PatchDocument::hardwareOutputId, 0 };

    PatchDocument source;
    source.configureHardware (channelNames ("Six-input device", 6), channelNames ("Output", 2));
    expectOk (source.addConnection (savedRoute), "could not create six-input saved route");
    const auto encoded = juce::JSON::toString (source.toJson(), true);
    const auto parsed = juce::JSON::parse (encoded);
    expect (! parsed.isVoid(), "six-input patch did not parse after serialization");

    PatchDocument restored;
    restored.configureHardware (channelNames ("Two-input device", 2), channelNames ("Output", 2));
    expectOk (restored.loadJson (parsed), "could not load six-input patch on two-input layout");

    const auto* hardwareInput = restored.findNode (PatchDocument::hardwareInputId);
    expect (hardwareInput != nullptr, "restored hardware input node is missing");
    expect (hardwareInput->processor->getNumOutputPorts() == 6,
            "missing saved inputs were not retained as placeholder ports");
    const auto& placeholder = hardwareInput->processor->getOutputPort (5);
    expect (! placeholder.active && placeholder.callbackChannelIndex == -1,
            "saved input 6 placeholder was incorrectly connected to the two-input callback");
    expect (placeholder.name.containsIgnoreCase ("missing"),
            "saved input 6 placeholder is not visibly marked missing");
    expect (hasConnection (restored, savedRoute),
            "route from saved input 6 was dropped on the two-input layout");

    restored.prepareAll (sampleRate, blockSize);
    auto compiled = GraphCompiler::compile (restored, blockSize);
    expect (compiled.succeeded(), "patch with disconnected placeholder route did not compile: "
                                  + compiled.error.toStdString());

    std::vector<std::vector<float>> inputs (2, std::vector<float> (blockSize, 0.6f));
    std::vector<std::vector<float>> outputs (2, std::vector<float> (blockSize, 9.0f));
    std::vector<const float*> inputPointers { inputs[0].data(), inputs[1].data() };
    std::vector<float*> outputPointers { outputs[0].data(), outputs[1].data() };
    compiled.plan->render (inputPointers.data(), 2, outputPointers.data(), 2, 0, blockSize);
    for (const auto sample : outputs[0])
        expect (std::abs (sample) < 1.0e-7f,
                "disconnected input 6 placeholder unexpectedly read a live callback channel");
}

void testSixToTwoToSixReconfigurationRetainsRoute()
{
    const Connection route { PatchDocument::hardwareInputId, 5,
                             PatchDocument::hardwareOutputId, 0 };
    PatchDocument document;
    document.configureHardware (channelNames ("Six-input device", 6), channelNames ("Output", 2));
    expectOk (document.addConnection (route), "could not create route from physical input 6");

    document.configureHardware (channelNames ("Two-input device", 2), channelNames ("Output", 2));
    const auto* shrunkInput = document.findNode (PatchDocument::hardwareInputId);
    expect (shrunkInput != nullptr && shrunkInput->processor->getNumOutputPorts() == 6,
            "six-input layout was not retained while the two-input device was active");
    expect (! shrunkInput->processor->getOutputPort (5).active,
            "unavailable input 6 did not become disconnected");
    expect (hasConnection (document, route), "input 6 route was dropped during 6-to-2 reconfiguration");

    document.configureHardware (channelNames ("Six-input device restored", 6), channelNames ("Output", 2));
    const auto* expandedInput = document.findNode (PatchDocument::hardwareInputId);
    expect (expandedInput != nullptr, "expanded hardware input node is missing");
    const auto& restoredPort = expandedInput->processor->getOutputPort (5);
    expect (restoredPort.active && restoredPort.callbackChannelIndex == 5,
            "restored physical input 6 did not reconnect to callback input 6");
    expect (! restoredPort.name.containsIgnoreCase ("missing"),
            "restored physical input 6 kept its missing placeholder label");
    expect (hasConnection (document, route), "input 6 route was not retained after 6-to-2-to-6 reconfiguration");

    document.prepareAll (sampleRate, blockSize);
    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "restored six-input graph did not compile: "
                                  + compiled.error.toStdString());

    std::vector<std::vector<float>> inputs (6, std::vector<float> (blockSize, 0.0f));
    std::vector<std::vector<float>> outputs (2, std::vector<float> (blockSize, 0.0f));
    std::fill (inputs[5].begin(), inputs[5].end(), 0.42f);
    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    for (const auto& channel : inputs)
        inputPointers.push_back (channel.data());
    for (auto& channel : outputs)
        outputPointers.push_back (channel.data());
    compiled.plan->render (inputPointers.data(), 6, outputPointers.data(), 2, 0, blockSize);
    for (const auto sample : outputs[0])
        expect (std::abs (sample - 0.42f) < 1.0e-6f,
                "retained input 6 route did not become live after restoring six inputs");
}

void testTypedPortRejection()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 6), channelNames ("Output", 2));
    const auto gain = document.addNode (NodeKind::gain, { 300.0f, 100.0f });
    const auto lfo = document.addNode (NodeKind::lfo, { 100.0f, 300.0f });
    expect (gain != 0 && lfo != 0, "could not create typed-port test nodes");

    const auto* gainNode = document.findNode (gain);
    expect (gainNode != nullptr, "gain node disappeared");
    const auto modulationPort = gainNode->processor->getParameter (0).inputPortIndex;

    const auto controlToAudio = document.addConnection ({ lfo, 0, gain, 0 });
    expect (controlToAudio.failed(), "control output was accepted by an audio input");

    const auto audioToControl = document.addConnection ({ PatchDocument::hardwareInputId, 0,
                                                           gain, modulationPort });
    expect (audioToControl.failed(), "audio output was accepted by a control input");

    expectOk (document.addConnection ({ lfo, 0, gain, modulationPort }),
              "compatible control connection was rejected");
}

void testUnguardedCycleIsRejected()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 6), channelNames ("Output", 2));
    document.prepareAll (sampleRate, blockSize);

    const auto firstGain = document.addNode (NodeKind::gain, { 300.0f, 120.0f });
    const auto secondGain = document.addNode (NodeKind::gain, { 600.0f, 120.0f });
    expect (firstGain != 0 && secondGain != 0, "could not create cycle test nodes");

    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, firstGain, 0 }),
              "could not connect hardware input to cycle");
    expectOk (document.addConnection ({ firstGain, 0, secondGain, 0 }),
              "could not connect first cycle edge");
    expectOk (document.addConnection ({ secondGain, 0, firstGain, 0 }),
              "could not connect second cycle edge");
    expectOk (document.addConnection ({ secondGain, 0,
                                         PatchDocument::hardwareOutputId, 0 }),
              "could not connect cycle to hardware output");

    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (! compiled.succeeded(), "unguarded zero-delay cycle compiled successfully");
    expect (compiled.error.containsIgnoreCase ("cycle"),
            "unguarded cycle failed without a useful cycle diagnostic");
}

void testFeedbackGuardCycleIsBounded()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 6), channelNames ("Output", 2));
    document.prepareAll (sampleRate, blockSize);

    const auto mixer = document.addNode (NodeKind::mixer, { 400.0f, 120.0f });
    const auto guard = document.addNode (NodeKind::feedbackGuard, { 700.0f, 300.0f });
    expect (mixer != 0 && guard != 0, "could not create guarded feedback nodes");

    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, mixer, 0 }),
              "could not connect input to feedback mixer");
    expectOk (document.addConnection ({ guard, 0, mixer, 1 }),
              "could not connect guarded send to feedback mixer");
    expectOk (document.addConnection ({ mixer, 0, guard, 0 }),
              "could not connect feedback mixer to guard return");
    expectOk (document.addConnection ({ mixer, 0, PatchDocument::hardwareOutputId, 0 }),
              "could not connect guarded loop to hardware output");

    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "cycle containing Feedback Guard did not compile: "
                                  + compiled.error.toStdString());

    std::vector<std::vector<float>> inputStorage (6, std::vector<float> (blockSize, 0.0f));
    std::vector<std::vector<float>> outputStorage (2, std::vector<float> (blockSize, 0.0f));
    std::fill (inputStorage[0].begin(), inputStorage[0].end(), 0.75f);

    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    for (const auto& channel : inputStorage)
        inputPointers.push_back (channel.data());
    for (auto& channel : outputStorage)
        outputPointers.push_back (channel.data());

    const auto* guardNode = document.findNode (guard);
    expect (guardNode != nullptr, "Feedback Guard node disappeared");
    const auto guardCeiling = juce::Decibels::decibelsToGain (
        guardNode->processor->getParameter (1).getValue());

    for (int block = 0; block < 80; ++block)
    {
        compiled.plan->render (inputPointers.data(), static_cast<int> (inputPointers.size()),
                               outputPointers.data(), static_cast<int> (outputPointers.size()),
                               0, blockSize);

        for (const auto sample : outputStorage[0])
        {
            expect (std::isfinite (sample), "guarded feedback produced a non-finite sample");
            expect (std::abs (sample) <= 0.9801f,
                    "guarded feedback exceeded the graph output safety ceiling");
        }

        expect (std::isfinite (guardNode->processor->outputPeak()),
                "Feedback Guard meter reported a non-finite peak");
        expect (guardNode->processor->outputPeak() <= guardCeiling + 1.0e-4f,
                "Feedback Guard output exceeded its configured ceiling");
    }
}

void testNonFiniteAudioAndDelayModulationAreContained()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    document.prepareAll (sampleRate, blockSize);

    const auto delay = document.addNode (NodeKind::delay, { 400.0f, 160.0f });
    expect (delay != 0, "could not create modulated delay test node");
    auto* delayNode = document.findNode (delay);
    expect (delayNode != nullptr, "modulated delay test node disappeared");
    delayNode->processor->getParameter (0).setValue (1.0f);
    delayNode->processor->getParameter (1).setValue (98.0f);
    delayNode->processor->getParameter (2).setValue (100.0f);

    constexpr NodeId hostileControlId = 900;
    auto hostileControl = std::make_shared<NonFiniteControlNode>();
    hostileControl->prepare (sampleRate, blockSize);
    document.getNodes().push_back ({ hostileControlId, hostileControl, { 180.0f, 380.0f }, false });
    // push_back may reallocate the node vector (it does on MSVC's 1.5x growth,
    // not on libstdc++'s doubling) — the earlier pointer is invalid now.
    delayNode = document.findNode (delay);
    expect (delayNode != nullptr, "modulated delay test node disappeared after insert");

    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, delay, 0 }),
              "could not connect hostile hardware audio to delay");
    for (int parameter = 0; parameter < delayNode->processor->getNumParameters(); ++parameter)
        expectOk (document.addConnection ({ hostileControlId, 0, delay,
                                             delayNode->processor->getParameter (parameter).inputPortIndex }),
                  "could not connect hostile modulation to delay parameter " + std::to_string (parameter));
    expectOk (document.addConnection ({ delay, 0, PatchDocument::hardwareOutputId, 0 }),
              "could not connect modulated delay to output");

    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "hostile modulated delay graph did not compile: "
                                  + compiled.error.toStdString());

    std::vector<float> input (blockSize, 0.0f);
    std::vector<float> output (blockSize, 0.0f);
    const float* inputPointer = input.data();
    float* outputPointer = output.data();

    for (int block = 0; block < 40; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            switch ((sample + block) % 4)
            {
                case 0:  input[static_cast<std::size_t> (sample)] = std::numeric_limits<float>::quiet_NaN(); break;
                case 1:  input[static_cast<std::size_t> (sample)] = std::numeric_limits<float>::infinity(); break;
                case 2:  input[static_cast<std::size_t> (sample)] = -std::numeric_limits<float>::infinity(); break;
                default: input[static_cast<std::size_t> (sample)] = 0.75f; break;
            }
        }

        compiled.plan->render (&inputPointer, 1, &outputPointer, 1, 0, blockSize);
        for (const auto sample : output)
        {
            expect (std::isfinite (sample),
                    "NaN/Inf audio or delay modulation escaped to the hardware output");
            expect (std::abs (sample) <= 0.9801f,
                    "hostile modulated delay exceeded the graph output safety ceiling");
        }
        expect (std::isfinite (delayNode->processor->outputPeak()),
                "modulated delay meter was poisoned by non-finite input");
    }
}

void testFeedbackGuardDelayIsFixedAcrossCallbackSizes()
{
    constexpr int preparedBlockSize = 64;
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    document.prepareAll (sampleRate, preparedBlockSize);
    const auto guard = document.addNode (NodeKind::feedbackGuard, { 420.0f, 200.0f });
    expect (guard != 0, "could not create variable-callback Feedback Guard");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, guard, 0 }),
              "could not connect input to variable-callback Feedback Guard");
    expectOk (document.addConnection ({ guard, 0, PatchDocument::hardwareOutputId, 0 }),
              "could not connect variable-callback Feedback Guard to output");

    auto compiled = GraphCompiler::compile (document, preparedBlockSize);
    expect (compiled.succeeded(), "variable-callback Feedback Guard graph did not compile: "
                                  + compiled.error.toStdString());
    expect (compiled.plan->getGraphLatencySamples() == preparedBlockSize,
            "Feedback Guard did not report the fixed prepared-block delay");

    const std::vector<int> callbackSizes { 13, 7, 29, 5, 11, 23, 4 };
    std::vector<float> rendered;
    int streamPosition = 0;
    for (const auto callbackSize : callbackSizes)
    {
        std::vector<float> input (static_cast<std::size_t> (callbackSize), 0.0f);
        std::vector<float> output (static_cast<std::size_t> (callbackSize), 0.0f);
        if (streamPosition == 0)
            input[0] = 0.5f;
        const float* inputPointer = input.data();
        float* outputPointer = output.data();
        compiled.plan->render (&inputPointer, 1, &outputPointer, 1, 0, callbackSize);
        rendered.insert (rendered.end(), output.begin(), output.end());
        streamPosition += callbackSize;
    }

    expect (rendered.size() > static_cast<std::size_t> (preparedBlockSize),
            "variable callback sequence was too short to observe guard delay");
    for (int sample = 0; sample < preparedBlockSize; ++sample)
        expect (std::abs (rendered[static_cast<std::size_t> (sample)]) < 1.0e-7f,
                "Feedback Guard emitted the impulse before its fixed 64-sample delay");
    expect (std::abs (rendered[static_cast<std::size_t> (preparedBlockSize)]) > 0.1f,
            "Feedback Guard did not emit the impulse exactly 64 stream samples later");
}

void testFeedbackGuardResetIsConsumedByRender()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    document.prepareAll (sampleRate, blockSize);
    const auto guardId = document.addNode (NodeKind::feedbackGuard, { 300.0f, 200.0f });
    auto* guardModel = document.findNode (guardId);
    expect (guardModel != nullptr, "could not create reset-request Feedback Guard");
    auto guard = guardModel->processor;

    juce::AudioBuffer<float> inputs (guard->getNumInputPorts(), blockSize);
    juce::AudioBuffer<float> outputs (guard->getNumOutputPorts(), blockSize);
    inputs.clear();
    inputs.applyGain (0.0f);
    for (int sample = 0; sample < blockSize; ++sample)
        inputs.setSample (0, sample, 20.0f);

    for (int block = 0; block < 3; ++block)
    {
        outputs.clear();
        guard->render (inputs, outputs, blockSize);
        guard->renderFeedbackWrite (inputs, blockSize);
    }
    expect (guard->safetyTripped(), "Feedback Guard did not trip after three dangerous blocks");

    guard->resetSafety();
    expect (guard->safetyTripped(),
            "resetSafety mutated realtime DSP state instead of posting a reset request");

    outputs.applyGain (0.0f);
    outputs.addFrom (0, 0, inputs, 0, 0, blockSize, 0.25f);
    guard->render (inputs, outputs, blockSize);
    expect (! guard->safetyTripped(), "audio-side render did not consume the reset request");
    for (int sample = 0; sample < blockSize; ++sample)
        expect (std::abs (outputs.getSample (0, sample)) < 1.0e-7f,
                "Feedback Guard reset did not clear its delayed buffer");

    inputs.clear();
    for (int sample = 0; sample < blockSize; ++sample)
        inputs.setSample (0, sample, 0.25f);
    guard->renderFeedbackWrite (inputs, blockSize);
    guard->render (inputs, outputs, blockSize);
    expect (! guard->safetyTripped(), "Feedback Guard retripped after a safe post-reset block");
    expect (outputs.getMagnitude (0, 0, blockSize) > 0.01f,
            "Feedback Guard did not resume signal flow after reset");
}

void testJsonRoundTrip()
{
    const auto inputs = channelNames ("Input", 6);
    const auto outputs = channelNames ("Output", 2);

    PatchDocument original;
    original.configureHardware (inputs, outputs);
    const auto distortion = original.addNode (NodeKind::distortion, { 420.5f, 210.25f }, 500);
    const auto lfo = original.addNode (NodeKind::lfo, { 180.0f, 430.0f }, 501);
    expect (distortion == 500 && lfo == 501, "requested node IDs were not created");

    auto* distortionNode = original.findNode (distortion);
    expect (distortionNode != nullptr, "distortion node disappeared");
    distortionNode->processor->setName ("Round-trip drive");
    distortionNode->processor->getParameter (0).setValue (21.5f);
    distortionNode->processor->getParameter (0).setModulationDepth (0.73f);
    const auto driveModulationPort = distortionNode->processor->getParameter (0).inputPortIndex;

    expectOk (original.addConnection ({ PatchDocument::hardwareInputId, 2, distortion, 0 }),
              "could not connect round-trip audio input");
    expectOk (original.addConnection ({ lfo, 0, distortion, driveModulationPort }),
              "could not connect round-trip modulation");
    expectOk (original.addConnection ({ distortion, 0,
                                         PatchDocument::hardwareOutputId, 1 }),
              "could not connect round-trip output");

    const auto encoded = juce::JSON::toString (original.toJson(), true);
    const auto parsed = juce::JSON::parse (encoded);
    expect (! parsed.isVoid(), "serialized patch could not be parsed as JSON");

    PatchDocument restored;
    restored.configureHardware (inputs, outputs);
    expectOk (restored.loadJson (parsed), "could not load serialized patch");

    const auto reencoded = juce::JSON::toString (restored.toJson(), true);
    expect (reencoded == encoded, "patch JSON changed after save/load round-trip");

    const auto* restoredDistortion = restored.findNode (distortion);
    expect (restoredDistortion != nullptr, "restored distortion node is missing");
    expect (restoredDistortion->processor->getName() == "Round-trip drive",
            "custom node name was not restored");
    expect (std::abs (restoredDistortion->processor->getParameter (0).getValue() - 21.5f) < 0.011f,
            "parameter value was not restored");
    expect (std::abs (restoredDistortion->processor->getParameter (0).getModulationDepth() - 0.73f) < 1.0e-6f,
            "parameter modulation depth was not restored");
    expect (restored.getConnections().size() == 3,
            "connections were not restored from JSON");
}

void testAllNodeKindsRenderFiniteOutput()
{
    const NodeKind kinds[] {
        NodeKind::gain, NodeKind::mixer, NodeKind::crossfade, NodeKind::distortion,
        NodeKind::filter, NodeKind::delay, NodeKind::reverb, NodeKind::chorus,
        NodeKind::phaser, NodeKind::tremolo, NodeKind::bitcrusher, NodeKind::ringMod,
        NodeKind::vowelFilter, NodeKind::pitchShifter, NodeKind::vocoder,
        NodeKind::pitchCorrector, NodeKind::granular, NodeKind::compressor,
        NodeKind::limiter, NodeKind::gate, NodeKind::feedbackGuard, NodeKind::monoSynth,
        NodeKind::noiseSource, NodeKind::pluck, NodeKind::drumMachine, NodeKind::sampler,
        NodeKind::fourTrack, NodeKind::lfo, NodeKind::randomLfo, NodeKind::envelopeFollower,
        NodeKind::stepSequencer, NodeKind::macro, NodeKind::spectralFollower,
        NodeKind::script, NodeKind::neuralAmpPlaceholder, NodeKind::neuralPedal
    };

    for (const auto kind : kinds)
    {
        const auto key = nodeKindKey (kind).toStdString();
        auto node = createNodeProcessor (kind);
        expect (node != nullptr, "factory returned no processor for " + key);
        node->prepare (sampleRate, blockSize);

        juce::AudioBuffer<float> inputs (juce::jmax (1, node->getNumInputPorts()), blockSize);
        juce::AudioBuffer<float> outputs (juce::jmax (1, node->getNumOutputPorts()), blockSize);
        double phase = 0.0;
        for (int block = 0; block < 60; ++block)
        {
            // Alternate short and full blocks: nodes must accept any length
            // up to the prepared maximum.
            const auto numSamples = block % 3 == 0 ? 17 : blockSize;
            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto value = 0.5f * static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * 440.0 * phase / sampleRate));
                phase += 1.0;
                for (int channel = 0; channel < inputs.getNumChannels(); ++channel)
                    inputs.setSample (channel, sample, value);
            }
            outputs.clear();
            node->render (inputs, outputs, numSamples);
            node->renderFeedbackWrite (inputs, numSamples);

            for (int port = 0; port < node->getNumOutputPorts(); ++port)
            {
                const auto* samples = outputs.getReadPointer (port);
                for (int sample = 0; sample < numSamples; ++sample)
                {
                    expect (std::isfinite (samples[sample]),
                            key + " produced a non-finite sample");
                    expect (std::abs (samples[sample]) < 100.0f,
                            key + " produced an unbounded sample");
                }
            }
        }

        node->reset();
        outputs.clear();
        node->render (inputs, outputs, blockSize);
        for (int port = 0; port < node->getNumOutputPorts(); ++port)
            expect (std::isfinite (outputs.getSample (port, blockSize - 1)),
                    key + " produced a non-finite sample after reset");
    }
}

void testNeuralAmpModelLoadsAndProcesses()
{
#if SIGNALPATCH_HAS_NAM
    const auto modelFile = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Projects/GitHub/external_clones/neural-amp-modeler-lv2-a2/models/BossWN-feather.nam");
    if (! modelFile.existsAsFile())
    {
        std::cout << "  (no .nam model on disk; NAM load check skipped)\n";
        return;
    }

    auto node = createNodeProcessor (NodeKind::neuralAmpPlaceholder);
    expect (node != nullptr, "could not create the neural amp node");
    node->prepare (sampleRate, blockSize);

    auto state = std::make_unique<juce::DynamicObject>();
    state->setProperty ("model", modelFile.getFullPathName());
    node->setExtraState (juce::var (state.release()));
    expect (! node->statusText().startsWith ("LOAD FAILED"),
            "NAM model failed to load: " + node->statusText().toStdString());
    expect (node->getExtraState().getProperty ("model", "").toString() == modelFile.getFullPathName(),
            "NAM model path was not kept in the node state");

    juce::AudioBuffer<float> inputs (node->getNumInputPorts(), blockSize);
    juce::AudioBuffer<float> outputs (node->getNumOutputPorts(), blockSize);
    float passthroughDifference = 0.0f;
    double phase = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = 0.4f * static_cast<float> (
                std::sin (juce::MathConstants<double>::twoPi * 220.0 * phase / sampleRate));
            phase += 1.0;
            for (int channel = 0; channel < inputs.getNumChannels(); ++channel)
                inputs.setSample (channel, sample, value);
        }
        outputs.clear();
        node->render (inputs, outputs, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto out = outputs.getSample (0, sample);
            expect (std::isfinite (out), "NAM produced a non-finite sample");
            passthroughDifference = std::max (passthroughDifference,
                                              std::abs (out - inputs.getSample (0, sample)));
        }
    }
    expect (passthroughDifference > 1.0e-4f,
            "NAM output is identical to its input; the model is not running");
#else
    std::cout << "  (built without NAM; check skipped)\n";
#endif
}

// Builds a patch containing one of every user-creatable kind: audio effects
// chained input -> ... -> output, sources mixed in, and controls modulating
// real destinations. Shared by the allocation-trap and churn tests.
PatchDocument buildKitchenSinkDocument()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 6), channelNames ("Output", 2));
    document.prepareAll (sampleRate, blockSize);

    const NodeKind chainKinds[] {
        NodeKind::gain, NodeKind::distortion, NodeKind::filter, NodeKind::delay,
        NodeKind::reverb, NodeKind::chorus, NodeKind::phaser, NodeKind::tremolo,
        NodeKind::bitcrusher, NodeKind::ringMod, NodeKind::vowelFilter,
        NodeKind::pitchShifter, NodeKind::pitchCorrector, NodeKind::granular,
        NodeKind::compressor, NodeKind::gate, NodeKind::limiter,
        NodeKind::neuralAmpPlaceholder, NodeKind::neuralPedal, NodeKind::script
    };
    NodeId previous = 0;
    for (const auto kind : chainKinds)
    {
        const auto id = document.addNode (kind, { 100.0f, 100.0f });
        expect (id != 0, "kitchen sink could not create " + nodeKindKey (kind).toStdString());
        expectOk (document.addConnection (previous == 0
                                              ? Connection { PatchDocument::hardwareInputId, 0, id, 0 }
                                              : Connection { previous, 0, id, 0 }),
                  "kitchen sink chain connection failed at " + nodeKindKey (kind).toStdString());
        previous = id;
    }
    expectOk (document.addConnection ({ previous, 0, PatchDocument::hardwareOutputId, 0 }),
              "kitchen sink could not reach the hardware output");

    // Sources summed into output 2 through a mixer.
    const auto mixer = document.addNode (NodeKind::mixer, {});
    const auto synth = document.addNode (NodeKind::monoSynth, {});
    const auto drums = document.addNode (NodeKind::drumMachine, {});
    const auto noise = document.addNode (NodeKind::noiseSource, {});
    const auto pluck = document.addNode (NodeKind::pluck, {});
    expectOk (document.addConnection ({ synth, 0, mixer, 0 }), "synth -> mixer");
    expectOk (document.addConnection ({ drums, 0, mixer, 1 }), "drums -> mixer");
    expectOk (document.addConnection ({ noise, 0, mixer, 2 }), "noise -> mixer");
    expectOk (document.addConnection ({ pluck, 0, mixer, 3 }), "pluck -> mixer");
    expectOk (document.addConnection ({ mixer, 0, PatchDocument::hardwareOutputId, 1 }),
              "mixer -> output 2");

    // Controls into live modulation targets, and the remaining kinds.
    const auto lfo = document.addNode (NodeKind::lfo, {});
    const auto sequencer = document.addNode (NodeKind::stepSequencer, {});
    const auto randomLfo = document.addNode (NodeKind::randomLfo, {});
    const auto macro = document.addNode (NodeKind::macro, {});
    const auto follower = document.addNode (NodeKind::envelopeFollower, {});
    const auto spectral = document.addNode (NodeKind::spectralFollower, {});
    const auto crossfade = document.addNode (NodeKind::crossfade, {});
    const auto sampler = document.addNode (NodeKind::sampler, {});
    const auto fourTrack = document.addNode (NodeKind::fourTrack, {});
    auto* synthNode = document.findNode (synth);
    expect (synthNode != nullptr, "synth disappeared");
    expectOk (document.addConnection ({ lfo, 0, synth,
                                        synthNode->processor->getParameter (1).inputPortIndex }),
              "lfo -> synth pitch mod");
    expectOk (document.addConnection ({ sequencer, 0, synth, 0 }), "sequencer -> synth gate");
    expectOk (document.addConnection ({ randomLfo, 0, pluck, 0 }), "random -> pluck trigger");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 1, follower, 0 }),
              "input 2 -> envelope follower");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 2, spectral, 0 }),
              "input 3 -> spectral follower");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 3, crossfade, 0 }),
              "input 4 -> crossfade A");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 4, crossfade, 1 }),
              "input 5 -> crossfade B");
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 5, sampler, 0 }),
              "input 6 -> sampler");
    expectOk (document.addConnection ({ crossfade, 0, fourTrack, 0 }), "crossfade -> 4-track");
    juce::ignoreUnused (macro);

    // A guarded feedback loop around a gain stage.
    const auto loopGain = document.addNode (NodeKind::gain, {});
    const auto guard = document.addNode (NodeKind::feedbackGuard, {});
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 1, loopGain, 0 }),
              "input 2 -> loop gain");
    expectOk (document.addConnection ({ loopGain, 0, guard, 0 }), "loop gain -> guard");
    expectOk (document.addConnection ({ guard, 0, loopGain, 0 }), "guard -> loop gain");

    // A vocoder fed voice and carrier.
    const auto vocoder = document.addNode (NodeKind::vocoder, {});
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, vocoder, 0 }),
              "input 1 -> vocoder voice");
    expectOk (document.addConnection ({ synth, 0, vocoder, 1 }), "synth -> vocoder carrier");

    // Real NAM model when present so inference runs under the trap too.
    const auto namModel = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Projects/GitHub/external_clones/neural-amp-modeler-lv2-a2/models/BossWN-feather.nam");
    if (namModel.existsAsFile())
        for (auto& node : document.getNodes())
            if (node.processor->getKind() == NodeKind::neuralAmpPlaceholder)
            {
                auto state = std::make_unique<juce::DynamicObject>();
                state->setProperty ("model", namModel.getFullPathName());
                node.processor->setExtraState (juce::var (state.release()));
            }

    return document;
}

void renderPlanBlocks (RenderPlan& plan, int blockCount, bool varyBlockSizes, bool armTrap = false)
{
    // Everything the harness itself needs is allocated before the trap arms;
    // inside the armed window only plan.render and arithmetic may run.
    std::vector<std::vector<float>> inputStorage (6, std::vector<float> (blockSize, 0.0f));
    std::vector<std::vector<float>> outputStorage (2, std::vector<float> (blockSize, 0.0f));
    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    for (const auto& channel : inputStorage)
        inputPointers.push_back (channel.data());
    for (auto& channel : outputStorage)
        outputPointers.push_back (channel.data());

    bool allFinite = true;
    if (armTrap)
    {
        rtTrap::violations.store (0);
        rtTrap::armed.store (true);
    }

    double phase = 0.0;
    for (int block = 0; block < blockCount; ++block)
    {
        const auto numSamples = ! varyBlockSizes ? blockSize
                              : (block % 3 == 0 ? 17 : (block % 3 == 1 ? 48 : blockSize));
        for (auto& channel : inputStorage)
            for (int sample = 0; sample < numSamples; ++sample)
            {
                channel[static_cast<std::size_t> (sample)] = 0.4f * static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * 180.0 * phase / sampleRate));
                phase += 1.0;
            }
        plan.render (inputPointers.data(), 6, outputPointers.data(), 2, 0, numSamples);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < numSamples; ++sample)
                allFinite = allFinite
                         && std::isfinite (outputStorage[static_cast<std::size_t> (channel)]
                                                        [static_cast<std::size_t> (sample)]);
    }

    if (armTrap)
        rtTrap::armed.store (false);
    expect (allFinite, "kitchen-sink render produced a non-finite output sample");
}

void testCallbackPathDoesNotAllocate()
{
    auto document = buildKitchenSinkDocument();
    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "kitchen-sink graph did not compile: "
                                  + compiled.error.toStdString());

    // Warm-up outside the trap: lazy one-time work is allowed before
    // publication, never inside the callback.
    renderPlanBlocks (*compiled.plan, 8, false);

    // SIGNALPATCH_SOAK_BLOCKS turns this into a long soak (e.g. 1687500
    // blocks = 30 minutes of 48 kHz/64 audio through the worst-case graph).
    auto blockCount = 400;
    if (const auto* soak = std::getenv ("SIGNALPATCH_SOAK_BLOCKS"))
        blockCount = juce::jmax (blockCount, std::atoi (soak));
    renderPlanBlocks (*compiled.plan, blockCount, true, true);

    const auto violations = rtTrap::violations.load();
    expect (violations == 0,
            "the render path allocated or freed memory " + std::to_string (violations)
            + " time(s) - REALTIME_SAFETY.md forbids this");
}

void testRecompileChurnKeepsRendering()
{
    auto document = buildKitchenSinkDocument();
    for (int round = 0; round < 25; ++round)
    {
        const auto extra = document.addNode (NodeKind::filter, {});
        expect (extra != 0, "churn round could not add a filter");
        expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 2, extra, 0 }),
                  "churn connection failed");
        auto compiled = GraphCompiler::compile (document, blockSize);
        expect (compiled.succeeded(), "churn graph stopped compiling: "
                                      + compiled.error.toStdString());
        renderPlanBlocks (*compiled.plan, 6, true);
        expect (document.removeNode (extra), "churn round could not remove the filter");
        // Parameter gesture bursts between compiles, like a live performer.
        for (auto& node : document.getNodes())
            if (node.processor->getNumParameters() > 0)
                node.processor->getParameter (0).setValue (
                    node.processor->getParameter (0).range.convertFrom0to1 (
                        static_cast<float> (round) / 25.0f));
    }
    auto finalCompiled = GraphCompiler::compile (document, blockSize);
    expect (finalCompiled.succeeded(), "graph no longer compiles after churn");
    renderPlanBlocks (*finalCompiled.plan, 20, true);
}

// Stage-3 qualification tool (docs/NAM_ROADMAP.md): benchmark every .nam in
// SIGNALPATCH_BENCH_NAM_DIR against the 64-sample/48 kHz deadline and print a
// table. Informational unless a model fails to load.
void testNamModelBenchmark()
{
#if SIGNALPATCH_HAS_NAM
    const auto* directoryEnv = std::getenv ("SIGNALPATCH_BENCH_NAM_DIR");
    if (directoryEnv == nullptr)
    {
        std::cout << "  (set SIGNALPATCH_BENCH_NAM_DIR=<folder> to benchmark .nam models)\n";
        return;
    }
    const juce::File folder ((juce::String (directoryEnv)));
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.nam");
    expect (! files.isEmpty(),
            "no .nam files found in " + folder.getFullPathName().toStdString());
    files.sort();

    constexpr int benchBlocks = 1500;
    const auto budgetSeconds = static_cast<double> (blockSize) / sampleRate;
    std::cout << "  model                                    avg%   max%   verdict @64/48k\n";

    for (const auto& file : files)
    {
        auto node = createNodeProcessor (NodeKind::neuralPedal);
        node->prepare (sampleRate, blockSize);
        auto state = std::make_unique<juce::DynamicObject>();
        state->setProperty ("model", file.getFullPathName());
        node->setExtraState (juce::var (state.release()));
        const auto status = node->statusText();
        expect (! status.startsWith ("LOAD FAILED"),
                file.getFileName().toStdString() + " failed to load: " + status.toStdString());

        juce::AudioBuffer<float> inputs (node->getNumInputPorts(), blockSize);
        juce::AudioBuffer<float> outputs (node->getNumOutputPorts(), blockSize);
        double phase = 0.0;
        auto fill = [&]
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = 0.4f * static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * 220.0 * phase / sampleRate));
                phase += 1.0;
                for (int channel = 0; channel < inputs.getNumChannels(); ++channel)
                    inputs.setSample (channel, sample, value);
            }
        };
        for (int block = 0; block < 32; ++block) // warm-up
        {
            fill();
            node->render (inputs, outputs, blockSize);
        }

        double totalSeconds = 0.0;
        double worstSeconds = 0.0;
        for (int block = 0; block < benchBlocks; ++block)
        {
            fill();
            const auto start = juce::Time::getHighResolutionTicks();
            node->render (inputs, outputs, blockSize);
            const auto elapsed = juce::Time::highResolutionTicksToSeconds (
                juce::Time::getHighResolutionTicks() - start);
            totalSeconds += elapsed;
            worstSeconds = std::max (worstSeconds, elapsed);
        }
        const auto averagePercent = 100.0 * (totalSeconds / benchBlocks) / budgetSeconds;
        const auto worstPercent = 100.0 * worstSeconds / budgetSeconds;
        const auto* verdict = worstPercent < 60.0 ? "OK"
                            : worstPercent < 100.0 ? "TIGHT (headroom < 40%)"
                                                   : "OVER BUDGET";
        std::cout << "  " << file.getFileNameWithoutExtension().paddedRight (' ', 40).toStdString()
                  << juce::String (averagePercent, 1).paddedLeft (' ', 5).toStdString() << "  "
                  << juce::String (worstPercent, 1).paddedLeft (' ', 5).toStdString() << "   "
                  << verdict << "\n";
    }
#else
    std::cout << "  (built without NAM; benchmark skipped)\n";
#endif
}

struct TestCase
{
    const char* name;
    std::function<void()> body;
};
} // namespace

int main()
{
    // Flush every insertion so a crash on CI still shows which test was
    // running (pipes are fully buffered otherwise).
    std::cout.setf (std::ios::unitbuf);
    std::cerr.setf (std::ios::unitbuf);

    const std::vector<TestCase> tests {
        { "dynamic hardware port counts", testDynamicHardwarePortCounts },
        { "straight-through rendering", testStraightThroughRendering },
        { "sparse physical callback mapping", testSparsePhysicalCallbackMapping },
        { "saved 6-input route on 2-input layout", testSavedSixInputRouteOnTwoInputLayout },
        { "6-to-2-to-6 route retention", testSixToTwoToSixReconfigurationRetainsRoute },
        { "typed-port rejection", testTypedPortRejection },
        { "unguarded-cycle compile rejection", testUnguardedCycleIsRejected },
        { "Feedback Guard bounded cycle", testFeedbackGuardCycleIsBounded },
        { "non-finite audio and delay modulation containment", testNonFiniteAudioAndDelayModulationAreContained },
        { "Feedback Guard fixed variable-callback delay", testFeedbackGuardDelayIsFixedAcrossCallbackSizes },
        { "Feedback Guard reset request", testFeedbackGuardResetIsConsumedByRender },
        { "JSON round-trip", testJsonRoundTrip },
        { "all node kinds render finite output", testAllNodeKindsRenderFiniteOutput },
        { "NAM model loads and processes", testNeuralAmpModelLoadsAndProcesses },
        { "callback path performs no allocation", testCallbackPathDoesNotAllocate },
        { "recompile churn keeps rendering", testRecompileChurnKeepsRendering },
        { "NAM model benchmark (env-gated)", testNamModelBenchmark }
    };

    int failures = 0;
    for (const auto& test : tests)
    {
        try
        {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
        catch (...)
        {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    if (failures == 0)
    {
        std::cout << "All " << tests.size() << " engine tests passed.\n";
        return 0;
    }

    std::cerr << failures << " of " << tests.size() << " engine tests failed.\n";
    return 1;
}
