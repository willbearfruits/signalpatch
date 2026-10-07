// BLE-MIDI codec: decoder forms, encoder packets, round trips, fuzz.

#include "core/BleMidi.h"
#include "core/Protocol.h"

#include <unity.h>

#include <cstdint>
#include <random>
#include <vector>

using namespace c3;
using Bytes = std::vector<uint8_t>;

namespace
{
struct Collector
{
    std::vector<Bytes> messages;
    static void callback (void* context, const uint8_t* message, size_t length)
    {
        static_cast<Collector*> (context)->messages.emplace_back (message, message + length);
    }
};

struct Packets
{
    std::vector<Bytes> packets;
    static void sink (void* context, const uint8_t* packet, size_t length)
    {
        static_cast<Packets*> (context)->packets.emplace_back (packet, packet + length);
    }
};

struct Rig
{
    uint8_t sysex[256];
    Collector out;
    blemidi::Decoder decoder { sysex, sizeof (sysex), &Collector::callback, &out };

    void feed (const Bytes& packet) { decoder.decodePacket (packet.data(), packet.size()); }
};

void expectBytes (const Bytes& expected, const Bytes& actual)
{
    TEST_ASSERT_EQUAL_UINT32 (expected.size(), actual.size());
    if (! expected.empty())
        TEST_ASSERT_EQUAL_HEX8_ARRAY (expected.data(), actual.data(), expected.size());
}
} // namespace

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------ Decoder

void test_decode_single_cc()
{
    Rig rig;
    rig.feed ({ 0x80, 0x80, 0xB9, 0x10, 0x40 });
    TEST_ASSERT_EQUAL (1, rig.out.messages.size());
    expectBytes ({ 0xB9, 0x10, 0x40 }, rig.out.messages[0]);
}

void test_decode_several_messages_with_timestamps()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0x99, 0x3C, 0x7F, 0x82, 0x89, 0x3C, 0x00, 0x83, 0xC9, 0x05 });
    TEST_ASSERT_EQUAL (3, rig.out.messages.size());
    expectBytes ({ 0x99, 0x3C, 0x7F }, rig.out.messages[0]);
    expectBytes ({ 0x89, 0x3C, 0x00 }, rig.out.messages[1]);
    expectBytes ({ 0xC9, 0x05 }, rig.out.messages[2]);
}

void test_decode_running_status_with_timestamp()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xB9, 0x10, 0x40, 0x82, 0x11, 0x41 });
    TEST_ASSERT_EQUAL (2, rig.out.messages.size());
    expectBytes ({ 0xB9, 0x11, 0x41 }, rig.out.messages[1]);
}

void test_decode_running_status_without_timestamp()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xB9, 0x10, 0x40, 0x11, 0x41, 0x12, 0x42 });
    TEST_ASSERT_EQUAL (3, rig.out.messages.size());
    expectBytes ({ 0xB9, 0x12, 0x42 }, rig.out.messages[2]);

    Rig two;
    two.feed ({ 0x80, 0x81, 0xC0, 0x05, 0x06, 0x82, 0x07 });
    TEST_ASSERT_EQUAL (3, two.out.messages.size());
    expectBytes ({ 0xC0, 0x06 }, two.out.messages[1]);
    expectBytes ({ 0xC0, 0x07 }, two.out.messages[2]);
}

void test_decode_sysex_in_one_packet()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xF0, 0x7D, 0x53, 0x7F, 0x01, 0x82, 0xF7 });
    TEST_ASSERT_EQUAL (1, rig.out.messages.size());
    expectBytes ({ 0xF0, 0x7D, 0x53, 0x7F, 0x01, 0xF7 }, rig.out.messages[0]);
}

void test_decode_sysex_across_packets()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xF0, 0x7D, 0x53, 0x04, 'A', 'B' });
    TEST_ASSERT_TRUE (rig.decoder.inSysEx());
    rig.feed ({ 0x80, 'C', 'D', 'E' });
    rig.feed ({ 0x81, 'F', 0x81, 0xF7, 0x81, 0xB9, 0x10, 0x40 });
    TEST_ASSERT_EQUAL (2, rig.out.messages.size());
    expectBytes ({ 0xF0, 0x7D, 0x53, 0x04, 'A', 'B', 'C', 'D', 'E', 'F', 0xF7 }, rig.out.messages[0]);
    expectBytes ({ 0xB9, 0x10, 0x40 }, rig.out.messages[1]);
    TEST_ASSERT_FALSE (rig.decoder.inSysEx());
}

