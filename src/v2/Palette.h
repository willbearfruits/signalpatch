#pragma once

#include "../audio/Graph.h"

#include <nanovg.h>

#include <array>
#include <vector>

// Colours for the GPU rack. Same palette as the JUCE rack so patches read
// identically in both; kept here because the v2 target links no JUCE GUI.
namespace signalpatch::v2
{
inline NVGcolor rgb (juce::uint32 argb) noexcept
{
    return nvgRGBA (static_cast<unsigned char> ((argb >> 16) & 255),
                    static_cast<unsigned char> ((argb >> 8) & 255),
                    static_cast<unsigned char> (argb & 255),
                    static_cast<unsigned char> ((argb >> 24) & 255));
}

inline NVGcolor alpha (NVGcolor colour, float a) noexcept
{
    colour.a = a;
    return colour;
}

inline NVGcolor mix (NVGcolor a, NVGcolor b, float t) noexcept
{
    return nvgLerpRGBA (a, b, t);
}

inline NVGcolor lighter (NVGcolor c, float amount) noexcept
{
    return mix (c, nvgRGBAf (1.0f, 1.0f, 1.0f, c.a), amount);
}

inline NVGcolor darker (NVGcolor c, float amount) noexcept
{
    return mix (c, nvgRGBAf (0.0f, 0.0f, 0.0f, c.a), amount);
}

namespace palette
{
    inline const NVGcolor workspace  = rgb (0xff0d1114);
    inline const NVGcolor panel      = rgb (0xff14191d);
    inline const NVGcolor panelRaised= rgb (0xff1e262c);
    inline const NVGcolor node       = rgb (0xff222b31);
    inline const NVGcolor nodeTop    = rgb (0xff2a343c);
    inline const NVGcolor nodeDark   = rgb (0xff11161a);
    inline const NVGcolor text       = rgb (0xffefeadd);
    inline const NVGcolor mutedText  = rgb (0xff8e9aa1);
    inline const NVGcolor grid       = rgb (0xff232d34);
    inline const NVGcolor gridDot    = rgb (0xff2d3941);
    inline const NVGcolor audio      = rgb (0xff4fd8c4);
    inline const NVGcolor control    = rgb (0xffb48cff);
    inline const NVGcolor feedback   = rgb (0xffffb45b);
    inline const NVGcolor warning    = rgb (0xffff5f58);
    inline const NVGcolor okay       = rgb (0xff92d95e);
    inline const NVGcolor selection  = rgb (0xfff0d67a);
} // namespace palette

/** A parameter value the way the plates print it ("1.20 kHz", "35.0 ms"). */
inline juce::String formatParameterValue (const DspParameter& parameter, float value)
{
    const auto& unit = parameter.unit;
    if (unit == "Hz")
        return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                : juce::String (value, value < 10.0f ? 2 : 0) + " Hz";
    if (unit == "ms")
        return juce::String (value, value >= 100.0f ? 0 : 1) + " ms";
    if (unit == "dB")
        return juce::String (value, 1) + " dB";
    if (unit == "%")
        return juce::String (value, 1) + "%";
    if (unit == ":1")
        return juce::String (value, 1) + ":1";
    return juce::String (value, 2);
}

struct ModuleEntry
{
    NodeKind kind;
    const char* label;
    const char* group;
    const char* blurb; // one line for the module picker
};

// Same vocabulary and grouping as the JUCE palette.
inline const std::vector<ModuleEntry>& moduleCatalogue()
{
    static const std::vector<ModuleEntry> entries {
        { NodeKind::tuner,                "TUNER",          "UTILITY", "Note and cents, audio passes" },
        { NodeKind::gain,                 "GAIN",           "UTILITY", "Level up or down" },
        { NodeKind::mixer,                "4-CH MIXER",     "UTILITY", "Four inputs into one" },
        { NodeKind::crossfade,            "CROSSFADE",      "UTILITY", "Blends input A into input B" },
        { NodeKind::distortion,           "DISTORTION",     "EFFECTS", "Drive, tone and mix" },
        { NodeKind::filter,               "FILTER",         "EFFECTS", "Resonant filter" },
        { NodeKind::delay,                "DELAY",          "EFFECTS", "Echoes with feedback" },
        { NodeKind::reverb,               "REVERB",         "EFFECTS", "Room to hall" },
        { NodeKind::chorus,               "CHORUS",         "EFFECTS", "Detuned copies, mono" },
        { NodeKind::phaser,               "PHASER",         "EFFECTS", "Sweeping notches" },
        { NodeKind::tremolo,              "TREMOLO",        "EFFECTS", "Volume that wobbles" },
        { NodeKind::bitcrusher,           "BITCRUSHER",     "EFFECTS", "Fewer bits, lower rate" },
        { NodeKind::ringMod,              "RING MOD",       "EFFECTS", "Multiplies the input by a tone" },
        { NodeKind::pitchShifter,         "PITCH SHIFTER",  "EFFECTS", "Up or down in semitones" },
        { NodeKind::granular,             "GRANULAR",       "EFFECTS", "Grains of the last seconds" },
        { NodeKind::neuralAmpPlaceholder, "NEURAL AMP",     "NEURAL", "A NAM amp capture" },
        { NodeKind::neuralPedal,          "NEURAL PEDAL",   "NEURAL", "A NAM pedal capture" },
        { NodeKind::cabinet,              "CABINET",        "NEURAL", "Speaker impulse response" },
        { NodeKind::pan,                  "PAN",            "STEREO", "Mono to left and right" },
        { NodeKind::stereoMerge,          "STEREO MERGE",   "STEREO", "Left and right to mono" },
        { NodeKind::stereoDelay,          "STEREO DELAY",   "STEREO", "Ping-pong echoes" },
        { NodeKind::stereoChorus,         "STEREO CHORUS",  "STEREO", "Chorus spread wide" },
        { NodeKind::stereoReverb,         "STEREO REVERB",  "STEREO", "Reverb in stereo" },
        { NodeKind::vowelFilter,          "VOWEL FILTER",   "VOICE", "Formants from A to U" },
        { NodeKind::vocoder,              "VOCODER",        "VOICE", "A voice shapes a carrier" },
        { NodeKind::pitchCorrector,       "AUTOTUNE",       "VOICE", "Pulls a voice to a scale" },
        { NodeKind::monoSynth,            "MONO SYNTH",     "INSTRUMENTS", "Played by MIDI notes" },
        { NodeKind::pluck,                "PLUCK",          "INSTRUMENTS", "Plucked string, MIDI notes" },
        { NodeKind::noiseSource,          "NOISE",          "INSTRUMENTS", "White to brown noise" },
        { NodeKind::drumMachine,          "DRUM MACHINE",   "INSTRUMENTS", "Pattern drums, tap tempo" },
        { NodeKind::sampler,              "SAMPLER",        "INSTRUMENTS", "Records and replays a phrase" },
        { NodeKind::looper,               "LOOPER",         "INSTRUMENTS", "Record, overdub, play" },
        { NodeKind::fourTrack,            "4-TRACK",        "INSTRUMENTS", "Four tracks of tape" },
        { NodeKind::compressor,           "COMPRESSOR",     "DYNAMICS", "Evens out the level" },
        { NodeKind::limiter,              "LIMITER",        "DYNAMICS", "Hard ceiling at the end" },
        { NodeKind::gate,                 "NOISE GATE",     "DYNAMICS", "Silence between notes" },
        { NodeKind::feedbackGuard,        "FEEDBACK GUARD", "DYNAMICS", "Makes a cable loop safe" },
        { NodeKind::clock,                "CLOCK",          "CONTROL", "Tempo, beat and bar pulses" },
        { NodeKind::midiNote,             "MIDI NOTE",      "CONTROL", "Notes to gate and pitch" },
        { NodeKind::lfo,                  "LFO",            "CONTROL", "A slow wave to move a knob" },
        { NodeKind::randomLfo,            "RANDOM",         "CONTROL", "Random steps, smoothed" },
        { NodeKind::envelopeFollower,     "ENVELOPE",       "CONTROL", "Follows how loud you play" },
        { NodeKind::stepSequencer,        "8-STEP SEQ",     "CONTROL", "Eight steps of control" },
        { NodeKind::macro,                "MACRO",          "CONTROL", "One knob that moves many" },
        { NodeKind::spectralFollower,     "SPECTRAL (FFT)", "CONTROL", "Band levels as controls" },
        { NodeKind::script,               "SCRIPT",         "CONTROL", "An expression per sample" },
    };
    return entries;
}

// Node colours read by family: every module in a family shares one hue, and
// siblings step a little lighter or darker so cables from neighbours still
// tell apart. Hardware and the Feedback Guard keep their own signal colours.
inline NVGcolor familyAccent (const juce::String& group) noexcept
{
    if (group == "UTILITY")     return rgb (0xffd6c47c); // sand
    if (group == "EFFECTS")     return rgb (0xffff8a5c); // coral
    if (group == "NEURAL")      return rgb (0xffe879b8); // magenta
    if (group == "STEREO")      return rgb (0xff5fd0ff); // cyan
    if (group == "VOICE")       return rgb (0xff62d9b8); // teal
    if (group == "INSTRUMENTS") return rgb (0xffffc850); // amber
    if (group == "DYNAMICS")    return rgb (0xff8fd95e); // green
    if (group == "CONTROL")     return rgb (0xffb48cff); // violet
    return palette::selection;
}

inline NVGcolor accent (NodeKind kind) noexcept
{
    switch (kind)
    {
        case NodeKind::hardwareInput:  return rgb (0xff3fd0b7);
        case NodeKind::hardwareOutput: return rgb (0xff4da3ff);
        case NodeKind::feedbackGuard:  return palette::feedback;
        default: break;
    }
    static const auto table = []
    {
        std::array<NVGcolor, 96> colours {};
        for (auto& colour : colours)
            colour = palette::selection;
        const auto& catalogue = moduleCatalogue();
        juce::String group;
        int indexInGroup = 0;
        for (const auto& entry : catalogue)
        {
            if (group != entry.group)
            {
                group = entry.group;
                indexInGroup = 0;
            }
            const auto base = familyAccent (group);
            // 0: base, 1: lighter, 2: darker, 3: lighter still, 4: darker still ...
            const auto step = (indexInGroup + 1) / 2;
            const auto amount = 0.09f * static_cast<float> (step);
            const auto shade = indexInGroup == 0 ? base : (indexInGroup % 2 == 1 ? lighter (base, amount) : darker (base, amount));
            if (static_cast<std::size_t> (entry.kind) < colours.size())
                colours[static_cast<std::size_t> (entry.kind)] = shade;
            ++indexInGroup;
        }
        return colours;
    }();
    const auto index = static_cast<std::size_t> (kind);
    return index < table.size() ? table[index] : palette::selection;
}
} // namespace signalpatch::v2
