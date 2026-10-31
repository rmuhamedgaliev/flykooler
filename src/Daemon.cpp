// SPDX-License-Identifier: MIT

#include "Daemon.h"
#include "Sensors.h"
#include "Shared.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>

using namespace FlyKooler;

Daemon::Daemon(QObject *parent)
    : QObject(parent),
      m_device(this)
{
    connect(&m_device, &Device::connected, this, &Daemon::onDeviceConnected);
    connect(&m_device, &Device::disconnected, this, &Daemon::onDeviceDisconnected);
    connect(&m_device, &Device::statusUpdated, this, &Daemon::onStatus);
    connect(&m_device, &Device::ackReceived, this, &Daemon::onAck);
    // Commands the cooler never answered: visible via
    // journalctl --user -u flykoolerd
    connect(&m_device, &Device::commandTimedOut, this, [this](quint8 cmd) {
        qWarning("no reply to command 0x%02X", cmd);
        if (cmd == Cmd::LightBegin || cmd == Cmd::LightBlock || cmd == Cmd::LightCommit) {
            // the upload did not complete — retry with the next change
            m_lightReady = false;
        }
    });

    connect(&m_smartTimer, &QTimer::timeout, this, &Daemon::smartTick);
    m_smartTimer.setInterval(2500);

    loadSettings();
}

Daemon::StartResult Daemon::start()
{
    qInfo("Flydigi Kooler backend %s", FLYKOOLER_VERSION);

    const QString socket = daemonSocketPath();
    auto lock = std::make_unique<QLockFile>(socket + QStringLiteral(".lock"));
    // A crashed daemon must not block the next start for the default 30 s.
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) {
        qWarning("another backend is running (lock %s)", qPrintable(lock->fileName()));
        return StartResult::AlreadyRunning;
    }

    // Older builds do not take the lock. If something still answers on the
    // socket, leave it alone instead of unlinking a live server's file.
    QLocalSocket probe;
    probe.connectToServer(socket);
    if (probe.waitForConnected(200)) {
        probe.abort();
        qWarning("socket %s is busy — is the daemon already running?", qPrintable(socket));
        return StartResult::AlreadyRunning;
    }

    // The lock is ours, so a socket file left behind is stale.
    QLocalServer::removeServer(socket);
    if (!m_server.listen(socket)) {
        qWarning("cannot listen on %s: %s", qPrintable(socket),
                 qPrintable(m_server.errorString()));
        return StartResult::Failed;
    }
    m_lock = std::move(lock);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            m_clients.push_back(client);
            connect(client, &QLocalSocket::disconnected, this, [this, client] {
                m_clients.removeAll(client);
                client->deleteLater();
            });
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                while (client->canReadLine()) {
                    const QByteArray line = client->readLine();
                    QJsonParseError err{};
                    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
                    if (err.error != QJsonParseError::NoError || !doc.isObject())
                        continue;
                    handleRequest(client, doc.object());
                }
            });
            // a fresh client gets status, config and sensors right away
            sendTo(client, statusJson());
            sendTo(client, QJsonObject{{QStringLiteral("type"),
                                        QStringLiteral("config")},
                                       {QStringLiteral("config"), m_cfg.toJson()}});
            sendTo(client, sensorsJson());
        }
    });

    m_device.start();
    m_smartTimer.start();
    return StartResult::Started;
}

void Daemon::loadSettings()
{
    QSettings s(QStringLiteral("flykooler"), QStringLiteral("flykooler"));
    Config c;
    c.fanMode = s.value(QStringLiteral("fan/mode"), QStringLiteral("smart")).toString();
    if (c.fanMode != QStringLiteral("smart") && c.fanMode != QStringLiteral("fixed") &&
        c.fanMode != QStringLiteral("gear"))
        c.fanMode = QStringLiteral("smart");
    c.fixedRpm = s.value(QStringLiteral("fan/fixedRpm"), 1500).toInt();
    c.gear = s.value(QStringLiteral("fan/gear"), 2).toInt();
    c.sensorChip = s.value(QStringLiteral("fan/sensorChip")).toString();
    c.sensorLabel = s.value(QStringLiteral("fan/sensorLabel")).toString();
    c.curve.clear();
    const QString raw = s.value(
        QStringLiteral("fan/curve"),
        QStringLiteral("40:700;50:1000;60:1600;70:2100;80:2600;90:3000")).toString();
    for (const QString &pt : raw.split(u';')) {
        const QStringList kv = pt.split(u':');
        if (kv.size() == 2)
            c.curve.push_back({kv[0].toInt(), kv[1].toInt()});
    }
    if (c.curve.isEmpty())
        c.curve = Config::defaultCurve();

    c.lightOn = s.value(QStringLiteral("light/on"), true).toBool();
    c.lightMode = s.value(QStringLiteral("light/mode"), 0).toInt();
    c.color = s.value(QStringLiteral("light/color"), QStringLiteral("#00e0b8")).toString();
    c.brightness = s.value(QStringLiteral("light/brightness"), 60).toInt();
    c.gearLeds = s.value(QStringLiteral("light/gearLeds"), true).toBool();
    c.standby = s.value(QStringLiteral("system/standby"), 0).toInt();
    c.wake = s.value(QStringLiteral("system/wake"), true).toBool();
    c.autostart = s.value(QStringLiteral("system/autostart"), false).toBool();

    m_cfg = c;
}

