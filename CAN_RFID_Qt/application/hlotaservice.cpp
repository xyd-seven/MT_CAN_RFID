#include "hlotaservice.h"
#include "domain/crc16.h"
#include <QFileInfo>
#include <QDebug>

HlOtaWorker::HlOtaWorker(Rs485Manager *manager, QObject *parent)
    : QThread(parent)
    , m_manager(manager)
    , m_queryOnly(false)
    , m_abortRequested(false)
{
}

HlOtaWorker::~HlOtaWorker()
{
    requestAbort();
    wait();
}

void HlOtaWorker::setup(const QString &filePath, bool queryOnly)
{
    m_firmwarePath = filePath;
    m_queryOnly = queryOnly;
    m_abortRequested = false;
    m_lastError.clear();

    QMutexLocker locker(&m_mutex);
    m_pendingPackets.clear();
}

void HlOtaWorker::requestAbort()
{
    m_abortRequested.store(true);
    m_waitCondition.wakeAll();
}

void HlOtaWorker::handleIncomingPacket(quint8 cmdCode, const QByteArray &payload)
{
    QMutexLocker locker(&m_mutex);
    Packet p;
    p.cmdCode = cmdCode;
    p.payload = payload;
    m_pendingPackets.enqueue(p);
    m_waitCondition.wakeAll();
}

bool HlOtaWorker::waitForResponse(quint8 expectedCmd, int timeoutMs, QByteArray &outPayload)
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
            // Check for Modbus exception response (cmdCode has bit 7 set)
            if (packet.cmdCode == (expectedCmd | 0x80)) {
                quint8 errCode = packet.payload.isEmpty() ? 0 : static_cast<quint8>(packet.payload.at(0));
                switch (errCode) {
                case 1: m_lastError = QStringLiteral("Modbus异常: 不支持的功能码 (01)"); break;
                case 2: m_lastError = QStringLiteral("Modbus异常: 非法的寄存器地址 (02)"); break;
                case 3: m_lastError = QStringLiteral("Modbus异常: 非法的寄存器值 (03)"); break;
                case 4: m_lastError = QStringLiteral("Modbus异常: 从机设备故障 (04)"); break;
                default: m_lastError = QStringLiteral("Modbus异常: 错误代码 %1").arg(errCode); break;
                }
                return false;
            }

            if (packet.cmdCode == expectedCmd) {
                outPayload = packet.payload;
                return true;
            }
        }
    }
}

bool HlOtaWorker::sendWritePacket(quint16 startReg, quint16 count, const QByteArray &regData)
{
    if (regData.size() > 252) {
        return false;
    }

    QByteArray pkt;
    pkt.reserve(9 + regData.size());

    pkt.append(static_cast<char>(0x0D)); // Device Address
    pkt.append(static_cast<char>(0x10)); // Function Code (Write Multiple)
    pkt.append(static_cast<char>((startReg >> 8) & 0xFF));
    pkt.append(static_cast<char>(startReg & 0xFF));
    pkt.append(static_cast<char>((count >> 8) & 0xFF));
    pkt.append(static_cast<char>(count & 0xFF));
    pkt.append(static_cast<char>(regData.size() & 0xFF));
    pkt.append(regData);

    quint16 crc = calculateModbusCrc16(reinterpret_cast<const quint8*>(pkt.constData()), pkt.size());
    pkt.append(static_cast<char>(crc & 0xFF));
    pkt.append(static_cast<char>((crc >> 8) & 0xFF));

    bool success = false;
    QMetaObject::invokeMethod(m_manager, "sendRawData", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(QByteArray, pkt));
    return success;
}

