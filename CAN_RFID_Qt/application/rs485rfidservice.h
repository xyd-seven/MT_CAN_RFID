#ifndef RS485RFIDSERVICE_H
#define RS485RFIDSERVICE_H

#include <QObject>
#include <QString>
#include <QTimer>
#include "rs485manager.h"

struct Rs485State
{
    int protocolMode = 2; // 2: BB, 3: FF
    QString tagId;
    QString bbRssi;
    QString bbPc;
    QString bbCrc;
    int errCode = 0;
    bool isCommunicationTimeout = false;
    QString errorMsg;
    
    // Static device info
    QString hwVersion;
    QString swVersion;
    QString manufacturer;
    QString deviceId;
    
    // Configuration states
    int transmitPower = -1; // in 0.01dBm units
    int ffMixer = -1;
    int ffIfAmp = -1;
    int ffThrd = -1;
    bool ffCardSwitch = true;
};

class Rs485RfidService : public QObject
{
    Q_OBJECT
public:
    explicit Rs485RfidService(Rs485Manager *manager, QObject *parent = nullptr);
    ~Rs485RfidService();

    void startScan(int hostPollIntervalMs, int readMode); // readMode: 1 = Auto Poll, 2 = Single Query
    void stopScan();
    void reset();

    Rs485State state() const { return m_state; }
    bool isScanning() const { return m_isScanning; }
    void setProtocolMode(int mode); // 2: BB, 3: FF

    // Downlink commands
    void queryDeviceInfo();
    void triggerSingleQuery();
    void setPower(int powerRaw01Dbm);
    void queryPower();
    
    // FF exclusive commands
    void ffReboot();
    void ffSetDemodulatorParams(int mixer, int ifAmp, int thrd);
    void ffQueryDemodulatorParams();
    void ffQueryCardSwitch();

signals:
    void stateUpdated(const Rs485State &state);
    void pollSkipped();
    // Emitted when a setting command finishes (success or failure)
    void commandFinished(bool success, const QString &message);

private slots:
    void onPollTimeout();
    void onPacketReceived(quint8 cmdCode, const QByteArray &payload);
    void onResponseTimeout();

private:
    // Helper to send packets easily
    bool sendBbPacket(quint8 type, quint8 code, const QByteArray &payload);
    bool sendFfPacket(quint8 code, const QByteArray &payload);
    
    void handleBbResponse(quint8 cmdCode, const QByteArray &payload);
    void handleFfResponse(quint8 cmdCode, const QByteArray &payload);
    
    void startTimeoutGuard(int timeoutMs = 500);
    void stopTimeoutGuard();
    bool isExpectedResponse(quint8 cmdCode) const;

    Rs485Manager *m_manager;
    QTimer *m_pollTimer;
    QTimer *m_timeoutTimer;
    
    Rs485State m_state;
    bool m_isScanning;
    int m_protocolMode; // 2: BB, 3: FF
    int m_readMode; // 1: Auto Poll, 2: Single Query
    int m_infoStep; // for sequential static info queries
    bool m_waitingForResponse;
    quint8 m_pendingCmdCode;
};

#endif // RS485RFIDSERVICE_H
