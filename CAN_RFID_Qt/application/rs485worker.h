#ifndef RS485WORKER_H
#define RS485WORKER_H

#include <QObject>
#include <QByteArray>
#include <QString>

#include "bbffotaservice.h"
#include "hlotaservice.h"
#include "rs485manager.h"
#include "rs485rfidservice.h"

class Rs485Worker : public QObject
{
    Q_OBJECT
public:
    explicit Rs485Worker(QObject *parent = nullptr);
    ~Rs485Worker();

public slots:
    void initialize();
    void shutdown();
    void openPort(const QString &portName, int baudRate);
    void closePort();
    void setProtocolMode(int mode);
    void startScan(int hostPollIntervalMs, int readMode);
    void stopScan();
    void queryDeviceInfo();
    void triggerSingleQuery();
    void setPower(int powerRaw01Dbm);
    void queryPower();
    void ffReboot();
    void ffSetDemodulatorParams(int mixer, int ifAmp, int thrd);
    void ffQueryDemodulatorParams();
    void ffQueryCardSwitch();
    void setHlConfig(quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt);
    void hlWriteScanControl(int startStop, quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt);
    void hlRebootDevice();
    void sendRawData(const QByteArray &data);
    void hlOtaQueryProgramStatus();
    void hlOtaStartUpgrade(const QString &firmwarePath);
    void hlOtaAbortUpgrade();
    void bbFfOtaStartUpgrade(const QString &firmwarePath, const QString &versionStr);
    void bbFfOtaAbortUpgrade();

signals:
    void portOpened(bool success, const QString &message);
    void portClosed();
    void portDisconnected();
    void scanStateChanged(bool scanning);
    void rawDataSent(bool success, const QString &message);
    void frameReceived(const QByteArray &data, const QString &decodeText);
    void frameSent(const QByteArray &data, const QString &decodeText);
    void stateUpdated(const Rs485State &state);
    void pollSkipped();
    void commandFinished(bool success, const QString &message);
    void hlOtaStateChanged(HlOtaService::State state, const QString &message);
    void hlOtaProgress(int percentage);
    void bbFfOtaStateChanged(BbFfOtaService::State state, const QString &message);
    void bbFfOtaProgress(int percentage);

private:
    void ensureInitialized();

    Rs485Manager *m_manager;
    Rs485RfidService *m_rfidService;
    HlOtaService *m_hlOtaService;
    BbFfOtaService *m_bbFfOtaService;
    int m_protocolMode;
};

#endif // RS485WORKER_H
