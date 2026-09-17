#include "../src/audio/Graph.h"
#include "../src/audio/PatchHistory.h"
#include "../src/audio/PatchBundle.h"
#include "../src/audio/MidiMap.h"
#include "../src/audio/ControllerFeedback.h"
#include "../src/audio/Tone3000.h"
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
#include <unordered_map>
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
        NodeKind::script, NodeKind::neuralAmpPlaceholder, NodeKind::neuralPedal,
        NodeKind::cabinet, NodeKind::looper, NodeKind::pan, NodeKind::stereoMerge, NodeKind::stereoDelay,
        NodeKind::stereoChorus, NodeKind::stereoReverb, NodeKind::tuner, NodeKind::midiNote, NodeKind::clock
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
        NodeKind::neuralAmpPlaceholder, NodeKind::neuralPedal, NodeKind::cabinet, NodeKind::looper, NodeKind::script,
        NodeKind::pan, NodeKind::stereoMerge, NodeKind::stereoDelay, NodeKind::stereoChorus, NodeKind::stereoReverb, NodeKind::tuner
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
    // A keyboard plays the pluck: MIDI Note gate -> trigger, its pitch -> pitch mod; the random LFO wobbles the level.
    const auto keys = document.addNode (NodeKind::midiNote, {});
    auto* pluckNode = document.findNode (pluck);
    expect (pluckNode != nullptr, "pluck disappeared");
    expectOk (document.addConnection ({ keys, 0, pluck, 0 }), "midi note gate -> pluck trigger");
    expectOk (document.addConnection ({ keys, 1, pluck, pluckNode->processor->getNumInputPorts() - 1 }), "midi note pitch -> pluck Pitch input");
    expectOk (document.addConnection ({ randomLfo, 0, pluck, pluckNode->processor->getParameter (pluckNode->processor->getNumParameters() - 1).inputPortIndex }),
              "random -> pluck last knob mod");
    // One clock steps the drums (eighths), the sequencer (beats) and gates the looper's transport (bars).
    const auto clock = document.addNode (NodeKind::clock, {});
    const auto* drumsNode = document.findNode (drums);
    const auto* sequencerNode = document.findNode (sequencer);
    NodeId looperId = 0;
    for (const auto& node : document.getNodes())
        if (node.processor->getKind() == NodeKind::looper)
            looperId = node.id;
    const auto* looperNode = document.findNode (looperId);
    expect (drumsNode != nullptr && sequencerNode != nullptr && looperNode != nullptr, "clock followers missing");
    expectOk (document.addConnection ({ clock, 1, drums, drumsNode->processor->getNumInputPorts() - 1 }), "clock eighths -> drums");
    expectOk (document.addConnection ({ clock, 0, sequencer, sequencerNode->processor->getNumInputPorts() - 1 }), "clock beats -> sequencer");
    expectOk (document.addConnection ({ clock, 2, looperId, looperNode->processor->getNumInputPorts() - 1 }), "clock bars -> looper");
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
        // A note every few blocks, like the engine's FIFO drain does before the render.
        if (block % 5 == 0)
            plan.dispatchMidiNote (1, 48 + (block / 5) % 24, 100, (block / 5) % 2 == 0);
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

// Model benchmark (docs/HANDHELD.md): benchmark every .nam in
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

void testUndoRedoStructure()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 2), channelNames ("Output", 2));
    PatchHistory history (document);

    const auto delay = document.addNode (NodeKind::delay, { 100.0f, 100.0f });
    history.recordNodeAdded (delay);
    const Connection in { PatchDocument::hardwareInputId, 0, delay, 0 };
    const Connection out { delay, 0, PatchDocument::hardwareOutputId, 0 };
    expectOk (document.addConnection (in), "connect in");
    history.recordConnected (in);
    expectOk (document.addConnection (out), "connect out");
    history.recordConnected (out);
    expect (history.getUndoCount() == 3, "three structural entries expected");

    expect (history.undo() == PatchHistory::Applied::structure, "undo cable is structural");
    expect (document.getConnections().size() == 1, "undo did not remove the last cable");
    expect (history.undo() == PatchHistory::Applied::structure, "undo second cable");
    expect (history.undo() == PatchHistory::Applied::structure, "undo add node");
    expect (document.findNode (delay) == nullptr, "undo did not remove the added node");
    expect (document.getConnections().empty(), "cables survived node undo");
    expect (history.undo() == PatchHistory::Applied::none, "history should be exhausted");

    expect (history.redo() == PatchHistory::Applied::structure, "redo add node");
    expect (document.findNode (delay) != nullptr, "redo did not restore the node under its old id");
    expect (history.redo() == PatchHistory::Applied::structure && history.redo() == PatchHistory::Applied::structure,
            "redo cables");
    expect (document.getConnections().size() == 2, "redo did not restore both cables");
    expect (history.redo() == PatchHistory::Applied::none, "redo should be exhausted");

    // Compile must still succeed after the round trip.
    const auto compiled = GraphCompiler::compile (document, 64, true);
    expect (compiled.succeeded(), "graph failed to compile after undo/redo round trip");
}

void testUndoDeleteRestoresSameProcessorAndCables()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 2), channelNames ("Output", 2));
    PatchHistory history (document);

    const auto drive = document.addNode (NodeKind::distortion, { 10.0f, 10.0f });
    const auto lfo = document.addNode (NodeKind::lfo, { 10.0f, 200.0f });
    auto* driveNode = document.findNode (drive);
    expect (driveNode != nullptr, "distortion missing");
    driveNode->processor->getParameter (0).setValue (17.5f);
    const auto* processorBefore = driveNode->processor.get();
    const auto modPort = driveNode->processor->getParameter (0).inputPortIndex;
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, drive, 0 }), "in");
    expectOk (document.addConnection ({ drive, 0, PatchDocument::hardwareOutputId, 0 }), "out");
    expectOk (document.addConnection ({ lfo, 0, drive, modPort }), "mod");

    expect (history.removeNode (drive), "removeNode failed");
    expect (document.findNode (drive) == nullptr, "node still present after delete");
    expect (document.getConnections().empty(), "delete left cables behind");

    expect (history.undo() == PatchHistory::Applied::structure, "undo delete");
    auto* restored = document.findNode (drive);
    expect (restored != nullptr, "undo did not restore the node");
    expect (restored->processor.get() == processorBefore, "undo created a new processor instead of restoring the old one");
    expect (std::abs (restored->processor->getParameter (0).getValue() - 17.5f) < 1.0e-6f,
            "parameter value lost across delete/undo");
    expect (document.getConnections().size() == 3, "undo did not restore all three cables");

    expect (history.redo() == PatchHistory::Applied::structure, "redo delete");
    expect (document.findNode (drive) == nullptr && document.getConnections().empty(), "redo delete incomplete");
}

void testUndoCompoundGestureMovesSeveralModulesAsOneStep()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    PatchHistory history (document);
    const auto a = document.addNode (NodeKind::gain, { 0.0f, 0.0f });
    const auto b = document.addNode (NodeKind::filter, { 100.0f, 0.0f });
    history.closeGesture();
    const auto baseline = history.getUndoCount();

    // A multi-drag: both modules move on every motion event, interleaved.
    history.beginCompoundGesture ("Move 2 modules");
    for (int step = 1; step <= 10; ++step)
        for (const auto id : { a, b })
        {
            auto* node = document.findNode (id);
            const auto next = node->position + juce::Point<float> (5.0f, 3.0f);
            history.recordMove (id, node->position, next);
            node->position = next;
        }
    history.closeGesture();
    expect (history.getUndoCount() == baseline + 2, "a compound drag should keep one entry per module: " + std::to_string (history.getUndoCount() - baseline));
    expect (history.getUndoDescription() == "Move 2 modules", "compound description lost");

    expect (history.undo() == PatchHistory::Applied::values, "undo the compound");
    expect (history.getUndoCount() == baseline, "one undo should take the whole compound");
    expect (document.findNode (a)->position == juce::Point<float> (0.0f, 0.0f) && document.findNode (b)->position == juce::Point<float> (100.0f, 0.0f), "both modules should be back");
    expect (history.redo() == PatchHistory::Applied::values, "redo the compound");
    expect (document.findNode (a)->position == juce::Point<float> (50.0f, 30.0f) && document.findNode (b)->position == juce::Point<float> (150.0f, 30.0f), "both modules should have moved again");
    expect (! history.canRedo(), "redo should have consumed the whole compound");
}

void testUndoDeleteBringsBackMidiAndGroups()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    PatchHistory history (document);
    const auto drive = document.addNode (NodeKind::distortion, { 0.0f, 0.0f });
    const auto delay = document.addNode (NodeKind::delay, { 200.0f, 0.0f });
    PedalGroup group;
    group.id = 1; group.name = "BOX"; group.members = { drive, delay }; group.knobs = { { drive, 0 }, { delay, 0 } };
    document.setGroups ({ group });
    MidiMapping stomp;
    stomp.source = MidiMapping::Source::note; stomp.number = 60; stomp.channel = 10;
    stomp.target = MidiMapping::Target::bypass; stomp.node = drive;
    document.setMidiMappings ({ stomp });
    history.closeGesture();

    expect (history.removeNode (drive), "delete should work");
    expect (document.getMidiMappings().empty(), "deleting scrubs the node's mappings");
    expect (document.getGroups().size() == 1 && document.getGroups()[0].members.size() == 1, "deleting takes the node out of its group");
    expect (history.undo() == PatchHistory::Applied::structure, "undo delete");
    expect (document.getMidiMappings().size() == 1 && document.getMidiMappings()[0].node == drive, "undo should bring the footswitch binding back");
    expect (document.getGroups().size() == 1 && document.getGroups()[0].members.size() == 2 && document.getGroups()[0].knobs.size() == 2, "undo should bring the group membership and knobs back");

    // Deleting every member drops the group; undoing both brings it back whole.
    history.closeGesture();
    history.beginCompoundGesture ("Remove 2 modules");
    history.removeNode (drive);
    history.removeNode (delay);
    history.closeGesture();
    expect (document.getGroups().empty(), "an emptied group is dropped");
    history.undo();
    expect (document.getGroups().size() == 1 && document.getGroups()[0].members.size() == 2, "undoing a multi-delete restores the whole group");
    expect (document.getMidiMappings().size() == 1, "and the binding");
}

