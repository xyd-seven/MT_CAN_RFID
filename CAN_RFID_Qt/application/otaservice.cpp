#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "otaservice.h"
#include "canthread.h"
#include "domain/crc16.h"
#include <QDebug>

namespace {
constexpr int OtaMaxRetryCount = 3;
constexpr int OtaDefaultChunkSize = 128;
constexpr int OtaMaxChunkSize = 240;
constexpr int OtaA2ResponseTimeoutMs = 500;
constexpr int OtaA3ResponseTimeoutMs = 1000;
constexpr int OtaA1ResponseTimeoutMs = 3000;
constexpr int OtaA4ResponseTimeoutMs = 3000;
constexpr int OtaConsecutiveFrameIntervalMs = 1;
constexpr int OtaMaxWaitFrameCount = 3;
constexpr int OtaPendingFrameLimit = 200;

class WindowsTimerResolutionGuard {
public:
    WindowsTimerResolutionGuard() {
#ifdef Q_OS_WIN
        if (timeBeginPeriod(1) == TIMERR_NOERROR) {
            enabled = true;
        }
#endif
    }
    ~WindowsTimerResolutionGuard() {
#ifdef Q_OS_WIN
        if (enabled) {
            timeEndPeriod(1);
        }
#endif
    }
private:
    bool enabled = false;
};
}

OtaWorker::OtaWorker(QObject *parent) :
    QThread(parent),
    m_canthread(nullptr),
    vendorCode(0x02), // UHF
    hwVersion(0),
    swVersion(0),
    protocolVersion(0x02), // OTA protocol version
    abortRequested(false),
    queryOnlyMode(false)
{
}

OtaWorker::~OtaWorker()
{
    requestAbort();
    wait();
}

void OtaWorker::setup(const QString &filePath, const IsoTpConfig &cfg, quint8 vendor, quint16 hw, quint16 sw, quint8 proto, CANThread *canthread, const OtaErrorConfig &injectCfg, bool queryOnly)
{
    firmwarePath = filePath;
    config = cfg;
    vendorCode = vendor;
    hwVersion = hw;
    swVersion = sw;
    protocolVersion = proto;
    transport.setConfig(cfg);
    abortRequested.store(false);
    queryOnlyMode = queryOnly;
    lastError.clear();
    injectConfig = injectCfg;
    m_canthread = canthread;

    QMutexLocker locker(&mutex);
    m_pendingFrames.clear();
}

void OtaWorker::requestAbort()
{
    abortRequested.store(true);
    QMutexLocker locker(&mutex);
    waitCondition.wakeAll();
}

void OtaWorker::handleIncomingFrame(const CanFrame &frame)
{
    if (frame.id != config.responseId || frame.channel != config.channel) {
        return;
    }
    QMutexLocker locker(&mutex);
    if (m_pendingFrames.size() >= OtaPendingFrameLimit) {
        m_pendingFrames.dequeue();
    }
    m_pendingFrames.enqueue(frame);
    waitCondition.wakeAll();
}

void OtaWorker::updateStatus(int stateVal, const QString &msg, int progressPercent)
{
    emit statusUpdated(stateVal, msg, progressPercent);
}

bool OtaWorker::waitForFrame(quint32 expectedId, quint8 firstByteMask, quint8 expectedFirstByte, int timeoutMs, CanFrame &matchedFrame)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&mutex);
    while (true) {
        if (abortRequested.load()) {
            return false;
        }

        while (!m_pendingFrames.isEmpty()) {
            CanFrame frame = m_pendingFrames.dequeue();
            if (frame.id == expectedId && frame.channel == config.channel && !frame.data.isEmpty()) {
                quint8 byte0 = static_cast<quint8>(frame.data[0]);
                if ((byte0 & firstByteMask) == expectedFirstByte) {
                    matchedFrame = frame;
                    return true;
                }
            }
        }

        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            return false;
        }

        if (!waitCondition.wait(&mutex, remaining)) {
            return false; // Timeout
        }
    }
}

