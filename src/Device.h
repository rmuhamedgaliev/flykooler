// SPDX-License-Identifier: MIT
// Cooler driver: discovery via /sys/class/hidraw, a command queue with
// acknowledgements, the 0xEF status stream, and automatic reconnection when
// the cable or Bluetooth link comes and goes. Runs on its own thread (or on
// the main thread in CLI mode — it only needs an event loop).

#pragma once

#include "Protocol.h"

#include <QByteArray>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <deque>

namespace FlyKooler {

struct FoundDevice {
    QString path;
    Model model = Model::Unknown;
    Transport transport = Transport::Bluetooth;
};

std::vector<FoundDevice> scanDevices();

class Device : public QObject
{
    Q_OBJECT

public:
    explicit Device(QObject *parent = nullptr);
    ~Device() override;

    bool isOpen() const { return m_open; }

    // Slots: the GUI invokes these from another thread via invokeMethod.
public slots:
    void start();
    void stop();
    void sendCommand(quint8 cmd, const QByteArray &payload = {}, bool expectAck = true);

signals:
    void connected(FlyKooler::Model model, FlyKooler::Transport transport, const QString &path);
    void disconnected();
    void openFailed(const QString &path, const QString &error);
    void statusUpdated(FlyKooler::Status status);
    void ackReceived(quint8 cmd, const QByteArray &payload);
    void commandTimedOut(quint8 cmd);

private slots:
    void pollHotplug();
    void readReady();
    void tryDequeue();

private:
    struct Item {
        QByteArray frame;
        bool expectAck = true;
    };

    void openDevice(const FoundDevice &found);
    void closeDevice();
    void onAckTimeout();

    enum class WriteResult { Written, Retry, Error };
    WriteResult writeNow(const Item &item);

    // hidraw is read non-blocking only: one report per read(), otherwise a
    // read would park the thread until the next frame arrives.
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_hotplugTimer;
    QTimer m_backupTimer;

    bool m_open = false;
    Model m_model = Model::Unknown;
    Transport m_transport = Transport::Bluetooth;
    QString m_path;

    std::deque<Item> m_queue;
    quint8 m_pendingCmd = 0;
    QTimer m_ackTimer;

    // Pacing: the firmware digests one frame per 5 ms.
    qint64 m_lastWriteMs = 0;

    Status m_lastStatus;
    qint64 m_lastStatusMs = 0;
    bool m_writeScheduled = false;
};

} // namespace FlyKooler

Q_DECLARE_METATYPE(FlyKooler::Model)
Q_DECLARE_METATYPE(FlyKooler::Transport)
Q_DECLARE_METATYPE(FlyKooler::Status)
