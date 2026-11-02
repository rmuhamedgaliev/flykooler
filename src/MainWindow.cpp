// SPDX-License-Identifier: MIT

#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace FlyKooler;

static const char *const ModeNames[3] = {"smart", "fixed", "gear"};

namespace {

// A backend started by the GUI escapes Restart= and races the user unit for
// the socket, so the installed GUI asks systemd instead. A development build
// keeps starting the binary next to it.
void startBackend()
{
    const QString local = QStringLiteral("%1/flykoolerd")
                              .arg(QCoreApplication::applicationDirPath());
    const QString installed = QStandardPaths::findExecutable(QStringLiteral("flykoolerd"));

    if (!installed.isEmpty() &&
        QFileInfo(local).canonicalFilePath() == QFileInfo(installed).canonicalFilePath()) {
        QProcess systemctl;
        systemctl.start(QStringLiteral("systemctl"),
                        {QStringLiteral("--user"), QStringLiteral("start"),
                         QStringLiteral("flykoolerd")});
        if (systemctl.waitForFinished(700) &&
            systemctl.exitStatus() == QProcess::NormalExit && systemctl.exitCode() == 0)
            return;
    }
    QProcess::startDetached(local, {});
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Flydigi Kooler"));
    setWindowIcon(appIcon());
    resize(600, 680);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_headerLabel = new QLabel(central);
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setWordWrap(true);
    root->addWidget(m_headerLabel);

    // Power state and manual on/off: green is running, red is off.
    auto *powerRow = new QHBoxLayout;
    powerRow->setSpacing(8);
    m_stateDot = new QLabel(central);
    m_stateDot->setFixedSize(12, 12);
    powerRow->addWidget(m_stateDot);
    m_stateLabel = new QLabel(central);
    powerRow->addWidget(m_stateLabel);
    powerRow->addStretch(1);
    m_powerOnBtn = new QPushButton(tr("Turn on"), central);
    m_powerOnBtn->setToolTip(tr("Wake the cooler and restore the settings"));
    m_powerOnBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: #2e7d32; border: none;"
        " border-radius: 4px; padding: 5px 14px; font-weight: 600; }"
        "QPushButton:disabled { background: #bdbdbd; }"));
    m_powerOffBtn = new QPushButton(tr("Turn off"), central);
    m_powerOffBtn->setToolTip(
        tr("Stop the fan, blank the light and put the cooler to sleep"));
    m_powerOffBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: #c62828; border: none;"
        " border-radius: 4px; padding: 5px 14px; font-weight: 600; }"
        "QPushButton:disabled { background: #bdbdbd; }"));
    connect(m_powerOnBtn, &QPushButton::clicked, this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("wake")}});
    });
    connect(m_powerOffBtn, &QPushButton::clicked, this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("sleep")}});
    });
    powerRow->addWidget(m_powerOnBtn);
    powerRow->addWidget(m_powerOffBtn);
    root->addLayout(powerRow);

    root->addWidget(buildFanGroup());
    root->addWidget(buildLightGroup());
    root->addWidget(buildSystemGroup());

    m_rpmLabel = new QLabel(central);
    root->addWidget(m_rpmLabel);
    root->addStretch(1);
    setCentralWidget(central);

    buildTray();

    m_reconnectTimer.setInterval(700);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] { connectToBackend(); });

    // Debounce: dragging a slider must not flood the backend with configs —
    // every lighting upload writes the cooler's flash.
    m_pushTimer.setSingleShot(true);
    m_pushTimer.setInterval(400);
    connect(&m_pushTimer, &QTimer::timeout, this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("set_config")},
                             {QStringLiteral("config"), m_cfg.toJson()}});
    });

    applyConfigToUi();
    fillCurveTable();
    setUiEnabled(false);
    updateHeader();
    updatePowerState();
    updateTray();

    connectToBackend();
}

MainWindow::~MainWindow() = default;

