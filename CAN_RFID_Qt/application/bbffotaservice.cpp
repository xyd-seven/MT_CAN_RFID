#include "bbffotaservice.h"
#include "domain/crc16.h"
#include <QFileInfo>
#include <QDebug>

BbFfOtaWorker::BbFfOtaWorker(Rs485Manager *manager, QObject *parent)
    : QThread(parent)
    , m_manager(manager)
    , m_abortRequested(false)
{
}

BbFfOtaWorker::~BbFfOtaWorker()
{
    requestAbort();
    wait();
}

void BbFfOtaWorker::setup(const QString &filePath, const QString &versionStr)
{
    m_firmwarePath = filePath;
    m_versionStr = versionStr;
    m_abortRequested = false;
    m_lastError.clear();

    QMutexLocker locker(&m_mutex);
    m_pendingPackets.clear();
}

void BbFfOtaWorker::requestAbort()
{
    m_abortRequested.store(true);
    m_waitCondition.wakeAll();
}

void BbFfOtaWorker::handleIncomingPacket(quint8 cmdCode, const QByteArray &payload)
{
    QMutexLocker locker(&m_mutex);
    Packet p;
    p.cmdCode = cmdCode;
    p.payload = payload;
    m_pendingPackets.enqueue(p);
    m_waitCondition.wakeAll();
}

bool BbFfOtaWorker::waitForResponse(quint8 expectedCmd, int timeoutMs, QByteArray &outPayload)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&m_mutex);
    while (true) {
        if (m_abortRequested.load()) {
            m_lastError = QStringLiteral("用户终止升级");
            return false;
        }

        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            m_lastError = QStringLiteral("等待从机响应超时");
            return false;
        }

        if (m_pendingPackets.isEmpty()) {
            if (!m_waitCondition.wait(&m_mutex, remaining)) {
                m_lastError = QStringLiteral("等待从机响应超时");
                return false;
            }
        }

        while (!m_pendingPackets.isEmpty()) {
            Packet packet = m_pendingPackets.dequeue();
            if (packet.cmdCode == expectedCmd) {
                outPayload = packet.payload;
                return true;
            }
        }
    }
}

bool BbFfOtaWorker::sendStartPacket(const QByteArray &versionBytes, quint32 totalSize)
{
    QByteArray payload;
    payload.append(versionBytes);
    payload.append(static_cast<char>((totalSize >> 24) & 0xFF));
    payload.append(static_cast<char>((totalSize >> 16) & 0xFF));
    payload.append(static_cast<char>((totalSize >> 8) & 0xFF));
    payload.append(static_cast<char>(totalSize & 0xFF));

    QByteArray pkt;
    pkt.reserve(14);
    pkt.append(static_cast<char>(0xAA));
    pkt.append(static_cast<char>(0x55));
    pkt.append(static_cast<char>(0x20)); // Address
    pkt.append(static_cast<char>(0x1A)); // Cmd
    pkt.append(static_cast<char>(0));    // Len H
    pkt.append(static_cast<char>(7));    // Len L
    pkt.append(payload);

    quint8 sum = 0;
    for (int i = 2; i < pkt.size(); ++i) {
        sum += static_cast<quint8>(pkt.at(i));
    }
    pkt.append(static_cast<char>(sum));

    bool success = false;
    QMetaObject::invokeMethod(m_manager, "sendRawData", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(QByteArray, pkt));
    return success;
}

bool BbFfOtaWorker::sendDataPacket(quint32 id, const QByteArray &data)
{
    QByteArray payload;
    payload.append(static_cast<char>((id >> 24) & 0xFF));
    payload.append(static_cast<char>((id >> 16) & 0xFF));
    payload.append(static_cast<char>((id >> 8) & 0xFF));
    payload.append(static_cast<char>(id & 0xFF));
    payload.append(data);

    quint16 len = payload.size();

    QByteArray pkt;
    pkt.reserve(7 + len);
    pkt.append(static_cast<char>(0xAA));
    pkt.append(static_cast<char>(0x55));
    pkt.append(static_cast<char>(0x20)); // Address
    pkt.append(static_cast<char>(0x1B)); // Cmd
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(payload);

    quint8 sum = 0;
    for (int i = 2; i < pkt.size(); ++i) {
        sum += static_cast<quint8>(pkt.at(i));
    }
    pkt.append(static_cast<char>(sum));

    bool success = false;
    QMetaObject::invokeMethod(m_manager, "sendRawData", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(QByteArray, pkt));
    return success;
}

bool BbFfOtaWorker::sendEndPacket(const QByteArray &crcBytes)
{
    QByteArray pkt;
    pkt.reserve(11);
    pkt.append(static_cast<char>(0xAA));
    pkt.append(static_cast<char>(0x55));
    pkt.append(static_cast<char>(0x20)); // Address
    pkt.append(static_cast<char>(0x1C)); // Cmd
    pkt.append(static_cast<char>(0));    // Len H
    pkt.append(static_cast<char>(4));    // Len L
    pkt.append(crcBytes);

    quint8 sum = 0;
    for (int i = 2; i < pkt.size(); ++i) {
        sum += static_cast<quint8>(pkt.at(i));
    }
    pkt.append(static_cast<char>(sum));

    bool success = false;
    QMetaObject::invokeMethod(m_manager, "sendRawData", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(QByteArray, pkt));
    return success;
}