void testParameterShapeNarrowsBendsAndSurvives()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    PatchHistory history (document);
    const auto filter = document.addNode (NodeKind::filter, { 0.0f, 0.0f });
    auto& cutoff = document.findNode (filter)->processor->getParameter (0);
    expect (cutoff.hasDefaultShape(), "a fresh knob should cover its whole range");
    cutoff.setValue (1200.0f);

    // Narrow the travel: the value stays, the ends of the knob are the new limits.
    const auto before = cutoff.getShape();
    cutoff.setShape ({ 200.0f, 2000.0f, 0.0f });
    history.recordParameterShape (filter, 0, before, 1200.0f, cutoff.getShape(), cutoff.getValue());
    expect (std::abs (cutoff.getValue() - 1200.0f) < 1.0f, "narrowing the travel moved a value that still fits");
    expect (std::abs (cutoff.valueFromNormalised (0.0f) - 200.0f) < 0.5f, "knob fully down is not the minimum");
    expect (std::abs (cutoff.valueFromNormalised (1.0f) - 2000.0f) < 1.0f, "knob fully up is not the maximum");
    cutoff.setValue (9000.0f);
    expect (std::abs (cutoff.getValue() - 2000.0f) < 1.0f, "a value above the travel should stop at its top");
    // Modulation is confined to the travel too.
    cutoff.setValue (1900.0f);
    cutoff.setModulationDepth (1.0f);
    cutoff.prepare (sampleRate);
    expect (cutoff.nextValue (1.0f) <= 2000.5f && cutoff.nextValue (-1.0f) >= 199.5f, "modulation escaped the knob's travel");

    // The curve bends the travel and stays monotonic; its inverse lands on the same knob position.
    cutoff.setShape ({ 200.0f, 2000.0f, 0.75f });
    float last = -1.0f;
    for (int step = 0; step <= 20; ++step)
    {
        const auto travel = static_cast<float> (step) / 20.0f;
        const auto value = cutoff.valueFromNormalised (travel);
        expect (value >= last, "a curved knob went backwards");
        expect (std::abs (cutoff.normalisedFromValue (value) - travel) < 2.0e-3f, "curve inverse does not return the knob position");
        last = value;
    }
    const auto straightMiddle = [&] { cutoff.setShape ({ 200.0f, 2000.0f, 0.0f }); return cutoff.valueFromNormalised (0.5f); }();
    cutoff.setShape ({ 200.0f, 2000.0f, 0.75f });
    expect (cutoff.valueFromNormalised (0.5f) < straightMiddle, "a positive curve should be finer at the start of the travel");

    // Minimum above maximum turns the knob around.
    cutoff.setShape ({ 2000.0f, 200.0f, 0.0f });
    expect (cutoff.valueFromNormalised (0.0f) > cutoff.valueFromNormalised (1.0f), "a reversed travel should run downwards");

    // Saved with the patch, and absent from the JSON while untouched.
    cutoff.setShape ({ 300.0f, 3000.0f, -0.5f });
    cutoff.setValue (700.0f);
    const auto parsed = juce::JSON::parse (juce::JSON::toString (document.toJson(), true));
    PatchDocument restored;
    restored.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    expectOk (restored.loadJson (parsed), "patch with a shaped knob did not load");
    const auto& restoredCutoff = restored.findNode (filter)->processor->getParameter (0);
    const auto shape = restoredCutoff.getShape();
    expect (std::abs (shape.minimum - 300.0f) < 0.5f && std::abs (shape.maximum - 3000.0f) < 0.5f && std::abs (shape.curve + 0.5f) < 1.0e-4f,
            "knob travel and curve did not survive save / load");
    expect (std::abs (restoredCutoff.getValue() - 700.0f) < 1.0f, "value inside a shaped travel did not survive save / load");
    expect (restored.findNode (filter)->processor->getParameter (1).hasDefaultShape(), "an untouched knob came back shaped");
    {
        bool untouchedWritesShape = false, shapedWritesShape = false;
        if (const auto* nodes = parsed.getProperty ("nodes", {}).getArray())
            for (const auto& node : *nodes)
                if (const auto* parameters = node.getProperty ("parameters", {}).getArray())
                    for (const auto& saved : *parameters)
                    {
                        if (saved.getProperty ("id", {}).toString() == "resonance" && saved.hasProperty ("min"))
                            untouchedWritesShape = true;
                        if (saved.getProperty ("id", {}).toString() == "cutoff" && saved.hasProperty ("min") && saved.hasProperty ("curve"))
                            shapedWritesShape = true;
                    }
        expect (shapedWritesShape && ! untouchedWritesShape, "only knobs with an edited travel should write one");
    }

    // Undo returns both the travel and the value.
    cutoff.setShape ({ 200.0f, 2000.0f, 0.0f });
    cutoff.setValue (1200.0f);
    expect (history.undo() == PatchHistory::Applied::values, "undo of a range edit");
    expect (cutoff.hasDefaultShape() && std::abs (cutoff.getValue() - 1200.0f) < 1.0f, "undo did not give the knob its whole range back");
    expect (history.redo() == PatchHistory::Applied::values, "redo of a range edit");
    expect (std::abs (cutoff.getShape().maximum - 2000.0f) < 0.5f, "redo did not narrow the knob again");
}

void testModulatedKnobReportsWhereItIs()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    document.prepareAll (sampleRate, blockSize);
    const auto lfo = document.addNode (NodeKind::lfo, { 0.0f, 0.0f });
    const auto filter = document.addNode (NodeKind::filter, { 0.0f, 0.0f });
    document.findNode (lfo)->processor->getParameter (0).setValue (8.0f);
    auto& cutoff = document.findNode (filter)->processor->getParameter (0);
    auto& resonance = document.findNode (filter)->processor->getParameter (1);
    cutoff.setModulationDepth (0.4f);
    expectOk (document.addConnection ({ PatchDocument::hardwareInputId, 0, filter, 0 }), "input to filter");
    expectOk (document.addConnection ({ filter, 0, PatchDocument::hardwareOutputId, 0 }), "filter to output");
    expectOk (document.addConnection ({ lfo, 0, filter, cutoff.inputPortIndex }), "lfo to cutoff");
    auto compiled = GraphCompiler::compile (document, blockSize);
    expect (compiled.succeeded(), "modulated filter did not compile");

    std::vector<float> input (blockSize, 0.1f), output (blockSize, 0.0f);
    const float* inputs[] = { input.data() };
    float* outputs[] = { output.data() };
    float lowest = 1.0f, highest = 0.0f;
    for (int block = 0; block < 400; ++block)
    {
        compiled.plan->render (inputs, 1, outputs, 1, 0, blockSize);
        const auto live = cutoff.getLiveNormalised();
        expect (live >= 0.0f && live <= 1.0f, "live knob position left 0..1");
        lowest = juce::jmin (lowest, live);
        highest = juce::jmax (highest, live);
    }
    const auto base = cutoff.getNormalisedValue();
    expect (highest > base + 0.2f && lowest < base - 0.2f, "a modulated knob should report its swing around the setting");
    expect (std::abs (resonance.getLiveNormalised() - resonance.getNormalisedValue()) < 1.0e-6f, "a knob with an empty mod socket should sit still");

    // Bypassed: nothing is modulating, the knob rests on its setting.
    document.findNode (filter)->processor->setBypassed (true);
    compiled.plan->render (inputs, 1, outputs, 1, 0, blockSize);
    expect (std::abs (cutoff.getLiveNormalised() - base) < 1.0e-6f, "a bypassed module's knob should rest");
}

void testUnusedSavedHardwareChannelsDoNotComeBack()
{
    // Saved on a device that showed ten inputs (six real, four loopbacks), cabled on input 2 only.
    PatchDocument source;
    source.configureHardware (channelNames ("Wide", 10), channelNames ("Output", 2));
    expectOk (source.addConnection ({ PatchDocument::hardwareInputId, 1, PatchDocument::hardwareOutputId, 0 }), "route on the wide device");
    const auto parsed = juce::JSON::parse (juce::JSON::toString (source.toJson(), true));

    PatchDocument restored;
    restored.configureHardware (channelNames ("Six", 6), channelNames ("Output", 2));
    expectOk (restored.loadJson (parsed), "wide patch on the six-input device");
    expect (restored.findNode (PatchDocument::hardwareInputId)->processor->getNumOutputPorts() == 6,
            "saved channels nobody cabled should not come back as placeholders");

    // A smaller device later: only what a cable holds is kept.
    restored.configureHardware (channelNames ("Mono", 1), channelNames ("Output", 2));
    expect (restored.findNode (PatchDocument::hardwareInputId)->processor->getNumOutputPorts() == 2,
            "the cabled channel should stay as a placeholder, the unused ones should go");
}

void testUndoCoalescesKnobGestures()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    PatchHistory history (document);

    const auto gain = document.addNode (NodeKind::gain, { 0.0f, 0.0f });
    auto& parameter = document.findNode (gain)->processor->getParameter (0);
    const auto start = parameter.getValue();

    // A drag: many tiny edits of the same knob in quick succession.
    float value = start;
    for (int step = 0; step < 25; ++step)
    {
        const auto next = value + 0.2f;
        history.recordParameter (gain, 0, value, next);
        parameter.setValue (next);
        value = next;
    }
    expect (history.getUndoCount() == 1, "a knob drag should be a single undo step");

    // A second gesture on the same knob after an explicit close is a new step.
    history.closeGesture();
    history.recordParameter (gain, 0, value, value + 1.0f);
    parameter.setValue (value + 1.0f);
    expect (history.getUndoCount() == 2, "closing the gesture should start a new entry");

    expect (history.undo() == PatchHistory::Applied::values, "undo second gesture");
    expect (std::abs (parameter.getValue() - value) < 1.0e-6f, "second gesture not undone");
    expect (history.undo() == PatchHistory::Applied::values, "undo drag");
    expect (std::abs (parameter.getValue() - start) < 1.0e-6f, "drag undo did not return to the pre-drag value");
    expect (history.redo() == PatchHistory::Applied::values, "redo drag");
    expect (std::abs (parameter.getValue() - value) < 1.0e-6f, "drag redo did not restore the end of the drag");

    // Editing after an undo discards the redo branch.
    history.recordParameter (gain, 0, value, 3.0f);
    expect (! history.canRedo(), "new edit should clear redo");
}

void writeTestImpulseFile (const juce::File& file, double sampleRate)
{
    // A 4-sample impulse response: a small tap at zero and the main tap three
    // samples later, so passthrough (one spike) and convolution (two spikes)
    // are distinguishable.
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
    expect (writer != nullptr, "could not create the test impulse file");
    juce::AudioBuffer<float> impulse (1, 4);
    impulse.clear();
    impulse.setSample (0, 0, 0.25f);
    impulse.setSample (0, 3, 1.0f);
    writer->writeFromAudioSampleBuffer (impulse, 0, 4);
}

// juce::dsp::Convolution builds a new engine on its background thread and
// installs it from a later process() call, then cross-fades it in over
// 50 ms. Tests therefore pace their blocks in real time; a tight loop would
// measure the placeholder engine.
template <typename RenderBlock>
void renderPaced (RenderBlock&& renderBlock, int blocks)
{
    for (int block = 0; block < blocks; ++block)
    {
        renderBlock();
        juce::Thread::sleep (5);
    }
}

