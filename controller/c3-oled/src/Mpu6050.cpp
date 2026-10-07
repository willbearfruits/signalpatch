#include "Mpu6050.h"

namespace
{
constexpr uint8_t kSampleRateDivider = 0x19;
constexpr uint8_t kConfig = 0x1A;
constexpr uint8_t kGyroConfig = 0x1B;
constexpr uint8_t kAccelConfig = 0x1C;
constexpr uint8_t kAccelOut = 0x3B; // 14 bytes: accel XYZ, temperature, gyro XYZ (big endian)
constexpr uint8_t kPowerManagement1 = 0x6B;
constexpr uint8_t kWhoAmI = 0x75;

constexpr float kAccelPerG = 8192.0f;     // ±4 g
constexpr float kGyroPerDegree = 65.5f;   // ±500 °/s

int16_t word (const uint8_t* p)
{
    return static_cast<int16_t> ((p[0] << 8) | p[1]);
}
} // namespace

bool Mpu6050::write (uint8_t reg, uint8_t value)
{
    bus->beginTransmission (address);
    bus->write (reg);
    bus->write (value);
    return bus->endTransmission() == 0;
}

bool Mpu6050::begin (TwoWire& wire, uint8_t i2cAddress)
{
    bus = &wire;
    address = i2cAddress;

    bus->beginTransmission (address);
    if (bus->endTransmission() != 0)
        return false; // nothing at this address

    bus->beginTransmission (address);
    bus->write (kWhoAmI);
    if (bus->endTransmission (false) != 0 || bus->requestFrom (address, static_cast<uint8_t> (1)) != 1)
        return false;
    identity = static_cast<uint8_t> (bus->read());
    // 0x68 is an MPU-6050; GY-521 clones also carry MPU-6500 family parts
    // (0x70, 0x71, 0x73...) with the same registers for what is used here.
    if (identity == 0x00 || identity == 0xFF)
        return false;

    return write (kPowerManagement1, 0x01)      // awake, clock from the X gyro PLL
           && write (kSampleRateDivider, 4)      // 200 Hz
           && write (kConfig, 0x03)              // ~44 Hz low-pass
           && write (kGyroConfig, 0x08)          // ±500 °/s
           && write (kAccelConfig, 0x08);        // ±4 g
}

bool Mpu6050::read (c3::ImuSample& sample)
{
    if (bus == nullptr)
        return false;
    bus->beginTransmission (address);
    bus->write (kAccelOut);
    if (bus->endTransmission (false) != 0)
        return false;
    if (bus->requestFrom (address, static_cast<uint8_t> (14)) != 14)
        return false;
    uint8_t raw[14];
    for (auto& b : raw)
        b = static_cast<uint8_t> (bus->read());
    sample.ax = static_cast<float> (word (raw + 0)) / kAccelPerG;
    sample.ay = static_cast<float> (word (raw + 2)) / kAccelPerG;
    sample.az = static_cast<float> (word (raw + 4)) / kAccelPerG;
    sample.gx = static_cast<float> (word (raw + 8)) / kGyroPerDegree;
    sample.gy = static_cast<float> (word (raw + 10)) / kGyroPerDegree;
    sample.gz = static_cast<float> (word (raw + 12)) / kGyroPerDegree;
    return true;
}
