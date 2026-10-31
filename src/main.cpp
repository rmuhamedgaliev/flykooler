// SPDX-License-Identifier: MIT
// Flydigi Kooler — control for Flydigi BS-series laptop coolers on
// KDE Plasma / Qt 6. Client-server design:
//   flykoolerd — backend: owns the cooler, runs the fan curve, Unix socket
//   flykooler  — GUI client (starts the backend itself if needed)
//   flykooler cli <command> — CLI: via the backend, or directly without it

#include "Device.h"
#include "MainWindow.h"
#include "Protocol.h"
#include "Shared.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

using namespace FlyKooler;

static void printUsage()
{
    std::puts("Usage: flykooler cli <command> [args]\n"
              "  status                 — current state\n"
              "  info                   — firmware, gears, power\n"
              "  set <rpm>              — hold a speed (500–4000)\n"
              "  stop                   — stop the fan\n"
              "  auto                   — give control back to the button\n"
              "  gear <1–4>             — select a gear\n"
              "  light on|off           — strip on/off\n"
              "  light color RRGGBB [brightness]\n"
              "  light effect <1–5> [brightness]\n"
              "  standby <0|1|2>        — behaviour when the link drops\n"
              "  sensors                — temperature sensors");
}

// ---------------- CLI over the backend (when the daemon runs) ----------------