QIcon MainWindow::appIcon() const
{
    QIcon icon;
    for (int s : {16, 22, 32, 48, 64}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(61, 174, 233)); // breeze blue
        p.drawEllipse(0, 0, s - 1, s - 1);
        p.setBrush(Qt::white);
        for (int k = 0; k < 3; ++k) {
            p.save();
            p.translate(s / 2.0, s / 2.0);
            p.rotate(120.0 * k);
            p.drawEllipse(QRectF(s * 0.08, -s * 0.36, s * 0.36, s * 0.28));
            p.restore();
        }
        p.setBrush(QColor(61, 174, 233));
        p.drawEllipse(QPointF(s / 2.0, s / 2.0), s * 0.09, s * 0.09);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QWidget *MainWindow::buildFanGroup()
{
    auto *box = new QGroupBox(tr("Fan"), this);
    auto *lay = new QVBoxLayout(box);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Mode:"), box));
    m_modeCombo = new QComboBox(box);
    m_modeCombo->addItem(tr("Smart — follow temperature"));
    m_modeCombo->addItem(tr("Fixed speed"));
    m_modeCombo->addItem(tr("Gears — device button"));
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_cfg.fanMode = QString::fromLatin1(ModeNames[idx]);
        pushConfig();
        for (int i = 0; i < 3; ++i)
            if (m_trayModeActs[i])
                m_trayModeActs[i]->setChecked(i == idx);
    });
    modeRow->addWidget(m_modeCombo, 1);
    lay->addLayout(modeRow);

    m_modeStack = new QStackedWidget(box);

    // --- smart mode ---
    {
        auto *page = new QWidget(m_modeStack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);

        auto *sensorRow = new QHBoxLayout;
        sensorRow->addWidget(new QLabel(tr("Sensor:"), page));
        m_sensorCombo = new QComboBox(page);
        connect(m_sensorCombo, &QComboBox::currentIndexChanged, this, [this] {
            m_cfg.sensorChip = m_sensorCombo->currentData(Qt::UserRole).toString();
            m_cfg.sensorLabel = m_sensorCombo->currentData(Qt::UserRole + 1).toString();
            pushConfig();
        });
        sensorRow->addWidget(m_sensorCombo, 1);
        m_tempLabel = new QLabel(page);
        sensorRow->addWidget(m_tempLabel);
        v->addLayout(sensorRow);

        m_curveTable = new QTableWidget(0, 2, page);
        m_curveTable->setHorizontalHeaderLabels({tr("°C"), tr("rpm")});
        m_curveTable->horizontalHeader()->setStretchLastSection(true);
        m_curveTable->verticalHeader()->hide();
        m_curveTable->setMinimumHeight(140);
        connect(m_curveTable, &QTableWidget::itemChanged, this, [this] {
            readCurveTable();
            pushConfig();
        });
        v->addWidget(m_curveTable);

        auto *rowBtns = new QHBoxLayout;
        auto *add = new QPushButton(tr("Add point"), page);
        auto *del = new QPushButton(tr("Remove point"), page);
        connect(add, &QPushButton::clicked, this, [this] {
            m_curveTable->blockSignals(true);
            const int r = m_curveTable->rowCount();
            m_curveTable->insertRow(r);
            auto *t = new QTableWidgetItem;
            auto *s = new QTableWidgetItem;
            t->setData(Qt::DisplayRole, 60 + 5 * r);
            s->setData(Qt::DisplayRole, 1500);
            m_curveTable->setItem(r, 0, t);
            m_curveTable->setItem(r, 1, s);
            m_curveTable->blockSignals(false);
            readCurveTable();
            pushConfig();
        });
        connect(del, &QPushButton::clicked, this, [this] {
            const int r = m_curveTable->currentRow();
            if (r < 0)
                return;
            m_curveTable->removeRow(r);
            readCurveTable();
            pushConfig();
        });
        rowBtns->addWidget(add);
        rowBtns->addWidget(del);
        rowBtns->addStretch(1);
        v->addLayout(rowBtns);
        m_modeStack->addWidget(page);
    }

    // --- fixed speed ---
    {
        auto *page = new QWidget(m_modeStack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        m_rpmSlider = new QSlider(Qt::Horizontal, page);
        m_rpmSlider->setRange(500, 4000);
        m_rpmSlider->setSingleStep(50);
        m_rpmSlider->setPageStep(200);
        m_rpmSpin = new QSpinBox(page);
        m_rpmSpin->setRange(500, 4000);
        m_rpmSpin->setSingleStep(50);
        m_rpmSpin->setSuffix(tr(" rpm"));
        m_stopBtn = new QPushButton(tr("Stop"), page);
        m_stopBtn->setToolTip(tr("Stop the fan completely"));
        connect(m_rpmSlider, &QSlider::valueChanged, m_rpmSpin, &QSpinBox::setValue);
        connect(m_rpmSpin, &QSpinBox::valueChanged, m_rpmSlider, &QSlider::setValue);
        connect(m_rpmSpin, &QSpinBox::valueChanged, this, [this](int v) {
            m_cfg.fixedRpm = v;
            pushConfig();
        });
        connect(m_stopBtn, &QPushButton::clicked, this, [this] {
            sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("stop")}});
        });
        row->addWidget(m_rpmSlider, 1);
        row->addWidget(m_rpmSpin);
        row->addWidget(m_stopBtn);
        v->addLayout(row);
        m_modeStack->addWidget(page);
    }

    // --- gears ---
    {
        auto *page = new QWidget(m_modeStack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        const QString names[4] = {tr("1 · Quiet"), tr("2 · Standard"),
                                  tr("3 · Strong"), tr("4 · Overclock")};
        for (int i = 0; i < 4; ++i) {
            m_gearBtns[i] = new QPushButton(names[i], page);
            m_gearBtns[i]->setCheckable(true);
            connect(m_gearBtns[i], &QPushButton::clicked, this, [this, i] {
                m_cfg.fanMode = QStringLiteral("gear");
                m_cfg.gear = i + 1;
                pushConfig();
            });
            row->addWidget(m_gearBtns[i]);
        }
        v->addLayout(row);

        m_gearTable = new QTableWidget(4, 2, page);
        m_gearTable->setHorizontalHeaderLabels({tr("Gear"), tr("rpm")});
        m_gearTable->verticalHeader()->hide();
        m_gearTable->horizontalHeader()->setStretchLastSection(true);
        m_gearTable->setMaximumHeight(150);
        for (int i = 0; i < 4; ++i) {
            auto *n = new QTableWidgetItem(names[i]);
            n->setFlags(n->flags() & ~Qt::ItemIsEditable);
            m_gearTable->setItem(i, 0, n);
            auto *r = new QTableWidgetItem;
            r->setData(Qt::DisplayRole, 0);
            m_gearTable->setItem(i, 1, r);
        }
        v->addWidget(m_gearTable);
        m_gearApplyBtn = new QPushButton(tr("Store gear speeds in the cooler"), page);
        connect(m_gearApplyBtn, &QPushButton::clicked, this, [this] {
            for (int slot = 0; slot < 4; ++slot) {
                const int rpm = m_gearTable->item(slot, 1)->data(Qt::DisplayRole).toInt();
                if (rpm <= 0 || rpm == m_gearRpm[slot])
                    continue;
                sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("gear_rpm")},
                                     {QStringLiteral("slot"), slot},
                                     {QStringLiteral("rpm"), rpm}});
            }
        });
        v->addWidget(m_gearApplyBtn);
        m_modeStack->addWidget(page);
    }

    lay->addWidget(m_modeStack);
    return box;
}