bool OtaWorker::waitForResponse(quint8 expectedSid, int timeoutMs, QByteArray &payload)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&mutex);
    while (true) {
        if (abortRequested.load()) {
            lastError = "Abort requested";
            return false;
        }

        while (!m_pendingFrames.isEmpty()) {
            CanFrame frame = m_pendingFrames.dequeue();
            if (frame.id == config.responseId && frame.channel == config.channel && frame.data.size() >= 2) {
                quint8 byte0 = static_cast<quint8>(frame.data[0]);
                quint8 byte1 = static_cast<quint8>(frame.data[1]);

                // ISO-TP 单帧 (SF) 校验：高 4 位为 0
                if ((byte0 & 0xF0) == 0x00) {
                    if (byte1 == expectedSid) {
                        int len = byte0;
                        if (len > 0 && len <= 7 && frame.data.size() >= len + 1) {
                            payload = frame.data.mid(1, len);
                            return true;
                        }
                    }
                    // 负响应校验 (0x7F)
                    else if (byte1 == 0x7F && frame.data.size() >= 4) {
                        quint8 originalSid = static_cast<quint8>(frame.data[2]);
                        quint8 nrc = static_cast<quint8>(frame.data[3]);
                        if (originalSid == (expectedSid - 0x40)) { // 比如正响应为 0xE1 (A1+0x40)
                            lastError = QString("Negative response: NRC=0x%1").arg(nrc, 2, 16, QChar('0'));
                            return false;
                        }
                    }
                }
            }
        }

        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            lastError = "Response wait timeout";
            return false;
        }

        if (!waitCondition.wait(&mutex, remaining)) {
            lastError = "Wait condition timeout";
            return false;
        }
    }
}

bool OtaWorker::waitForFlowControlWithWait(int timeoutMs, IsoTpFlowControl &fc)
{
    {
        QMutexLocker locker(&mutex);
        clearPendingFramesLocked(); // Clear queue before waiting!
    }

    int waitCount = 0;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        if (abortRequested.load()) {
            lastError = "Abort requested during Flow Control";
            return false;
        }

        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            lastError = "Flow Control timeout";
            return false;
        }

        CanFrame frame;
        // Wait for Flow Control frame: expected ID = responseId, first byte mask = 0xF0, expected first byte = 0x30 (Flow Control)
        if (!waitForFrame(config.responseId, 0xF0, 0x30, remaining, frame)) {
            lastError = "Flow Control wait failed";
            return false;
        }

        if (!IsoTpTransport::parseFlowControl(frame.data, fc)) {
            lastError = "Parse Flow Control failed";
            return false;
        }

        if (fc.flowStatus == 1) { // WAIT
            waitCount++;
            if (waitCount > OtaMaxWaitFrameCount) {
                lastError = "Too many WAIT flow control frames";
                return false;
            }
            // Restart timer and wait again
            timer.restart();
            continue;
        }
        break;
    }
    return true;
}

bool OtaWorker::sendSingleFrame(quint8 sid, const QByteArray &params)
{
    QByteArray payload;
    payload.append(static_cast<char>(sid));
    payload.append(params);

    if (payload.size() > 7) {
        return false;
    }

    QVector<CanFrame> frames;
    if (transport.buildRequestFrames(payload, &frames) != IsoTpTransport::Result::Ok || frames.isEmpty()) {
        return false;
    }

    {
        QMutexLocker locker(&mutex);
        clearPendingFramesLocked(); // Clear queue before sending request!
    }

    if (m_canthread != nullptr && m_canthread->isRunning()) {
        m_canthread->sendClassicData(frames[0].id, config.channel, frames[0].data);
    } else {
        lastError = "CAN device is not ready or thread dead";
        return false;
    }

    emit transmitFrame(frames[0].id, frames[0].data);
    return true;
}

