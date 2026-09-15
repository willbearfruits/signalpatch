#pragma once

#include "../audio/Graph.h"

#include <nanovg.h>

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

inline NVGcolor accent (NodeKind kind) noexcept
{
    switch (kind)
    {
        case NodeKind::hardwareInput:        return rgb (0xff3fd0b7);
        case NodeKind::hardwareOutput:       return rgb (0xff4da3ff);
        case NodeKind::gain:                 return rgb (0xffd9c76a);
        case NodeKind::mixer:                return rgb (0xffe0b352);
        case NodeKind::crossfade:            return rgb (0xffd9b16a);
        case NodeKind::distortion:           return rgb (0xffff7a53);
        case NodeKind::filter:               return rgb (0xff4fb3e8);
        case NodeKind::delay:                return rgb (0xff53c7ff);
        case NodeKind::reverb:               return rgb (0xff6fc7d9);
        case NodeKind::chorus:               return rgb (0xffff9eb5);
        case NodeKind::phaser:               return rgb (0xffe08cff);
        case NodeKind::tremolo:              return rgb (0xffffd166);
        case NodeKind::bitcrusher:           return rgb (0xfff069c4);
        case NodeKind::ringMod:              return rgb (0xffe86a8a);
        case NodeKind::vowelFilter:          return rgb (0xffffab70);
        case NodeKind::pitchShifter:         return rgb (0xff6fd7b2);
        case NodeKind::vocoder:              return rgb (0xff62c9c3);
        case NodeKind::pitchCorrector:       return rgb (0xff7ab8ff);
        case NodeKind::granular:             return rgb (0xffc9a2ff);
        case NodeKind::compressor:           return rgb (0xff8fd95e);
        case NodeKind::limiter:              return rgb (0xff6fce74);
        case NodeKind::gate:                 return rgb (0xffa8d95e);
        case NodeKind::feedbackGuard:        return palette::feedback;
        case NodeKind::monoSynth:            return rgb (0xffffcf5c);
        case NodeKind::noiseSource:          return rgb (0xffa8b6bf);
        case NodeKind::pluck:                return rgb (0xffb8e986);
        case NodeKind::drumMachine:          return rgb (0xffff9552);
        case NodeKind::sampler:              return rgb (0xff6fe3c2);
        case NodeKind::fourTrack:            return rgb (0xffd4a373);
        case NodeKind::lfo:                  return rgb (0xffb48cff);
        case NodeKind::randomLfo:            return rgb (0xff8f7dff);
        case NodeKind::envelopeFollower:     return rgb (0xffcf8cff);
        case NodeKind::stepSequencer:        return rgb (0xff9d8cff);
        case NodeKind::macro:                return palette::selection;
        case NodeKind::spectralFollower:     return rgb (0xff8fd0ff);
        case NodeKind::script:               return rgb (0xff9be564);
        case NodeKind::neuralAmpPlaceholder: return rgb (0xffb0889a);
        case NodeKind::neuralPedal:          return rgb (0xffe879b8);
        case NodeKind::cabinet:              return rgb (0xffd9a066);
        case NodeKind::looper:               return rgb (0xffff8a80);
        case NodeKind::pan:                  return rgb (0xff7fd4ff);
        case NodeKind::stereoMerge:          return rgb (0xff7fd4ff);
        case NodeKind::stereoDelay:          return rgb (0xff53c7ff);
        case NodeKind::stereoChorus:         return rgb (0xffff9eb5);
        case NodeKind::stereoReverb:         return rgb (0xff6fc7d9);
    }
    return palette::selection;
}
struct ModuleEntry
{
    NodeKind kind;
    const char* label;
    const char* group;
};

// Same vocabulary and grouping as the JUCE palette.
inline const std::vector<ModuleEntry>& moduleCatalogue()
{
    static const std::vector<ModuleEntry> entries {
        { NodeKind::gain,                 "GAIN",           "UTILITY" },
        { NodeKind::mixer,                "4-CH MIXER",     "UTILITY" },
        { NodeKind::crossfade,            "CROSSFADE",      "UTILITY" },
        { NodeKind::distortion,           "DISTORTION",     "EFFECTS" },
        { NodeKind::filter,               "FILTER",         "EFFECTS" },
        { NodeKind::delay,                "DELAY",          "EFFECTS" },
        { NodeKind::reverb,               "REVERB",         "EFFECTS" },
        { NodeKind::chorus,               "CHORUS",         "EFFECTS" },
        { NodeKind::phaser,               "PHASER",         "EFFECTS" },
        { NodeKind::tremolo,              "TREMOLO",        "EFFECTS" },
        { NodeKind::bitcrusher,           "BITCRUSHER",     "EFFECTS" },
        { NodeKind::ringMod,              "RING MOD",       "EFFECTS" },
        { NodeKind::pitchShifter,         "PITCH SHIFTER",  "EFFECTS" },
        { NodeKind::granular,             "GRANULAR",       "EFFECTS" },
        { NodeKind::neuralAmpPlaceholder, "NEURAL AMP",     "NEURAL" },
        { NodeKind::neuralPedal,          "NEURAL PEDAL",   "NEURAL" },
        { NodeKind::cabinet,              "CABINET",        "NEURAL" },
        { NodeKind::pan,                  "PAN",            "STEREO" },
        { NodeKind::stereoMerge,          "STEREO MERGE",   "STEREO" },
        { NodeKind::stereoDelay,          "STEREO DELAY",   "STEREO" },
        { NodeKind::stereoChorus,         "STEREO CHORUS",  "STEREO" },
        { NodeKind::stereoReverb,         "STEREO REVERB",  "STEREO" },
        { NodeKind::vowelFilter,          "VOWEL FILTER",   "VOICE" },
        { NodeKind::vocoder,              "VOCODER",        "VOICE" },
        { NodeKind::pitchCorrector,       "AUTOTUNE",       "VOICE" },
        { NodeKind::monoSynth,            "MONO SYNTH",     "INSTRUMENTS" },
        { NodeKind::pluck,                "PLUCK",          "INSTRUMENTS" },
        { NodeKind::noiseSource,          "NOISE",          "INSTRUMENTS" },
        { NodeKind::drumMachine,          "DRUM MACHINE",   "INSTRUMENTS" },
        { NodeKind::sampler,              "SAMPLER",        "INSTRUMENTS" },
        { NodeKind::looper,               "LOOPER",         "INSTRUMENTS" },
        { NodeKind::fourTrack,            "4-TRACK",        "INSTRUMENTS" },
        { NodeKind::compressor,           "COMPRESSOR",     "DYNAMICS" },
        { NodeKind::limiter,              "LIMITER",        "DYNAMICS" },
        { NodeKind::gate,                 "NOISE GATE",     "DYNAMICS" },
        { NodeKind::feedbackGuard,        "FEEDBACK GUARD", "DYNAMICS" },
        { NodeKind::lfo,                  "LFO",            "CONTROL" },
        { NodeKind::randomLfo,            "RANDOM",         "CONTROL" },
        { NodeKind::envelopeFollower,     "ENVELOPE",       "CONTROL" },
        { NodeKind::stepSequencer,        "8-STEP SEQ",     "CONTROL" },
        { NodeKind::macro,                "MACRO",          "CONTROL" },
        { NodeKind::spectralFollower,     "SPECTRAL (FFT)", "CONTROL" },
        { NodeKind::script,               "SCRIPT",         "CONTROL" },
    };
    return entries;
}
} // namespace signalpatch::v2