void testRawJuceConvolutionSanity()
{
    // Isolates juce::dsp::Convolution from the cabinet node using the node's
    // exact ingredients (WAV read, normalise, reset after prepare, two
    // convolvers on one queue).
    const double sampleRate = 48000.0;
    const int blockSize = 64;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("signalpatch-test-raw-ir.wav");
    writeTestImpulseFile (file, sampleRate);

    juce::dsp::ConvolutionMessageQueue queue;
    juce::dsp::Convolution convolver (queue);
    juce::dsp::Convolution other (queue);
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 1 };
    convolver.prepare (spec);
    other.prepare (spec);
    convolver.reset();

    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (file));
    expect (reader != nullptr, "raw: reader failed");
    juce::AudioBuffer<float> impulse (1, static_cast<int> (reader->lengthInSamples));
    reader->read (&impulse, 0, impulse.getNumSamples(), 0, true, false);
    convolver.loadImpulseResponse (std::move (impulse), reader->sampleRate,
                                   juce::dsp::Convolution::Stereo::no,
                                   juce::dsp::Convolution::Trim::no,
                                   juce::dsp::Convolution::Normalise::yes);

    std::vector<float> scratch (static_cast<std::size_t> (blockSize), 0.0f);
    auto run = [&]
    {
        float* channels[1] { scratch.data() };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<std::size_t> (blockSize));
        juce::dsp::ProcessContextReplacing<float> context (block);
        convolver.process (context);
    };
    renderPaced ([&] { std::fill (scratch.begin(), scratch.end(), 0.0f); run(); }, 200);
    std::fill (scratch.begin(), scratch.end(), 0.0f);
    scratch[0] = 1.0f;
    run();
    std::string firstSamples;
    for (int sample = 0; sample < 6; ++sample)
        firstSamples += std::to_string (scratch[static_cast<std::size_t> (sample)]) + " ";
    const auto ratio = scratch[0] / scratch[3];
    expect (scratch[3] > 0.1f && std::abs (ratio - 0.25f) < 0.02f,
            "raw juce convolution did not reproduce the IR: " + firstSamples);
    file.deleteFile();
}

void testCabinetConvolvesImpulse()
{
    const double sampleRate = 48000.0;
    const int blockSize = 64;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("signalpatch-test-cab-ir.wav");
    writeTestImpulseFile (file, sampleRate);

    auto node = createNodeProcessor (NodeKind::cabinet);
    expect (node != nullptr, "cabinet node not created");
    node->getParameter (2).setValue (20.0f);    // low cut wide open
    node->getParameter (3).setValue (20000.0f); // high cut wide open: keep the impulse peak in place
    node->prepare (sampleRate, blockSize);
    {
        auto state = std::make_unique<juce::DynamicObject>();
        state->setProperty ("ir", file.getFullPathName());
        node->setExtraState (juce::var (state.release()));
    }
    expect (! node->statusText().contains ("LOAD FAILED"),
            "impulse failed to load: " + node->statusText().toStdString());

    juce::AudioBuffer<float> inputs (node->getNumInputPorts(), blockSize);
    juce::AudioBuffer<float> outputs (node->getNumOutputPorts(), blockSize);
    inputs.clear();
    renderPaced ([&] { node->render (inputs, outputs, blockSize); }, 200);

    inputs.setSample (0, 0, 1.0f);
    node->render (inputs, outputs, blockSize);
    int peakIndex = -1;
    float peak = 0.0f;
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto magnitude = std::abs (outputs.getSample (0, sample));
        if (magnitude > peak)
        {
            peak = magnitude;
            peakIndex = sample;
        }
    }
    std::string firstSamples;
    for (int sample = 0; sample < 8; ++sample)
        firstSamples += std::to_string (outputs.getSample (0, sample)) + " ";
    expect (peak > 0.05f, "cabinet output was silent for an impulse: " + firstSamples);
    expect (peakIndex == 3, "impulse response delay not reproduced (peak at " + std::to_string (peakIndex)
                            + "; first samples " + firstSamples + ")");
    const auto ratio = outputs.getSample (0, 0) / outputs.getSample (0, 3);
    expect (std::abs (ratio - 0.25f) < 0.05f, "impulse taps not in proportion: " + firstSamples);
    file.deleteFile();
}

void testMergeJsonAddsNodesWithFreshIds()
{
    PatchDocument source;
    source.configureHardware (channelNames ("Input", 2), channelNames ("Output", 2));
    const auto drive = source.addNode (NodeKind::distortion, { 100.0f, 100.0f }, 500);
    const auto delay = source.addNode (NodeKind::delay, { 300.0f, 100.0f }, 501);
    source.findNode (drive)->processor->getParameter (0).setValue (13.0f);
    source.findNode (drive)->processor->setName ("Fuzz");
    expectOk (source.addConnection ({ PatchDocument::hardwareInputId, 0, drive, 0 }), "in");
    expectOk (source.addConnection ({ drive, 0, delay, 0 }), "chain");
    expectOk (source.addConnection ({ delay, 0, PatchDocument::hardwareOutputId, 1 }), "out");
    const auto json = source.toJson();

    PatchDocument target;
    target.configureHardware (channelNames ("Input", 2), channelNames ("Output", 2));
    const auto existing = target.addNode (NodeKind::gain, { 0.0f, 0.0f }, 500); // same id as the source's drive
    std::vector<NodeId> added;
    std::vector<Connection> cables;
    expectOk (target.mergeJson (json, { 50.0f, 50.0f }, added, cables), "merge");
    expect (added.size() == 2, "two nodes should have been merged");
    expect (target.findNode (existing)->processor->getKind() == NodeKind::gain, "existing node clobbered by merge");
    expect (cables.size() == 3, "three cables expected after merge");
    const auto* fuzz = target.findNode (added[0]);
    expect (fuzz != nullptr && fuzz->processor->getName() == "Fuzz", "merged node lost its name");
    expect (std::abs (fuzz->processor->getParameter (0).getValue() - 13.0f) < 1.0e-5f, "merged parameter lost");
    expect (fuzz->position == juce::Point<float> (150.0f, 150.0f), "merged node not offset");
    expect (GraphCompiler::compile (target, 64, true).succeeded(), "merged document does not compile");
}

void testBundleRoundTripKeepsAssetsRelative()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("signalpatch-bundle-test-" + juce::Uuid().toString());
    temp.createDirectory();
    const auto impulse = temp.getChildFile ("my cab.wav");
    writeTestImpulseFile (impulse, 48000.0);

    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    const auto cab = document.addNode (NodeKind::cabinet, { 10.0f, 10.0f });
    {
        auto state = std::make_unique<juce::DynamicObject>();
        state->setProperty ("ir", impulse.getFullPathName());
        document.findNode (cab)->processor->setExtraState (juce::var (state.release()));
    }
    const auto nam = document.addNode (NodeKind::neuralPedal, { 200.0f, 10.0f });
    {
        auto state = std::make_unique<juce::DynamicObject>();
        state->setProperty ("model", temp.getChildFile ("does-not-exist.nam").getFullPathName());
        document.findNode (nam)->processor->setExtraState (juce::var (state.release()));
    }

    const auto zip = temp.getChildFile ("MyRig.zip");
    const auto exported = bundle::exportBundle (document, zip);
    expect (zip.existsAsFile(), "export did not write a zip");
    expect (exported.failed() && exported.getErrorMessage().contains ("does-not-exist.nam"),
            "export should report the missing model but still write the bundle");

    const auto unpackRoot = temp.getChildFile ("unpacked");
    juce::File patchFile;
    expectOk (bundle::extractBundle (zip, unpackRoot, patchFile), "extract");
    expect (patchFile.getFileName() == "MyRig.signalpatch", "bundle patch not named after the zip: " + patchFile.getFileName().toStdString());
    auto json = juce::JSON::parse (patchFile);
    juce::String storedIr;
    for (const auto& nodeValue : *json.getDynamicObject()->getProperty ("nodes").getArray())
        if (auto* extra = nodeValue.getDynamicObject()->getProperty ("extra").getDynamicObject())
            if (extra->hasProperty ("ir"))
                storedIr = extra->getProperty ("ir").toString();
    expect (storedIr == "assets/irs/my cab.wav", "impulse path not relative inside the bundle: " + storedIr.toStdString());
    expect (patchFile.getParentDirectory().getChildFile (storedIr).existsAsFile(), "impulse not copied into the bundle");

    // Loading resolves the relative path against the bundle folder.
    bundle::rebaseAssetPaths (json, patchFile.getParentDirectory(), false);
    PatchDocument reloaded;
    reloaded.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    expectOk (reloaded.loadJson (json), "reload");
    juce::String resolved;
    for (const auto& node : reloaded.getNodes())
        if (node.processor->getKind() == NodeKind::cabinet)
            resolved = node.processor->getExtraState().getProperty ("ir", "").toString();
    expect (juce::File::isAbsolutePath (resolved) && juce::File (resolved).existsAsFile(),
            "reloaded impulse path did not resolve: " + resolved.toStdString());

    // Saving next to assets keeps paths relative; saving elsewhere keeps them absolute.
    auto saved = reloaded.toJson();
    bundle::rebaseAssetPaths (saved, patchFile.getParentDirectory(), true);
    bool relative = false;
    for (const auto& nodeValue : *saved.getDynamicObject()->getProperty ("nodes").getArray())
        if (auto* extra = nodeValue.getDynamicObject()->getProperty ("extra").getDynamicObject())
            if (extra->hasProperty ("ir"))
                relative = ! juce::File::isAbsolutePath (extra->getProperty ("ir").toString());
    expect (relative, "re-save inside the project folder should store a relative path");
    temp.deleteRecursively();
}

void testBoardPositionsAndGroupsRoundTrip()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    const auto drive = document.addNode (NodeKind::distortion, { 10.0f, 10.0f }, 500);
    const auto delay = document.addNode (NodeKind::delay, { 300.0f, 10.0f }, 501);
    document.findNode (drive)->boardPosition = juce::Point<float> (120.0f, 40.0f);
    PedalGroup group;
    group.name = "Dirt";
    group.members = { drive, delay };
    group.knobs = { { drive, 0 }, { delay, 1 } };
    group.boardPosition = juce::Point<float> (5.0f, 6.0f);
    document.setGroups ({ group });
    expect (document.getGroups().size() == 1 && document.getGroups().front().id > 0, "group did not get an id");

    PatchDocument reloaded;
    reloaded.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    expectOk (reloaded.loadJson (document.toJson()), "reload with groups");
    const auto* reloadedDrive = reloaded.findNode (drive);
    expect (reloadedDrive != nullptr && reloadedDrive->boardPosition.has_value()
            && reloadedDrive->boardPosition->x == 120.0f, "board position lost");
    expect (reloaded.findNode (delay)->boardPosition.has_value() == false, "unset board position became set");
    expect (reloaded.getGroups().size() == 1, "group lost in the round trip");
    const auto& back = reloaded.getGroups().front();
    expect (back.name == "Dirt" && back.members.size() == 2 && back.knobs.size() == 2 && back.knobs[1] == std::make_pair (delay, 1)
            && back.boardPosition.has_value() && back.boardPosition->y == 6.0f, "group contents changed in the round trip");

    // Deleting a member scrubs it from the group; a group with no members goes away.
    reloaded.removeNode (delay);
    expect (reloaded.getGroups().front().members.size() == 1 && reloaded.getGroups().front().knobs.size() == 1, "removed node still referenced by the group");
    reloaded.removeNode (drive);
    expect (reloaded.getGroups().empty(), "empty group survived");
}

