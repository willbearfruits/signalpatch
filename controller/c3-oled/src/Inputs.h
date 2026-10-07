#pragma once

// Raw input reads. Decoding (debounce, quadrature, mapping) lives in core/.

#include <cstdint>

namespace inputs
{
constexpr int kButtonCount = 4; // buttons 1-3, encoder push
constexpr int kAnalogCount = 4; // pots 1-3, expression

void begin();

/** True while button `index` (0-2 buttons 1-3, 3 the encoder push) is held down. */
bool buttonDown (int index);

/** Encoder detents counted by the pin interrupts since the last call (positive = clockwise). */
int takeEncoderDetents();

/** Average of `samples` calibrated ADC reads of input `index`, in millivolts. */
int readMillivolts (int index, int samples);
} // namespace inputs