static int runIpcCli(const QStringList &args)
{
    QLocalSocket sock;
    sock.connectToServer(daemonSocketPath());
    if (!sock.waitForConnected(400))
        return -1; // no backend — the caller falls back to direct mode

    const auto roundTrip = [&sock](const QJsonObject &req,
                                   const QString &wantType) -> QJsonObject {
        sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
        sock.waitForBytesWritten(500);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 6000) {
            while (sock.canReadLine()) {
                const QJsonObject o =
                    QJsonDocument::fromJson(sock.readLine()).object();
                if (o.value(QStringLiteral("type")).toString() == wantType)
                    return o;
            }
            sock.waitForReadyRead(200);
        }
        return {};
    };

    const auto setConfig = [&](const QJsonObject &patch) {
        return roundTrip(QJsonObject{{QStringLiteral("request"),
                                      QStringLiteral("set_config")},
                                     {QStringLiteral("config"), patch}},
                         QStringLiteral("reply"));
    };

    const QString cmd = args.value(0);

    if (cmd == QStringLiteral("status") || cmd == QStringLiteral("info")) {
        const QJsonObject st = roundTrip(
            QJsonObject{{QStringLiteral("request"), QStringLiteral("status")}},
            QStringLiteral("status"));
        if (st.isEmpty()) {
            std::puts("The backend did not answer");
            return 1;
        }
        if (!st.value(QStringLiteral("connected")).toBool()) {
            std::puts("Cooler not found: turn it on or connect cable/Bluetooth");
            return 1;
        }
        std::puts(qPrintable(QStringLiteral("model: %1 · link: %2 · firmware: %3")
                                 .arg(st.value(QStringLiteral("model")).toString(),
                                      st.value(QStringLiteral("link")).toString(),
                                      st.value(QStringLiteral("fw")).toString())));
        std::puts(qPrintable(QStringLiteral("fan: %1 → target %2 rpm")
                                 .arg(st.value(QStringLiteral("current")).toInt())
                                 .arg(st.value(QStringLiteral("target")).toInt())));
        const QString mode = st.value(QStringLiteral("mode")).toString();
        std::puts(qPrintable(QStringLiteral("mode: %1 · realtime: %2 · gear: %3 · %4")
                                 .arg(mode,
                                      st.value(QStringLiteral("realtime")).toBool()
                                          ? QStringLiteral("yes")
                                          : QStringLiteral("no"),
                                      QString::number(st.value(QStringLiteral("gear")).toInt()),
                                      st.value(QStringLiteral("asleep")).toBool()
                                          ? QStringLiteral("asleep")
                                          : QStringLiteral("running"))));
        QString supply;
        switch (st.value(QStringLiteral("supply")).toInt()) {
        case 1: supply = QStringLiteral("low"); break;
        case 2: supply = QStringLiteral("medium"); break;
        case 3: supply = QStringLiteral("full"); break;
        default: supply = QStringLiteral("measuring"); break;
        }
        std::puts(qPrintable(QStringLiteral("power: %1 (ceiling %2 rpm)")
                                 .arg(supply)
                                 .arg(st.value(QStringLiteral("rpmCeil")).toInt())));
        if (cmd == QStringLiteral("info")) {
            const QJsonArray gears = st.value(QStringLiteral("gearRpms")).toArray();
            const char *gn[4] = {"quiet", "standard", "strong", "overclock"};
            for (int i = 0; i < 4 && i < gears.size(); ++i)
                std::puts(qPrintable(QStringLiteral("gear %1 (%2): %3 rpm")
                                         .arg(i + 1)
                                         .arg(QString::fromLatin1(gn[i]))
                                         .arg(gears[i].toInt())));
        }
        return 0;
    }

    if (cmd == QStringLiteral("sensors")) {
        const QJsonObject s = roundTrip(
            QJsonObject{{QStringLiteral("request"), QStringLiteral("sensors")},
                        {QStringLiteral("type"), QStringLiteral("sensors")}},
            QStringLiteral("sensors"));
        const QJsonArray arr = s.value(QStringLiteral("sensors")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            std::puts(qPrintable(QStringLiteral("%1%2: %3 °C")
                                     .arg(o.value(QStringLiteral("chip")).toString(),
                                          o.value(QStringLiteral("label")).toString().isEmpty()
                                              ? QString()
                                              : QStringLiteral(" · ") +
                                                    o.value(QStringLiteral("label")).toString(),
                                          o.value(QStringLiteral("temp")).isNull()
                                              ? QStringLiteral("?")
                                              : QString::number(
                                                    o.value(QStringLiteral("temp")).toDouble(),
                                                    'f', 0))));
        }
        return 0;
    }

    if (cmd == QStringLiteral("set")) {
        const quint16 rpm = args.value(1).toUShort();
        if (rpm < 500 || rpm > 4000) {
            std::puts("Speed must be 500–4000");
            return 1;
        }
        return setConfig(QJsonObject{{QStringLiteral("fanMode"), QStringLiteral("fixed")},
                                     {QStringLiteral("fixedRpm"), int(rpm)}})
                       .isEmpty()
                   ? (std::puts("The backend did not answer"), 1)
                   : (std::puts("Done"), 0);
    }
    if (cmd == QStringLiteral("stop")) {
        return roundTrip(QJsonObject{{QStringLiteral("request"), QStringLiteral("stop")}},
                         QStringLiteral("reply"))
                       .isEmpty()
                   ? (std::puts("The backend did not answer"), 1)
                   : (std::puts("Done"), 0);
    }
    if (cmd == QStringLiteral("auto")) {
        return setConfig(QJsonObject{{QStringLiteral("fanMode"), QStringLiteral("gear")}})
                       .isEmpty()
                   ? (std::puts("The backend did not answer"), 1)
                   : (std::puts("Done"), 0);
    }
    if (cmd == QStringLiteral("gear")) {
        const int gear = args.value(1).toInt();
        if (gear < 1 || gear > 4) {
            std::puts("Gear must be 1–4");
            return 1;
        }
        return setConfig(QJsonObject{{QStringLiteral("fanMode"), QStringLiteral("gear")},
                                     {QStringLiteral("gear"), gear}})
                       .isEmpty()
                   ? (std::puts("The backend did not answer"), 1)
                   : (std::puts("Done"), 0);
    }
    if (cmd == QStringLiteral("standby")) {
        const int mode = args.value(1, QStringLiteral("-1")).toInt();
        if (mode < 0 || mode > 2) {
            std::puts("Standby: 0 — stay awake, 1 — at once, 2 — after a minute");
            return 1;
        }
        return setConfig(QJsonObject{{QStringLiteral("standby"), mode}})
                       .isEmpty()
                   ? (std::puts("The backend did not answer"), 1)
                   : (std::puts("Done"), 0);
    }
    if (cmd == QStringLiteral("light")) {
        const QString what = args.value(1);
        if (what == QStringLiteral("on") || what == QStringLiteral("off"))
            return setConfig(QJsonObject{{QStringLiteral("lightOn"),
                                          what == QStringLiteral("on")}})
                           .isEmpty()
                       ? (std::puts("The backend did not answer"), 1)
                       : (std::puts("Done"), 0);
        if (what == QStringLiteral("color")) {
            QString hex = args.value(2).toLower();
            if (hex.startsWith(u'#'))
                hex.remove(0, 1);
            if (hex.size() != 6 ||
                !hex.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{6}$")))) {
                std::puts("Colour is six hex digits, e.g. 00E0B8");
                return 1;
            }
            const int bri = qBound(0, args.value(3, QStringLiteral("100")).toInt(), 100);
            return setConfig(QJsonObject{{QStringLiteral("lightMode"), 0},
                                         {QStringLiteral("color"), QStringLiteral("#") + hex},
                                         {QStringLiteral("brightness"), bri}})
                           .isEmpty()
                       ? (std::puts("The backend did not answer"), 1)
                       : (std::puts("Done"), 0);
        }
        if (what == QStringLiteral("effect")) {
            const int effect = args.value(2).toInt();
            if (effect < 1 || effect > 5) {
                std::puts("Effect is a number 1–5");
                return 1;
            }
            const int bri = qBound(0, args.value(3, QStringLiteral("100")).toInt(), 100);
            return setConfig(QJsonObject{{QStringLiteral("lightMode"), effect},
                                         {QStringLiteral("brightness"), bri}})
                           .isEmpty()
                       ? (std::puts("The backend did not answer"), 1)
                       : (std::puts("Done"), 0);
        }
        std::puts("flykooler cli light on|off | color RRGGBB [brightness] | effect <1–5> [brightness]");
        return 1;
    }

    printUsage();
    return 1;
}

