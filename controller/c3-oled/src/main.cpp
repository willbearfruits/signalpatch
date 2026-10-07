// SignalPatch C3 handheld controller: an ESP32-C3 with a 0.42" OLED, three
// pots, an encoder, three buttons, an expression jack and an MPU-6050, over
// Bluetooth LE MIDI. The behaviour is in core/Controller; this file reads the
// hardware, moves BLE-MIDI packets and paces the display.

#include "BleMidiPort.h"
#include "Display.h"
#include "Inputs.h"
#include "Mpu6050.h"
#include "Pins.h"
#include "SettingsStore.h"
#include "core/BleMidi.h"
#include "core/Controller.h"

#include <Arduino.h>
#include <Wire.h>

namespace
{
constexpr uint32_t kAnalogIntervalMs = 4;
constexpr int kAnalogSamples = 4;
constexpr uint32_t kImuIntervalMs = 10;
constexpr int kImuFailuresBeforeMissing = 25;

BleMidiPort ble;
Display display;
Mpu6050 mpu;
bool imuPresent = false;
int imuFailures = 0;
uint32_t lastAnalogMs = 0;
uint32_t lastImuMs = 0;
uint32_t lastImuMicros = 0;
c3::Debouncer debouncers[inputs::kButtonCount] = { c3::Debouncer (pins::kDebounceMs), c3::Debouncer (pins::kDebounceMs),
                                                   c3::Debouncer (pins::kDebounceMs), c3::Debouncer (pins::kDebounceMs) };

void queuePacket (void*, const uint8_t* packet, size_t length)
{
    ble.queue (packet, length);
}

c3::blemidi::Encoder midiEncoder (&queuePacket, nullptr);

class Host : public c3::ControllerHost
{
public:
    void sendMidi (const uint8_t* message, size_t length) override
    {
        if (ble.canSend())
            midiEncoder.add (message, length, millis());
    }

    void saveSettings (const c3::Settings& settings) override { settings_store::save (settings); }
};

Host host;
c3::Controller* controller = nullptr;

void receiveMessage (void*, const uint8_t* message, size_t length)
{
    controller->midiIn (message, length, millis());
}

uint8_t sysexBuffer[256];
c3::blemidi::Decoder midiDecoder (sysexBuffer, sizeof (sysexBuffer), &receiveMessage, nullptr);

c3::screen::State screenState;
c3::screen::DrawList drawList;

void setLed (bool on)
{
    digitalWrite (pins::kLed, (on != pins::kLedActiveLow) ? HIGH : LOW);
}

void handleBleEvents (uint32_t now)
{
    BleMidiPort::Event event;
    while (ble.pollEvent (event))
    {
        switch (event)
        {
            case BleMidiPort::Event::connected:
                midiDecoder.reset();
                midiEncoder.clear();
                controller->connected (now);
                Serial.println ("ble: connected");
                break;
            case BleMidiPort::Event::disconnected:
                midiDecoder.reset();
                midiEncoder.clear();
                ble.dropQueued();
                controller->disconnected (now);
                Serial.println ("ble: disconnected");
                break;
            case BleMidiPort::Event::subscribed:
                controller->subscribed (true, now);
                Serial.println ("ble: notifications on");
                break;
            case BleMidiPort::Event::unsubscribed:
                controller->subscribed (false, now);
                Serial.println ("ble: notifications off");
                break;
        }
    }
    if (midiEncoder.packetSize() != ble.packetSize())
        midiEncoder.setPacketSize (ble.packetSize()); // after the MTU exchange
}

void readImu (uint32_t now)
{
    if (! imuPresent || now - lastImuMs < kImuIntervalMs)
        return;
    lastImuMs = now;
    c3::ImuSample sample;
    if (! mpu.read (sample))
    {
        if (++imuFailures >= kImuFailuresBeforeMissing)
        {
            imuPresent = false;
            controller->setGyroPresent (false);
            Serial.println ("mpu6050: stopped answering");
        }
        return;
    }
    imuFailures = 0;
    const uint32_t micro = micros();
    const float dt = lastImuMicros == 0 ? 0.0f : static_cast<float> (micro - lastImuMicros) * 1e-6f;
    lastImuMicros = micro;
    controller->imu (sample, dt, now);
}

void updateLed (uint32_t now)
{
    if (! controller->linkConnected())
        setLed ((now / 500) % 2 == 0); // blinking: waiting to be paired / connected
    else
        setLed (! controller->appConnected()); // steady: connected, waiting for SignalPatch
}

void updateDisplay (uint32_t now)
{
    const c3::Settings& settings = controller->settings();
    display.configure (settings.get (c3::kBrightness), settings.flag (c3::kUpsideDown), controller->displayAwake (now));
    display.service();
    if (! display.readyForFrame (now))
        return;
    controller->screenState (now, screenState);
    c3::screen::layout (screenState, drawList);
    display.show (drawList, now);
}
} // namespace