bool HlOtaWorker::sendReadPacket(quint16 startReg, quint16 count)
{
    QByteArray pkt;
    pkt.reserve(8);

    pkt.append(static_cast<char>(0x0D)); // Device Address
    pkt.append(static_cast<char>(0x03)); // Function Code (Read)
    pkt.append(static_cast<char>((startReg >> 8) & 0xFF));
    pkt.append(static_cast<char>(startReg & 0xFF));
    pkt.append(static_cast<char>((count >> 8) & 0xFF));
    pkt.append(static_cast<char>(count & 0xFF));

    quint16 crc = calculateModbusCrc16(reinterpret_cast<const quint8*>(pkt.constData()), pkt.size());
    pkt.append(static_cast<char>(crc & 0xFF));
    pkt.append(static_cast<char>((crc >> 8) & 0xFF));

    bool success = false;
    QMetaObject::invokeMethod(m_manager, "sendRawData", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(QByteArray, pkt));
    return success;
}

void HlOtaWorker::run()
{
    if (m_queryOnly) {
        emit statusUpdated(7, QStringLiteral("正在查询设备程序状态..."), 0);

        bool success = false;
        QByteArray resp;
        for (int retry = 0; retry < 3; ++retry) {
            if (m_abortRequested.load()) {
                emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
                return;
            }
            
            // Query registers 0 to 6
            if (!sendReadPacket(0, 7)) {
                m_lastError = QStringLiteral("串口写入失败");
                msleep(100);
                continue;
            }
            if (waitForResponse(0x03, 1000, resp)) {
                success = true;
                break;
            }
            msleep(100);
        }

        if (success && resp.size() == 14) {
            quint16 verType = (static_cast<quint8>(resp.at(8)) << 8) | static_cast<quint8>(resp.at(9));
            QString modeStr = (verType == 1) ? QStringLiteral("BOOT 模式 (等待升级)") : QStringLiteral("APP 模式 (正常工作)");
            emit statusUpdated(6, QStringLiteral("查询成功: 设备运行在 %1").arg(modeStr), 100);
        } else {
            emit statusUpdated(5, QStringLiteral("查询失败: %1").arg(m_lastError.isEmpty() ? QStringLiteral("无响应") : m_lastError), 0);
        }
        return;
    }

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

    int totalBytes = fileData.size();
    int totalChunks = (totalBytes + 127) / 128;

    emit statusUpdated(1, QStringLiteral("使能固件下载模式..."), 0);

    bool downloadEnabled = false;
    for (int retry = 0; retry < 3; ++retry) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        // Write 1 to register 300
        QByteArray regVal;
        regVal.append(static_cast<char>(0));
        regVal.append(static_cast<char>(1));

        if (!sendWritePacket(300, 1, regVal)) {
            m_lastError = QStringLiteral("串口写入失败");
            msleep(150);
            continue;
        }

        QByteArray resp;
        if (waitForResponse(0x10, 1000, resp)) {
            downloadEnabled = true;
            break;
        }
        msleep(150);
    }

    if (!downloadEnabled) {
        emit statusUpdated(5, QStringLiteral("使能版本下载失败: %1").arg(m_lastError.isEmpty() ? QStringLiteral("超时") : m_lastError), 0);
        return;
    }

    emit statusUpdated(2, QStringLiteral("开始下发固件数据..."), 1);

    for (int i = 0; i < totalChunks; ++i) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        quint32 offset = i * 128;
        QByteArray chunk;
        chunk.reserve(132);

        // offset (uint32_t big-endian)
        chunk.append(static_cast<char>((offset >> 24) & 0xFF));
        chunk.append(static_cast<char>((offset >> 16) & 0xFF));
        chunk.append(static_cast<char>((offset >> 8) & 0xFF));
        chunk.append(static_cast<char>(offset & 0xFF));

        // payload (128 bytes)
        QByteArray dataChunk = fileData.mid(offset, 128);
        if (dataChunk.size() < 128) {
            dataChunk.append(QByteArray(128 - dataChunk.size(), static_cast<char>(0))); // pad with 0
        }
        chunk.append(dataChunk);

        bool chunkSent = false;
        for (int retry = 0; retry < 3; ++retry) {
            if (m_abortRequested.load()) {
                emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
                return;
            }

            // Write 66 registers to register 302
            if (!sendWritePacket(302, 66, chunk)) {
                m_lastError = QStringLiteral("串口写入失败");
                msleep(100);
                continue;
            }

            QByteArray resp;
            if (waitForResponse(0x10, 1000, resp)) {
                chunkSent = true;
                break;
            }
            msleep(100);
        }

        if (!chunkSent) {
            emit statusUpdated(5, QStringLiteral("传输固件失败 (包 %1/%2): %3").arg(i + 1).arg(totalChunks).arg(m_lastError.isEmpty() ? QStringLiteral("超时") : m_lastError), 0);
            return;
        }

        int progress = (i + 1) * 98 / totalChunks;
        emit statusUpdated(2, QStringLiteral("已发送包 %1 / %2").arg(i + 1).arg(totalChunks), progress);
    }

    emit statusUpdated(3, QStringLiteral("数据发送完毕，校验固件并跳转中..."), 98);

    for (int retry = 0; retry < 3; ++retry) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, QStringLiteral("用户终止升级"), 0);
            return;
        }

        // Write 1 to register 301
        QByteArray regVal;
        regVal.append(static_cast<char>(0));
        regVal.append(static_cast<char>(1));

        if (!sendWritePacket(301, 1, regVal)) {
            m_lastError = QStringLiteral("串口写入失败");
            msleep(150);
            continue;
        }

        QByteArray resp;
        // The device might restart immediately and not respond, so a timeout here is common.
        if (waitForResponse(0x10, 1000, resp)) {
            break;
        }
        msleep(150);
    }

    // Treat either response or timeout as success for final command
    emit statusUpdated(6, QStringLiteral("升级成功! 设备已重启跳转"), 100);
}


