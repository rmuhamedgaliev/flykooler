// SPDX-License-Identifier: MIT
// flykoolerd — the Flydigi Kooler backend. The single owner of the cooler:
// it keeps the link, applies the configuration, runs the fan curve and
// serves clients (GUI/CLI) over a Unix socket.

#include "Daemon.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSocketNotifier>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// The POSIX handler may only touch async-signal-safe things: it writes a byte
// into a socketpair, and the event loop picks it up in onUnixSignal().
int g_signalFd[2] = {-1, -1};

extern "C" void forwardSignal(int)
{
    if (g_signalFd[1] < 0)
        return;
    const char byte = 1;
    const ssize_t n = ::write(g_signalFd[1], &byte, sizeof byte);
    Q_UNUSED(n);
}

bool installSignalHandlers()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_signalFd) != 0)
        return false;

    struct sigaction action {};
    action.sa_handler = forwardSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(sig, &action, nullptr);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("flykoolerd"));
    QCoreApplication::setOrganizationName(QStringLiteral("flykooler"));

    FlyKooler::Daemon daemon;
    switch (daemon.start()) {
    case FlyKooler::Daemon::StartResult::Started:
        break;
    case FlyKooler::Daemon::StartResult::AlreadyRunning:
        // Normal when the GUI or systemd raced us; do not let
        // Restart=on-failure spin on it.
        return 0;
    case FlyKooler::Daemon::StartResult::Failed:
        return 1;
    }

    // systemd stops the user service on logout and shutdown. Put the cooler
    // to sleep first, give the queued frames a moment, then leave.
    if (installSignalHandlers()) {
        auto *signalNotifier = new QSocketNotifier(g_signalFd[0],
                                                   QSocketNotifier::Read, &app);
        QObject::connect(signalNotifier, &QSocketNotifier::activated, &app,
                         [&daemon, &app, signalNotifier] {
                             char byte = 0;
                             const ssize_t n = ::read(g_signalFd[0], &byte, sizeof byte);
                             Q_UNUSED(n);
                             signalNotifier->setEnabled(false);
                             daemon.powerOffCooler();
                             QTimer::singleShot(300, &app, &QCoreApplication::quit);
                         });
    } else {
        qWarning("cannot install the shutdown signal handlers");
    }

    return app.exec();
}
