#include "productioncanworkerhost.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QLocalSocket>
#include <QTimer>
#include <cstring>

#include "application/productioncanipc.h"
#include "canthread.h"

namespace {
constexpr quint32 ProductionCanBaudRate = 500000;

QByteArray framedPacket(const QByteArray &payload)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << static_cast<quint32>(payload.size());
    packet.append(payload);
    return packet;
}
}
ProductionCanWorkerHost::ProductionCanWorkerHost(const QString &serverName, QObject *parent)
    : QObject(parent)
    , m_serverName(serverName)
    , m_socket(nullptr)
    , m_canThread(new CANThread())
    , m_channel(0)
{
    connect(&m_server, &QLocalServer::newConnection,
            this, &ProductionCanWorkerHost::handleNewConnection);
    connect(m_canThread, &CANThread::recvedFrames,
            this, &ProductionCanWorkerHost::sendFrames);
    connect(m_canThread, &CANThread::driverDiagnostic,
            this, &ProductionCanWorkerHost::sendDiagnostic);
}

ProductionCanWorkerHost::~ProductionCanWorkerHost()
{
    stopDevice();
    delete m_canThread;
    m_canThread = nullptr;
    QLocalServer::removeServer(m_serverName);
}

bool ProductionCanWorkerHost::listen(QString *error)
{
    QLocalServer::removeServer(m_serverName);
    if (m_server.listen(m_serverName)) {
        return true;
    }
    if (error != nullptr) {
        *error = m_server.errorString();
    }
    return false;
}

void ProductionCanWorkerHost::handleNewConnection()
{
    if (m_socket != nullptr) {
        QLocalSocket *extraSocket = m_server.nextPendingConnection();
        extraSocket->disconnectFromServer();
        extraSocket->deleteLater();
        return;
    }
    m_socket = m_server.nextPendingConnection();
    connect(m_socket, &QLocalSocket::readyRead,
            this, &ProductionCanWorkerHost::handleReadyRead);
    connect(m_socket, &QLocalSocket::disconnected,
            this, &ProductionCanWorkerHost::handleDisconnected);
}

void ProductionCanWorkerHost::handleReadyRead()
{
    if (m_socket == nullptr) {
        return;
    }
    m_receiveBuffer.append(m_socket->readAll());
    while (m_receiveBuffer.size() >= static_cast<int>(sizeof(quint32))) {
        QDataStream header(m_receiveBuffer);
        header.setVersion(QDataStream::Qt_5_15);
        quint32 packetSize = 0;
        header >> packetSize;
        if (packetSize > ProductionCanIpc::MaxPacketBytes) {
            m_socket->abort();
            return;
        }
        const int totalSize = static_cast<int>(sizeof(quint32) + packetSize);
        if (m_receiveBuffer.size() < totalSize) {
            return;
        }
        const QByteArray packet = m_receiveBuffer.mid(sizeof(quint32), packetSize);
        m_receiveBuffer.remove(0, totalSize);
        processPacket(packet);
    }
}

void ProductionCanWorkerHost::handleDisconnected()
{
    stopDevice();
    if (m_socket != nullptr) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}

void ProductionCanWorkerHost::processPacket(const QByteArray &packet)
{
    QDataStream stream(packet);
    stream.setVersion(QDataStream::Qt_5_15);
    quint8 command = 0;
    quint32 requestId = 0;
    stream >> command >> requestId;

    if (command == ProductionCanIpc::StartDevice) {
        quint32 deviceType = 0;
        quint32 deviceIndex = 0;
        quint32 channel = 0;
        bool resistanceEnabled = false;
        stream >> deviceType >> deviceIndex >> channel >> resistanceEnabled;
        QString error;
        const bool success = startDevice(
            deviceType, deviceIndex, channel, resistanceEnabled, &error);
        sendResponse(requestId, success ? 1 : 0, error);
        return;
    }
    if (command == ProductionCanIpc::StopDevice) {
        stopDevice();
        sendResponse(requestId, 1);
        return;
    }
    if (command == ProductionCanIpc::SendFrame) {
        quint32 id = 0;
        bool extended = false;
        quint32 channel = 0;
        QByteArray data;
        stream >> id >> extended >> channel >> data;
        const bool success = m_canThread->sendData(
            id, extended ? 1U : 0U, 0, 0, channel,
            data.constData(), static_cast<quint32>(data.size()));
        sendResponse(requestId, success ? 1 : 0);
        return;
    }
    if (command == ProductionCanIpc::SetPeriodicControl) {
        bool enabled = false;
        quint32 channel = 0;
        bool scanning = false;
        stream >> enabled >> channel >> scanning;
        m_canThread->setRfidControlPeriodicEnabled(enabled, channel, scanning);
        sendResponse(requestId, 1);
        return;
    }
    if (command == ProductionCanIpc::SendManualControl) {
        quint32 channel = 0;
        bool scanning = false;
        stream >> channel >> scanning;
        const CanTransport::RfidControlSendResult result =
            m_canThread->sendManualRfidControlFrame(channel, scanning, nullptr);
        sendResponse(requestId, static_cast<qint32>(result));
        return;
    }
    sendResponse(requestId, 0, QStringLiteral("未知CAN工作进程命令：%1").arg(command));
}

