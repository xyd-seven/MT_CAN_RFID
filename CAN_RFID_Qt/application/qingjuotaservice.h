#ifndef QINGJUOTASERVICE_H
#define QINGJUOTASERVICE_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QFile>
#include <atomic>
#include "qingjucanmanager.h"

struct QingjuOtaErrorConfig
{
    bool enabled = false;
    int caseMode = 0; // 0: 无异常, 1~5 代表 case1 ~ case5
};

class QingjuOtaWorker : public QThread
{
    Q_OBJECT
public:
    explicit QingjuOtaWorker(QingjuCanManager *canManager, QObject *parent = nullptr);
    ~QingjuOtaWorker();

    void setup(const QString &filePath, const QingjuOtaErrorConfig &injectCfg);
    void requestAbort();
    void handleIncomingModbusPacket(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);

signals:
    void statusUpdated(int stateVal, const QString &message, int progressPercent);

protected:
    void run() override;

private:
    struct ModbusPacket {
        quint8 srcAddr;
        quint8 destAddr;
        quint8 funcCode;
        QByteArray payload;
    };

    bool waitForResponse(quint8 expectedSrc, quint8 expectedFunc, quint8 expectedKey, int timeoutMs, QByteArray &outPayload);

    QingjuCanManager *m_canManager;
    QString m_firmwarePath;
    QingjuOtaErrorConfig m_injectConfig;

    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QQueue<ModbusPacket> m_pendingPackets;
    std::atomic_bool m_abortRequested;
    QString m_lastError;
};

class QingjuOtaService : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Idle = 0,
        StartUpgrade = 1,
        SendData = 2,
        FinishUpgrade = 3,
        Abort = 4,
        Failed = 5,
        Completed = 6
    };
    Q_ENUM(State)

    explicit QingjuOtaService(QingjuCanManager *canManager, QObject *parent = nullptr);
    ~QingjuOtaService();

    State state() const { return m_currentState; }
    QString stateText() const;
    QString lastMessage() const { return m_currentMessage; }
    int progress() const { return m_currentProgress; }

    void startUpgrade(const QString &firmwarePath, const QingjuOtaErrorConfig &injectCfg = QingjuOtaErrorConfig());
    void abortUpgrade();
    void handleIncomingModbusPacket(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);

signals:
    void otaStateChanged(QingjuOtaService::State state, const QString &message);
    void otaProgress(int percentage);

private slots:
    void onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent);

private:
    void setState(State state, const QString &message);

    QingjuCanManager *m_canManager;
    State m_currentState;
    QString m_currentMessage;
    int m_currentProgress;
    QingjuOtaWorker *m_worker;
};

#endif // QINGJUOTASERVICE_H
