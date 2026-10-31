// SPDX-License-Identifier: MIT
// flykoolerd — the Flydigi Kooler backend. The single owner of the cooler:
// it keeps the link, applies the configuration, runs the fan curve and
// serves clients (GUI/CLI) over a Unix socket.

#include "Daemon.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("flykoolerd"));
    QCoreApplication::setOrganizationName(QStringLiteral("flykooler"));

    FlyKooler::Daemon daemon;
    switch (daemon.start()) {
    case FlyKooler::Daemon::StartResult::Started:
        return app.exec();
    case FlyKooler::Daemon::StartResult::AlreadyRunning:
        // Normal when the GUI or systemd raced us; do not let
        // Restart=on-failure spin on it.
        return 0;
    case FlyKooler::Daemon::StartResult::Failed:
        return 1;
    }
    return 1;
}