void setup()
{
    Serial.setTxTimeoutMs (0); // never wait for a USB host that is not reading
    Serial.begin (115200);

    pinMode (pins::kLed, OUTPUT);
    setLed (false);
    inputs::begin();
    delay (5);
    const bool forgetPairings = inputs::buttonDown (3); // encoder push held at power-up

    static c3::Controller instance (host, settings_store::load(),
                                    c3::ControllerOptions { pins::kGyroSwapTiltRoll, pins::kGyroInvertTilt, pins::kGyroInvertRoll });
    controller = &instance;
    const c3::Settings& settings = controller->settings();

    if (! display.begin())
        Serial.println ("display: no answer at 0x3C");
    Wire.setClock (pins::kI2cHz);
    Wire.setTimeOut (20);

    imuPresent = mpu.begin (Wire, pins::kMpuAddress);
    controller->setGyroPresent (imuPresent);
    if (imuPresent)
        Serial.printf ("mpu6050: found, WHO_AM_I 0x%02X\n", mpu.whoAmI());
    else
        Serial.println ("mpu6050: not found");

    char name[20] = "SignalPatch C3";
    const uint8_t number = settings.get (c3::kNameNumber);
    if (number > 0)
    {
        name[14] = ' ';
        name[15] = static_cast<char> ('0' + number);
        name[16] = '\0';
    }
    ble.begin (name);
    midiEncoder.setPacketSize (ble.packetSize());
    Serial.printf ("ble: advertising as \"%s\"\n", name);

    if (forgetPairings)
    {
        BleMidiPort::forgetBonds();
        display.configure (settings.get (c3::kBrightness), settings.flag (c3::kUpsideDown), true);
        display.message ("Pairings", "forgotten");
        Serial.println ("ble: pairings forgotten");
        while (inputs::buttonDown (3))
            delay (10); // the release must not count as a press
        delay (1000);
    }

    for (int i = 0; i < inputs::kButtonCount; ++i)
        debouncers[i].prime (inputs::buttonDown (i));
}

void loop()
{
    const uint32_t now = millis();

    handleBleEvents (now);

    uint8_t packet[BleMidiPort::kMaxPacket];
    for (size_t length = ble.pollPacket (packet); length > 0; length = ble.pollPacket (packet))
        midiDecoder.decodePacket (packet, length);

    for (int i = 0; i < inputs::kButtonCount; ++i)
        if (debouncers[i].update (inputs::buttonDown (i), now))
            controller->button (i, debouncers[i].level(), now);

    if (const int detents = inputs::takeEncoderDetents())
        controller->encoder (detents, now);

    if (now - lastAnalogMs >= kAnalogIntervalMs)
    {
        lastAnalogMs = now;
        for (int i = 0; i < inputs::kAnalogCount; ++i)
            controller->analog (i, inputs::readMillivolts (i, kAnalogSamples), now);
    }

    readImu (now);
    controller->tick (now);

    midiEncoder.flush();
    ble.pump();

    updateLed (now);
    updateDisplay (now);

    vTaskDelay (1); // ~1 kHz loop; lets the idle task run
}
