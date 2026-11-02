// SPDX-License-Identifier: MIT
// The flykoolerd backend: the single owner of the cooler. It keeps the link
// (cable or Bluetooth), applies the configuration, runs the temperature fan
// curve and serves GUI/CLI clients over a Unix socket (newline-delimited
// JSON).

#pragma once

#include "Device.h"
#include "Protocol.h"
#include "Shared.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include <memory>

namespace FlyKooler {

class Daemon : public QObject
{
    Q_OBJECT

public:
    enum class StartResult { Started, AlreadyRunning, Failed };

    explicit Daemon(QObject *parent = nullptr);

    // Listens on the socket. A second instance must not steal the socket from
    // a running one: the cooler would then be driven by two processes.
    StartResult start();

    // Stop the fan and the light and send the cooler to sleep before the host
    // suspends or powers off. Safe to call more than once.
    void powerOffCooler();

private slots:
    void onPrepareForSleep(bool start);
    void onPrepareForShutdown(bool start);

private:
    void watchLogind();
    void sleepCooler();
    void resumeCooler();

    void loadSettings();
    void saveSettings();

    void onDeviceConnected(Model model, Transport transport, const QString &path);
    void onDeviceDisconnected();
    void onStatus(const Status &status);
    void onAck(quint8 cmd, const QByteArray &payload);

    void applyConfig(const Config &previous);
    void applyFanMode();
    void applyLighting(bool force);
    void smartTick();

    void broadcast(const QJsonObject &message);
    QJsonObject statusJson() const;
    QJsonObject sensorsJson();
    void handleRequest(QLocalSocket *client, const QJsonObject &request);
    void sendTo(QLocalSocket *client, const QJsonObject &message);

    Device m_device;
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock; // held for the process lifetime
    QList<QLocalSocket *> m_clients;
    QTimer m_smartTimer;

    Config m_cfg;
    bool m_lightReady = false; // the strip holds our buffer already

    bool m_connected = false;
    bool m_hostOff = false; // sleep was requested because the host went down
    Status m_status;
    Model m_model = Model::Unknown;
    Transport m_transport = Transport::Bluetooth;
    QString m_fw;
    int m_gearRpm[4] = {0, 0, 0, 0};
    bool m_inRealtime = false;
    int m_lastSentRpm = -1;
    double m_lastTemp = qQNaN();
};

} // namespace FlyKooler