bool OtaWorker::sendMultiFrame(const QByteArray &payload, int timeoutMs)
{
    if (payload.isEmpty()) {
        return false;
    }

    int totalSize = payload.size();
    QByteArray ffPayload = payload.left(6);
    CanFrame ffFrame = transport.buildFirstFrame(ffPayload, totalSize);

    // 发送首帧 (FF)
    {
        QMutexLocker locker(&mutex);
        clearPendingFramesLocked(); // Clear queue before sending FF!
    }
    
    if (m_canthread != nullptr && m_canthread->isRunning()) {
        m_canthread->sendClassicData(ffFrame.id, config.channel, ffFrame.data);
    } else {
        lastError = "CAN device is not ready or thread dead";
        return false;
    }
    emit transmitFrame(ffFrame.id, ffFrame.data);

    // 等待流控帧 (FC)
    IsoTpFlowControl fc;
    if (!waitForFlowControlWithWait(timeoutMs, fc)) {
        return false;
    }

    if (fc.flowStatus == 2) { // OVERFLOW
        lastError = "Flow status OVERFLOW";
        return false;
    } else if (fc.flowStatus != 0) { // CTS
        lastError = QString("Flow status error: %1").arg(fc.flowStatus);
        return false;
    }

    // 发送连续帧 (CF)
    int offset = 6;
    quint8 seq = 1;
    int bsCount = 0;

    while (offset < totalSize) {
        if (abortRequested.load()) {
            lastError = "Abort requested during CF transmission";
            return false;
        }

        // 帧间隔控制 (STmin)
        int stMin = fc.stMin;
        qint64 targetNsecs = 0;
        if (stMin == 0) {
            targetNsecs = static_cast<qint64>(config.consecutiveFrameIntervalMs) * 1000000;
        } else if (stMin <= 0x7F) {
            targetNsecs = static_cast<qint64>(stMin) * 1000000;
        } else if (stMin >= 0xF1 && stMin <= 0xF9) {
            targetNsecs = static_cast<qint64>(stMin - 0xF0) * 100000;
        } else {
            targetNsecs = 1000000; // Default 1ms
        }

        if (injectConfig.enabled && injectConfig.ignoreFcInterval) {
            targetNsecs = 0;
        }

        if (targetNsecs > 0 && !abortRequested.load()) {
            if (targetNsecs >= 1000000) {
                QThread::msleep(static_cast<unsigned long>(targetNsecs / 1000000));
            } else {
                QThread::usleep(static_cast<unsigned long>((targetNsecs + 999) / 1000));
            }
        }

        int len = qMin(7, totalSize - offset);
        QByteArray cfPayload = payload.mid(offset, len);
        CanFrame cfFrame = transport.buildConsecutiveFrame(cfPayload, seq);

        if (injectConfig.enabled && injectConfig.isoTpSnError && seq == 3) {
            if (!cfFrame.data.isEmpty()) {
                // 故意将序号为 3 的连续帧的 SN 字节篡改为 5 (跳包)
                cfFrame.data[0] = static_cast<char>((cfFrame.data[0] & 0xF0) | 0x05);
            }
        }

        offset += len;
        seq = (seq + 1) & 0x0F;

        // 发送连续帧
        if (m_canthread != nullptr && m_canthread->isRunning()) {
            m_canthread->sendClassicData(cfFrame.id, config.channel, cfFrame.data);
        } else {
            lastError = "CAN device is not ready or thread dead during CF";
            return false;
        }
        emit transmitFrame(cfFrame.id, cfFrame.data);

        bsCount++;
        if (fc.blockSize > 0 && bsCount >= fc.blockSize && offset < totalSize) {
            bsCount = 0;
            // 每次等待流控帧前，清除之前累积的流控帧/数据帧
            if (!waitForFlowControlWithWait(timeoutMs, fc)) {
                return false;
            }
            if (fc.flowStatus == 2) { // OVERFLOW
                lastError = "Flow status OVERFLOW during CF";
                return false;
            } else if (fc.flowStatus != 0) {
                lastError = QString("Flow status error during CF: %1").arg(fc.flowStatus);
                return false;
            }
        }
    }

    return true;
}