QWidget *MainWindow::buildLightGroup()
{
    auto *box = new QGroupBox(tr("Lighting"), this);
    auto *lay = new QVBoxLayout(box);

    m_stripChk = new QCheckBox(tr("Strip on"), box);
    m_stripChk->setChecked(true);
    connect(m_stripChk, &QCheckBox::toggled, this, [this](bool on) {
        m_cfg.lightOn = on;
        pushConfig();
    });

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Mode:"), box));
    m_lightModeCombo = new QComboBox(box);
    m_lightModeCombo->addItem(tr("Steady colour"));
    m_lightModeCombo->addItem(tr("Effect: green wave"));
    m_lightModeCombo->addItem(tr("Effect: yellow wave"));
    m_lightModeCombo->addItem(tr("Effect: red wave"));
    m_lightModeCombo->addItem(tr("Effect: steady red"));
    m_lightModeCombo->addItem(tr("Effect: multicolour"));
    row1->addWidget(m_lightModeCombo, 1);
    m_colorBtn = new QToolButton(box);
    m_colorBtn->setText(tr("Colour…"));
    connect(m_colorBtn, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(m_cfg.color), this, tr("Strip colour"));
        if (!c.isValid())
            return;
        m_cfg.color = c.name();
        m_colorBtn->setText(tr("Colour… (%1)").arg(c.name()));
        pushConfig();
    });
    row1->addWidget(m_colorBtn);
    connect(m_lightModeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_cfg.lightMode = idx;
        m_colorBtn->setEnabled(idx == 0);
        pushConfig();
    });

    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(tr("Brightness:"), box));
    m_brightSlider = new QSlider(Qt::Horizontal, box);
    m_brightSlider->setRange(0, 100);
    m_brightSlider->setValue(60);
    m_brightLabel = new QLabel(box);
    connect(m_brightSlider, &QSlider::valueChanged, this, [this](int v) {
        m_brightLabel->setText(QString::number(v) + u'%');
        m_cfg.brightness = v;
        pushConfig();
    });
    row2->addWidget(m_brightSlider, 1);
    row2->addWidget(m_brightLabel);

    m_gearLedsChk = new QCheckBox(tr("Gear indicator LEDs"), box);
    m_gearLedsChk->setChecked(true);
    connect(m_gearLedsChk, &QCheckBox::toggled, this, [this](bool on) {
        m_cfg.gearLeds = on;
        pushConfig();
    });

    lay->addWidget(m_stripChk);
    lay->addLayout(row1);
    lay->addLayout(row2);
    lay->addWidget(m_gearLedsChk);
    return box;
}

