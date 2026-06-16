#include "qingjuotaservice.h"
#include "domain/crc16.h"
#include <QDebug>
#include <QElapsedTimer>

QingjuOtaWorker::QingjuOtaWorker(QingjuCanManager *canManager, QObject *parent)
    : QThread(parent)
    , m_canManager(canManager)
    , m_abortRequested(false)
{
}

QingjuOtaWorker::~QingjuOtaWorker()
{
    requestAbort();
    wait();
}

void QingjuOtaWorker::setup(const QString &filePath, const QingjuOtaErrorConfig &injectCfg)
{
    m_firmwarePath = filePath;
    m_injectConfig = injectCfg;
    m_abortRequested.store(false);
    m_lastError.clear();
    
    QMutexLocker locker(&m_mutex);
    m_pendingPackets.clear();
}

void QingjuOtaWorker::requestAbort()
{
    m_abortRequested.store(true);
    QMutexLocker locker(&m_mutex);
    m_waitCondition.wakeAll();
}

void QingjuOtaWorker::handleIncomingModbusPacket(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload)
{
    // OTA 升级只处理来自 RFR 设备 (0x0B) 且发送给中控 (0x01) 的功能码 0x45 (固件升级) 报文
    if (srcAddr != 0x0B || destAddr != 0x01 || funcCode != 0x45) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    ModbusPacket p = { srcAddr, destAddr, funcCode, payload };
    m_pendingPackets.enqueue(p);
    m_waitCondition.wakeAll();
}

bool QingjuOtaWorker::waitForResponse(quint8 expectedSrc, quint8 expectedFunc, quint8 expectedKey, int timeoutMs, QByteArray &outPayload)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&m_mutex);
    while (true) {
        if (m_abortRequested.load()) {
            m_lastError = "用户终止升级";
            return false;
        }

        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            m_lastError = QString("等待响应超时 (KEY=0x%1)").arg(expectedKey, 2, 16, QChar('0'));
            return false;
        }

        if (m_pendingPackets.isEmpty()) {
            if (!m_waitCondition.wait(&m_mutex, remaining)) {
                m_lastError = QString("等待响应超时 (KEY=0x%1)").arg(expectedKey, 2, 16, QChar('0'));
                return false;
            }
        }

        while (!m_pendingPackets.isEmpty()) {
            ModbusPacket packet = m_pendingPackets.dequeue();
            if (packet.srcAddr == expectedSrc && packet.funcCode == expectedFunc && !packet.payload.isEmpty()) {
                quint8 key = static_cast<quint8>(packet.payload.at(0));
                if (key == expectedKey) {
                    outPayload = packet.payload;
                    return true;
                }
            }
        }
    }
}

