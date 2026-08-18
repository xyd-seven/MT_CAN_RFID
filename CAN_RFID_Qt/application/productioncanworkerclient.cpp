#include "productioncanworkerclient.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include "application/productioncanipc.h"

namespace {
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
ProductionCanWorkerClient::ProductionCanWorkerClient(int stationNumber, QObject *parent)
    : QObject(parent)
    , m_stationNumber(stationNumber)
    , m_nextRequestId(1)
    , m_waitingRequestId(0)
    , m_waitingDone(false)
    , m_waitingResult(0)
    , m_ready(false)
    , m_stopping(false)
{
    connect(&m_socket, &QLocalSocket::readyRead,
            this, &ProductionCanWorkerClient::handleReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected,
            this, &ProductionCanWorkerClient::handleSocketDisconnected);
}

ProductionCanWorkerClient::~ProductionCanWorkerClient()
{
    stopDevice();
}

bool ProductionCanWorkerClient::startDevice(quint32 deviceType,
                                            quint32 deviceIndex,
                                            quint32 channel,
                                            bool resistanceEnabled,
                                            QString *error)
{
    if (m_ready) {
        return true;
    }
    if (!startWorker(error)) {
        return false;
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << deviceType << deviceIndex << channel << resistanceEnabled;
    qint32 result = 0;
    QString workerError;
    if (!sendRequest(ProductionCanIpc::StartDevice,
                     payload,
                     &result,
                     &workerError,
                     ProductionCanIpc::WorkerStartupTimeoutMs) || result != 1) {
        if (error != nullptr) {
            *error = workerError.isEmpty()
                         ? QStringLiteral("设备%1工作进程启动CAN失败").arg(deviceIndex)
                         : workerError;
        }
        stopDevice();
        return false;
    }
    m_ready = true;
    emit diagnostic(QStringLiteral("CAN工作进程已连接：设备%1/CAN%2，进程ID=%3")
                        .arg(deviceIndex)
                        .arg(channel)
                        .arg(m_workerProcess.processId()));
    return true;
}

void ProductionCanWorkerClient::stopDevice()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    if (m_socket.state() == QLocalSocket::ConnectedState) {
        qint32 result = 0;
        sendRequest(ProductionCanIpc::StopDevice, QByteArray(), &result, nullptr, 1000);
        m_socket.disconnectFromServer();
        m_socket.waitForDisconnected(300);
    }
    if (m_workerProcess.state() != QProcess::NotRunning) {
        m_workerProcess.terminate();
        if (!m_workerProcess.waitForFinished(1000)) {
            m_workerProcess.kill();
            m_workerProcess.waitForFinished(1000);
        }
    }
    m_receiveBuffer.clear();
    m_ready = false;
    m_stopping = false;
}

bool ProductionCanWorkerClient::isReady() const
{
    return m_ready && m_socket.state() == QLocalSocket::ConnectedState &&
           m_workerProcess.state() != QProcess::NotRunning;
}

bool ProductionCanWorkerClient::sendData(quint32 id,
                                         quint32 frameTypeIndex,
                                         quint32 protocolIndex,
                                         quint32 canFdExpansionIndex,
                                         quint32 channel,
                                         const char *data,
                                         quint32 length,
                                         CanFrame *sentFrame)
{
    Q_UNUSED(canFdExpansionIndex)
    if (!isReady() || data == nullptr || length == 0 || protocolIndex != 0) {
        return false;
    }
    const QByteArray frameData(data, static_cast<int>(length));
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << id << (frameTypeIndex != 0) << channel << frameData;
    qint32 result = 0;
    if (!sendRequest(ProductionCanIpc::SendFrame, payload, &result) || result != 1) {
        return false;
    }
    fillSentFrame(sentFrame, id, channel, frameData, frameTypeIndex != 0);
    return true;
}

bool ProductionCanWorkerClient::sendClassicData(quint32 id,
                                                quint32 channel,
                                                const QByteArray &payload,
                                                CanFrame *sentFrame)
{
    if (id > 0x7FF || payload.isEmpty() || payload.size() > 8) {
        return false;
    }
    return sendData(id, 0, 0, 0, channel,
                    payload.constData(), static_cast<quint32>(payload.size()), sentFrame);
}

CanTransport::RfidControlSendResult
ProductionCanWorkerClient::sendManualRfidControlFrame(
    quint32 channel, bool scanning, CanFrame *sentFrame)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << channel << scanning;
    qint32 result = static_cast<qint32>(RfidControlSendResult::Failed);
    if (!sendRequest(ProductionCanIpc::SendManualControl, payload, &result)) {
        return RfidControlSendResult::Failed;
    }
    const RfidControlSendResult sendResult =
        static_cast<RfidControlSendResult>(result);
    if (sendResult == RfidControlSendResult::Sent) {
        QByteArray controlPayload(8, 0);
        controlPayload[0] = scanning ? 1 : 0;
        fillSentFrame(sentFrame, 0x207, channel, controlPayload, false);
    }
    return sendResult;
}

