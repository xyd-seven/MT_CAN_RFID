#ifndef OTASERVICE_H
#define OTASERVICE_H

#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <QQueue>
#include <atomic>
#include "domain/canframe.h"
#include "domain/isotptransport.h"

class CANThread;

struct OtaErrorConfig
{
    bool enabled = false;
    bool crcError = false;
    bool seqError = false;
    bool vendorMismatch = false;
    bool hwMismatch = false;
    bool a2FirstFrameDataError = false;
    bool silentTimeout = false;
    bool ignoreFcInterval = false;
    bool isoTpSnError = false;
    bool outOfOrderState = false;
};

class OtaWorker : public QThread
{
    Q_OBJECT
public:
    explicit OtaWorker(QObject *parent = nullptr);
    ~OtaWorker();

    void setup(const QString &filePath, const IsoTpConfig &cfg, quint8 vendor, quint16 hw, quint16 sw, quint8 proto, CANThread *canthread, const OtaErrorConfig &injectCfg = OtaErrorConfig(), bool queryOnly = false);
    void requestAbort();
    void handleIncomingFrame(const CanFrame &frame);

signals:
    void transmitFrame(quint32 id, const QByteArray &payload);
    void statusUpdated(int stateVal, const QString &message, int progressPercent);

protected:
    void run() override;

private:
    bool waitForFlowControl(int timeoutMs, IsoTpFlowControl &fc);
    bool waitForResponse(quint8 expectedSid, int timeoutMs, QByteArray &payload);
    bool waitForFrame(quint32 expectedId, quint8 firstByteMask, quint8 expectedFirstByte, int timeoutMs, CanFrame &matchedFrame);
    bool waitForFlowControlWithWait(int timeoutMs, IsoTpFlowControl &fc);
    void updateStatus(int stateVal, const QString &msg, int progress = -1);
    bool sendSingleFrame(quint8 sid, const QByteArray &params = QByteArray());
    bool sendMultiFrame(const QByteArray &payload, int timeoutMs);
    void clearPendingFramesLocked();

    CANThread *m_canthread;
    QString firmwarePath;
    IsoTpConfig config;
    quint8 vendorCode;
    quint16 hwVersion;
    quint16 swVersion;
    quint8 protocolVersion;
    QMutex mutex;
    QWaitCondition waitCondition;
    QQueue<CanFrame> m_pendingFrames;
    std::atomic_bool abortRequested;
    bool queryOnlyMode;
    QString lastError;
    IsoTpTransport transport;
    OtaErrorConfig injectConfig;
};

class OtaService : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Idle = 0,
        QueryProgram = 1,
        StartUpgrade = 2,
        SendData = 3,
        FinishUpgrade = 4,
        Abort = 5,
        Failed = 6,
        Completed = 7
    };
    Q_ENUM(State)

    explicit OtaService(QObject *parent = nullptr);
    ~OtaService();

    State state() const;
    QString stateText() const;
    QString lastMessage() const;
    int progress() const;
    void setChannel(quint32 channel);
    void setDeviceVersions(quint8 vendor, quint16 hw, quint16 sw);
    void queryProgramLocation();
    void startUpgrade(const QString &firmwarePath, const OtaErrorConfig &injectCfg = OtaErrorConfig());
    void abortUpgrade();
    void setCanThread(CANThread *canthread);
    void handleIncomingFrame(const CanFrame &frame);

signals:
    void transmitFrame(quint32 id, const QByteArray &payload);
    void otaStateChanged(OtaService::State state, const QString &message);
    void otaProgress(int percentage);

private slots:
    void onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent);

private:
    void setState(State state, const QString &message);

    State currentState;
    QString currentMessage;
    int currentProgress;
    IsoTpConfig config;
    quint8 vendorCode;
    quint16 hwVersion;
    quint16 swVersion;
    CANThread *m_canthread;
    OtaWorker *worker;
};

#endif // OTASERVICE_H
