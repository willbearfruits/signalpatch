#pragma once

// Input decoding that does not need hardware: rotary encoder quadrature,
// encoder acceleration, button debouncing, ADC millivolts to CC, and the CC
// quantiser (hysteresis, dead band, rate limit).

#include <cstdint>

namespace c3
{
// ------------------------------------------------------------ Quadrature

/** Rotary encoder state machine (after Ben Buxton's table decoder).
    It reports a step only after a complete, valid sequence of A/B states,
    so contact bounce and impossible transitions never count.
    Full-step: one step per four transitions (rest at A=B=1 with pull-ups),
    the usual EC11 with one detent per quadrature cycle.
    Half-step: one step per two transitions (rest at 00 and 11). */
class QuadratureDecoder
{
public:
    explicit QuadratureDecoder (bool halfStep = false) : half (halfStep) {}

    /** Feed the current pin levels; returns +1, -1 or 0. */
    int update (bool a, bool b);
    void reset() { state = 0; }

private:
    bool half;
    uint8_t state = 0;
};

// ------------------------------------------------------------ Acceleration

/** Turns detent timing into a step size n for the relative CC (64 ± n). */
class EncoderAcceleration
{
public:
    static constexpr int kMaxStep = 8;

    /** `detents` arrived together at `nowMs` in direction `direction` (+1/-1).
        Returns the step size for each of them (1 when acceleration is off). */
    int step (int direction, int detents, uint32_t nowMs, bool enabled);

    /** Step size for an average interval between detents (pure, for tests). */
    static int stepForInterval (uint32_t intervalMs);

private:
    uint32_t lastMs = 0;
    uint32_t averageMs = 120;
    int lastDirection = 0;
    bool started = false;
};

/** 64 + n for clockwise, 64 - n for counter-clockwise, kept inside 1..127. */
uint8_t relativeCcValue (int direction, int step);

// ------------------------------------------------------------ Debounce

/** A level must hold for `holdMs` before it counts. */
class Debouncer
{
public:
    explicit Debouncer (uint32_t holdMs = 5) : hold (holdMs) {}

    void prime (bool level) { stable = raw = level; primed = true; }

    /** Returns true when the debounced level changed; read it with pressed()/level(). */
    bool update (bool level, uint32_t nowMs);
    bool level() const { return stable; }

private:
    uint32_t hold;
    uint32_t changedAt = 0;
    bool raw = false;
    bool stable = false;
    bool primed = false;
};

// ------------------------------------------------------------ Analog

/** ADC millivolts (0..fullScaleMv) to a CC position 0..127 as a float. */
float millivoltsToPosition (int millivolts, int fullScaleMv = 2450);

/** One-pole low-pass. coefficient 1 = no smoothing. */
class Smoother
{
public:
    void reset() { primed = false; }
    float update (float x, float coefficient);
    float value() const { return y; }
    bool isPrimed() const { return primed; }

private:
    float y = 0.0f;
    bool primed = false;
};

/** Turns a continuous position (0..127) into CC values worth sending.
    While the control keeps moving one way every step goes out (with a
    quarter step of hysteresis). Starting from rest or turning back needs a
    move larger than the dead band too, so noise around a resting value
    sends nothing. Reaching an end (0 / 127) always sends. At most one value
    per `minIntervalMs`. */
class CcQuantiser
{
public:
    static constexpr float kHysteresis = 0.25f;

    /** sendFirst: the first update after a reset sends its value; otherwise it only records it. */
    void reset (bool sendFirst)
    {
        last = -1;
        lastDirection = 0;
        sendFirstValue = sendFirst;
    }

    /** Returns the value to send, or -1. */
    int update (float position, int deadBand, uint32_t nowMs, uint32_t minIntervalMs = 10);

    int lastValue() const { return last; }

private:
    int last = -1;
    int lastDirection = 0;
    uint32_t lastSentMs = 0;
    bool sendFirstValue = false;
    bool everSent = false;
};

/** Angle (degrees from level) to CC: -range → 0, 0 → 64, +range → 127, clamped. */
float angleToPosition (float degrees, float rangeDegrees);
} // namespace c3