void ProductionCanWorkerClient::setRfidControlPeriodicEnabled(
    bool enabled, quint32 channel, bool scanning)
{
    if (!isReady()) {
        return;
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << enabled << channel << scanning;
    qint32 result = 0;
    sendRequest(ProductionCanIpc::SetPeriodicControl, payload, &result);
}

bool ProductionCanWorkerClient::startWorker(QString *error)
{
    m_serverName = QStringLiteral("CAN_RFID_PRODUCTION_%1_%2_%3")
                       .arg(QCoreApplication::applicationPid())
                       .arg(m_stationNumber)
                       .arg(QDateTime::currentMSecsSinceEpoch());
    m_workerProcess.setProgram(QCoreApplication::applicationFilePath());
    m_workerProcess.setArguments({QStringLiteral("--production-can-worker"), m_serverName});
    m_workerProcess.start();
    if (!m_workerProcess.waitForStarted(ProductionCanIpc::WorkerStartupTimeoutMs)) {
        if (error != nullptr) {
            *error = QStringLiteral("无法启动工位%1的CAN工作进程：%2")
                         .arg(m_stationNumber)
                         .arg(m_workerProcess.errorString());
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ProductionCanIpc::WorkerStartupTimeoutMs) {
        m_socket.abort();
        m_socket.connectToServer(m_serverName);
        if (m_socket.waitForConnected(200)) {
            return true;
        }
        if (m_workerProcess.state() == QProcess::NotRunning) {
            break;
        }
        QThread::msleep(50);
    }
    if (error != nullptr) {
        *error = QStringLiteral("工位%1无法连接CAN工作进程：%2")
                     .arg(m_stationNumber)
                     .arg(m_socket.errorString());
    }
    return false;
}

bool ProductionCanWorkerClient::sendRequest(quint8 command,
                                            const QByteArray &payload,
                                            qint32 *result,
                                            QString *error,
                                            int timeoutMs)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        if (error != nullptr) {
            *error = QStringLiteral("CAN工作进程连接已断开");
        }
        return false;
    }
    const quint32 requestId = m_nextRequestId++;
    QByteArray request;
    QDataStream stream(&request, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << command << requestId;
    request.append(payload);
    m_waitingRequestId = requestId;
    m_waitingDone = false;
    m_waitingResult = 0;
    m_waitingError.clear();
    sendPacket(request);

    QElapsedTimer timer;
    timer.start();
    while (!m_waitingDone && timer.elapsed() < timeoutMs) {
        m_socket.waitForReadyRead(25);
        handleReadyRead();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            break;
        }
    }
    if (!m_waitingDone) {
        if (error != nullptr) {
            *error = QStringLiteral("CAN工作进程请求超时，命令=%1").arg(command);
        }
        return false;
    }
    if (result != nullptr) {
        *result = m_waitingResult;
    }
    if (error != nullptr) {
        *error = m_waitingError;
    }
    return true;
}

void ProductionCanWorkerClient::sendPacket(const QByteArray &payload)
{
    m_socket.write(framedPacket(payload));
    m_socket.flush();
}

void ProductionCanWorkerClient::handleReadyRead()
{
    m_receiveBuffer.append(m_socket.readAll());
    while (m_receiveBuffer.size() >= static_cast<int>(sizeof(quint32))) {
        QDataStream header(m_receiveBuffer);
        header.setVersion(QDataStream::Qt_5_15);
        quint32 packetSize = 0;
        header >> packetSize;
        if (packetSize > ProductionCanIpc::MaxPacketBytes) {
            emit diagnostic(QStringLiteral("CAN工作进程返回了非法数据包"));
            m_socket.abort();
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

void ProductionCanWorkerClient::processPacket(const QByteArray &packet)
{
    QDataStream stream(packet);
    stream.setVersion(QDataStream::Qt_5_15);
    quint8 type = 0;
    stream >> type;
    if (type == ProductionCanIpc::Response) {
        quint32 requestId = 0;
        qint32 result = 0;
        QString message;
        stream >> requestId >> result >> message;
        if (requestId == m_waitingRequestId) {
            m_waitingResult = result;
            m_waitingError = message;
            m_waitingDone = true;
        }
        return;
    }
    if (type == ProductionCanIpc::Diagnostic) {
        QString message;
        stream >> message;
        emit diagnostic(message);
        return;
    }
    if (type != ProductionCanIpc::ReceivedFrames) {
        return;
    }

    quint32 count = 0;
    stream >> count;
    QVector<CanFrame> frames;
    frames.reserve(static_cast<int>(count));
    for (quint32 index = 0; index < count; ++index) {
        CanFrame frame;
        quint8 direction = 0;
        quint8 protocol = 0;
        qint64 hostTimeMs = 0;
        stream >> frame.id >> frame.channel >> frame.data >> direction >> protocol
               >> frame.extendedFrame >> frame.remoteFrame >> frame.monotonicElapsedMs
               >> hostTimeMs >> frame.zlgTimestampRaw >> frame.hasZlgTimestamp;
        frame.direction = static_cast<CanFrameDirection>(direction);
        frame.protocol = static_cast<CanFrameProtocol>(protocol);
        frame.hostDateTime = QDateTime::fromMSecsSinceEpoch(hostTimeMs);
        frames.append(frame);
    }
    if (!frames.isEmpty()) {
        emit receivedFrames(frames);
    }
}

void ProductionCanWorkerClient::handleSocketDisconnected()
{
    m_ready = false;
    if (!m_stopping) {
        emit diagnostic(QStringLiteral("CAN工作进程连接意外断开"));
        emit workerDisconnected();
    }
}

void ProductionCanWorkerClient::fillSentFrame(CanFrame *frame,
                                              quint32 id,
                                              quint32 channel,
                                              const QByteArray &payload,
                                              bool extended)
{
    if (frame == nullptr) {
        return;
    }
    frame->id = id;
    frame->channel = channel;
    frame->data = payload;
    frame->direction = CanFrameDirection::Tx;
    frame->protocol = CanFrameProtocol::ClassicCan;
    frame->extendedFrame = extended;
    frame->remoteFrame = false;
    frame->hostDateTime = QDateTime::currentDateTime();
}