void testLooperRecordsClosesOverdubsAndUndoes()
{
    auto node = createNodeProcessor (NodeKind::looper);
    expect (node != nullptr, "looper not created");
    const int block = 64;
    node->prepare (48000.0, block);
    node->getParameter (1).setValue (-60.0f); // dry down so the output is the loop alone
    juce::AudioBuffer<float> inputs (node->getNumInputPorts(), block);
    juce::AudioBuffer<float> outputs (node->getNumOutputPorts(), block);
    auto render = [&] (float level)
    {
        inputs.clear();
        for (int i = 0; i < block; ++i)
            inputs.setSample (0, i, level);
        node->render (inputs, outputs, block);
        float peak = 0.0f;
        for (int i = 0; i < block; ++i)
            peak = juce::jmax (peak, std::abs (outputs.getSample (0, i)));
        return peak;
    };
    for (int i = 0; i < 20; ++i) render (0.0f); // settle the dry smoother
    expect (node->statusText().startsWith ("EMPTY"), "looper should start empty");

    node->handleUiCommand ("rec");
    for (int i = 0; i < 10; ++i) render (0.5f);          // record 10 blocks of 0.5
    expect (node->uiToggleState ("rec"), "should be recording");
    node->handleUiCommand ("rec");                        // close the loop
    render (0.0f);
    expect (node->statusText().startsWith ("PLAY"), "loop should play after closing: " + node->statusText().toStdString());
    float playbackPeak = 0.0f;
    for (int i = 0; i < 5; ++i) playbackPeak = juce::jmax (playbackPeak, render (0.0f));
    expect (playbackPeak > 0.4f && playbackPeak < 0.6f, "playback level wrong: " + std::to_string (playbackPeak));

    node->handleUiCommand ("rec");                        // overdub 0.25 on top for a full cycle
    for (int i = 0; i < 12; ++i) render (0.25f);
    node->handleUiCommand ("rec");                        // stop overdubbing
    float overdubbed = 0.0f;
    for (int i = 0; i < 12; ++i) overdubbed = juce::jmax (overdubbed, render (0.0f));
    expect (overdubbed > 0.65f, "overdub not added: " + std::to_string (overdubbed));

    node->handleUiCommand ("undo");
    float undone = 0.0f;
    for (int i = 0; i < 12; ++i) undone = juce::jmax (undone, render (0.0f));
    expect (undone < 0.6f && undone > 0.4f, "undo did not restore the previous pass: " + std::to_string (undone));

    node->handleUiCommand ("clear");
    render (0.0f);
    expect (node->statusText().startsWith ("EMPTY"), "clear should empty the loop");
}

void testLiveRateChangeKeepsRecordings()
{
    // Looper, 4-track and sampler keep their audio when the device switches from 48 to 96 kHz.
    auto looper = createNodeProcessor (NodeKind::looper);
    looper->prepare (48000.0, 64);
    juce::AudioBuffer<float> take (1, 24000);
    for (int i = 0; i < take.getNumSamples(); ++i) take.setSample (0, i, 0.5f * std::sin (static_cast<float> (i) * 0.02f));
    looper->importAudioContent (take);
    looper->prepare (96000.0, 64);
    const auto after = looper->exportAudioContent().getNumSamples();
    expect (std::abs (after - 48000) < 16, "a half-second loop must still be half a second at 96 kHz, got " + std::to_string (after));

    auto deck = createNodeProcessor (NodeKind::fourTrack);
    deck->prepare (48000.0, 64);
    juce::AudioBuffer<float> tapes (4, 48000);
    tapes.clear();
    for (int i = 0; i < 48000; ++i) tapes.setSample (2, i, 0.25f);
    deck->importAudioContent (tapes);
    deck->prepare (96000.0, 64);
    const auto deckAfter = deck->exportAudioContent();
    expect (deckAfter.getNumChannels() == 4 && std::abs (deckAfter.getNumSamples() - 96000) < 32, "one second on track 3 must stay one second, got " + std::to_string (deckAfter.getNumSamples()));
    expect (std::abs (deckAfter.getSample (2, 50000) - 0.25f) < 0.01f, "the tape content must survive the resample");
}

void testPluckIsInTune()
{
    auto pluck = createNodeProcessor (NodeKind::pluck);
    const int block = 256;
    pluck->prepare (48000.0, block);
    const auto triggerPort = 0;
    std::string report;
    double worst = 0.0;
    for (const auto semitones : { 0.0f, 12.0f, 19.0f, 24.0f }) // A2, A3, E4, A4 (the knob tops out at +24)
    {
        pluck->reset();
        pluck->getParameter (0).setValue (semitones);
        pluck->getParameter (1).setValue (0.0f); // no extra damping
        juce::AudioBuffer<float> inputs (pluck->getNumInputPorts(), block), outputs (1, block);
        std::vector<float> signal;
        inputs.clear();
        for (int b = 0; b < 60; ++b) pluck->render (inputs, outputs, block); // let the Pitch knob's smoother arrive before plucking
        for (int b = 0; b < 40; ++b)
        {
            inputs.clear();
            if (b == 0) for (int i = 0; i < 32; ++i) inputs.setSample (triggerPort, i, 1.0f);
            for (int i = 0; i < block; ++i) inputs.setSample (0, i, inputs.getSample (triggerPort, i));
            pluck->render (inputs, outputs, block);
            for (int i = 0; i < block; ++i) signal.push_back (outputs.getSample (0, i));
        }
        // Period by autocorrelation around the expected lag, with parabolic refinement.
        const auto expectedHz = 110.0 * std::pow (2.0, semitones / 12.0);
        const auto expectedLag = 48000.0 / expectedHz;
        const int from = static_cast<int> (expectedLag * 0.8), to = static_cast<int> (expectedLag * 1.2) + 2;
        const int start = 2048, length = 4096;
        auto correlation = [&] (int lag) { double sum = 0.0; for (int i = 0; i < length; ++i) sum += signal[static_cast<std::size_t> (start + i)] * signal[static_cast<std::size_t> (start + i + lag)]; return sum; };
        int best = from; double bestValue = -1.0e9;
        for (int lag = from; lag <= to; ++lag) { const auto c = correlation (lag); if (c > bestValue) { bestValue = c; best = lag; } }
        const auto c0 = correlation (best - 1), c1 = correlation (best), c2 = correlation (best + 1);
        const auto refined = best + 0.5 * (c0 - c2) / (c0 - 2.0 * c1 + c2);
        const auto cents = 1200.0 * std::log2 (expectedLag / refined);
        report += std::to_string (static_cast<int> (semitones)) + " st: " + std::to_string (cents) + " c; ";
        worst = std::max (worst, std::abs (cents));
    }
    expect (worst < 3.0, "pluck tuning: " + report);
}

void testRecordedAudioAtAnotherRateKeepsItsLength()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("signalpatch-rate-test-" + juce::Uuid().toString());
    temp.createDirectory();
    const auto patchFile = temp.getChildFile ("rate.signalpatch");
    PatchDocument source;
    source.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    source.prepareAll (48000.0, 64);
    const auto looper = source.addNode (NodeKind::looper, { 0.0f, 0.0f }, 710);
    juce::AudioBuffer<float> take (1, 48000); // one second at 48 kHz
    for (int i = 0; i < take.getNumSamples(); ++i)
        take.setSample (0, i, 0.5f * std::sin (static_cast<float> (i) * 0.01f));
    source.findNode (looper)->processor->importAudioContent (take);
    std::unordered_map<NodeId, juce::uint32> saved;
    const auto json = bundle::toJsonWithAudio (source, patchFile, saved);
    expect (patchFile.getSiblingFile ("assets").getChildFile ("audio").getChildFile ("rate-710.wav").existsAsFile(), "the take should be written");
    expect (! patchFile.getSiblingFile ("assets").getChildFile ("audio").getChildFile ("rate-710.wav.part").existsAsFile(), "no partial file should remain");

    PatchDocument reopened;
    reopened.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    reopened.prepareAll (96000.0, 64);
    expectOk (reopened.loadJson (json), "reload at 96 kHz");
    reopened.prepareAll (96000.0, 64);
    bundle::loadAudioContent (reopened, json, temp);
    const auto length = reopened.findNode (looper)->processor->exportAudioContent().getNumSamples();
    expect (std::abs (length - 96000) < 16, "a one-second take must stay one second at 96 kHz, got " + std::to_string (length) + " samples");
    temp.deleteRecursively();
}

void testLooperOverdubOncePerSlotAndUndoAtAnySpeed()
{
    const int block = 64;
    for (const auto* mode : { "half", "reverse", "fast" })
    {
        auto node = createNodeProcessor (NodeKind::looper);
        node->prepare (48000.0, block);
        juce::AudioBuffer<float> inputs (node->getNumInputPorts(), block), outputs (1, block);
        auto render = [&] (float level) { inputs.clear(); for (int i = 0; i < block; ++i) inputs.setSample (0, i, level); node->render (inputs, outputs, block); };
        // A loop with a distinct value in every slot (a ramp), 75 blocks = 4800 samples.
        node->handleUiCommand ("rec");
        render (0.0f); // the command lands at the block start
        int written = 0;
        for (int b = 0; b < 75; ++b)
        {
            inputs.clear();
            for (int i = 0; i < block; ++i) inputs.setSample (0, i, -0.5f + static_cast<float> (written++) / 4800.0f);
            node->render (inputs, outputs, block);
        }
        node->handleUiCommand ("rec");
        render (0.0f);
        const auto original = node->exportAudioContent();
        expect (original.getNumSamples() > 4000, std::string (mode) + ": the loop should have closed");
        const auto versionBefore = node->audioContentVersion();

        if (std::string (mode) == "half") node->handleUiCommand ("half");
        if (std::string (mode) == "reverse") node->handleUiCommand ("reverse");
        if (std::string (mode) == "fast") node->getParameter (3).setValue (200.0f);
        for (int b = 0; b < 30; ++b) render (0.0f); // let the speed smoother settle and move the head
        node->handleUiCommand ("rec");              // overdub +0.25 for 20 blocks
        for (int b = 0; b < 20; ++b) render (0.25f);
        node->handleUiCommand ("play");             // stop the overdub with PLAY
        render (0.0f);
        expect (node->audioContentVersion() != versionBefore, std::string (mode) + ": stopping an overdub with PLAY must change the saved-content version");
        const auto dubbed = node->exportAudioContent();
        int touched = 0;
        for (int i = 0; i < dubbed.getNumSamples(); ++i)
        {
            const auto delta = dubbed.getSample (0, i) - original.getSample (0, i);
            if (std::abs (delta) > 1.0e-4f)
            {
                ++touched;
                expect (std::abs (delta - 0.25f) < 1.0e-3f, std::string (mode) + ": each slot must get the overdub exactly once, got +" + std::to_string (delta) + " at " + std::to_string (i));
            }
        }
        expect (touched > 300, std::string (mode) + ": the overdub should have covered a run of slots: " + std::to_string (touched));

        node->handleUiCommand ("undo");
        for (int b = 0; b < 4; ++b) render (0.0f);  // the restore may take a few blocks
        const auto undone = node->exportAudioContent();
        for (int i = 0; i < undone.getNumSamples(); ++i)
            if (std::abs (undone.getSample (0, i) - original.getSample (0, i)) > 1.0e-5f)
                expect (false, std::string (mode) + ": undo must restore every slot and touch nothing else (slot " + std::to_string (i) + ")");
    }
}