void Daemon::saveSettings()
{
    QSettings s(QStringLiteral("flykooler"), QStringLiteral("flykooler"));
    s.setValue(QStringLiteral("fan/mode"), m_cfg.fanMode);
    s.setValue(QStringLiteral("fan/fixedRpm"), m_cfg.fixedRpm);
    s.setValue(QStringLiteral("fan/gear"), m_cfg.gear);
    s.setValue(QStringLiteral("fan/sensorChip"), m_cfg.sensorChip);
    s.setValue(QStringLiteral("fan/sensorLabel"), m_cfg.sensorLabel);
    QStringList pts;
    for (const auto &p : m_cfg.curve)
        pts << QStringLiteral("%1:%2").arg(p.first).arg(p.second);
    s.setValue(QStringLiteral("fan/curve"), pts.join(u';'));

    s.setValue(QStringLiteral("light/on"), m_cfg.lightOn);
    s.setValue(QStringLiteral("light/mode"), m_cfg.lightMode);
    s.setValue(QStringLiteral("light/color"), m_cfg.color);
    s.setValue(QStringLiteral("light/brightness"), m_cfg.brightness);
    s.setValue(QStringLiteral("light/gearLeds"), m_cfg.gearLeds);
    s.setValue(QStringLiteral("system/standby"), m_cfg.standby);
    s.setValue(QStringLiteral("system/wake"), m_cfg.wake);
    s.setValue(QStringLiteral("system/autostart"), m_cfg.autostart);
    s.sync(); // QSettings otherwise only writes on exit — and the daemon never exits
}

void Daemon::onDeviceConnected(Model model, Transport transport, const QString &path)
{
    Q_UNUSED(path);
    m_connected = true;
    m_model = model;
    m_transport = transport;
    m_inRealtime = false;
    m_lastSentRpm = -1;
    m_lightReady = false;
    qInfo("cooler connected: %s", modelName(model));
    broadcast(statusJson());
    applyConfig(m_cfg);
    m_device.sendCommand(Cmd::Standby, payload1(quint8(m_cfg.standby))); // stored in the cooler
}

void Daemon::onDeviceDisconnected()
{
    m_connected = false;
    m_inRealtime = false;
    qInfo("cooler link lost");
    broadcast(statusJson());
}

void Daemon::onStatus(const Status &status)
{
    m_status = status;
    if (!status.realtime)
        m_inRealtime = false;
    broadcast(statusJson());
}

void Daemon::onAck(quint8 cmd, const QByteArray &payload)
{
    switch (cmd) {
    case Cmd::FwVersion:
        if (payload.size() >= 4) {
            m_fw = QStringLiteral("%1.%2.%3.%4")
                       .arg(quint8(payload[0]))
                       .arg(quint8(payload[1]))
                       .arg(quint8(payload[2]))
                       .arg(quint8(payload[3]), 2, 10, QChar(u'0'));
            broadcast(statusJson());
        }
        break;
    case Cmd::PowerState:
        if (payload.size() >= 1 && quint8(payload[0]) == 0x02 && m_cfg.wake)
            m_device.sendCommand(Cmd::Wake);
        break;
    case Cmd::QueryGearTable:
        if (payload.size() >= 8) {
            for (int i = 0; i < 4; ++i)
                m_gearRpm[i] = quint8(payload[i * 2]) | (quint8(payload[i * 2 + 1]) << 8);
            broadcast(statusJson());
        }
        break;
    default:
        break;
    }
}

void Daemon::applyConfig(const Config &previous)
{
    if (!m_connected)
        return;

    // the light changed or was never applied — re-upload (flash only on change)
    if (!m_lightReady || previous.lightOn != m_cfg.lightOn ||
        previous.lightMode != m_cfg.lightMode || previous.color != m_cfg.color ||
        previous.brightness != m_cfg.brightness) {
        applyLighting(true);
    } else {
        applyLighting(false);
    }
    if (previous.gearLeds != m_cfg.gearLeds)
        applyLighting(false);

    if (previous.standby != m_cfg.standby)
        m_device.sendCommand(Cmd::Standby, payload1(quint8(m_cfg.standby)));

    applyFanMode();
}

