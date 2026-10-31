// SPDX-License-Identifier: MIT

#include "Sensors.h"

#include <QDir>
#include <QFile>
#include <cmath>

namespace FlyKooler {

QString Sensor::display() const
{
    return chip + (label.isEmpty() ? QString() : QStringLiteral(" · ") + label);
}

static QStringList listFiles(const QString &dir, const QString &prefix)
{
    return QDir(dir).entryList({prefix + QStringLiteral("*")}, QDir::Files, QDir::Name);
}

std::vector<Sensor> listSensors()
{
    std::vector<Sensor> out;

    // hwmon: chips with tempN_input files.
    const QDir hw(QStringLiteral("/sys/class/hwmon"));
    const QStringList chips = hw.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &chipDir : chips) {
        const QString base = hw.filePath(chipDir);
        QFile nameFile(base + QStringLiteral("/name"));
        QString chip = chipDir;
        if (nameFile.open(QIODevice::ReadOnly)) {
            const QString n = QString::fromLatin1(nameFile.readAll().trimmed());
            if (!n.isEmpty())
                chip = n;
        }
        for (const QString &f : listFiles(base, QStringLiteral("temp"))) {
            if (!f.endsWith(QStringLiteral("_input")))
                continue;
            QString label;
            QFile lf(base + u'/' + f.chopped(6) + QStringLiteral("_label"));
            if (lf.open(QIODevice::ReadOnly))
                label = QString::fromLatin1(lf.readAll().trimmed());
            out.push_back(Sensor{chip, label, base + u'/' + f});
        }
    }

    // ACPI zones: what has no hwmon (board thermistors).
    const QDir tz(QStringLiteral("/sys/class/thermal"));
    const QStringList zones = tz.entryList({QStringLiteral("thermal_zone*")},
                                           QDir::Dirs, QDir::Name);
    for (const QString &zone : zones) {
        const QString base = tz.filePath(zone);
        QFile tf(base + QStringLiteral("/type"));
        QFile tempf(base + QStringLiteral("/temp"));
        if (!tempf.exists() || !tf.open(QIODevice::ReadOnly))
            continue;
        const QString type = QString::fromLatin1(tf.readAll().trimmed());
        if (!tempf.open(QIODevice::ReadOnly))
            continue;
        const QString raw = QString::fromLatin1(tempf.readAll().trimmed());
        bool ok = false;
        raw.toInt(&ok);
        if (!ok)
            continue;
        out.push_back(Sensor{QStringLiteral("thermal"), type, base + QStringLiteral("/temp")});
    }

    return out;
}

double readTemperature(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::numeric_limits<double>::quiet_NaN();
    const int milli = QString::fromLatin1(f.readAll().trimmed()).toInt();
    return double(milli) / 1000.0;
}

double readMaxForChip(const QString &chip)
{
    double best = std::numeric_limits<double>::quiet_NaN();
    for (const Sensor &s : listSensors()) {
        if (s.chip != chip)
            continue;
        const double t = readTemperature(s.path);
        if (std::isnan(t))
            continue;
        if (std::isnan(best) || t > best)
            best = t;
    }
    return best;
}

} // namespace FlyKooler
