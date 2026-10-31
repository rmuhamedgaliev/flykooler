// SPDX-License-Identifier: MIT
// Wire protocol of the Flydigi BS cooler series. Frame layout:
//   5A A5 <cmd> <len> <payload> <cksum>, len = 2 + payload size,
//   cksum = (cmd + len + sum(payload)) & 0xFF.
// USB transport: 31-byte writes with no report id, 32-byte reads (the first
// byte is a firmware constant 0x03). Bluetooth: 25-byte reports, report id
// 0x02 on writes and 0x01 on reads.

#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <optional>

namespace FlyKooler {

enum class Model { Bs1, Bs2Pro, Bs3, Bs3Pro, Unknown };
enum class Transport { Usb, Bluetooth };

const char *modelName(Model m);
Model modelFromPid(quint16 pid);

namespace Cmd {
enum : quint8 {
    FwVersion      = 0x01, // reply: 4 firmware version bytes
    PowerState     = 0x02, // reply: 01 awake / 02 asleep
    Wake           = 0x05, // wake the device up
    Supply         = 0x07, // reply: power supply level 00..03
    SelectGear     = 0x08, // payload: 01..04 — WARNING: out of range corrupts the gear table!
    AutoStart      = 0x0C, // payload: 01 on / 02 off (auto-start after power-up)
    Standby        = 0x0D, // payload: 00 stay / 01 sleep at once / 02 sleep after a minute
    SetRealtimeRpm = 0x21, // payload: u16 LE — realtime mode only
    QueryRpm       = 0x22, // reply: current u16 LE + target u16 LE
    EnterRealtime  = 0x23,
    ExitRealtime   = 0x24,
    WorkMode       = 0x25, // reply: 01..04 gear / 05 realtime / 04 asleep
    SetGearRpm     = 0x26, // payload: slot 00..03 + u16 LE — store a gear speed
    QueryGearTable = 0x27, // reply: 4 x u16 LE
    LightBegin     = 0x41, // start an animation upload
    LightBlock     = 0x42, // data block, up to 15 bytes
    LightCommit    = 0x43, // apply and persist to flash
    SelectEffect   = 0x44, // built-in effect 1..5 — realtime mode only
    QueryStrip     = 0x45, // reply: 01 strip on
    StripPower     = 0x46, // payload: 01 / 00
    GearLeds       = 0x48, // payload: 01 / 00 — gear indicator leds
    StatusNotify   = 0xEF, // status frame pushed by the cooler, ~every 500 ms
};
}

// One 0xEF status frame. BS3 sends an 11-byte payload, BS2 Pro sends 9
// (without the ramp-profile and persisted-realtime bytes), so the sequence
// counter sits two bytes earlier on a BS2 Pro.
struct Status {
    quint16 currentRpm = 0;
    quint16 targetRpm = 0;
    quint16 seq = 0;
    int gear = 0;            // 1..4, 0 = unknown
    int supplyLevel = 0;     // 0 undecided, 1 low, 2 medium, 3 full
    int standby = 0;         // 0/1/2
    bool realtime = false;
    bool asleep = false;
    bool bleUp = false;
    bool usbUp = false;
    bool autoStart = false;
    bool stripOn = false;
    bool gearLedsOn = false;

    int maxRpm() const;      // rpm ceiling for the current supply level
};

struct Frame {
    quint8 cmd = 0;
    QByteArray payload;
};

// Build a frame (without report id and padding — Device shapes transport).
QByteArray buildFrame(quint8 cmd, const QByteArray &payload = {});

// Payload builders for the sendCommand-style APIs.
QByteArray payload1(quint8 v);
QByteArray payloadLe16(quint16 v);

// Parse a report read from the device (USB 32 bytes starting with 0x03, or
// Bluetooth 25 bytes starting with 0x01).
std::optional<Frame> parseFrame(const QByteArray &report);
std::optional<Status> parseStatus(const QByteArray &report);

// Lighting: a 186-byte animation buffer for the strip (uploaded by the
// caller with the 0x41/0x42/0x43 commands).
enum class LightPreset { Static, Effect1, Effect2, Effect3, Effect4, Effect5 };
QByteArray lightBuffer(LightPreset preset, quint8 r, quint8 g, quint8 b, quint8 brightness);

} // namespace FlyKooler
