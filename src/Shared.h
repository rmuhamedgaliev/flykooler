// SPDX-License-Identifier: MIT
// Shared between the backend (flykoolerd) and the clients (GUI, CLI):
// the configuration and its JSON form for the socket protocol.

#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace FlyKooler {

// Unix socket path of the backend — shared by the daemon and its clients.
QString daemonSocketPath();

struct Config {
    QString fanMode = QStringLiteral("smart"); // smart | fixed | gear
    int fixedRpm = 1500;
    int gear = 2;                              // 1..4
    QVector<QPair<int, int>> curve;            // (°C, rpm), sorted by °C

    QString sensorChip;
    QString sensorLabel;

    bool lightOn = true;
    int lightMode = 0;                         // 0 static colour, 1..5 effects
    QString color = QStringLiteral("#00e0b8");
    int brightness = 60;
    bool gearLeds = true;

    int standby = 0;                           // 0/1/2
    bool wake = true;
    bool autostart = false;

    QJsonObject toJson() const;

    // Partial application: missing or invalid fields fall back to `fallback`,
    // so a client may send only what changed.
    static Config fromJson(const QJsonObject &o);
    static Config fromJson(const QJsonObject &o, const Config &fallback);

    // Fan speed for a temperature: linear between the curve points, the
    // first/last point outside them.
    int rpmFor(double tempC) const;

    static QVector<QPair<int, int>> defaultCurve();
};

} // namespace FlyKooler
