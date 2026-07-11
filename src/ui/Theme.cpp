#include "Theme.h"

namespace signalpatch::ui
{
juce::Colour kindAccent (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::hardwareInput:        return juce::Colour (0xff3fd0b7);
        case NodeKind::hardwareOutput:       return juce::Colour (0xff4da3ff);
        case NodeKind::gain:                 return juce::Colour (0xffd9c76a);
        case NodeKind::mixer:                return juce::Colour (0xffe0b352);
        case NodeKind::crossfade:            return juce::Colour (0xffd9b16a);
        case NodeKind::distortion:           return juce::Colour (0xffff7a53);
        case NodeKind::filter:               return juce::Colour (0xff4fb3e8);
        case NodeKind::delay:                return juce::Colour (0xff53c7ff);
        case NodeKind::reverb:               return juce::Colour (0xff6fc7d9);
        case NodeKind::chorus:               return juce::Colour (0xffff9eb5);
        case NodeKind::phaser:               return juce::Colour (0xffe08cff);
        case NodeKind::tremolo:              return juce::Colour (0xffffd166);
        case NodeKind::bitcrusher:           return juce::Colour (0xfff069c4);
        case NodeKind::ringMod:              return juce::Colour (0xffe86a8a);
        case NodeKind::vowelFilter:          return juce::Colour (0xffffab70);
        case NodeKind::pitchShifter:         return juce::Colour (0xff6fd7b2);
        case NodeKind::vocoder:              return juce::Colour (0xff62c9c3);
        case NodeKind::pitchCorrector:       return juce::Colour (0xff7ab8ff);
        case NodeKind::granular:             return juce::Colour (0xffc9a2ff);
        case NodeKind::compressor:           return juce::Colour (0xff8fd95e);
        case NodeKind::limiter:              return juce::Colour (0xff6fce74);
        case NodeKind::gate:                 return juce::Colour (0xffa8d95e);
        case NodeKind::feedbackGuard:        return colours::feedback;
        case NodeKind::monoSynth:            return juce::Colour (0xffffcf5c);
        case NodeKind::noiseSource:          return juce::Colour (0xffa8b6bf);
        case NodeKind::pluck:                return juce::Colour (0xffb8e986);
        case NodeKind::drumMachine:          return juce::Colour (0xffff9552);
        case NodeKind::sampler:              return juce::Colour (0xff6fe3c2);
        case NodeKind::fourTrack:            return juce::Colour (0xffd4a373);
        case NodeKind::lfo:                  return juce::Colour (0xffb48cff);
        case NodeKind::randomLfo:            return juce::Colour (0xff8f7dff);
        case NodeKind::envelopeFollower:     return juce::Colour (0xffcf8cff);
        case NodeKind::stepSequencer:        return juce::Colour (0xff9d8cff);
        case NodeKind::macro:                return colours::selection;
        case NodeKind::spectralFollower:     return juce::Colour (0xff8fd0ff);
        case NodeKind::script:               return juce::Colour (0xff9be564);
        case NodeKind::neuralAmpPlaceholder: return juce::Colour (0xffb0889a);
        case NodeKind::neuralPedal:          return juce::Colour (0xffe879b8);
    }

    jassertfalse;
    return colours::selection;
}

juce::String kindTag (NodeKind kind)
{
    switch (kind)
    {
        case NodeKind::hardwareInput:
        case NodeKind::hardwareOutput:       return "I/O";
        case NodeKind::gain:
        case NodeKind::mixer:
        case NodeKind::crossfade:            return "UTIL";
        case NodeKind::distortion:
        case NodeKind::filter:
        case NodeKind::delay:
        case NodeKind::reverb:
        case NodeKind::chorus:
        case NodeKind::phaser:
        case NodeKind::tremolo:
        case NodeKind::bitcrusher:
        case NodeKind::ringMod:
        case NodeKind::pitchShifter:
        case NodeKind::granular:             return "FX";
        case NodeKind::vowelFilter:
        case NodeKind::vocoder:
        case NodeKind::pitchCorrector:       return "VOX";
        case NodeKind::compressor:
        case NodeKind::limiter:
        case NodeKind::gate:                 return "DYN";
        case NodeKind::feedbackGuard:        return "SAFE";
        case NodeKind::monoSynth:
        case NodeKind::noiseSource:
        case NodeKind::pluck:
        case NodeKind::drumMachine:
        case NodeKind::sampler:              return "INST";
        case NodeKind::fourTrack:            return "TAPE";
        case NodeKind::lfo:
        case NodeKind::randomLfo:
        case NodeKind::envelopeFollower:
        case NodeKind::stepSequencer:
        case NodeKind::macro:
        case NodeKind::spectralFollower:     return "CTL";
        case NodeKind::script:               return "CODE";
        case NodeKind::neuralAmpPlaceholder:
        case NodeKind::neuralPedal:          return "NAM";
    }

    jassertfalse;
    return "?";
}

