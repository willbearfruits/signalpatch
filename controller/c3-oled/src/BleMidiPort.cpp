#include "BleMidiPort.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace
{
// MIDI over Bluetooth Low Energy 1.0.
const char* const kMidiService = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
const char* const kMidiCharacteristic = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

BleMidiPort* port = nullptr;
NimBLECharacteristic* characteristic = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect (NimBLEServer* server, NimBLEConnInfo& info) override
    {
        // 7.5-15 ms connection interval, no latency, 4 s supervision timeout.
        server->updateConnParams (info.getConnHandle(), 6, 12, 0, 400);
        port->onConnect (info.getConnHandle());
    }

    void onDisconnect (NimBLEServer*, NimBLEConnInfo&, int) override
    {
        port->onDisconnect(); // advertising restarts on its own (advertiseOnDisconnect)
    }

    void onMTUChange (uint16_t mtu, NimBLEConnInfo&) override { port->onMtu (mtu); }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite (NimBLECharacteristic* c, NimBLEConnInfo&) override
    {
        const NimBLEAttValue value = c->getValue();
        port->onWrite (value.data(), value.size());
        static const uint8_t none[1] = { 0 };
        c->setValue (none, 0); // BLE-MIDI: a read returns no payload
    }

    void onSubscribe (NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override
    {
        port->onSubscribe ((subValue & 0x0001) != 0);
    }
};

ServerCallbacks serverCallbacks;
CharacteristicCallbacks characteristicCallbacks;
} // namespace

void BleMidiPort::begin (const char* name)
{
    port = this;
    NimBLEDevice::init (name);
    NimBLEDevice::setMTU (kPreferredMtu);
    // Bonding, no man-in-the-middle protection (there is nothing to type a
    // code on), LE secure connections: the "just works" pairing that Windows
    // and BlueZ use for MIDI devices.
    NimBLEDevice::setSecurityAuth (true, false, true);
    NimBLEDevice::setSecurityIOCap (BLE_HS_IO_NO_INPUT_OUTPUT);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks (&serverCallbacks, false);
    server->advertiseOnDisconnect (true);

    NimBLEService* service = server->createService (kMidiService);
    characteristic = service->createCharacteristic (
        kMidiCharacteristic, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
    characteristic->setCallbacks (&characteristicCallbacks);
    server->start(); // starts the service too

    // Flags and the MIDI service in the advertisement (21 of 31 bytes), the
    // name in the scan response.
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->enableScanResponse (true);
    NimBLEAdvertisementData advertisement;
    advertisement.setFlags (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advertisement.setCompleteServices (NimBLEUUID (kMidiService));
    NimBLEAdvertisementData scanResponse;
    scanResponse.setName (name);
    if (! advertising->setAdvertisementData (advertisement) || ! advertising->setScanResponseData (scanResponse))
        Serial.println ("ble: advertising data rejected");
    advertising->setMinInterval (32); // 20-40 ms
    advertising->setMaxInterval (64);
    if (! advertising->start())
        Serial.println ("ble: advertising did not start");
}

void BleMidiPort::forgetBonds()
{
    NimBLEDevice::deleteAllBonds();
}

// ------------------------------------------------------------ NimBLE task side

void BleMidiPort::onConnect (uint16_t connHandle)
{
    connection.store (connHandle);
    mtu.store (23);
    notifying.store (false);
    const uint8_t e = static_cast<uint8_t> (Event::connected);
    events.push (&e, 1);
}

void BleMidiPort::onDisconnect()
{
    connection.store (0xFFFF);
    notifying.store (false);
    const uint8_t e = static_cast<uint8_t> (Event::disconnected);
    events.push (&e, 1);
}

void BleMidiPort::onSubscribe (bool notify)
{
    notifying.store (notify);
    const uint8_t e = static_cast<uint8_t> (notify ? Event::subscribed : Event::unsubscribed);
    events.push (&e, 1);
}

void BleMidiPort::onMtu (uint16_t value)
{
    mtu.store (value);
}

void BleMidiPort::onWrite (const uint8_t* data, size_t length)
{
    rx.push (data, length);
}

// ------------------------------------------------------------ Main loop side

bool BleMidiPort::pollEvent (Event& event)
{
    uint8_t e = 0;
    if (events.pop (&e) == 0)
        return false;
    event = static_cast<Event> (e);
    return true;
}

size_t BleMidiPort::pollPacket (uint8_t* out)
{
    return rx.pop (out);
}

size_t BleMidiPort::packetSize() const
{
    const uint32_t m = mtu.load();
    const size_t payload = m > 3 ? m - 3 : 20;
    return payload < kMaxPacket ? payload : kMaxPacket;
}

bool BleMidiPort::canSend() const
{
    return connection.load() != 0xFFFF && notifying.load();
}

bool BleMidiPort::queue (const uint8_t* packet, size_t length)
{
    return tx.push (packet, length);
}

void BleMidiPort::dropQueued()
{
    tx.clear();
    pendingLength = 0;
}

void BleMidiPort::pump()
{
    if (! canSend())
    {
        dropQueued();
        return;
    }
    const uint16_t handle = static_cast<uint16_t> (connection.load());
    for (;;)
    {
        if (pendingLength == 0)
            pendingLength = tx.pop (pending);
        if (pendingLength == 0)
            return;
        if (! characteristic->notify (pending, pendingLength, handle))
            return; // out of buffers: try again on the next pass
        pendingLength = 0;
    }
}
