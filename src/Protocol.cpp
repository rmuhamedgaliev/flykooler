// SPDX-License-Identifier: MIT

#include "Protocol.h"

#include <QtGlobal>

namespace FlyKooler {
namespace {

constexpr quint8 Magic[2] = {0x5A, 0xA5};

// Animation buffer steps that actually light up (slot indices 0..17).
constexpr int LitSteps[] = {2, 5, 8, 11, 14};

quint8 checksum(quint8 cmd, const QByteArray &payload)
{
    quint32 sum = cmd + quint8(2 + payload.size());
    for (char c : payload)
        sum += quint8(c);
    return quint8(sum & 0xFF);
}

} // namespace

const char *modelName(Model m)
{
    switch (m) {
    case Model::Bs2Pro: return "BS2 Pro";
    case Model::Bs3:    return "BS3";
    case Model::Bs3Pro: return "BS3 Pro";
    default:            return "BS?";
    }
}

Model modelFromPid(quint16 pid)
{
    switch (pid) {
    // BS1 (0x1001) is BLE-only and not HID; ignoring it beats sending frames
    // the firmware does not understand.
    case 0x1001: return Model::Unknown;
    case 0x1002: return Model::Bs2Pro;
    case 0x1003: return Model::Bs3;
    case 0x1004: return Model::Bs3Pro;
    default:     return Model::Unknown;
    }
}

QByteArray buildFrame(quint8 cmd, const QByteArray &payload)
{
    // The firmware caps the frame length: payload <= 15 bytes.
    QByteArray f;
    f.reserve(payload.size() + 6);
    f.append(char(Magic[0]));
    f.append(char(Magic[1]));
    f.append(char(cmd));
    f.append(char(2 + payload.size()));
    f.append(payload);
    f.append(char(checksum(cmd, payload)));
    return f;
}

QByteArray payload1(quint8 v)
{
    return QByteArray(1, char(v));
}

QByteArray payloadLe16(quint16 v)
{
    QByteArray p;
    p.append(char(v & 0xFF));
    p.append(char((v >> 8) & 0xFF));
    return p;
}

std::optional<Frame> parseFrame(const QByteArray &report)
{
    // The first byte is the report id (Bluetooth) or the firmware constant
    // 0x03 (USB); the magic follows it.
    if (report.size() < 6 || quint8(report[1]) != Magic[0] || quint8(report[2]) != Magic[1])
        return std::nullopt;
    const quint8 cmd = quint8(report[3]);
    const int len = quint8(report[4]);
    if (len < 2)
        return std::nullopt;
    const int payloadLen = len - 2;
    if (report.size() < 5 + payloadLen + 1)
        return std::nullopt;
    const QByteArray payload = report.mid(5, payloadLen);
    if (quint8(report[5 + payloadLen]) != checksum(cmd, payload))
        return std::nullopt;
    return Frame{cmd, payload};
}

std::optional<Status> parseStatus(const QByteArray &report)
{
    const auto frame = parseFrame(report);
    if (!frame || frame->cmd != Cmd::StatusNotify || frame->payload.size() < 9)
        return std::nullopt;
    const QByteArray &p = frame->payload;

    Status s;
    const quint8 flags = quint8(p[0]);
    const quint8 mode = quint8(p[1]);
    const quint8 settings = p.size() >= 3 ? quint8(p[2]) : 0;

    s.asleep    = flags & 0x01;
    s.gear      = ((flags >> 1) & 0x03) + 1;
    s.bleUp     = flags & 0x08;
    s.usbUp     = flags & 0x10;
    s.supplyLevel = (flags >> 5) & 0x03;
    s.realtime  = mode & 0x01;
    s.autoStart = mode & 0x02;
    s.standby   = (mode >> 2) & 0x03;
    s.gearLedsOn = settings & 0x01;
    s.stripOn   = settings & 0x04;

    auto rpmAt = [&p](int at) {
        return quint16(quint8(p[at]) | (quint8(p[at + 1]) << 8));
    };
    s.currentRpm = rpmAt(3);
    s.targetRpm  = rpmAt(5);
    // BS3 Pro: 11 payload bytes, counter at [9..10]. BS2 Pro: 9 bytes, [7..8].
    s.seq = rpmAt(p.size() >= 11 ? 9 : 7);
    return s;
}

int Status::maxRpm() const
{
    switch (supplyLevel) {
    case 1:  return 2700;
    case 2:  return 3300;
    default: return 4000; // level 3 or not measured yet
    }
}

namespace {

// Paints a steady colour: a header plus lit steps carrying the same colour.
void paintStatic(QByteArray &buf, quint8 r, quint8 g, quint8 b, quint8 brightness)
{
    buf.fill(0, 186);
    buf[1] = 0x02;
    buf[3] = 0x00;      // steady
    buf[4] = 0x0A;      // speed
    buf[5] = quint8(brightness > 100 ? 100 : brightness);
    buf[6] = char(r);
    buf[7] = char(g);
    buf[8] = char(b);
    const auto scale = [brightness](quint8 c) {
        return char(c * (brightness > 100 ? 100 : brightness) / 100);
    };
    for (int step : LitSteps) {
        const int at = (step + 1) * 10;
        buf[at + 6] = scale(r);
        buf[at + 7] = scale(g);
        buf[at + 8] = scale(b);
    }
}

} // namespace

QByteArray lightBuffer(LightPreset preset, quint8 r, quint8 g, quint8 b, quint8 brightness)
{
    if (brightness > 100)
        brightness = 100;

    if (preset == LightPreset::Static) {
        QByteArray buf(186, char(0));
        paintStatic(buf, r, g, b, brightness);
        return buf;
    }

    // Built-in firmware effects, transcribed from the BS3 Pro disassembly
    // (fun_ram_00005bdc) and ported from flydigictl. Header layout:
    // [1]=02 [3]=mode [4]=speed [5]=brightness; the palette is RGB triplets.
    QByteArray buf(180, char(0));
    buf[1] = 0x02;
    buf[5] = char(brightness);

    struct Ramp { quint8 mode, speed; quint8 colors[4][3]; };
    const Ramp ramps[3] = {
        {0x03, 0x0A, {{0x00, 0xFF, 0x00}, {0x3F, 0xFF, 0x3F}, {0xFF, 0xFF, 0xFF}, {0x3F, 0xFF, 0x3F}}},
        {0x03, 0x06, {{0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0x3F}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0x3F}}},
        {0x03, 0x02, {{0xFF, 0x00, 0x00}, {0xFF, 0x3F, 0x3F}, {0xFF, 0xFF, 0xFF}, {0xFF, 0x3F, 0x3F}}},
    };