void drawKindGlyph (juce::Graphics& graphics, NodeKind kind,
                    juce::Rectangle<float> area, juce::Colour colour)
{
    graphics.setColour (colour);
    const auto stroke = juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded);
    juce::Path path;
    const auto x = area.getX();
    const auto y = area.getY();
    const auto w = area.getWidth();
    const auto h = area.getHeight();

    switch (kind)
    {
        case NodeKind::hardwareInput:
            path.startNewSubPath (x, y + h * 0.5f);
            path.lineTo (x + w * 0.62f, y + h * 0.5f);
            path.addTriangle (x + w * 0.55f, y + h * 0.18f,
                              x + w * 0.55f, y + h * 0.82f,
                              x + w,         y + h * 0.5f);
            graphics.strokePath (path, stroke);
            graphics.fillPath (path);
            return;
        case NodeKind::hardwareOutput:
            graphics.fillEllipse (x + w * 0.05f, y + h * 0.3f, w * 0.4f, h * 0.4f);
            path.startNewSubPath (x + w * 0.45f, y + h * 0.5f);
            path.lineTo (x + w, y + h * 0.5f);
            path.startNewSubPath (x + w * 0.72f, y + h * 0.15f);
            path.lineTo (x + w, y + h * 0.5f);
            path.lineTo (x + w * 0.72f, y + h * 0.85f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::gain:
            path.addTriangle (x, y + h * 0.15f, x, y + h * 0.85f, x + w, y + h * 0.5f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::mixer:
            for (int lane = 0; lane < 3; ++lane)
            {
                const auto laneY = y + h * (0.22f + 0.28f * static_cast<float> (lane));
                graphics.drawLine (x, laneY, x + w, laneY, 1.4f);
                const auto knobX = x + w * (0.25f + 0.25f * static_cast<float> (lane));
                graphics.fillEllipse (knobX - 2.0f, laneY - 2.0f, 4.0f, 4.0f);
            }
            return;
        case NodeKind::crossfade:
            path.startNewSubPath (x, y + h * 0.15f);
            path.cubicTo (x + w * 0.6f, y + h * 0.15f, x + w * 0.4f, y + h * 0.85f, x + w, y + h * 0.85f);
            path.startNewSubPath (x, y + h * 0.85f);
            path.cubicTo (x + w * 0.6f, y + h * 0.85f, x + w * 0.4f, y + h * 0.15f, x + w, y + h * 0.15f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::distortion:
            path.startNewSubPath (x, y + h);
            path.lineTo (x + w * 0.3f, y + h * 0.15f);
            path.lineTo (x + w * 0.5f, y + h * 0.75f);
            path.lineTo (x + w * 0.7f, y + h * 0.2f);
            path.lineTo (x + w, y + h * 0.6f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::filter:
            path.startNewSubPath (x, y + h * 0.35f);
            path.lineTo (x + w * 0.5f, y + h * 0.35f);
            path.quadraticTo (x + w * 0.68f, y + h * 0.3f, x + w * 0.72f, y + h * 0.15f);
            path.startNewSubPath (x + w * 0.72f, y + h * 0.15f);
            path.quadraticTo (x + w * 0.85f, y + h * 0.9f, x + w, y + h);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::reverb:
            for (int arc = 0; arc < 3; ++arc)
            {
                path.clear();
                const auto radius = w * (0.25f + 0.25f * static_cast<float> (arc));
                path.addCentredArc (x, y + h * 0.5f, radius, h * (0.2f + 0.16f * static_cast<float> (arc)),
                                    0.0f, 0.35f, juce::MathConstants<float>::pi - 0.35f, true);
                graphics.strokePath (path, juce::PathStrokeType (1.6f - 0.3f * static_cast<float> (arc)));
            }
            graphics.fillEllipse (x - 1.8f, y + h * 0.5f - 1.8f, 3.6f, 3.6f);
            return;
        case NodeKind::chorus:
            for (int voice = 0; voice < 2; ++voice)
            {
                path.clear();
                const auto offset = h * 0.14f * static_cast<float> (voice);
                path.startNewSubPath (x, y + h * 0.5f + offset);
                for (int step = 1; step <= 12; ++step)
                {
                    const auto t = static_cast<float> (step) / 12.0f;
                    path.lineTo (x + w * t,
                                 y + h * 0.5f + offset
                                     - std::sin ((t + 0.08f * voice) * juce::MathConstants<float>::twoPi)
                                           * h * 0.3f);
                }
                graphics.strokePath (path, juce::PathStrokeType (voice == 0 ? 1.6f : 1.0f));
            }
            return;
        case NodeKind::phaser:
            path.startNewSubPath (x, y + h * 0.5f);
            path.lineTo (x + w, y + h * 0.5f);
            graphics.strokePath (path, juce::PathStrokeType (1.2f));
            for (int notch = 0; notch < 3; ++notch)
            {
                const auto notchX = x + w * (0.2f + 0.3f * static_cast<float> (notch));
                path.clear();
                path.startNewSubPath (notchX - 3.0f, y + h * 0.5f);
                path.lineTo (notchX, y + h * 0.05f);
                path.lineTo (notchX + 3.0f, y + h * 0.5f);
                graphics.strokePath (path, juce::PathStrokeType (1.4f));
            }
            return;
        case NodeKind::tremolo:
            path.startNewSubPath (x, y + h * 0.5f);
            for (int step = 1; step <= 16; ++step)
            {
                const auto t = static_cast<float> (step) / 16.0f;
                const auto envelope = 0.5f + 0.5f * std::sin (t * juce::MathConstants<float>::twoPi);
                path.lineTo (x + w * t,
                             y + h * 0.5f
                                 - std::sin (t * 4.0f * juce::MathConstants<float>::twoPi)
                                       * h * 0.42f * envelope);
            }
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::bitcrusher:
        {
            const float steps[] { 0.6f, 0.2f, 0.75f, 0.4f };
            path.startNewSubPath (x, y + h * steps[0]);
            for (int step = 0; step < 4; ++step)
            {
                path.lineTo (x + w * 0.25f * static_cast<float> (step + 1), y + h * steps[step]);
                if (step < 3)
                    path.lineTo (x + w * 0.25f * static_cast<float> (step + 1), y + h * steps[step + 1]);
            }
            graphics.strokePath (path, stroke);
            return;
        }
        case NodeKind::gate:
            path.startNewSubPath (x, y + h * 0.85f);
            path.lineTo (x + w * 0.35f, y + h * 0.85f);
            path.lineTo (x + w * 0.35f, y + h * 0.15f);
            path.lineTo (x + w * 0.65f, y + h * 0.15f);
            path.lineTo (x + w * 0.65f, y + h * 0.85f);
            path.lineTo (x + w, y + h * 0.85f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::ringMod:
            graphics.drawEllipse (x + w * 0.05f, y + h * 0.2f, w * 0.6f, h * 0.6f, 1.5f);
            graphics.drawEllipse (x + w * 0.35f, y + h * 0.2f, w * 0.6f, h * 0.6f, 1.5f);
            return;
        case NodeKind::vowelFilter:
            path.addEllipse (x + w * 0.08f, y + h * 0.25f, w * 0.84f, h * 0.5f);
            graphics.strokePath (path, stroke);
            graphics.drawLine (x + w * 0.2f, y + h * 0.5f, x + w * 0.8f, y + h * 0.5f, 1.2f);
            return;
        case NodeKind::pitchShifter:
            for (int arrow = 0; arrow < 2; ++arrow)
            {
                const auto arrowX = x + w * (0.28f + 0.44f * static_cast<float> (arrow));
                path.clear();
                path.startNewSubPath (arrowX, y + h * 0.85f);
                path.lineTo (arrowX, y + h * 0.25f);
                graphics.strokePath (path, stroke);
                path.clear();
                path.addTriangle (arrowX - 3.2f, y + h * 0.35f, arrowX + 3.2f, y + h * 0.35f, arrowX, y);
                graphics.fillPath (path);
            }
            return;
        case NodeKind::vocoder:
        {
            const float bars[] { 0.4f, 0.85f, 0.6f, 0.95f, 0.5f };
            for (int bar = 0; bar < 5; ++bar)
                graphics.fillRoundedRectangle (x + w * 0.2f * static_cast<float> (bar),
                                               y + h * (1.0f - bars[bar]), w * 0.12f, h * bars[bar], 1.0f);
            return;
        }
        case NodeKind::pitchCorrector:
            graphics.drawLine (x, y + h * 0.3f, x + w, y + h * 0.3f, 1.0f);
            path.startNewSubPath (x, y + h * 0.8f);
            path.quadraticTo (x + w * 0.4f, y + h * 0.75f, x + w * 0.55f, y + h * 0.45f);
            path.lineTo (x + w * 0.65f, y + h * 0.3f);
            path.lineTo (x + w, y + h * 0.3f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::granular:
        {
            const float dotsX[] { 0.1f, 0.35f, 0.55f, 0.8f, 0.25f, 0.65f, 0.9f, 0.45f };
            const float dotsY[] { 0.3f, 0.15f, 0.4f, 0.2f, 0.7f, 0.75f, 0.55f, 0.9f };
            for (int dot = 0; dot < 8; ++dot)
            {
                const auto radius = 1.2f + 0.8f * static_cast<float> (dot % 3);
                graphics.fillEllipse (x + w * dotsX[dot] - radius, y + h * dotsY[dot] - radius,
                                      radius * 2.0f, radius * 2.0f);
            }
            return;
        }
        case NodeKind::monoSynth:
            path.startNewSubPath (x, y + h * 0.85f);
            path.lineTo (x + w * 0.33f, y + h * 0.15f);
            path.lineTo (x + w * 0.33f, y + h * 0.85f);
            path.lineTo (x + w * 0.66f, y + h * 0.15f);
            path.lineTo (x + w * 0.66f, y + h * 0.85f);
            path.lineTo (x + w, y + h * 0.15f);
            graphics.strokePath (path, juce::PathStrokeType (1.4f));
            return;
        case NodeKind::noiseSource:
        {
            const float levels[] { 0.5f, 0.2f, 0.75f, 0.35f, 0.9f, 0.15f, 0.6f, 0.45f, 0.8f };
            path.startNewSubPath (x, y + h * levels[0]);
            for (int step = 1; step < 9; ++step)
                path.lineTo (x + w * static_cast<float> (step) / 8.0f, y + h * levels[step]);
            graphics.strokePath (path, juce::PathStrokeType (1.2f));
            return;
        }
        case NodeKind::pluck:
            path.startNewSubPath (x, y + h * 0.5f);
            path.quadraticTo (x + w * 0.3f, y + h * 0.05f, x + w * 0.6f, y + h * 0.5f);
            path.quadraticTo (x + w * 0.8f, y + h * 0.8f, x + w, y + h * 0.5f);
            graphics.strokePath (path, stroke);
            graphics.fillEllipse (x - 1.5f, y + h * 0.5f - 1.5f, 3.0f, 3.0f);
            graphics.fillEllipse (x + w - 1.5f, y + h * 0.5f - 1.5f, 3.0f, 3.0f);
            return;
        case NodeKind::drumMachine:
            for (int row = 0; row < 2; ++row)
                for (int column = 0; column < 3; ++column)
                {
                    const auto cell = juce::Rectangle<float> (x + w * 0.36f * static_cast<float> (column),
                                                              y + h * 0.55f * static_cast<float> (row) + h * 0.08f,
                                                              w * 0.26f, h * 0.32f);
                    if ((row + column) % 2 == 0)
                        graphics.fillRoundedRectangle (cell, 1.5f);
                    else
                        graphics.drawRoundedRectangle (cell, 1.5f, 1.0f);
                }
            return;
        case NodeKind::sampler:
            path.startNewSubPath (x + w * 0.25f, y);
            path.lineTo (x, y);
            path.lineTo (x, y + h);
            path.lineTo (x + w * 0.25f, y + h);
            path.startNewSubPath (x + w * 0.75f, y);
            path.lineTo (x + w, y);
            path.lineTo (x + w, y + h);
            path.lineTo (x + w * 0.75f, y + h);
            graphics.strokePath (path, juce::PathStrokeType (1.3f));
            for (int bar = 0; bar < 3; ++bar)
            {
                const auto barHeight = h * (0.3f + 0.25f * static_cast<float> ((bar * 5) % 3));
                graphics.drawLine (x + w * (0.32f + 0.18f * static_cast<float> (bar)),
                                   y + (h - barHeight) * 0.5f,
                                   x + w * (0.32f + 0.18f * static_cast<float> (bar)),
                                   y + (h + barHeight) * 0.5f, 1.6f);
            }
            return;
        case NodeKind::fourTrack:
            graphics.drawEllipse (x + w * 0.08f, y + h * 0.2f, w * 0.32f, h * 0.6f, 1.4f);
            graphics.drawEllipse (x + w * 0.6f, y + h * 0.2f, w * 0.32f, h * 0.6f, 1.4f);
            graphics.drawLine (x + w * 0.4f, y + h * 0.5f, x + w * 0.6f, y + h * 0.5f, 1.2f);
            return;
        case NodeKind::spectralFollower:
        {
            const float spectrum[] { 0.9f, 0.7f, 0.5f, 0.35f, 0.22f, 0.14f };
            for (int bin = 0; bin < 6; ++bin)
                graphics.fillRect (x + w * static_cast<float> (bin) / 6.0f,
                                   y + h * (1.0f - spectrum[bin]),
                                   w * 0.11f, h * spectrum[bin]);
            return;
        }
        case NodeKind::script:
            path.startNewSubPath (x + w * 0.35f, y + h * 0.15f);
            path.lineTo (x + w * 0.1f, y + h * 0.5f);
            path.lineTo (x + w * 0.35f, y + h * 0.85f);
            path.startNewSubPath (x + w * 0.65f, y + h * 0.15f);
            path.lineTo (x + w * 0.9f, y + h * 0.5f);
            path.lineTo (x + w * 0.65f, y + h * 0.85f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::delay:
            for (int echo = 0; echo < 4; ++echo)
            {
                const auto echoX = x + w * (0.08f + 0.28f * static_cast<float> (echo));
                const auto echoH = h * (0.85f - 0.2f * static_cast<float> (echo));
                graphics.drawLine (echoX, y + (h - echoH) * 0.5f, echoX, y + (h + echoH) * 0.5f,
                                   2.2f - 0.35f * static_cast<float> (echo));
            }
            return;
        case NodeKind::compressor:
        case NodeKind::limiter:
            path.startNewSubPath (x, y + h);
            path.lineTo (x + w * 0.55f, y + h * 0.35f);
            path.lineTo (x + w, y + (kind == NodeKind::limiter ? h * 0.35f : h * 0.2f));
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::feedbackGuard:
            path.addCentredArc (x + w * 0.5f, y + h * 0.5f, w * 0.38f, h * 0.38f,
                                0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            graphics.strokePath (path, stroke);
            path.clear();
            path.addTriangle (x + w * 0.62f, y, x + w * 0.98f, y + h * 0.22f, x + w * 0.58f, y + h * 0.38f);
            graphics.fillPath (path);
            return;
        case NodeKind::lfo:
            path.startNewSubPath (x, y + h * 0.5f);
            for (int step = 1; step <= 12; ++step)
            {
                const auto t = static_cast<float> (step) / 12.0f;
                path.lineTo (x + w * t,
                             y + h * 0.5f - std::sin (t * juce::MathConstants<float>::twoPi) * h * 0.38f);
            }
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::envelopeFollower:
            path.startNewSubPath (x, y + h);
            path.quadraticTo (x + w * 0.18f, y, x + w * 0.36f, y + h * 0.25f);
            path.quadraticTo (x + w * 0.7f, y + h * 0.62f, x + w, y + h * 0.85f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::randomLfo:
        {
            const float levels[] { 0.3f, 0.75f, 0.15f, 0.6f, 0.4f };
            path.startNewSubPath (x, y + h * levels[0]);
            for (int step = 0; step < 4; ++step)
            {
                path.lineTo (x + w * 0.25f * static_cast<float> (step + 1), y + h * levels[step]);
                path.lineTo (x + w * 0.25f * static_cast<float> (step + 1), y + h * levels[step + 1]);
            }
            graphics.strokePath (path, juce::PathStrokeType (1.4f));
            return;
        }
        case NodeKind::stepSequencer:
        {
            const float heights[] { 0.45f, 0.8f, 0.3f, 0.65f };
            for (int step = 0; step < 4; ++step)
                graphics.fillRect (x + w * 0.25f * static_cast<float> (step),
                                   y + h * (1.0f - heights[step]),
                                   w * 0.18f, h * heights[step]);
            return;
        }
        case NodeKind::macro:
            graphics.drawEllipse (x + w * 0.12f, y + h * 0.12f, w * 0.76f, h * 0.76f, 1.6f);
            path.startNewSubPath (x + w * 0.5f, y + h * 0.5f);
            path.lineTo (x + w * 0.28f, y + h * 0.22f);
            graphics.strokePath (path, stroke);
            return;
        case NodeKind::neuralPedal:
            graphics.drawRoundedRectangle (x + w * 0.14f, y, w * 0.72f, h, 2.5f, 1.4f);
            graphics.fillEllipse (x + w * 0.5f - 2.6f, y + h * 0.68f - 2.6f, 5.2f, 5.2f);
            graphics.fillEllipse (x + w * 0.32f - 1.5f, y + h * 0.28f - 1.5f, 3.0f, 3.0f);
            graphics.fillEllipse (x + w * 0.5f - 1.5f, y + h * 0.18f - 1.5f, 3.0f, 3.0f);
            graphics.fillEllipse (x + w * 0.68f - 1.5f, y + h * 0.28f - 1.5f, 3.0f, 3.0f);
            return;
        case NodeKind::neuralAmpPlaceholder:
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < (column == 1 ? 3 : 2); ++row)
                {
                    const auto nodeX = x + w * 0.5f * static_cast<float> (column);
                    const auto nodeY = column == 1 ? y + h * 0.5f * static_cast<float> (row)
                                                   : y + h * (0.25f + 0.5f * static_cast<float> (row));
                    graphics.fillEllipse (nodeX - 1.8f, nodeY - 1.8f, 3.6f, 3.6f);
                }
            return;
    }
}

const std::vector<NodePaletteEntry>& nodePalette()
{
    static const std::vector<NodePaletteEntry> entries {
        { NodeKind::gain,                 "GAIN",           "UTILITY",     "Trim or boost a mono signal" },
        { NodeKind::mixer,                "4-CH MIXER",     "UTILITY",     "Sum four mono signals" },
        { NodeKind::crossfade,            "CROSSFADE",      "UTILITY",     "Equal-power blend between two signals" },
        { NodeKind::distortion,           "DISTORTION",     "EFFECTS",     "Saturating drive with live pre/post waveform" },
        { NodeKind::filter,               "FILTER",         "EFFECTS",     "State-variable LP/BP/HP/notch with modulatable cutoff" },
        { NodeKind::delay,                "DELAY",          "EFFECTS",     "Modulatable bounded-feedback delay" },
        { NodeKind::reverb,               "REVERB",         "EFFECTS",     "Mono Freeverb-style room" },
        { NodeKind::chorus,               "CHORUS",         "EFFECTS",     "Modulated short delay thickener" },
        { NodeKind::phaser,               "PHASER",         "EFFECTS",     "Six-stage swept all-pass phaser" },
        { NodeKind::tremolo,              "TREMOLO",        "EFFECTS",     "Sine or rounded-square amplitude modulation" },
        { NodeKind::bitcrusher,           "BITCRUSHER",     "EFFECTS",     "Bit-depth and sample-rate reduction" },
        { NodeKind::ringMod,              "RING MOD",       "EFFECTS",     "Carrier multiplication; robots and bells" },
        { NodeKind::pitchShifter,         "PITCH SHIFTER",  "EFFECTS",     "Dual-tap windowed transposition, -24..+24 st" },
        { NodeKind::granular,             "GRANULAR",       "EFFECTS",     "Grain cloud over a live 4 s buffer" },
        { NodeKind::neuralAmpPlaceholder, "NEURAL AMP",     "NEURAL",      "Neural Amp Modeler head; load a .nam capture" },
        { NodeKind::neuralPedal,          "NEURAL PEDAL",   "NEURAL",      "NAM pedal capture with wet/dry mix; step models with the arrows" },
        { NodeKind::vowelFilter,          "VOWEL FILTER",   "VOICE",       "Formant filter morphing A-E-I-O-U" },
        { NodeKind::vocoder,              "VOCODER",        "VOICE",       "12-band vocoder: voice shapes carrier" },
        { NodeKind::pitchCorrector,       "AUTOTUNE",       "VOICE",       "Pitch detection snapped to a scale" },
        { NodeKind::monoSynth,            "MONO SYNTH",     "INSTRUMENTS", "Subtractive voice; gate and pitch are patchable" },
        { NodeKind::pluck,                "PLUCK",          "INSTRUMENTS", "Karplus-Strong string; trigger it from a sequencer" },
        { NodeKind::noiseSource,          "NOISE",          "INSTRUMENTS", "White, pink or brown noise source" },
        { NodeKind::drumMachine,          "DRUM MACHINE",   "INSTRUMENTS", "Kick/snare/hat step machine; click the grid" },
        { NodeKind::sampler,              "SAMPLER",        "INSTRUMENTS", "Records the input; retrigger, pitch and loop it" },
        { NodeKind::fourTrack,            "4-TRACK",        "INSTRUMENTS", "60 s tape loop with four armable tracks and varispeed" },
        { NodeKind::compressor,           "COMPRESSOR",     "DYNAMICS",    "Zero-lookahead dynamics" },
        { NodeKind::limiter,              "LIMITER",        "DYNAMICS",    "Zero-lookahead live safety limiter" },
        { NodeKind::gate,                 "NOISE GATE",     "DYNAMICS",    "Threshold gate with attack, release and range" },
        { NodeKind::feedbackGuard,        "FEEDBACK GUARD", "DYNAMICS",    "Required causal delay and hard safety bound for loops" },
        { NodeKind::lfo,                  "LFO",            "CONTROL",     "Audio-rate sine, triangle, square or saw control" },
        { NodeKind::randomLfo,            "RANDOM",         "CONTROL",     "Sample-and-hold random control with slew" },
        { NodeKind::envelopeFollower,     "ENVELOPE",       "CONTROL",     "Convert audio amplitude to control" },
        { NodeKind::stepSequencer,        "8-STEP SEQ",     "CONTROL",     "Sample-clocked control sequence" },
        { NodeKind::macro,                "MACRO",          "CONTROL",     "Manual control value with slew, patchable anywhere" },
        { NodeKind::spectralFollower,     "SPECTRAL (FFT)", "CONTROL",     "FFT band energies, centroid and level as control outs" },
        { NodeKind::script,               "SCRIPT",         "CONTROL",     "Per-sample expression: y = f(x, a, b, c, t)" }
    };
    return entries;
}

LabLookAndFeel::LabLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::workspace);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::nodeDark);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, colours::nodeDark);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, colours::selection);
    setColour (juce::Slider::trackColourId, colours::control.withAlpha (0.7f));
    setColour (juce::Slider::backgroundColourId, colours::nodeDark);
    setColour (juce::ScrollBar::thumbColourId, colours::mutedText.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, colours::nodeDark);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::grid.brighter (0.3f));
    setColour (juce::ComboBox::arrowColourId, colours::mutedText);
    setColour (juce::PopupMenu::backgroundColourId, colours::panelRaised);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::grid.brighter (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TextEditor::backgroundColourId, colours::nodeDark);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::grid.brighter (0.3f));
    setColour (juce::TextEditor::focusedOutlineColourId, colours::selection.withAlpha (0.6f));
    setColour (juce::ListBox::backgroundColourId, colours::panel);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::selection);
    setColour (juce::ToggleButton::tickDisabledColourId, colours::mutedText);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::grid.brighter (0.4f));
    setColour (juce::DialogWindow::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::textColourId, colours::text);
}

