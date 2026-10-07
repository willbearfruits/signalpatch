#pragma once

// Minimal MPU-6050 driver: wake it, set its ranges and filter, read
// acceleration and rotation in one burst.

#include "core/Tilt.h"

#include <Wire.h>

class Mpu6050
{
public:
    /** Probes and configures the chip. Returns false when nothing answers at `address`. */
    bool begin (TwoWire& wire, uint8_t i2cAddress);

    /** Reads one sample (g and degrees per second). Returns false on a bus error. */
    bool read (c3::ImuSample& sample);

    uint8_t whoAmI() const { return identity; }

private:
    bool write (uint8_t reg, uint8_t value);

    TwoWire* bus = nullptr;
    uint8_t address = 0x68;
    uint8_t identity = 0;
};