void testFourTrackSyncWithTrackOneStoppedAndNoDoubledMonitor()
{
    auto deck = createNodeProcessor (NodeKind::fourTrack);
    const int block = 480;
    deck->prepare (48000.0, block);
    juce::AudioBuffer<float> inputs (deck->getNumInputPorts(), block), outputs (1, block);
    auto feed = [&] (float level) { inputs.clear(); for (int i = 0; i < block; ++i) inputs.setSample (0, i, level); deck->render (inputs, outputs, block); };

    // Recording an armed track: the output is the input once, not input + tape readback.
    deck->handleUiCommand ("arm1");
    deck->handleUiCommand ("rec");
    deck->handleUiCommand ("play");
    feed (0.2f);
    feed (0.2f);
    expect (std::abs (outputs.getSample (0, 200) - 0.2f) < 1.0e-4f, "recording must monitor the input once, got " + std::to_string (outputs.getSample (0, 200)));
    const auto versionDuringTake = deck->audioContentVersion();
    deck->handleUiCommand ("play"); // end the take with PLAY, REC stays lit
    feed (0.0f);
    expect (deck->audioContentVersion() != versionDuringTake, "a take ended by PLAY must change the saved-content version");

    // SYNC on (default), track 1 stopped: tracks 2-4 keep moving along the tape.
    deck->handleUiCommand ("rec");
    deck->handleUiCommand ("arm1");
    deck->handleUiCommand ("play1");
    deck->handleUiCommand ("rtz");
    deck->handleUiCommand ("play");
    feed (0.0f);
    const auto first = deck->lanePosition (1);
    for (int b = 0; b < 100; ++b) feed (0.0f);   // one second
    const auto later = deck->lanePosition (1);
    expect (later >= 0.0f && (later - first) * 60.0f > 0.9f, "track 2 must advance ~1 s while track 1 is stopped, moved " + std::to_string ((later - first) * 60.0f) + " s");
}

void testStereoBypassKeepsBothSides()
{
    for (const auto kind : { NodeKind::stereoDelay, NodeKind::stereoChorus, NodeKind::stereoReverb })
    {
        auto node = createNodeProcessor (kind);
        const int block = 64;
        node->prepare (48000.0, block);
        node->setBypassed (true);
        juce::AudioBuffer<float> inputs (node->getNumInputPorts(), block), outputs (node->getNumOutputPorts(), block);
        inputs.clear();
        for (int i = 0; i < block; ++i) { inputs.setSample (0, i, 0.3f); inputs.setSample (1, i, -0.2f); }
        node->render (inputs, outputs, block);
        expect (std::abs (outputs.getSample (0, 10) - 0.3f) < 1.0e-6f && std::abs (outputs.getSample (1, 10) + 0.2f) < 1.0e-6f,
                "bypass must pass In L to Out L and In R to Out R for " + nodeKindKey (kind).toStdString());
    }
    auto pan = createNodeProcessor (NodeKind::pan);
    pan->prepare (48000.0, 64);
    pan->setBypassed (true);
    juce::AudioBuffer<float> inputs (pan->getNumInputPorts(), 64), outputs (2, 64);
    inputs.clear();
    for (int i = 0; i < 64; ++i) inputs.setSample (0, i, 0.4f);
    pan->render (inputs, outputs, 64);
    expect (std::abs (outputs.getSample (0, 5) - 0.4f) < 1.0e-6f && std::abs (outputs.getSample (1, 5) - 0.4f) < 1.0e-6f, "a bypassed Pan feeds its one input to both sides");
}

void testRecordedAudioSavesAndLoadsWithThePatch()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("signalpatch-audio-test-" + juce::Uuid().toString());
    temp.createDirectory();
    const auto patchFile = temp.getChildFile ("loops.signalpatch");

    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    document.prepareAll (48000.0, 64);
    const auto looper = document.addNode (NodeKind::looper, { 0.0f, 0.0f }, 700);
    juce::AudioBuffer<float> loop (1, 4800);
    for (int i = 0; i < 4800; ++i)
        loop.setSample (0, i, std::sin (static_cast<float> (i) * 0.01f) * 0.5f);
    document.findNode (looper)->processor->importAudioContent (loop);
    expect (document.findNode (looper)->processor->hasAudioContent(), "looper should report content after import");

    std::unordered_map<NodeId, juce::uint32> saved;
    const auto json = bundle::toJsonWithAudio (document, patchFile, saved);
    const auto audioFile = temp.getChildFile ("assets").getChildFile ("audio").getChildFile ("loops-700.wav");
    expect (audioFile.existsAsFile(), "loop WAV not written next to the patch");
    juce::String stored;
    for (const auto& nodeValue : *json.getDynamicObject()->getProperty ("nodes").getArray())
        if (auto* object = nodeValue.getDynamicObject(); object != nullptr && object->hasProperty ("audio"))
            stored = object->getProperty ("audio").toString();
    expect (stored == "assets/audio/loops-700.wav", "audio path should be relative: " + stored.toStdString());

    // Unchanged content is not rewritten; changed content is.
    const auto modified = audioFile.getLastModificationTime();
    juce::Thread::sleep (20);
    bundle::toJsonWithAudio (document, patchFile, saved);
    expect (audioFile.getLastModificationTime() == modified, "unchanged loop was rewritten");

    PatchDocument reloaded;
    reloaded.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    reloaded.prepareAll (48000.0, 64);
    expectOk (reloaded.loadJson (json), "reload");
    bundle::loadAudioContent (reloaded, json, patchFile.getParentDirectory());
    const auto* back = reloaded.findNode (looper);
    expect (back != nullptr && back->processor->hasAudioContent(), "loop did not come back with the patch");
    const auto audio = back->processor->exportAudioContent();
    expect (audio.getNumSamples() == 4800, "loop length changed: " + std::to_string (audio.getNumSamples()));
    expect (std::abs (audio.getSample (0, 100) - loop.getSample (0, 100)) < 1.0e-4f, "loop samples changed in the round trip");
    temp.deleteRecursively();
}

void testDrumMachineTapTempo()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    const auto drums = document.addNode (NodeKind::drumMachine, { 0.0f, 0.0f }, 820);
    auto& node = *document.findNode (drums)->processor;
    auto& bpm = node.getParameter (0);
    expect (bpm.name == "Tempo", "parameter 0 should be the tempo");
    const auto before = bpm.getValue();
    expect (node.handleUiCommand ("tap"), "tap should be a known command");
    expect (std::abs (bpm.getValue() - before) < 1.0e-6f, "one tap must not change the tempo");
    // Three more taps 150 ms apart: 400 bpm is out of range, so the knob clamps to 240 - use 300 ms => 200 bpm.
    for (int i = 0; i < 3; ++i)
    {
        juce::Thread::sleep (300);
        node.handleUiCommand ("tap");
    }
    expect (std::abs (bpm.getValue() - 200.0f) < 12.0f, "four taps at 300 ms should give ~200 bpm, got " + std::to_string (bpm.getValue()));
    expect (! node.handleUiCommand ("nonsense"), "unknown commands are refused");
}

void testControllerFeedback()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    const auto drive = document.addNode (NodeKind::distortion, { 0.0f, 0.0f }, 810);
    const auto loop = document.addNode (NodeKind::looper, { 0.0f, 0.0f }, 811);
    document.findNode (drive)->processor->setName ("Fuzz Face");
    MidiMapping stomp;
    stomp.source = MidiMapping::Source::note; stomp.number = controller::firstSwitchNote; stomp.channel = controller::switchChannel;
    stomp.target = MidiMapping::Target::bypass; stomp.node = drive;
    MidiMapping rec;
    rec.source = MidiMapping::Source::note; rec.number = controller::firstSwitchNote + 1; rec.channel = 0;
    rec.target = MidiMapping::Target::command; rec.node = loop; rec.command = "rec";
    MidiMapping slot;
    slot.source = MidiMapping::Source::note; slot.number = controller::firstSwitchNote + 7; slot.channel = controller::switchChannel;
    slot.target = MidiMapping::Target::slot; slot.slot = 2;
    document.setMidiMappings ({ stomp, rec, slot });

    auto state = controller::computeState (document, 2, "Wednesday rig, the long name");
    expect (state.switches[0].led == 1 && state.switches[0].label == "Fuzz Fac", "switch 1 should show the enabled pedal, label cut to 8: " + state.switches[0].label.toStdString());
    expect (state.switches[1].led == 0 && state.switches[1].label.startsWith ("REC "), "switch 2 should be the looper's REC: " + state.switches[1].label.toStdString());
    expect (state.switches[7].led == 1 && state.switches[7].label == "RIG 3", "switch 8 should light for the live slot");
    expect (state.switches[3].led == 0 && state.switches[3].label.isEmpty(), "unmapped switch should be dark and blank");
    expect (state.rig == "Wednesday rig, t", "rig name should be cut to 16");

    const auto full = controller::encode ({}, state, true);
    expect (full.size() == static_cast<std::size_t> (controller::switchCount * 2 + controller::slotCount + 1), "full dump message count: " + std::to_string (full.size()));
    const auto* led = full[0].getSysExData();
    expect (full[0].isSysEx() && led[0] == 0x7D && led[1] == 0x53 && led[2] == 0x01 && led[3] == 0 && led[4] == 1, "switch 1 LED message bytes");
    const auto* label = full[1].getSysExData();
    expect (full[1].getSysExDataSize() == 4 + 8 && label[2] == 0x02 && label[3] == 0 && label[4] == 'F', "switch 1 label message bytes");

    document.findNode (drive)->processor->setBypassed (true);
    auto next = controller::computeState (document, 1, "Wednesday rig, the long name");
    const auto diff = controller::encode (state, next, false);
    // One LED (switch 1 off), one LED (switch 8 off), slot 3 off + slot 2 on.
    expect (diff.size() == 4, "diff should carry only what changed: " + std::to_string (diff.size()));
    expect (diff[0].getSysExData()[2] == 0x01 && diff[0].getSysExData()[4] == 0, "switch 1 should go dark");

    expect (controller::isHello (controller::helloMessage (controller::fromController), controller::fromController), "hello should be recognised");
    expect (! controller::isHello (controller::helloMessage (controller::fromSignalPatch), controller::fromController), "our own hello must not count as a controller's");
}

