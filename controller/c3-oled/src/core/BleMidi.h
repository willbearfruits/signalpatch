#pragma once

// BLE-MIDI packet codec (MIDI over Bluetooth Low Energy 1.0).
//
// A packet is: header (10tttttt, timestamp bits 12-7), then for every message
// a timestamp byte (1ttttttt, bits 6-0) and the message. Inside one packet a
// message may use running status ("ts data data", or just "data data" sharing
// the previous timestamp). A SysEx starts with "ts F0", continues in later
// packets as "header data data ...", and ends with "ts F7"; real-time bytes may
// appear inside it as "ts F8".
//
// The encoder never uses running status unless asked (setRunningStatus), so
// every receiver gets the plainest form; the decoder accepts all of it.

#include <cstddef>
#include <cstdint>

namespace c3::blemidi
{
/** Expected length of a message that starts with `status` (0 = SysEx / invalid). */
size_t messageLength (uint8_t status);

class Decoder
{
public:
    using Callback = void (*) (void* context, const uint8_t* message, size_t length);

    /** `sysexBuffer` holds one SysEx including F0 and F7; longer ones are dropped whole. */
    Decoder (uint8_t* sysexBuffer, size_t sysexCapacity, Callback callback, void* context);

    void reset();
    void decodePacket (const uint8_t* data, size_t length);

    bool inSysEx() const { return sysexActive; }

private:
    enum class State : uint8_t
    {
        waitTimestamp, // start of packet: data bytes here belong to a lost SysEx and are dropped
        waitStatus,    // after a timestamp
        inMessage,     // collecting data bytes
        afterMessage   // a message ended: timestamp, or running-status data
    };

    void status (uint8_t byte);
    void runningData (uint8_t byte);
    void data (uint8_t byte);
    void sysexByte (uint8_t byte);
    void endSysEx();
    void emit (const uint8_t* message, size_t length);

    uint8_t* sysex;
    size_t sysexCapacity;
    size_t sysexLength = 0;
    bool sysexActive = false;
    bool sysexOverflow = false;

    Callback callback;
    void* context;

    State state = State::waitTimestamp;
    uint8_t running = 0;
    uint8_t message[3] {};
    size_t messageFill = 0;
    size_t messageNeeded = 0;
};

class Encoder
{
public:
    static constexpr size_t kMaxPacket = 244; // ATT_MTU 247 - 3
    static constexpr size_t kMinPacket = 5;   // header + timestamp + a 3-byte message

    using Sink = void (*) (void* context, const uint8_t* packet, size_t length);

    Encoder (Sink sink, void* context);

    /** Largest packet (ATT_MTU - 3). Flushes what is buffered first. */
    void setPacketSize (size_t bytes);
    size_t packetSize() const { return limit; }

    void setRunningStatus (bool enabled) { useRunningStatus = enabled; }

    /** Adds one complete message (channel, system common, real-time, or F0..F7).
        `timeMs` is a millisecond clock (only its low 13 bits are sent). Packets
        that fill up go to the sink; call flush() to send the rest. Returns false
        for a malformed message (nothing is added). */
    bool add (const uint8_t* message, size_t length, uint32_t timeMs);

    void flush();
    void clear();
    bool empty() const { return fill == 0; }

private:
    void begin (uint32_t timeMs);
    void put (uint8_t byte) { packet[fill++] = byte; }
    void putTimestamp (uint32_t timeMs) { put (static_cast<uint8_t> (0x80 | (timeMs & 0x7F))); }
    size_t room() const { return limit - fill; }

    Sink sink;
    void* context;
    uint8_t packet[kMaxPacket] {};
    size_t fill = 0;
    size_t limit = 20;
    uint32_t lastTime = 0;
    uint8_t lastStatus = 0;
    bool useRunningStatus = false;
};
} // namespace c3::blemidi
