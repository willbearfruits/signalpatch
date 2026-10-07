#include "Controller.h"

#include <cstdlib>
#include <cstring>

namespace c3
{
namespace
{
    // Small fixed-buffer string building (no printf on the hot path, no truncation warnings).
    class Text
    {
    public:
        Text (char* buffer, size_t capacity) : out (buffer), cap (capacity)
        {
            if (cap > 0)
                out[0] = '\0';
        }

        Text& add (const char* s)
        {
            while (*s != '\0')
                add (*s++);
            return *this;
        }

        Text& add (char c)
        {
            if (n + 1 < cap)
            {
                out[n++] = c;
                out[n] = '\0';
            }
            return *this;
        }

        /** Decimal, right-aligned to `width` with spaces; `sign` forces a leading + for positives. */
        Text& number (int value, int width = 0, bool sign = false)
        {
            char digits[12];
            int count = 0;
            unsigned magnitude = value < 0 ? static_cast<unsigned> (-value) : static_cast<unsigned> (value);
            do
            {
                digits[count++] = static_cast<char> ('0' + magnitude % 10);
                magnitude /= 10;
            } while (magnitude > 0 && count < 10);
            const char signChar = value < 0 ? '-' : (sign ? '+' : '\0');
            const int length = count + (signChar != '\0' ? 1 : 0);
            for (int pad = length; pad < width; ++pad)
                add (' ');
            if (signChar != '\0')
                add (signChar);
            while (count > 0)
                add (digits[--count]);
            return *this;
        }

    private:
        char* out;
        size_t cap;
        size_t n = 0;
    };

    void copyString (char* out, size_t capacity, const char* in)
    {
        Text (out, capacity).add (in);
    }

