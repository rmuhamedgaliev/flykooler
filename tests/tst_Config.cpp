// SPDX-License-Identifier: MIT
// Configuration and fan-curve tests.

#include <QtTest>

#include "Shared.h"

using namespace FlyKooler;

class TestConfig : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsThroughJson();
    void partialUpdateKeepsTheRest();
    void clampsAndRejectsBadValues();
    void sortsTheCurve();
    void interpolatesTheCurve();
    void curveBoundaries();

private:
    Config sample() const;
};

Config TestConfig::sample() const
{
    Config c;
    c.fanMode = QStringLiteral("fixed");
    c.fixedRpm = 2600;
    c.gear = 3;
    c.curve = {{40, 700}, {60, 1800}, {90, 4000}};
    c.sensorChip = QStringLiteral("k10temp");
    c.sensorLabel = QStringLiteral("Tctl");
    c.lightOn = false;
    c.lightMode = 2;
    c.color = QStringLiteral("#112233");
    c.brightness = 33;
    c.gearLeds = false;
    c.standby = 2;
    c.wake = false;
    c.autostart = true;
    return c;
}

void TestConfig::roundTripsThroughJson()
{
    const Config c = sample();
    const Config back = Config::fromJson(c.toJson());
    QCOMPARE(back.toJson(), c.toJson()); // lossless and stable
}

void TestConfig::partialUpdateKeepsTheRest()
{
    const Config base = sample();
    const QJsonObject patch = QJsonObject{
        {QStringLiteral("fanMode"), QStringLiteral("smart")},
        {QStringLiteral("fixedRpm"), 1800},
    };
    const Config merged = Config::fromJson(patch, base);

    QCOMPARE(merged.fanMode, QStringLiteral("smart"));
    QCOMPARE(merged.fixedRpm, 1800);
    // everything the patch did not mention is untouched
    QCOMPARE(merged.gear, base.gear);
    QCOMPARE(merged.curve, base.curve);
    QCOMPARE(merged.lightOn, base.lightOn);
    QCOMPARE(merged.color, base.color);
    QCOMPARE(merged.standby, base.standby);
    QCOMPARE(merged.wake, base.wake);
}

void TestConfig::clampsAndRejectsBadValues()
{
    const Config base = sample();

    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("fixedRpm"), 99999}}, base).fixedRpm,
             4000);
    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("fixedRpm"), 1}}, base).fixedRpm, 500);
    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("gear"), 9}}, base).gear, 4);
    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("brightness"), 150}}, base).brightness,
             100);
    // an unknown mode is ignored, the fallback stays
    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("fanMode"), "turbo"}}, base).fanMode,
             base.fanMode);
    // a malformed colour is ignored
    QCOMPARE(Config::fromJson(QJsonObject{{QStringLiteral("color"), "#12345"}}, base).color,
             base.color);
    // curve points outside sane ranges are dropped
    const Config withJunk = Config::fromJson(QJsonObject{
        {QStringLiteral("curve"), QJsonArray{QJsonArray{150, 4000}, QJsonArray{60, 1800},
                                             QJsonArray{70, 99999}}},
    }, base);
    QCOMPARE(withJunk.curve.size(), 1);
    QCOMPARE(withJunk.curve.first().first, 60);
}

void TestConfig::sortsTheCurve()
{
    const Config c = Config::fromJson(QJsonObject{
        {QStringLiteral("curve"), QJsonArray{QJsonArray{90, 4000}, QJsonArray{40, 700},
                                             QJsonArray{60, 1800}}},
    });
    QCOMPARE(c.curve.size(), 3);
    QVERIFY(c.curve[0].first < c.curve[1].first);
    QVERIFY(c.curve[1].first < c.curve[2].first);
}

void TestConfig::interpolatesTheCurve()
{
    Config c;
    c.curve = {{40, 700}, {60, 1800}, {90, 4000}};

    QCOMPARE(c.rpmFor(40.0), 700);  // exactly on a point
    QCOMPARE(c.rpmFor(50.0), 1250); // halfway between 40 and 60
    QCOMPARE(c.rpmFor(55.0), 1525);
    QCOMPARE(c.rpmFor(75.0), 2900); // halfway between 60 and 90
}

void TestConfig::curveBoundaries()
{
    Config c;
    c.curve = {{40, 700}, {90, 4000}};

    QCOMPARE(c.rpmFor(20.0), 700);   // below the first point
    QCOMPARE(c.rpmFor(120.0), 4000); // above the last point

    // a single-point curve is a constant
    Config flat;
    flat.curve = {{60, 1500}};
    QCOMPARE(flat.rpmFor(10.0), 1500);
    QCOMPARE(flat.rpmFor(95.0), 1500);

    // an unsorted curve still interpolates correctly
    Config shuffled;
    shuffled.curve = {{90, 4000}, {40, 700}};
    QCOMPARE(shuffled.rpmFor(50.0), 1360); // 20% between 40 and 90

    // an empty curve falls back to a sane middle ground
    Config empty;
    QCOMPARE(empty.rpmFor(50.0), 1500);
}

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_Config.moc"