void testMidiMappingsRoundTripAndScrub()
{
    PatchDocument document;
    document.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    const auto drive = document.addNode (NodeKind::distortion, { 0.0f, 0.0f }, 800);
    const auto loop = document.addNode (NodeKind::looper, { 0.0f, 0.0f }, 801);
    MidiMapping knob;
    knob.source = MidiMapping::Source::controlChange; knob.number = 21; knob.channel = 0;
    knob.target = MidiMapping::Target::parameter; knob.node = drive; knob.parameter = 0;
    MidiMapping stomp;
    stomp.source = MidiMapping::Source::note; stomp.number = 60; stomp.channel = 10;
    stomp.target = MidiMapping::Target::bypass; stomp.node = drive;
    MidiMapping rec;
    rec.source = MidiMapping::Source::controlChange; rec.number = 64;
    rec.target = MidiMapping::Target::command; rec.node = loop; rec.command = "rec";
    MidiMapping slot;
    slot.source = MidiMapping::Source::programChange; slot.number = 2;
    slot.target = MidiMapping::Target::slot; slot.slot = 2;
    MidiMapping encoder = knob;
    encoder.number = 20; encoder.relative = true;
    MidiMapping pedal = knob;
    pedal.number = 11; pedal.low = 20; pedal.high = 110;
    MidiMapping dangling = knob;
    dangling.node = 999; // no such node: dropped on set
    document.setMidiMappings ({ knob, stomp, rec, slot, encoder, pedal, dangling });
    expect (document.getMidiMappings().size() == 6, "dangling mapping should be dropped");

    PatchDocument reloaded;
    reloaded.configureHardware (channelNames ("Input", 1), channelNames ("Output", 1));
    expectOk (reloaded.loadJson (document.toJson()), "reload with midi");
    const auto& back = reloaded.getMidiMappings();
    expect (back.size() == 6, "mappings lost in the round trip");
    expect (back[0].matches (MidiMapping::Source::controlChange, 5, 21) && back[0].target == MidiMapping::Target::parameter && back[0].parameter == 0, "knob mapping changed");
    expect (back[1].matches (MidiMapping::Source::note, 10, 60) && ! back[1].matches (MidiMapping::Source::note, 1, 60), "channel filter lost");
    expect (back[2].command == "rec" && back[2].node == loop, "command mapping changed");
    expect (back[3].target == MidiMapping::Target::slot && back[3].slot == 2 && back[3].sourceLabel() == "PC2", "slot mapping changed");
    expect (back[4].relative && ! back[0].relative, "relative flag lost in the round trip");
    expect (back[0].low == 0 && back[0].high == 127, "default span should stay 0-127");
    expect (back[5].low == 20 && back[5].high == 110, "calibrated span lost in the round trip");
    expect (std::abs (back[5].normalised (20)) < 1.0e-6f && std::abs (back[5].normalised (110) - 1.0f) < 1.0e-6f && std::abs (back[5].normalised (65) - 0.5f) < 1.0e-6f
            && back[5].normalised (0) == 0.0f && back[5].normalised (127) == 1.0f, "calibrated span should map heel..toe onto 0..1 and clamp outside");
    MidiMapping inverted = back[5];
    inverted.low = 110; inverted.high = 20;
    expect (std::abs (inverted.normalised (110)) < 1.0e-6f && std::abs (inverted.normalised (20) - 1.0f) < 1.0e-6f, "swapped ends should invert the pedal");

    reloaded.removeNode (drive);
    expect (reloaded.getMidiMappings().size() == 2, "mappings to a deleted node should be scrubbed");
}

void testStereoNodes()
{
    const int block = 64;
    // Pan: centre is equal power, hard left is silent on the right.
    {
        auto pan = createNodeProcessor (NodeKind::pan);
        pan->prepare (48000.0, block);
        juce::AudioBuffer<float> inputs (pan->getNumInputPorts(), block), outputs (2, block);
        auto run = [&] (float value)
        {
            inputs.clear();
            for (int i = 0; i < block; ++i) inputs.setSample (0, i, 1.0f);
            pan->getParameter (0).setValue (value);
            for (int pass = 0; pass < 40; ++pass) pan->render (inputs, outputs, block); // let smoothing settle
        };
        run (0.0f);
        expect (std::abs (outputs.getSample (0, 32) - 0.7071f) < 0.02f && std::abs (outputs.getSample (1, 32) - 0.7071f) < 0.02f,
                "centre pan is not equal power");
        run (-100.0f);
        expect (outputs.getSample (0, 32) > 0.98f && std::abs (outputs.getSample (1, 32)) < 0.02f, "hard left leaks into the right");
    }
    // Stereo delay: a click on the left bounces to the right with ping-pong.
    {
        auto delay = createNodeProcessor (NodeKind::stereoDelay);
        delay->getParameter (0).setValue (10.0f);   // 10 ms = 480 samples
        delay->getParameter (1).setValue (60.0f);   // feedback
        delay->getParameter (2).setValue (100.0f);  // wet only
        delay->getParameter (3).setValue (100.0f);  // full ping-pong
        delay->getParameter (4).setValue (100.0f);  // same time on R
        delay->getParameter (5).setValue (0.0f);    // do not sum inputs
        delay->prepare (48000.0, block);
        juce::AudioBuffer<float> inputs (delay->getNumInputPorts(), block), outputs (2, block);
        inputs.clear();
        for (int pass = 0; pass < 40; ++pass) delay->render (inputs, outputs, block);
        inputs.setSample (0, 0, 1.0f);
        float leftPeak = 0.0f, rightPeak = 0.0f, rightAtFirstEcho = 0.0f;
        for (int pass = 0; pass < 40; ++pass)
        {
            delay->render (inputs, outputs, block);
            inputs.clear();
            for (int i = 0; i < block; ++i)
            {
                leftPeak = juce::jmax (leftPeak, std::abs (outputs.getSample (0, i)));
                rightPeak = juce::jmax (rightPeak, std::abs (outputs.getSample (1, i)));
            }
            if (pass == 7) // samples 448..511 hold the first echo at 480
                for (int i = 0; i < block; ++i) rightAtFirstEcho = juce::jmax (rightAtFirstEcho, std::abs (outputs.getSample (1, i)));
        }
        expect (leftPeak > 0.4f, "left echo missing: " + std::to_string (leftPeak));
        expect (rightPeak > 0.2f, "ping-pong never reached the right: " + std::to_string (rightPeak));
        expect (rightAtFirstEcho < 0.05f, "right should be silent at the first echo with full ping-pong from L");
    }
    // Cabinet: the R output mirrors the main output with one impulse loaded.
    {
        auto cab = createNodeProcessor (NodeKind::cabinet);
        expect (cab->getNumOutputPorts() == 2, "cabinet should have a right output");
    }
    // Stereo reverb and chorus produce finite output from a mono left feed.
    for (const auto kind : { NodeKind::stereoReverb, NodeKind::stereoChorus })
    {
        auto node = createNodeProcessor (kind);
        node->prepare (48000.0, block);
        juce::AudioBuffer<float> inputs (node->getNumInputPorts(), block), outputs (2, block);
        float rightEnergy = 0.0f;
        for (int pass = 0; pass < 60; ++pass)
        {
            inputs.clear();
            for (int i = 0; i < block; ++i) inputs.setSample (0, i, 0.3f * std::sin (static_cast<float> (pass * block + i) * 0.05f));
            node->render (inputs, outputs, block);
            for (int i = 0; i < block; ++i)
            {
                expect (std::isfinite (outputs.getSample (0, i)) && std::isfinite (outputs.getSample (1, i)), "stereo node produced non-finite output");
                rightEnergy += outputs.getSample (1, i) * outputs.getSample (1, i);
            }
        }
        expect (rightEnergy > 0.01f, "mono left feed should reach the right channel (sum inputs)");
    }
}

void testTunerDetectsPitch()
{
    auto tuner = createNodeProcessor (NodeKind::tuner);
    const int block = 64;
    tuner->prepare (48000.0, block);
    juce::AudioBuffer<float> inputs (tuner->getNumInputPorts(), block), outputs (1, block);
    double phase = 0.0;
    for (int pass = 0; pass < 120; ++pass) // > 4096 samples of 110 Hz (A2) with a little harmonic content
    {
        inputs.clear();
        for (int i = 0; i < block; ++i)
        {
            inputs.setSample (0, i, 0.4f * static_cast<float> (std::sin (phase)) + 0.15f * static_cast<float> (std::sin (2.0 * phase)));
            phase += juce::MathConstants<double>::twoPi * 110.0 / 48000.0;
        }
        tuner->render (inputs, outputs, block);
    }
    juce::Thread::sleep (45); // past the analysis cache
    const auto status = tuner->statusText();
    expect (status.startsWith ("A2"), "tuner should read A2 for 110 Hz: " + status.toStdString());
    const auto needle = tuner->currentStep();
    expect (needle >= 45 && needle <= 55, "needle should sit near centre: " + std::to_string (needle));
}

namespace
{
    int risingEdges (const juce::AudioBuffer<float>& buffer, int channel, int numSamples, bool& wasHigh)
    {
        int edges = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            const bool high = buffer.getSample (channel, i) > 0.5f;
            if (high && ! wasHigh)
                ++edges;
            wasHigh = high;
        }
        return edges;
    }
}