void LabLookAndFeel::drawRotarySlider (juce::Graphics& graphics,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       float sliderPosition,
                                       float rotaryStartAngle,
                                       float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto radius = static_cast<float> (juce::jmin (width, height)) * 0.5f - 4.0f;
    const auto centre = juce::Point<float> (static_cast<float> (x) + static_cast<float> (width) * 0.5f,
                                            static_cast<float> (y) + static_cast<float> (height) * 0.5f);
    const auto bounds = juce::Rectangle<float> (centre.x - radius, centre.y - radius,
                                                radius * 2.0f, radius * 2.0f);
    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour (juce::Slider::thumbColourId);

    // Knob body with a subtle top light so it reads as a physical control.
    graphics.setGradientFill (juce::ColourGradient (colours::nodeTop.brighter (0.06f),
                                                    centre.x, bounds.getY(),
                                                    colours::nodeDark,
                                                    centre.x, bounds.getBottom(), false));
    graphics.fillEllipse (bounds.reduced (2.5f));
    graphics.setColour (juce::Colours::black.withAlpha (0.5f));
    graphics.drawEllipse (bounds.reduced (2.5f), 1.0f);
    graphics.setColour (juce::Colours::white.withAlpha (0.05f));
    graphics.drawEllipse (bounds.reduced (4.0f), 1.0f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius, radius,
                                 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    graphics.setColour (colours::nodeDark.brighter (0.25f));
    graphics.strokePath (backgroundArc, juce::PathStrokeType (2.6f,
                                                              juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius, radius,
                            0.0f, rotaryStartAngle, angle, true);
    graphics.setColour (accent.withAlpha (0.25f));
    graphics.strokePath (valueArc, juce::PathStrokeType (5.0f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
    graphics.setColour (accent);
    graphics.strokePath (valueArc, juce::PathStrokeType (2.6f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

    const auto tip = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius;
    graphics.fillEllipse (tip.x - 2.6f, tip.y - 2.6f, 5.2f, 5.2f);

    const auto pointerStart = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.28f);
    const auto pointerEnd = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius * 0.72f);
    graphics.setColour (colours::text);
    graphics.drawLine ({ pointerStart, pointerEnd }, 2.0f);
}

void LabLookAndFeel::drawLinearSlider (juce::Graphics& graphics,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       float sliderPosition,
                                       float minSliderPosition,
                                       float maxSliderPosition,
                                       juce::Slider::SliderStyle style,
                                       juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (graphics, x, y, width, height, sliderPosition,
                                          minSliderPosition, maxSliderPosition, style, slider);
        return;
    }

    const auto centreY = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const auto track = juce::Rectangle<float> (static_cast<float> (x), centreY - 2.0f,
                                               static_cast<float> (width), 4.0f);
    const auto accent = slider.findColour (juce::Slider::trackColourId);

    graphics.setColour (colours::nodeDark);
    graphics.fillRoundedRectangle (track, 2.0f);
    graphics.setColour (juce::Colours::white.withAlpha (0.04f));
    graphics.drawRoundedRectangle (track, 2.0f, 1.0f);
    graphics.setColour (accent);
    graphics.fillRoundedRectangle (track.withRight (sliderPosition), 2.0f);

    graphics.setColour (colours::text);
    graphics.fillEllipse (sliderPosition - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
    graphics.setColour (juce::Colours::black.withAlpha (0.4f));
    graphics.drawEllipse (sliderPosition - 4.0f, centreY - 4.0f, 8.0f, 8.0f, 1.0f);
}

void LabLookAndFeel::drawButtonBackground (juce::Graphics& graphics,
                                           juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool isMouseOverButton,
                                           bool isButtonDown)
{
    auto colour = backgroundColour;
    if (isButtonDown)
        colour = colour.darker (0.15f);
    else if (isMouseOverButton)
        colour = colour.brighter (0.1f);

    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    graphics.setGradientFill (juce::ColourGradient (colour.brighter (0.08f),
                                                    bounds.getCentreX(), bounds.getY(),
                                                    colour.darker (0.08f),
                                                    bounds.getCentreX(), bounds.getBottom(), false));
    graphics.fillRoundedRectangle (bounds, 6.0f);
    graphics.setColour (juce::Colours::white.withAlpha (isMouseOverButton ? 0.16f : 0.07f));
    graphics.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

juce::Font LabLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (12.5f, static_cast<float> (buttonHeight) * 0.55f),
                                          juce::Font::bold));
}
} // namespace signalpatch::ui
