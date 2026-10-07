#pragma once

// BLE-MIDI peripheral on NimBLE: the standard MIDI service and characteristic,
// one connection at a time, bonding with "just works" pairing. NimBLE calls
// back on its own task; this class only queues packets and link events there,
// and the main loop drains them (poll*) and sends notifications (pump).

#include "core/PacketRing.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class BleMidiPort
{
public:
    static constexpr size_t kMaxPacket = 125; // ATT_MTU 128 - 3
    static constexpr uint16_t kPreferredMtu = 128;

    enum class Event : uint8_t
    {
        connected,
        disconnected,
        subscribed,
        unsubscribed
    };

    /** Starts NimBLE and advertising under `name`. */
    void begin (const char* name);

    /** Removes every stored pairing (the computer has to pair again). */
    static void forgetBonds();

    // Main loop side
    bool pollEvent (Event& event);
    /** Copies the next received packet into `out` (kMaxPacket bytes); returns its length or 0. */
    size_t pollPacket (uint8_t* out);
    /** Largest notification payload for the current connection. */
    size_t packetSize() const;
    bool canSend() const;
    /** Queues one BLE-MIDI packet for notification. Returns false when the queue is full. */
    bool queue (const uint8_t* packet, size_t length);
    /** Sends queued notifications while the stack has buffers for them. */
    void pump();
    void dropQueued();
    uint32_t droppedIn() const { return rx.droppedCount(); }

    // NimBLE task side (called from the callbacks in BleMidiPort.cpp)
    void onConnect (uint16_t connHandle);
    void onDisconnect();
    void onSubscribe (bool notify);
    void onMtu (uint16_t value);
    void onWrite (const uint8_t* data, size_t length);

private:
    c3::PacketRing<64, kMaxPacket> rx;  // the app's full state arrives as a burst
    c3::PacketRing<16, 1> events;
    c3::PacketRing<16, kMaxPacket> tx; // loop side only
    uint8_t pending[kMaxPacket] {};       // a notification the stack had no buffer for yet
    size_t pendingLength = 0;
    std::atomic<uint32_t> connection { 0xFFFF };
    std::atomic<uint32_t> mtu { 23 };
    std::atomic<bool> notifying { false };
};
