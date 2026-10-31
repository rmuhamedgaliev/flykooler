// SPDX-License-Identifier: MIT
// Protocol tests. The status frames are golden samples: the BS2 Pro frame
// was captured over USB from a live device, the BS3 Pro frame comes from
// the firmware analysis published by the flydigictl project.

#include <QtTest>

#include "Protocol.h"

using namespace FlyKooler;

namespace {

QByteArray fromHex(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

} // namespace

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void buildsFramesTheFirmwareAccepts();
    void payloadHelpers();
    void parsesBs2ProStatus();
    void parsesBs3ProStatus();
    void rejectsCorruptInput();
    void mapsModelPids();
    void rpmCeilingsFollowSupply();
    void buildsStaticLightBuffer();
    void clampsLightBrightness();
};

void TestProtocol::buildsFramesTheFirmwareAccepts()
{
    // checksum = (cmd + len + sum(payload)) & 0xFF — verified on hardware
    QCOMPARE(buildFrame(Cmd::EnterRealtime).toHex(), QByteArray("5aa5230225"));
    QCOMPARE(buildFrame(Cmd::SetRealtimeRpm, payloadLe16(2600)).toHex(),
             QByteArray("5aa52104280a57"));
    QCOMPARE(buildFrame(Cmd::StripPower, payload1(0x01)).toHex(),
             QByteArray("5aa54603014a"));
}

void TestProtocol::payloadHelpers()
{
    QCOMPARE(payload1(0x02), QByteArray(1, char(0x02)));
    QCOMPARE(payloadLe16(2600).toHex(), QByteArray("280a"));
    QCOMPARE(payloadLe16(0xBEEF).toHex(), QByteArray("efbe"));
}

void TestProtocol::parsesBs2ProStatus()
{
    // Captured from a BS2 Pro over USB: the first byte is the firmware
    // constant 0x03, the payload is 9 bytes (no ramp/realtime bytes —
    // that is the BS2 Pro difference), the counter sits two bytes earlier.
    const QByteArray report = fromHex("035aa5ef0b3a0305d007d0079b0085")
                              + QByteArray(16, char(0));

    const auto st = parseStatus(report);
    QVERIFY(st.has_value());
    QCOMPARE(st->currentRpm, quint16(2000));
    QCOMPARE(st->targetRpm, quint16(2000));
    QCOMPARE(st->seq, quint16(0x9B));
    QCOMPARE(st->gear, 2);
    QCOMPARE(st->supplyLevel, 1);
    QCOMPARE(st->realtime, true);
    QCOMPARE(st->asleep, false);
    QCOMPARE(st->usbUp, true);
    QCOMPARE(st->bleUp, true);
    QCOMPARE(st->gearLedsOn, true);
    QCOMPARE(st->stripOn, true);
}

void TestProtocol::parsesBs3ProStatus()
{
    // From the flydigictl firmware documentation: a Bluetooth frame with
    // the full 11-byte payload.
    const QByteArray report = fromHex("015aa5ef0d6803056c076c070101b4232b")
                              + QByteArray(8, char(0));

    const auto st = parseStatus(report);
    QVERIFY(st.has_value());
    QCOMPARE(st->currentRpm, quint16(1900));
    QCOMPARE(st->targetRpm, quint16(1900));
    QCOMPARE(st->seq, quint16(0x23B4));
    QCOMPARE(st->gear, 1);
    QCOMPARE(st->supplyLevel, 3);
    QCOMPARE(st->realtime, true);
    QCOMPARE(st->usbUp, false);
    QCOMPARE(st->bleUp, true);
}

void TestProtocol::rejectsCorruptInput()
{
    // bad magic
    QVERIFY(!parseFrame(fromHex("0355a5ef0b3a0305d007d0079b0085")).has_value());
    // truncated mid-payload
    QVERIFY(!parseFrame(fromHex("035aa5ef0b3a0305d007")).has_value());
    // wrong checksum (last byte flipped)
    QVERIFY(!parseFrame(fromHex("035aa5ef0b3a0305d007d0079b0084")).has_value());
    // length byte below the header size
    QVERIFY(!parseFrame(fromHex("035aa5ef01")).has_value());
    // a status frame with a too short payload
    QVERIFY(!parseStatus(fromHex("035aa5ef053a030539")).has_value());
    // a non-status frame is not a status
    QVERIFY(!parseStatus(buildFrame(Cmd::EnterRealtime)).has_value());
}

void TestProtocol::mapsModelPids()
{
    QCOMPARE(modelFromPid(0x1001), Model::Bs1);
    QCOMPARE(modelFromPid(0x1002), Model::Bs2Pro);
    QCOMPARE(modelFromPid(0x1003), Model::Bs3);
    QCOMPARE(modelFromPid(0x1004), Model::Bs3Pro);
    QCOMPARE(modelFromPid(0x1005), Model::Unknown);
    QCOMPARE(QByteArray(modelName(Model::Bs2Pro)), QByteArray("BS2 Pro"));
}

void TestProtocol::rpmCeilingsFollowSupply()
{
    Status st;
    st.supplyLevel = 1;
    QCOMPARE(st.maxRpm(), 2700); // a laptop USB port
    st.supplyLevel = 2;
    QCOMPARE(st.maxRpm(), 3300);
    st.supplyLevel = 3;
    QCOMPARE(st.maxRpm(), 4000); // a PD adapter
    st.supplyLevel = 0;
    QCOMPARE(st.maxRpm(), 4000); // not measured yet — unrestricted
}

void TestProtocol::buildsStaticLightBuffer()
{
    const QByteArray buf = lightBuffer(LightPreset::Static, 0x33, 0x66, 0x99, 50);
    QCOMPARE(buf.size(), 186);

    // header: version, steady mode, speed, brightness, raw colour
    QCOMPARE(quint8(buf[1]), quint8(0x02));
    QCOMPARE(quint8(buf[3]), quint8(0x00));
    QCOMPARE(quint8(buf[4]), quint8(0x0A));
    QCOMPARE(quint8(buf[5]), quint8(50));
    QCOMPARE(quint8(buf[6]), quint8(0x33));
    QCOMPARE(quint8(buf[7]), quint8(0x66));
    QCOMPARE(quint8(buf[8]), quint8(0x99));

    // the lit steps carry the colour scaled by brightness
    const int step2 = (2 + 1) * 10;
    QCOMPARE(quint8(buf[step2 + 6]), quint8(0x19)); // 0x33 * 50%
    QCOMPARE(quint8(buf[step2 + 7]), quint8(0x33)); // 0x66 * 50%
    QCOMPARE(quint8(buf[step2 + 8]), quint8(0x4C)); // 0x99 * 50%

    // a dark step stays dark
    QCOMPARE(quint8(buf[10]), quint8(0));
    QCOMPARE(quint8(buf[11]), quint8(0));
}

void TestProtocol::clampsLightBrightness()
{
    const QByteArray buf = lightBuffer(LightPreset::Static, 1, 2, 3, 150);
    QCOMPARE(quint8(buf[5]), quint8(100)); // above 100 clamps to 100
}

QTEST_GUILESS_MAIN(TestProtocol)
#include "tst_Protocol.moc"
