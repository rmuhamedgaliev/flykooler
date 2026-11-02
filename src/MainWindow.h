// SPDX-License-Identifier: MIT
// The Flydigi Kooler GUI — a thin client of the flykoolerd backend: it
// subscribes to status over a Unix socket and pushes configuration changes.
// It never touches the device itself — the daemon owns it, so several
// clients cannot conflict.

#pragma once

#include "Shared.h"

#include <QCloseEvent>
#include <QColor>
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>

class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QLocalSocket;

namespace FlyKooler {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildFanGroup();
    QWidget *buildLightGroup();
    QWidget *buildSystemGroup();
    void buildTray();
    QIcon appIcon() const;

    void connectToBackend();
    void sendJson(const class QJsonObject &request);
    void pushConfig();
    void onBackendMessage(const class QJsonObject &message);
    void applyConfigToUi();

    void fillCurveTable();
    void readCurveTable();
    void fillSensors(const class QJsonArray &sensors);
    void setUiEnabled(bool on);
    void updateHeader();
    void updatePowerState();
    void updateTray();
    void setAutostart(bool on);
    static QString autostartPath();

    // --- backend link ---
    QLocalSocket *m_sock = nullptr;
    QTimer m_reconnectTimer;
    QTimer m_pushTimer;
    int m_backendTries = 0;
    bool m_backendUp = false;

    // --- state (mirror of the backend) ---
    Config m_cfg;
    bool m_deviceConnected = false;
    bool m_wasDeviceConnected = false;
    QString m_model, m_link, m_fw;
    int m_currentRpm = 0, m_targetRpm = 0, m_gear = 0, m_supply = 0, m_rpmCeil = 4000;
    int m_gearRpm[4] = {0, 0, 0, 0};
    bool m_asleep = false;
    bool m_coolerOff = false; // manual "off" (or the host went down)
    double m_lastTemp = qQNaN();
    int m_smartRpm = 0;
    bool m_shutdown = false;

    // --- widgets ---
    QLabel *m_headerLabel = nullptr;
    QLabel *m_stateDot = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_rpmLabel = nullptr;
    QLabel *m_tempLabel = nullptr;
    QPushButton *m_powerOnBtn = nullptr;
    QPushButton *m_powerOffBtn = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QStackedWidget *m_modeStack = nullptr;
    QComboBox *m_sensorCombo = nullptr;
    QTableWidget *m_curveTable = nullptr;
    QSlider *m_rpmSlider = nullptr;
    QSpinBox *m_rpmSpin = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_gearBtns[4] = {};
    QPushButton *m_gearApplyBtn = nullptr;
    QTableWidget *m_gearTable = nullptr;
    QCheckBox *m_stripChk = nullptr;
    QComboBox *m_lightModeCombo = nullptr;
    QToolButton *m_colorBtn = nullptr;
    QSlider *m_brightSlider = nullptr;
    QLabel *m_brightLabel = nullptr;
    QCheckBox *m_gearLedsChk = nullptr;
    QComboBox *m_standbyCombo = nullptr;
    QCheckBox *m_offWithHostChk = nullptr;
    QCheckBox *m_wakeChk = nullptr;
    QCheckBox *m_autostartChk = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_trayModeActs[3] = {};
};

} // namespace FlyKooler