void test_decode_sysex_end_alone_in_a_packet()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xF0, 0x7D, 0x53, 0x10 });
    rig.feed ({ 0x80, 0x81, 0xF7 });
    TEST_ASSERT_EQUAL (1, rig.out.messages.size());
    expectBytes ({ 0xF0, 0x7D, 0x53, 0x10, 0xF7 }, rig.out.messages[0]);
}

void test_decode_realtime_inside_sysex_and_messages()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xF0, 0x01, 0x02, 0x81, 0xF8, 0x03, 0x82, 0xF7 });
    TEST_ASSERT_EQUAL (2, rig.out.messages.size());
    expectBytes ({ 0xF8 }, rig.out.messages[0]);
    expectBytes ({ 0xF0, 0x01, 0x02, 0x03, 0xF7 }, rig.out.messages[1]);

    Rig between;
    between.feed ({ 0x80, 0x81, 0x90, 0x3C, 0x7F, 0x81, 0xF8, 0x3D, 0x7F });
    TEST_ASSERT_EQUAL (3, between.out.messages.size());
    expectBytes ({ 0xF8 }, between.out.messages[1]);
    expectBytes ({ 0x90, 0x3D, 0x7F }, between.out.messages[2]); // running status survives real-time

    Rig inside;
    inside.feed ({ 0x80, 0x81, 0xB0, 0x07, 0x81, 0xFE, 0x64 });
    TEST_ASSERT_EQUAL (2, inside.out.messages.size());
    expectBytes ({ 0xFE }, inside.out.messages[0]);
    expectBytes ({ 0xB0, 0x07, 0x64 }, inside.out.messages[1]);
}

void test_decode_system_common_cancels_running_status()
{
    Rig rig;
    rig.feed ({ 0x80, 0x81, 0xB0, 0x01, 0x02, 0x81, 0xF3, 0x05, 0x06, 0x07 });
    TEST_ASSERT_EQUAL (2, rig.out.messages.size());
    expectBytes ({ 0xF3, 0x05 }, rig.out.messages[1]);
}

void test_decode_rejects_malformed_packets()
{
    Rig rig;
    rig.feed ({});
    rig.feed ({ 0x80 });
    rig.feed ({ 0x00, 0x81, 0xB0, 0x01, 0x02 }); // header without bit 7
    rig.feed ({ 0xC0, 0x81, 0xB0, 0x01, 0x02 }); // header with bit 6
    TEST_ASSERT_EQUAL (0, rig.out.messages.size());

    rig.feed ({ 0x80, 0x10, 0x20, 0x81, 0xB0, 0x01, 0x02 }); // data before any timestamp
    TEST_ASSERT_EQUAL (1, rig.out.messages.size());
    expectBytes ({ 0xB0, 0x01, 0x02 }, rig.out.messages[0]);

    rig.feed ({ 0x80, 0x81, 0xB0, 0x01 });            // truncated at the end of the packet
    rig.feed ({ 0x80, 0x81, 0xB0, 0x02, 0x03 });
    TEST_ASSERT_EQUAL (2, rig.out.messages.size());
    expectBytes ({ 0xB0, 0x02, 0x03 }, rig.out.messages[1]);

    rig.feed ({ 0x80, 0x81, 0xF7, 0x81, 0xB0, 0x04, 0x05 }); // stray EOX
    TEST_ASSERT_EQUAL (3, rig.out.messages.size());

    rig.feed ({ 0x80, 0x81, 0xF0, 0x01, 0x81, 0xB0, 0x10, 0x20 }); // a status abandons the SysEx
    TEST_ASSERT_EQUAL (4, rig.out.messages.size());
    expectBytes ({ 0xB0, 0x10, 0x20 }, rig.out.messages[3]);
    TEST_ASSERT_FALSE (rig.decoder.inSysEx());

    Rig fresh;
    fresh.feed ({ 0x80, 0x81, 0x10, 0x20 }); // running status with nothing to run on
    TEST_ASSERT_EQUAL (0, fresh.out.messages.size());
}

