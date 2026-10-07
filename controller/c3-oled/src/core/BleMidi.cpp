#include "BleMidi.h"

namespace c3::blemidi
{
size_t messageLength (uint8_t status)
{
    if (status < 0x80)
        return 0;
    if (status < 0xC0 || (status >= 0xE0 && status < 0xF0))
        return 3; // note off/on, poly pressure, CC, pitch bend
    if (status < 0xE0)
        return 2; // program change, channel pressure
    switch (status)
    {
        case 0xF0: return 0; // SysEx: variable
        case 0xF1: return 2; // MTC quarter frame
        case 0xF2: return 3; // song position
        case 0xF3: return 2; // song select
        case 0xF7: return 0; // EOX alone is not a message
        default:   return 1; // F4, F5, F6 and real-time F8-FF
    }
}

// ---------------------------------------------------------------- Decoder

Decoder::Decoder (uint8_t* sysexBuffer, size_t capacity, Callback cb, void* ctx)
    : sysex (sysexBuffer), sysexCapacity (capacity), callback (cb), context (ctx)
{
}

void Decoder::reset()
{
    state = State::waitTimestamp;
    running = 0;
    messageFill = 0;
    messageNeeded = 0;
    sysexActive = false;
    sysexLength = 0;
    sysexOverflow = false;
}

void Decoder::emit (const uint8_t* m, size_t length)
{
    if (callback != nullptr)
        callback (context, m, length);
}

void Decoder::sysexByte (uint8_t byte)
{
    if (sysexLength < sysexCapacity)
        sysex[sysexLength++] = byte;
    else
        sysexOverflow = true;
}

void Decoder::endSysEx()
{
    sysexActive = false;
    if (! sysexOverflow && sysexLength < sysexCapacity)
    {
        sysex[sysexLength++] = 0xF7;
        emit (sysex, sysexLength);
    }
    sysexLength = 0;
    sysexOverflow = false;
}

void Decoder::status (uint8_t byte)
{
    if (byte == 0xF0)
    {
        sysexActive = true;
        sysexLength = 0;
        sysexOverflow = false;
        running = 0;
        sysexByte (0xF0);
        return;
    }
    if (byte >= 0xF8) // real-time: running status survives it
    {
        emit (&byte, 1);
        state = State::afterMessage;
        return;
    }
    if (byte == 0xF7) // EOX without a SysEx
    {
        running = 0;
        state = State::afterMessage;
        return;
    }
    running = byte < 0xF0 ? byte : 0; // system common cancels running status
    message[0] = byte;
    messageFill = 1;
    messageNeeded = messageLength (byte);
    if (messageFill >= messageNeeded)
    {
        emit (message, messageFill);
        messageFill = 0;
        state = State::afterMessage;
    }
    else
        state = State::inMessage;
}

void Decoder::runningData (uint8_t byte)
{
    if (running == 0)
    {
        state = State::waitTimestamp; // nothing to run on: skip to the next timestamp
        return;
    }
    message[0] = running;
    messageFill = 1;
    messageNeeded = messageLength (running);
    data (byte);
}

void Decoder::data (uint8_t byte)
{
    message[messageFill++] = byte;
    if (messageFill >= messageNeeded)
    {
        emit (message, messageFill);
        messageFill = 0;
        state = State::afterMessage;
    }
    else
        state = State::inMessage;
}

void Decoder::decodePacket (const uint8_t* data_, size_t length)
{
    if (data_ == nullptr || length < 2 || (data_[0] & 0xC0) != 0x80)
        return; // not a BLE-MIDI packet

    // A channel message never spans packets; a SysEx may.
    state = State::waitTimestamp;
    messageFill = 0;

    for (size_t i = 1; i < length; ++i)
    {
        const uint8_t b = data_[i];

        if (sysexActive)
        {
            if (b < 0x80)
            {
                sysexByte (b);
                continue;
            }
            // A timestamp. What follows ends the SysEx, interleaves a
            // real-time byte, or (malformed) starts something else.
            if (i + 1 >= length)
                break;
            const uint8_t next = data_[++i];
            if (next == 0xF7)
            {
                endSysEx();
                state = State::afterMessage;
            }
            else if (next >= 0xF8)
                emit (&next, 1);
            else if (next >= 0x80)
            {
                sysexActive = false; // abandoned: drop it
                sysexLength = 0;
                status (next);
            }
            else
                sysexByte (next); // a timestamp before plain data: tolerated
            continue;
        }

        switch (state)
        {
            case State::waitTimestamp:
                if (b & 0x80)
                    state = State::waitStatus;
                break;
            case State::waitStatus:
                if (b & 0x80)
                    status (b);
                else
                    runningData (b);
                break;
            case State::inMessage:
                if (! (b & 0x80))
                    data (b);
                else if (i + 1 < length && data_[i + 1] >= 0xF8)
                    emit (&data_[++i], 1); // real-time inside a message
                else
                {
                    messageFill = 0; // a timestamp cut the message short
                    state = State::waitStatus;
                }
                break;
            case State::afterMessage:
                if (b & 0x80)
                    state = State::waitStatus;
                else
                    runningData (b); // running status sharing the last timestamp
                break;
        }
    }
}

// ---------------------------------------------------------------- Encoder

Encoder::Encoder (Sink s, void* ctx) : sink (s), context (ctx)
{
}

void Encoder::setPacketSize (size_t bytes)
{
    flush();
    limit = bytes < kMinPacket ? kMinPacket : (bytes > kMaxPacket ? kMaxPacket : bytes);
}

void Encoder::begin (uint32_t timeMs)
{
    fill = 0;
    put (static_cast<uint8_t> (0x80 | ((timeMs >> 7) & 0x3F)));
    lastStatus = 0;
    lastTime = timeMs;
}

void Encoder::flush()
{
    if (fill > 1 && sink != nullptr)
        sink (context, packet, fill);
    fill = 0;
    lastStatus = 0;
}

void Encoder::clear()
{
    fill = 0;
    lastStatus = 0;
}

bool Encoder::add (const uint8_t* m, size_t n, uint32_t timeMs)
{
    if (m == nullptr || n == 0 || m[0] < 0x80)
        return false;
    const uint8_t s = m[0];
    if (s == 0xF0)
    {
        if (n < 2 || m[n - 1] != 0xF7)
            return false;
        for (size_t i = 1; i + 1 < n; ++i)
            if (m[i] & 0x80)
                return false;
    }
    else
    {
        if (n != messageLength (s))
            return false;
        for (size_t i = 1; i < n; ++i)
            if (m[i] & 0x80)
                return false;
    }

    // Inside a packet each timestamp may only be up to 127 ms after the last
    // (the receiver carries a wrap of the low 7 bits into the header's bits).
    if (fill > 0 && ((timeMs - lastTime) & 0x1FFF) >= 128)
        flush();

    if (s == 0xF0)
    {
        if (fill > 0 && room() < 3) // "ts F0" and at least one more byte
            flush();
        if (fill == 0)
            begin (timeMs);
        putTimestamp (timeMs);
        put (0xF0);
        for (size_t i = 1; i + 1 < n; ++i)
        {
            if (room() == 0)
            {
                flush();
                begin (timeMs); // continuation packet: header, then data bytes
            }
            put (m[i]);
        }
        if (room() < 2)
        {
            flush();
            begin (timeMs);
        }
        putTimestamp (timeMs);
        put (0xF7);
        lastStatus = 0;
        lastTime = timeMs;
        return true;
    }

    bool runningStatus = useRunningStatus && s < 0xF0 && s == lastStatus && fill > 0;
    size_t need = 1 + (runningStatus ? n - 1 : n);
    if (fill > 0 && need > room())
    {
        flush();
        runningStatus = false;
    }
    if (fill == 0)
        begin (timeMs);
    putTimestamp (timeMs);
    for (size_t i = runningStatus ? 1 : 0; i < n; ++i)
        put (m[i]);
    if (s < 0xF0)
        lastStatus = s;
    else if (s < 0xF8)
        lastStatus = 0; // system common cancels running status; real-time does not
    lastTime = timeMs;
    return true;
}
} // namespace c3::blemidi
