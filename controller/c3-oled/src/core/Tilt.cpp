#include "Tilt.h"

#include <cmath>

namespace c3
{
namespace
{
    constexpr float kRadToDeg = 57.29577951f;
}

float wrapDegrees (float degrees)
{
    while (degrees > 180.0f)
        degrees -= 360.0f;
    while (degrees < -180.0f)
        degrees += 360.0f;
    return degrees;
}

float TiltFilter::accelPitch (const ImuSample& s)
{
    return std::atan2 (-s.ax, std::sqrt (s.ay * s.ay + s.az * s.az)) * kRadToDeg;
}

float TiltFilter::accelRoll (const ImuSample& s)
{
    return std::atan2 (s.ay, s.az) * kRadToDeg;
}

void TiltFilter::update (const ImuSample& s, float dt)
{
    const float accPitch = accelPitch (s);
    const float accRoll = accelRoll (s);
    if (! primed || ! (dt > 0.0f) || dt > 0.5f)
    {
        // First sample, or a gap long enough that the integrated gyro means nothing.
        pitchDeg = accPitch;
        rollDeg = accRoll;
        primed = true;
        return;
    }
    // Keep the gyro weight per second constant whatever the sample interval.
    const float weight = std::pow (kGyroWeight, dt / 0.01f);
    const float pitchPredicted = pitchDeg + s.gy * dt;
    const float rollPredicted = rollDeg + s.gx * dt;
    pitchDeg = wrapDegrees (pitchPredicted + (1.0f - weight) * wrapDegrees (accPitch - pitchPredicted));
    rollDeg = wrapDegrees (rollPredicted + (1.0f - weight) * wrapDegrees (accRoll - rollPredicted));
}
} // namespace c3
