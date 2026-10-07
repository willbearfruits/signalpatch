#pragma once

// Single-producer single-consumer queue of whole packets. BLE-MIDI decoding
// depends on packet boundaries (each starts with a header byte), so writes
// from the BLE host task are queued as packets and decoded in the main loop.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace c3
{
template <size_t Slots, size_t SlotBytes>
class PacketRing
{
    static_assert (Slots >= 2 && (Slots & (Slots - 1)) == 0, "Slots must be a power of two");

public:
    /** Producer side. Returns false (and drops the packet) when full or too long. */
    bool push (const uint8_t* data, size_t length)
    {
        if (length == 0 || length > SlotBytes)
        {
            dropped.store (dropped.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return false;
        }
        const uint32_t head = writeIndex.load (std::memory_order_relaxed);
        const uint32_t tail = readIndex.load (std::memory_order_acquire);
        if (head - tail >= Slots)
        {
            dropped.store (dropped.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            return false;
        }
        Slot& slot = slots[head & (Slots - 1)];
        std::memcpy (slot.data, data, length);
        slot.length = static_cast<uint16_t> (length);
        writeIndex.store (head + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. Copies the oldest packet into `out` (SlotBytes long); returns its length or 0. */
    size_t pop (uint8_t* out)
    {
        const uint32_t tail = readIndex.load (std::memory_order_relaxed);
        const uint32_t head = writeIndex.load (std::memory_order_acquire);
        if (head == tail)
            return 0;
        const Slot& slot = slots[tail & (Slots - 1)];
        const size_t length = slot.length;
        std::memcpy (out, slot.data, length);
        readIndex.store (tail + 1, std::memory_order_release);
        return length;
    }

    /** Consumer side: forget everything queued. */
    void clear() { readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release); }

    uint32_t droppedCount() const { return dropped.load (std::memory_order_relaxed); }

private:
    struct Slot
    {
        uint8_t data[SlotBytes];
        uint16_t length;
    };

    Slot slots[Slots] {};
    std::atomic<uint32_t> writeIndex { 0 };
    std::atomic<uint32_t> readIndex { 0 };
    std::atomic<uint32_t> dropped { 0 };
};
} // namespace c3
