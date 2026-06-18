#ifndef BBFFOTASERVICE_H
#define BBFFOTASERVICE_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QFile>
#include <QElapsedTimer>
#include <atomic>
#include "rs485manager.h"

class BbFfOtaWorker : public QThread
{
    Q_OBJECT
public:
    explicit BbFfOtaWorker(Rs485Manager *manager, QObject *parent = nullptr);
    ~BbFfOtaWorker();

    void setup(const QString &filePath, const QString &versionStr);
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
    bool sendStartPacket(const QByteArray &versionBytes, quint32 totalSize);
    bool sendDataPacket(quint32 id, const QByteArray &data);
    bool sendEndPacket(const QByteArray &crcBytes);

    Rs485Manager *m_manager;
    QString m_firmwarePath;
    QString m_versionStr;

    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QQueue<Packet> m_pendingPackets;
    std::atomic_bool m_abortRequested;
    QString m_lastError;
};

class BbFfOtaService : public QObject
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

    explicit BbFfOtaService(Rs485Manager *manager, QObject *parent = nullptr);
    ~BbFfOtaService();

    State state() const { return m_currentState; }
    QString stateText() const;
    QString lastMessage() const { return m_currentMessage; }
    int progress() const { return m_currentProgress; }

    void startUpgrade(const QString &firmwarePath, const QString &versionStr);
    void abortUpgrade();

signals:
    void otaStateChanged(BbFfOtaService::State state, const QString &message);
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
    BbFfOtaWorker *m_worker;
};

#endif // BBFFOTASERVICE_H
