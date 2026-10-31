// SPDX-License-Identifier: MIT
// Temperature sensors from /sys/class/hwmon and ACPI zones under
// /sys/class/thermal.

#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace FlyKooler {

struct Sensor {
    QString chip;    // k10temp, amdgpu, nvme…
    QString label;   // Tctl, edge, Composite… (may be empty)
    QString path;    // /sys/class/hwmon/hwmonX/tempN_input
    QString display() const;
};

std::vector<Sensor> listSensors();
double readTemperature(const QString &path); // °C, NaN on error
double readMaxForChip(const QString &chip);  // hottest input of a chip, NaN if none

} // namespace FlyKooler
