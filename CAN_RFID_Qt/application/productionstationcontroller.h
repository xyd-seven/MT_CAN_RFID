#ifndef PRODUCTIONSTATIONCONTROLLER_H
#define PRODUCTIONSTATIONCONTROLLER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "application/productiontestservice.h"
#include "application/qingjurfidservice.h"
#include "application/rfiddiagnostictransfer.h"
#include "application/rfidservice.h"
#include "domain/canframe.h"

class ProductionCanWorkerClient;
class QingjuCanManager;

class ProductionStationController : public QObject
{
    Q_OBJECT
public:
    explicit ProductionStationController(int stationNumber, QObject *parent = nullptr);
    ~ProductionStationController() override;

    bool startDevice(quint32 deviceType,
                     quint32 deviceIndex,
                     quint32 channel,
                     bool resistanceEnabled,
                     QString *error);
    void stopDevice();

    bool startTest(int protocolMode,
                   const QString &sn,
                   const QString &hardwareVersion,
                   const QString &materialChange,
                   const ProductionTestConfig &config,
                   QString *error);
    bool setProtocolMode(int protocolMode);
    void stopTest(const QString &reason);
    void resetTest();

    int stationNumber() const;
    quint32 deviceIndex() const;
    quint32 channel() const;
    bool isDeviceReady() const;
    bool isRunning() const;
    int protocolMode() const;
    ProductionTestState state() const;
    QString deviceStatusText() const;

signals:
    void qingjuRfrSoftwareVersionUpdated(const QString &version);
    void deviceStateChanged();
    void testStateChanged(const ProductionTestState &state);
    void logMessage(const QString &message);
    void finished(bool passed, const ProductionTestState &state);
    void frameObserved(const CanFrame &frame);
    void qingjuDeviceInfoUpdated(const QString &softwareVersion,
                                 const QString &hardwareVersion,
                                 const QString &deviceSn);

private:
    void handleReceivedFrames(const QVector<CanFrame> &frames);
    void handleMeituanFrame(const CanFrame &frame);
    void handleQingjuPacket(quint8 sourceAddress,
                            quint8 destinationAddress,
                            quint8 functionCode,
                            const QByteArray &payload);
    void handleWriteRequested(quint16 dataId, const QByteArray &data);
    void handleScanControlRequested(bool enabled);
    void handleWriteTimeout();
    void finishPendingWrite(bool success, const QString &message);
    void beginQingjuSnVerification();
    void beginQingjuHardwareVersionVerification();
    bool performQingjuWrite(quint16 dataId, const QByteArray &data);
    bool sendClassicFrame(quint32 canId, const QByteArray &payload);
    void resetProtocolState();
    void setDeviceStatus(const QString &status);

    int m_stationNumber;
    quint32 m_deviceIndex;
    quint32 m_channel;
    bool m_deviceReady;
    int m_protocolMode;
    QString m_deviceStatus;

    ProductionCanWorkerClient *m_canWorker;
    RfidService m_rfidService;
    ProductionTestService m_testService;
    RfidDiagnosticTransfer m_diagnosticTransfer;
    QingjuCanManager *m_qingjuCanManager;
    QingjuRfidService *m_qingjuRfidService;

    QTimer *m_writeTimer;
    QTimer *m_rfrVersionTimer;
    bool m_writePending;
    bool m_qingjuVerifySnPending;
    bool m_qingjuVerifyHardwarePending;
    int m_qingjuWriteRetryCount;
    quint16 m_qingjuWritePendingRegister;
    quint16 m_qingjuWritePendingRegisterCount;
    QByteArray m_qingjuWritePendingData;
};

#endif // PRODUCTIONSTATIONCONTROLLER_H
