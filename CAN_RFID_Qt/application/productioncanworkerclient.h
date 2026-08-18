#ifndef PRODUCTIONCANWORKERCLIENT_H
#define PRODUCTIONCANWORKERCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QLocalSocket>
#include <QVector>

#include "domain/cantransport.h"

class ProductionCanWorkerClient : public QObject, public CanTransport
{
    Q_OBJECT
public:
    explicit ProductionCanWorkerClient(int stationNumber, QObject *parent = nullptr);
    ~ProductionCanWorkerClient() override;

    bool startDevice(quint32 deviceType,
                     quint32 deviceIndex,
                     quint32 channel,
                     bool resistanceEnabled,
                     QString *error);
    void stopDevice();
    bool isReady() const;

    bool sendData(quint32 id,
                  quint32 frameTypeIndex,
                  quint32 protocolIndex,
                  quint32 canFdExpansionIndex,
                  quint32 channel,
                  const char *data,
                  quint32 length,
                  CanFrame *sentFrame = nullptr) override;
    bool sendClassicData(quint32 id,
                         quint32 channel,
                         const QByteArray &payload,
                         CanFrame *sentFrame = nullptr) override;
    RfidControlSendResult sendManualRfidControlFrame(
        quint32 channel, bool scanning, CanFrame *sentFrame) override;
    void setRfidControlPeriodicEnabled(
        bool enabled, quint32 channel, bool scanning) override;

signals:
    void receivedFrames(const QVector<CanFrame> &frames);
    void diagnostic(const QString &message);
    void workerDisconnected();

private slots:
    void handleReadyRead();
    void handleSocketDisconnected();

private:
    bool startWorker(QString *error);
    bool sendRequest(quint8 command,
                     const QByteArray &payload,
                     qint32 *result,
                     QString *error = nullptr,
                     int timeoutMs = 3000);
    void sendPacket(const QByteArray &payload);
    void processPacket(const QByteArray &packet);
    static void fillSentFrame(CanFrame *frame,
                              quint32 id,
                              quint32 channel,
                              const QByteArray &payload,
                              bool extended);

    int m_stationNumber;
    QString m_serverName;
    QProcess m_workerProcess;
    QLocalSocket m_socket;
    QByteArray m_receiveBuffer;
    quint32 m_nextRequestId;
    quint32 m_waitingRequestId;
    bool m_waitingDone;
    qint32 m_waitingResult;
    QString m_waitingError;
    bool m_ready;
    bool m_stopping;
};

#endif // PRODUCTIONCANWORKERCLIENT_H
