// Encoder decoding and acceleration, debouncing, analog mapping, tilt.

#include "core/InputMath.h"
#include "core/PacketRing.h"
#include "core/Tilt.h"

#include <unity.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace c3;

void setUp() {}
void tearDown() {}

namespace
{
int run (QuadratureDecoder& decoder, const std::vector<std::pair<int, int>>& states)
{
    int total = 0;
    for (const auto& s : states)
        total += decoder.update (s.first != 0, s.second != 0);
    return total;
}
} // namespace

// ------------------------------------------------------------ Quadrature

void test_full_step_counts_one_per_cycle()
{
    QuadratureDecoder d;
    // (a, b) with pull-ups: rest is 1,1.
    TEST_ASSERT_EQUAL (0, run (d, { { 1, 1 }, { 1, 0 }, { 0, 0 }, { 0, 1 } }));
    TEST_ASSERT_EQUAL (1, d.update (true, true));
    TEST_ASSERT_EQUAL (-1, run (d, { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } }));
    TEST_ASSERT_EQUAL (3, run (d, { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 },
                                    { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } }));
}

void test_full_step_ignores_bounce_and_invalid_jumps()
{
    QuadratureDecoder d;
    // Contact bounce on every edge still gives exactly one step.
    TEST_ASSERT_EQUAL (1, run (d, { { 1, 1 }, { 1, 0 }, { 1, 1 }, { 1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 0 }, { 0, 1 },
                                    { 0, 0 }, { 0, 1 }, { 1, 1 }, { 0, 1 }, { 1, 1 } }));
    // Half a cycle and back: nothing.
    TEST_ASSERT_EQUAL (0, run (d, { { 1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } }));
    // Both pins flipping at once is impossible: nothing.
    TEST_ASSERT_EQUAL (0, run (d, { { 0, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 } }));
    // Repeated identical reads (polling) change nothing.
    TEST_ASSERT_EQUAL (0, run (d, { { 1, 1 }, { 1, 1 }, { 1, 1 } }));
}

void test_half_step_counts_two_per_cycle()
{
    QuadratureDecoder d (true);
    TEST_ASSERT_EQUAL (2, run (d, { { 1, 1 }, { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } }));
    TEST_ASSERT_EQUAL (-2, run (d, { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } }));
}

// ------------------------------------------------------------ Acceleration

void test_acceleration_curve()
{
    TEST_ASSERT_EQUAL (1, EncoderAcceleration::stepForInterval (500));
    TEST_ASSERT_EQUAL (1, EncoderAcceleration::stepForInterval (60));
    TEST_ASSERT_EQUAL (2, EncoderAcceleration::stepForInterval (30));
    TEST_ASSERT_EQUAL (3, EncoderAcceleration::stepForInterval (20));
    TEST_ASSERT_EQUAL (6, EncoderAcceleration::stepForInterval (10));
    TEST_ASSERT_EQUAL (8, EncoderAcceleration::stepForInterval (5));
    TEST_ASSERT_EQUAL (8, EncoderAcceleration::stepForInterval (0));
    for (uint32_t ms = 1; ms < 300; ++ms)
        TEST_ASSERT_TRUE (EncoderAcceleration::stepForInterval (ms) >= EncoderAcceleration::stepForInterval (ms + 1));
}

void test_acceleration_builds_on_fast_spins_and_resets()
{
    EncoderAcceleration a;
    uint32_t t = 1000;
    for (int i = 0; i < 10; ++i, t += 200)
        TEST_ASSERT_EQUAL (1, a.step (1, 1, t, true)); // slow turning: always 1
    int step = 1;
    for (int i = 0; i < 12; ++i, t += 8)
        step = a.step (1, 1, t, true);
    TEST_ASSERT_TRUE (step >= 6);
    TEST_ASSERT_EQUAL (1, a.step (-1, 1, t + 8, true)); // direction change starts over
    TEST_ASSERT_EQUAL (1, a.step (-1, 1, t + 600, true)); // a pause starts over

    EncoderAcceleration off;
    for (int i = 0; i < 12; ++i, t += 8)
        TEST_ASSERT_EQUAL (1, off.step (1, 1, t, false));
}