QWidget *MainWindow::buildSystemGroup()
{
    auto *box = new QGroupBox(tr("System"), this);
    auto *lay = new QVBoxLayout(box);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("When the link drops:"), box));
    m_standbyCombo = new QComboBox(box);
    m_standbyCombo->addItem(tr("stay awake"));
    m_standbyCombo->addItem(tr("sleep at once"));
    m_standbyCombo->addItem(tr("sleep after a minute"));
    connect(m_standbyCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_cfg.standby = idx;
        pushConfig();
    });
    row->addWidget(m_standbyCombo, 1);
    lay->addLayout(row);

    m_offWithHostChk = new QCheckBox(
        tr("Turn the cooler off when the laptop sleeps or shuts down"), box);
    m_offWithHostChk->setToolTip(
        tr("The cooler stays off until the laptop wakes it again"));
    connect(m_offWithHostChk, &QCheckBox::toggled, this, [this](bool on) {
        m_cfg.offWithHost = on;
        m_standbyCombo->setEnabled(!on);
        m_standbyCombo->blockSignals(true);
        m_standbyCombo->setCurrentIndex(on ? 1 : qBound(0, m_cfg.standby, 2));
        m_standbyCombo->blockSignals(false);
        pushConfig();
    });
    lay->addWidget(m_offWithHostChk);

    m_wakeChk = new QCheckBox(tr("Wake the cooler on connect"), box);
    m_wakeChk->setChecked(true);
    connect(m_wakeChk, &QCheckBox::toggled, this, [this](bool on) {
        m_cfg.wake = on;
        pushConfig();
    });
    m_autostartChk = new QCheckBox(
        tr("Start Flydigi Kooler at Plasma login (backend and window)"), box);
    connect(m_autostartChk, &QCheckBox::toggled, this, [this](bool on) {
        m_cfg.autostart = on;
        setAutostart(on);
        pushConfig();
    });
    lay->addWidget(m_wakeChk);
    lay->addWidget(m_autostartChk);
    return box;
}