void OtaWorker::run()
{
    WindowsTimerResolutionGuard timerGuard;

    if (queryOnlyMode) {
        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("正在查询程序位置..."), 0);
        for (int retry = 0; retry < OtaMaxRetryCount; ++retry) {
            if (sendSingleFrame(0xA4)) {
                QByteArray responsePayload;
                if (waitForResponse(0xE4, OtaA4ResponseTimeoutMs, responsePayload) && responsePayload.size() >= 2) {
                    const quint8 location = static_cast<quint8>(responsePayload[1]);
                    const QString locationText = (location == 0x00) ? QStringLiteral("BOOT") : QStringLiteral("APP");
                    updateStatus(static_cast<int>(OtaService::State::Completed),
                                 QStringLiteral("程序位置：%1").arg(locationText), 100);
                    return;
                }
            }
        }
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("查询程序位置失败"), 0);
        return;
    }

    // --- 提前读取固件文件 (以支持越权直接发包测试) ---
    QFile file(firmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("无法打开固件文件"), 5);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("固件文件为空"), 5);
        return;
    }
    quint32 fileSize = fileData.size();
    int chunkSize = OtaDefaultChunkSize; // 默认分包字节数，作为函数作用域变量以避免 goto 交叉初始化
    // 越权直接发包测试注入
    if (injectConfig.enabled && injectConfig.outOfOrderState) {
        updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("[注入] 越权发包：跳过握手直接下发固件包..."), 15);
        chunkSize = 240;
        goto send_data_phase;
    }

    { // 握手阶段局部作用域，防止 goto 交叉初始化本地变量
        // --- Phase 1: Query (查询) ---
        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("正在查询程序位置..."), 0);
        bool queryOk = false;
        for (int retry = 0; retry < OtaMaxRetryCount; ++retry) {
            if (sendSingleFrame(0xA4)) {
                QByteArray e4Resp;
                if (waitForResponse(0xE4, OtaA4ResponseTimeoutMs, e4Resp)) {
                    if (e4Resp.size() >= 2) {
                        quint8 location = static_cast<quint8>(e4Resp[1]);
                        QString locText = (location == 0x00) ? QStringLiteral("BOOT") : QStringLiteral("APP");
                        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("程序位置：%1").arg(locText), 5);
                        queryOk = true;
                        break;
                    }
                }
            }
        }
        if (!queryOk) {
            // 查询失败通常非致命，仅打印日志并继续
            qWarning() << "Query program location timeout or failed: " << lastError;
            updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("查询程序位置失败，继续尝试升级"), 5);
        }

        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 5);
            return;
        }

        // --- Phase 2: Start Upgrade (开始) ---
        updateStatus(static_cast<int>(OtaService::State::StartUpgrade), QStringLiteral("正在启动固件升级，包大小：%1 字节...").arg(fileSize), 10);
        
        QByteArray a1Payload;
        a1Payload.append(static_cast<char>(0xA1));
        a1Payload.append(static_cast<char>(protocolVersion));
        quint8 sendVendorCode = vendorCode;
        if (injectConfig.enabled && injectConfig.vendorMismatch) {
            sendVendorCode = vendorCode == 0xFF ? 0x00 : static_cast<quint8>(vendorCode ^ 0xFF);
            updateStatus(static_cast<int>(OtaService::State::StartUpgrade),
                         QStringLiteral("[注入] A1 厂商代码不匹配：0x%1 -> 0x%2")
                             .arg(vendorCode, 2, 16, QChar('0'))
                             .arg(sendVendorCode, 2, 16, QChar('0'))
                             .toUpper(),
                         10);
        }
        a1Payload.append(static_cast<char>(sendVendorCode));
        if (injectConfig.enabled && injectConfig.hwMismatch) {
            // 故意发送错误的硬件版本，模拟硬件版本不匹配
            a1Payload.append(static_cast<char>(0xFF));
            a1Payload.append(static_cast<char>(0xFF));
        } else {
            a1Payload.append(static_cast<char>((hwVersion >> 8) & 0xFF));
            a1Payload.append(static_cast<char>(hwVersion & 0xFF));
        }
        a1Payload.append(static_cast<char>((swVersion >> 8) & 0xFF));
        a1Payload.append(static_cast<char>(swVersion & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 24) & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 16) & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 8) & 0xFF));
        a1Payload.append(static_cast<char>(fileSize & 0xFF));

        bool startOk = false;
        int negotiatedChunkSize = OtaDefaultChunkSize; // 默认分包字节数为128
        for (int retry = 0; retry < OtaMaxRetryCount; ++retry) {
            if (sendMultiFrame(a1Payload, OtaA1ResponseTimeoutMs)) {
                QByteArray e1Resp;
                if (waitForResponse(0xE1, OtaA1ResponseTimeoutMs, e1Resp)) {
                    if (e1Resp.size() >= 2 && static_cast<quint8>(e1Resp[1]) == 0x00) {
                        startOk = true;
                        if (e1Resp.size() >= 4) {
                            quint16 parsedSize = (static_cast<quint8>(e1Resp[2]) << 8) | static_cast<quint8>(e1Resp[3]);
                            if (parsedSize >= 64 && parsedSize <= OtaMaxChunkSize) {
                                negotiatedChunkSize = parsedSize;
                            } else {
                                qWarning() << "OTA start response: invalid negotiated packet size" << parsedSize << ", fallback to default" << OtaDefaultChunkSize;
                                negotiatedChunkSize = OtaDefaultChunkSize;
                            }
                        }
                        QString sysStatusText = QStringLiteral("Unknown");
                        if (e1Resp.size() >= 7) {
                            quint8 sysStatus = static_cast<quint8>(e1Resp[6]);
                            if (sysStatus == 0x01) {
                                sysStatusText = QStringLiteral("APP");
                            } else if (sysStatus == 0x02) {
                                sysStatusText = QStringLiteral("Bootloader");
                            }
                        }
                        qDebug() << "OTA start accepted. Negotiated chunk size:" << negotiatedChunkSize << "Device status:" << sysStatusText;
                        break;
                    } else if (e1Resp.size() >= 2) {
                        lastError = QString("Refused by device: 0x%1").arg(static_cast<quint8>(e1Resp[1]), 2, 16, QChar('0'));
                    }
                }
            }
            if (abortRequested.load()) {
                updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 10);
                return;
            }
        }

        if (!startOk) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("升级启动请求失败: ") + lastError, 10);
            return;
        }
        chunkSize = negotiatedChunkSize; // 导出协商包大小到外部变量
    } // 结束作用域

    // --- Phase 3: Send Data (发包阶段) ---