void test_relative_cc_values()
{
    TEST_ASSERT_EQUAL (65, relativeCcValue (1, 1));
    TEST_ASSERT_EQUAL (63, relativeCcValue (-1, 1));
    TEST_ASSERT_EQUAL (72, relativeCcValue (1, 8));
    TEST_ASSERT_EQUAL (56, relativeCcValue (-1, 8));
    TEST_ASSERT_EQUAL (127, relativeCcValue (1, 200));
    TEST_ASSERT_EQUAL (1, relativeCcValue (-1, 200));
    TEST_ASSERT_EQUAL (65, relativeCcValue (1, 0));
}

// ------------------------------------------------------------ Debounce

void test_debounce_needs_a_stable_level()
{
    Debouncer d (5);
    TEST_ASSERT_FALSE (d.update (false, 0)); // primes
    TEST_ASSERT_FALSE (d.level());
    TEST_ASSERT_FALSE (d.update (true, 1));
    TEST_ASSERT_FALSE (d.update (false, 2)); // bounce
    TEST_ASSERT_FALSE (d.update (true, 3));
    TEST_ASSERT_FALSE (d.update (true, 7));
    TEST_ASSERT_TRUE (d.update (true, 8)); // 5 ms stable
    TEST_ASSERT_TRUE (d.level());
    TEST_ASSERT_FALSE (d.update (true, 20));
    TEST_ASSERT_FALSE (d.update (false, 21));
    TEST_ASSERT_FALSE (d.update (false, 25));
    TEST_ASSERT_TRUE (d.update (false, 26));
    TEST_ASSERT_FALSE (d.level());
}

// ------------------------------------------------------------ Analog

void test_millivolts_to_position()
{
    TEST_ASSERT_EQUAL_FLOAT (0.0f, millivoltsToPosition (0));
    TEST_ASSERT_EQUAL_FLOAT (0.0f, millivoltsToPosition (-20));
    TEST_ASSERT_EQUAL_FLOAT (127.0f, millivoltsToPosition (2450));
    TEST_ASSERT_EQUAL_FLOAT (127.0f, millivoltsToPosition (3300));
    TEST_ASSERT_FLOAT_WITHIN (0.01f, 63.5f, millivoltsToPosition (1225));
}

void test_quantiser_does_not_send_its_first_reading()
{
    CcQuantiser q;
    q.reset (false);
    TEST_ASSERT_EQUAL (-1, q.update (40.2f, 1, 0));
    TEST_ASSERT_EQUAL (40, q.lastValue());

    CcQuantiser first;
    first.reset (true);
    TEST_ASSERT_EQUAL (64, first.update (64.1f, 0, 0));
}

void test_quantiser_hysteresis_and_dead_band()
{
    CcQuantiser q;
    q.reset (false);
    q.update (64.0f, 0, 0);
    // Dead band 0: a quarter step of hysteresis around the rounding point.
    TEST_ASSERT_EQUAL (-1, q.update (64.6f, 0, 100));
    TEST_ASSERT_EQUAL (65, q.update (64.8f, 0, 200));
    TEST_ASSERT_EQUAL (-1, q.update (64.4f, 0, 300)); // noise around 64.5 does not flicker
    TEST_ASSERT_EQUAL (-1, q.update (64.6f, 0, 400));
    TEST_ASSERT_EQUAL (64, q.update (64.2f, 0, 500));

    // Dead band 2: single and double steps are ignored.
    CcQuantiser wide;
    wide.reset (false);
    wide.update (64.0f, 2, 0);
    TEST_ASSERT_EQUAL (-1, wide.update (66.0f, 2, 100));
    TEST_ASSERT_EQUAL (67, wide.update (66.8f, 2, 200));
    TEST_ASSERT_EQUAL (68, wide.update (67.8f, 2, 250)); // carrying on the same way: every step
    TEST_ASSERT_EQUAL (-1, wide.update (66.0f, 2, 300)); // turning back needs the dead band
    TEST_ASSERT_EQUAL (65, wide.update (65.2f, 2, 400));
}

