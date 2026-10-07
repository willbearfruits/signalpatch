#pragma once

// Every pin and wiring choice of the SignalPatch C3 controller, in one place.
// Board: ESP32-C3 SuperMini style with a 0.42" 72x40 SSD1306 OLED.

#include <stdint.h>

namespace pins
{
// I2C bus shared by the OLED (0x3C) and the MPU-6050 (0x68).
constexpr int kI2cSda = 5;
constexpr int kI2cScl = 6;
constexpr uint32_t kI2cHz = 400000;
constexpr uint8_t kOledAddress = 0x3C;
constexpr uint8_t kMpuAddress = 0x68; // AD0 low; 0x69 with AD0 high

// Onboard LED, lit when the pin is low.
constexpr int kLed = 8;
constexpr bool kLedActiveLow = true;

// Pot wipers and the expression jack tip, all on ADC1.
// Read at 12 dB attenuation (about 0..2.5 V); 2450 mV and above is CC 127.
constexpr int kPot1 = 0;
constexpr int kPot2 = 1;
constexpr int kPot3 = 3;
constexpr int kExpression = 4; // 100k pull-down to GND so an empty jack reads 0
constexpr int kAnalogFullScaleMv = 2450;

// Rotary encoder (common pin to GND, internal pull-ups).
constexpr int kEncoderA = 7;
constexpr int kEncoderB = 10;
constexpr int kEncoderPush = 20;
// false: one detent per full quadrature cycle (most EC11 parts).
// true: one detent per half cycle (encoders that rest at both 00 and 11).
constexpr bool kEncoderHalfStep = false;

// Buttons to GND with internal pull-ups.
// GPIO2 and GPIO9 are strapping pins: do not hold buttons 2 and 3 at power-up.
// Button 3 shares GPIO9 with the BOOT button; held at power-up it enters the bootloader.
constexpr int kButton1 = 21;
constexpr int kButton2 = 2;
constexpr int kButton3 = 9;

constexpr uint32_t kDebounceMs = 5;

// How the MPU-6050 sits in the case. Tilt (pitch) turns about the board's
// Y axis, roll about its X axis, with the chip face up.
constexpr bool kGyroSwapTiltRoll = false;
constexpr bool kGyroInvertTilt = false;
constexpr bool kGyroInvertRoll = false;
} // namespace pins