void Daemon::applyFanMode()
{
    if (!m_connected)
        return;
    if (m_cfg.fanMode == QStringLiteral("gear")) {
        m_inRealtime = false;
        m_device.sendCommand(Cmd::ExitRealtime);
        m_device.sendCommand(Cmd::SelectGear, payload1(quint8(m_cfg.gear)));
        return;
    }
    if (m_cfg.fanMode == QStringLiteral("fixed")) {
        int rpm = m_cfg.fixedRpm;
        if (m_status.supplyLevel > 0 && rpm > m_status.maxRpm())
            rpm = m_status.maxRpm();
        if (!m_inRealtime) {
            m_device.sendCommand(Cmd::EnterRealtime);
            m_inRealtime = true;
        }
        m_device.sendCommand(Cmd::SetRealtimeRpm, payloadLe16(quint16(rpm)));
        m_lastSentRpm = rpm;
        return;
    }
    // smart: the next curve tick enters realtime and sets the speed
    m_inRealtime = false;
    smartTick();
}

void Daemon::applyLighting(bool force)
{
    if (!m_connected)
        return;
    m_device.sendCommand(Cmd::StripPower, payload1(m_cfg.lightOn ? 0x01 : 0x00));
    m_device.sendCommand(Cmd::GearLeds, payload1(m_cfg.gearLeds ? 0x01 : 0x00));

    if (m_cfg.lightOn && (force || !m_lightReady)) {
        // "#rrggbb" parsed by hand: QtGui has no place in the backend
        quint8 r = 0, g = 0, b = 0;
        if (m_cfg.color.size() == 7) {
            r = quint8(m_cfg.color.mid(1, 2).toUInt(nullptr, 16));
            g = quint8(m_cfg.color.mid(3, 2).toUInt(nullptr, 16));
            b = quint8(m_cfg.color.mid(5, 2).toUInt(nullptr, 16));
        }
        const LightPreset preset = m_cfg.lightMode == 0
            ? LightPreset::Static
            : LightPreset(int(LightPreset::Effect1) + m_cfg.lightMode - 1);
        const QByteArray buf = lightBuffer(preset, r, g, b, quint8(m_cfg.brightness));
        qInfo("lighting upload: mode %d, %d blocks", m_cfg.lightMode,
              int((buf.size() + 14) / 15) + 2);
        m_device.sendCommand(Cmd::LightBegin);
        for (int at = 0; at < buf.size(); at += 15)
            m_device.sendCommand(Cmd::LightBlock, buf.mid(at, 15));
        m_device.sendCommand(Cmd::LightCommit, payload1(0x01)); // flash — only on change
        m_lightReady = true;
    }
}

void Daemon::smartTick()
{
    if (!m_connected || m_cfg.fanMode != QStringLiteral("smart"))
        return;

    double t = std::numeric_limits<double>::quiet_NaN();
    for (const Sensor &s : listSensors()) {
        if (s.chip != m_cfg.sensorChip ||
            (!m_cfg.sensorLabel.isEmpty() && s.label != m_cfg.sensorLabel))
            continue;
        const double v = readTemperature(s.path);
        if (!std::isnan(v) && (std::isnan(t) || v > t))
            t = v;
    }
    if (std::isnan(t))
        t = m_lastTemp;
    if (std::isnan(t))
        return;
    m_lastTemp = t;

    int rpm = m_cfg.rpmFor(t);
    if (m_status.supplyLevel > 0 && rpm > m_status.maxRpm())
        rpm = m_status.maxRpm();
    if (rpm < 500)
        rpm = 500;

    if (!m_inRealtime) {
        m_device.sendCommand(Cmd::EnterRealtime);
        m_inRealtime = true;
    }
    if (qAbs(rpm - m_lastSentRpm) >= 50) {
        m_device.sendCommand(Cmd::SetRealtimeRpm, payloadLe16(quint16(rpm)));
        m_lastSentRpm = rpm;
    }
}