void test_quantiser_slow_sweep_sends_every_step()
{
    CcQuantiser q;
    q.reset (false);
    q.update (20.0f, 1, 0);
    std::vector<int> sent;
    uint32_t t = 0;
    for (float p = 20.0f; p <= 40.0f; p += 0.05f)
    {
        t += 4;
        const int v = q.update (p, 1, t);
        if (v >= 0)
            sent.push_back (v);
    }
    // Starting out takes the dead band (21 is skipped), then nothing is missed.
    TEST_ASSERT_EQUAL (22, sent.front());
    TEST_ASSERT_EQUAL (40, sent.back());
    for (size_t i = 1; i < sent.size(); ++i)
        TEST_ASSERT_EQUAL (sent[i - 1] + 1, sent[i]);
    // Noise at rest sends nothing.
    for (int i = 0; i < 100; ++i)
        TEST_ASSERT_EQUAL (-1, q.update (40.0f + ((i % 2) ? 0.6f : -0.6f), 1, t += 4));
}

void test_quantiser_reaches_the_ends_whatever_the_dead_band()
{
    CcQuantiser q;
    q.reset (false);
    q.update (3.0f, 4, 0);
    TEST_ASSERT_EQUAL (0, q.update (0.3f, 4, 100));
    TEST_ASSERT_EQUAL (-1, q.update (0.6f, 4, 200));
    q.update (124.0f, 4, 300);
    TEST_ASSERT_EQUAL (127, q.update (126.6f, 4, 400));
}

void test_quantiser_rate_limit()
{
    CcQuantiser q;
    q.reset (false);
    q.update (10.0f, 0, 0);
    TEST_ASSERT_EQUAL (20, q.update (20.0f, 0, 1000));
    TEST_ASSERT_EQUAL (-1, q.update (30.0f, 0, 1004)); // under 10 ms since the last
    TEST_ASSERT_EQUAL (-1, q.update (31.0f, 0, 1009));
    TEST_ASSERT_EQUAL (32, q.update (32.0f, 0, 1010)); // the latest value goes out
}

