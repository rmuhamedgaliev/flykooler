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
    if (!daemon.start()) {
        // already running — exit quietly
        return 0;
    }
    return app.exec();
}
