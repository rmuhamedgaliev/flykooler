// SPDX-License-Identifier: MIT

#include "Device.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace FlyKooler {
namespace {

constexpr int AckTimeoutMs = 1500;
constexpr int WriteGapMs = 6;      // the firmware digests frames at most one per 5 ms
constexpr int HotplugMs = 2000;
constexpr int BackupPollMs = 3000; // 0x22 poll while the 0xEF stream is silent

QByteArray readFileTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll().trimmed();
}

} // namespace

std::vector<FoundDevice> scanDevices()
{
    std::vector<FoundDevice> found;
    const QDir sysfs(QStringLiteral("/sys/class/hidraw"));
    const QStringList nodes = sysfs.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : nodes) {
        const QString text = QString::fromLatin1(
            readFileTrimmed(sysfs.filePath(name + QStringLiteral("/device/uevent"))));
        const qsizetype at = text.indexOf(QStringLiteral("HID_ID="));
        if (at < 0)
            continue;
        QString hidId = text.mid(at + 7);
        const qsizetype nl = hidId.indexOf(u'\n');
        if (nl >= 0)
            hidId.truncate(nl);
        hidId = hidId.trimmed();
        const QStringList parts = hidId.split(u':');
        if (parts.size() < 3)
            continue;
        bool okBus = false, okVendor = false, okProduct = false;
        const quint32 bus = parts[0].toUInt(&okBus, 16);
        const quint32 vendor = parts[1].toUInt(&okVendor, 16);
        const quint32 product = parts[2].toUInt(&okProduct, 16);
        if (!okBus || !okVendor || !okProduct || vendor != 0x37D7)
            continue;
        const Model model = modelFromPid(quint16(product));
        if (model == Model::Unknown)
            continue;
        found.push_back({QStringLiteral("/dev/") + name, model,
                         bus == 0x03 ? Transport::Usb : Transport::Bluetooth});
    }
    // A cable beats the air: USB first.
    std::sort(found.begin(), found.end(), [](const FoundDevice &a, const FoundDevice &b) {
        return int(a.transport) < int(b.transport);
    });
    return found;
}

Device::Device(QObject *parent)
    : QObject(parent)
{
    m_ackTimer.setSingleShot(true);
    m_ackTimer.setInterval(AckTimeoutMs);
    connect(&m_ackTimer, &QTimer::timeout, this, &Device::onAckTimeout);

    connect(&m_backupTimer, &QTimer::timeout, this, [this] {
        // If the cooler does not push 0xEF frames (or the stream stalled),
        // ask for the rpm directly.
        if (!m_open || !m_queue.empty() || m_pendingCmd)
            return;
        if (QDateTime::currentMSecsSinceEpoch() - m_lastStatusMs < 6000)
            return;
        sendCommand(Cmd::QueryRpm);
    });
    m_backupTimer.setInterval(BackupPollMs);

    connect(&m_hotplugTimer, &QTimer::timeout, this, &Device::pollHotplug);
    m_hotplugTimer.setInterval(HotplugMs);
}

Device::~Device()
{
    closeDevice();
}

void Device::start()
{
    pollHotplug();
    m_hotplugTimer.start();
    m_backupTimer.start();
}

void Device::stop()
{
    m_hotplugTimer.stop();
    m_backupTimer.stop();
    closeDevice();
}

void Device::pollHotplug()
{
    if (m_open) {
        if (!QFileInfo::exists(m_path)) {
            closeDevice();
            emit disconnected();
        }
        return;
    }
    const auto found = scanDevices();
    if (!found.empty())
        openDevice(found.front());
}

void Device::openDevice(const FoundDevice &found)
{
    if (m_open)
        closeDevice();

    // O_NONBLOCK only: a blocking hidraw read would wait for the next
    // report (one per 500 ms) with the whole thread frozen.
    m_fd = ::open(QFile::encodeName(found.path), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        emit openFailed(found.path, QString::fromLatin1(strerror(errno)));
        return;
    }

    m_open = true;
    m_model = found.model;
    m_transport = found.transport;
    m_path = found.path;
    m_lastStatusMs = QDateTime::currentMSecsSinceEpoch();

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { readReady(); });

    emit connected(m_model, m_transport, m_path);

    sendCommand(Cmd::PowerState);
    sendCommand(Cmd::FwVersion);
    sendCommand(Cmd::Supply);
    sendCommand(Cmd::QueryGearTable);
    sendCommand(Cmd::QueryStrip);
}

void Device::closeDevice()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_open = false;
    m_pendingCmd = 0;
    m_ackTimer.stop();
    m_queue.clear();
}