void BbFfOtaWorker::run()
{
    emit statusUpdated(1, QStringLiteral("读取固件文件..."), 0);

    QFile file(m_firmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusUpdated(5, QStringLiteral("无法打开固件文件"), 0);
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        emit statusUpdated(5, QStringLiteral("固件文件为空"), 0);
        return;
    }

    quint32 totalBytes = fileData.size();

    // 固件大小校验限制：必须是 4 的倍数，大于 256 字节，且不超过 25K
    if (totalBytes <= 256) {
        emit statusUpdated(5, QStringLiteral("升级终止: 固件文件必须大于 256 字节"), 0);
        return;
    }
    if (totalBytes % 4 != 0) {
        emit statusUpdated(5, QStringLiteral("升级终止: 固件大小必须为 4 的倍数"), 0);
        return;
    }
    if (totalBytes > 25600) {
        emit statusUpdated(5, QStringLiteral("升级终止: 固件文件大小不能超过 25KB"), 0);
        return;
    }

    // 解析版本号字符串，如 1.2.255 -> 0x01, 0x02, 0xFF
    QByteArray versionBytes;
    QStringList parts = m_versionStr.split('.');
    if (parts.size() == 3) {
        bool ok1, ok2, ok3;
        int v1 = parts[0].toInt(&ok1);
        int v2 = parts[1].toInt(&ok2);
        int v3 = parts[2].toInt(&ok3);
        if (!ok1 || !ok2 || !ok3 || v1 < 0 || v1 > 255 || v2 < 0 || v2 > 255 || v3 < 0 || v3 > 255) {
            emit statusUpdated(5, QStringLiteral("升级终止: 版本号数值无效或超出 0-255 范围"), 0);
            return;
        }
        versionBytes.append(static_cast<char>(v1));
        versionBytes.append(static_cast<char>(v2));
        versionBytes.append(static_cast<char>(v3));
    } else {
        emit statusUpdated(5, QStringLiteral("升级终止: 无效的版本号格式"), 0);
        return;
    }

    emit statusUpdated(1, QStringLiteral("下发开始升级握手指令..."), 0);

    quint16 negotiateChunkSize = 256;
    bool handshakeSuccess = false;
    for (int retry = 0; retry < 3; ++retry) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        if (!sendStartPacket(versionBytes, totalBytes)) {
            m_lastError = QStringLiteral("串口写入失败");
            msleep(150);
            continue;
        }

        QByteArray resp;
        if (waitForResponse(0x1A, 1000, resp)) {
            if (resp.size() >= 3) {
                quint8 rc = static_cast<quint8>(resp.at(0));
                if (rc == 0x01) {
                    emit statusUpdated(5, QStringLiteral("升级被拒绝: 固件版本号过低或型号不符"), 0);
                    return;
                } else if (rc == 0x00) {
                    negotiateChunkSize = (static_cast<quint8>(resp.at(1)) << 8) | static_cast<quint8>(resp.at(2));
                    // 钳制单包大小，如非法则默认 256
                    if (negotiateChunkSize < 64 || negotiateChunkSize > 1024) {
                        negotiateChunkSize = 256;
                    }
                    handshakeSuccess = true;
                    break;
                }
            }
        }
        msleep(150);
    }

    if (!handshakeSuccess) {
        emit statusUpdated(5, QStringLiteral("握手失败: 从机无响应"), 0);
        return;
    }

    emit statusUpdated(2, QStringLiteral("开始下发固件数据..."), 1);

    int totalChunks = (totalBytes + negotiateChunkSize - 1) / negotiateChunkSize;

    for (int i = 0; i < totalChunks; ++i) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        quint32 offset = static_cast<quint32>(i) * negotiateChunkSize;
        quint32 remainingBytes = totalBytes - offset;
        quint32 currentChunkSize = (remainingBytes > negotiateChunkSize) ? negotiateChunkSize : remainingBytes;

        QByteArray chunk = fileData.mid(offset, currentChunkSize);

        bool chunkSent = false;
        quint32 expectedId = i + 1;

        for (int retry = 0; retry < 3; ++retry) {
            if (m_abortRequested.load()) {
                emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
                return;
            }

            if (!sendDataPacket(expectedId, chunk)) {
                m_lastError = QStringLiteral("串口写入失败");
                msleep(100);
                continue;
            }

            QByteArray resp;
            if (waitForResponse(0x1B, 1000, resp)) {
                if (resp.size() >= 4) {
                    quint32 returnedId = (static_cast<quint32>(static_cast<quint8>(resp.at(0))) << 24) |
                                         (static_cast<quint32>(static_cast<quint8>(resp.at(1))) << 16) |
                                         (static_cast<quint32>(static_cast<quint8>(resp.at(2))) << 8)  |
                                         static_cast<quint32>(static_cast<quint8>(resp.at(3)));
                    if (returnedId == expectedId) {
                        chunkSent = true;
                        break;
                    } else if (returnedId == 0) {
                        emit statusUpdated(5, QStringLiteral("升级失败: 读卡器在包 %1 校验出错").arg(expectedId), 0);
                        return;
                    }
                }
            }
            msleep(100);
        }

        if (!chunkSent) {
            emit statusUpdated(5, QStringLiteral("传输固件失败 (包 %1/%2): %3").arg(expectedId).arg(totalChunks).arg(m_lastError.isEmpty() ? QStringLiteral("超时") : m_lastError), 0);
            return;
        }

        int progress = (i + 1) * 98 / totalChunks;
        emit statusUpdated(2, QStringLiteral("已发送包 %1 / %2").arg(expectedId).arg(totalChunks), progress);
    }

    emit statusUpdated(3, QStringLiteral("数据发送完毕，校验固件中..."), 98);

    // 计算整个固件文件的 CRC16-CCITT 校验值
    quint16 finalCrc = calculateCrc16(reinterpret_cast<const quint8*>(fileData.constData()), fileData.size());
    QByteArray crcBytes;
    crcBytes.append(static_cast<char>(0));
    crcBytes.append(static_cast<char>(0));
    crcBytes.append(static_cast<char>((finalCrc >> 8) & 0xFF));
    crcBytes.append(static_cast<char>(finalCrc & 0xFF));

    bool verifySuccess = false;
    for (int retry = 0; retry < 3; ++retry) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        if (!sendEndPacket(crcBytes)) {
            m_lastError = QStringLiteral("串口写入失败");
            msleep(200);
            continue;
        }

        QByteArray resp;
        if (waitForResponse(0x1C, 2000, resp)) {
            if (resp.size() >= 1) {
                quint8 result = static_cast<quint8>(resp.at(0));
                if (result == 0) {
                    verifySuccess = true;
                    break;
                } else {
                    emit statusUpdated(5, QStringLiteral("升级校验失败: 设备验证固件不通过"), 0);
                    return;
                }
            }
        }
        msleep(200);
    }

    if (verifySuccess) {
        emit statusUpdated(6, QStringLiteral("升级成功! 设备已重启"), 100);
    } else {
        emit statusUpdated(5, QStringLiteral("结束校验失败: %1").arg(m_lastError.isEmpty() ? QStringLiteral("超时") : m_lastError), 0);
    }
}