void test_decode_drops_oversized_sysex_and_recovers()
{
    uint8_t small[16];
    Collector out;
    blemidi::Decoder decoder (small, sizeof (small), &Collector::callback, &out);
    Bytes packet { 0x80, 0x81, 0xF0 };
    for (int i = 0; i < 40; ++i)
        packet.push_back (static_cast<uint8_t> (i));
    packet.insert (packet.end(), { 0x81, 0xF7, 0x81, 0xB0, 0x01, 0x02 });
    decoder.decodePacket (packet.data(), packet.size());
    TEST_ASSERT_EQUAL (1, out.messages.size());
    expectBytes ({ 0xB0, 0x01, 0x02 }, out.messages[0]);

    const Bytes fits { 0x80, 0x81, 0xF0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0x81, 0xF7 }; // 16 bytes with F0/F7
    decoder.decodePacket (fits.data(), fits.size());
    TEST_ASSERT_EQUAL (2, out.messages.size());
    TEST_ASSERT_EQUAL (16, out.messages[1].size());
}

void test_decode_fuzz_never_emits_malformed_messages()
{
    Rig rig;
    std::mt19937 random (1234);
    for (int round = 0; round < 200000; ++round)
    {
        Bytes packet (1 + random() % 40);
        for (auto& b : packet)
            b = static_cast<uint8_t> (random());
        if (random() % 2)
            packet[0] = static_cast<uint8_t> (0x80 | (packet[0] & 0x3F));
        rig.feed (packet);
    }
    TEST_ASSERT_TRUE (rig.out.messages.size() > 1000);
    for (const auto& m : rig.out.messages)
    {
        TEST_ASSERT_TRUE (! m.empty());
        TEST_ASSERT_TRUE (m[0] >= 0x80);
        if (m[0] == 0xF0)
        {
            TEST_ASSERT_EQUAL_HEX8 (0xF7, m.back());
            for (size_t i = 1; i + 1 < m.size(); ++i)
                TEST_ASSERT_TRUE (m[i] < 0x80);
        }
        else
        {
            TEST_ASSERT_EQUAL (blemidi::messageLength (m[0]), m.size());
            for (size_t i = 1; i < m.size(); ++i)
                TEST_ASSERT_TRUE (m[i] < 0x80);
        }
    }
}

// ------------------------------------------------------------ Encoder

void test_encode_header_and_timestamp_bits()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    const uint8_t cc[] = { 0xB9, 0x10, 0x40 };
    TEST_ASSERT_TRUE (encoder.add (cc, 3, 0x1234)); // 4660 ms: high 36, low 52
    encoder.flush();
    TEST_ASSERT_EQUAL (1, out.packets.size());
    expectBytes ({ 0xA4, 0xB4, 0xB9, 0x10, 0x40 }, out.packets[0]);

    out.packets.clear();
    TEST_ASSERT_TRUE (encoder.add (cc, 3, 0x2000 + 5)); // only 13 bits are sent
    encoder.flush();
    expectBytes ({ 0x80, 0x85, 0xB9, 0x10, 0x40 }, out.packets[0]);
}

void test_encode_packs_messages_and_respects_packet_size()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    encoder.setPacketSize (20);
    for (uint8_t i = 0; i < 6; ++i)
    {
        const uint8_t cc[] = { 0xB9, i, 0x40 };
        TEST_ASSERT_TRUE (encoder.add (cc, 3, 100));
    }
    encoder.flush();
    TEST_ASSERT_EQUAL (2, out.packets.size());
    TEST_ASSERT_EQUAL (17, out.packets[0].size()); // header + 4 × (timestamp + 3)
    TEST_ASSERT_EQUAL (9, out.packets[1].size());
    encoder.flush();
    TEST_ASSERT_EQUAL (2, out.packets.size()); // nothing left
}

void test_encode_running_status_when_enabled()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    encoder.setRunningStatus (true);
    const uint8_t a[] = { 0xB9, 0x10, 0x40 };
    const uint8_t b[] = { 0xB9, 0x11, 0x41 };
    const uint8_t c[] = { 0x99, 0x3C, 0x7F };
    encoder.add (a, 3, 0);
    encoder.add (b, 3, 0);
    encoder.add (c, 3, 0);
    encoder.flush();
    expectBytes ({ 0x80, 0x80, 0xB9, 0x10, 0x40, 0x80, 0x11, 0x41, 0x80, 0x99, 0x3C, 0x7F }, out.packets[0]);
}