send_data_phase:
    updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("开始下发固件包..."), 15);
    int totalChunks = (fileData.size() + chunkSize - 1) / chunkSize;
    bool firstA2DataErrorInjected = false;

    for (int i = 0; i < totalChunks; ++i) {
        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 15);
            sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 中止升级命令
            return;
        }

        if (injectConfig.enabled && injectConfig.silentTimeout && i >= totalChunks / 2) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("[注入] 中途静默超时已触发"), 50);
            return;
        }

        int offset = i * chunkSize;
        int len = qMin(chunkSize, fileData.size() - offset);
        QByteArray chunkData = fileData.mid(offset, len);
        quint16 chunkId = i + 1;

        if (injectConfig.enabled &&
            injectConfig.a2FirstFrameDataError &&
            !firstA2DataErrorInjected &&
            i == 0) {
            if (!chunkData.isEmpty()) {
                chunkData[0] = static_cast<char>(static_cast<quint8>(chunkData[0]) ^ 0xFF);
                firstA2DataErrorInjected = true;
                updateStatus(static_cast<int>(OtaService::State::SendData),
                             QStringLiteral("[注入] A2 首帧数据错误：已篡改第 1 个 A2 包的数据内容，包号保持 0x0001。"),
                             15);
            } else {
                updateStatus(static_cast<int>(OtaService::State::SendData),
                             QStringLiteral("[注入] A2 首帧数据错误未执行：首包数据为空。"),
                             15);
            }
        }

        if (injectConfig.enabled && injectConfig.seqError && chunkId == 3) {
            // 故意篡改第 3 包的包号为 99，制造包号不连续
            chunkId = 99;
        }

        QByteArray a2Payload;
        a2Payload.append(static_cast<char>(0xA2));
        a2Payload.append(static_cast<char>((chunkId >> 8) & 0xFF));
        a2Payload.append(static_cast<char>(chunkId & 0xFF));
        a2Payload.append(chunkData);

        bool chunkOk = false;
        for (int retry = 0; retry < OtaMaxRetryCount; ++retry) {
            if (sendMultiFrame(a2Payload, OtaA2ResponseTimeoutMs)) {
                QByteArray e2Resp;
                if (waitForResponse(0xE2, OtaA2ResponseTimeoutMs, e2Resp)) {
                    if (e2Resp.size() >= 4) {
                        quint16 respChunkId = (static_cast<quint8>(e2Resp[1]) << 8) | static_cast<quint8>(e2Resp[2]);
                        quint8 writeStatus = static_cast<quint8>(e2Resp[3]);
                        if (respChunkId == chunkId && (writeStatus == 0x00 || writeStatus == 0x02)) {
                            chunkOk = true;
                            break;
                        } else {
                            lastError = QString("Chunk ID mismatch or failure: chunkId=%1 state=0x%2").arg(respChunkId).arg(writeStatus, 2, 16, QChar('0'));
                        }
                    }
                }
            }
            if (abortRequested.load()) {
                updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 15);
                sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 中止升级命令
                return;
            }
        }

        if (!chunkOk) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("发送数据包失败，包号 %1，错误: ").arg(chunkId) + lastError);
            sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 中止升级命令
            return;
        }

        int progressPercent = 15 + ((i + 1) * 80 / totalChunks); // 15% 到 95%
        updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("已写入包 %1/%2").arg(chunkId).arg(totalChunks), progressPercent);
    }

    // --- Phase 4: Finish & Verify (结束校验) ---
    updateStatus(static_cast<int>(OtaService::State::FinishUpgrade), QStringLiteral("固件下发完成，正在计算校验码..."), 95);
    quint16 finalCrc = calculateCrc16(reinterpret_cast<const quint8*>(fileData.constData()), fileData.size());
    
    if (injectConfig.enabled && injectConfig.crcError) {
        // 校验码取反，制造 CRC16 校验错误
        finalCrc ^= 0xFFFF;
    }

    QByteArray a3Params;
    a3Params.append(static_cast<char>(0x01)); // 执行升级
    a3Params.append(static_cast<char>(finalCrc & 0xFF)); // 小端模式 CRC
    a3Params.append(static_cast<char>((finalCrc >> 8) & 0xFF));

    bool finishOk = false;
    for (int retry = 0; retry < OtaMaxRetryCount; ++retry) {
        if (sendSingleFrame(0xA3, a3Params)) {
            QByteArray e3Resp;
            if (waitForResponse(0xE3, OtaA3ResponseTimeoutMs, e3Resp)) {
                if (e3Resp.size() >= 2) {
                    quint8 status = static_cast<quint8>(e3Resp[1]);
                    if (status == 0x00) {
                        finishOk = true;
                        break;
                    } else if (status == 0x01) {
                        lastError = QStringLiteral("固件校验错误 (CRC16 校验失败)");
                        break; // 校验失败重试无用
                    } else if (status == 0x02) {
                        lastError = QStringLiteral("设备端升级已中止");
                        break;
                    }
                }
            }
        }
        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 95);
            return;
        }
    }

    if (finishOk) {
        updateStatus(static_cast<int>(OtaService::State::Completed), QStringLiteral("固件校验成功，升级完成，设备重启中..."), 100);
    } else {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("升级校验请求失败: ") + lastError, 95);
    }
}


