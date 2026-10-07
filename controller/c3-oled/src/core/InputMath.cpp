#include "InputMath.h"

#include <cmath>

namespace c3
{
// ------------------------------------------------------------ Quadrature

namespace
{
    // Pin state index = (b << 1) | a. Low nibble = next state, high bits = step.
    constexpr uint8_t kCw = 0x10;
    constexpr uint8_t kCcw = 0x20;

    // Full step. States: 0 start (rest, 11), 1 cw final, 2 cw begin, 3 cw next,
    // 4 ccw begin, 5 ccw final, 6 ccw next.
    constexpr uint8_t fullTable[7][4] = {
        /* start     */ { 0x0, 0x2, 0x4, 0x0 },
        /* cw final  */ { 0x3, 0x0, 0x1, 0x0 | kCw },
        /* cw begin  */ { 0x3, 0x2, 0x0, 0x0 },
        /* cw next   */ { 0x3, 0x2, 0x1, 0x0 },
        /* ccw begin */ { 0x6, 0x0, 0x4, 0x0 },
        /* ccw final */ { 0x6, 0x5, 0x0, 0x0 | kCcw },
        /* ccw next  */ { 0x6, 0x5, 0x4, 0x0 },
    };

    // Half step: rest at 11 and at 00.
    // States: 0 start (11), 1 ccw begin, 2 cw begin, 3 start middle (00),
    // 4 cw begin middle, 5 ccw begin middle.
    constexpr uint8_t halfTable[6][4] = {
        /* start         */ { 0x3, 0x2, 0x1, 0x0 },
        /* ccw begin     */ { 0x3 | kCcw, 0x0, 0x1, 0x0 },
        /* cw begin      */ { 0x3 | kCw, 0x2, 0x0, 0x0 },
        /* start middle  */ { 0x3, 0x5, 0x4, 0x0 },
        /* cw begin mid  */ { 0x3, 0x3, 0x4, 0x0 | kCw },
        /* ccw begin mid */ { 0x3, 0x5, 0x3, 0x0 | kCcw },
    };
} // namespace

int QuadratureDecoder::update (bool a, bool b)
{
    const uint8_t pins = static_cast<uint8_t> ((b ? 2 : 0) | (a ? 1 : 0));
    const uint8_t next = half ? halfTable[state & 0x0F][pins] : fullTable[state & 0x0F][pins];
    state = next & 0x0F;
    if (next & kCw)
        return 1;
    if (next & kCcw)
        return -1;
    return 0;
}

// ------------------------------------------------------------ Acceleration

int EncoderAcceleration::stepForInterval (uint32_t intervalMs)
{
    // About 60 ms per detent and slower: single steps. Faster spins scale up
    // to kMaxStep (a detent every 7.5 ms or less).
    if (intervalMs >= 60)
        return 1;
    if (intervalMs == 0)
        return kMaxStep;
    const int n = static_cast<int> ((60 + intervalMs / 2) / intervalMs);
    return n < 1 ? 1 : (n > kMaxStep ? kMaxStep : n);
}

int EncoderAcceleration::step (int direction, int detents, uint32_t nowMs, bool enabled)
{
    if (detents <= 0)
        return 1;
    const uint32_t since = nowMs - lastMs;
    const bool fresh = ! started || direction != lastDirection || since > 250;
    const uint32_t interval = since / static_cast<uint32_t> (detents);
    averageMs = fresh ? 120 : (averageMs + interval) / 2;
    started = true;
    lastMs = nowMs;
    lastDirection = direction;
    return enabled ? stepForInterval (averageMs) : 1;
}

uint8_t relativeCcValue (int direction, int step)
{
    if (step < 1)
        step = 1;
    if (step > 63)
        step = 63;
    return static_cast<uint8_t> (direction >= 0 ? 64 + step : 64 - step);
}

// ------------------------------------------------------------ Debounce

bool Debouncer::update (bool level, uint32_t nowMs)
{
    if (! primed)
    {
        prime (level);
        changedAt = nowMs;
        return false;
    }
    if (level != raw)
    {
        raw = level;
        changedAt = nowMs;
    }
    if (raw != stable && nowMs - changedAt >= hold)
    {
        stable = raw;
        return true;
    }
    return false;
}

// ------------------------------------------------------------ Analog

float millivoltsToPosition (int millivolts, int fullScaleMv)
{
    if (fullScaleMv <= 0 || millivolts <= 0)
        return 0.0f;
    if (millivolts >= fullScaleMv)
        return 127.0f;
    return static_cast<float> (millivolts) * 127.0f / static_cast<float> (fullScaleMv);
}

float Smoother::update (float x, float coefficient)
{
    if (! primed)
    {
        y = x;
        primed = true;
        return y;
    }
    y += (x - y) * coefficient;
    return y;
}

int CcQuantiser::update (float position, int deadBand, uint32_t nowMs, uint32_t minIntervalMs)
{
    if (position < 0.0f)
        position = 0.0f;
    if (position > 127.0f)
        position = 127.0f;
    const int candidate = static_cast<int> (std::lround (position));

    if (last < 0)
    {
        last = candidate;
        if (! sendFirstValue)
            return -1;
        lastSentMs = nowMs;
        everSent = true;
        return candidate;
    }

    // Moving on in the same direction needs half a step plus the hysteresis;
    // starting out or turning back also needs the dead band.
    const int direction = position > static_cast<float> (last) ? 1 : -1;
    const float threshold = 0.5f + kHysteresis + (direction == lastDirection ? 0.0f : static_cast<float> (deadBand));
    const bool atNewEnd = (candidate == 0 || candidate == 127) && candidate != last;
    const bool moved = std::fabs (position - static_cast<float> (last)) >= threshold;
    if (! (moved || atNewEnd) || candidate == last)
        return -1;
    if (everSent && nowMs - lastSentMs < minIntervalMs)
        return -1; // try again on a later update
    lastDirection = candidate > last ? 1 : -1;
    last = candidate;
    lastSentMs = nowMs;
    everSent = true;
    return candidate;
}

float angleToPosition (float degrees, float rangeDegrees)
{
    if (rangeDegrees < 1.0f)
        rangeDegrees = 1.0f;
    const float x = degrees / rangeDegrees; // -1..1 inside the range
    const float position = x >= 0.0f ? 64.0f + x * 63.0f : 64.0f + x * 64.0f;
    if (position < 0.0f)
        return 0.0f;
    if (position > 127.0f)
        return 127.0f;
    return position;
}
} // namespace c3
