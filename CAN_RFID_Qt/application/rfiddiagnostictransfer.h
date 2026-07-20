#ifndef RFIDDIAGNOSTICTRANSFER_H
#define RFIDDIAGNOSTICTRANSFER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>

#include "domain/canframe.h"
#include "domain/isotptransport.h"

class RfidDiagnosticTransfer : public QObject
{
    Q_OBJECT
public:
    explicit RfidDiagnosticTransfer(QObject *parent = nullptr);

    bool isBusy() const;
    bool startWriteNonVolatile(quint16 dataId, const QByteArray &data);
    void handleResponseFrame(const CanFrame &frame);
    void abort();

signals:
    void frameReady(quint32 canId, const QByteArray &payload);
    void payloadSent();
    void finished(bool success, const QString &message);
    void logMessage(const QString &message);

private slots:
    void onTimeout();
    void sendNextConsecutiveFrame();

private:
    enum class State
    {
        Idle,
        WaitingFlowControl,
        SendingConsecutiveFrames,
        WaitingFinalResponse
    };

    void resetTransfer();
    void failTransfer(const QString &message);
    void finishTransfer(const QString &message);
    void startTimeout(int timeoutMs);
    void enterWaitingFlowControl();
    void enterWaitingFinalResponse();
    void handleFlowControl(const QByteArray &payload);
    void handleFinalResponse(const QByteArray &payload);
    int normalizedStMinMs(quint8 stMin) const;
    static QString nrcText(quint8 nrc);

    State m_state;
    QByteArray m_payload;
    quint16 m_dataId;
    int m_nextOffset;
    quint8 m_nextSequenceNumber;
    quint8 m_blockSize;
    quint8 m_blockSentCount;
    int m_stMinMs;
    int m_waitFrameCount;
    QTimer *m_timeoutTimer;
    QTimer *m_cfTimer;
    IsoTpTransport m_transport;
};

#endif // RFIDDIAGNOSTICTRANSFER_H