void test_encode_settings_dump_over_default_mtu()
{
    Settings settings;
    uint8_t dump[proto::kSettingsDumpSize];
    const size_t length = proto::buildSettingsDump (settings, dump, sizeof (dump));
    TEST_ASSERT_EQUAL (53, length);

    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    encoder.setPacketSize (20); // ATT_MTU 23
    TEST_ASSERT_TRUE (encoder.add (dump, length, 0));
    encoder.flush();
    TEST_ASSERT_EQUAL (3, out.packets.size());
    TEST_ASSERT_EQUAL (20, out.packets[0].size());
    TEST_ASSERT_EQUAL (20, out.packets[1].size());
    TEST_ASSERT_EQUAL (18, out.packets[2].size());

    // First: header, timestamp, F0, 17 data bytes.
    expectBytes ({ 0x80, 0x80, 0xF0, 0x7D, 0x53, 0x11, 0x00, 10, 0x01, 16, 0x02, 17, 0x03, 18, 0x04, 11, 0x05, 12, 0x06, 13 },
                 out.packets[0]);
    // Continuation: header, then data only.
    TEST_ASSERT_EQUAL_HEX8 (0x80, out.packets[1][0]);
    for (size_t i = 1; i < out.packets[1].size(); ++i)
        TEST_ASSERT_TRUE (out.packets[1][i] < 0x80);
    // Last: header, data, timestamp, F7.
    TEST_ASSERT_EQUAL_HEX8 (0x80, out.packets[2][16]);
    TEST_ASSERT_EQUAL_HEX8 (0xF7, out.packets[2][17]);

    Rig rig;
    for (const auto& p : out.packets)
        rig.feed (p);
    TEST_ASSERT_EQUAL (1, rig.out.messages.size());
    expectBytes (Bytes (dump, dump + length), rig.out.messages[0]);
}

void test_encode_sysex_end_gets_its_own_packet_when_full()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    encoder.setPacketSize (20);
    Bytes sysex { 0xF0 };
    for (uint8_t i = 0; i < 17; ++i)
        sysex.push_back (i);
    sysex.push_back (0xF7);
    encoder.add (sysex.data(), sysex.size(), 0);
    encoder.flush();
    TEST_ASSERT_EQUAL (2, out.packets.size());
    TEST_ASSERT_EQUAL (20, out.packets[0].size());
    expectBytes ({ 0x80, 0x80, 0xF7 }, out.packets[1]);
}

void test_encode_timestamps_within_a_packet()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    encoder.setPacketSize (100);
    const uint8_t cc[] = { 0xB0, 0x01, 0x02 };
    encoder.add (cc, 3, 127);
    encoder.add (cc, 3, 130); // low 7 bits wrap: allowed in the same packet
    encoder.add (cc, 3, 400); // 270 ms later: a new packet
    encoder.flush();
    TEST_ASSERT_EQUAL (2, out.packets.size());
    expectBytes ({ 0x80, 0xFF, 0xB0, 0x01, 0x02, 0x82, 0xB0, 0x01, 0x02 }, out.packets[0]);
    expectBytes ({ 0x83, 0x90, 0xB0, 0x01, 0x02 }, out.packets[1]);
}

void test_encode_rejects_malformed_messages()
{
    Packets out;
    blemidi::Encoder encoder (&Packets::sink, &out);
    const uint8_t data[] = { 0x10, 0x20 };
    const uint8_t shortCc[] = { 0xB0, 0x01 };
    const uint8_t highData[] = { 0xB0, 0x81, 0x02 };
    const uint8_t openSysex[] = { 0xF0, 0x01, 0x02 };
    const uint8_t badSysex[] = { 0xF0, 0x81, 0xF7 };
    const uint8_t eox[] = { 0xF7 };
    TEST_ASSERT_FALSE (encoder.add (data, 2, 0));
    TEST_ASSERT_FALSE (encoder.add (shortCc, 2, 0));
    TEST_ASSERT_FALSE (encoder.add (highData, 3, 0));
    TEST_ASSERT_FALSE (encoder.add (openSysex, 3, 0));
    TEST_ASSERT_FALSE (encoder.add (badSysex, 3, 0));
    TEST_ASSERT_FALSE (encoder.add (eox, 1, 0));
    TEST_ASSERT_FALSE (encoder.add (nullptr, 3, 0));
    encoder.flush();
    TEST_ASSERT_EQUAL (0, out.packets.size());
}

