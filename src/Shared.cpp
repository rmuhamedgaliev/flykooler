// SPDX-License-Identifier: MIT

#include "Shared.h"

#include <QJsonArray>
#include <QStandardPaths>

#include <algorithm>

namespace FlyKooler {

QString daemonSocketPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
           QStringLiteral("/flykooler.sock");
}

QVector<QPair<int, int>> Config::defaultCurve()
{
    return {{40, 700},  {50, 1000}, {60, 1600},
            {70, 2100}, {80, 2600}, {90, 3000}};
}

QJsonObject Config::toJson() const
{
    QJsonArray curveJson;
    for (const auto &p : curve)
        curveJson.append(QJsonArray{p.first, p.second});

    return QJsonObject{
        {QStringLiteral("fanMode"), fanMode},
        {QStringLiteral("fixedRpm"), fixedRpm},
        {QStringLiteral("gear"), gear},
        {QStringLiteral("curve"), curveJson},
        {QStringLiteral("sensorChip"), sensorChip},
        {QStringLiteral("sensorLabel"), sensorLabel},
        {QStringLiteral("lightOn"), lightOn},
        {QStringLiteral("lightMode"), lightMode},
        {QStringLiteral("color"), color},
        {QStringLiteral("brightness"), brightness},
        {QStringLiteral("gearLeds"), gearLeds},
        {QStringLiteral("standby"), standby},
        {QStringLiteral("wake"), wake},
        {QStringLiteral("autostart"), autostart},
    };
}

Config Config::fromJson(const QJsonObject &o)
{
    return fromJson(o, Config{});
}Config Config::fromJson(const QJsonObject &o, const Config &fb)
{
    Config c = fb;

    const QString mode = o.value(QStringLiteral("fanMode")).toString();
    if (mode == QStringLiteral("smart") || mode == QStringLiteral("fixed") ||
        mode == QStringLiteral("gear"))
        c.fanMode = mode;

    c.fixedRpm = qBound(500, o.value(QStringLiteral("fixedRpm")).toInt(c.fixedRpm), 4000);
    c.gear = qBound(1, o.value(QStringLiteral("gear")).toInt(c.gear), 4);

    const QJsonValue curve = o.value(QStringLiteral("curve"));
    if (curve.isArray()) {
        QVector<QPair<int, int>> pts;
        for (const QJsonValue &v : curve.toArray()) {
            if (!v.isArray())
                continue;
            const QJsonArray pair = v.toArray();
            if (pair.size() != 2)
                continue;
            const int t = pair[0].toInt(-1);
            const int r = pair[1].toInt(-1);
            if (t < 20 || t > 110 || r < 500 || r > 4000)
                continue;
            pts.push_back({t, r});
        }
        if (!pts.isEmpty()) {
            std::sort(pts.begin(), pts.end());
            c.curve = pts;
        }
    }

    c.sensorChip = o.value(QStringLiteral("sensorChip")).toString(c.sensorChip);
    c.sensorLabel = o.value(QStringLiteral("sensorLabel")).toString(c.sensorLabel);

    c.lightOn = o.value(QStringLiteral("lightOn")).toBool(c.lightOn);
    c.lightMode = qBound(0, o.value(QStringLiteral("lightMode")).toInt(c.lightMode), 5);
    const QString color = o.value(QStringLiteral("color")).toString();
    if (color.size() == 7 && color.startsWith(u'#'))
        c.color = color.toLower();
    c.brightness = qBound(0, o.value(QStringLiteral("brightness")).toInt(c.brightness), 100);
    c.gearLeds = o.value(QStringLiteral("gearLeds")).toBool(c.gearLeds);

    c.standby = qBound(0, o.value(QStringLiteral("standby")).toInt(c.standby), 2);
    c.wake = o.value(QStringLiteral("wake")).toBool(c.wake);
    c.autostart = o.value(QStringLiteral("autostart")).toBool(c.autostart);

    return c;
}

int Config::rpmFor(double tempC) const
{
    if (curve.isEmpty())
        return 1500;
    QVector<QPair<int, int>> pts = curve; // points may arrive unsorted
    std::sort(pts.begin(), pts.end());
    if (tempC <= pts.first().first)
        return pts.first().second;
    for (int i = 1; i < pts.size(); ++i) {
        if (tempC > pts[i].first)
            continue;
        const double span = pts[i].first - pts[i - 1].first;
        if (span <= 0)
            return pts[i].second;
        const double k = (tempC - pts[i - 1].first) / span;
        return int(pts[i - 1].second + k * (pts[i].second - pts[i - 1].second));
    }
    return pts.last().second;
}

} // namespace FlyKooler