// ---------------- CLI directly (no backend running) ----------------

static int runCli(QCoreApplication &app, const QStringList &args)
{
    if (args.isEmpty()) {
        printUsage();
        return 1;
    }
    const QString cmd = args[0];
    quint16 rpm = 0;
    int gear = 0, effect = 0, standbyMode = -1;
    QString lightArg, colorHex;
    quint8 brightness = 100;

    if (cmd == QStringLiteral("set")) {
        if (args.size() < 2) {
            std::puts("flykooler cli set <rpm>");
            return 1;
        }
        rpm = args[1].toUShort();
        if (rpm < 500 || rpm > 4000) {
            std::puts("Speed must be 500–4000 (0 only via stop)");
            return 1;
        }
    } else if (cmd == QStringLiteral("gear")) {
        gear = args.value(1).toInt();
        if (gear < 1 || gear > 4) {
            std::puts("Gear must be 1–4");
            return 1;
        }
    } else if (cmd == QStringLiteral("standby")) {
        standbyMode = args.value(1).toInt();
        if (standbyMode < 0 || standbyMode > 2) {
            std::puts("Standby: 0 — stay awake, 1 — at once, 2 — after a minute");
            return 1;
        }
    } else if (cmd == QStringLiteral("light")) {
        lightArg = args.value(1);
        if (lightArg == QStringLiteral("color")) {
            colorHex = args.value(2).toLower();
            if (colorHex.startsWith(u'#'))
                colorHex.remove(0, 1);
            if (colorHex.size() != 6 ||
                !colorHex.contains(QRegularExpression(QStringLiteral("^[0-9a-f]{6}$")))) {
                std::puts("Colour is six hex digits, e.g. 00E0B8");
                return 1;
            }
            brightness = quint8(qBound(0, args.value(3, QStringLiteral("100")).toInt(), 100));
        } else if (lightArg == QStringLiteral("effect")) {
            effect = args.value(2).toInt();
            if (effect < 1 || effect > 5) {
                std::puts("Effect is a number 1–5");
                return 1;
            }
            brightness = quint8(qBound(0, args.value(3, QStringLiteral("100")).toInt(), 100));
        } else if (lightArg != QStringLiteral("on") && lightArg != QStringLiteral("off")) {
            std::puts("flykooler cli light on|off | color RRGGBB [brightness] | effect <1–5> [brightness]");
            return 1;
        }
    } else if (cmd != QStringLiteral("status") && cmd != QStringLiteral("info") &&
               cmd != QStringLiteral("auto") && cmd != QStringLiteral("stop")) {
        printUsage();
        return 1;
    }

    Device dev;
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(12000);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);

    QString fw, openError;
    bool haveFw = false, haveGears = false, haveStatus = false, connectedOnce = false;
    quint8 wantAck = 0;
    Status st;
    Model model = Model::Unknown;
    Transport transport = Transport::Bluetooth;

    QObject::connect(&dev, &Device::openFailed,
                     [&openError](const QString &, const QString &err) { openError = err; });

    QObject::connect(&dev, &Device::connected, [&](Model m, Transport t, const QString &) {
        connectedOnce = true;
        model = m;
        transport = t;
        if (cmd == QStringLiteral("set")) {
            wantAck = Cmd::SetRealtimeRpm;
            dev.sendCommand(Cmd::EnterRealtime);
            dev.sendCommand(Cmd::SetRealtimeRpm, payloadLe16(rpm));
        } else if (cmd == QStringLiteral("stop")) {
            wantAck = Cmd::SetRealtimeRpm;
            dev.sendCommand(Cmd::EnterRealtime);
            dev.sendCommand(Cmd::SetRealtimeRpm, payloadLe16(0));
        } else if (cmd == QStringLiteral("auto")) {
            wantAck = Cmd::ExitRealtime;
            dev.sendCommand(Cmd::ExitRealtime);
        } else if (cmd == QStringLiteral("gear")) {
            wantAck = Cmd::SelectGear;
            dev.sendCommand(Cmd::SelectGear, payload1(quint8(gear)));
        } else if (cmd == QStringLiteral("standby")) {
            wantAck = Cmd::Standby;
            dev.sendCommand(Cmd::Standby, payload1(quint8(standbyMode)));
        } else if (cmd == QStringLiteral("light")) {
            if (lightArg == QStringLiteral("on") || lightArg == QStringLiteral("off")) {
                wantAck = Cmd::StripPower;
                dev.sendCommand(Cmd::StripPower,
                                payload1(lightArg == QStringLiteral("on") ? 0x01 : 0x00));
            } else {
                wantAck = Cmd::LightCommit;
                QByteArray buf;
                if (lightArg == QStringLiteral("color")) {
                    bool okR = false, okG = false, okB = false;
                    const quint8 r = quint8(colorHex.mid(0, 2).toUInt(&okR, 16));
                    const quint8 g = quint8(colorHex.mid(2, 2).toUInt(&okG, 16));
                    const quint8 b = quint8(colorHex.mid(4, 2).toUInt(&okB, 16));
                    buf = lightBuffer(LightPreset::Static, r, g, b, brightness);
                } else {
                    buf = lightBuffer(LightPreset(int(LightPreset::Effect1) + effect - 1),
                                      0, 0, 0, brightness);
                }
                dev.sendCommand(Cmd::LightBegin);
                for (int at = 0; at < buf.size(); at += 15)
                    dev.sendCommand(Cmd::LightBlock, buf.mid(at, 15));
                dev.sendCommand(Cmd::LightCommit, payload1(0x01));
            }
        }
    });

    QObject::connect(&dev, &Device::statusUpdated, [&](const Status &s) {
        st = s;
        haveStatus = true;
        if (cmd == QStringLiteral("status"))
            loop.quit();
    });

    QObject::connect(&dev, &Device::ackReceived, [&](quint8 c, const QByteArray &p) {
        if (c == Cmd::FwVersion && p.size() >= 4) {
            fw = QStringLiteral("%1.%2.%3.%4")
                     .arg(quint8(p[0]))
                     .arg(quint8(p[1]))
                     .arg(quint8(p[2]))
                     .arg(quint8(p[3]), 2, 10, QChar(u'0'));
            haveFw = true;
        } else if (c == Cmd::Supply && p.size() >= 1) {
            st.supplyLevel = quint8(p[0]);
        } else if (c == Cmd::QueryGearTable && p.size() >= 8) {
            haveGears = true;
        } else if (c == Cmd::QueryRpm && p.size() >= 4) {
            st.currentRpm = quint8(p[0]) | (quint8(p[1]) << 8);
            st.targetRpm = quint8(p[2]) | (quint8(p[3]) << 8);
            haveStatus = true;
            if (cmd == QStringLiteral("status"))
                loop.quit();
        }
        if (cmd == QStringLiteral("info") && haveGears)
            loop.quit();
        if (wantAck && c == wantAck)
            loop.quit();
    });

    dev.start();
    watchdog.start();
    loop.exec();
    Q_UNUSED(app);

    if (!connectedOnce) {
        std::puts(openError.isEmpty()
                      ? "Cooler not found: turn it on or connect cable/Bluetooth"
                      : qPrintable(QStringLiteral("Cannot open the device: %1").arg(openError)));
        return 1;
    }

    const QString gearNames[4] = {QStringLiteral("quiet"), QStringLiteral("standard"),
                                  QStringLiteral("strong"), QStringLiteral("overclock")};
    if (cmd == QStringLiteral("status")) {
        if (!haveStatus) {
            std::puts("The cooler is silent — could not read the status");
            return 1;
        }
        QString link;
        if (st.usbUp && st.bleUp)
            link = QStringLiteral("cable + Bluetooth");
        else if (st.usbUp)
            link = QStringLiteral("cable");
        else if (st.bleUp)
            link = QStringLiteral("Bluetooth");
        QString supply;
        switch (st.supplyLevel) {
        case 1: supply = QStringLiteral("low (ceiling 2700)"); break;
        case 2: supply = QStringLiteral("medium (ceiling 3300)"); break;
        case 3: supply = QStringLiteral("full (ceiling 4000)"); break;
        default: supply = QStringLiteral("measuring"); break;
        }
        std::puts(qPrintable(QStringLiteral("model: %1 · link: %2")
                                 .arg(QString::fromLatin1(modelName(model)), link)));
        std::puts(qPrintable(QStringLiteral("fan: %1 → target %2 rpm")
                                 .arg(st.currentRpm)
                                 .arg(st.targetRpm)));
        std::puts(qPrintable(QStringLiteral("mode: %1 · gear: %2 (%3) · %4")
                                 .arg(st.realtime ? QStringLiteral("realtime")
                                                  : QStringLiteral("gears"),
                                      QString::number(st.gear),
                                      gearNames[qBound(1, st.gear, 4) - 1],
                                      st.asleep ? QStringLiteral("asleep")
                                                : QStringLiteral("running"))));
        std::puts(qPrintable(QStringLiteral("power: %1").arg(supply)));
        return 0;
    }

    if (cmd == QStringLiteral("info")) {
        std::puts(qPrintable(QStringLiteral("model: %1 · link: %2 · firmware: %3")
                                 .arg(QString::fromLatin1(modelName(model)),
                                      transport == Transport::Usb
                                          ? QStringLiteral("cable")
                                          : QStringLiteral("Bluetooth"),
                                      haveFw ? fw : QStringLiteral("?"))));
        if (haveStatus)
            std::puts(qPrintable(QStringLiteral("fan: %1 → target %2 rpm")
                                     .arg(st.currentRpm)
                                     .arg(st.targetRpm)));
        return 0;
    }

    std::puts("Done");
    return 0;
}