void MainWindow::buildTray()
{
    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(QStringLiteral("Flydigi Kooler"));

    QMenu *menu = new QMenu(this);
    menu->addAction(tr("Open"), this, [this] {
        show();
        raise();
        activateWindow();
    });
    menu->addSeparator();

    auto *group = new QActionGroup(menu);
    const char *names[3] = {"Smart mode", "Fixed speed", "Device button"};
    for (int i = 0; i < 3; ++i) {
        QAction *a = group->addAction(QString::fromUtf8(names[i]));
        a->setCheckable(true);
        const int idx = i;
        connect(a, &QAction::triggered, this,
                [this, idx] { m_modeCombo->setCurrentIndex(idx); });
        m_trayModeActs[i] = a;
    }
    menu->addActions(group->actions());
    menu->addSeparator();
    menu->addAction(tr("Stop the fan"), this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("stop")}});
    });
    menu->addAction(tr("Turn the cooler off"), this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("sleep")}});
    });
    menu->addAction(tr("Turn the cooler on"), this, [this] {
        sendJson(QJsonObject{{QStringLiteral("request"), QStringLiteral("wake")}});
    });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this] {
        m_shutdown = true;
        close();
        QCoreApplication::quit();
    });

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::DoubleClick || r == QSystemTrayIcon::Trigger)
                    show();
            });
    m_tray->show();
}

// ---------------- backend link ----------------

void MainWindow::connectToBackend()
{
    if (m_backendUp)
        return;

    if (!m_sock) {
        m_sock = new QLocalSocket(this);
        connect(m_sock, &QLocalSocket::connected, this, [this] {
            m_backendUp = true;
            m_backendTries = 0;
            m_reconnectTimer.stop();
            setUiEnabled(true);
        });
        connect(m_sock, &QLocalSocket::disconnected, this, [this] {
            m_backendUp = false;
            setUiEnabled(false);
            m_headerLabel->setText(
                tr("○ Backend unavailable — reconnecting…"));
            m_reconnectTimer.start();
        });
        connect(m_sock, &QLocalSocket::readyRead, this, [this] {
            while (m_sock->canReadLine()) {
                const QByteArray line = m_sock->readLine();
                const QJsonDocument doc = QJsonDocument::fromJson(line);
                if (doc.isObject())
                    onBackendMessage(doc.object());
            }
        });
    }

    m_sock->connectToServer(daemonSocketPath());
    if (!m_sock->waitForConnected(150)) {
        // first failure — start the backend
        if (m_backendTries == 0)
            startBackend();
        ++m_backendTries;
        if (m_backendTries > 30) {
            m_headerLabel->setText(
                tr("○ Backend failed to start: check %1/flykoolerd")
                    .arg(QCoreApplication::applicationDirPath()));
            m_reconnectTimer.stop();
            return;
        }
        m_reconnectTimer.start();
    }
}