    Control controlForButton (int index)
    {
        switch (index)
        {
            case 0: return Control::button1;
            case 1: return Control::button2;
            case 2: return Control::button3;
            default: return Control::push;
        }
    }
} // namespace

const char* controlName (Control control)
{
    switch (control)
    {
        case Control::pot1: return "Pot 1";
        case Control::pot2: return "Pot 2";
        case Control::pot3: return "Pot 3";
        case Control::expression: return "Expression";
        case Control::tilt: return "Tilt";
        case Control::roll: return "Roll";
        case Control::encoder: return "Encoder";
        case Control::button1: return "Button 1";
        case Control::button2: return "Button 2";
        case Control::button3: return "Button 3";
        case Control::push: return "Push";
    }
    return "";
}

Controller::Controller (ControllerHost& hostRef, const Settings& initial, ControllerOptions opts)
    : host (hostRef), config (initial), options (opts), bootNameNumber (initial.get (kNameNumber))
{
    for (auto& q : analogQuantiser)
        q.reset (false); // pots report movement, not their position at power-up
    for (auto& q : gyroQuantiser)
        q.reset (true);
}

// ------------------------------------------------------------ MIDI out

uint8_t Controller::channelStatus (uint8_t type) const
{
    return static_cast<uint8_t> (type | ((config.midiChannel() - 1) & 0x0F));
}

void Controller::sendCc (uint8_t cc, uint8_t value)
{
    const uint8_t message[3] = { channelStatus (0xB0), static_cast<uint8_t> (cc & 0x7F), static_cast<uint8_t> (value & 0x7F) };
    host.sendMidi (message, 3);
}

void Controller::pressNote (int index, uint32_t now)
{
    if (held[index].on)
        return;
    const uint8_t note = config.get (static_cast<uint8_t> (kButton1Note + index));
    const uint8_t message[3] = { channelStatus (0x90), note, 127 };
    host.sendMidi (message, 3);
    held[index] = { true, message[0], note };
    showNote (controlForButton (index), note, true, now);
}

void Controller::releaseNote (int index, uint32_t now)
{
    if (! held[index].on)
        return;
    // Note Off on the channel and number the Note On used, even if a setting changed since.
    const uint8_t message[3] = { static_cast<uint8_t> (0x80 | (held[index].status & 0x0F)), held[index].note, 0 };
    host.sendMidi (message, 3);
    held[index].on = false;
    showNote (controlForButton (index), message[1], false, now);
}

void Controller::sendHello (uint32_t now)
{
    uint8_t message[proto::kHelloSize];
    const size_t length = proto::buildHello (message, sizeof (message));
    host.sendMidi (message, length);
    helloSent = true;
    helloMs = now;
}

void Controller::sendSettingsDump()
{
    uint8_t message[proto::kSettingsDumpSize];
    const size_t length = proto::buildSettingsDump (config, message, sizeof (message));
    host.sendMidi (message, length);
}

// ------------------------------------------------------------ Link

void Controller::connected (uint32_t now)
{
    linkUp = true;
    notifying = false;
    appSeen = false;
    helloSent = false;
    probing = false;
    fb.clear();
    lastActivityMs = now;
}

void Controller::subscribed (bool on, uint32_t now)
{
    notifying = on;
    appSeen = false; // a new subscriber is a new session: hello again
    helloSent = false;
    probing = false;
    fb.clear();
    lastActivityMs = now;
}

void Controller::disconnected (uint32_t now)
{
    linkUp = false;
    notifying = false;
    appSeen = false;
    helloSent = false;
    probing = false;
    fb.clear();
    for (auto& h : held)
        h.on = false;
    pushDown = false;
    pushLong = false;
    overlay.active = false;
    lastActivityMs = now;
}

void Controller::midiIn (const uint8_t* message, size_t length, uint32_t now)
{
    if (message == nullptr || length == 0 || message[0] != 0xF0)
        return; // the controller only listens to SysEx

    const auto parsed = proto::handleSysEx (message, length, fb);
    if (parsed.kind == proto::Kind::ignored)
        return;
    lastAppMs = now;
    probing = false;

    switch (parsed.kind)
    {
        case proto::Kind::appHello:
            if (! appSeen)
            {
                fb.clear(); // the full state follows the hello
                appSeen = true;
                lastActivityMs = now;
            }
            break;
        case proto::Kind::settingsRequest:
            sendSettingsDump();
            break;
        case proto::Kind::settingsChange:
        {
            const Settings before = config;
            proto::applySettingsChange (parsed.payload, parsed.payloadLength, config);
            applyEffects (before, now);
            if (config != before)
                host.saveSettings (config);
            sendSettingsDump();
            break;
        }
        case proto::Kind::settingsReset:
        {
            const Settings before = config;
            config.resetToDefaults();
            applyEffects (before, now);
            if (config != before)
                host.saveSettings (config);
            sendSettingsDump();
            break;
        }
        case proto::Kind::gyroZero:
            zeroPending = true;
            break;
        case proto::Kind::feedback:
        case proto::Kind::ignored:
            break;
    }
}

void Controller::applyEffects (const Settings& before, uint32_t now)
{
    (void) now;
    if (before.gyroMode() != config.gyroMode())
    {
        pushDown = false;
        pushLong = false;
        setGyro (config.gyroMode() == GyroMode::alwaysOn);
    }
    if (before.get (kExpressionOn) != config.get (kExpressionOn)
        || before.get (kExpressionReversed) != config.get (kExpressionReversed))
    {
        analogSmooth[3].reset();
        analogQuantiser[3].reset (false);
    }
}

// ------------------------------------------------------------ Inputs

void Controller::touched (uint32_t now)
{
    lastActivityMs = now;
    // The app may have restarted while the link stayed up (BlueZ keeps it):
    // after a long silence, ask again. No answer within 3 s → generic screen.
    if (linkUp && notifying && appSeen && ! probing && now - lastAppMs >= kAppQuietMs)
    {
        sendHello (now);
        probing = true;
        probeMs = now;
    }
}

void Controller::button (int index, bool pressed, uint32_t now)
{
    if (index < 0 || index >= kButtons)
        return;
    touched (now);
    const GyroMode mode = config.gyroMode();

    if (index == 2 && mode == GyroMode::whileButton3Held)
    {
        setGyro (pressed); // button 3 only holds the gyro on; no note
        if (pressed && ! imuPresent)
            showMessage ("Gyro", "missing", now);
        if (! pressed)
            releaseNote (2, now); // a note held from before the mode changed
        return;
    }

    if (index == 3 && mode == GyroMode::pushToggles)
    {
        if (pressed)
        {
            pushDown = true;
            pushLong = false;
            pushDownMs = now;
            return;
        }
        if (pushDown && ! pushLong)
        {
            pressNote (3, now); // a short press sends its note on release
            releaseNote (3, now);
        }
        else
            releaseNote (3, now);
        pushDown = false;
        pushLong = false;
        return;
    }

    if (pressed)
        pressNote (index, now);
    else
        releaseNote (index, now);
}

void Controller::encoder (int detents, uint32_t now)
{
    if (detents == 0)
        return;
    touched (now);
    int direction = detents > 0 ? 1 : -1;
    const int count = detents > 0 ? detents : -detents;
    if (config.flag (kEncoderReversed))
        direction = -direction;
    const int step = acceleration.step (direction, count, now, config.flag (kEncoderAcceleration));
    const uint8_t cc = config.get (kEncoderCc);
    const uint8_t value = relativeCcValue (direction, step);
    for (int i = 0; i < count; ++i)
        sendCc (cc, value); // one message per detent
    showCc (Control::encoder, cc, value, direction * step, true, now);
}

void Controller::analog (int index, int millivolts, uint32_t now)
{
    if (index < 0 || index >= kAnalogInputs)
        return;
    float position = millivoltsToPosition (millivolts);
    if (index == 3)
    {
        if (! config.flag (kExpressionOn))
            return;
        if (config.flag (kExpressionReversed))
            position = 127.0f - position;
    }
    const float smoothed = analogSmooth[index].update (position, kAnalogSmoothing);
    const int value = analogQuantiser[index].update (smoothed, config.get (kPotDeadBand), now, kCcIntervalMs);
    if (value < 0)
        return;
    const uint8_t cc = config.get (index == 3 ? static_cast<uint8_t> (kExpressionCc) : static_cast<uint8_t> (kPot1Cc + index));
    sendCc (cc, static_cast<uint8_t> (value));
    touched (now);
    const Control control = index == 3 ? Control::expression : static_cast<Control> (index);
    showCc (control, cc, static_cast<uint8_t> (value), 0, false, now);
}

void Controller::imu (const ImuSample& sample, float dtSeconds, uint32_t now)
{
    tiltFilter.update (sample, dtSeconds);
    if (! gyroOn || ! imuPresent)
        return;
    if (zeroPending)
    {
        zeroPitch = tiltFilter.pitch();
        zeroRoll = tiltFilter.roll();
        zeroPending = false;
        for (auto& s : gyroSmooth)
            s.reset();
        for (auto& q : gyroQuantiser)
            q.reset (true); // the zero point (64) goes out at once
    }
    float angles[2] = { wrapDegrees (tiltFilter.pitch() - zeroPitch), wrapDegrees (tiltFilter.roll() - zeroRoll) };
    if (options.swapTiltRoll)
    {
        const float t = angles[0];
        angles[0] = angles[1];
        angles[1] = t;
    }
    if (options.invertTilt)
        angles[0] = -angles[0];
    if (options.invertRoll)
        angles[1] = -angles[1];

    const float coefficient = 1.0f / (1.0f + static_cast<float> (config.get (kGyroSmoothing)));
    const float range = static_cast<float> (config.get (kGyroRange));
    for (int axis = 0; axis < 2; ++axis)
    {
        const float position = angleToPosition (gyroSmooth[axis].update (angles[axis], coefficient), range);
        const int value = gyroQuantiser[axis].update (position, 0, now, kCcIntervalMs);
        if (value < 0)
            continue;
        const uint8_t cc = config.get (axis == 0 ? static_cast<uint8_t> (kGyroTiltCc) : static_cast<uint8_t> (kGyroRollCc));
        sendCc (cc, static_cast<uint8_t> (value));
        gyroOverlay (axis, cc, static_cast<uint8_t> (value), now);
    }
}

void Controller::setGyroPresent (bool present)
{
    imuPresent = present;
    if (! present)
        gyroOn = false;
    else if (config.gyroMode() == GyroMode::alwaysOn)
        setGyro (true);
}

void Controller::setGyro (bool on)
{
    if (on && ! imuPresent)
        on = false;
    if (on == gyroOn)
        return;
    gyroOn = on;
    if (on)
    {
        zeroPending = true; // zeroed at its current angle whenever it is switched on
        gyroOverlayValue[0] = gyroOverlayValue[1] = 64;
    }
}

void Controller::toggleGyro (uint32_t now)
{
    if (! imuPresent)
    {
        showMessage ("Gyro", "missing", now);
        return;
    }
    setGyro (! gyroOn);
    showMessage ("Gyro", gyroOn ? "on" : "off", now);
}

void Controller::tick (uint32_t now)
{
    if (pushDown && ! pushLong && now - pushDownMs >= kLongPressMs)
    {
        pushLong = true;
        if (config.gyroMode() == GyroMode::pushToggles)
            toggleGyro (now);
    }

    if (linkUp && notifying)
    {
        if (! appSeen)
        {
            if (! helloSent || now - helloMs >= kHelloRepeatMs)
                sendHello (now);
        }
        else if (probing && now - probeMs >= kHelloRepeatMs)
        {
            probing = false; // nobody answered: the app is gone
            appSeen = false;
            fb.clear();
        }
    }
}

// ------------------------------------------------------------ Overlay

bool Controller::overlayVisible (uint32_t now) const
{
    return overlay.active && now - overlay.shownMs < kOverlayMs;
}

void Controller::showCc (Control control, uint8_t cc, uint8_t value, int step, bool relative, uint32_t now)
{
    const bool continuing = overlayVisible (now) && overlay.kind == OverlayKind::cc && overlay.control == control
                            && overlay.number == cc;
    if (! continuing)
        overlay.stampAtStart = fb.stamp;
    overlay.active = true;
    overlay.kind = OverlayKind::cc;
    overlay.control = control;
    overlay.number = cc;
    overlay.value = value;
    overlay.step = static_cast<int8_t> (step);
    overlay.relative = relative;
    overlay.shownMs = now;
}

void Controller::showNote (Control control, uint8_t note, bool on, uint32_t now)
{
    if (appSeen)
        return; // the home screen shows the buttons
    overlay.active = true;
    overlay.kind = OverlayKind::note;
    overlay.control = control;
    overlay.number = note;
    overlay.noteOn = on;
    overlay.shownMs = now;
}

void Controller::showMessage (const char* title, const char* text, uint32_t now)
{
    overlay.active = true;
    overlay.kind = OverlayKind::message;
    copyString (overlay.title, sizeof (overlay.title), title);
    copyString (overlay.text, sizeof (overlay.text), text);
    overlay.shownMs = now;
}

void Controller::gyroOverlay (int axis, uint8_t cc, uint8_t value, uint32_t now)
{
    // The gyro is never quite still: it takes the overlay only for a real
    // move, and never snatches it from a control touched a moment ago.
    const Control control = axis == 0 ? Control::tilt : Control::roll;
    const bool showing = overlayVisible (now) && overlay.kind == OverlayKind::cc && overlay.control == control;
    const int moved = std::abs (static_cast<int> (value) - static_cast<int> (gyroOverlayValue[axis]));
    const bool free = ! overlayVisible (now) || now - overlay.shownMs >= kGyroTakeOverMs;
    if (showing || (free && moved >= 2))
    {
        showCc (control, cc, value, 0, false, now);
        gyroOverlayValue[axis] = value;
        touched (now);
    }
}

// ------------------------------------------------------------ Screen

bool Controller::displayAwake (uint32_t now) const
{
    const uint32_t minutes = config.get (kSleepMinutes);
    return minutes == 0 || now - lastActivityMs < minutes * 60000u;
}

void Controller::screenState (uint32_t now, screen::State& out) const
{
    out = screen::State {};
    out.link = ! linkUp ? screen::Link::advertising : (appSeen ? screen::Link::app : screen::Link::connected);
    out.nameNumber = bootNameNumber;
    out.blinkFast = (now / 250) % 2 == 0;
    out.blinkSlow = (now / 500) % 2 == 0;

    copyString (out.rig, sizeof (out.rig), fb.rig);
    out.slot = fb.activeSlot;
    out.muted = fb.muted;
    for (int i = 0; i < 4; ++i)
    {
        out.led[i] = fb.led[i];
        copyString (out.label[i], sizeof (out.label[i]), fb.label[i]);
    }

    const GyroMode mode = config.gyroMode();
    out.gyroOn = gyroOn;
    out.gyroMissing = mode != GyroMode::off && ! imuPresent;
    out.button3HoldsGyro = mode == GyroMode::whileButton3Held;

    if (! linkUp || ! overlayVisible (now))
        return;

    auto& o = out.overlay;
    o.visible = true;
    switch (overlay.kind)
    {
        case OverlayKind::message:
            copyString (o.title, sizeof (o.title), overlay.title);
            copyString (o.value, sizeof (o.value), overlay.text);
            return;

        case OverlayKind::note:
        {
            copyString (o.title, sizeof (o.title), controlName (overlay.control));
            Text (o.value, sizeof (o.value)).add ('N').number (overlay.number).add (overlay.noteOn ? " on" : " off");
            return;
        }

        case OverlayKind::cc:
        {
            const auto& cc = fb.cc[overlay.number & 0x7F];
            if (! appSeen)
            {
                // No SignalPatch yet: plain "CC16  64".
                copyString (o.title, sizeof (o.title), controlName (overlay.control));
                Text text (o.value, sizeof (o.value));
                text.add ("CC").number (overlay.number).add (' ');
                if (overlay.relative)
                    text.number (overlay.step, 3, true);
                else
                    text.number (overlay.value, 3);
                o.bar = ! overlay.relative;
                o.position = overlay.value;
                return;
            }
            copyString (o.title, sizeof (o.title), cc.label[0] != '\0' ? cc.label : controlName (overlay.control));
            const bool fresh = cc.valueStamp != 0 && cc.valueStamp > overlay.stampAtStart;
            if (fresh)
            {
                if (cc.value[0] != '\0')
                    copyString (o.value, sizeof (o.value), cc.value);
                else
                    Text (o.value, sizeof (o.value)).number (cc.position);
                o.bar = true;
                o.position = cc.position;
            }
            else if (overlay.relative)
                Text (o.value, sizeof (o.value)).number (overlay.step, 0, true);
            else
            {
                Text (o.value, sizeof (o.value)).number (overlay.value);
                o.bar = true;
                o.position = overlay.value;
            }
            return;
        }
    }
}
} // namespace c3