// ---------------- main ----------------

int main(int argc, char *argv[])
{
    if (argc >= 2 && qstrcmp(argv[1], "cli") == 0) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("flykooler"));
        QStringList args;
        for (int i = 2; i < argc; ++i)
            args << QString::fromLocal8Bit(argv[i]);
        if (args.isEmpty()) {
            printUsage();
            return 1;
        }
        const int ipc = runIpcCli(args);
        if (ipc >= 0)
            return ipc; // the backend answered — it owns the cooler
        return runCli(app, args); // no backend — talk to the device directly
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("flykooler"));
    QApplication::setOrganizationName(QStringLiteral("flykooler"));
    QApplication::setQuitOnLastWindowClosed(false);

    // UI translations (ru_RU and friends), embedded at :/i18n
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("flykooler"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        app.installTranslator(&translator);

    // One window per user: a second launch raises the existing one.
    const QString guiSock = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                            QStringLiteral("/flykooler-gui.sock");
    QLockFile lock(QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                   QStringLiteral("/flykooler.lock"));
    if (!lock.tryLock(100)) {
        QLocalSocket sock;
        sock.connectToServer(guiSock);
        if (sock.waitForConnected(300)) {
            sock.write("show");
            sock.flush();
            sock.waitForBytesWritten(300);
        }
        std::puts("Flydigi Kooler is already running — window raised");
        return 0;
    }

    MainWindow window;
    window.show();

    QLocalServer server;
    QLocalServer::removeServer(guiSock);
    server.listen(guiSock);
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        QLocalSocket *client = server.nextPendingConnection();
        if (!client)
            return;
        QObject::connect(client, &QLocalSocket::disconnected, client,
                         &QLocalSocket::deleteLater);
        QObject::connect(client, &QLocalSocket::readyRead, client, [client, &window] {
            if (client->readAll().contains("show")) {
                window.showNormal();
                window.raise();
                window.activateWindow();
            }
        });
    });

    return app.exec();
}