void MainWindow::sendJson(const QJsonObject &request)
{
    if (!m_backendUp || !m_sock)
        return;
    m_sock->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void MainWindow::pushConfig()
{
    m_pushTimer.start();
}

void MainWindow::onBackendMessage(const QJsonObject &msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("status")) {
        m_deviceConnected = msg.value(QStringLiteral("connected")).toBool();
        m_model = msg.value(QStringLiteral("model")).toString();
        m_link = msg.value(QStringLiteral("link")).toString();
        m_fw = msg.value(QStringLiteral("fw")).toString();
        m_currentRpm = msg.value(QStringLiteral("current")).toInt();
        m_targetRpm = msg.value(QStringLiteral("target")).toInt();
        m_gear = msg.value(QStringLiteral("gear")).toInt();
        m_supply = msg.value(QStringLiteral("supply")).toInt();
        m_rpmCeil = msg.value(QStringLiteral("rpmCeil")).toInt();
        m_asleep = msg.value(QStringLiteral("asleep")).toBool();
        m_coolerOff = msg.value(QStringLiteral("off")).toBool();
        const QJsonValue t = msg.value(QStringLiteral("temp"));
        if (t.isDouble())
            m_lastTemp = t.toDouble();
        const QJsonValue sr = msg.value(QStringLiteral("smartRpm"));
        if (sr.isDouble())
            m_smartRpm = int(sr.toDouble());
        const QJsonValue gt = msg.value(QStringLiteral("gearRpms"));
        if (gt.isArray()) {
            const QJsonArray arr = gt.toArray();
            for (int i = 0; i < 4 && i < arr.size(); ++i) {
                m_gearRpm[i] = arr[i].toInt();
                m_gearTable->blockSignals(true);
                m_gearTable->item(i, 1)->setData(Qt::DisplayRole, m_gearRpm[i]);
                m_gearTable->blockSignals(false);
                m_gearBtns[i]->setToolTip(tr("%1 rpm").arg(m_gearRpm[i]));
            }
        }

        if (m_deviceConnected != m_wasDeviceConnected && m_tray->isVisible() && !isVisible()) {
            m_tray->showMessage(m_deviceConnected ? tr("Cooler connected")
                                                  : tr("Cooler disconnected"),
                                m_deviceConnected
                                    ? tr("%1 — link established").arg(m_model)
                                    : tr("Link lost — waiting for the cooler"),
                                QSystemTrayIcon::Information, 3000);
        }
        m_wasDeviceConnected = m_deviceConnected;

        const QString state = m_asleep ? tr("asleep") : tr("running");
        m_rpmLabel->setText(m_deviceConnected
                                ? tr("Fan: %1 → %2 rpm · gear %3 · %4")
                                      .arg(m_currentRpm)
                                      .arg(m_targetRpm)
                                      .arg(m_gear)
                                      .arg(state)
                                : tr("No cooler link"));
        if (!std::isnan(m_lastTemp))
            m_tempLabel->setText(tr("%1 °C → %2 rpm")
                                     .arg(m_lastTemp, 0, 'f', 0)
                                     .arg(m_smartRpm));
        if (m_cfg.fanMode == QStringLiteral("gear") && m_gear >= 1 && m_gear <= 4) {
            for (int k = 0; k < 4; ++k)
                m_gearBtns[k]->setChecked(k + 1 == m_gear);
        }
        updateHeader();
        updatePowerState();
        updateTray();
    } else if (type == QStringLiteral("config")) {
        m_cfg = Config::fromJson(msg.value(QStringLiteral("config")).toObject(), m_cfg);
        applyConfigToUi();
        fillCurveTable();
    } else if (type == QStringLiteral("sensors")) {
        fillSensors(msg.value(QStringLiteral("sensors")).toArray());
    }
}

