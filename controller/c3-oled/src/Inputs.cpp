#include "Inputs.h"

#include "Pins.h"
#include "core/InputMath.h"

#include <Arduino.h>

namespace
{
const int buttonPins[inputs::kButtonCount] = { pins::kButton1, pins::kButton2, pins::kButton3, pins::kEncoderPush };
const int analogPins[inputs::kAnalogCount] = { pins::kPot1, pins::kPot2, pins::kPot3, pins::kExpression };

c3::QuadratureDecoder decoder (pins::kEncoderHalfStep);
volatile int32_t detentCount = 0; // written only by the interrupt
int32_t detentsTaken = 0;

// Runs on every edge of A or B. The decoder reads the levels as they are
// now, so bounce and edges missed while flash is busy cannot add false steps.
void encoderEdge()
{
    const int step = decoder.update (digitalRead (pins::kEncoderA) != 0, digitalRead (pins::kEncoderB) != 0);
    if (step != 0)
        detentCount = detentCount + step;
}
} // namespace

namespace inputs
{
void begin()
{
    for (const int pin : buttonPins)
        pinMode (pin, INPUT_PULLUP);

    pinMode (pins::kEncoderA, INPUT_PULLUP);
    pinMode (pins::kEncoderB, INPUT_PULLUP);
    attachInterrupt (digitalPinToInterrupt (pins::kEncoderA), encoderEdge, CHANGE);
    attachInterrupt (digitalPinToInterrupt (pins::kEncoderB), encoderEdge, CHANGE);

    analogReadResolution (12);
    for (const int pin : analogPins)
        analogSetPinAttenuation (pin, ADC_11db); // about 0..2.5 V on the C3
}

bool buttonDown (int index)
{
    if (index < 0 || index >= kButtonCount)
        return false;
    return digitalRead (buttonPins[index]) == LOW;
}

int takeEncoderDetents()
{
    const int32_t count = detentCount; // a 32-bit read is atomic on the C3
    const int32_t delta = count - detentsTaken;
    detentsTaken = count;
    return static_cast<int> (delta);
}

int readMillivolts (int index, int samples)
{
    if (index < 0 || index >= kAnalogCount || samples < 1)
        return 0;
    int32_t sum = 0;
    for (int i = 0; i < samples; ++i)
        sum += static_cast<int32_t> (analogReadMilliVolts (analogPins[index]));
    return static_cast<int> (sum / samples);
}
} // namespace inputs