void testClockPulsesAndFollowers()
{
    const int block = 64;
    // The clock: 120 bpm, 4/4. One second holds beats at 0 and 0.5 s, eighths at 0/.25/.5/.75, the bar at 0.
    auto clock = createNodeProcessor (NodeKind::clock);
    clock->prepare (48000.0, block);
    juce::AudioBuffer<float> clockInputs (juce::jmax (1, clock->getNumInputPorts()), block), pulses (3, block);
    clockInputs.clear();
    int beats = 0, eighths = 0, bars = 0;
    bool beatHigh = false, eighthHigh = false, barHigh = false;
    for (int rendered = 0; rendered < 48000; rendered += block)
    {
        clock->render (clockInputs, pulses, block);
        beats += risingEdges (pulses, 0, block, beatHigh);
        eighths += risingEdges (pulses, 1, block, eighthHigh);
        bars += risingEdges (pulses, 2, block, barHigh);
    }
    expect (beats == 2 && eighths == 4 && bars == 1, "one second at 120 bpm: beats " + std::to_string (beats) + " eighths " + std::to_string (eighths) + " bars " + std::to_string (bars));
    expect (clock->handleUiCommand ("reset"), "reset is a command");
    clock->render (clockInputs, pulses, block);
    expect (pulses.getSample (2, 0) == 1.0f, "reset should restart on a downbeat");
    expect (clock->handleUiCommand ("run") && ! clock->uiToggleState ("run"), "run toggles off");
    clock->render (clockInputs, pulses, block);
    beatHigh = false;
    for (int rendered = 0; rendered < 48000; rendered += block)
    {
        clock->render (clockInputs, pulses, block);
        beats += risingEdges (pulses, 0, block, beatHigh);
    }
    expect (beats == 2, "a stopped clock must not pulse");

    // The drum machine follows pulses on its Clock port and ignores its own tempo while they come.
    auto drums = createNodeProcessor (NodeKind::drumMachine);
    drums->prepare (48000.0, block);
    drums->getParameter (0).setValue (40.0f); // internal step every 0.75 s
    const auto drumClockPort = drums->getNumInputPorts() - 1;
    expect (drums->getInputPort (drumClockPort).name == "Clock", "the drum machine's last input should be the clock");
    juce::AudioBuffer<float> drumInputs (drums->getNumInputPorts(), block), drumOut (1, block);
    drumInputs.clear();
    drums->render (drumInputs, drumOut, block); // first sample of the internal clock advances 7 -> 0
    const auto startStep = drums->currentStep();
    for (int pulse = 0; pulse < 4; ++pulse)
    {
        drumInputs.clear();
        for (int i = 0; i < 16; ++i)
            drumInputs.setSample (drumClockPort, i, 0.8f); // regular pulses are 0.8 high
        drums->render (drumInputs, drumOut, block);
        drumInputs.clear();
        drums->render (drumInputs, drumOut, block);
    }
    expect (drums->currentStep() == (startStep + 4) % 8, "four pulses should advance four steps: " + std::to_string (drums->currentStep()));
    for (int rendered = 0; rendered < 48000; rendered += block)
        drums->render (drumInputs, drumOut, block); // a second of silence on the clock port: still external, no internal steps
    expect (drums->currentStep() == (startStep + 4) % 8, "internal stepping must stay off while the clock is recent");
    for (int rendered = 0; rendered < 48000 * 4; rendered += block)
        drums->render (drumInputs, drumOut, block); // four more seconds: the clock is gone, the internal tempo returns
    expect (drums->currentStep() != (startStep + 4) % 8, "internal stepping should resume after four silent seconds");

    // A full-height pulse (Clock start/reset) puts the grid back on step 1 whatever step it was on.
    for (int pulse = 0; pulse < 3; ++pulse)
    {
        drumInputs.clear();
        for (int i = 0; i < 16; ++i) drumInputs.setSample (drumClockPort, i, 0.8f);
        drums->render (drumInputs, drumOut, block);
        drumInputs.clear();
        drums->render (drumInputs, drumOut, block);
    }
    drumInputs.clear();
    for (int i = 0; i < 16; ++i) drumInputs.setSample (drumClockPort, i, 1.0f);
    drums->render (drumInputs, drumOut, block);
    expect (drums->currentStep() == 0, "a restart pulse must realign the drum machine to step 1, got " + std::to_string (drums->currentStep()));

    // The looper waits for the pulse when a clock is present.
    auto looper = createNodeProcessor (NodeKind::looper);
    looper->prepare (48000.0, block);
    const auto looperClockPort = looper->getNumInputPorts() - 1;
    juce::AudioBuffer<float> loopInputs (looper->getNumInputPorts(), block), loopOut (1, block);
    loopInputs.clear();
    for (int i = 0; i < 16; ++i)
        loopInputs.setSample (looperClockPort, i, 0.8f);
    looper->render (loopInputs, loopOut, block); // one pulse: the clock is now "present"
    loopInputs.clear();
    looper->render (loopInputs, loopOut, block);
    expect (looper->handleUiCommand ("rec"), "rec is a command");
    looper->render (loopInputs, loopOut, block);
    expect (! looper->uiToggleState ("rec"), "recording must wait for the next pulse");
    expect (looper->statusText().startsWith ("ON THE NEXT PULSE"), "status should say it is waiting: " + looper->statusText().toStdString());
    for (int i = 0; i < 16; ++i)
        loopInputs.setSample (looperClockPort, i, 1.0f);
    looper->render (loopInputs, loopOut, block);
    expect (looper->uiToggleState ("rec"), "recording should start on the pulse");

    // With a Clock, Bars counts its pulses: Bars 2 closes the loop on the second pulse after the start.
    auto barLooper = createNodeProcessor (NodeKind::looper);
    barLooper->prepare (48000.0, block);
    barLooper->getParameter (5).setValue (2.0f);
    const auto barPort = barLooper->getNumInputPorts() - 1;
    juce::AudioBuffer<float> barIn (barLooper->getNumInputPorts(), block), barOut (1, block);
    auto pulseBlock = [&] (bool pulse)
    {
        barIn.clear();
        for (int i = 0; i < block; ++i) barIn.setSample (0, i, 0.3f);
        if (pulse) for (int i = 0; i < 16; ++i) barIn.setSample (barPort, i, 0.8f);
        barLooper->render (barIn, barOut, block);
    };
    for (int b = 0; b < 30; ++b) pulseBlock (false); // settle the Bars smoother
    pulseBlock (true);                                // the clock is present
    barLooper->handleUiCommand ("rec");
    pulseBlock (false);                               // queued
    pulseBlock (true);                                // recording starts on this pulse
    expect (barLooper->uiToggleState ("rec"), "bar looper should be recording");
    for (int b = 0; b < 10; ++b) pulseBlock (false);
    pulseBlock (true);                                // first bar
    expect (barLooper->statusText().startsWith ("REC"), "one pulse in: still recording: " + barLooper->statusText().toStdString());
    for (int b = 0; b < 10; ++b) pulseBlock (false);
    pulseBlock (true);                                // second bar closes it
    expect (barLooper->statusText().startsWith ("PLAY"), "the second pulse must close a 2-bar loop: " + barLooper->statusText().toStdString());
}

void testFourTrackMonitorsSyncsAndVarispeedsPerTrack()
{
    auto deck = createNodeProcessor (NodeKind::fourTrack);
    const int block = 480;
    deck->prepare (48000.0, block);
    juce::AudioBuffer<float> inputs (deck->getNumInputPorts(), block), outputs (1, block);
    inputs.clear();
    for (int i = 0; i < block; ++i)
        inputs.setSample (0, i, 0.25f);
    deck->render (inputs, outputs, block);
    expect (std::abs (outputs.getSample (0, 10) - 0.25f) < 1.0e-5f, "a stopped deck must still pass the input through (monitoring)");
    expect (deck->lanePosition (0) < 0.0f, "lanes idle while stopped");

    // Record a DC take on track 1 for one second, then play it back at double speed on that track only.
    deck->handleUiCommand ("arm1");
    deck->handleUiCommand ("rec");
    deck->handleUiCommand ("play");
    for (int b = 0; b < 100; ++b)
        deck->render (inputs, outputs, block); // 1 s at 48 kHz
    deck->handleUiCommand ("rec");
    deck->handleUiCommand ("rtz");
    inputs.clear();
    deck->render (inputs, outputs, block);
    expect (std::abs (outputs.getSample (0, 100) - 0.25f) < 1.0e-4f, "the take should play back at unity: " + std::to_string (outputs.getSample (0, 100)));
    expect (deck->uiToggleState ("sync") && deck->lanePosition (1) >= 0.0f && std::abs (deck->lanePosition (1) - deck->lanePosition (0)) < 1.0e-6f, "synced lanes move together");

    deck->handleUiCommand ("sync"); // free-running
    deck->getParameter (deck->getNumParameters() - 4).setValue (200.0f); // Speed 1 = 200 %
    deck->handleUiCommand ("rtz");
    for (int b = 0; b < 50; ++b)
        deck->render (inputs, outputs, block); // 0.5 s of real time
    const auto lane0 = deck->lanePosition (0), lane1 = deck->lanePosition (1);
    expect (std::abs (lane0 * 60.0f - 1.0f) < 0.02f, "track 1 at 200 % should have covered 1 s of tape in 0.5 s: " + std::to_string (lane0 * 60.0f));
    expect (std::abs (lane1 * 60.0f - 0.5f) < 0.02f, "track 2 at 100 % should be at 0.5 s: " + std::to_string (lane1 * 60.0f));
    deck->handleUiCommand ("play2");
    deck->render (inputs, outputs, block);
    expect (deck->lanePosition (1) < 0.0f && deck->lanePosition (0) >= 0.0f, "a stopped track goes idle while the others run");
}

void testNeuralAmpToneStackShapesTheSpectrum()
{
    // Without a model the amp is trim + tone stack; a full bass cut must lose low end and keep highs.
    auto amp = createNodeProcessor (NodeKind::neuralAmpPlaceholder);
    const int block = 256;
    amp->prepare (48000.0, block);
    expect (amp->getParameter (2).name == "Bass" && amp->getParameter (5).name == "Presence", "amp tone knobs should follow the trims");
    juce::AudioBuffer<float> inputs (amp->getNumInputPorts(), block), outputs (1, block);
    auto rms = [&] (double frequency)
    {
        amp->reset();
        double sum = 0.0; int counted = 0;
        for (int b = 0; b < 40; ++b)
        {
            inputs.clear();
            for (int i = 0; i < block; ++i)
                inputs.setSample (0, i, 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frequency * (b * block + i) / 48000.0)));
            amp->render (inputs, outputs, block);
            if (b >= 20)
                for (int i = 0; i < block; ++i) { sum += outputs.getSample (0, i) * outputs.getSample (0, i); ++counted; }
        }
        return std::sqrt (sum / counted);
    };
    const auto flatLow = rms (80.0), flatHigh = rms (5000.0);
    amp->getParameter (2).setValue (-12.0f);
    const auto cutLow = rms (80.0), cutHigh = rms (5000.0);
    expect (cutLow < flatLow * 0.4f, "bass at -12 dB should drop 80 Hz: " + std::to_string (cutLow / flatLow));
    expect (std::abs (cutHigh - flatHigh) < flatHigh * 0.05f, "bass knob must leave 5 kHz alone");
    amp->getParameter (2).setValue (0.0f);
    amp->getParameter (5).setValue (12.0f);
    expect (rms (4800.0) > flatHigh * 2.0f, "presence at +12 dB should lift 4.8 kHz");

    auto pedal = createNodeProcessor (NodeKind::neuralPedal);
    expect (pedal->getParameter (0).name == "Drive" && pedal->getParameter (3).name == "Tone", "pedal knobs: Drive, Level, Mix, Tone");
}

