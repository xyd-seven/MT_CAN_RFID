#ifndef PRODUCTIONCANWORKERHOST_H
#define PRODUCTIONCANWORKERHOST_H

#include <QByteArray>
#include <QObject>
#include <QLocalServer>
#include <QVector>

#include "domain/canframe.h"

class CANThread;
class QLocalSocket;

class ProductionCanWorkerHost : public QObject
{
    Q_OBJECT
public:
    explicit ProductionCanWorkerHost(const QString &serverName, QObject *parent = nullptr);
    ~ProductionCanWorkerHost() override;

    bool listen(QString *error);

private slots:
    void handleNewConnection();
    void handleReadyRead();
    void handleDisconnected();

private:
    void processPacket(const QByteArray &packet);
    void sendPacket(const QByteArray &payload);
    void sendResponse(quint32 requestId, qint32 result, const QString &message = QString());
    void sendDiagnostic(const QString &message);
    void sendFrames(const QVector<CanFrame> &frames);
    bool startDevice(quint32 deviceType,
                     quint32 deviceIndex,
                     quint32 channel,
                     bool resistanceEnabled,
                     QString *error);
    void stopDevice();

    QString m_serverName;
    QLocalServer m_server;
    QLocalSocket *m_socket;
    QByteArray m_receiveBuffer;
    CANThread *m_canThread;
    quint32 m_channel;
};

#endif // PRODUCTIONCANWORKERHOST_H