HlOtaService::HlOtaService(Rs485Manager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_currentState(State::Idle)
    , m_currentProgress(0)
{
    m_worker = new HlOtaWorker(manager, this);
    connect(m_worker, &HlOtaWorker::statusUpdated, this, &HlOtaService::onWorkerStatusUpdated, Qt::QueuedConnection);
    connect(m_manager, &Rs485Manager::packetReceived, this, &HlOtaService::onPacketReceived);
}

HlOtaService::~HlOtaService()
{
    m_worker->requestAbort();
    m_worker->wait();
}

QString HlOtaService::stateText() const
{
    switch (m_currentState) {
    case State::Idle: return QStringLiteral("空闲");
    case State::StartUpgrade: return QStringLiteral("启动升级");
    case State::SendData: return QStringLiteral("传输数据");
    case State::FinishUpgrade: return QStringLiteral("校验跳转");
    case State::Abort: return QStringLiteral("被终止");
    case State::Failed: return QStringLiteral("升级失败");
    case State::Completed: return QStringLiteral("升级完成");
    case State::QueryProgram: return QStringLiteral("查询状态");
    }
    return QStringLiteral("未知");
}

void HlOtaService::startUpgrade(const QString &firmwarePath)
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
    }
    setState(State::StartUpgrade, QStringLiteral("开始升级..."));
    m_currentProgress = 0;
    emit otaProgress(0);

    m_worker->setup(firmwarePath, false);
    m_worker->start();
}

void HlOtaService::abortUpgrade()
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
        setState(State::Abort, QStringLiteral("升级已被用户手动终止"));
    }
}

void HlOtaService::queryProgramStatus()
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
    }
    setState(State::QueryProgram, QStringLiteral("正在查询设备程序状态..."));
    m_currentProgress = 0;
    emit otaProgress(0);

    m_worker->setup(QString(), true);
    m_worker->start();
}

void HlOtaService::onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent)
{
    State newState = static_cast<State>(stateVal);
    m_currentProgress = progressPercent;
    emit otaProgress(progressPercent);
    setState(newState, message);
}

void HlOtaService::onPacketReceived(quint8 cmdCode, const QByteArray &payload)
{
    if (m_worker->isRunning()) {
        m_worker->handleIncomingPacket(cmdCode, payload);
    }
}

void HlOtaService::setState(State state, const QString &message)
{
    if (m_currentState != state || m_currentMessage != message) {
        m_currentState = state;
        m_currentMessage = message;
        emit otaStateChanged(state, message);
    }
}