void QingjuOtaWorker::run()
{
    emit statusUpdated(1, "开始加载固件文件...", 0);

    QFile file(m_firmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusUpdated(5, "无法打开固件文件", 0);
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        emit statusUpdated(5, "固件文件为空", 0);
        return;
    }

    quint32 fileSize = fileData.size();
    quint16 fileCrc = calculateModbusCrc16(reinterpret_cast<const quint8*>(fileData.constData()), fileSize);

    emit statusUpdated(1, "开始发送进入升级模式请求...", 2);

    // 1. 发送进入升级模式请求 (KEY = 0x01)
    bool enterOk = false;
    QByteArray respPayload;
    for (int retry = 0; retry < 3; ++retry) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, "升级已终止", 0);
            return;
        }
        
        m_canManager->sendModbusRequest(0x0B, 0x45, QByteArray::fromHex("01"), 7); // OTA 优先级为 7
        if (waitForResponse(0x0B, 0x45, 0x02, 1000, respPayload)) {
            enterOk = true;
            break;
        }
    }

    if (!enterOk) {
        emit statusUpdated(5, "进入升级模式失败: " + m_lastError, 0);
        return;
    }

    // 检查进入升级响应：KEY(0x02) + Status(0x00:成功) + Error(0x00:成功)
    if (respPayload.size() < 3 || static_cast<quint8>(respPayload.at(1)) != 0x00) {
        quint8 errCode = respPayload.size() >= 3 ? static_cast<quint8>(respPayload.at(2)) : 0xFF;
        QString errText = "未知";
        if (errCode == 0x01) errText = "电量过低";
        else if (errCode == 0x02) errText = "不支持固件升级";
        else if (errCode == 0x03) errText = "异常保护状态";
        emit statusUpdated(5, QString("进入升级模式被拒: %1 (错误码:0x%2)").arg(errText).arg(errCode, 2, 16, QChar('0')), 0);
        return;
    }

    // 异常 Case 4: 升级过程静默超时
    if (m_injectConfig.enabled && m_injectConfig.caseMode == 4) {
        emit statusUpdated(1, "[Case 4 注入] 升级静默开始，等待 6s...", 5);
        for (int i = 0; i < 6; ++i) {
            if (m_abortRequested.load()) {
                emit statusUpdated(4, "升级已终止", 0);
                return;
            }
            QThread::sleep(1);
        }
        emit statusUpdated(5, "[Case 4 注入] 模拟静默超时完成，升级终止", 5);
        return;
    }

    // 2. 发送固件基本信息 (KEY = 0x13)
    emit statusUpdated(1, "下发固件基本信息...", 5);

    QByteArray infoVal;
    // 厂家信息 (10字节)，垫空
    infoVal.append(QByteArray("DI DI").leftJustified(10, '\0'));
    // 硬件型号编号 (1字节)
    infoVal.append(static_cast<char>(0x01));
    // 客户编号 (1字节)
    infoVal.append(static_cast<char>(0x01));

    // 固件类型 (1字节)：低6位为固件类型 RFR = 0x0B
    quint8 firmwareType = 0x0B;
    if (m_injectConfig.enabled && m_injectConfig.caseMode == 1) {
        firmwareType = 0x99; // [Case 1 注入] 不匹配的固件类型
        emit statusUpdated(1, "[Case 1 注入] 发送不匹配的固件类型 0x99", 5);
    }
    infoVal.append(static_cast<char>(firmwareType));

    // 版本号 (3字节) V1.0.0
    infoVal.append(static_cast<char>(0x01));
    infoVal.append(static_cast<char>(0x00));
    infoVal.append(static_cast<char>(0x00));

    // 固件包大小 (4字节) 大端
    infoVal.append(static_cast<char>((fileSize >> 24) & 0xFF));
    infoVal.append(static_cast<char>((fileSize >> 16) & 0xFF));
    infoVal.append(static_cast<char>((fileSize >> 8) & 0xFF));
    infoVal.append(static_cast<char>(fileSize & 0xFF));

    // 固件包校验值 (2字节) 大端
    quint16 sendCrc = fileCrc;
    if (m_injectConfig.enabled && m_injectConfig.caseMode == 2) {
        sendCrc = fileCrc ^ 0xFFFF; // [Case 2 注入] 错误的 CRC 校验值
        emit statusUpdated(1, "[Case 2 注入] 发送错误的固件 CRC 校验", 5);
    }
    infoVal.append(static_cast<char>((sendCrc >> 8) & 0xFF));
    infoVal.append(static_cast<char>(sendCrc & 0xFF));

    QByteArray reqPayload;
    reqPayload.append(static_cast<char>(0x13));
    reqPayload.append(infoVal);

    m_canManager->sendModbusRequest(0x0B, 0x45, reqPayload, 7);

    if (!waitForResponse(0x0B, 0x45, 0x14, 2000, respPayload)) {
        emit statusUpdated(5, "固件信息回应超时: " + m_lastError, 5);
        return;
    }

    // 检查响应：KEY(0x14) + Status(1Byte) + BlockSize(1Byte) + ReqBlockNo(2Bytes)
    if (respPayload.size() < 5) {
        emit statusUpdated(5, "固件信息响应数据长度错误", 5);
        return;
    }

    quint8 otaStatus = static_cast<quint8>(respPayload.at(1));
    if (otaStatus == 0x00 || otaStatus == 0x02) {
        emit statusUpdated(6, "从机提示已是最新，无需升级", 100);
        return;
    }

    if (otaStatus != 0x06 && otaStatus != 0x01) {
        emit statusUpdated(5, QString("从机拒绝升级，状态码:0x%1").arg(otaStatus, 2, 16, QChar('0')), 5);
        return;
    }

    quint8 blockSizeCode = static_cast<quint8>(respPayload.at(2));
    int blockSize = 128;
    if (blockSizeCode == 0x01) blockSize = 64;
    else if (blockSizeCode == 0x02) blockSize = 128;
    else if (blockSizeCode == 0x03) blockSize = 240;
    else if (blockSizeCode == 0x04) blockSize = 192;

    quint16 nextBlock = (static_cast<quint8>(respPayload.at(3)) << 8) | static_cast<quint8>(respPayload.at(4));

    int totalBlocks = (fileSize + blockSize - 1) / blockSize;

    emit statusUpdated(1, QString("开始发送固件数据 (分块大小: %1B, 共 %2 块)...").arg(blockSize).arg(totalBlocks), 10);

    bool dupSent = false;

    // 3. 循环发送数据块 (KEY = 0x15)
    while (nextBlock < totalBlocks) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, "升级已终止", 0);
            return;
        }

        // 异常 Case 3: 传输中途静默 (模拟断电)
        if (m_injectConfig.enabled && m_injectConfig.caseMode == 3 && nextBlock >= 5) {
            emit statusUpdated(5, "[Case 3 注入] 模拟中途断电 (数据包 index >= 5 时终止发送)", (nextBlock * 90) / totalBlocks + 10);
            return;
        }

        int offset = nextBlock * blockSize;
        int len = qMin(blockSize, static_cast<int>(fileSize) - offset);
        QByteArray chunk = fileData.mid(offset, len);

        // 组装 0x15 数据包的值
        QByteArray blockVal;
        // 当前固件版本号 (3字节) V1.0.0
        blockVal.append(static_cast<char>(0x01));
        blockVal.append(static_cast<char>(0x00));
        blockVal.append(static_cast<char>(0x00));
        // 固件类型 (1字节) RFR = 0x0B
        blockVal.append(static_cast<char>(0x0B));
        // 数据块编号 (2字节) 大端
        blockVal.append(static_cast<char>((nextBlock >> 8) & 0xFF));
        blockVal.append(static_cast<char>(nextBlock & 0xFF));
        // 完整数据块数 (2字节) 大端
        blockVal.append(static_cast<char>((totalBlocks >> 8) & 0xFF));
        blockVal.append(static_cast<char>(totalBlocks & 0xFF));
        // 固件数据校验 (2字节) 大端
        quint16 chunkCrc = calculateModbusCrc16(reinterpret_cast<const quint8*>(chunk.constData()), chunk.size());
        blockVal.append(static_cast<char>((chunkCrc >> 8) & 0xFF));
        blockVal.append(static_cast<char>(chunkCrc & 0xFF));
        // 固件数据
        blockVal.append(chunk);

        QByteArray blockReq;
        blockReq.append(static_cast<char>(0x15));
        blockReq.append(blockVal);

        // 发送数据块
        m_canManager->sendModbusRequest(0x0B, 0x45, blockReq, 7);

        // 异常 Case 5: 收到 ECU 重复的数据包
        if (m_injectConfig.enabled && m_injectConfig.caseMode == 5 && nextBlock == 2 && !dupSent) {
            dupSent = true;
            // 稍等并直接重发一次 Block 2
            QThread::msleep(100);
            emit statusUpdated(1, "[Case 5 注入] 重发数据块 2 ...", (nextBlock * 90) / totalBlocks + 10);
            m_canManager->sendModbusRequest(0x0B, 0x45, blockReq, 7);
        }

        // 等待 0x16 响应 (接收结果)
        if (!waitForResponse(0x0B, 0x45, 0x16, 3000, respPayload)) {
            emit statusUpdated(5, QString("数据块 [%1] 响应超时: ").arg(nextBlock) + m_lastError, (nextBlock * 90) / totalBlocks + 10);
            return;
        }

        // 检查 0x16 响应：KEY(0x16) + Result(1Byte) + HwModel(1Byte) + ClientNo(1Byte) + ProtoType(1Byte) + ReqBlockNo(2Bytes)
        if (respPayload.size() < 7) {
            emit statusUpdated(5, "数据块接收响应长度错误", (nextBlock * 90) / totalBlocks + 10);
            return;
        }

        quint8 result = static_cast<quint8>(respPayload.at(1));
        if (result != 0 && result != 3) {
            QString errStr = "未知错误";
            if (result == 1) errStr = "校验失败";
            else if (result == 2) errStr = "烧写失败";
            else if (result == 4) errStr = "长度异常";
            else if (result == 5) errStr = "完整性异常";
            emit statusUpdated(5, QString("从机报告错误: %1 (代码: 0x%2)").arg(errStr).arg(result, 2, 16, QChar('0')), (nextBlock * 90) / totalBlocks + 10);
            return;
        }

        // 从机要求的下一个块号
        nextBlock = (static_cast<quint8>(respPayload.at(5)) << 8) | static_cast<quint8>(respPayload.at(6));

        int pct = (nextBlock * 80) / totalBlocks + 10; // 进度在 10% - 90% 之间
        emit statusUpdated(2, QString("正在传输固件数据: 块 %1/%2").arg(nextBlock).arg(totalBlocks), pct);
    }

    // 4. 等待校验与重置 (最终确认)
    emit statusUpdated(3, "数据发送完成，正在等待设备写入校验...", 92);
    QThread::sleep(2); // 延时 2s 等待擦写完成

    bool checkOk = false;
    for (int checkLoop = 0; checkLoop < 20; ++checkLoop) {
        if (m_abortRequested.load()) {
            emit statusUpdated(4, "升级已终止", 0);
            return;
        }

        // 再次下发 0x13 固件基本信息进行查询
        m_canManager->sendModbusRequest(0x0B, 0x45, reqPayload, 7);

        if (waitForResponse(0x0B, 0x45, 0x14, 1500, respPayload)) {
            if (respPayload.size() >= 2) {
                quint8 finalStatus = static_cast<quint8>(respPayload.at(1));
                if (finalStatus == 0x02) {
                    checkOk = true;
                    break;
                } else if (finalStatus == 0x03 || finalStatus == 0x07) {
                    emit statusUpdated(5, QString("固件写入校验失败，状态码:0x%1").arg(finalStatus, 2, 16, QChar('0')), 95);
                    return;
                }
            }
        }
        QThread::msleep(500);
    }

    if (checkOk) {
        emit statusUpdated(6, "升级成功完成！", 100);
    } else {
        emit statusUpdated(5, "等待最终升级确认超时", 95);
    }
}