void Device::cancelPending()
{
    m_queue.clear();
    m_pendingCmd = 0;
    m_ackTimer.stop();
}

void Device::sendCommand(quint8 cmd, const QByteArray &payload, bool expectAck)
{
    // 0xDF erases the firmware, 0x06 is a factory reset: never send either.
    if (cmd == 0xDF || cmd == 0x06)
        return;
    if (cmd == Cmd::SelectGear &&
        (payload.isEmpty() || quint8(payload[0]) < 1 || quint8(payload[0]) > 4))
        return; // outside 01..04 the firmware corrupts the gear table

    m_queue.push_back(Item{buildFrame(cmd, payload), expectAck});
    tryDequeue();
}

void Device::tryDequeue()
{
    if (!m_open || m_queue.empty() || m_pendingCmd || m_writeScheduled)
        return;

    const qint64 since = QDateTime::currentMSecsSinceEpoch() - m_lastWriteMs;
    if (since < WriteGapMs) {
        m_writeScheduled = true;
        QTimer::singleShot(WriteGapMs - int(since), this, [this] {
            m_writeScheduled = false;
            tryDequeue();
        });
        return;
    }

    const Item item = std::move(m_queue.front());
    m_queue.pop_front();
    switch (writeNow(item)) {
    case WriteResult::Written:
        break;
    case WriteResult::Retry:
        return;
    case WriteResult::Error:
        return;
    }

    if (item.expectAck) {
        m_pendingCmd = quint8(item.frame[2]); // third frame byte is the command
        m_ackTimer.start();
    } else {
        tryDequeue();
    }
}

Device::WriteResult Device::writeNow(const Item &item)
{
    // USB: 31 bytes, the frame starts at the magic. Bluetooth: 25 bytes with
    // report id 0x02 in front.
    QByteArray report;
    if (m_transport == Transport::Usb) {
        report.resize(31);
        report.fill(char(0));
        const int n = qMin<int>(item.frame.size(), 31);
        memcpy(report.data(), item.frame.constData(), n);
    } else {
        report.resize(25);
        report.fill(char(0));
        report[0] = char(0x02);
        const int n = qMin<int>(item.frame.size(), 24);
        memcpy(report.data() + 1, item.frame.constData(), n);
    }

    const ssize_t n = ::write(m_fd, report.constData(), size_t(report.size()));
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) {
            // no free URB — put the frame back and retry a bit later
            m_queue.push_front(item);
            m_writeScheduled = true;
            QTimer::singleShot(20, this, [this] {
                m_writeScheduled = false;
                tryDequeue();
            });
            return WriteResult::Retry;
        }
        closeDevice();
        emit disconnected();
        return WriteResult::Error;
    }
    if (n != report.size()) {
        // a partial hidraw write does not happen; a short one means trouble
        closeDevice();
        emit disconnected();
        return WriteResult::Error;
    }
    m_lastWriteMs = QDateTime::currentMSecsSinceEpoch();
    return WriteResult::Written;
}

void Device::readReady()
{
    if (!m_open)
        return;

    char raw[64];
    const int n = ::read(m_fd, raw, sizeof raw);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return;
        closeDevice();
        emit disconnected();
        return;
    }
    if (n == 0) {
        closeDevice();
        emit disconnected();
        return;
    }
    const QByteArray buf(raw, n);

    const auto frame = parseFrame(buf);
    if (!frame)
        return;

    if (frame->cmd == Cmd::StatusNotify) {
        const auto st = parseStatus(buf);
        if (st) {
            m_lastStatus = *st;
            m_lastStatusMs = QDateTime::currentMSecsSinceEpoch();
            emit statusUpdated(*st);
        }
        return;
    }

    if (m_pendingCmd && frame->cmd == m_pendingCmd) {
        const quint8 cmd = m_pendingCmd;
        m_pendingCmd = 0;
        m_ackTimer.stop();

        if (cmd == Cmd::QueryRpm && frame->payload.size() >= 4) {
            m_lastStatus.currentRpm =
                quint8(frame->payload[0]) | (quint8(frame->payload[1]) << 8);
            m_lastStatus.targetRpm =
                quint8(frame->payload[2]) | (quint8(frame->payload[3]) << 8);
            m_lastStatusMs = QDateTime::currentMSecsSinceEpoch();
            emit statusUpdated(m_lastStatus);
        }

        emit ackReceived(cmd, frame->payload);
        tryDequeue();
    }
}

void Device::onAckTimeout()
{
    if (!m_pendingCmd)
        return;
    const quint8 cmd = m_pendingCmd;
    m_pendingCmd = 0;
    emit commandTimedOut(cmd);
    tryDequeue();
}

} // namespace FlyKooler
