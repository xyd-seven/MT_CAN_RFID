#ifndef QINGJURFIDSERVICE_H
#define QINGJURFIDSERVICE_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QVector>
#include "qingjucanmanager.h"

struct QingjuNpkState
{
    quint16 result = 0;
    QByteArray uid;
    QByteArray assetData;
    quint16 alarm = 0;
    quint32 password = 0;
    bool statusSample = false;
    
    QString statusText;
    QString uidText;
    QString assetModel;
    QString assetSupplier;
    QString assetSerial;
    QString assetFullText;
    QString alarmText;
    
    QString devSn;
    QString firmwareVer;
    QString hardwareVer;
    QString appStatus;
    QString vendorInfo;     // 0xA005
    QString modelCodeText;  // 0xA015
    QString fwVersionStr;   // 0xA016
    QString hwVersionStr;   // 0xA020
};

class QingjuRfidService : public QObject
{
    Q_OBJECT
public:
    explicit QingjuRfidService(QingjuCanManager *canManager, QObject *parent = nullptr);

    void startScan(int intervalMs = 100, int hostPollIntervalMs = 500, int readMode = 1);
    void stopScan();
    void reset();

    QingjuNpkState state() const { return m_state; }
    bool isScanning() const { return m_isScanning; }
    int readMode() const { return m_readMode; }
    void setAutoWritePassword(bool enabled);
    bool isAutoWritePassword() const { return m_autoWritePassword; }
    void setTargetAddress(quint8 addr);
    quint8 targetAddress() const { return m_targetAddress; }
    void queryDeviceInfo(bool resumeScanAfterInfo = true);
    void queryDeviceInfo(quint8 targetAddress, bool resumeScanAfterInfo);
    void queryDeviceInfo(quint8 targetAddress, bool resumeScanAfterInfo, bool deviceInfoOnly);
    void triggerSingleQuery();

signals:
    void stateUpdated(const QingjuNpkState &state);

private slots:
    void onPollTimeout();
    void onModbusPacketReceived(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);
    void onDeviceInfoTimerTimeout();

private:
    void parseStatusData(const QByteArray &data);
    void parseVersionData(const QByteArray &data);
    void parseSnData(const QByteArray &data);
    void parseAppStatusData(const QByteArray &data);
    void parseVendorData(const QByteArray &data);
    void parseModelData(const QByteArray &data);
    void parseFwStrData(const QByteArray &data);
    void parseHwStrData(const QByteArray &data);
    void calculatePassword(const QByteArray &uid);
    void sendDeviceInfoRequest();
    void startPollingOrSingleQuery();

    QingjuCanManager *m_canManager;
    QTimer *m_pollTimer;
    QingjuNpkState m_state;
    QByteArray m_lastUid;
    bool m_isScanning;
    bool m_autoWritePassword;
    quint8 m_targetAddress;
    QTimer *m_deviceInfoTimer;
    int m_infoStep;
    int m_readMode;
    quint8 m_deviceInfoTargetAddress;
    bool m_resumeScanAfterDeviceInfo;
    bool m_deviceInfoOnlyMode;
    qint64 m_ignoreStatusUntilMs;
};

#endif // QINGJURFIDSERVICE_H