void MainWindow::applyConfigToUi()
{
    m_modeCombo->blockSignals(true);
    m_lightModeCombo->blockSignals(true);
    m_rpmSlider->blockSignals(true);
    m_rpmSpin->blockSignals(true);
    m_brightSlider->blockSignals(true);
    m_stripChk->blockSignals(true);
    m_gearLedsChk->blockSignals(true);
    m_standbyCombo->blockSignals(true);
    m_offWithHostChk->blockSignals(true);
    m_wakeChk->blockSignals(true);
    m_autostartChk->blockSignals(true);

    const int mode = m_cfg.fanMode == QStringLiteral("fixed") ? 1
                     : m_cfg.fanMode == QStringLiteral("gear") ? 2 : 0;
    m_modeCombo->setCurrentIndex(mode);
    m_modeStack->setCurrentIndex(mode);
    for (int i = 0; i < 3; ++i)
        if (m_trayModeActs[i])
            m_trayModeActs[i]->setChecked(i == mode);

    m_rpmSpin->setValue(qBound(500, m_cfg.fixedRpm, 4000));
    m_rpmSlider->setValue(m_rpmSpin->value());

    m_stripChk->setChecked(m_cfg.lightOn);
    m_lightModeCombo->setCurrentIndex(qBound(0, m_cfg.lightMode, 5));
    m_colorBtn->setText(tr("Colour… (%1)").arg(m_cfg.color));
    m_colorBtn->setEnabled(m_cfg.lightMode == 0);
    m_brightSlider->setValue(qBound(0, m_cfg.brightness, 100));
    m_brightLabel->setText(QString::number(m_brightSlider->value()) + u'%');
    m_gearLedsChk->setChecked(m_cfg.gearLeds);
    m_standbyCombo->setCurrentIndex(qBound(0, m_cfg.standby, 2));
    m_offWithHostChk->setChecked(m_cfg.offWithHost);
    // With "off with the host" the cooler always sleeps at once on a link
    // loss; the detailed choice only matters when that option is off.
    m_standbyCombo->setEnabled(!m_cfg.offWithHost);
    if (m_cfg.offWithHost)
        m_standbyCombo->setCurrentIndex(1);
    m_wakeChk->setChecked(m_cfg.wake);
    m_autostartChk->setChecked(m_cfg.autostart);
    for (int k = 0; k < 4; ++k)
        m_gearBtns[k]->setChecked(k + 1 == m_cfg.gear);

    m_modeCombo->blockSignals(false);
    m_lightModeCombo->blockSignals(false);
    m_rpmSlider->blockSignals(false);
    m_rpmSpin->blockSignals(false);
    m_brightSlider->blockSignals(false);
    m_stripChk->blockSignals(false);
    m_gearLedsChk->blockSignals(false);
    m_standbyCombo->blockSignals(false);
    m_offWithHostChk->blockSignals(false);
    m_wakeChk->blockSignals(false);
    m_autostartChk->blockSignals(false);
}

void MainWindow::fillCurveTable()
{
    m_curveTable->blockSignals(true);
    m_curveTable->setRowCount(int(m_cfg.curve.size()));
    for (int i = 0; i < m_cfg.curve.size(); ++i) {
        auto *t = new QTableWidgetItem;
        auto *r = new QTableWidgetItem;
        t->setData(Qt::DisplayRole, m_cfg.curve[i].first);
        r->setData(Qt::DisplayRole, m_cfg.curve[i].second);
        m_curveTable->setItem(i, 0, t);
        m_curveTable->setItem(i, 1, r);
    }
    m_curveTable->blockSignals(false);
}

void MainWindow::readCurveTable()
{
    QVector<QPair<int, int>> pts;
    for (int i = 0; i < m_curveTable->rowCount(); ++i) {
        const auto *t = m_curveTable->item(i, 0);
        const auto *r = m_curveTable->item(i, 1);
        if (!t || !r)
            continue;
        const int temp = t->data(Qt::DisplayRole).toInt();
        const int rpm = r->data(Qt::DisplayRole).toInt();
        if (temp <= 0 || temp > 110 || rpm < 500 || rpm > 4000)
            continue;
        pts.push_back({temp, rpm});
    }
    if (!pts.isEmpty()) {
        std::sort(pts.begin(), pts.end());
        m_cfg.curve = pts;
    }
}

void MainWindow::fillSensors(const QJsonArray &sensors)
{
    m_sensorCombo->blockSignals(true);
    m_sensorCombo->clear();
    int pick = 0;
    for (int i = 0; i < sensors.size(); ++i) {
        const QJsonObject s = sensors[i].toObject();
        const QString chip = s.value(QStringLiteral("chip")).toString();
        const QString label = s.value(QStringLiteral("label")).toString();
        m_sensorCombo->addItem(chip + (label.isEmpty() ? QString() : QStringLiteral(" · ") + label));
        m_sensorCombo->setItemData(i, chip, Qt::UserRole);
        m_sensorCombo->setItemData(i, label, Qt::UserRole + 1);
        if (chip == m_cfg.sensorChip && (m_cfg.sensorLabel.isEmpty() || label == m_cfg.sensorLabel))
            pick = i;
        else if (m_cfg.sensorChip.isEmpty() &&
                 (chip.contains(QStringLiteral("k10temp"), Qt::CaseInsensitive) ||
                  chip.contains(QStringLiteral("coretemp"), Qt::CaseInsensitive) ||
                  chip.contains(QStringLiteral("zenpower"), Qt::CaseInsensitive)))
            pick = i;
    }
    m_sensorCombo->setCurrentIndex(pick);
    m_sensorCombo->blockSignals(false);
    m_cfg.sensorChip = m_sensorCombo->currentData(Qt::UserRole).toString();
    m_cfg.sensorLabel = m_sensorCombo->currentData(Qt::UserRole + 1).toString();
}

