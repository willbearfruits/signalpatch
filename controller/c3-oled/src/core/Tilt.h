#pragma once

// Complementary filter for tilt (pitch) and roll from an accelerometer and a
// gyroscope. Angles in degrees, -180..180.

namespace c3
{
struct ImuSample
{
    float ax = 0, ay = 0, az = 1; // acceleration in g
    float gx = 0, gy = 0, gz = 0; // rotation in degrees per second
};

class TiltFilter
{
public:
    /** Weight of the integrated gyro against the accelerometer per update at 100 Hz. */
    static constexpr float kGyroWeight = 0.98f;

    void reset() { primed = false; }
    void update (const ImuSample& sample, float dtSeconds);

    bool isPrimed() const { return primed; }
    float pitch() const { return pitchDeg; }
    float roll() const { return rollDeg; }

    /** Angles from gravity alone (for tests and for priming). */
    static float accelPitch (const ImuSample& s);
    static float accelRoll (const ImuSample& s);

private:
    float pitchDeg = 0;
    float rollDeg = 0;
    bool primed = false;
};

/** Wraps an angle into -180..180. */
float wrapDegrees (float degrees);
} // namespace c3