// ==========================================
// OtaService Implementation
// ==========================================

OtaService::OtaService(QObject *parent) :
    QObject(parent),
    currentState(State::Idle),
    currentProgress(0),
    vendorCode(0x02), // 默认超高频威科姆 (0x02)
    hwVersion(0),
    swVersion(0),
    m_canthread(nullptr)
{
    worker = new OtaWorker(this);
    connect(worker, &OtaWorker::transmitFrame, this, &OtaService::transmitFrame);
    connect(worker, &OtaWorker::statusUpdated, this, &OtaService::onWorkerStatusUpdated);
}

void OtaService::setCanThread(CANThread *canthread)
{
    if (m_recvedFramesConn) {
        QObject::disconnect(m_recvedFramesConn);
    }
    m_canthread = canthread;
    if (m_canthread != nullptr) {
        m_recvedFramesConn = connect(m_canthread, &CANThread::recvedFrames, this, [this](const QVector<CanFrame> &frames) {
            if (worker != nullptr && worker->isRunning()) {
                for (const CanFrame &frame : frames) {
                    if (frame.id == config.responseId && frame.channel == config.channel) {
                        worker->handleIncomingFrame(frame);
                    }
                }
            }
        }, Qt::DirectConnection); // 使用 DirectConnection 并绑定 this 生命期守护
    }
}

