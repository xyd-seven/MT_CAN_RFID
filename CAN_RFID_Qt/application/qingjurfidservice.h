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
    
    QString statusText;
    QString uidText;
    QString assetModel;
    QString assetSupplier;
    QString assetSerial;
    QString alarmText;
    
    QString devSn;
    QString firmwareVer;
    QString hardwareVer;
    QString appStatus;
};

class QingjuRfidService : public QObject
{
    Q_OBJECT
public:
    explicit QingjuRfidService(QingjuCanManager *canManager, QObject *parent = nullptr);

    void startScan(int intervalMs = 100);
    void stopScan();
    void reset();

    QingjuNpkState state() const { return m_state; }
    bool isScanning() const { return m_isScanning; }

signals:
    void stateUpdated(const QingjuNpkState &state);

private slots:
    void onPollTimeout();
    void onModbusPacketReceived(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);

private:
    void parseStatusData(const QByteArray &data);
    void parseVersionData(const QByteArray &data);
    void parseSnData(const QByteArray &data);
    void parseAppStatusData(const QByteArray &data);
    void calculatePassword(const QByteArray &uid);

    QingjuCanManager *m_canManager;
    QTimer *m_pollTimer;
    QingjuNpkState m_state;
    QByteArray m_lastUid;
    bool m_isScanning;
};

#endif // QINGJURFIDSERVICE_H
