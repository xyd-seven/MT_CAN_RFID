#ifndef HLOTASERVICE_H
#define HLOTASERVICE_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QFile>
#include <QElapsedTimer>
#include <atomic>
#include "rs485manager.h"

class HlOtaWorker : public QThread
{
    Q_OBJECT
public:
    explicit HlOtaWorker(Rs485Manager *manager, QObject *parent = nullptr);
    ~HlOtaWorker();

    void setup(const QString &filePath, bool queryOnly = false);
    void requestAbort();
    void handleIncomingPacket(quint8 cmdCode, const QByteArray &payload);

signals:
    void statusUpdated(int stateVal, const QString &message, int progressPercent);

protected:
    void run() override;

private:
    struct Packet {
        quint8 cmdCode;
        QByteArray payload;
    };

    bool waitForResponse(quint8 expectedCmd, int timeoutMs, QByteArray &outPayload);
    bool sendWritePacket(quint16 startReg, quint16 count, const QByteArray &regData);
    bool sendReadPacket(quint16 startReg, quint16 count);

    Rs485Manager *m_manager;
    QString m_firmwarePath;
    bool m_queryOnly;

    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QQueue<Packet> m_pendingPackets;
    std::atomic_bool m_abortRequested;
    QString m_lastError;
};

class HlOtaService : public QObject
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
        Completed = 6,
        QueryProgram = 7
    };
    Q_ENUM(State)

    explicit HlOtaService(Rs485Manager *manager, QObject *parent = nullptr);
    ~HlOtaService();

    State state() const { return m_currentState; }
    QString stateText() const;
    QString lastMessage() const { return m_currentMessage; }
    int progress() const { return m_currentProgress; }

    void startUpgrade(const QString &firmwarePath);
    void abortUpgrade();
    void queryProgramStatus();

signals:
    void otaStateChanged(HlOtaService::State state, const QString &message);
    void otaProgress(int percentage);

private slots:
    void onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent);
    void onPacketReceived(quint8 cmdCode, const QByteArray &payload);

private:
    void setState(State state, const QString &message);

    Rs485Manager *m_manager;
    State m_currentState;
    QString m_currentMessage;
    int m_currentProgress;
    HlOtaWorker *m_worker;
};

#endif // HLOTASERVICE_H