void test_round_trip_random_streams()
{
    std::mt19937 random (99);
    const size_t sizes[] = { 5, 6, 7, 20, 23, 64, 125, 244 };
    for (size_t packetSize : sizes)
        for (int useRunning = 0; useRunning < 2; ++useRunning)
        {
            Packets out;
            blemidi::Encoder encoder (&Packets::sink, &out);
            encoder.setPacketSize (packetSize);
            encoder.setRunningStatus (useRunning != 0);

            std::vector<Bytes> sent;
            uint32_t time = random() % 8192;
            for (int i = 0; i < 400; ++i)
            {
                Bytes m;
                const int kind = static_cast<int> (random() % 10);
                if (kind < 5)
                {
                    const uint8_t statuses[] = { 0x80, 0x90, 0xB0, 0xC0, 0xE0 };
                    const uint8_t status = static_cast<uint8_t> (statuses[random() % 5] | (random() % 2));
                    m.push_back (status);
                    for (size_t d = 1; d < blemidi::messageLength (status); ++d)
                        m.push_back (static_cast<uint8_t> (random() % 128));
                }
                else if (kind < 7)
                {
                    m.push_back (0xF0);
                    const size_t length = random() % 120;
                    for (size_t d = 0; d < length; ++d)
                        m.push_back (static_cast<uint8_t> (random() % 128));
                    m.push_back (0xF7);
                }
                else if (kind < 8)
                    m.push_back (static_cast<uint8_t> (0xF8 + random() % 8));
                else
                {
                    m = { 0xB9, static_cast<uint8_t> (random() % 128), static_cast<uint8_t> (random() % 128) };
                }
                TEST_ASSERT_TRUE (encoder.add (m.data(), m.size(), time));
                sent.push_back (m);
                time += random() % 50;
                if (random() % 8 == 0)
                    encoder.flush();
            }
            encoder.flush();

            Rig rig;
            for (const auto& p : out.packets)
            {
                TEST_ASSERT_TRUE (p.size() <= packetSize);
                TEST_ASSERT_TRUE (p.size() >= 2);
                TEST_ASSERT_EQUAL_HEX8 (0x80, p[0] & 0xC0);
                rig.feed (p);
            }
            TEST_ASSERT_EQUAL (sent.size(), rig.out.messages.size());
            for (size_t i = 0; i < sent.size(); ++i)
                expectBytes (sent[i], rig.out.messages[i]);
        }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST (test_decode_single_cc);
    RUN_TEST (test_decode_several_messages_with_timestamps);
    RUN_TEST (test_decode_running_status_with_timestamp);
    RUN_TEST (test_decode_running_status_without_timestamp);
    RUN_TEST (test_decode_sysex_in_one_packet);
    RUN_TEST (test_decode_sysex_across_packets);
    RUN_TEST (test_decode_sysex_end_alone_in_a_packet);
    RUN_TEST (test_decode_realtime_inside_sysex_and_messages);
    RUN_TEST (test_decode_system_common_cancels_running_status);
    RUN_TEST (test_decode_rejects_malformed_packets);
    RUN_TEST (test_decode_drops_oversized_sysex_and_recovers);
    RUN_TEST (test_decode_fuzz_never_emits_malformed_messages);
    RUN_TEST (test_encode_header_and_timestamp_bits);
    RUN_TEST (test_encode_packs_messages_and_respects_packet_size);
    RUN_TEST (test_encode_running_status_when_enabled);
    RUN_TEST (test_encode_settings_dump_over_default_mtu);
    RUN_TEST (test_encode_sysex_end_gets_its_own_packet_when_full);
    RUN_TEST (test_encode_timestamps_within_a_packet);
    RUN_TEST (test_encode_rejects_malformed_messages);
    RUN_TEST (test_round_trip_random_streams);
    return UNITY_END();
}