void test_angle_to_position()
{
    TEST_ASSERT_EQUAL_FLOAT (64.0f, angleToPosition (0.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT (127.0f, angleToPosition (30.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT (0.0f, angleToPosition (-30.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT (127.0f, angleToPosition (75.0f, 30.0f));
    TEST_ASSERT_EQUAL_FLOAT (0.0f, angleToPosition (-75.0f, 30.0f));
    TEST_ASSERT_FLOAT_WITHIN (0.01f, 95.5f, angleToPosition (15.0f, 30.0f));
    TEST_ASSERT_FLOAT_WITHIN (0.01f, 32.0f, angleToPosition (-45.0f, 90.0f));
}

// ------------------------------------------------------------ Tilt

void test_tilt_from_gravity()
{
    ImuSample flat;
    TEST_ASSERT_FLOAT_WITHIN (0.01f, 0.0f, TiltFilter::accelPitch (flat));
    TEST_ASSERT_FLOAT_WITHIN (0.01f, 0.0f, TiltFilter::accelRoll (flat));

    ImuSample pitched;
    pitched.ax = -std::sin (0.349066f); // 20 degrees
    pitched.az = std::cos (0.349066f);
    TEST_ASSERT_FLOAT_WITHIN (0.05f, 20.0f, TiltFilter::accelPitch (pitched));

    ImuSample rolled;
    rolled.ay = std::sin (0.523599f); // 30 degrees
    rolled.az = std::cos (0.523599f);
    TEST_ASSERT_FLOAT_WITHIN (0.05f, 30.0f, TiltFilter::accelRoll (rolled));
}

void test_tilt_filter_follows_gyro_and_settles_on_gravity()
{
    TiltFilter f;
    ImuSample s;
    f.update (s, 0.01f);
    TEST_ASSERT_TRUE (f.isPrimed());

    // Rotate about Y at 100 deg/s for 0.2 s; the accelerometer agrees.
    for (int i = 1; i <= 20; ++i)
    {
        const float deg = static_cast<float> (i);
        s.gy = 100.0f;
        s.ax = -std::sin (deg * 0.0174533f);
        s.az = std::cos (deg * 0.0174533f);
        f.update (s, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN (1.0f, 20.0f, f.pitch());

    // Held still at 20 degrees with a gyro bias of 2 deg/s: settles near 20.
    for (int i = 0; i < 500; ++i)
    {
        s.gy = 2.0f;
        f.update (s, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN (1.5f, 20.0f, f.pitch());
    TEST_ASSERT_FLOAT_WITHIN (0.5f, 0.0f, f.roll());
}

void test_wrap_degrees()
{
    TEST_ASSERT_FLOAT_WITHIN (0.001f, -170.0f, wrapDegrees (190.0f));
    TEST_ASSERT_FLOAT_WITHIN (0.001f, 170.0f, wrapDegrees (-190.0f));
    TEST_ASSERT_FLOAT_WITHIN (0.001f, 10.0f, wrapDegrees (370.0f));
}

// ------------------------------------------------------------ Packet ring

void test_packet_ring_keeps_boundaries_and_drops_when_full()
{
    PacketRing<4, 8> ring;
    uint8_t out[8];
    TEST_ASSERT_EQUAL (0, ring.pop (out));
    const uint8_t a[] = { 1, 2, 3 };
    const uint8_t b[] = { 4 };
    TEST_ASSERT_TRUE (ring.push (a, 3));
    TEST_ASSERT_TRUE (ring.push (b, 1));
    TEST_ASSERT_EQUAL (3, ring.pop (out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (a, out, 3);
    TEST_ASSERT_EQUAL (1, ring.pop (out));
    TEST_ASSERT_EQUAL (4, out[0]);
    for (int i = 0; i < 4; ++i)
        TEST_ASSERT_TRUE (ring.push (a, 3));
    TEST_ASSERT_FALSE (ring.push (a, 3));
    const uint8_t big[9] = {};
    TEST_ASSERT_FALSE (ring.push (big, 9));
    TEST_ASSERT_FALSE (ring.push (big, 0));
    TEST_ASSERT_EQUAL (3u, ring.droppedCount());
    ring.clear();
    TEST_ASSERT_EQUAL (0, ring.pop (out));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST (test_full_step_counts_one_per_cycle);
    RUN_TEST (test_full_step_ignores_bounce_and_invalid_jumps);
    RUN_TEST (test_half_step_counts_two_per_cycle);
    RUN_TEST (test_acceleration_curve);
    RUN_TEST (test_acceleration_builds_on_fast_spins_and_resets);
    RUN_TEST (test_relative_cc_values);
    RUN_TEST (test_debounce_needs_a_stable_level);
    RUN_TEST (test_millivolts_to_position);
    RUN_TEST (test_quantiser_does_not_send_its_first_reading);
    RUN_TEST (test_quantiser_hysteresis_and_dead_band);
    RUN_TEST (test_quantiser_slow_sweep_sends_every_step);
    RUN_TEST (test_quantiser_reaches_the_ends_whatever_the_dead_band);
    RUN_TEST (test_quantiser_rate_limit);
    RUN_TEST (test_angle_to_position);
    RUN_TEST (test_tilt_from_gravity);
    RUN_TEST (test_tilt_filter_follows_gyro_and_settles_on_gravity);
    RUN_TEST (test_wrap_degrees);
    RUN_TEST (test_packet_ring_keeps_boundaries_and_drops_when_full);
    return UNITY_END();
}