void testSynthPitchInputIsExactOctaves()
{
    auto synth = createNodeProcessor (NodeKind::monoSynth);
    const int block = 256;
    synth->prepare (48000.0, block);
    const auto pitchPort = synth->getNumInputPorts() - 1;
    expect (synth->getInputPort (pitchPort).name == "Pitch", "the synth's last input should be the exact Pitch");
    juce::AudioBuffer<float> inputs (synth->getNumInputPorts(), block), outputs (1, block);
    auto zeroCrossingsPerSecond = [&] (float pitchControl)
    {
        synth->reset();
        inputs.clear();
        for (int i = 0; i < block; ++i)
        {
            inputs.setSample (0, i, 1.0f);             // gate open
            inputs.setSample (pitchPort, i, pitchControl);
        }
        int crossings = 0; float last = 0.0f;
        for (int b = 0; b < 200; ++b)                  // ~1 s
        {
            synth->render (inputs, outputs, block);
            if (b < 20) continue;                       // let the glide settle
            for (int i = 0; i < block; ++i)
            {
                const auto v = outputs.getSample (0, i);
                if (last <= 0.0f && v > 0.0f) ++crossings;
                last = v;
            }
        }
        return static_cast<double> (crossings) / (180.0 * block / 48000.0);
    };
    const auto base = zeroCrossingsPerSecond (0.0f);
    const auto octave = zeroCrossingsPerSecond (0.25f); // 12 semitones
    expect (base > 50.0 && std::abs (octave / base - 2.0) < 0.06, "0.25 on the Pitch input should be exactly one octave up: " + std::to_string (octave / base));
}

void testTone3000PiecesAreRight()
{
    // RFC 7636 appendix B vector.
    expect (tone3000::Pkce::challengeFor ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
            "PKCE S256 challenge should match the RFC example");
    const auto pkce = tone3000::Pkce::generate();
    expect (pkce.verifier.length() == 43 && pkce.state.length() >= 20 && pkce.challenge == tone3000::Pkce::challengeFor (pkce.verifier), "generated PKCE should be consistent");

    const auto url = tone3000::authorizeUrl ("t3k_pub_x", tone3000::redirectUri(), pkce).toString (true);
    expect (url.startsWith ("https://www.tone3000.com/api/v1/oauth/authorize?") && url.contains ("client_id=t3k_pub_x") && url.contains ("code_challenge_method=S256")
            && url.contains ("response_type=code") && url.contains ("redirect_uri=http%3A%2F%2Flocalhost%3A41443%2Fcb"), "authorize URL is missing parameters: " + url.toStdString());

    expect (tone3000::codeFromCallback ("GET /cb?code=abc%2Fdef&state=" + pkce.state + " HTTP/1.1", pkce.state) == "abc/def", "callback code should be extracted and unescaped");
    expect (tone3000::codeFromCallback ("GET /cb?code=abc&state=wrong HTTP/1.1", pkce.state).isEmpty(), "a wrong state must be rejected");

    const auto page = tone3000::parseTonePage (juce::JSON::parse (R"({"data":[{"id":12,"title":"Plexi 51","gear":"amp","models_count":3,"makes":[{"name":"Marshall"}],"user":{"username":"tim"},"images":["https://x/p.jpg"]},{"id":13,"title":"Nano Rat","gear":"pedal","models_count":1}],"page":2,"page_size":25,"total":51,"total_pages":3})"));
    expect (page.tones.size() == 2 && page.page == 2 && page.totalPages == 3 && page.total == 51, "tone page envelope");
    expect (page.tones[0].id == 12 && page.tones[0].title == "Plexi 51" && page.tones[0].make == "Marshall" && page.tones[0].user == "tim" && page.tones[0].modelsCount == 3, "tone fields");
    expect (page.tones[1].make.isEmpty() && page.tones[1].gear == "pedal", "missing make should stay empty");
    expect (page.tones[0].image == "https://x/p.jpg" && page.tones[1].image.isEmpty(), "first photo URL should be kept");
    expect (tone3000::modelFileName (page.tones[0], tone3000::Model { 1, "V30 57", "https://x/i.wav", "", 0 }, "wav") == "Plexi 51 - V30 57 [1].wav", "impulse file name");

    const auto models = tone3000::parseModels (juce::JSON::parse (R"({"data":[{"id":7,"tone_id":12,"name":"Plexi 51 DI#03","model_url":"https://x/y.nam","size":"standard","architecture_version":2},{"id":8,"name":"no url"}]})"));
    expect (models.size() == 1 && models[0].id == 7 && models[0].size == "standard" && models[0].architecture == 2 && models[0].url == "https://x/y.nam", "model fields; entries without a url dropped");
    const auto bare = tone3000::parseModels (juce::JSON::parse (R"([{"id":9,"name":"n","model_url":"https://x/z.nam"}])"));
    expect (bare.size() == 1, "a bare array should parse too");
    expect (tone3000::modelFileName (page.tones[0], models[0]) == "Plexi 51 - Plexi 51 DI03 (standard) [7].nam", "file name: " + tone3000::modelFileName (page.tones[0], models[0]).toStdString());

    auto tokens = tone3000::Tokens::fromTokenResponse (juce::JSON::parse (R"({"access_token":"A","refresh_token":"R","token_type":"bearer","expires_in":3600})"), 1000);
    expect (tokens.present() && tokens.refresh == "R" && tokens.expiresAtMs == 3601000, "token response");
    expect (! tokens.expired (1000) && tokens.expired (3600000), "expiry with a 30 s margin");
    const auto back = tone3000::Tokens::fromJson (tokens.toJson());
    expect (back.access == "A" && back.refresh == "R" && back.expiresAtMs == 3601000, "token round trip");
}

void testMidiNoteNodeDrivesGateAndPitch()
{
    auto node = createNodeProcessor (NodeKind::midiNote);
    const int block = 64;
    node->prepare (48000.0, block);
    expect (std::abs (node->getParameter (0).getValue() - 45.0f) < 1.0e-4f, "default base should be A2 (45) so 0 on Pitch is 110 Hz");
    node->getParameter (0).setValue (60.0f); // the checks below were written around a C4 base
    juce::AudioBuffer<float> inputs (juce::jmax (1, node->getNumInputPorts()), block), outputs (3, block);
    inputs.clear();
    node->render (inputs, outputs, block);
    expect (outputs.getSample (0, 20) == 0.0f, "gate should start closed");
    node->handleMidiNote (1, 72, 100, true);   // C5: one octave above the base
    node->render (inputs, outputs, block);
    expect (outputs.getSample (0, 20) == 1.0f, "gate should open on note on");
    expect (std::abs (outputs.getSample (1, 20) - 12.0f / 48.0f) < 1.0e-5f, "pitch control should be +12 semitones / 48");
    expect (std::abs (outputs.getSample (2, 20) - 100.0f / 127.0f) < 1.0e-5f, "velocity control wrong");
    node->handleMidiNote (1, 60, 90, true);    // second note takes over
    node->render (inputs, outputs, block);
    expect (std::abs (outputs.getSample (1, 20)) < 1.0e-5f, "newest note should sound");
    node->handleMidiNote (1, 60, 0, false);    // release it: back to the held C5
    node->render (inputs, outputs, block);
    expect (outputs.getSample (0, 20) == 1.0f && std::abs (outputs.getSample (1, 20) - 0.25f) < 1.0e-5f, "held note should return after release");
    node->handleMidiNote (1, 72, 0, false);
    node->render (inputs, outputs, block);
    expect (outputs.getSample (0, 20) == 0.0f && std::abs (outputs.getSample (1, 20) - 0.25f) < 1.0e-5f, "gate closes, pitch holds for the tail");
    node->getParameter (1).setValue (5.0f);    // channel filter
    node->handleMidiNote (1, 64, 100, true);
    node->render (inputs, outputs, block);
    expect (outputs.getSample (0, 20) == 0.0f, "notes on another channel should be ignored");
}

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
        { "NAM model benchmark (env-gated)", testNamModelBenchmark },
        { "undo/redo structure round trip", testUndoRedoStructure },
        { "undo delete restores processor and cables", testUndoDeleteRestoresSameProcessorAndCables },
        { "undo coalesces knob gestures", testUndoCoalescesKnobGestures },
        { "knob travel and curve: narrow, bend, reverse, save, undo", testParameterShapeNarrowsBendsAndSurvives },
        { "a modulated knob reports where it is", testModulatedKnobReportsWhereItIs },
        { "unused saved hardware channels do not come back", testUnusedSavedHardwareChannelsDoNotComeBack },
        { "undo of a delete brings back MIDI bindings and groups", testUndoDeleteBringsBackMidiAndGroups },
        { "undo compound gesture moves several modules as one step", testUndoCompoundGestureMovesSeveralModulesAsOneStep },
        { "raw juce convolution sanity", testRawJuceConvolutionSanity },
        { "cabinet convolves an impulse", testCabinetConvolvesImpulse },
        { "merge patch adds nodes with fresh ids", testMergeJsonAddsNodesWithFreshIds },
        { "portable bundle round trip", testBundleRoundTripKeepsAssetsRelative },
        { "board positions and groups round trip", testBoardPositionsAndGroupsRoundTrip },
        { "looper records, closes, overdubs, undoes", testLooperRecordsClosesOverdubsAndUndoes },
        { "looper overdubs once per slot and undoes at any speed", testLooperOverdubOncePerSlotAndUndoAtAnySpeed },
        { "recorded audio at another rate keeps its length", testRecordedAudioAtAnotherRateKeepsItsLength },
        { "a live sample-rate change keeps recordings", testLiveRateChangeKeepsRecordings },
        { "pluck is in tune up to A4", testPluckIsInTune },
        { "4-track sync with track 1 stopped, no doubled monitor, take versions", testFourTrackSyncWithTrackOneStoppedAndNoDoubledMonitor },
        { "stereo bypass keeps both sides", testStereoBypassKeepsBothSides },
        { "recorded audio saves and loads with the patch", testRecordedAudioSavesAndLoadsWithThePatch },
        { "midi mappings round trip and scrub", testMidiMappingsRoundTripAndScrub },
        { "controller feedback state and sysex", testControllerFeedback },
        { "drum machine tap tempo", testDrumMachineTapTempo },
        { "stereo nodes: pan, ping-pong delay, cabinet R, reverb/chorus", testStereoNodes },
        { "tuner detects pitch", testTunerDetectsPitch },
        { "clock pulses; drums and looper follow it", testClockPulsesAndFollowers },
        { "4-track monitors, syncs and varispeeds per track", testFourTrackMonitorsSyncsAndVarispeedsPerTrack },
        { "neural amp tone stack shapes the spectrum", testNeuralAmpToneStackShapesTheSpectrum },
        { "synth Pitch input is exact octaves", testSynthPitchInputIsExactOctaves },
        { "TONE3000 pieces: PKCE, URLs, callback, JSON", testTone3000PiecesAreRight },
        { "MIDI Note node drives gate and pitch", testMidiNoteNodeDrivesGateAndPitch }
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