QJsonObject Daemon::statusJson() const
{
    QString transport = QStringLiteral("none");
    if (m_connected)
        transport = m_transport == Transport::Usb ? QStringLiteral("cable")
                                                  : QStringLiteral("bluetooth");

    QString link;
    if (m_status.usbUp && m_status.bleUp)
        link = QStringLiteral("cable + Bluetooth");
    else if (m_status.usbUp)
        link = QStringLiteral("cable");
    else if (m_status.bleUp)
        link = QStringLiteral("Bluetooth");

    QJsonArray gearTable;
    for (int i = 0; i < 4; ++i)
        gearTable.append(m_gearRpm[i]);

    QJsonObject o{
        {QStringLiteral("type"), QStringLiteral("status")},
        {QStringLiteral("connected"), m_connected},
        {QStringLiteral("model"), QString::fromLatin1(modelName(m_model))},
        {QStringLiteral("transport"), transport},
        {QStringLiteral("link"), link},
        {QStringLiteral("fw"), m_fw},
        {QStringLiteral("current"), m_status.currentRpm},
        {QStringLiteral("target"), m_status.targetRpm},
        {QStringLiteral("mode"), m_cfg.fanMode},
        {QStringLiteral("realtime"), m_status.realtime},
        {QStringLiteral("gear"), m_status.gear},
        {QStringLiteral("supply"), m_status.supplyLevel},
        {QStringLiteral("rpmCeil"), m_status.maxRpm()},
        {QStringLiteral("asleep"), m_status.asleep},
        {QStringLiteral("gearRpms"), gearTable},
    };
    if (!std::isnan(m_lastTemp)) {
        o.insert(QStringLiteral("temp"), m_lastTemp);
        o.insert(QStringLiteral("smartRpm"), m_lastSentRpm < 0 ? 0 : m_lastSentRpm);
    }
    return o;
}

QJsonObject Daemon::sensorsJson()
{
    QJsonArray arr;
    for (const Sensor &s : listSensors()) {
        const double t = readTemperature(s.path);
        arr.append(QJsonObject{
            {QStringLiteral("chip"), s.chip},
            {QStringLiteral("label"), s.label},
            {QStringLiteral("temp"), std::isnan(t) ? QJsonValue() : QJsonValue(t)},
        });
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("sensors")},
                       {QStringLiteral("sensors"), arr}};
}

void Daemon::handleRequest(QLocalSocket *client, const QJsonObject &request)
{
    const QString req = request.value(QStringLiteral("request")).toString();

    if (req == QStringLiteral("status")) {
        sendTo(client, statusJson());
    } else if (req == QStringLiteral("config")) {
        sendTo(client, QJsonObject{{QStringLiteral("type"), QStringLiteral("config")},
                                   {QStringLiteral("config"), m_cfg.toJson()}});
    } else if (req == QStringLiteral("sensors")) {
        sendTo(client, sensorsJson());
    } else if (req == QStringLiteral("stop")) {
        if (m_connected) {
            m_device.sendCommand(Cmd::EnterRealtime);
            m_inRealtime = true;
            m_device.sendCommand(Cmd::SetRealtimeRpm, payloadLe16(0));
            m_lastSentRpm = 0;
        }
        sendTo(client, QJsonObject{{QStringLiteral("type"), QStringLiteral("reply")},
                                   {QStringLiteral("ok"), true}});
    } else if (req == QStringLiteral("wake")) {
        if (m_connected)
            m_device.sendCommand(Cmd::Wake);
        sendTo(client, QJsonObject{{QStringLiteral("type"), QStringLiteral("reply")},
                                   {QStringLiteral("ok"), true}});
    } else if (req == QStringLiteral("gear_rpm")) {
        const int slot = request.value(QStringLiteral("slot")).toInt(-1);
        const int rpm = request.value(QStringLiteral("rpm")).toInt(0);
        if (slot >= 0 && slot <= 3 && rpm >= 500 && rpm <= 4000 && m_connected) {
            QByteArray p;
            p.append(char(slot));
            p.append(char(rpm & 0xFF));
            p.append(char((rpm >> 8) & 0xFF));
            m_device.sendCommand(Cmd::SetGearRpm, p);
            m_gearRpm[slot] = rpm;
            broadcast(statusJson());
        }
        sendTo(client, QJsonObject{{QStringLiteral("type"), QStringLiteral("reply")},
                                   {QStringLiteral("ok"), true}});
    } else if (req == QStringLiteral("set_config")) {
        const Config previous = m_cfg;
        m_cfg = Config::fromJson(request.value(QStringLiteral("config")).toObject(), m_cfg);
        if (m_cfg.fanMode == QStringLiteral("fixed"))
            m_lastSentRpm = -1;
        saveSettings();
        applyConfig(previous);
        broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("config")},
                              {QStringLiteral("config"), m_cfg.toJson()}});
        sendTo(client, QJsonObject{{QStringLiteral("type"), QStringLiteral("reply")},
                                   {QStringLiteral("ok"), true}});
    } else if (req == QStringLiteral("quit")) {
        QCoreApplication::quit();
    }
}

void Daemon::sendTo(QLocalSocket *client, const QJsonObject &message)
{
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    client->write(data);
}

void Daemon::broadcast(const QJsonObject &message)
{
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    for (QLocalSocket *client : m_clients)
        client->write(data);
}