OtaService::~OtaService()
{
    if (m_recvedFramesConn) {
        QObject::disconnect(m_recvedFramesConn);
    }
    if (worker->isRunning()) {
        worker->requestAbort();
        worker->wait();
    }
}

OtaService::State OtaService::state() const
{
    return currentState;
}

QString OtaService::stateText() const
{
    switch (currentState) {
    case State::Idle:
        return QStringLiteral("空闲");
    case State::QueryProgram:
        return QStringLiteral("查询程序位置");
    case State::StartUpgrade:
        return QStringLiteral("开始升级");
    case State::SendData:
        return QStringLiteral("发送数据");
    case State::FinishUpgrade:
        return QStringLiteral("升级校验");
    case State::Abort:
        return QStringLiteral("已中止");
    case State::Failed:
        return QStringLiteral("失败");
    case State::Completed:
        return QStringLiteral("完成");
    default:
        return QStringLiteral("未知");
    }
}

QString OtaService::lastMessage() const
{
    return currentMessage;
}

int OtaService::progress() const
{
    return currentProgress;
}

void OtaService::setChannel(quint32 channel)
{
    config.channel = channel;
}

void OtaService::setDeviceVersions(quint8 vendor, quint16 hw, quint16 sw)
{
    vendorCode = vendor;
    hwVersion = hw;
    swVersion = sw;
}

void OtaService::queryProgramLocation()
{
    if (worker->isRunning()) {
        return;
    }
    setState(State::QueryProgram, QStringLiteral("准备查询程序位置..."));
    currentProgress = 0;
    worker->setup(QString(), config, vendorCode, hwVersion, swVersion, 0x02, m_canthread, OtaErrorConfig(), true);
    worker->start();
}

void OtaService::startUpgrade(const QString &firmwarePath, const OtaErrorConfig &injectCfg)
{
    if (worker->isRunning()) {
        return;
    }
    if (firmwarePath.isEmpty() || firmwarePath == "-") {
        setState(State::Failed, QStringLiteral("请先选择正确的固件文件！"));
        return;
    }
    setState(State::StartUpgrade, QStringLiteral("准备启动升级流程..."));
    currentProgress = 0;
    worker->setup(firmwarePath, config, vendorCode, hwVersion, swVersion, 0x02, m_canthread, injectCfg);
    worker->start();
}

void OtaService::abortUpgrade()
{
    if (worker->isRunning()) {
        worker->requestAbort();
        setState(State::Abort, QStringLiteral("正在中止升级..."));
    } else {
        setState(State::Abort, QStringLiteral("升级未运行"));
    }
}

void OtaService::handleIncomingFrame(const CanFrame &frame)
{
    if (m_canthread != nullptr) {
        // Handled by direct connection in CANThread context, skip to avoid duplicates.
        return;
    }
    if (worker->isRunning()) {
        worker->handleIncomingFrame(frame);
    }
}

void OtaWorker::clearPendingFramesLocked()
{
    m_pendingFrames.clear();
}

void OtaService::onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent)
{
    State newState = static_cast<State>(stateVal);
    if (progressPercent >= 0) {
        currentProgress = progressPercent;
        emit otaProgress(currentProgress);
    }
    setState(newState, message);
}

void OtaService::setState(State state, const QString &message)
{
    currentState = state;
    currentMessage = message;
    emit otaStateChanged(currentState, message);
}
