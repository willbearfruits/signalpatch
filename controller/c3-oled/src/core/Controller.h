#pragma once

// The controller's behaviour, without hardware: what each input sends, the
// gyro modes, the hello handshake with SignalPatch, settings over SysEx, and
// what the screen should show. The firmware feeds it debounced buttons,
// encoder detents, ADC millivolts, IMU samples and BLE-MIDI messages, and it
// answers through ControllerHost.

#include "InputMath.h"
#include "Protocol.h"
#include "Screen.h"
#include "Settings.h"
#include "Tilt.h"

#include <cstddef>
#include <cstdint>

namespace c3
{
class ControllerHost
{
public:
    virtual ~ControllerHost() = default;
    /** One complete MIDI message (channel message or F0..F7). */
    virtual void sendMidi (const uint8_t* message, size_t length) = 0;
    virtual void saveSettings (const Settings& settings) = 0;
};

/** How the MPU-6050 sits in the case (from Pins.h). */
struct ControllerOptions
{
    bool swapTiltRoll = false;
    bool invertTilt = false;
    bool invertRoll = false;
};

enum class Control : uint8_t
{
    pot1, pot2, pot3, expression, tilt, roll, encoder, button1, button2, button3, push
};

const char* controlName (Control control);

class Controller
{
public:
    static constexpr int kButtons = 4;            // buttons 1-3, encoder push
    static constexpr int kAnalogInputs = 4;       // pots 1-3, expression
    static constexpr uint32_t kHelloRepeatMs = 3000;
    static constexpr uint32_t kAppQuietMs = 30000; // re-hello on use after this much silence from the app
    static constexpr uint32_t kLongPressMs = 600;
    static constexpr uint32_t kOverlayMs = 1500;
    static constexpr uint32_t kCcIntervalMs = 10;
    static constexpr uint32_t kGyroTakeOverMs = 300;
    static constexpr float kAnalogSmoothing = 0.35f;

    Controller (ControllerHost& host, const Settings& settings, ControllerOptions options = {});

    // Bluetooth link
    void connected (uint32_t now);
    void subscribed (bool on, uint32_t now);
    void disconnected (uint32_t now);
    /** A complete MIDI message from the app. */
    void midiIn (const uint8_t* message, size_t length, uint32_t now);

    // Inputs
    void button (int index, bool pressed, uint32_t now); // 0-2 buttons 1-3, 3 encoder push
    void encoder (int detents, uint32_t now);            // positive = clockwise
    void analog (int index, int millivolts, uint32_t now); // 0-2 pots, 3 expression
    void imu (const ImuSample& sample, float dtSeconds, uint32_t now);
    void setGyroPresent (bool present);

    void tick (uint32_t now);

    // For the display and the board
    void screenState (uint32_t now, screen::State& out) const;
    bool displayAwake (uint32_t now) const;
    const Settings& settings() const { return config; }
    const proto::Feedback& feedback() const { return fb; }
    bool linkConnected() const { return linkUp; }
    bool notifications() const { return notifying; }
    bool appConnected() const { return appSeen; }
    bool gyroActive() const { return gyroOn; }
    bool gyroPresent() const { return imuPresent; }

private:
    enum class OverlayKind : uint8_t { cc, note, message };

    struct Held
    {
        bool on = false;
        uint8_t status = 0; // the Note On's status byte (keeps its channel)
        uint8_t note = 0;
    };

    struct OverlayState
    {
        bool active = false;
        OverlayKind kind = OverlayKind::cc;
        Control control = Control::pot1;
        uint8_t number = 0; // CC or note
        uint8_t value = 0;
        int8_t step = 0;    // relative encoder step, signed
        bool relative = false;
        bool noteOn = false;
        uint32_t shownMs = 0;
        uint32_t stampAtStart = 0;
        char title[13] {};
        char text[11] {};
    };

    uint8_t channelStatus (uint8_t type) const;
    void sendCc (uint8_t cc, uint8_t value);
    void pressNote (int index, uint32_t now);
    void releaseNote (int index, uint32_t now);
    void sendHello (uint32_t now);
    void sendSettingsDump();
    void touched (uint32_t now);
    void applyEffects (const Settings& before, uint32_t now);
    void setGyro (bool on);
    void toggleGyro (uint32_t now);

    bool overlayVisible (uint32_t now) const;
    void showCc (Control control, uint8_t cc, uint8_t value, int step, bool relative, uint32_t now);
    void showNote (Control control, uint8_t note, bool on, uint32_t now);
    void showMessage (const char* title, const char* text, uint32_t now);
    void gyroOverlay (int axis, uint8_t cc, uint8_t value, uint32_t now);

    ControllerHost& host;
    Settings config;
    ControllerOptions options;
    uint8_t bootNameNumber;
    proto::Feedback fb;

    bool linkUp = false;
    bool notifying = false;
    bool appSeen = false;
    bool helloSent = false;
    uint32_t helloMs = 0;
    uint32_t lastAppMs = 0;
    bool probing = false;
    uint32_t probeMs = 0;
    uint32_t lastActivityMs = 0;

    Held held[kButtons];
    bool pushDown = false;
    bool pushLong = false;
    uint32_t pushDownMs = 0;

    EncoderAcceleration acceleration;

    Smoother analogSmooth[kAnalogInputs];
    CcQuantiser analogQuantiser[kAnalogInputs];

    TiltFilter tiltFilter;
    bool imuPresent = false;
    bool gyroOn = false;
    bool zeroPending = false;
    float zeroPitch = 0;
    float zeroRoll = 0;
    Smoother gyroSmooth[2];
    CcQuantiser gyroQuantiser[2];
    uint8_t gyroOverlayValue[2] = { 64, 64 };

    OverlayState overlay;
};
} // namespace c3