BbFfOtaService::BbFfOtaService(Rs485Manager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_currentState(State::Idle)
    , m_currentProgress(0)
{
    m_worker = new BbFfOtaWorker(manager, this);
    connect(m_worker, &BbFfOtaWorker::statusUpdated, this, &BbFfOtaService::onWorkerStatusUpdated, Qt::QueuedConnection);
    connect(m_manager, &Rs485Manager::packetReceived, this, &BbFfOtaService::onPacketReceived);
}

BbFfOtaService::~BbFfOtaService()
{
    m_worker->requestAbort();
    m_worker->wait();
}

QString BbFfOtaService::stateText() const
{
    switch (m_currentState) {
    case State::Idle: return QStringLiteral("空闲");
    case State::StartUpgrade: return QStringLiteral("开始升级");
    case State::SendData: return QStringLiteral("传输数据");
    case State::FinishUpgrade: return QStringLiteral("校验跳转");
    case State::Abort: return QStringLiteral("被终止");
    case State::Failed: return QStringLiteral("升级失败");
    case State::Completed: return QStringLiteral("升级完成");
    }
    return QStringLiteral("未知");
}

void BbFfOtaService::startUpgrade(const QString &firmwarePath, const QString &versionStr)
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
    }
    setState(State::StartUpgrade, QStringLiteral("开始升级..."));
    m_currentProgress = 0;
    emit otaProgress(0);

    m_worker->setup(firmwarePath, versionStr);
    m_worker->start();
}

void BbFfOtaService::abortUpgrade()
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
        setState(State::Abort, QStringLiteral("升级已被用户手动终止"));
    }
}

void BbFfOtaService::onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent)
{
    State newState = static_cast<State>(stateVal);
    m_currentProgress = progressPercent;
    emit otaProgress(progressPercent);
    setState(newState, message);
}

void BbFfOtaService::onPacketReceived(quint8 cmdCode, const QByteArray &payload)
{
    if (m_worker->isRunning()) {
        m_worker->handleIncomingPacket(cmdCode, payload);
    }
}

void BbFfOtaService::setState(State state, const QString &message)
{
    if (m_currentState != state || m_currentMessage != message) {
        m_currentState = state;
        m_currentMessage = message;
        emit otaStateChanged(state, message);
    }
}