void ProductionCanWorkerHost::sendPacket(const QByteArray &payload)
{
    if (m_socket == nullptr || m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    m_socket->write(framedPacket(payload));
    m_socket->flush();
}

void ProductionCanWorkerHost::sendResponse(quint32 requestId,
                                           qint32 result,
                                           const QString &message)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << ProductionCanIpc::Response << requestId << result << message;
    sendPacket(payload);
}

void ProductionCanWorkerHost::sendDiagnostic(const QString &message)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << ProductionCanIpc::Diagnostic << message;
    sendPacket(payload);
}

void ProductionCanWorkerHost::sendFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty()) {
        return;
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << ProductionCanIpc::ReceivedFrames << static_cast<quint32>(frames.size());
    for (const CanFrame &frame : frames) {
        stream << frame.id << frame.channel << frame.data
               << static_cast<quint8>(frame.direction)
               << static_cast<quint8>(frame.protocol)
               << frame.extendedFrame << frame.remoteFrame
               << frame.monotonicElapsedMs << frame.hostDateTime.toMSecsSinceEpoch()
               << frame.zlgTimestampRaw << frame.hasZlgTimestamp;
    }
    sendPacket(payload);
}

bool ProductionCanWorkerHost::startDevice(quint32 deviceType,
                                          quint32 deviceIndex,
                                          quint32 channel,
                                          bool resistanceEnabled,
                                          QString *error)
{
    m_channel = channel;
    if (!m_canThread->openDevice(deviceType, deviceIndex, 0)) {
        if (error != nullptr) {
            *error = QStringLiteral("设备%1打开失败，可能已被其他程序占用").arg(deviceIndex);
        }
        return false;
    }
    if (!m_canThread->setClassicBaudrateForChannel(channel, ProductionCanBaudRate)) {
        if (error != nullptr) {
            *error = QStringLiteral("设备%1设置500k波特率失败").arg(deviceIndex);
        }
        stopDevice();
        return false;
    }
    if (!m_canThread->initClassicCANChannel(channel)) {
        if (error != nullptr) {
            *error = QStringLiteral("设备%1初始化CAN%2失败").arg(deviceIndex).arg(channel);
        }
        stopDevice();
        return false;
    }
    if (!m_canThread->setResistanceEnableForChannel(
            channel, resistanceEnabled ? 1U : 0U)) {
        if (error != nullptr) {
            *error = QStringLiteral("设备%1设置终端电阻失败").arg(deviceIndex);
        }
        stopDevice();
        return false;
    }
    if (!m_canThread->startCAN()) {
        if (error != nullptr) {
            *error = QStringLiteral("设备%1启动CAN失败").arg(deviceIndex);
        }
        stopDevice();
        return false;
    }
    m_canThread->start();
    return true;
}

void ProductionCanWorkerHost::stopDevice()
{
    if (m_canThread == nullptr) {
        return;
    }
    m_canThread->setRfidControlPeriodicEnabled(false, m_channel, false);
    m_canThread->stop();
    if (m_canThread->isRunning()) {
        m_canThread->wait(1500);
    }
    m_canThread->closeDevice();
}