void MainWindow::setUiEnabled(bool on)
{
    const QList<QGroupBox *> boxes = findChildren<QGroupBox *>();
    for (QGroupBox *b : boxes)
        b->setEnabled(on);
    if (!on)
        m_rpmLabel->setText(tr("No backend link"));
    updatePowerState();
}

void MainWindow::updateHeader()
{
    if (!m_deviceConnected) {
        m_headerLabel->setText(
            m_backendUp
                ? tr("○ Cooler not found — turn it on or connect cable / Bluetooth")
                : tr("○ Backend unavailable"));
        return;
    }
    QString supply;
    switch (m_supply) {
    case 1: supply = tr("power: low (up to %1)").arg(m_rpmCeil); break;
    case 2: supply = tr("power: medium (up to %1)").arg(m_rpmCeil); break;
    case 3: supply = tr("power: full (up to %1)").arg(m_rpmCeil); break;
    default: supply = tr("power: measuring…"); break;
    }
    m_headerLabel->setText(
        tr("● %1 · %2 · %3 · %4")
            .arg(m_model.isEmpty() ? QStringLiteral("BS") : m_model,
                 m_link.isEmpty() ? tr("no host link") : m_link, supply,
                 m_fw.isEmpty() ? tr("firmware: ?") : tr("firmware %1").arg(m_fw)));
}

void MainWindow::updatePowerState()
{
    if (!m_stateDot || !m_stateLabel || !m_powerOnBtn || !m_powerOffBtn)
        return;

    QString color = QStringLiteral("#9e9e9e");
    QString text = tr("Backend unavailable");

    if (m_backendUp && m_deviceConnected) {
        const bool off = m_asleep || m_coolerOff;
        color = off ? QStringLiteral("#e53935") : QStringLiteral("#43a047");
        text = off ? tr("Off") : tr("On");
        m_powerOnBtn->setEnabled(off);
        m_powerOffBtn->setEnabled(!off);
    } else {
        if (m_backendUp)
            text = tr("No cooler link");
        m_powerOnBtn->setEnabled(false);
        m_powerOffBtn->setEnabled(false);
    }

    m_stateDot->setStyleSheet(
        QStringLiteral("background-color: %1; border-radius: 6px;").arg(color));
    m_stateLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600;").arg(color));
    m_stateLabel->setText(text);
}

void MainWindow::updateTray()
{
    if (!m_tray)
        return;
    m_tray->setToolTip(m_deviceConnected
                           ? QStringLiteral("Flydigi Kooler — %1 rpm").arg(m_currentRpm)
                           : QStringLiteral("Flydigi Kooler — no link"));
}

QString MainWindow::autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/autostart/flykooler.desktop");
}

void MainWindow::setAutostart(bool on)
{
    // the backend's systemd unit — the curve runs even without the window;
    // startDetached keeps the UI thread free
    const auto systemctl = [on](const char *action) {
        QProcess::startDetached(QStringLiteral("systemctl"),
                                {QStringLiteral("--user"), QString::fromLatin1(action),
                                 QStringLiteral("flykoolerd.service")});
    };
    systemctl(on ? "enable" : "disable");
    if (on)
        systemctl("start");

    // the window itself
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Flydigi Kooler\n"
                           "Exec=%1\n"
                           "Icon=flykooler\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "Terminal=false\n")
                .arg(QCoreApplication::applicationFilePath())
                .toUtf8());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_shutdown && m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}