    const int effect = int(preset) - int(LightPreset::Effect1) + 1; // 1..5
    if (effect >= 1 && effect <= 3) {
        const Ramp &rp = ramps[effect - 1];
        buf[3] = char(rp.mode);
        buf[4] = char(rp.speed);
        for (int half : {0, 90}) {
            const int starts[3] = {6, 36, 66};
            for (int group = 0; group < 3; ++group) {
                for (int slot = 0; slot < 4; ++slot) {
                    const auto &c = rp.colors[(slot + group) % 4];
                    const int at = half + starts[group] + slot * 3;
                    buf[at]     = char(c[0]);
                    buf[at + 1] = char(c[1]);
                    buf[at + 2] = char(c[2]);
                }
            }
        }
        return buf;
    }

    if (effect == 4) { // steady red
        buf[3] = 0x01;
        buf[4] = 0x02;
        for (int base = 0; base < 180; base += 30) {
            for (int slot = 0; slot < 2; ++slot) {
                const int at = base + 6 + slot * 3;
                buf[at] = char(0xFF);
                buf[at + 1] = 0x00;
                buf[at + 2] = 0x00;
            }
        }
        return buf;
    }

    // Effect 5: multicolour, individual words.
    buf[3] = 0x02;
    buf[4] = 0x0A;
    const struct { int off; const char *bytes; int len; } words[] = {
        {6,   "\xFF\xFF", 2},
        {8,   "\xFF\x1E\x1E\xA0", 4},
        {14,  "\x50", 1},
        {36,  "\x00\x00\x50\xFF", 4},
        {40,  "\xFF\xFF\x1E\x1E", 4},
        {44,  "\xA0", 1},
        {68,  "\x50\x1E\x1E\xA0", 4},
        {72,  "\xFF\xFF", 2},
        {74,  "\xFF", 1},
        {96,  "\xFF\xFF\xFF\x1E", 4},
        {100, "\x1E\xA0", 2},
        {104, "\x50", 1},
        {128, "\x50\xFF\xFF\xFF", 4},
        {132, "\x1E\x1E", 2},
        {134, "\xA0", 1},
        {156, "\x00\x00\x50\x1E", 4},
        {160, "\x1E\xA0\xFF\xFF", 4},
        {164, "\xFF", 1},
    };
    for (const auto &w : words)
        for (int i = 0; i < w.len; ++i)
            buf[w.off + i] = w.bytes[i];
    return buf;
}

} // namespace FlyKooler