// ------------------ QingjuOtaService ------------------

QingjuOtaService::QingjuOtaService(QingjuCanManager *canManager, QObject *parent)
    : QObject(parent)
    , m_canManager(canManager)
    , m_currentState(State::Idle)
    , m_currentProgress(0)
{
    m_worker = new QingjuOtaWorker(m_canManager, this);
    connect(m_worker, &QingjuOtaWorker::statusUpdated, this, &QingjuOtaService::onWorkerStatusUpdated);
}

QingjuOtaService::~QingjuOtaService()
{
}

QString QingjuOtaService::stateText() const
{
    switch (m_currentState) {
    case State::Idle: return "空闲";
    case State::StartUpgrade: return "启动升级";
    case State::SendData: return "发送数据";
    case State::FinishUpgrade: return "确认升级";
    case State::Abort: return "已终止";
    case State::Failed: return "升级失败";
    case State::Completed: return "升级成功";
    }
    return "未知";
}

void QingjuOtaService::startUpgrade(const QString &firmwarePath, const QingjuOtaErrorConfig &injectCfg)
{
    if (m_currentState != State::Idle && m_currentState != State::Completed && m_currentState != State::Failed && m_currentState != State::Abort) {
        return;
    }

    m_currentProgress = 0;
    m_currentMessage = "正在初始化...";
    setState(State::StartUpgrade, m_currentMessage);

    m_worker->setup(firmwarePath, injectCfg);
    m_worker->start();
}

void QingjuOtaService::abortUpgrade()
{
    if (m_worker->isRunning()) {
        m_worker->requestAbort();
        m_worker->wait();
    }
    setState(State::Abort, "升级已被用户终止");
}

void QingjuOtaService::handleIncomingModbusPacket(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload)
{
    if (m_worker && m_worker->isRunning()) {
        m_worker->handleIncomingModbusPacket(srcAddr, destAddr, funcCode, payload);
    }
}

void QingjuOtaService::onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent)
{
    m_currentProgress = progressPercent;
    m_currentMessage = message;

    State targetState = m_currentState;
    switch (stateVal) {
    case 1: targetState = State::StartUpgrade; break;
    case 2: targetState = State::SendData; break;
    case 3: targetState = State::FinishUpgrade; break;
    case 4: targetState = State::Abort; break;
    case 5: targetState = State::Failed; break;
    case 6: targetState = State::Completed; break;
    default: break;
    }

    setState(targetState, message);
    emit otaProgress(progressPercent);
}

void QingjuOtaService::setState(State state, const QString &message)
{
    if (m_currentState != state || m_currentMessage != message) {
        m_currentState = state;
        m_currentMessage = message;
        emit otaStateChanged(state, message);
    }
}
